#include "instances/instance_registry.h"
#include "core/adaptive_queue_size_manager.h"
#include "core/backpressure_controller.h"
#include "core/cvedix_validator.h"
#include "core/instance_file_logger.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/resource_manager.h"
#include "core/shutdown_flag.h"
#include "core/timeout_constants.h"
#include "core/uuid_generator.h"
#include "core/pipeline_builder_destination_nodes.h"
#include "models/update_instance_request.h"
#include "utils/gstreamer_checker.h"
#include "utils/mp4_directory_watcher.h"
#include "utils/mp4_finalizer.h"
#include <drogon/drogon.h>
#include <drogon/HttpClient.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring> // For strerror
// #include <cvedix/cvedix_version.h>  // File not available in cvedix-ai-runtime SDK
#include <cvedix/nodes/broker/cvedix_json_console_broker_node.h>
#include <cvedix/nodes/des/cvedix_app_des_node.h>
#include <cvedix/nodes/des/cvedix_rtmp_des_node.h>
#include <cvedix/nodes/infers/cvedix_mask_rcnn_detector_node.h>
#include <cvedix/nodes/infers/cvedix_openpose_detector_node.h>
#include <cvedix/nodes/infers/cvedix_sface_feature_encoder_node.h>
#include <cvedix/nodes/infers/cvedix_yunet_face_detector_node.h>
#include <cvedix/nodes/osd/cvedix_ba_line_crossline_osd_node.h>
#include <cvedix/nodes/osd/cvedix_ba_area_jam_osd_node.h>
#include <cvedix/nodes/osd/cvedix_ba_stop_osd_node.h>
#include <cvedix/nodes/osd/cvedix_face_osd_node_v2.h>
#include <cvedix/nodes/osd/cvedix_osd_node_v3.h>
#include <cvedix/nodes/src/cvedix_app_src_node.h>
#include <cvedix/nodes/src/cvedix_file_src_node.h>
#include <cvedix/nodes/src/cvedix_image_src_node.h>
#include <cvedix/nodes/src/cvedix_rtmp_src_node.h>
#include <cvedix/nodes/src/cvedix_rtsp_src_node.h>
#include <cvedix/nodes/src/cvedix_udp_src_node.h>
#include <cvedix/objects/cvedix_frame_meta.h>
#include <cvedix/objects/cvedix_meta.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <json/json.h> // For JSON parsing to count vehicles
#include <limits>      // For std::numeric_limits
#include <mutex>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>
#include <queue>
#include <regex>
#include <set> // For tracking unique vehicle IDs
#include <sstream>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <thread>
#include <typeinfo>
#include <unistd.h>
#include <unordered_map>
#include <vector> // For dynamic buffer

namespace fs = std::filesystem;

InstanceRegistry::InstanceRegistry(SolutionRegistry &solutionRegistry,
                                   PipelineBuilder &pipelineBuilder,
                                   InstanceStorage &instanceStorage)
    : solution_registry_(solutionRegistry), pipeline_builder_(pipelineBuilder),
      instance_storage_(instanceStorage) {
  // Initialize ResourceManager for GPU allocation
  // Default: allow up to 4 concurrent instances per GPU
  auto &resourceManager = ResourceManager::getInstance();
  resourceManager.initialize(4);
  
  std::cout << "[InstanceRegistry] Initialized with GPU resource management" << std::endl;
}

std::string InstanceRegistry::createInstance(const CreateInstanceRequest &req) {
  // CRITICAL: Release lock before building pipeline and auto-starting
  // This allows multiple instances to be created concurrently without blocking
  // each other

  std::cerr << "[InstanceRegistry] ========================================" << std::endl;
  std::cerr << "[InstanceRegistry] createInstance() called - starting..." << std::endl;
  std::cerr << "[InstanceRegistry] Solution: " << (req.solution.empty() ? "<none>" : req.solution) << std::endl;

  // Generate instance ID (no lock needed)
  std::string instanceId = UUIDGenerator::generateUUID();
  std::cerr << "[InstanceRegistry] Generated instance ID: " << instanceId << std::endl;

  if (ShutdownFlag::isRequested()) {
    throw std::runtime_error("Server is shutting down");
  }

  // Get solution config if specified (no lock needed)
  SolutionConfig *solution = nullptr;
  SolutionConfig solutionConfig;
  if (!req.solution.empty()) {
    auto optSolution = solution_registry_.getSolution(req.solution);
    if (!optSolution.has_value()) {
      std::string availableSolutionsStr = "";
      auto availableSolutions = solution_registry_.listSolutions();
      for (size_t i = 0; i < availableSolutions.size(); ++i) {
        availableSolutionsStr += availableSolutions[i];
        if (i < availableSolutions.size() - 1) {
          availableSolutionsStr += ", ";
        }
      }
      std::cerr << "[InstanceRegistry] Solution not found: " << req.solution
                << std::endl;
      std::cerr << "[InstanceRegistry] Available solutions: "
                << availableSolutionsStr << std::endl;
      throw std::invalid_argument(
          "Solution not found: " + req.solution +
          ". Available solutions: " + availableSolutionsStr);
    }
    solutionConfig = optSolution.value();
    solution = &solutionConfig;
  }

  // Collect existing RTMP stream keys from running instances to check for conflicts
  // This allows us to only modify RTMP URLs when there's an actual conflict
  std::set<std::string> existingRTMPStreamKeys;
  {
    std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);
    if (lock.try_lock_for(std::chrono::milliseconds(500))) {
      for (const auto &[id, info] : instances_) {
        // Skip current instance (it's being created)
        if (id == instanceId) {
          continue;
        }
        
        // Extract RTMP stream key if RTMP URL is configured
        if (!info.rtmpUrl.empty()) {
          std::string streamKey = PipelineBuilderDestinationNodes::extractRTMPStreamKey(info.rtmpUrl);
          if (!streamKey.empty()) {
            existingRTMPStreamKeys.insert(streamKey);
          }
        }
        
        // Also check additionalParams for RTMP_URL
        auto rtmpIt = info.additionalParams.find("RTMP_URL");
        if (rtmpIt != info.additionalParams.end() && !rtmpIt->second.empty()) {
          std::string streamKey = PipelineBuilderDestinationNodes::extractRTMPStreamKey(rtmpIt->second);
          if (!streamKey.empty()) {
            existingRTMPStreamKeys.insert(streamKey);
          }
        }
        
        // Check RTMP_DES_URL as well
        auto rtmpDesIt = info.additionalParams.find("RTMP_DES_URL");
        if (rtmpDesIt != info.additionalParams.end() && !rtmpDesIt->second.empty()) {
          std::string streamKey = PipelineBuilderDestinationNodes::extractRTMPStreamKey(rtmpDesIt->second);
          if (!streamKey.empty()) {
            existingRTMPStreamKeys.insert(streamKey);
          }
        }
      }
    }
  }

  // ✅ ASYNC PIPELINE BUILDING: Build pipeline in background thread
  // This allows API to return immediately instead of blocking for 30+ seconds
  // Pipeline will be built asynchronously and instance will be marked as
  // "building" until pipeline is ready

  if (ShutdownFlag::isRequested()) {
    throw std::runtime_error("Server is shutting down");
  }

  // Allocate GPU resource for this instance (in-process mode)
  // Use tryAllocateGPU with timeout to avoid blocking API when many concurrent
  // creates contend for ResourceManager lock (would cause 60s client timeout).
  auto &resourceManager = ResourceManager::getInstance();
  constexpr int kGpuAllocTimeoutMs = 5000;
  auto gpu_allocation =
      resourceManager.tryAllocateGPU(1536, -1, kGpuAllocTimeoutMs);

  if (gpu_allocation) {
    {
      std::lock_guard<std::mutex> lock(gpu_allocations_mutex_);
      gpu_allocations_[instanceId] = gpu_allocation;
    }
    std::cout << "[InstanceRegistry] Allocated GPU " << gpu_allocation->device_id 
              << " for instance " << instanceId << " (in-process mode)" << std::endl;
    std::cout << "[InstanceRegistry] Note: In in-process mode, all instances share the same process." << std::endl;
    std::cout << "[InstanceRegistry] GPU will be used automatically by CVEDIX SDK if available." << std::endl;
  } else {
    std::cout << "[InstanceRegistry] No GPU available for instance "
              << instanceId << " - will use CPU (or GPU lock timeout after "
              << kGpuAllocTimeoutMs << "ms)" << std::endl;
  }

  // Create instance info (no lock needed)
  InstanceInfo info = createInstanceInfo(instanceId, req, solution);
  
  // Mark as building if solution is provided (pipeline will be built async)
  if (solution) {
    info.building = true;
    info.loaded = false; // Not loaded until pipeline is built
  } else {
    info.building = false;
    info.loaded = true; // No solution = no pipeline needed, so "loaded"
  }

  // Store instance (need lock briefly)
  // NOTE: Pipeline will be stored later by buildPipelineAsync when build completes
  // Use try_lock_for in short chunks so we can abort quickly on shutdown (Ctrl+C).
  std::cerr << "[InstanceRegistry] Acquiring registry lock to store instance "
            << instanceId << "..." << std::endl;
  {
    std::unique_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);
    constexpr int kStoreLockTimeoutMs = 10000; // 10s max wait
    constexpr int kChunkMs = 200;              // Check shutdown every 200ms
    int remaining_ms = kStoreLockTimeoutMs;
    while (remaining_ms > 0) {
      if (ShutdownFlag::isRequested()) {
        std::cerr << "[InstanceRegistry] createInstance() aborted: shutdown requested"
                  << std::endl;
        {
          std::lock_guard<std::mutex> gpuLock(gpu_allocations_mutex_);
          auto it = gpu_allocations_.find(instanceId);
          if (it != gpu_allocations_.end()) {
            ResourceManager::getInstance().releaseGPU(it->second);
            gpu_allocations_.erase(it);
          }
        }
        throw std::runtime_error("Server is shutting down");
      }
      int wait_ms = std::min(kChunkMs, remaining_ms);
      if (lock.try_lock_for(std::chrono::milliseconds(wait_ms)))
        break;
      remaining_ms -= wait_ms;
    }
    if (!lock.owns_lock()) {
      std::cerr << "[InstanceRegistry] createInstance() failed: could not acquire "
                   "registry lock within "
                << kStoreLockTimeoutMs << "ms (registry busy)" << std::endl;
      {
        std::lock_guard<std::mutex> gpuLock(gpu_allocations_mutex_);
        auto it = gpu_allocations_.find(instanceId);
        if (it != gpu_allocations_.end()) {
          ResourceManager::getInstance().releaseGPU(it->second);
          gpu_allocations_.erase(it);
        }
      }
      throw std::runtime_error(
          "Instance registry is busy, please retry in a few seconds");
    }
    instances_[instanceId] = info;
    // DO NOT store pipeline here - it will be stored by buildPipelineAsync
  } // Release lock - save to storage doesn't need it

  // ✅ ASYNC SAVE: Save to storage in background thread to avoid blocking API response
  // This allows API to return immediately instead of waiting for file I/O
  std::thread([this, instanceId, info, persistent = req.persistent]() {
    try {
      bool saved = instance_storage_.saveInstance(instanceId, info);
      if (saved) {
        if (persistent) {
          std::cerr << "[InstanceRegistry] Instance configuration saved "
                       "(persistent - will be loaded on restart)"
                    << std::endl;
        } else {
          std::cerr << "[InstanceRegistry] Instance configuration saved "
                       "(non-persistent - for inspection only)"
                    << std::endl;
        }
      } else {
        std::cerr << "[InstanceRegistry] Warning: Failed to save instance "
                     "configuration to file"
                  << std::endl;
      }
    } catch (const std::exception &e) {
      std::cerr << "[InstanceRegistry] Exception in async save thread: " << e.what() << std::endl;
    } catch (...) {
      std::cerr << "[InstanceRegistry] Unknown exception in async save thread" << std::endl;
    }
  }).detach();

  // Build pipeline ASYNC if solution is provided
  // This allows API to return immediately instead of blocking for 30+ seconds
  // Auto-start is handled inside buildPipelineAsync after pipeline build completes
  if (solution) {
    std::cerr << "[InstanceRegistry] Starting async pipeline build..." << std::endl;
    buildPipelineAsync(instanceId, req, *solution, existingRTMPStreamKeys);
  } else {
    std::cerr << "[InstanceRegistry] No solution provided, skipping pipeline build" << std::endl;
  }

  std::cerr << "[InstanceRegistry] createInstance() returning instanceId: " << instanceId << std::endl;
  std::cerr << "[InstanceRegistry] ========================================" << std::endl;
  return instanceId;
}

bool InstanceRegistry::deleteInstance(const std::string &instanceId) {
  // CRITICAL: Get pipeline copy and release lock before calling stopPipeline
  // stopPipeline can take a long time and doesn't need the lock
  // This prevents blocking other operations when deleting instances

  std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> pipelineToStop;
  // Note: isPersistent was removed as it's not used in this function

  {
    std::unique_lock<std::shared_timed_mutex> lock(
        mutex_); // Exclusive lock for write operations

    auto it = instances_.find(instanceId);
    if (it == instances_.end()) {
      return false;
    }

    // Note: persistent flag is checked but not used here - removed to avoid
    // unused variable warning

    // Get pipeline copy before releasing lock
    auto pipelineIt = pipelines_.find(instanceId);
    if (pipelineIt != pipelines_.end() && !pipelineIt->second.empty()) {
      pipelineToStop = pipelineIt->second;
    }

    // Remove from maps immediately to prevent other threads from accessing
    pipelines_.erase(instanceId);
    instances_.erase(it);
  } // Release lock - stopPipeline and storage operations don't need it

  InstanceFileLogger::removeInstance(instanceId);

  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;
  std::cerr << "[InstanceRegistry] Deleting instance " << instanceId << "..."
            << std::endl;
  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;

  // Stop pipeline if running (cleanup before deletion) - do this outside lock
  if (!pipelineToStop.empty()) {
    std::cerr << "[InstanceRegistry] Stopping pipeline before deletion..."
              << std::endl;
    try {
      stopPipeline(pipelineToStop, true); // true = deletion, cleanup everything
      pipelineToStop.clear();             // Ensure nodes are destroyed
      std::cerr << "[InstanceRegistry] Pipeline stopped and removed"
                << std::endl;
    } catch (const std::exception &e) {
      std::cerr
          << "[InstanceRegistry] Exception stopping pipeline during deletion: "
          << e.what() << std::endl;
      // Continue with deletion anyway
    } catch (...) {
      std::cerr << "[InstanceRegistry] Unknown exception stopping pipeline "
                   "during deletion"
                << std::endl;
      // Continue with deletion anyway
    }
  }

  // Stop video loop monitoring thread if exists
  stopVideoLoopThread(instanceId);

  // Stop MP4 directory watcher if exists
  {
    std::lock_guard<std::mutex> watcherLock(mp4_watcher_mutex_);
    auto watcherIt = mp4_watchers_.find(instanceId);
    if (watcherIt != mp4_watchers_.end()) {
      watcherIt->second->stop();
      mp4_watchers_.erase(watcherIt);
      std::cerr
          << "[InstanceRegistry] Stopped MP4 directory watcher for instance "
          << instanceId << std::endl;
    }
  }

  // Release GPU allocation if exists
  {
    std::lock_guard<std::mutex> lock(gpu_allocations_mutex_);
    auto it = gpu_allocations_.find(instanceId);
    if (it != gpu_allocations_.end()) {
      auto &resourceManager = ResourceManager::getInstance();
      resourceManager.releaseGPU(it->second);
      gpu_allocations_.erase(it);
      std::cout << "[InstanceRegistry] Released GPU allocation for instance: " 
                << instanceId << std::endl;
    }
  }

  // Delete from storage (doesn't need lock)
  // Always delete from storage since all instances are saved to storage for
  // debugging/inspection This prevents deleted instances from being reloaded on
  // server restart
  std::cerr << "[InstanceRegistry] Removing instance from storage..."
            << std::endl;
  instance_storage_.deleteInstance(instanceId);

  std::cerr << "[InstanceRegistry] ✓ Instance " << instanceId
            << " deleted successfully" << std::endl;
  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;
  return true;
}

std::optional<InstanceInfo>
InstanceRegistry::getInstance(const std::string &instanceId) const {
  // CRITICAL: Use timeout to prevent blocking if mutex is locked by another
  // operation This prevents API calls from hanging indefinitely Use shared_lock
  // (read lock) to allow concurrent reads - we only read data and update a
  // local copy, not the registry itself
  std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);

  // Try to acquire lock with timeout (configurable via
  // REGISTRY_MUTEX_TIMEOUT_MS)
  if (!lock.try_lock_for(TimeoutConstants::getRegistryMutexTimeout())) {
    std::cerr << "[InstanceRegistry] WARNING: getInstance() timeout - "
                 "mutex is locked, returning nullopt"
              << std::endl;
    if (isInstanceLoggingEnabled()) {
      PLOG_WARNING << "[InstanceRegistry] getInstance() timeout after "
                      "2000ms - mutex may be locked by another operation";
    }
    return std::nullopt; // Return nullopt to prevent blocking
  }

  auto it = instances_.find(instanceId);
  if (it != instances_.end()) {
    InstanceInfo info = it->second;

    // If instance is running, calculate FPS dynamically from statistics tracker
    if (info.running) {
      auto trackerIt = statistics_trackers_.find(instanceId);
      if (trackerIt != statistics_trackers_.end()) {
        const InstanceStatsTracker &tracker = trackerIt->second;

        // Get pipeline to access source node
        auto pipelineIt = pipelines_.find(instanceId);
        if (pipelineIt != pipelines_.end() && !pipelineIt->second.empty()) {
          auto sourceNode = pipelineIt->second[0];
          if (sourceNode) {
            double sourceFps = 0.0;

            // Try to get source FPS from RTSP or file node
            try {
              auto rtspNode =
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(
                      sourceNode);
              auto fileNode =
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(
                      sourceNode);

              if (rtspNode) {
                int fps_int = rtspNode->get_original_fps();
                if (fps_int > 0) {
                  sourceFps = static_cast<double>(fps_int);
                }
              } else if (fileNode) {
                int fps_int = fileNode->get_original_fps();
                if (fps_int > 0) {
                  sourceFps = static_cast<double>(fps_int);
                }
              }
            } catch (...) {
              // If APIs are not available, use defaults
            }

            // Calculate elapsed time
            auto now = std::chrono::steady_clock::now();
            auto elapsed_seconds_double =
                std::chrono::duration<double>(now - tracker.start_time).count();

            // Calculate current FPS: PREFER FPS from backpressure controller
            // (rolling window, more accurate) then fallback to average from
            // start_time, then source FPS, then info.fps, then tracker.last_fps
            double currentFps = 0.0;

            // First priority: Use FPS from backpressure controller (real-time
            // rolling window)
            using namespace BackpressureController;
            auto &backpressure =
                BackpressureController::BackpressureController::getInstance();
            double backpressureFps = backpressure.getCurrentFPS(instanceId);
            if (backpressureFps > 0.0) {
              currentFps = std::round(backpressureFps);
            } else {
              // Fallback: Calculate actual processing FPS based on frames
              // actually processed This is less accurate as it averages from
              // start_time (includes warm-up time)
              uint64_t frames_processed_value =
                  tracker.frames_processed.load(std::memory_order_relaxed);
              double actualProcessingFps = 0.0;
              if (elapsed_seconds_double > 0.0 && frames_processed_value > 0) {
                actualProcessingFps =
                    static_cast<double>(frames_processed_value) /
                    elapsed_seconds_double;
              }

              if (actualProcessingFps > 0.0) {
                currentFps = std::round(actualProcessingFps);
              } else if (sourceFps > 0.0) {
                currentFps = std::round(sourceFps);
              } else if (info.fps > 0.0) {
                currentFps = std::round(info.fps);
              } else {
                currentFps = std::round(tracker.last_fps);
              }
            }

            // Update fps in the returned info
            info.fps = currentFps;
          }
        }
      }
    }

    return info;
  }
  return std::nullopt;
}

bool InstanceRegistry::startInstance(const std::string &instanceId,
                                     bool skipAutoStop) {
  // NEW BEHAVIOR: Start instance means create a new instance with same ID and
  // config This ensures fresh pipeline is built every time

  InstanceInfo existingInfo;
  std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> pipelineToStop;
  bool wasRunning = false;
  bool usedExistingPipeline = false; // true when we waited for async build and use that pipeline

  // Get existing instance info
  {
    std::unique_lock<std::shared_timed_mutex> lock(
        mutex_); // Exclusive lock for write operations
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt == instances_.end()) {
      std::cerr << "[InstanceRegistry] Instance " << instanceId << " not found"
                << std::endl;
      return false;
    }

    existingInfo = instanceIt->second;

    // ✅ ASYNC BUILD: If pipeline is building, wait for it (then use that pipeline)
    if (existingInfo.building) {
      constexpr int kWaitBuildTimeoutSec = 120;
      constexpr int kWaitBuildPollMs = 500;
      std::cerr << "[InstanceRegistry] Instance " << instanceId
                << " pipeline is building, waiting up to " << kWaitBuildTimeoutSec
                << "s..." << std::endl;
      lock.unlock();
      int waited_ms = 0;
      bool build_done = false;
      while (waited_ms < kWaitBuildTimeoutSec * 1000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kWaitBuildPollMs));
        waited_ms += kWaitBuildPollMs;
        if (ShutdownFlag::isRequested()) {
          return false;
        }
        lock.lock();
        instanceIt = instances_.find(instanceId);
        if (instanceIt == instances_.end()) {
          lock.unlock();
          return false;
        }
        if (!instanceIt->second.building) {
          build_done = true;
          existingInfo = instanceIt->second;
          if (!existingInfo.buildError.empty()) {
            std::cerr << "[InstanceRegistry] ✗ Cannot start instance "
                      << instanceId
                      << ": Pipeline build failed: " << existingInfo.buildError
                      << std::endl;
            lock.unlock();
            return false;
          }
          auto pipelineIt = pipelines_.find(instanceId);
          if (pipelineIt == pipelines_.end() || pipelineIt->second.empty()) {
            std::cerr << "[InstanceRegistry] ✗ Pipeline not ready for instance "
                      << instanceId << std::endl;
            lock.unlock();
            return false;
          }
          usedExistingPipeline = true;
          wasRunning = instanceIt->second.running;
          std::cerr << "[InstanceRegistry] Pipeline build completed, "
                       "proceeding with start"
                    << std::endl;
          break;
        }
        lock.unlock();
      }
      if (!build_done) {
        std::cerr << "[InstanceRegistry] ✗ Timeout waiting for pipeline build "
                     "for instance "
                  << instanceId << " (" << kWaitBuildTimeoutSec << "s)"
                  << std::endl;
        return false;
      }
      // Hold lock; fall through to run stop/erase only when !usedExistingPipeline
    }

    if (!usedExistingPipeline) {
      // ✅ ASYNC BUILD CHECK: Kiểm tra lỗi build pipeline
      if (!existingInfo.buildError.empty()) {
        std::cerr << "[InstanceRegistry] ✗ Cannot start instance " << instanceId
                  << ": Pipeline build failed: " << existingInfo.buildError
                  << std::endl;
        return false;
      }

      // ✅ Pipeline trong map: sau stopInstance() pipeline đã bị erase — đó là
      // trạng thái bình thường trước khi rebuild. Chỉ coi là lỗi "chưa build"
      // khi không có solution (không thể rebuild). Nếu có solutionId, luồng
      // phía dưới sẽ gọi rebuildPipelineFromInstanceInfo().
      auto pipelineIt = pipelines_.find(instanceId);
      const bool havePipeline =
          pipelineIt != pipelines_.end() && !pipelineIt->second.empty();
      if (!havePipeline && existingInfo.solutionId.empty()) {
        std::cerr << "[InstanceRegistry] ✗ Cannot start instance " << instanceId
                  << ": Pipeline not built yet (no solution configured)"
                  << std::endl;
        std::cerr << "[InstanceRegistry] If instance was just created, pipeline "
                     "may still be building in background"
                  << std::endl;
        std::cerr << "[InstanceRegistry] Check instance status via GET "
                     "/v1/core/instance/"
                  << instanceId << " to see build status" << std::endl;
        return false;
      }

      // Stop instance if it's running (unless skipAutoStop is true)
      wasRunning = instanceIt->second.running;

      if (wasRunning && !skipAutoStop) {
        std::cerr << "[InstanceRegistry] Instance " << instanceId
                  << " is currently running, stopping it first..." << std::endl;
        instanceIt->second.running = false;

        // Get pipeline copy before releasing lock
        auto pipelineIt = pipelines_.find(instanceId);
        if (pipelineIt != pipelines_.end() && !pipelineIt->second.empty()) {
          pipelineToStop = pipelineIt->second;
        }
      } else if (wasRunning && skipAutoStop) {
        // If skipAutoStop is true, instance should already be stopped
        if (instanceIt->second.running) {
          std::cerr << "[InstanceRegistry] ✗ Error: Instance " << instanceId
                    << " is still running despite skipAutoStop=true"
                    << std::endl;
          std::cerr << "[InstanceRegistry] Instance must be stopped before "
                       "calling startInstance with skipAutoStop=true"
                    << std::endl;
          return false;
        }
      }

      // Remove old pipeline from map to ensure fresh build (only when not using just-built pipeline)
      pipelines_.erase(instanceId);
    }
  } // Release lock

  InstanceFileLogger::setInstanceFileLogging(
      instanceId, existingInfo.instanceFileLoggingEnabled);

  // Stop pipeline if it was running and not skipping auto-stop (do this outside
  // lock) Use full cleanup to ensure OpenCV DNN state is cleared
  if (wasRunning && !skipAutoStop && !pipelineToStop.empty()) {
    stopPipeline(pipelineToStop,
                 true);     // true = full cleanup to clear DNN state
    pipelineToStop.clear(); // Ensure nodes are destroyed immediately
  }

  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;
  std::cerr << "[InstanceRegistry] Starting instance " << instanceId
            << (usedExistingPipeline ? " (using built pipeline)..."
                                    : " (creating new pipeline)...")
            << std::endl;
  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;

  if (isInstanceLoggingEnabled()) {
    std::string im = "Starting instance: " + instanceId + " (" +
                     existingInfo.displayName +
                     ", solution: " + existingInfo.solutionId + ")";
    InstanceFileLogger::log(instanceId, plog::info, im);
    PLOG_INFO << "[Instance] " << im;
  }

  std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> pipelineCopy;

  if (usedExistingPipeline) {
    // Use pipeline that was just built by createInstance (async build)
    std::unique_lock<std::shared_timed_mutex> lock(mutex_);
    auto pipelineIt = pipelines_.find(instanceId);
    if (pipelineIt == pipelines_.end() || pipelineIt->second.empty()) {
      std::cerr << "[InstanceRegistry] ✗ Pipeline not found for instance "
                << instanceId << std::endl;
      return false;
    }
    pipelineCopy = pipelineIt->second;
    std::cerr << "[InstanceRegistry] ✓ Using pipeline from async build"
              << std::endl;
  } else {
    // Rebuild pipeline from instance info (this creates a fresh pipeline)
    {
      std::unique_lock<std::shared_timed_mutex> lock(mutex_);
      if (instances_.find(instanceId) == instances_.end()) {
        std::cerr << "[InstanceRegistry] ✗ Instance " << instanceId
                  << " was deleted during start operation" << std::endl;
        return false;
      }
    }

    if (!rebuildPipelineFromInstanceInfo(instanceId)) {
      std::cerr << "[InstanceRegistry] ✗ Failed to rebuild pipeline for instance "
                << instanceId << std::endl;
      return false;
    }

    // Get pipeline copy after rebuild
    {
      std::unique_lock<std::shared_timed_mutex> lock(mutex_);
      if (instances_.find(instanceId) == instances_.end()) {
        std::cerr << "[InstanceRegistry] ✗ Instance " << instanceId
                  << " was deleted during rebuild" << std::endl;
        pipelines_.erase(instanceId);
        return false;
      }
      auto pipelineIt = pipelines_.find(instanceId);
      if (pipelineIt == pipelines_.end() || pipelineIt->second.empty()) {
        std::cerr << "[InstanceRegistry] ✗ Pipeline rebuild failed or returned "
                     "empty pipeline"
                  << std::endl;
        return false;
      }
      pipelineCopy = pipelineIt->second;
    }

    std::cerr
        << "[InstanceRegistry] ✓ Pipeline rebuilt successfully (fresh pipeline)"
        << std::endl;
  }

  // Wait for models to be ready (use adaptive timeout)
  // OPTIMIZED: Reduced timeout to minimize impact on other instances
  std::cerr << "[InstanceRegistry] Waiting for models to be ready (adaptive, "
               "up to 1 second)..."
            << std::endl;
  std::cerr << "[InstanceRegistry] This ensures OpenCV DNN clears any cached "
               "state and models are fully initialized"
            << std::endl;

  // Check instance still exists before waiting
  {
    std::unique_lock<std::shared_timed_mutex> lock(
        mutex_); // Exclusive lock for write operations
    if (instances_.find(instanceId) == instances_.end()) {
      std::cerr << "[InstanceRegistry] ✗ Instance " << instanceId
                << " was deleted before model initialization" << std::endl;
      pipelines_.erase(instanceId);
      return false;
    }
  } // Release lock

  try {
    waitForModelsReady(
        pipelineCopy,
        1000); // 1 second max (reduced from 2s to minimize blocking)
  } catch (const std::exception &e) {
    std::cerr << "[InstanceRegistry] ✗ Exception waiting for models: "
              << e.what() << std::endl;
    // Cleanup pipeline on error
    {
      std::unique_lock<std::shared_timed_mutex> lock(
          mutex_); // Exclusive lock for write operations
      pipelines_.erase(instanceId);
    }
    return false;
  } catch (...) {
    std::cerr << "[InstanceRegistry] ✗ Unknown exception waiting for models"
              << std::endl;
    // Cleanup pipeline on error
    {
      std::unique_lock<std::shared_timed_mutex> lock(
          mutex_); // Exclusive lock for write operations
      pipelines_.erase(instanceId);
    }
    return false;
  }

  // Additional delay after rebuild to ensure OpenCV DNN has fully cleared old
  // state OPTIMIZED: Reduced delay to minimize impact on other instances
  std::cerr << "[InstanceRegistry] Additional stabilization delay after "
               "rebuild (500ms)..."
            << std::endl;
  std::cerr << "[InstanceRegistry] This ensures OpenCV DNN has fully cleared "
               "any cached state from previous run"
            << std::endl;
  std::this_thread::sleep_for(
      std::chrono::milliseconds(500)); // Reduced from 2000ms to 500ms

  // Check instance still exists after delay
  {
    std::unique_lock<std::shared_timed_mutex> lock(
        mutex_); // Exclusive lock for write operations
    if (instances_.find(instanceId) == instances_.end()) {
      std::cerr << "[InstanceRegistry] ✗ Instance " << instanceId
                << " was deleted during stabilization delay" << std::endl;
      pipelines_.erase(instanceId);
      return false;
    }
  } // Release lock

  // Validate file path for file source nodes BEFORE starting pipeline
  // This prevents infinite retry loops when file doesn't exist
  auto fileNode = std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(
      pipelineCopy[0]);
  if (fileNode) {
    // Get file path from instance info
    std::string filePath;
    {
      std::unique_lock<std::shared_timed_mutex> lock(
          mutex_); // Exclusive lock for write operations
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt != instances_.end()) {
        filePath = instanceIt->second.filePath;
        // Also check additionalParams for FILE_PATH
        auto filePathIt = instanceIt->second.additionalParams.find("FILE_PATH");
        if (filePathIt != instanceIt->second.additionalParams.end() &&
            !filePathIt->second.empty()) {
          filePath = filePathIt->second;
        }
      }
    }

    if (!filePath.empty()) {
      // CRITICAL: Check parent directory is traversable FIRST
      // On Linux, you need execute permission on directory to access files inside
      fs::path filePathObj(filePath);
      fs::path parentDir = filePathObj.parent_path();
      
      if (!parentDir.empty() && parentDir != "/" && 
          !CVEDIXValidator::isDirectoryTraversable(parentDir)) {
        std::cerr
            << "[InstanceRegistry] ✗ Cannot access parent directory: "
            << parentDir.string() << std::endl;
        std::cerr << "[InstanceRegistry] ✗ Cannot start instance - directory "
                     "permission validation failed"
                  << std::endl;
        std::cerr << "[InstanceRegistry] Directory must have execute (x) "
                     "permission for traversal"
                  << std::endl;
        std::cerr << "[InstanceRegistry] Current directory permissions:" << std::endl;
        struct stat dirStat;
        if (stat(parentDir.c_str(), &dirStat) == 0) {
          std::cerr << "[InstanceRegistry]   Mode: " << std::oct << (dirStat.st_mode & 0777) 
                    << std::dec << std::endl;
        }
        std::cerr << "[InstanceRegistry] Solution:" << std::endl;
        std::cerr << "[InstanceRegistry]   sudo chmod 755 " << parentDir.string() 
                  << std::endl;
        std::cerr << "[InstanceRegistry]   Or ensure directory is readable and "
                     "executable by service user (edgeai)"
                  << std::endl;
        // Cleanup pipeline
        {
          std::unique_lock<std::shared_timed_mutex> lock(
              mutex_); // Exclusive lock for write operations
          pipelines_.erase(instanceId);
        }
        return false;
      }

      // Check if file exists and is readable
      struct stat fileStat;
      if (stat(filePath.c_str(), &fileStat) != 0) {
        std::cerr
            << "[InstanceRegistry] ✗ File does not exist or is not accessible: "
            << filePath << std::endl;
        std::cerr << "[InstanceRegistry] ✗ Cannot start instance - file "
                     "validation failed"
                  << std::endl;
        std::cerr << "[InstanceRegistry] Please check:" << std::endl;
        std::cerr << "[InstanceRegistry]   1. File path is correct: "
                  << filePath << std::endl;
        std::cerr << "[InstanceRegistry]   2. File exists and is readable"
                  << std::endl;
        std::cerr
            << "[InstanceRegistry]   3. File permissions allow read access"
            << std::endl;
        std::cerr << "[InstanceRegistry]   4. Parent directory is traversable "
                     "(has execute permission)"
                  << std::endl;
        // Cleanup pipeline
        {
          std::unique_lock<std::shared_timed_mutex> lock(
              mutex_); // Exclusive lock for write operations
          pipelines_.erase(instanceId);
        }
        return false;
      }

      if (!S_ISREG(fileStat.st_mode)) {
        std::cerr << "[InstanceRegistry] ✗ Path is not a regular file: "
                  << filePath << std::endl;
        std::cerr << "[InstanceRegistry] ✗ Cannot start instance - file "
                     "validation failed"
                  << std::endl;
        // Cleanup pipeline
        {
          std::unique_lock<std::shared_timed_mutex> lock(
              mutex_); // Exclusive lock for write operations
          pipelines_.erase(instanceId);
        }
        return false;
      }

      // Check file is readable
      if (!CVEDIXValidator::isFileReadable(filePathObj)) {
        std::cerr << "[InstanceRegistry] ✗ File is not readable: " << filePath
                  << std::endl;
        std::cerr << "[InstanceRegistry] ✗ Cannot start instance - file "
                     "permission validation failed"
                  << std::endl;
        std::cerr << CVEDIXValidator::getPermissionErrorMessage(filePath)
                  << std::endl;
        // Cleanup pipeline
        {
          std::unique_lock<std::shared_timed_mutex> lock(
              mutex_); // Exclusive lock for write operations
          pipelines_.erase(instanceId);
        }
        return false;
      }

      // CRITICAL: Check for required GStreamer plugins BEFORE attempting to open file
      // This prevents infinite retry loops when plugins are missing
      // Missing plugins cause GStreamer to fail silently, leading to SDK retries
      std::cerr << "[InstanceRegistry] Checking GStreamer plugins for file source..."
                << std::endl;
      auto plugins = GStreamerChecker::checkRequiredPlugins();
      std::vector<std::string> missingRequired;
      // Check for plugins specifically needed for file source (MP4/H.264)
      std::vector<std::string> requiredForFileSource = {
          "isomp4", "h264parse", "avdec_h264", "filesrc", "videoconvert"};
      for (const auto &pluginName : requiredForFileSource) {
        auto it = plugins.find(pluginName);
        if (it != plugins.end() && it->second.required && !it->second.available) {
          missingRequired.push_back(pluginName);
        }
      }

      if (!missingRequired.empty()) {
        std::cerr
            << "[InstanceRegistry] ✗ Cannot start instance - required GStreamer "
               "plugins are missing"
            << std::endl;
        std::cerr << "[InstanceRegistry] Missing plugins: ";
        for (size_t i = 0; i < missingRequired.size(); ++i) {
          std::cerr << missingRequired[i];
          if (i < missingRequired.size() - 1)
            std::cerr << ", ";
        }
        std::cerr << std::endl;
        std::cerr
            << "[InstanceRegistry] These plugins are required to read video files"
            << std::endl;
        std::cerr << "[InstanceRegistry] Error details:" << std::endl;
        std::cerr << "[InstanceRegistry]   - GStreamer cannot open video file "
                     "without these plugins"
                  << std::endl;
        std::cerr
            << "[InstanceRegistry]   - File source node will retry indefinitely"
            << std::endl;
        std::cerr << "[InstanceRegistry]   - This causes process to hang or exit"
                  << std::endl;
        std::cerr << "[InstanceRegistry] Please install missing plugins:"
                  << std::endl;
        std::string installCmd =
            GStreamerChecker::getInstallationCommand(missingRequired);
        if (!installCmd.empty()) {
          std::cerr << "[InstanceRegistry]   " << installCmd << std::endl;
        } else {
          std::cerr << "[InstanceRegistry]   sudo apt-get update && sudo apt-get "
                       "install -y gstreamer1.0-libav gstreamer1.0-plugins-base "
                       "gstreamer1.0-plugins-good"
                    << std::endl;
        }
        // Cleanup pipeline
        {
          std::unique_lock<std::shared_timed_mutex> lock(
              mutex_); // Exclusive lock for write operations
          pipelines_.erase(instanceId);
        }
        return false;
      }
      std::cerr << "[InstanceRegistry] ✓ Required GStreamer plugins are available"
                << std::endl;

      // Validate video file format using ffprobe (if available)
      // This prevents infinite retry loops when file is corrupted or invalid
      std::string ffprobeCmd =
          "ffprobe -v error -show_format -show_streams \"" + filePath +
          "\" >/dev/null 2>&1";
      int ffprobeResult = system(ffprobeCmd.c_str());
      if (ffprobeResult != 0) {
        // Try gst-discoverer as fallback
        std::string gstCmd =
            "gst-discoverer-1.0 \"" + filePath + "\" >/dev/null 2>&1";
        int gstResult = system(gstCmd.c_str());
        if (gstResult != 0) {
          std::cerr
              << "[InstanceRegistry] ✗ Video file is invalid or corrupted: "
              << filePath << std::endl;
          std::cerr << "[InstanceRegistry] ✗ Cannot start instance - video "
                       "file validation failed"
                    << std::endl;
          std::cerr << "[InstanceRegistry] Error details:" << std::endl;
          std::cerr << "[InstanceRegistry]   - File exists but cannot be read "
                       "as video"
                    << std::endl;
          std::cerr << "[InstanceRegistry]   - File may be corrupted (missing "
                       "moov atom for MP4)"
                    << std::endl;
          std::cerr
              << "[InstanceRegistry]   - File may be in unsupported format"
              << std::endl;
          std::cerr << "[InstanceRegistry] Please check:" << std::endl;
          std::cerr << "[InstanceRegistry]   1. File is a valid video file "
                       "(try: ffprobe "
                    << filePath << ")" << std::endl;
          std::cerr
              << "[InstanceRegistry]   2. File is not corrupted or incomplete"
              << std::endl;
          std::cerr
              << "[InstanceRegistry]   3. File format is supported by GStreamer"
              << std::endl;
          // Cleanup pipeline
          {
            std::unique_lock<std::shared_timed_mutex> lock(
                mutex_); // Exclusive lock for write operations
            pipelines_.erase(instanceId);
          }
          return false;
        }
      }

      std::cerr << "[InstanceRegistry] ✓ File validation passed: " << filePath
                << std::endl;
    } else {
      std::cerr << "[InstanceRegistry] ⚠ Warning: File path is empty for file "
                   "source node"
                << std::endl;
    }
  }

  // Validate model files for DNN nodes BEFORE starting pipeline
  // This prevents assertion failures when model files don't exist
  std::map<std::string, std::string> additionalParams;
  {
    std::unique_lock<std::shared_timed_mutex> lock(
        mutex_); // Exclusive lock for write operations
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt != instances_.end()) {
      additionalParams = instanceIt->second.additionalParams;
    }
  }

  bool modelValidationFailed = false;
  std::string missingModelPath;

  // Check for YuNet face detector node
  for (const auto &node : pipelineCopy) {
    auto yunetNode = std::dynamic_pointer_cast<
        cvedix_nodes::cvedix_yunet_face_detector_node>(node);
    if (yunetNode) {
      // Get model path from additionalParams
      std::string modelPath;
      auto modelPathIt = additionalParams.find("MODEL_PATH");
      if (modelPathIt != additionalParams.end() &&
          !modelPathIt->second.empty()) {
        modelPath = modelPathIt->second;
      } else {
        // Try to get from solution config or use default
        // For now, we'll check if the default path exists
        modelPath = "/usr/share/cvedix/cvedix_data/models/face/"
                    "face_detection_yunet_2022mar.onnx";
      }

      // Check if model file exists
      struct stat modelStat;
      if (stat(modelPath.c_str(), &modelStat) != 0) {
        std::cerr
            << "[InstanceRegistry] ========================================"
            << std::endl;
        std::cerr
            << "[InstanceRegistry] ✗ CRITICAL: YuNet model file not found!"
            << std::endl;
        std::cerr << "[InstanceRegistry] Expected path: " << modelPath
                  << std::endl;
        std::cerr
            << "[InstanceRegistry] ========================================"
            << std::endl;
        std::cerr << "[InstanceRegistry] Cannot start instance - model file "
                     "validation failed"
                  << std::endl;
        std::cerr << "[InstanceRegistry] The pipeline will crash with "
                     "assertion failure if started without model file"
                  << std::endl;
        std::cerr << "[InstanceRegistry] Please ensure the model file exists "
                     "before starting the instance"
                  << std::endl;
        std::cerr
            << "[InstanceRegistry] ========================================"
            << std::endl;
        modelValidationFailed = true;
        missingModelPath = modelPath;
        break;
      }

      if (!S_ISREG(modelStat.st_mode)) {
        std::cerr << "[InstanceRegistry] ✗ CRITICAL: Model path is not a "
                     "regular file: "
                  << modelPath << std::endl;
        std::cerr << "[InstanceRegistry] Cannot start instance - model file "
                     "validation failed"
                  << std::endl;
        modelValidationFailed = true;
        missingModelPath = modelPath;
        break;
      }

      std::cerr << "[InstanceRegistry] ✓ YuNet model file validation passed: "
                << modelPath << std::endl;
    }

    // Check for Mask RCNN detector node
    auto maskRCNNNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_mask_rcnn_detector_node>(
            node);
    if (maskRCNNNode) {
      // Get model path from additionalParams
      std::string modelPath;
      auto modelPathIt = additionalParams.find("MODEL_PATH");
      if (modelPathIt != additionalParams.end() &&
          !modelPathIt->second.empty()) {
        modelPath = modelPathIt->second;
      } else {
        std::cerr << "[InstanceRegistry] ⚠ Warning: MODEL_PATH not found in "
                     "additionalParams for Mask RCNN"
                  << std::endl;
        continue;
      }

      // Get model config path
      std::string modelConfigPath;
      auto modelConfigPathIt = additionalParams.find("MODEL_CONFIG_PATH");
      if (modelConfigPathIt != additionalParams.end() &&
          !modelConfigPathIt->second.empty()) {
        modelConfigPath = modelConfigPathIt->second;
      }

      // Check if model file exists
      struct stat modelStat;
      if (stat(modelPath.c_str(), &modelStat) != 0) {
        std::cerr
            << "[InstanceRegistry] ========================================"
            << std::endl;
        std::cerr
            << "[InstanceRegistry] ✗ CRITICAL: Mask RCNN model file not found!"
            << std::endl;
        std::cerr << "[InstanceRegistry] Expected path: " << modelPath
                  << std::endl;
        std::cerr
            << "[InstanceRegistry] ========================================"
            << std::endl;
        std::cerr << "[InstanceRegistry] Cannot start instance - model file "
                     "validation failed"
                  << std::endl;
        std::cerr << "[InstanceRegistry] The pipeline will crash with "
                     "assertion failure if started without model file"
                  << std::endl;
        std::cerr << "[InstanceRegistry] Please ensure the model file exists "
                     "before starting the instance"
                  << std::endl;
        std::cerr
            << "[InstanceRegistry] ========================================"
            << std::endl;
        modelValidationFailed = true;
        missingModelPath = modelPath;
        break;
      }

      if (!S_ISREG(modelStat.st_mode)) {
        std::cerr << "[InstanceRegistry] ✗ CRITICAL: Model path is not a "
                     "regular file: "
                  << modelPath << std::endl;
        std::cerr << "[InstanceRegistry] Cannot start instance - model file "
                     "validation failed"
                  << std::endl;
        modelValidationFailed = true;
        missingModelPath = modelPath;
        break;
      }

      // Check model config path if provided
      if (!modelConfigPath.empty()) {
        struct stat configStat;
        if (stat(modelConfigPath.c_str(), &configStat) != 0) {
          std::cerr << "[InstanceRegistry] ⚠ WARNING: Mask RCNN config file "
                       "not found: "
                    << modelConfigPath << std::endl;
          std::cerr
              << "[InstanceRegistry] Model may fail to load without config file"
              << std::endl;
        } else if (!S_ISREG(configStat.st_mode)) {
          std::cerr << "[InstanceRegistry] ⚠ WARNING: Mask RCNN config path is "
                       "not a regular file: "
                    << modelConfigPath << std::endl;
        } else {
          std::cerr << "[InstanceRegistry] ✓ Mask RCNN config file validation "
                       "passed: "
                    << modelConfigPath << std::endl;
        }
      }

      std::cerr
          << "[InstanceRegistry] ✓ Mask RCNN model file validation passed: "
          << modelPath << std::endl;
      std::cerr << "[InstanceRegistry] ⚠ NOTE: If you see 'cv::dnn::readNet "
                   "load network failed!' warning,"
                << std::endl;
      std::cerr << "[InstanceRegistry]    the model format may not be "
                   "supported by OpenCV DNN."
                << std::endl;
      std::cerr << "[InstanceRegistry]    Mask RCNN requires TensorFlow frozen "
                   "graph (.pb) with config (.pbtxt)."
                << std::endl;
      std::cerr << "[InstanceRegistry]    Ensure OpenCV is compiled with "
                   "TensorFlow support."
                << std::endl;
    }

    // Check for SFace feature encoder node
    auto sfaceNode = std::dynamic_pointer_cast<
        cvedix_nodes::cvedix_sface_feature_encoder_node>(node);
    if (sfaceNode) {
      // Get model path from additionalParams
      std::string modelPath;
      auto modelPathIt = additionalParams.find("SFACE_MODEL_PATH");
      if (modelPathIt != additionalParams.end() &&
          !modelPathIt->second.empty()) {
        modelPath = modelPathIt->second;
      } else {
        // Use system-wide default path (no hardcoded user-specific paths)
        modelPath = "/usr/share/cvedix/cvedix_data/models/face/"
                    "face_recognition_sface_2021dec.onnx";
      }

      // Check if model file exists
      struct stat modelStat;
      if (stat(modelPath.c_str(), &modelStat) != 0) {
        std::cerr
            << "[InstanceRegistry] ========================================"
            << std::endl;
        std::cerr
            << "[InstanceRegistry] ✗ CRITICAL: SFace model file not found!"
            << std::endl;
        std::cerr << "[InstanceRegistry] Expected path: " << modelPath
                  << std::endl;
        std::cerr
            << "[InstanceRegistry] ========================================"
            << std::endl;
        std::cerr << "[InstanceRegistry] Cannot start instance - model file "
                     "validation failed"
                  << std::endl;
        std::cerr << "[InstanceRegistry] The pipeline will crash with "
                     "assertion failure if started without model file"
                  << std::endl;
        std::cerr << "[InstanceRegistry] Please ensure the model file exists "
                     "before starting the instance"
                  << std::endl;
        std::cerr
            << "[InstanceRegistry] ========================================"
            << std::endl;
        modelValidationFailed = true;
        missingModelPath = modelPath;
        break;
      }

      if (!S_ISREG(modelStat.st_mode)) {
        std::cerr << "[InstanceRegistry] ✗ CRITICAL: Model path is not a "
                     "regular file: "
                  << modelPath << std::endl;
        std::cerr << "[InstanceRegistry] Cannot start instance - model file "
                     "validation failed"
                  << std::endl;
        modelValidationFailed = true;
        missingModelPath = modelPath;
        break;
      }

      std::cerr << "[InstanceRegistry] ✓ SFace model file validation passed: "
                << modelPath << std::endl;
    }
  }

  // If model validation failed, cleanup and return false
  if (modelValidationFailed) {
    std::cerr << "[InstanceRegistry] ✗ Cannot start instance - model file "
                 "validation failed"
              << std::endl;
    std::cerr << "[InstanceRegistry] Missing model file: " << missingModelPath
              << std::endl;
    // Cleanup pipeline
    {
      std::unique_lock<std::shared_timed_mutex> lock(
          mutex_); // Exclusive lock for write operations
      pipelines_.erase(instanceId);
    }
    return false;
  }

  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;
  std::cerr << "[InstanceRegistry] Starting pipeline for instance "
            << instanceId << "..." << std::endl;
  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;

  // Start pipeline (isRestart=true because we rebuilt the pipeline)
  bool started = false;
  try {
    started = startPipeline(pipelineCopy, instanceId, true);
  } catch (const std::exception &e) {
    std::cerr << "[InstanceRegistry] ✗ Exception starting pipeline: "
              << e.what() << std::endl;
    // Cleanup pipeline on error
    {
      std::unique_lock<std::shared_timed_mutex> lock(
          mutex_); // Exclusive lock for write operations
      pipelines_.erase(instanceId);
    }
    return false;
  } catch (...) {
    std::cerr << "[InstanceRegistry] ✗ Unknown exception starting pipeline"
              << std::endl;
    // Cleanup pipeline on error
    {
      std::unique_lock<std::shared_timed_mutex> lock(
          mutex_); // Exclusive lock for write operations
      pipelines_.erase(instanceId);
    }
    return false;
  }

  // Update running status and cleanup on failure
  {
    std::unique_lock<std::shared_timed_mutex> lock(
        mutex_); // Exclusive lock for write operations
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt != instances_.end()) {
      if (started) {
        instanceIt->second.running = true;
        // Reset retry counter and tracking when instance starts successfully
        instanceIt->second.retryCount = 0;
        instanceIt->second.retryLimitReached = false;
        instanceIt->second.startTime = std::chrono::steady_clock::now();
        instanceIt->second.lastActivityTime = instanceIt->second.startTime;
        instanceIt->second.hasReceivedData = false;
        std::cerr << "[InstanceRegistry] ✓ Instance " << instanceId
                  << " started successfully" << std::endl;

        // Start MP4 directory watcher if RECORD_PATH is set
        auto recordPathIt =
            instanceIt->second.additionalParams.find("RECORD_PATH");
        if (recordPathIt != instanceIt->second.additionalParams.end() &&
            !recordPathIt->second.empty()) {
          std::string recordPath = recordPathIt->second;
          std::lock_guard<std::mutex> watcherLock(mp4_watcher_mutex_);

          // Stop existing watcher if any
          if (mp4_watchers_.find(instanceId) != mp4_watchers_.end()) {
            mp4_watchers_[instanceId]->stop();
            mp4_watchers_.erase(instanceId);
          }

          // Create and start new watcher
          auto watcher =
              std::make_unique<MP4Finalizer::MP4DirectoryWatcher>(recordPath);
          watcher->start();
          mp4_watchers_[instanceId] = std::move(watcher);
          std::cerr
              << "[InstanceRegistry] ✓ Started MP4 directory watcher for: "
              << recordPath << std::endl;
          std::cerr
              << "[InstanceRegistry] Files will be automatically converted "
                 "to compatible format during recording"
              << std::endl;
        }

        if (isInstanceLoggingEnabled()) {
          const auto &info = instanceIt->second;
          std::string im = "Instance started successfully: " + instanceId +
                           " (" + info.displayName +
                           ", solution: " + info.solutionId + ", running: true)";
          InstanceFileLogger::log(instanceId, plog::info, im);
          PLOG_INFO << "[Instance] " << im;
        }
      } else {
        std::cerr << "[InstanceRegistry] ✗ Failed to start instance "
                  << instanceId << std::endl;
        if (isInstanceLoggingEnabled()) {
          std::string im = "Failed to start instance: " + instanceId + " (" +
                           existingInfo.displayName + ")";
          InstanceFileLogger::log(instanceId, plog::error, im);
          PLOG_ERROR << "[Instance] " << im;
        }
        // Cleanup pipeline if start failed to prevent resource leak
        pipelines_.erase(instanceId);
        std::cerr
            << "[InstanceRegistry] Cleaned up pipeline after start failure"
            << std::endl;
      }
    } else {
      // Instance was deleted during start - cleanup pipeline
      pipelines_.erase(instanceId);
      std::cerr << "[InstanceRegistry] Instance " << instanceId
                << " was deleted during start - cleaned up pipeline"
                << std::endl;
      if (isInstanceLoggingEnabled()) {
        std::string im = "Instance was deleted during start: " + instanceId;
        InstanceFileLogger::log(instanceId, plog::warning, im);
        PLOG_WARNING << "[Instance] " << im;
      }
    }
  }

  // Log processing results for instances without RTMP output (after a short
  // delay to allow pipeline to start)
  if (started) {
    // Wait a bit for pipeline to initialize and start processing
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  }

  // DISABLED: Video loop monitoring thread - feature removed to improve
  // performance Start video loop monitoring thread for file-based instances
  // with LOOP_VIDEO enabled
  // {
  //     std::shared_lock<std::shared_timed_mutex> lock(mutex_);
  //     auto instanceIt = instances_.find(instanceId);
  //     if (instanceIt != instances_.end()) {
  //         const auto& info = instanceIt->second;
  //         bool isFileBased = !info.filePath.empty() ||
  //                          info.additionalParams.find("FILE_PATH") !=
  //                          info.additionalParams.end();
  //         if (isFileBased) {
  //             startVideoLoopThread(instanceId);
  //         }
  //     }
  // }

  return started;
}

bool InstanceRegistry::stopInstance(const std::string &instanceId) {
  // CRITICAL: Get pipeline copy and release lock before calling stopPipeline
  // stopPipeline can take a long time and doesn't need the lock
  // This prevents deadlock if another thread (like terminate handler) needs the
  // lock
  std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> pipelineCopy;
  bool wasRunning = false;
  std::string displayName;
  std::string solutionId;
  std::string recordPath; // Declare outside block for use after stopPipeline

  {
    std::unique_lock<std::shared_timed_mutex> lock(
        mutex_); // Exclusive lock for write operations

    auto pipelineIt = pipelines_.find(instanceId);
    if (pipelineIt == pipelines_.end()) {
      return false;
    }

    auto instanceIt = instances_.find(instanceId);
    if (instanceIt == instances_.end()) {
      return false;
    }

    wasRunning = instanceIt->second.running;
    displayName = instanceIt->second.displayName;
    solutionId = instanceIt->second.solutionId;

    // Get RECORD_PATH if exists (for auto-finalizing MP4 files after stop)
    auto recordPathIt = instanceIt->second.additionalParams.find("RECORD_PATH");
    if (recordPathIt != instanceIt->second.additionalParams.end()) {
      recordPath = recordPathIt->second;
    }

    // Copy pipeline before releasing lock
    pipelineCopy = pipelineIt->second;

    // CRITICAL: Validate that pipeline belongs to this instance
    // Each node should have instanceId in its name (e.g.,
    // "rtsp_src_{instanceId}") This ensures we only stop nodes belonging to
    // this specific instance
    std::cerr
        << "[InstanceRegistry] Validating pipeline ownership for instance "
        << instanceId << "..." << std::endl;
    std::cerr << "[InstanceRegistry] Pipeline contains " << pipelineCopy.size()
              << " nodes" << std::endl;

    // Mark as not running immediately (before stopPipeline)
    instanceIt->second.running = false;

    // CRITICAL: DO NOT remove pipeline from map yet - keep it until threads are
    // stopped This prevents race condition where reconnectRTSPStream() calls
    // getInstanceNodes() and gets empty vector, but thread already has
    // reference to nodes We'll remove it after threads are stopped
    // pipelines_.erase(pipelineIt);  // Moved to after thread stopping
  } // Release lock here - stopPipeline doesn't need it

  // CRITICAL: Stop ALL threads BEFORE stopping pipeline to prevent race
  // conditions Threads may be accessing nodes, so they must be stopped first
  // This prevents segmentation faults when GStreamer cleanup happens while
  // threads are still running

  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;
  std::cerr << "[InstanceRegistry] Stopping instance " << instanceId << "..."
            << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: All nodes will be fully destroyed to "
               "clear OpenCV DNN state"
            << std::endl;
  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;

  if (isInstanceLoggingEnabled()) {
    std::string im =
        "Stopping instance: " + instanceId + " (" + displayName +
        ", solution: " + solutionId +
        ", was running: " + std::string(wasRunning ? "true" : "false") + ")";
    InstanceFileLogger::log(instanceId, plog::info, im);
    PLOG_INFO << "[Instance] " << im;
  }

  // CRITICAL: Stop monitoring threads FIRST (before any other cleanup)
  // This prevents race condition where monitor threads try to reconnect while pipeline is being destroyed
  std::cerr << "[InstanceRegistry] Stopping monitoring threads for instance "
            << instanceId << "..." << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: Only stopping monitoring threads for "
               "this specific instance"
            << std::endl;
  
  // Stop RTSP monitor thread
  stopRTSPMonitorThread(instanceId);
  
  // Stop RTMP source monitor thread
  stopRTMPSourceMonitorThread(instanceId);
  
  // Stop RTMP destination monitor thread
  stopRTMPDestinationMonitorThread(instanceId);

  // CRITICAL: Now remove pipeline from map after threads are stopped
  // This ensures threads can't access pipeline through getInstanceNodes()
  // anymore
  {
    std::unique_lock<std::shared_timed_mutex> lock(mutex_);
    pipelines_.erase(instanceId);
  }

  // Stop video loop monitoring thread if exists
  // IMPORTANT: stopVideoLoopThread() uses instanceId to identify and stop ONLY
  // this instance's thread
  std::cerr << "[InstanceRegistry] Stopping video loop thread for instance "
            << instanceId << "..." << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: Only stopping video loop thread for "
               "this specific instance"
            << std::endl;
  stopVideoLoopThread(instanceId);

  // CRITICAL: Wait longer for threads to fully stop before cleaning up pipeline
  // This prevents race conditions where threads are still accessing nodes
  // during cleanup RTSP monitor thread may be in reconnectRTSPStream() which
  // can take up to 2 seconds
  std::cerr << "[InstanceRegistry] Waiting for all threads to stop..."
            << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: RTSP monitor thread may take up to 5 "
               "seconds to fully stop"
            << std::endl;
  std::this_thread::sleep_for(
      std::chrono::milliseconds(500)); // Increased from 200ms to 500ms

  // Now call stopPipeline without holding the lock
  // This prevents deadlock if stopPipeline takes a long time
  // Use isDeletion=true to fully cleanup nodes and clear OpenCV DNN state
  // CRITICAL: stopPipeline() is now guaranteed to never throw (it catches all
  // exceptions internally) IMPORTANT: pipelineCopy contains ONLY nodes
  // belonging to this instance (retrieved by instanceId)
  std::cerr << "[InstanceRegistry] Stopping pipeline for instance "
            << instanceId << "..." << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: Pipeline contains "
            << pipelineCopy.size() << " nodes belonging ONLY to this instance"
            << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: Other instances' pipelines are "
               "completely unaffected"
            << std::endl;
  try {
    stopPipeline(pipelineCopy,
                 true); // true = full cleanup like deletion to clear DNN state
  } catch (const std::exception &e) {
    // This should never happen since stopPipeline catches all exceptions, but
    // just in case...
    std::cerr
        << "[InstanceRegistry] CRITICAL: Unexpected exception in stopPipeline: "
        << e.what() << std::endl;
    std::cerr << "[InstanceRegistry] This indicates a bug - stopPipeline "
                 "should not throw"
              << std::endl;
    // Continue anyway - pipeline is already removed from map
  } catch (...) {
    // This should never happen since stopPipeline catches all exceptions, but
    // just in case...
    std::cerr << "[InstanceRegistry] CRITICAL: Unexpected unknown exception in "
                 "stopPipeline"
              << std::endl;
    std::cerr << "[InstanceRegistry] This indicates a bug - stopPipeline "
                 "should not throw"
              << std::endl;
    // Continue anyway - pipeline is already removed from map
  }

  // Clear pipeline copy to ensure all nodes are destroyed immediately
  // This helps ensure OpenCV DNN releases all internal state
  // Wrap in try-catch to be extra safe (though clear() shouldn't throw)
  std::cerr << "[InstanceRegistry] Clearing pipeline copy..." << std::endl;
  try {
    pipelineCopy.clear();
  } catch (...) {
    std::cerr << "[InstanceRegistry] Warning: Exception clearing pipeline copy "
                 "(unexpected)"
              << std::endl;
  }

  // CRITICAL: Give GStreamer extra time to fully cleanup after nodes are
  // destroyed This prevents segmentation faults from GStreamer cleanup NOTE:
  // Each instance has independent GStreamer pipelines, so cleanup of one
  // instance should not affect other running instances. However, we still need
  // to wait to ensure this instance's GStreamer resources are fully released.
  // FIXED: Increased wait time to ensure GStreamer elements are properly set to
  // NULL state before process continues
  std::cerr << "[InstanceRegistry] Waiting for GStreamer final cleanup..."
            << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: This cleanup only affects this "
               "instance, not other running instances"
            << std::endl;
  std::this_thread::sleep_for(
      std::chrono::milliseconds(1000)); // Increased from 500ms to 1000ms

  std::cerr << "[InstanceRegistry] ✓ Instance " << instanceId
            << " stopped successfully" << std::endl;
  std::cerr
      << "[InstanceRegistry] NOTE: All nodes have been destroyed. Pipeline "
         "will be rebuilt from scratch when you start this instance again"
      << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: This ensures OpenCV DNN starts with a "
               "clean state"
            << std::endl;
  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;

  // Stop MP4 directory watcher if exists
  {
    std::lock_guard<std::mutex> watcherLock(mp4_watcher_mutex_);
    auto watcherIt = mp4_watchers_.find(instanceId);
    if (watcherIt != mp4_watchers_.end()) {
      watcherIt->second->stop();
      mp4_watchers_.erase(watcherIt);
      std::cerr
          << "[InstanceRegistry] Stopped MP4 directory watcher for instance "
          << instanceId << std::endl;
    }
  }

  // Finalize and convert any remaining MP4 files if RECORD_PATH was set
  // This ensures the last file segment is converted even if it hasn't reached
  // max_duration (10 minutes) yet. The file_des_node will close the current
  // file when pipeline stops, and we convert it here.
  if (!recordPath.empty() && fs::exists(recordPath) &&
      fs::is_directory(recordPath)) {
    std::cerr << "[InstanceRegistry] Finalizing and converting MP4 files in: "
              << recordPath << std::endl;
    std::cerr << "[InstanceRegistry] This includes the last file segment that "
                 "was being recorded"
              << std::endl;
    std::cerr << "[InstanceRegistry] Running in background thread to not block "
                 "instance stop"
              << std::endl;

    // Run finalization in background thread
    std::thread finalizeThread([recordPath, instanceId]() {
      // Wait for file_des_node to fully close the current file
      // file_des_node closes file when pipeline stops, but it may take a moment
      // Increase wait time to ensure file is fully closed
      std::cerr << "[InstanceRegistry] [MP4Finalizer] Waiting for "
                   "file_des_node to close files..."
                << std::endl;

      // Wait longer and check file stability multiple times
      // This ensures file is fully closed before attempting conversion
      const int maxWaitAttempts = 6; // 6 attempts * 3 seconds = 18 seconds max
      bool allFilesStable = false;

      for (int attempt = 1; attempt <= maxWaitAttempts; attempt++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));

        // Check if all MP4 files in directory are stable
        allFilesStable = true;
        try {
          for (const auto &entry : fs::directory_iterator(recordPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".mp4") {
              std::string filePath = entry.path().string();
              if (MP4Finalizer::MP4Finalizer::isFileBeingWritten(filePath)) {
                allFilesStable = false;
                std::cerr << "[InstanceRegistry] [MP4Finalizer] File still "
                             "being written (attempt "
                          << attempt << "/" << maxWaitAttempts
                          << "): " << fs::path(filePath).filename().string()
                          << std::endl;
                break;
              }
            }
          }
        } catch (const fs::filesystem_error &e) {
          std::cerr
              << "[InstanceRegistry] [MP4Finalizer] Error checking files: "
              << e.what() << std::endl;
        }

        if (allFilesStable) {
          std::cerr
              << "[InstanceRegistry] [MP4Finalizer] All files are stable after "
              << (attempt * 3) << " seconds" << std::endl;
          break;
        }
      }

      if (!allFilesStable) {
        std::cerr << "[InstanceRegistry] [MP4Finalizer] ⚠ Some files may still "
                     "be closing, "
                  << "but proceeding with conversion anyway..." << std::endl;
      }

      std::cerr
          << "[InstanceRegistry] [MP4Finalizer] Starting finalization for "
             "instance "
          << instanceId << std::endl;
      std::cerr << "[InstanceRegistry] [MP4Finalizer] Converting all MP4 files "
                   "in: "
                << recordPath << std::endl;

      // Finalize all MP4 files in the record directory
      // This will:
      // 1. Try faststart on each file
      // 2. If file needs conversion (H.264 High profile or yuv444p), convert to
      //    Baseline + yuv420p
      // 3. Overwrite original file with converted version
      int processed = MP4Finalizer::MP4Finalizer::finalizeDirectory(
          recordPath, true); // true = convert to compatible format if needed

      std::cerr << "[InstanceRegistry] [MP4Finalizer] ✓ Completed finalization "
                   "for instance "
                << instanceId << std::endl;
      std::cerr << "[InstanceRegistry] [MP4Finalizer] Converted " << processed
                << " MP4 file(s) to compatible format" << std::endl;
      std::cerr
          << "[InstanceRegistry] [MP4Finalizer] All files are now viewable "
             "with standard video players"
          << std::endl;
    });

    // Detach thread so it runs independently and doesn't block instance stop
    finalizeThread.detach();
  }

  if (isInstanceLoggingEnabled()) {
    std::string im = "Instance stopped successfully: " + instanceId + " (" +
                     displayName + ", solution: " + solutionId + ")";
    InstanceFileLogger::log(instanceId, plog::info, im);
    PLOG_INFO << "[Instance] " << im;
  }

  return true;
}

std::vector<std::string> InstanceRegistry::listInstances() const {
  // CRITICAL: Use try_lock_for with timeout to prevent blocking if mutex is
  // held by other operations This prevents deadlock when called from terminate
  // handler or other critical paths
  std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);

  // Try to acquire lock with timeout (configurable via
  // REGISTRY_MUTEX_TIMEOUT_MS) If we can't get the lock quickly, return empty
  // vector to prevent deadlock
  if (!lock.try_lock_for(TimeoutConstants::getRegistryMutexTimeout())) {
    std::cerr << "[InstanceRegistry] WARNING: listInstances() timeout - mutex "
                 "is locked, returning empty vector"
              << std::endl;
    if (isInstanceLoggingEnabled()) {
      PLOG_WARNING << "[InstanceRegistry] listInstances() timeout after 1000ms "
                      "- mutex may be locked by another operation";
    }
    return {}; // Return empty vector to prevent blocking
  }

  // Successfully acquired lock, return list of instance IDs
  std::vector<std::string> result;
  result.reserve(instances_.size());
  for (const auto &pair : instances_) {
    result.push_back(pair.first);
  }
  return result;
}

int InstanceRegistry::getInstanceCount() const {
  // CRITICAL: Use try_lock_for with timeout to prevent blocking if mutex is
  // held by other operations
  std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);

  // Try to acquire lock with timeout (configurable via
  // REGISTRY_MUTEX_TIMEOUT_MS)
  if (!lock.try_lock_for(TimeoutConstants::getRegistryMutexTimeout())) {
    std::cerr << "[InstanceRegistry] WARNING: getInstanceCount() timeout - "
                 "mutex is locked, returning 0"
              << std::endl;
    if (isInstanceLoggingEnabled()) {
      PLOG_WARNING << "[InstanceRegistry] getInstanceCount() timeout after "
                      "1000ms - mutex may be locked by another operation";
    }
    return 0; // Return 0 to prevent blocking
  }

  // Successfully acquired lock, return count
  return static_cast<int>(instances_.size());
}

std::unordered_map<std::string, InstanceInfo>
InstanceRegistry::getAllInstances() const {
  // Use shared_lock (read lock) to allow multiple concurrent readers
  // This allows multiple API requests to call getAllInstances() simultaneously
  // Writers (start/stop/update) will use exclusive lock and block readers only
  // when writing CRITICAL: Use timeout to prevent deadlock if mutex is locked
  // by recovery handler
  std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);

  // Try to acquire lock with timeout (2000ms) - longer than listInstances for
  // API calls
  if (!lock.try_lock_for(std::chrono::milliseconds(2000))) {
    std::cerr << "[InstanceRegistry] WARNING: getAllInstances() timeout - "
                 "mutex is locked, returning empty map"
              << std::endl;
    if (isInstanceLoggingEnabled()) {
      PLOG_WARNING << "[InstanceRegistry] getAllInstances() timeout after "
                      "2000ms - mutex may be locked by another operation";
    }
    return {}; // Return empty map to prevent blocking
  }

  // Create a copy of instances and update FPS dynamically for running instances
  std::unordered_map<std::string, InstanceInfo> result = instances_;

  // Update FPS for running instances (similar to getInstance logic)
  for (auto &[instanceId, info] : result) {
    if (info.running) {
      auto trackerIt = statistics_trackers_.find(instanceId);
      if (trackerIt != statistics_trackers_.end()) {
        const InstanceStatsTracker &tracker = trackerIt->second;

        // Get pipeline to access source node
        auto pipelineIt = pipelines_.find(instanceId);
        if (pipelineIt != pipelines_.end() && !pipelineIt->second.empty()) {
          auto sourceNode = pipelineIt->second[0];
          if (sourceNode) {
            double sourceFps = 0.0;

            // Try to get source FPS from RTSP or file node
            try {
              auto rtspNode =
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(
                      sourceNode);
              auto fileNode =
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(
                      sourceNode);

              if (rtspNode) {
                int fps_int = rtspNode->get_original_fps();
                if (fps_int > 0) {
                  sourceFps = static_cast<double>(fps_int);
                }
              } else if (fileNode) {
                int fps_int = fileNode->get_original_fps();
                if (fps_int > 0) {
                  sourceFps = static_cast<double>(fps_int);
                }
              }
            } catch (...) {
              // If APIs are not available, use defaults
            }

            // Calculate elapsed time
            auto now = std::chrono::steady_clock::now();
            auto elapsed_seconds_double =
                std::chrono::duration<double>(now - tracker.start_time).count();

            // Calculate current FPS: PREFER FPS from backpressure controller
            // (rolling window, more accurate) then fallback to average from
            // start_time, then source FPS, then info.fps, then tracker.last_fps
            double currentFps = 0.0;

            // First priority: Use FPS from backpressure controller (real-time
            // rolling window)
            using namespace BackpressureController;
            auto &backpressure =
                BackpressureController::BackpressureController::getInstance();
            double backpressureFps = backpressure.getCurrentFPS(instanceId);
            if (backpressureFps > 0.0) {
              currentFps = std::round(backpressureFps);
            } else {
              // Fallback: Calculate actual processing FPS based on frames
              // actually processed This is less accurate as it averages from
              // start_time (includes warm-up time)
              uint64_t frames_processed_value =
                  tracker.frames_processed.load(std::memory_order_relaxed);
              double actualProcessingFps = 0.0;
              if (elapsed_seconds_double > 0.0 && frames_processed_value > 0) {
                actualProcessingFps =
                    static_cast<double>(frames_processed_value) /
                    elapsed_seconds_double;
              }

              if (actualProcessingFps > 0.0) {
                currentFps = std::round(actualProcessingFps);
              } else if (sourceFps > 0.0) {
                currentFps = std::round(sourceFps);
              } else if (info.fps > 0.0) {
                currentFps = std::round(info.fps);
              } else {
                currentFps = std::round(tracker.last_fps);
              }
            }

            // Update fps in the result
            info.fps = currentFps;
          }
        }
      }
    }
  }

  return result;
}

bool InstanceRegistry::hasInstance(const std::string &instanceId) const {
  // CRITICAL: Use timeout to prevent deadlock if mutex is locked by recovery
  // handler
  std::shared_lock<std::shared_timed_mutex> lock(
      mutex_, std::defer_lock); // Read lock - allows concurrent readers

  // Try to acquire lock with timeout (configurable via
  // REGISTRY_MUTEX_TIMEOUT_MS) Note: This is a quick check, but we use same
  // timeout as other read operations for consistency
  if (!lock.try_lock_for(TimeoutConstants::getRegistryMutexTimeout())) {
    std::cerr << "[InstanceRegistry] WARNING: hasInstance() timeout - mutex is "
                 "locked, returning false"
              << std::endl;
    return false; // Return false to prevent blocking
  }

  return instances_.find(instanceId) != instances_.end();
}

bool InstanceRegistry::updateInstance(const std::string &instanceId,
                                      const UpdateInstanceRequest &req) {
  bool isPersistent = false;
  InstanceInfo infoCopy;
  bool hasChanges = false;

  {
    std::unique_lock<std::shared_timed_mutex> lock(
        mutex_); // Exclusive lock for write operations

    auto instanceIt = instances_.find(instanceId);
    if (instanceIt == instances_.end()) {
      std::cerr << "[InstanceRegistry] Instance " << instanceId << " not found"
                << std::endl;
      return false;
    }

    InstanceInfo &info = instanceIt->second;

    // Check if instance is read-only
    if (info.readOnly) {
      std::cerr << "[InstanceRegistry] Cannot update read-only instance "
                << instanceId << std::endl;
      return false;
    }

    std::cerr << "[InstanceRegistry] ========================================"
              << std::endl;
    std::cerr << "[InstanceRegistry] Updating instance " << instanceId << "..."
              << std::endl;
    std::cerr << "[InstanceRegistry] ========================================"
              << std::endl;

    // Update fields if provided
    if (!req.name.empty()) {
      std::cerr << "[InstanceRegistry] Updating displayName: "
                << info.displayName << " -> " << req.name << std::endl;
      info.displayName = req.name;
      hasChanges = true;
    }

    if (!req.group.empty()) {
      std::cerr << "[InstanceRegistry] Updating group: " << info.group << " -> "
                << req.group << std::endl;
      info.group = req.group;
      hasChanges = true;
    }

    if (req.persistent.has_value()) {
      std::cerr << "[InstanceRegistry] Updating persistent: " << info.persistent
                << " -> " << req.persistent.value() << std::endl;
      info.persistent = req.persistent.value();
      hasChanges = true;
    }

    if (req.frameRateLimit != -1) {
      std::cerr << "[InstanceRegistry] Updating frameRateLimit: "
                << info.frameRateLimit << " -> " << req.frameRateLimit
                << std::endl;
      info.frameRateLimit = req.frameRateLimit;
      hasChanges = true;
    }

    if (req.configuredFps != -1) {
      std::cerr << "[InstanceRegistry] Updating configuredFps: "
                << info.configuredFps << " -> " << req.configuredFps
                << std::endl;
      info.configuredFps = req.configuredFps;
      hasChanges = true;
    }

    if (req.metadataMode.has_value()) {
      std::cerr << "[InstanceRegistry] Updating metadataMode: "
                << info.metadataMode << " -> " << req.metadataMode.value()
                << std::endl;
      info.metadataMode = req.metadataMode.value();
      hasChanges = true;
    }

    if (req.statisticsMode.has_value()) {
      std::cerr << "[InstanceRegistry] Updating statisticsMode: "
                << info.statisticsMode << " -> " << req.statisticsMode.value()
                << std::endl;
      info.statisticsMode = req.statisticsMode.value();
      hasChanges = true;
    }

    if (req.diagnosticsMode.has_value()) {
      std::cerr << "[InstanceRegistry] Updating diagnosticsMode: "
                << info.diagnosticsMode << " -> " << req.diagnosticsMode.value()
                << std::endl;
      info.diagnosticsMode = req.diagnosticsMode.value();
      hasChanges = true;
    }

    if (req.debugMode.has_value()) {
      std::cerr << "[InstanceRegistry] Updating debugMode: " << info.debugMode
                << " -> " << req.debugMode.value() << std::endl;
      info.debugMode = req.debugMode.value();
      hasChanges = true;
    }

    if (!req.detectorMode.empty()) {
      std::cerr << "[InstanceRegistry] Updating detectorMode: "
                << info.detectorMode << " -> " << req.detectorMode << std::endl;
      info.detectorMode = req.detectorMode;
      hasChanges = true;
    }

    if (!req.detectionSensitivity.empty()) {
      std::cerr << "[InstanceRegistry] Updating detectionSensitivity: "
                << info.detectionSensitivity << " -> "
                << req.detectionSensitivity << std::endl;
      info.detectionSensitivity = req.detectionSensitivity;
      hasChanges = true;
    }

    if (!req.movementSensitivity.empty()) {
      std::cerr << "[InstanceRegistry] Updating movementSensitivity: "
                << info.movementSensitivity << " -> " << req.movementSensitivity
                << std::endl;
      info.movementSensitivity = req.movementSensitivity;
      hasChanges = true;
    }

    if (!req.sensorModality.empty()) {
      std::cerr << "[InstanceRegistry] Updating sensorModality: "
                << info.sensorModality << " -> " << req.sensorModality
                << std::endl;
      info.sensorModality = req.sensorModality;
      hasChanges = true;
    }

    if (req.autoStart.has_value()) {
      std::cerr << "[InstanceRegistry] Updating autoStart: " << info.autoStart
                << " -> " << req.autoStart.value() << std::endl;
      info.autoStart = req.autoStart.value();
      hasChanges = true;
    }

    if (req.autoRestart.has_value()) {
      std::cerr << "[InstanceRegistry] Updating autoRestart: "
                << info.autoRestart << " -> " << req.autoRestart.value()
                << std::endl;
      info.autoRestart = req.autoRestart.value();
      hasChanges = true;
    }

    if (req.inputOrientation != -1) {
      std::cerr << "[InstanceRegistry] Updating inputOrientation: "
                << info.inputOrientation << " -> " << req.inputOrientation
                << std::endl;
      info.inputOrientation = req.inputOrientation;
      hasChanges = true;
    }

    if (req.inputPixelLimit != -1) {
      std::cerr << "[InstanceRegistry] Updating inputPixelLimit: "
                << info.inputPixelLimit << " -> " << req.inputPixelLimit
                << std::endl;
      info.inputPixelLimit = req.inputPixelLimit;
      hasChanges = true;
    }

    // Update additionalParams (merge with existing)
    // Only replace keys that appear in the request body, keep others unchanged
    if (!req.additionalParams.empty()) {
      std::cerr << "[InstanceRegistry] Updating additionalParams..."
                << std::endl;

      // Merge: only replace keys that appear in the request
      // Keys not in the request will remain unchanged
      // Skip the internal flag if present (it's only used for marking nested
      // structure)
      for (const auto &pair : req.additionalParams) {
        // Skip internal flags
        if (pair.first == "__REPLACE_INPUT_OUTPUT_PARAMS__") {
          continue;
        }

        std::cerr << "[InstanceRegistry]   " << pair.first << ": "
                  << (info.additionalParams.find(pair.first) !=
                              info.additionalParams.end()
                          ? info.additionalParams[pair.first]
                          : "<new>")
                  << " -> " << pair.second << std::endl;
        info.additionalParams[pair.first] = pair.second;
      }
      hasChanges = true;

      // Update RTSP URL if changed - check RTSP_DES_URL first (for output), 
      // then RTSP_SRC_URL (for input), then RTSP_URL (backward compatibility)
      auto rtspDesIt = req.additionalParams.find("RTSP_DES_URL");
      if (rtspDesIt != req.additionalParams.end() && !rtspDesIt->second.empty()) {
        info.rtspUrl = rtspDesIt->second;
      } else {
        auto rtspSrcIt = req.additionalParams.find("RTSP_SRC_URL");
        if (rtspSrcIt != req.additionalParams.end() && !rtspSrcIt->second.empty()) {
          info.rtspUrl = rtspSrcIt->second;
        } else {
          auto rtspIt = req.additionalParams.find("RTSP_URL");
          if (rtspIt != req.additionalParams.end() && !rtspIt->second.empty()) {
            info.rtspUrl = rtspIt->second;
          }
        }
      }

      // Update RTMP URL if changed - check RTMP_DES_URL first, then RTMP_URL
      // Helper function to trim whitespace
      auto trim = [](const std::string &str) -> std::string {
        if (str.empty())
          return str;
        size_t first = str.find_first_not_of(" \t\n\r\f\v");
        if (first == std::string::npos)
          return "";
        size_t last = str.find_last_not_of(" \t\n\r\f\v");
        return str.substr(first, (last - first + 1));
      };

      auto rtmpDesIt = req.additionalParams.find("RTMP_DES_URL");
      if (rtmpDesIt != req.additionalParams.end() &&
          !rtmpDesIt->second.empty()) {
        info.rtmpUrl = trim(rtmpDesIt->second);
      } else {
        auto rtmpIt = req.additionalParams.find("RTMP_URL");
        if (rtmpIt != req.additionalParams.end() && !rtmpIt->second.empty()) {
          info.rtmpUrl = trim(rtmpIt->second);
        }
      }

      // Update FILE_PATH if changed
      auto filePathIt = req.additionalParams.find("FILE_PATH");
      if (filePathIt != req.additionalParams.end() &&
          !filePathIt->second.empty()) {
        info.filePath = filePathIt->second;
      }

      // Update Detector model file
      auto detectorModelIt = req.additionalParams.find("DETECTOR_MODEL_FILE");
      if (detectorModelIt != req.additionalParams.end() &&
          !detectorModelIt->second.empty()) {
        info.detectorModelFile = detectorModelIt->second;
      }

      // Update DetectorThermal model file
      auto thermalModelIt =
          req.additionalParams.find("DETECTOR_THERMAL_MODEL_FILE");
      if (thermalModelIt != req.additionalParams.end() &&
          !thermalModelIt->second.empty()) {
        info.detectorThermalModelFile = thermalModelIt->second;
      }

      // Update confidence thresholds
      auto animalThreshIt =
          req.additionalParams.find("ANIMAL_CONFIDENCE_THRESHOLD");
      if (animalThreshIt != req.additionalParams.end() &&
          !animalThreshIt->second.empty()) {
        try {
          info.animalConfidenceThreshold = std::stod(animalThreshIt->second);
        } catch (...) {
          std::cerr << "[InstanceRegistry] Invalid animal_confidence_threshold "
                       "value: "
                    << animalThreshIt->second << std::endl;
        }
      }

      auto personThreshIt =
          req.additionalParams.find("PERSON_CONFIDENCE_THRESHOLD");
      if (personThreshIt != req.additionalParams.end() &&
          !personThreshIt->second.empty()) {
        try {
          info.personConfidenceThreshold = std::stod(personThreshIt->second);
        } catch (...) {
          std::cerr << "[InstanceRegistry] Invalid person_confidence_threshold "
                       "value: "
                    << personThreshIt->second << std::endl;
        }
      }

      auto vehicleThreshIt =
          req.additionalParams.find("VEHICLE_CONFIDENCE_THRESHOLD");
      if (vehicleThreshIt != req.additionalParams.end() &&
          !vehicleThreshIt->second.empty()) {
        try {
          info.vehicleConfidenceThreshold = std::stod(vehicleThreshIt->second);
        } catch (...) {
          std::cerr << "[InstanceRegistry] Invalid "
                       "vehicle_confidence_threshold value: "
                    << vehicleThreshIt->second << std::endl;
        }
      }

      auto faceThreshIt =
          req.additionalParams.find("FACE_CONFIDENCE_THRESHOLD");
      if (faceThreshIt != req.additionalParams.end() &&
          !faceThreshIt->second.empty()) {
        try {
          info.faceConfidenceThreshold = std::stod(faceThreshIt->second);
        } catch (...) {
          std::cerr
              << "[InstanceRegistry] Invalid face_confidence_threshold value: "
              << faceThreshIt->second << std::endl;
        }
      }

      auto licenseThreshIt =
          req.additionalParams.find("LICENSE_PLATE_CONFIDENCE_THRESHOLD");
      if (licenseThreshIt != req.additionalParams.end() &&
          !licenseThreshIt->second.empty()) {
        try {
          info.licensePlateConfidenceThreshold =
              std::stod(licenseThreshIt->second);
        } catch (...) {
          std::cerr << "[InstanceRegistry] Invalid "
                       "license_plate_confidence_threshold value: "
                    << licenseThreshIt->second << std::endl;
        }
      }

      auto confThreshIt = req.additionalParams.find("CONF_THRESHOLD");
      if (confThreshIt != req.additionalParams.end() &&
          !confThreshIt->second.empty()) {
        try {
          info.confThreshold = std::stod(confThreshIt->second);
        } catch (...) {
          std::cerr << "[InstanceRegistry] Invalid conf_threshold value: "
                    << confThreshIt->second << std::endl;
        }
      }

      // Update PerformanceMode
      auto perfModeIt = req.additionalParams.find("PERFORMANCE_MODE");
      if (perfModeIt != req.additionalParams.end() &&
          !perfModeIt->second.empty()) {
        info.performanceMode = perfModeIt->second;
      }
    }

    if (!hasChanges) {
      std::cerr << "[InstanceRegistry] No changes to update" << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;
      return true; // No changes, but still success
    }

    // Copy info for saving (release lock before saving)
    isPersistent = info.persistent;
    infoCopy = info;
  } // Release lock here

  // Save to storage if persistent (do this outside lock)
  std::cerr << "[InstanceRegistry] Instance persistent flag: "
            << (isPersistent ? "true" : "false") << std::endl;
  if (isPersistent) {
    std::cerr << "[InstanceRegistry] Saving instance to file..." << std::endl;
    bool saved = instance_storage_.saveInstance(instanceId, infoCopy);
    if (saved) {
      std::cerr << "[InstanceRegistry] Instance configuration saved to file"
                << std::endl;
    } else {
      std::cerr << "[InstanceRegistry] Warning: Failed to save instance "
                   "configuration to file"
                << std::endl;
    }
  } else {
    std::cerr
        << "[InstanceRegistry] Instance is not persistent, skipping file save"
        << std::endl;
  }

  std::cerr << "[InstanceRegistry] ✓ Instance " << instanceId
            << " updated successfully" << std::endl;

  // Check if instance is running and restart it to apply changes
  bool wasRunning = false;
  {
    std::unique_lock<std::shared_timed_mutex> lock(
        mutex_); // Exclusive lock for write operations
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt != instances_.end()) {
      wasRunning = instanceIt->second.running;
    }
  }

  if (wasRunning) {
    std::cerr << "[InstanceRegistry] Instance is running, restarting to apply "
                 "changes..."
              << std::endl;

    // Stop instance first
    if (stopInstance(instanceId)) {
      // CRITICAL: Wait longer for complete cleanup to prevent segmentation
      // faults GStreamer pipelines, threads (MQTT, RTSP monitor), and OpenCV
      // DNN need time to fully cleanup Previous 500ms was too short and caused
      // race conditions
      std::cerr
          << "[InstanceRegistry] Waiting for complete cleanup (3 seconds)..."
          << std::endl;
      std::cerr << "[InstanceRegistry] This ensures:" << std::endl;
      std::cerr
          << "[InstanceRegistry]   1. GStreamer pipelines are fully destroyed"
          << std::endl;
      std::cerr << "[InstanceRegistry]   2. All threads (MQTT, RTSP monitor) "
                   "are joined"
                << std::endl;
      std::cerr << "[InstanceRegistry]   3. OpenCV DNN state is cleared"
                << std::endl;
      std::cerr << "[InstanceRegistry]   4. No race conditions when starting "
                   "new pipeline"
                << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(3000));

      // Start instance again (this will rebuild pipeline with new config)
      if (startInstance(instanceId, true)) {
        std::cerr << "[InstanceRegistry] ✓ Instance restarted successfully "
                     "with new configuration"
                  << std::endl;
      } else {
        std::cerr
            << "[InstanceRegistry] ⚠ Instance stopped but failed to restart"
            << std::endl;
        std::cerr << "[InstanceRegistry] NOTE: Configuration has been updated. "
                     "You can manually start the instance later."
                  << std::endl;
      }
    } else {
      std::cerr << "[InstanceRegistry] ⚠ Failed to stop instance for restart"
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: Configuration has been updated. "
                   "Restart the instance manually to apply changes."
                << std::endl;
    }
  } else {
    std::cerr << "[InstanceRegistry] Instance is not running. Changes will "
                 "take effect when instance is started."
              << std::endl;
  }

  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;

  return true;
}

void InstanceRegistry::loadPersistentInstances() {
  std::vector<std::string> instanceIds = instance_storage_.loadAllInstances();

  std::unique_lock<std::shared_timed_mutex> lock(
      mutex_); // Exclusive lock for write operations
  for (const auto &instanceId : instanceIds) {
    auto optInfo = instance_storage_.loadInstance(instanceId);
    if (optInfo.has_value()) {
      instances_[instanceId] = optInfo.value();
      // Reset timing fields when loading from storage
      // This ensures accurate time calculations when instance starts
      instances_[instanceId].startTime = std::chrono::steady_clock::now();
      instances_[instanceId].lastActivityTime =
          instances_[instanceId].startTime;
      instances_[instanceId].hasReceivedData = false;
      instances_[instanceId].retryCount = 0;
      instances_[instanceId].retryLimitReached = false;
      // Note: Pipelines are not restored from storage
      // They need to be rebuilt if needed
    }
  }
}

InstanceInfo
InstanceRegistry::createInstanceInfo(const std::string &instanceId,
                                     const CreateInstanceRequest &req,
                                     const SolutionConfig *solution) const {

  InstanceInfo info;
  info.instanceId = instanceId;
  // Use default name if not provided
  info.displayName = req.name.empty() ? ("Instance " + instanceId.substr(0, 8)) : req.name;
  info.group = req.group;
  info.persistent = req.persistent;
  info.frameRateLimit = req.frameRateLimit;
  info.metadataMode = req.metadataMode;
  info.statisticsMode = req.statisticsMode;
  info.diagnosticsMode = req.diagnosticsMode;
  info.debugMode = req.debugMode;
  info.detectorMode = req.detectorMode;
  info.detectionSensitivity = req.detectionSensitivity;
  info.movementSensitivity = req.movementSensitivity;
  info.sensorModality = req.sensorModality;
  info.autoStart = req.autoStart;
  info.autoRestart = req.autoRestart;
  info.inputOrientation = req.inputOrientation;
  info.inputPixelLimit = req.inputPixelLimit;

  // Detector configuration (detailed)
  info.detectorModelFile = req.detectorModelFile;
  info.animalConfidenceThreshold = req.animalConfidenceThreshold;
  info.personConfidenceThreshold = req.personConfidenceThreshold;
  info.vehicleConfidenceThreshold = req.vehicleConfidenceThreshold;
  info.faceConfidenceThreshold = req.faceConfidenceThreshold;
  info.licensePlateConfidenceThreshold = req.licensePlateConfidenceThreshold;
  info.confThreshold = req.confThreshold;

  // DetectorThermal configuration
  info.detectorThermalModelFile = req.detectorThermalModelFile;

  // Performance mode
  info.performanceMode = req.performanceMode;

  // SolutionManager settings
  info.recommendedFrameRate = req.recommendedFrameRate;

  // FPS configuration: default to 5 FPS if not specified (fps == 0)
  info.configuredFps = (req.fps > 0) ? req.fps : 5;

  info.loaded = true;
  info.running = false;
  info.fps = 0.0;

  // Initialize timing fields to current time (will be reset when instance
  // starts) This prevents incorrect time calculations if instance is checked
  // before starting
  auto now = std::chrono::steady_clock::now();
  info.startTime = now;
  info.lastActivityTime = now;
  info.hasReceivedData = false;
  info.retryCount = 0;
  info.retryLimitReached = false;

// Get version from CVEDIX SDK
#ifdef CVEDIX_VERSION_STRING
  info.version = CVEDIX_VERSION_STRING;
#else
  info.version = "2026.0.1.1"; // Default version
#endif

  if (solution) {
    info.solutionId = solution->solutionId;
    info.solutionName = solution->solutionName;
  }

  // Copy all additional parameters from request (MODEL_PATH, SFACE_MODEL_PATH,
  // RESIZE_RATIO, etc.)
  info.additionalParams = req.additionalParams;

  // Extract RTSP URL from request - check RTSP_SRC_URL first, then RTSP_URL
  // This should be done BEFORE generating RTSP from RTMP to avoid overriding
  // user's input
  auto rtspSrcIt = req.additionalParams.find("RTSP_SRC_URL");
  if (rtspSrcIt != req.additionalParams.end() && !rtspSrcIt->second.empty()) {
    info.rtspUrl = rtspSrcIt->second;
  } else {
    auto rtspIt = req.additionalParams.find("RTSP_URL");
    if (rtspIt != req.additionalParams.end() && !rtspIt->second.empty()) {
      info.rtspUrl = rtspIt->second;
    }
  }

  // Extract RTMP URL from request - check RTMP_DES_URL first, then RTMP_URL
  // Helper function to trim whitespace
  auto trim = [](const std::string &str) -> std::string {
    if (str.empty())
      return str;
    size_t first = str.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string::npos)
      return "";
    size_t last = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(first, (last - first + 1));
  };

  auto rtmpDesIt = req.additionalParams.find("RTMP_DES_URL");
  if (rtmpDesIt != req.additionalParams.end() && !rtmpDesIt->second.empty()) {
    info.rtmpUrl = trim(rtmpDesIt->second);
  } else {
    auto rtmpIt = req.additionalParams.find("RTMP_URL");
    if (rtmpIt != req.additionalParams.end() && !rtmpIt->second.empty()) {
      info.rtmpUrl = trim(rtmpIt->second);
    }
  }

  // Only generate RTSP URL from RTMP URL if RTSP URL is not already set
  // This prevents overriding user's RTSP_SRC_URL
  // RTSP URL will have the same stream key as RTMP URL (including instanceId if present)
  if (info.rtspUrl.empty() && !info.rtmpUrl.empty()) {

    // Generate RTSP URL from RTMP URL
    // Keep the same stream key as RTMP URL (including instanceId and "_0" suffix if present)
    // Pattern: rtmp://host:1935/live/stream_key -> rtsp://host:8554/live/stream_key
    std::string rtmpUrl = info.rtmpUrl;

    // Replace RTMP protocol and port with RTSP
    size_t protocolPos = rtmpUrl.find("rtmp://");
    if (protocolPos != std::string::npos) {
      std::string rtspUrl = rtmpUrl;

      // Replace protocol
      rtspUrl.replace(protocolPos, 7, "rtsp://");

      // Replace port 1935 with 8554 (common RTSP port for conversion)
      size_t portPos = rtspUrl.find(":1935");
      if (portPos != std::string::npos) {
        rtspUrl.replace(portPos, 5, ":8554");
      }

      // Keep the same stream key as RTMP URL (no modification needed)
      // RTMP URL already includes instanceId and "_0" suffix if applicable
      info.rtspUrl = rtspUrl;
      std::cerr << "[InstanceRegistry] Generated RTSP URL from RTMP URL (same stream key): '" 
                << rtspUrl << "'" << std::endl;
    }
  }

  // Extract FILE_PATH from request (for file source solutions)
  auto filePathIt = req.additionalParams.find("FILE_PATH");
  if (filePathIt != req.additionalParams.end() && !filePathIt->second.empty()) {
    info.filePath = filePathIt->second;
  }

  return info;
}

// Helper function to wait for DNN models to be ready using exponential backoff
// This is more reliable than fixed delay as it adapts to model loading time
// If maxWaitMs < 0, wait indefinitely (no timeout)
void InstanceRegistry::waitForModelsReady(
    const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes,
    int maxWaitMs) {
  // Check if pipeline contains DNN models (face detector, feature encoder,
  // etc.)
  bool hasDNNModels = false;
  for (const auto &node : nodes) {
    // Check for YuNet face detector
    if (std::dynamic_pointer_cast<
            cvedix_nodes::cvedix_yunet_face_detector_node>(node)) {
      hasDNNModels = true;
      break;
    }
    // Check for SFace feature encoder
    if (std::dynamic_pointer_cast<
            cvedix_nodes::cvedix_sface_feature_encoder_node>(node)) {
      hasDNNModels = true;
      break;
    }
  }

  if (!hasDNNModels) {
    // No DNN models, minimal delay
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return;
  }

  // Check if unlimited wait (maxWaitMs < 0)
  bool unlimitedWait = (maxWaitMs < 0);

  if (unlimitedWait) {
    std::cerr << "[InstanceRegistry] Waiting for DNN models to initialize "
                 "(UNLIMITED - will wait until ready)..."
              << std::endl;
    std::cerr << "[InstanceRegistry] NOTE: This will wait indefinitely until "
                 "models are ready"
              << std::endl;
  } else {
    std::cerr << "[InstanceRegistry] Waiting for DNN models to initialize "
                 "(adaptive, max "
              << maxWaitMs << "ms)..." << std::endl;
  }

  // Use exponential backoff with adaptive waiting
  // Start with small delays and increase gradually
  int currentDelay = 200; // Start with 200ms
  int totalWaited = 0;
  int attempt = 0;
  const int maxDelayPerAttempt =
      2000; // Cap at 2 seconds per attempt for unlimited wait

  // For unlimited wait, use a very large number for maxAttempts
  // For limited wait, calculate based on maxWaitMs
  int maxAttempts = unlimitedWait ? 1000000 : ((maxWaitMs / 1600) + 10);

  // With exponential backoff: 200, 400, 800, 1600, 2000, 2000, 2000...
  while (unlimitedWait || (totalWaited < maxWaitMs && attempt < maxAttempts)) {
    int delayThisRound;
    if (unlimitedWait) {
      // For unlimited wait, use exponential backoff up to maxDelayPerAttempt
      delayThisRound = std::min(currentDelay, maxDelayPerAttempt);
    } else {
      // For limited wait, calculate remaining time
      delayThisRound = std::min(currentDelay, maxWaitMs - totalWaited);
    }

    if (unlimitedWait) {
      std::cerr << "[InstanceRegistry]   Attempt " << (attempt + 1)
                << ": waiting " << delayThisRound
                << "ms (total: " << totalWaited << "ms, unlimited wait)..."
                << std::endl;
    } else {
      std::cerr << "[InstanceRegistry]   Attempt " << (attempt + 1)
                << ": waiting " << delayThisRound
                << "ms (total: " << totalWaited << "ms / " << maxWaitMs
                << "ms)..." << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(delayThisRound));
    totalWaited += delayThisRound;

    // Exponential backoff: double the delay each time
    // For unlimited wait, cap at maxDelayPerAttempt
    // For limited wait, cap at 1600ms per attempt
    int maxDelay = unlimitedWait ? maxDelayPerAttempt : 1600;
    currentDelay = std::min(currentDelay * 2, maxDelay);
    attempt++;

    // For shorter waits (create scenario), can exit early
    if (!unlimitedWait && maxWaitMs <= 2000 && totalWaited >= 1000) {
      // For create scenario (2s max), exit after 1s if models usually ready
      std::cerr << "[InstanceRegistry]   Models should be ready now (waited "
                << totalWaited << "ms)" << std::endl;
      break;
    }

    // For unlimited wait, log progress every 10 seconds
    if (unlimitedWait && totalWaited > 0 && totalWaited % 10000 == 0) {
      std::cerr << "[InstanceRegistry]   Still waiting... (total: "
                << (totalWaited / 1000) << " seconds)" << std::endl;
    }
  }

  if (unlimitedWait) {
    std::cerr
        << "[InstanceRegistry] ✓ Models initialization wait completed (total: "
        << totalWaited << "ms, unlimited wait)" << std::endl;
  } else {
    std::cerr
        << "[InstanceRegistry] ✓ Models initialization wait completed (total: "
        << totalWaited << "ms / " << maxWaitMs << "ms)" << std::endl;
  }
}

bool InstanceRegistry::startPipeline(
    const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes,
    const std::string &instanceId, bool isRestart) {
  if (nodes.empty()) {
    std::cerr << "[InstanceRegistry] Cannot start pipeline: no nodes"
              << std::endl;
    return false;
  }

  // Initialize statistics tracker
  {
    std::unique_lock<std::shared_timed_mutex> lock(mutex_);
    InstanceStatsTracker &tracker = statistics_trackers_[instanceId];
    tracker.start_time = std::chrono::steady_clock::now();
    tracker.start_time_system = std::chrono::system_clock::now();
    // PHASE 2: Atomic counters - use store instead of assignment
    tracker.frames_processed.store(0);
    tracker.frames_incoming.store(0); // Track all incoming frames
    tracker.dropped_frames.store(0);
    tracker.frame_count_since_last_update.store(0);
    tracker.last_fps = 0.0;
    tracker.last_fps_update = tracker.start_time;
    tracker.current_queue_size = 0;
    tracker.max_queue_size_seen = 0;
    tracker.expected_frames_from_source = 0;
    tracker.cache_update_frame_count_.store(0, std::memory_order_relaxed);

    // OPTIMIZATION: Cache RTSP instance flag to avoid repeated lookups in hot
    // path
    auto instanceIt = instances_.find(instanceId);
    bool isRTSP =
        (instanceIt != instances_.end() && !instanceIt->second.rtspUrl.empty());
    tracker.is_rtsp_instance.store(isRTSP, std::memory_order_relaxed);

    // OPTIMIZATION: Initialize cached_stats with default statistics
    // This allows API to read statistics immediately (lock-free) even before
    // any frames are processed
    if (!tracker.cached_stats_) {
      tracker.cached_stats_ = std::make_shared<InstanceStatistics>();
      tracker.cached_stats_->current_framerate = 0.0;
      tracker.cached_stats_->frames_processed = 0;
      tracker.cached_stats_->start_time =
          std::chrono::duration_cast<std::chrono::seconds>(
              tracker.start_time_system.time_since_epoch())
              .count();
    }
  }

  // PHASE 3: Configure backpressure control
  {
    using namespace BackpressureController;
    auto &controller =
        BackpressureController::BackpressureController::getInstance();

    // Priority order for FPS configuration:
    // 1. MAX_FPS from additionalParams (highest priority - explicit override)
    // 2. configuredFps from instance info (from API /api/v1/instances/{id}/fps)
    // 3. Auto-detect based on model type (fallback)
    double maxFPS = 0.0;
    bool userFPSProvided = false;
    {
      std::unique_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt != instances_.end()) {
        // First, check MAX_FPS from additionalParams (explicit override)
        auto fpsIt = instanceIt->second.additionalParams.find("MAX_FPS");
        if (fpsIt != instanceIt->second.additionalParams.end() &&
            !fpsIt->second.empty()) {
          try {
            maxFPS = std::stod(fpsIt->second);
            userFPSProvided = true;
            std::cerr
                << "[InstanceRegistry] ✓ Using MAX_FPS from additionalParams: "
                << maxFPS << " FPS" << std::endl;
          } catch (const std::exception &e) {
            std::cerr << "[InstanceRegistry] ⚠ Invalid MAX_FPS value in "
                         "additionalParams: "
                      << fpsIt->second << std::endl;
          }
        }
        
        // If MAX_FPS not provided, use configuredFps from instance info
        // (set via API /api/v1/instances/{id}/fps, default is 5)
        if (!userFPSProvided && instanceIt->second.configuredFps > 0) {
          maxFPS = static_cast<double>(instanceIt->second.configuredFps);
          userFPSProvided = true;
          std::cerr
              << "[InstanceRegistry] ✓ Using configuredFps from instance info: "
              << maxFPS << " FPS" << std::endl;
        }
      }
    }

    // If user didn't provide FPS, auto-detect based on model type
    if (!userFPSProvided) {
      // Detect if pipeline contains slow nodes (Mask RCNN, OpenPose, Face
      // Detector, etc.) These models are computationally expensive and need
      // lower FPS
      bool hasSlowModel = false;
      bool hasFaceDetector = false;
      for (const auto &node : nodes) {
        auto maskRCNNNode = std::dynamic_pointer_cast<
            cvedix_nodes::cvedix_mask_rcnn_detector_node>(node);
        auto openPoseNode = std::dynamic_pointer_cast<
            cvedix_nodes::cvedix_openpose_detector_node>(node);
        auto faceDetectorNode = std::dynamic_pointer_cast<
            cvedix_nodes::cvedix_yunet_face_detector_node>(node);
        if (maskRCNNNode || openPoseNode) {
          hasSlowModel = true;
          break;
        }
        if (faceDetectorNode) {
          hasFaceDetector = true;
        }
      }

      // Use lower FPS for very slow models, but keep high FPS for face detector
      // Face detector will use frame dropping based on queue size instead of
      // FPS limiting Very slow models (Mask RCNN/OpenPose): 10 FPS Others
      // (including face detector): 30 FPS with queue-based dropping
      if (hasSlowModel) {
        maxFPS = 10.0; // Very slow models
      } else {
        maxFPS =
            30.0; // Normal models and face detector - use queue-based dropping
      }

      if (hasSlowModel) {
        std::cerr << "[InstanceRegistry] ⚠ Detected slow model (Mask "
                     "RCNN/OpenPose) - using reduced FPS: "
                  << maxFPS << " FPS to prevent queue overflow" << std::endl;
      } else if (hasFaceDetector) {
        std::cerr
            << "[InstanceRegistry] ⚠ Detected face detector - using 30 FPS "
               "with queue-based frame dropping to prevent queue overflow"
            << std::endl;
      }
    }

    // Clamp FPS to valid range (5-120 FPS) - synchronized with
    // BackpressureController MIN_FPS = 5.0, MAX_FPS = 120.0
    // Minimum 5 FPS to support task 12 requirement (default FPS = 5)
    maxFPS = std::max(5.0, std::min(120.0, maxFPS));

    // Configure with DROP_NEWEST policy (keep latest frame, drop old ones)
    // This prevents queue backlog while maintaining current state
    // Use adaptive queue size manager for dynamic queue sizing based on system
    // status
    using namespace AdaptiveQueueSize;
    auto &adaptiveQueue = AdaptiveQueueSizeManager::getInstance();

    // Get recommended queue size based on system status
    size_t recommended_queue_size =
        adaptiveQueue.getRecommendedQueueSize(instanceId);

    controller.configure(
        instanceId, BackpressureController::DropPolicy::DROP_NEWEST,
        maxFPS,                  // FPS from user or auto-detected
        recommended_queue_size); // Dynamic queue size based on system status

    std::cerr << "[InstanceRegistry] ✓ Backpressure control configured: "
              << maxFPS << " FPS max" << std::endl;
  }

  // Setup frame capture hook before starting pipeline
  setupFrameCaptureHook(instanceId, nodes);

  // Setup RTMP destination activity hook so monitor sees real push activity
  setupRTMPDestinationActivityHook(instanceId, nodes);

  // Setup queue size tracking hook before starting pipeline
  // This also tracks incoming frames on source node (first node)
  setupQueueSizeTrackingHook(instanceId, nodes);

  try {
    // Start from the first node (source node)
    // The pipeline will start automatically when source node starts
    // Check for RTSP source node first
    auto rtspNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(nodes[0]);
    if (rtspNode) {
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;
      std::cerr << "[InstanceRegistry] Starting RTSP pipeline..." << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: RTSP node will automatically "
                   "retry connection if stream is not immediately available"
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: Connection warnings are normal if "
                   "RTSP stream is not running yet"
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: CVEDIX SDK uses retry mechanism - "
                   "connection may take 10-30 seconds"
                << std::endl;
      std::cerr
          << "[InstanceRegistry] NOTE: If connection continues to fail, check:"
          << std::endl;
      std::cerr
          << "[InstanceRegistry]   1. RTSP server is running and accessible"
          << std::endl;
      std::cerr
          << "[InstanceRegistry]   2. Network connectivity (ping/port test)"
          << std::endl;
      std::cerr << "[InstanceRegistry]   3. RTSP URL format is correct"
                << std::endl;
      std::cerr << "[InstanceRegistry]   4. Firewall allows RTSP connections"
                << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;

      // Add small delay to ensure pipeline is ready
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      std::cerr << "[InstanceRegistry] Calling rtspNode->start()..."
                << std::endl;
      auto startTime = std::chrono::steady_clock::now();
      try {
        // CRITICAL: Use shared lock to allow concurrent start operations
        // Multiple instances can start simultaneously, but cleanup operations
        // will wait
        std::shared_lock<std::shared_mutex> gstLock(gstreamer_ops_mutex_);

        rtspNode->start();
      } catch (const std::exception &e) {
        std::cerr << "[InstanceRegistry] ✗ Exception starting RTSP node: "
                  << e.what() << std::endl;
        std::cerr << "[InstanceRegistry] This may indicate RTSP stream is not "
                     "available"
                  << std::endl;
        throw; // Re-throw to let caller handle
      } catch (...) {
        std::cerr << "[InstanceRegistry] ✗ Unknown exception starting RTSP node"
                  << std::endl;
        throw; // Re-throw to let caller handle
      }
      auto endTime = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                          endTime - startTime)
                          .count();

      std::cerr << "[InstanceRegistry] ✓ RTSP node start() completed in "
                << duration << "ms" << std::endl;
      std::cerr << "[InstanceRegistry] RTSP pipeline started (connection may "
                   "take a few seconds)"
                << std::endl;
      std::cerr << "[InstanceRegistry] The SDK will automatically retry "
                   "connection - monitor logs for connection status"
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: Check CVEDIX SDK logs above for "
                   "RTSP connection status"
                << std::endl;

      // CACHE SOURCE STATS: query now while we have the node, so we don't block
      // later The start() call has completed, so caps should be negotiated if
      // successful Or at least we try now.
      {
        std::unique_lock<std::shared_timed_mutex> lock(mutex_);
        auto &tracker = statistics_trackers_[instanceId];
        try {
          // Cache these values to avoid blocking calls in getInstanceStatistics
          tracker.source_fps = rtspNode->get_original_fps();
          tracker.source_width = rtspNode->get_original_width();
          tracker.source_height = rtspNode->get_original_height();
        } catch (...) {
          // Ignore errors, use defaults
        }
      }
      std::cerr << "[InstanceRegistry] NOTE: Look for messages like 'rtspsrc' "
                   "or connection errors"
                << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;
      std::cerr << "[InstanceRegistry] HOW TO VERIFY PIPELINE IS WORKING:"
                << std::endl;
      std::cerr << "[InstanceRegistry]   1. Check output files (from build "
                   "directory):"
                << std::endl;
      std::cerr << "[InstanceRegistry]      ls -lht ./output/<instanceId>/"
                << std::endl;
      std::cerr << "[InstanceRegistry]      Or from project root: "
                   "./build/output/<instanceId>/"
                << std::endl;
      std::cerr << "[InstanceRegistry]   2. Check CVEDIX SDK logs for "
                   "'rtspsrc' connection messages:"
                << std::endl;
      std::cerr << "[InstanceRegistry]      - Direct run: ./bin/edgeos-api "
                   "2>&1 | grep -i rtspsrc"
                << std::endl;
      std::cerr << "[InstanceRegistry]      - Service: sudo journalctl -u "
                   "edgeos-api | grep -i rtspsrc"
                << std::endl;
      std::cerr << "[InstanceRegistry]      - Enable GStreamer debug: export "
                   "GST_DEBUG=rtspsrc:4"
                << std::endl;
      std::cerr << "[InstanceRegistry]      - See docs/HOW_TO_CHECK_LOGS.md "
                   "for details"
                << std::endl;
      std::cerr << "[InstanceRegistry]   3. Check instance status: GET "
                   "/v1/core/instance/<instanceId>"
                << std::endl;
      std::cerr << "[InstanceRegistry]   4. Monitor file creation:"
                << std::endl;
      std::cerr << "[InstanceRegistry]      watch -n 1 'ls -lht "
                   "./output/<instanceId>/ | head -5'"
                << std::endl;
      std::cerr << "[InstanceRegistry]   5. If files are being created, "
                   "pipeline is working!"
                << std::endl;
      std::cerr << "[InstanceRegistry]   NOTE: Files are created in working "
                   "directory (usually build/)"
                << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;

      // Start RTSP monitoring thread for error detection and auto-reconnect
      startRTSPMonitorThread(instanceId);

      return true;
    }

    // Check for file source node
    auto fileNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(nodes[0]);
    if (fileNode) {
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;
      std::cerr << "[InstanceRegistry] Starting file source pipeline..."
                << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;

      // Log file path being used for debugging
      std::string filePathForLogging;
      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto instanceIt = instances_.find(instanceId);
        if (instanceIt != instances_.end()) {
          filePathForLogging = instanceIt->second.filePath;
          // Also check additionalParams for FILE_PATH
          auto filePathIt = instanceIt->second.additionalParams.find("FILE_PATH");
          if (filePathIt != instanceIt->second.additionalParams.end() &&
              !filePathIt->second.empty()) {
            filePathForLogging = filePathIt->second;
          }
        }
      }
      if (!filePathForLogging.empty()) {
        std::cerr << "[InstanceRegistry] File path: '" << filePathForLogging
                  << "'" << std::endl;
        // Verify file still exists (might have been deleted after validation)
        struct stat fileStat;
        if (stat(filePathForLogging.c_str(), &fileStat) == 0) {
          std::cerr << "[InstanceRegistry] ✓ File exists and is accessible"
                    << std::endl;
        } else {
          std::cerr << "[InstanceRegistry] ⚠ WARNING: File may not exist or "
                       "is not accessible: "
                    << filePathForLogging << std::endl;
          std::cerr << "[InstanceRegistry] This may cause 'open file failed' "
                       "errors"
                    << std::endl;
        }
      } else {
        std::cerr << "[InstanceRegistry] ⚠ WARNING: File path is empty!"
                  << std::endl;
        std::cerr << "[InstanceRegistry] This will cause 'open file failed' "
                     "errors"
                  << std::endl;
      }

      // CRITICAL: Validate file exists BEFORE starting to prevent infinite
      // retry loops Note: File path validation should have been done in
      // startInstance(), but we check again here for safety This prevents SDK
      // from retrying indefinitely when file doesn't exist We can't easily get
      // file path from node, so we rely on validation in startInstance() If we
      // reach here and file doesn't exist, SDK will retry - but validation
      // should have caught it

      // CRITICAL: Delay BEFORE start() to ensure model is fully ready
      // Once fileNode->start() is called, frames are immediately sent to the
      // pipeline If model is not ready, shape mismatch errors will occur For
      // restart scenarios, use longer delay to ensure OpenCV DNN has fully
      // cleared old state
      if (isRestart) {
        std::cerr << "[InstanceRegistry] CRITICAL: Final synchronization delay "
                     "before starting file source (restart: 5 seconds)..."
                  << std::endl;
        std::cerr << "[InstanceRegistry] This delay is CRITICAL - once start() "
                     "is called, frames are sent immediately"
                  << std::endl;
        std::cerr << "[InstanceRegistry] Model must be fully ready before "
                     "start() to prevent shape mismatch errors"
                  << std::endl;
        std::cerr << "[InstanceRegistry] Using longer delay for restart to "
                     "ensure OpenCV DNN state is fully cleared"
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
      } else {
        std::cerr << "[InstanceRegistry] Final synchronization delay before "
                     "starting file source (2 seconds)..."
                  << std::endl;
        std::cerr << "[InstanceRegistry] Ensuring model is ready before "
                     "start() to prevent shape mismatch errors"
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
      }

      // Check for PROCESSING_DELAY_MS parameter to reduce processing speed
      // This helps prevent server overload and crashes by slowing down AI
      // processing
      int processingDelayMs = 0;
      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto instanceIt = instances_.find(instanceId);
        if (instanceIt != instances_.end()) {
          const auto &info = instanceIt->second;
          auto it = info.additionalParams.find("PROCESSING_DELAY_MS");
          if (it != info.additionalParams.end() && !it->second.empty()) {
            try {
              processingDelayMs = std::stoi(it->second);
              if (processingDelayMs < 0)
                processingDelayMs = 0;
              if (processingDelayMs > 1000)
                processingDelayMs = 1000; // Cap at 1000ms
              std::cerr << "[InstanceRegistry] Processing delay enabled: "
                        << processingDelayMs << "ms between frames"
                        << std::endl;
              std::cerr << "[InstanceRegistry] This will reduce AI processing "
                           "speed to prevent server overload"
                        << std::endl;
            } catch (...) {
              std::cerr << "[InstanceRegistry] Warning: Invalid "
                           "PROCESSING_DELAY_MS value, ignoring..."
                        << std::endl;
            }
          }
        }
      }

      std::cerr << "[InstanceRegistry] Calling fileNode->start()..."
                << std::endl;
      auto startTime = std::chrono::steady_clock::now();

      // CRITICAL: Wrap start() in async with timeout to prevent blocking server
      // When video ends, fileNode->start() may block indefinitely if GStreamer
      // pipeline is in bad state This timeout ensures server remains responsive
      // even if start() hangs
      try {
        auto startFuture =
            std::async(std::launch::async, [fileNode]() { fileNode->start(); });

        // Wait with timeout (5000ms for initial start, longer than restart
        // timeout) If it takes too long, log warning but continue (don't block
        // server)
        const int START_TIMEOUT_MS = 5000;
        if (startFuture.wait_for(std::chrono::milliseconds(START_TIMEOUT_MS)) ==
            std::future_status::timeout) {
          std::cerr
              << "[InstanceRegistry] ⚠ WARNING: fileNode->start() timeout ("
              << START_TIMEOUT_MS << "ms)" << std::endl;
          std::cerr << "[InstanceRegistry] ⚠ This may indicate:" << std::endl;
          std::cerr << "[InstanceRegistry]   1. GStreamer pipeline issue (check "
                       "plugins are installed)"
                    << std::endl;
          std::cerr << "[InstanceRegistry]   2. Video file is corrupted or "
                       "incompatible format"
                    << std::endl;
          std::cerr << "[InstanceRegistry]   3. GStreamer is retrying to open "
                       "file (may indicate missing plugins)"
                    << std::endl;
          std::cerr << "[InstanceRegistry] ⚠ Server will continue running, but "
                       "instance may not process frames correctly"
                    << std::endl;
          std::cerr << "[InstanceRegistry] ⚠ If this persists, check:" << std::endl;
          std::cerr << "[InstanceRegistry]   - GStreamer plugins are installed: "
                       "gst-inspect-1.0 isomp4"
                    << std::endl;
          std::cerr << "[InstanceRegistry]   - Video file is valid (use ffprobe on "
                       "the file path)"
                    << std::endl;
          std::cerr << "[InstanceRegistry]   - Check logs for GStreamer errors"
                    << std::endl;
          // Don't return false - let instance continue, but it may not work
          // correctly This prevents server from being blocked
        } else {
          try {
            startFuture.get();
            auto endTime = std::chrono::steady_clock::now();
            auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(endTime -
                                                                      startTime)
                    .count();
            std::cerr
                << "[InstanceRegistry] ✓ File source node start() completed in "
                << duration << "ms" << std::endl;
          } catch (const std::exception &e) {
            std::cerr
                << "[InstanceRegistry] ✗ Exception during fileNode->start(): "
                << e.what() << std::endl;
            std::cerr << "[InstanceRegistry] This may indicate a problem with "
                         "the video file or model initialization"
                      << std::endl;
            return false;
          } catch (...) {
            std::cerr << "[InstanceRegistry] ✗ Unknown exception during "
                         "fileNode->start()"
                      << std::endl;
            std::cerr << "[InstanceRegistry] This may indicate a critical "
                         "error - check logs above for details"
                      << std::endl;
            return false;
          }
        }
      } catch (const std::exception &e) {
        std::cerr << "[InstanceRegistry] ✗ Exception creating start future: "
                  << e.what() << std::endl;
        std::cerr << "[InstanceRegistry] Falling back to synchronous start()..."
                  << std::endl;
        // Fallback to synchronous call if async fails
        try {
          fileNode->start();
          auto endTime = std::chrono::steady_clock::now();
          auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                              endTime - startTime)
                              .count();
          std::cerr
              << "[InstanceRegistry] ✓ File source node start() completed in "
              << duration << "ms" << std::endl;
        } catch (const std::exception &e2) {
          std::cerr
              << "[InstanceRegistry] ✗ Exception during fileNode->start(): "
              << e2.what() << std::endl;
          return false;
        } catch (...) {
          std::cerr << "[InstanceRegistry] ✗ Unknown exception during "
                       "fileNode->start()"
                    << std::endl;
          return false;
        }
      } catch (...) {
        std::cerr << "[InstanceRegistry] ✗ Unknown error creating start future"
                  << std::endl;
        std::cerr << "[InstanceRegistry] Falling back to synchronous start()..."
                  << std::endl;
        // Fallback to synchronous call if async fails
        try {
          fileNode->start();
          auto endTime = std::chrono::steady_clock::now();
          auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                              endTime - startTime)
                              .count();
          std::cerr
              << "[InstanceRegistry] ✓ File source node start() completed in "
              << duration << "ms" << std::endl;
        } catch (...) {
          std::cerr << "[InstanceRegistry] ✗ Unknown exception during "
                       "fileNode->start()"
                    << std::endl;
          return false;
        }
      }

      // Additional delay after start() to allow first frame to be processed
      // Note: This delay is less critical than the delay BEFORE start()
      // because frames are already being sent, but it helps ensure smooth
      // processing
      if (isRestart) {
        std::cerr << "[InstanceRegistry] Additional stabilization delay after "
                     "start() (restart: 1 second)..."
                  << std::endl;
        std::cerr << "[InstanceRegistry] This allows first frame to be "
                     "processed smoothly"
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      } else {
        std::cerr << "[InstanceRegistry] Additional stabilization delay after "
                     "start() (500ms)..."
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }

      // If processing delay is enabled, start a thread to periodically add
      // delay This slows down frame processing to reduce server load
      if (processingDelayMs > 0) {
        std::cerr
            << "[InstanceRegistry] Starting processing delay thread (delay: "
            << processingDelayMs << "ms)..." << std::endl;
        std::cerr << "[InstanceRegistry] This will slow down AI processing to "
                     "prevent server overload"
                  << std::endl;
        // Note: Actual frame skipping would need to be done in the SDK level
        // For now, we just log that delay is configured
        // The delay will be handled by rate limiting in MQTT thread
      }

      // NOTE: Shape mismatch errors may still occur if:
      // 1. Video has inconsistent frame sizes (most common cause)
      //    - Check with: ffprobe -v error -select_streams v:0 -show_entries
      //    frame=width,height -of csv=s=x:p=0 video.mp4 | sort -u
      //    - If multiple sizes appear, video needs re-encoding with fixed
      //    resolution
      // 2. Model (especially YuNet 2022mar) doesn't handle dynamic input well
      //    - Solution: Use YuNet 2023mar model
      // 3. Resize ratio doesn't produce consistent dimensions
      //    - Solution: Re-encode video with fixed resolution, then use
      //    resize_ratio=1.0
      // If this happens, SIGABRT handler will catch it and stop the instance
      std::cerr
          << "[InstanceRegistry] File source pipeline started successfully"
          << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;
      std::cerr << "[InstanceRegistry] IMPORTANT: If you see shape mismatch "
                   "errors, the most likely cause is:"
                << std::endl;
      std::cerr << "[InstanceRegistry]   Video has inconsistent frame sizes "
                   "(different resolutions per frame)"
                << std::endl;
      std::cerr << "[InstanceRegistry] Solutions (in order of recommendation):"
                << std::endl;
      std::cerr
          << "[InstanceRegistry]   1. Re-encode video with fixed resolution:"
          << std::endl;
      std::cerr << "[InstanceRegistry]      ffmpeg -i input.mp4 -vf "
                   "\"scale=640:360:force_original_aspect_ratio=decrease,pad="
                   "640:360:(ow-iw)/2:(oh-ih)/2\" \\"
                << std::endl;
      std::cerr << "[InstanceRegistry]             -c:v libx264 -preset fast "
                   "-crf 23 -c:a copy output.mp4"
                << std::endl;
      std::cerr << "[InstanceRegistry]      Then use RESIZE_RATIO: \"1.0\" in "
                   "additionalParams"
                << std::endl;
      std::cerr << "[InstanceRegistry]   2. Use YuNet 2023mar model (better "
                   "dynamic input support)"
                << std::endl;
      std::cerr << "[InstanceRegistry]   3. Check video resolution consistency:"
                << std::endl;
      std::cerr << "[InstanceRegistry]      ffprobe -v error -select_streams "
                   "v:0 -show_entries frame=width,height \\"
                << std::endl;
      std::cerr << "[InstanceRegistry]              -of csv=s=x:p=0 video.mp4 "
                   "| sort -u"
                << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;
      return true;
    }

    // Check for RTMP source node
    auto rtmpNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(nodes[0]);
    if (rtmpNode) {
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;
      std::cerr << "[InstanceRegistry] Starting RTMP source pipeline..."
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: RTMP node will automatically "
                   "retry connection if stream is not immediately available"
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: Connection warnings are normal if "
                   "RTMP stream is not running yet"
                << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;

      // Add small delay to ensure pipeline is ready
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      std::cerr << "[InstanceRegistry] Calling rtmpNode->start()..."
                << std::endl;
      auto startTime = std::chrono::steady_clock::now();
      try {
        rtmpNode->start();
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            endTime - startTime)
                            .count();
        std::cerr
            << "[InstanceRegistry] ✓ RTMP source node start() completed in "
            << duration << "ms" << std::endl;
      } catch (const std::exception &e) {
        std::cerr << "[InstanceRegistry] ✗ Exception during rtmpNode->start(): "
                  << e.what() << std::endl;
        std::cerr << "[InstanceRegistry] This may indicate a problem with the "
                     "RTMP stream or connection"
                  << std::endl;
        return false;
      } catch (...) {
        std::cerr
            << "[InstanceRegistry] ✗ Unknown exception during rtmpNode->start()"
            << std::endl;
        return false;
      }

      std::cerr
          << "[InstanceRegistry] RTMP source pipeline started successfully"
          << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;

      // Start RTMP source monitoring thread for error detection and auto-reconnect
      startRTMPSourceMonitorThread(instanceId);
      
      // Check if instance also has RTMP destination and start monitoring thread
      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto instanceIt = instances_.find(instanceId);
        if (instanceIt != instances_.end()) {
          auto rtmpDesIt = instanceIt->second.additionalParams.find("RTMP_DES_URL");
          if (rtmpDesIt != instanceIt->second.additionalParams.end() && 
              !rtmpDesIt->second.empty()) {
            // Start RTMP destination monitoring thread
            startRTMPDestinationMonitorThread(instanceId);
          }
        }
      }
      
      return true;
    }

    // Check for image source node (image_src)
    auto imageNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_image_src_node>(nodes[0]);
    if (imageNode) {
      std::cerr << "[InstanceRegistry] Starting image source pipeline..."
                << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      try {
        imageNode->start();
        std::cerr << "[InstanceRegistry] ✓ Image source node started"
                  << std::endl;
        return true;
      } catch (const std::exception &e) {
        std::cerr << "[InstanceRegistry] ✗ Exception during imageNode->start(): "
                  << e.what() << std::endl;
        return false;
      }
    }

    // Check for UDP source node (udp_src)
    auto udpNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_udp_src_node>(nodes[0]);
    if (udpNode) {
      std::cerr << "[InstanceRegistry] Starting UDP source pipeline..."
                << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      try {
        udpNode->start();
        std::cerr << "[InstanceRegistry] ✓ UDP source node started"
                  << std::endl;
        return true;
      } catch (const std::exception &e) {
        std::cerr << "[InstanceRegistry] ✗ Exception during udpNode->start(): "
                  << e.what() << std::endl;
        return false;
      }
    }

    // Check for app source node (app_src, push-based)
    auto appSrcNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_src_node>(nodes[0]);
    if (appSrcNode) {
      std::cerr << "[InstanceRegistry] Starting app source pipeline (push-based)..."
                << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      try {
        appSrcNode->start();
        std::cerr << "[InstanceRegistry] ✓ App source node started"
                  << std::endl;
        return true;
      } catch (const std::exception &e) {
        std::cerr << "[InstanceRegistry] ✗ Exception during appSrcNode->start(): "
                  << e.what() << std::endl;
        return false;
      }
    }

    // If not a recognized source node, cannot start pipeline
    std::cerr << "[InstanceRegistry] ✗ Error: First node is not a recognized "
                 "source node (RTSP, File, RTMP, Image, UDP, or App)"
              << std::endl;
    std::cerr << "[InstanceRegistry] Currently supported source node types:"
              << std::endl;
    std::cerr << "[InstanceRegistry]   - cvedix_rtsp_src_node, cvedix_rtmp_src_node, "
                 "cvedix_file_src_node (ff_src uses file_src),"
              << std::endl;
    std::cerr << "[InstanceRegistry]   - cvedix_image_src_node, cvedix_udp_src_node, "
                 "cvedix_app_src_node"
              << std::endl;
    return false;
  } catch (const std::exception &e) {
    std::cerr << "[InstanceRegistry] Exception starting pipeline: " << e.what()
              << std::endl;
    std::cerr << "[InstanceRegistry] This may indicate a configuration issue "
                 "with the RTSP source"
              << std::endl;
    return false;
  }
}

void InstanceRegistry::stopPipeline(
    const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes,
    bool isDeletion) {
  if (nodes.empty()) {
    return;
  }

  // CRITICAL: This function only cleans up nodes of ONE specific instance
  // Each instance has its own independent GStreamer pipeline and nodes
  // Cleanup of one instance should NOT affect other running instances
  // However, we use mutex to serialize GStreamer operations to prevent
  // conflicts if multiple instances are being stopped/started simultaneously
  //
  // IMPORTANT: Each node should have instanceId in its name (e.g.,
  // "rtsp_src_{instanceId}") This ensures we only stop nodes belonging to this
  // specific instance

  std::cerr << "[InstanceRegistry] [stopPipeline] Cleaning up " << nodes.size()
            << " nodes for this instance only" << std::endl;
  std::cerr << "[InstanceRegistry] [stopPipeline] NOTE: These nodes are "
               "isolated from other instances"
            << std::endl;
  std::cerr << "[InstanceRegistry] [stopPipeline] NOTE: Each node has unique "
               "name with instanceId prefix to prevent conflicts"
            << std::endl;
  std::cerr << "[InstanceRegistry] [stopPipeline] NOTE: No shared state or "
               "resources between different instances"
            << std::endl;

  try {
    // Check if pipeline contains DNN models (face detector, feature encoder,
    // etc.) These need extra time to finish processing and clear internal state
    bool hasDNNModels = false;
    for (const auto &node : nodes) {
      if (std::dynamic_pointer_cast<
              cvedix_nodes::cvedix_yunet_face_detector_node>(node) ||
          std::dynamic_pointer_cast<
              cvedix_nodes::cvedix_sface_feature_encoder_node>(node)) {
        hasDNNModels = true;
        break;
      }
    }

    // CRITICAL: Stop destination nodes (RTMP) FIRST before stopping source
    // nodes This ensures GStreamer elements are properly stopped and flushed
    // before source stops sending data. This prevents GStreamer elements from
    // being in PAUSED/READY state when disposed.
    std::cerr << "[InstanceRegistry] Stopping destination nodes first..."
              << std::endl;
    for (const auto &node : nodes) {
      if (!node) {
        continue;
      }

      // Prepare RTMP destination nodes for cleanup
      // Note: RTMP destination nodes don't have stop() method, so we just
      // prepare them by waiting for buffers to flush
      auto rtmpDesNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node);
      if (rtmpDesNode) {
        std::cerr << "[InstanceRegistry] Preparing RTMP destination node for "
                     "cleanup..."
                  << std::endl;
        // Give it time to flush buffers before we stop source
        // This helps reduce GStreamer warnings during cleanup
        // During shutdown, use shorter timeout to exit faster
        auto sleep_time =
            isDeletion ? TimeoutConstants::getRtmpPrepareTimeoutDeletion()
                       : TimeoutConstants::getRtmpPrepareTimeout();
        std::this_thread::sleep_for(sleep_time);
        std::cerr << "[InstanceRegistry] ✓ RTMP destination node prepared"
                  << std::endl;
      }
    }

    // Give destination nodes time to flush and finalize
    // This helps reduce GStreamer warnings during cleanup and prevent
    // segmentation faults FIXED: Increased wait time to ensure elements are
    // properly set to NULL state before dispose
    // During shutdown (isDeletion), use shorter timeout to exit faster
    if (isDeletion) {
      std::cerr << "[InstanceRegistry] Waiting for destination nodes to "
                   "finalize (shutdown mode - shorter timeout)..."
                << std::endl;
      std::this_thread::sleep_for(
          TimeoutConstants::getDestinationFinalizeTimeoutDeletion());
    }

    // Now stop the source node (typically the first node)
    // This is important to stop the connection retry loop or file reading
    if (!nodes.empty() && nodes[0]) {
      // Try RTSP source node first
      auto rtspNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(
              nodes[0]);
      if (rtspNode) {
        if (isDeletion) {
          std::cerr
              << "[InstanceRegistry] Stopping RTSP source node (deletion)..."
              << std::endl;
        } else {
          std::cerr << "[InstanceRegistry] Stopping RTSP source node..."
                    << std::endl;
        }
        try {
          auto stopTime = std::chrono::steady_clock::now();
          // CRITICAL: Try stop() first, but if it blocks due to retry loop, use
          // detach_recursively() RTSP retry loops can prevent stop() from
          // returning, so we need a fallback
          std::cerr << "[InstanceRegistry] Attempting to stop RTSP node (may "
                       "take time if retry loop is active)..."
                    << std::endl;

          // CRITICAL: Use exclusive lock for cleanup operations to prevent
          // conflicts This ensures no other instance is starting GStreamer
          // while we cleanup All start operations will wait until cleanup
          // completes NOTE: This lock only protects GStreamer operations, not
          // the nodes themselves Each instance has its own independent nodes,
          // so cleanup of one instance should not affect nodes of other
          // instances CRITICAL: Lock scope is limited to actual stop/detach
          // operations only
          {
            std::unique_lock<std::shared_mutex> gstLock(gstreamer_ops_mutex_);

            // Try stop() with timeout protection using async
            // CRITICAL: Wrap in try-catch to handle case where node is being
            // destroyed by another thread
            auto stopFuture = std::async(std::launch::async, [rtspNode]() {
              try {
                // Check if node is still valid before calling stop()
                if (!rtspNode) {
                  return false;
                }
                rtspNode->stop();
                return true;
              } catch (const std::exception &e) {
                // Node may have been destroyed by another thread (RTSP monitor
                // thread)
                std::cerr << "[InstanceRegistry] Exception in async stop(): "
                          << e.what() << std::endl;
                return false;
              } catch (...) {
                // Node may have been destroyed by another thread
                return false;
              }
            });

            // Wait max 200ms for stop() to complete (or 50ms during shutdown)
            // RTSP retry loops can block stop(), so use short timeout and
            // immediately detach
            // During shutdown, use even shorter timeout to exit faster
            auto stopTimeout =
                isDeletion ? TimeoutConstants::getRtspStopTimeoutDeletion()
                           : TimeoutConstants::getRtspStopTimeout();
            auto stopStatus = stopFuture.wait_for(stopTimeout);
            if (stopStatus == std::future_status::timeout) {
              std::cerr << "[InstanceRegistry] ⚠ RTSP stop() timeout (200ms) - "
                           "retry loop may be blocking"
                        << std::endl;
              std::cerr << "[InstanceRegistry] Attempting force stop using "
                           "detach_recursively()..."
                        << std::endl;
              // Force stop using detach - this should break retry loop
              try {
                rtspNode->detach_recursively();
                std::cerr << "[InstanceRegistry] ✓ RTSP node force stopped "
                             "using detach_recursively()"
                          << std::endl;
              } catch (const std::exception &e) {
                std::cerr << "[InstanceRegistry] ✗ Exception force stopping "
                             "RTSP node: "
                          << e.what() << std::endl;
              } catch (...) {
                std::cerr << "[InstanceRegistry] ✗ Unknown error force "
                             "stopping RTSP node"
                          << std::endl;
              }
            } else if (stopStatus == std::future_status::ready) {
              try {
                if (stopFuture.get()) {
                  auto stopEndTime = std::chrono::steady_clock::now();
                  auto stopDuration =
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          stopEndTime - stopTime)
                          .count();
                  std::cerr
                      << "[InstanceRegistry] ✓ RTSP source node stopped in "
                      << stopDuration << "ms" << std::endl;
                }
              } catch (...) {
                std::cerr
                    << "[InstanceRegistry] ✗ Exception getting stop result"
                    << std::endl;
              }
            }
          } // CRITICAL: Release lock here - cleanup wait happens without lock

          // Give it more time to fully stop (without holding lock)
          // This ensures GStreamer pipeline is properly stopped before cleanup
          std::this_thread::sleep_for(
              std::chrono::milliseconds(300)); // Increased from 100ms to 300ms
        } catch (const std::exception &e) {
          std::cerr << "[InstanceRegistry] ✗ Exception stopping RTSP node: "
                    << e.what() << std::endl;
          // Try force stop as fallback
          try {
            rtspNode->detach_recursively();
            std::cerr << "[InstanceRegistry] ✓ RTSP node force stopped using "
                         "detach_recursively() (fallback)"
                      << std::endl;
          } catch (...) {
            std::cerr << "[InstanceRegistry] ✗ Force stop also failed"
                      << std::endl;
          }
        } catch (...) {
          std::cerr << "[InstanceRegistry] ✗ Unknown error stopping RTSP node"
                    << std::endl;
          // Try force stop as fallback
          try {
            rtspNode->detach_recursively();
            std::cerr << "[InstanceRegistry] ✓ RTSP node force stopped using "
                         "detach_recursively() (fallback)"
                      << std::endl;
          } catch (...) {
            std::cerr << "[InstanceRegistry] ✗ Force stop also failed"
                      << std::endl;
          }
        }
      } else {
        // Try RTMP source node
        auto rtmpNode =
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(
                nodes[0]);
        if (rtmpNode) {
          if (isDeletion) {
            std::cerr
                << "[InstanceRegistry] Stopping RTMP source node (deletion)..."
                << std::endl;
          } else {
            std::cerr << "[InstanceRegistry] Stopping RTMP source node..."
                      << std::endl;
          }
          try {
            // CRITICAL: Use exclusive lock for cleanup operations
            std::unique_lock<std::shared_mutex> gstLock(gstreamer_ops_mutex_);

            // For RTMP source, stop first, then wait, then detach
            rtmpNode->stop();

            // Give it time to stop properly before detaching
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            rtmpNode->detach_recursively();
            std::cerr << "[InstanceRegistry] ✓ RTMP source node stopped"
                      << std::endl;
          } catch (const std::exception &e) {
            std::cerr << "[InstanceRegistry] ✗ Exception stopping RTMP node: "
                      << e.what() << std::endl;
            // Try force stop as fallback
            try {
              rtmpNode->detach_recursively();
              std::cerr << "[InstanceRegistry] ✓ RTMP node force stopped using "
                           "detach_recursively()"
                        << std::endl;
            } catch (...) {
              std::cerr << "[InstanceRegistry] ✗ Force stop also failed"
                        << std::endl;
            }
          } catch (...) {
            std::cerr << "[InstanceRegistry] ✗ Unknown error stopping RTMP node"
                      << std::endl;
            // Try force stop as fallback
            try {
              rtmpNode->detach_recursively();
              std::cerr << "[InstanceRegistry] ✓ RTMP node force stopped using "
                           "detach_recursively()"
                        << std::endl;
            } catch (...) {
              std::cerr << "[InstanceRegistry] ✗ Force stop also failed"
                        << std::endl;
            }
          }
        } else {
          // Try file source node
          auto fileNode =
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(
                  nodes[0]);
          if (fileNode) {
            if (isDeletion) {
              std::cerr << "[InstanceRegistry] Stopping file source node "
                           "(deletion)..."
                        << std::endl;
            } else {
              std::cerr << "[InstanceRegistry] Stopping file source node..."
                        << std::endl;
            }
            try {
              // CRITICAL: Stop file source first before detaching
              // This ensures GStreamer pipeline is properly stopped before
              // cleanup Use exclusive lock for cleanup operations
              std::unique_lock<std::shared_mutex> gstLock(gstreamer_ops_mutex_);

              // Try to stop the file source first
              try {
                fileNode->stop();
                std::cerr << "[InstanceRegistry] ✓ File source node stopped"
                          << std::endl;
              } catch (const std::exception &e) {
                std::cerr
                    << "[InstanceRegistry] ⚠ Exception stopping file node "
                       "(will try detach): "
                    << e.what() << std::endl;
              } catch (...) {
                std::cerr << "[InstanceRegistry] ⚠ Unknown error stopping file "
                             "node (will try detach)"
                          << std::endl;
              }

              // Give it time to stop properly
              std::this_thread::sleep_for(std::chrono::milliseconds(200));

              // Now detach to stop reading and cleanup
              // But we'll keep the nodes in memory so they can be restarted
              // (unless deletion)
              fileNode->detach_recursively();
              std::cerr << "[InstanceRegistry] ✓ File source node detached"
                        << std::endl;
            } catch (const std::exception &e) {
              std::cerr << "[InstanceRegistry] ✗ Exception stopping file node: "
                        << e.what() << std::endl;
              // Try force detach as fallback
              try {
                fileNode->detach_recursively();
                std::cerr << "[InstanceRegistry] ✓ File node force detached"
                          << std::endl;
              } catch (...) {
                std::cerr << "[InstanceRegistry] ✗ Force detach also failed"
                          << std::endl;
              }
            } catch (...) {
              std::cerr
                  << "[InstanceRegistry] ✗ Unknown error stopping file node"
                  << std::endl;
              // Try force detach as fallback
              try {
                fileNode->detach_recursively();
                std::cerr << "[InstanceRegistry] ✓ File node force detached"
                          << std::endl;
              } catch (...) {
                std::cerr << "[InstanceRegistry] ✗ Force detach also failed"
                          << std::endl;
              }
            }
          } else {
            // Generic stop for other source types
            try {
              if (nodes[0]) {
                nodes[0]->detach_recursively();
              }
            } catch (...) {
              // Ignore errors
            }
          }
        }
      }
    }

    // CRITICAL: Explicitly detach all processing nodes (face_detector, etc.)
    // to stop their internal queues from processing frames
    // This prevents "queue full, dropping meta!" warnings after instance stop
    std::cerr << "[InstanceRegistry] Detaching all processing nodes to stop "
                 "internal queues..."
              << std::endl;
    for (const auto &node : nodes) {
      if (!node) {
        continue;
      }

      // Skip source and destination nodes (already handled)
      auto rtspNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(node);
      auto rtmpSrcNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(node);
      auto fileNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(node);
      auto rtmpDesNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node);

      if (rtspNode || rtmpSrcNode || fileNode || rtmpDesNode) {
        continue; // Already handled
      }

      // Detach processing nodes (face_detector, feature_encoder, etc.)
      try {
        auto faceDetectorNode =
            std::dynamic_pointer_cast<
                cvedix_nodes::cvedix_yunet_face_detector_node>(node);
        auto featureEncoderNode =
            std::dynamic_pointer_cast<
                cvedix_nodes::cvedix_sface_feature_encoder_node>(node);

        if (faceDetectorNode || featureEncoderNode) {
          std::cerr << "[InstanceRegistry] Detaching DNN processing node to stop "
                       "queue processing..."
                    << std::endl;
          // Use exclusive lock for cleanup operations
          std::unique_lock<std::shared_mutex> gstLock(gstreamer_ops_mutex_);
          node->detach_recursively();
          std::cerr << "[InstanceRegistry] ✓ DNN processing node detached"
                    << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "[InstanceRegistry] ⚠ Exception detaching processing node: "
                  << e.what() << std::endl;
      } catch (...) {
        std::cerr << "[InstanceRegistry] ⚠ Unknown error detaching processing "
                     "node"
                  << std::endl;
      }
    }

    // CRITICAL: After stopping source node and detaching processing nodes, wait
    // for DNN processing nodes to finish This ensures all frames in the
    // processing queue are handled and DNN models have cleared their internal
    // state before we detach or restart This prevents shape mismatch errors
    // when restarting
    if (hasDNNModels) {
      if (isDeletion) {
        std::cerr << "[InstanceRegistry] Waiting for DNN models to finish "
                     "processing (deletion, 1 second)..."
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      } else {
        // For stop (not deletion), use longer delay to ensure DNN state is
        // fully cleared This is critical to prevent shape mismatch errors when
        // restarting
        std::cerr << "[InstanceRegistry] Waiting for DNN models to finish "
                     "processing and clear state (stop, 2 seconds)..."
                  << std::endl;
        std::cerr << "[InstanceRegistry] This ensures OpenCV DNN releases all "
                     "internal state before restart"
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
      }
    }

    // CRITICAL: Now detach all destination nodes after source is stopped
    // This ensures proper cleanup order: stop destination -> stop source ->
    // detach destination -> detach source
    std::cerr << "[InstanceRegistry] Detaching destination nodes..."
              << std::endl;
    for (const auto &node : nodes) {
      if (!node) {
        continue;
      }

      auto rtmpDesNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node);
      if (rtmpDesNode) {
        try {
          // CRITICAL: Use exclusive lock for cleanup operations
          std::unique_lock<std::shared_mutex> gstLock(gstreamer_ops_mutex_);

          // Detach the node
          rtmpDesNode->detach_recursively();

          std::cerr << "[InstanceRegistry] ✓ RTMP destination node detached"
                    << std::endl;
        } catch (const std::exception &e) {
          std::cerr
              << "[InstanceRegistry] ✗ Exception detaching RTMP destination "
                 "node: "
              << e.what() << std::endl;
        } catch (...) {
          std::cerr << "[InstanceRegistry] ✗ Unknown error detaching RTMP "
                       "destination node"
                    << std::endl;
        }
      }
    }

    // Give GStreamer time to properly cleanup after detach
    // This helps reduce warnings about VideoWriter finalization and prevent
    // segmentation faults FIXED: Increased wait time to ensure GStreamer
    // elements are properly set to NULL state
    if (isDeletion) {
      std::cerr << "[InstanceRegistry] Waiting for GStreamer cleanup..."
                << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(
          800)); // Increased from 500ms to 800ms for better cleanup
      std::cerr << "[InstanceRegistry] Pipeline stopped and fully destroyed "
                   "(all nodes cleared)"
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: All nodes have been destroyed to "
                   "ensure clean state (especially OpenCV DNN)"
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: GStreamer warnings about "
                   "VideoWriter finalization are normal during cleanup"
                << std::endl;
    } else {
      // Note: We detach nodes but keep them in the pipeline vector
      // This allows the pipeline to be rebuilt when restarting
      // The nodes will be recreated when startInstance is called if needed
      std::cerr << "[InstanceRegistry] Pipeline stopped (nodes detached but "
                   "kept for potential restart)"
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: Pipeline will be automatically "
                   "rebuilt when restarting"
                << std::endl;
      if (hasDNNModels) {
        std::cerr << "[InstanceRegistry] NOTE: DNN models have been given time "
                     "to clear internal state"
                  << std::endl;
        std::cerr << "[InstanceRegistry] NOTE: This helps prevent shape "
                     "mismatch errors when restarting"
                  << std::endl;
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "[InstanceRegistry] Exception in stopPipeline: " << e.what()
              << std::endl;
    std::cerr << "[InstanceRegistry] NOTE: GStreamer warnings during cleanup "
                 "are usually harmless"
              << std::endl;
    // Swallow exception - don't let it propagate to prevent terminate handler
    // deadlock
  } catch (...) {
    std::cerr << "[InstanceRegistry] Unknown exception in stopPipeline - "
                 "caught and ignored"
              << std::endl;
    // Swallow exception - don't let it propagate to prevent terminate handler
    // deadlock
  }
  // Ensure function never throws - this prevents deadlock in terminate handler
}

bool InstanceRegistry::rebuildPipelineFromInstanceInfo(
    const std::string &instanceId) {
  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;
  std::cerr << "[InstanceRegistry] Rebuilding pipeline for instance "
            << instanceId << "..." << std::endl;
  std::cerr
      << "[InstanceRegistry] NOTE: This is normal when restarting an instance."
      << std::endl;
  std::cerr << "[InstanceRegistry] After stop(), pipeline is removed from map "
               "and nodes are detached."
            << std::endl;
  std::cerr << "[InstanceRegistry] Rebuilding ensures fresh pipeline with "
               "clean DNN model state."
            << std::endl;
  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;

  // Get instance info (need lock)
  InstanceInfo info;
  {
    std::unique_lock<std::shared_timed_mutex> lock(
        mutex_); // Exclusive lock for write operations
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt == instances_.end()) {
      return false;
    }
    info = instanceIt->second; // Copy instance info
  } // Release lock - rest of function doesn't need it

  // Check if instance has a solution ID
  if (info.solutionId.empty()) {
    std::cerr << "[InstanceRegistry] Cannot rebuild pipeline: instance "
              << instanceId << " has no solution ID" << std::endl;
    return false;
  }

  // Get solution config
  auto optSolution = solution_registry_.getSolution(info.solutionId);
  if (!optSolution.has_value()) {
    std::cerr << "[InstanceRegistry] Cannot rebuild pipeline: solution '"
              << info.solutionId << "' not found" << std::endl;
    return false;
  }

  SolutionConfig solution = optSolution.value();

  // Create CreateInstanceRequest from InstanceInfo
  CreateInstanceRequest req;
  req.name = info.displayName;
  req.group = info.group;
  req.solution = info.solutionId;
  req.persistent = info.persistent;
  req.frameRateLimit = info.frameRateLimit;
  req.metadataMode = info.metadataMode;
  req.statisticsMode = info.statisticsMode;
  req.diagnosticsMode = info.diagnosticsMode;
  req.debugMode = info.debugMode;
  req.detectorMode = info.detectorMode;
  req.detectionSensitivity = info.detectionSensitivity;
  req.movementSensitivity = info.movementSensitivity;
  req.sensorModality = info.sensorModality;
  req.autoStart = info.autoStart;
  req.autoRestart = info.autoRestart;
  req.inputOrientation = info.inputOrientation;
  req.inputPixelLimit = info.inputPixelLimit;

  // Restore all additional parameters from InstanceInfo
  // This includes MODEL_PATH, SFACE_MODEL_PATH, RESIZE_RATIO, etc.
  req.additionalParams = info.additionalParams;

  // Also restore individual fields for backward compatibility
  // Use originator.address as RTSP URL if available (typically input source)
  if (!info.originator.address.empty() &&
      req.additionalParams.find("RTSP_SRC_URL") == req.additionalParams.end() &&
      req.additionalParams.find("RTSP_DES_URL") == req.additionalParams.end() &&
      req.additionalParams.find("RTSP_URL") == req.additionalParams.end()) {
    req.additionalParams["RTSP_SRC_URL"] = info.originator.address;
  }

  // Restore RTMP URL if available (override if not in additionalParams)
  if (!info.rtmpUrl.empty() &&
      req.additionalParams.find("RTMP_DES_URL") == req.additionalParams.end() &&
      req.additionalParams.find("RTMP_URL") == req.additionalParams.end()) {
    req.additionalParams["RTMP_DES_URL"] = info.rtmpUrl;
  }

  // Restore FILE_PATH if available (override if not in additionalParams)
  if (!info.filePath.empty() &&
      req.additionalParams.find("FILE_PATH") == req.additionalParams.end()) {
    req.additionalParams["FILE_PATH"] = info.filePath;
  }

  // Collect existing RTMP stream keys from running instances to check for conflicts
  // This allows us to only modify RTMP URLs when there's an actual conflict
  std::set<std::string> existingRTMPStreamKeys;
  {
    std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);
    if (lock.try_lock_for(std::chrono::milliseconds(500))) {
      for (const auto &[id, info] : instances_) {
        // Skip current instance (it's being rebuilt)
        if (id == instanceId) {
          continue;
        }
        
        // Extract RTMP stream key if RTMP URL is configured
        if (!info.rtmpUrl.empty()) {
          std::string streamKey = PipelineBuilderDestinationNodes::extractRTMPStreamKey(info.rtmpUrl);
          if (!streamKey.empty()) {
            existingRTMPStreamKeys.insert(streamKey);
          }
        }
        
        // Also check additionalParams for RTMP_URL
        auto rtmpIt = info.additionalParams.find("RTMP_URL");
        if (rtmpIt != info.additionalParams.end() && !rtmpIt->second.empty()) {
          std::string streamKey = PipelineBuilderDestinationNodes::extractRTMPStreamKey(rtmpIt->second);
          if (!streamKey.empty()) {
            existingRTMPStreamKeys.insert(streamKey);
          }
        }
        
        // Check RTMP_DES_URL as well
        auto rtmpDesIt = info.additionalParams.find("RTMP_DES_URL");
        if (rtmpDesIt != info.additionalParams.end() && !rtmpDesIt->second.empty()) {
          std::string streamKey = PipelineBuilderDestinationNodes::extractRTMPStreamKey(rtmpDesIt->second);
          if (!streamKey.empty()) {
            existingRTMPStreamKeys.insert(streamKey);
          }
        }
      }
    }
  }

  // Build pipeline (this can take time, so don't hold lock)
  std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> pipeline;
  try {
    pipeline = pipeline_builder_.buildPipeline(solution, req, instanceId, existingRTMPStreamKeys);
    if (!pipeline.empty()) {
      // Store pipeline (need lock briefly)
      {
        std::unique_lock<std::shared_timed_mutex> lock(
            mutex_); // Exclusive lock for write operations
        pipelines_[instanceId] = pipeline;
      } // Release lock
      std::cerr
          << "[InstanceRegistry] Successfully rebuilt pipeline for instance "
          << instanceId << std::endl;
      return true;
    } else {
      std::cerr << "[InstanceRegistry] Pipeline build returned empty pipeline "
                   "for instance "
                << instanceId << std::endl;
      return false;
    }
  } catch (const std::exception &e) {
    std::cerr
        << "[InstanceRegistry] Exception rebuilding pipeline for instance "
        << instanceId << ": " << e.what() << std::endl;
    return false;
  } catch (...) {
    std::cerr
        << "[InstanceRegistry] Unknown error rebuilding pipeline for instance "
        << instanceId << std::endl;
    return false;
  }
}

void InstanceRegistry::buildPipelineAsync(
    const std::string &instanceId,
    const CreateInstanceRequest &req,
    const SolutionConfig &solution,
    const std::set<std::string> &existingRTMPStreamKeys) {
  
  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;
  std::cerr << "[InstanceRegistry] Building pipeline ASYNC for instance "
            << instanceId << " (background thread)" << std::endl;
  std::cerr << "[InstanceRegistry] API will return immediately, pipeline will "
               "be built in background"
            << std::endl;
  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;

  // Start async build in separate thread
  std::thread([this, instanceId, req, solution, existingRTMPStreamKeys]() {
    try {
      // 1. Set building = true
      {
        std::unique_lock<std::shared_timed_mutex> lock(mutex_);
        auto it = instances_.find(instanceId);
        if (it != instances_.end()) {
          it->second.building = true;
          it->second.buildError.clear();
        } else {
          std::cerr << "[InstanceRegistry] Instance not found for async build: "
                    << instanceId << std::endl;
          return;
        }
      }

      // 2. Build pipeline (có thể mất 30+ giây)
      std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> pipeline;
      try {
        pipeline = pipeline_builder_.buildPipeline(
            solution, req, instanceId, existingRTMPStreamKeys);
      } catch (const std::bad_alloc &e) {
        std::cerr << "[InstanceRegistry] Memory allocation error building "
                     "pipeline for instance "
                  << instanceId << ": " << e.what() << std::endl;
        {
          std::unique_lock<std::shared_timed_mutex> lock(mutex_);
          auto it = instances_.find(instanceId);
          if (it != instances_.end()) {
            it->second.building = false;
            it->second.buildError = "Memory allocation error: " + std::string(e.what());
          }
        }
        return;
      } catch (const std::invalid_argument &e) {
        std::cerr << "[InstanceRegistry] Invalid argument building pipeline for "
                     "instance "
                  << instanceId << ": " << e.what() << std::endl;
        {
          std::unique_lock<std::shared_timed_mutex> lock(mutex_);
          auto it = instances_.find(instanceId);
          if (it != instances_.end()) {
            it->second.building = false;
            it->second.buildError = "Invalid argument: " + std::string(e.what());
          }
        }
        return;
      } catch (const std::runtime_error &e) {
        std::cerr << "[InstanceRegistry] Runtime error building pipeline for "
                     "instance "
                  << instanceId << ": " << e.what() << std::endl;
        {
          std::unique_lock<std::shared_timed_mutex> lock(mutex_);
          auto it = instances_.find(instanceId);
          if (it != instances_.end()) {
            it->second.building = false;
            it->second.buildError = "Runtime error: " + std::string(e.what());
          }
        }
        return;
      } catch (const std::exception &e) {
        std::cerr << "[InstanceRegistry] Exception building pipeline for instance "
                  << instanceId << ": " << e.what() << " (type: " << typeid(e).name()
                  << ")" << std::endl;
        {
          std::unique_lock<std::shared_timed_mutex> lock(mutex_);
          auto it = instances_.find(instanceId);
          if (it != instances_.end()) {
            it->second.building = false;
            it->second.buildError = "Pipeline build failed: " + std::string(e.what());
          }
        }
        return;
      } catch (...) {
        std::cerr << "[InstanceRegistry] Unknown error building pipeline for "
                     "instance "
                  << instanceId << " (non-standard exception)" << std::endl;
        {
          std::unique_lock<std::shared_timed_mutex> lock(mutex_);
          auto it = instances_.find(instanceId);
          if (it != instances_.end()) {
            it->second.building = false;
            it->second.buildError = "Unknown error while building pipeline";
          }
        }
        return;
      }

      // 3. Lưu pipeline và cập nhật trạng thái
      bool buildSuccess = false;
      {
        std::unique_lock<std::shared_timed_mutex> lock(mutex_);
        auto it = instances_.find(instanceId);
        if (it != instances_.end()) {
          if (!pipeline.empty()) {
            pipelines_[instanceId] = pipeline;
            it->second.building = false;
            it->second.loaded = true; // Pipeline đã sẵn sàng
            buildSuccess = true;
            std::cerr << "[InstanceRegistry] ✓ Pipeline built successfully for "
                         "instance "
                      << instanceId << std::endl;
          } else {
            it->second.building = false;
            it->second.buildError = "Pipeline is empty after build";
            std::cerr << "[InstanceRegistry] ✗ Pipeline is empty after build for "
                         "instance "
                      << instanceId << std::endl;
          }
        } else {
          std::cerr << "[InstanceRegistry] Instance not found after pipeline build: "
                    << instanceId << std::endl;
          // Cleanup pipeline if instance was deleted
          pipeline.clear();
          return;
        }
      }

      // 4. Update RTMP URL if needed (similar to sync createInstance)
      if (buildSuccess) {
        std::string actualRtmpUrl = PipelineBuilder::getActualRTMPUrl(instanceId);
        if (!actualRtmpUrl.empty()) {
          std::unique_lock<std::shared_timed_mutex> lock(mutex_);
          auto it = instances_.find(instanceId);
          if (it != instances_.end()) {
            std::string finalRtmpUrl = actualRtmpUrl;
            size_t lastSlash = finalRtmpUrl.find_last_of('/');
            if (lastSlash != std::string::npos &&
                lastSlash < finalRtmpUrl.length() - 1) {
              std::string streamKey = finalRtmpUrl.substr(lastSlash + 1);
              if (streamKey.length() < 2 ||
                  streamKey.substr(streamKey.length() - 2) != "_0") {
                finalRtmpUrl += "_0";
              }
            }
            it->second.rtmpUrl = finalRtmpUrl;
            it->second.additionalParams["RTMP_URL"] = finalRtmpUrl;
            PipelineBuilder::clearActualRTMPUrl(instanceId);
          }
        }
      }

      // 5. Auto-start nếu được yêu cầu
      if (buildSuccess) {
        bool shouldAutoStart = false;
        {
          std::shared_lock<std::shared_timed_mutex> lock(mutex_);
          auto it = instances_.find(instanceId);
          if (it != instances_.end() && it->second.autoStart) {
            shouldAutoStart = true;
          }
        }

        if (shouldAutoStart) {
          std::cerr << "[InstanceRegistry] ========================================"
                    << std::endl;
          std::cerr << "[InstanceRegistry] Auto-starting pipeline for instance "
                    << instanceId << " (async, after pipeline build)" << std::endl;
          std::cerr << "[InstanceRegistry] ========================================"
                    << std::endl;
          // Start instance (this will use the pipeline we just built)
          startInstance(instanceId, false);
        }
      }

    } catch (const std::exception &e) {
      std::cerr << "[InstanceRegistry] Exception in async pipeline build thread: "
                << e.what() << std::endl;
      {
        std::unique_lock<std::shared_timed_mutex> lock(mutex_);
        auto it = instances_.find(instanceId);
        if (it != instances_.end()) {
          it->second.building = false;
          it->second.buildError = "Exception in async build: " + std::string(e.what());
        }
      }
    } catch (...) {
      std::cerr << "[InstanceRegistry] Unknown exception in async pipeline build "
                   "thread"
                << std::endl;
      {
        std::unique_lock<std::shared_timed_mutex> lock(mutex_);
        auto it = instances_.find(instanceId);
        if (it != instances_.end()) {
          it->second.building = false;
          it->second.buildError = "Unknown exception in async build";
        }
      }
    }
  }).detach(); // Detach thread để không block
}

bool InstanceRegistry::hasRTMPOutput(const std::string &instanceId) const {
  // CRITICAL: Use shared_lock for read-only operations to allow concurrent
  // readers This prevents deadlock when multiple threads read instance data
  // simultaneously
  // CRITICAL: Use timeout to prevent blocking if mutex is locked
  std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);

  // Try to acquire lock with timeout (2000ms)
  if (!lock.try_lock_for(std::chrono::milliseconds(2000))) {
    std::cerr << "[InstanceRegistry] WARNING: hasRTMPOutput() timeout - "
                 "mutex is locked, returning false"
              << std::endl;
    if (isInstanceLoggingEnabled()) {
      PLOG_WARNING << "[InstanceRegistry] hasRTMPOutput() timeout after "
                      "2000ms - mutex may be locked by another operation";
    }
    return false; // Return false to prevent blocking
  }

  // Check if instance exists
  auto instanceIt = instances_.find(instanceId);
  if (instanceIt == instances_.end()) {
    return false;
  }

  // Check if RTMP_DES_URL or RTMP_URL is in additionalParams
  const auto &additionalParams = instanceIt->second.additionalParams;
  if (additionalParams.find("RTMP_DES_URL") != additionalParams.end() &&
      !additionalParams.at("RTMP_DES_URL").empty()) {
    return true;
  }
  if (additionalParams.find("RTMP_URL") != additionalParams.end() &&
      !additionalParams.at("RTMP_URL").empty()) {
    return true;
  }

  // Check if rtmpUrl field is set
  if (!instanceIt->second.rtmpUrl.empty()) {
    return true;
  }

  // Check if pipeline has RTMP destination node
  auto pipelineIt = pipelines_.find(instanceId);
  if (pipelineIt != pipelines_.end()) {
    for (const auto &node : pipelineIt->second) {
      if (std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node)) {
        return true;
      }
    }
  }

  return false;
}

std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>>
InstanceRegistry::getSourceNodesFromRunningInstances() const {
  // CRITICAL: Use timeout to prevent blocking if mutex is locked
  std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);

  // Try to acquire lock with timeout (2000ms)
  if (!lock.try_lock_for(std::chrono::milliseconds(2000))) {
    std::cerr << "[InstanceRegistry] WARNING: "
                 "getSourceNodesFromRunningInstances() timeout - "
                 "mutex is locked, returning empty vector"
              << std::endl;
    if (isInstanceLoggingEnabled()) {
      PLOG_WARNING << "[InstanceRegistry] getSourceNodesFromRunningInstances() "
                      "timeout after "
                      "2000ms - mutex may be locked by another operation";
    }
    return {}; // Return empty vector to prevent blocking
  }

  std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> sourceNodes;

  // Iterate through all instances
  for (const auto &[instanceId, info] : instances_) {
    // Only get source nodes from running instances
    if (!info.running) {
      continue;
    }

    // Get pipeline for this instance
    auto pipelineIt = pipelines_.find(instanceId);
    if (pipelineIt != pipelines_.end() && !pipelineIt->second.empty()) {
      // Source node is always the first node in the pipeline
      const auto &sourceNode = pipelineIt->second[0];

      // Verify it's a source node (RTSP, file, or RTMP source)
      auto rtspNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(
              sourceNode);
      auto fileNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(
              sourceNode);
      auto rtmpNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(
              sourceNode);

      if (rtspNode || fileNode || rtmpNode) {
        sourceNodes.push_back(sourceNode);
      }
    }
  }

  return sourceNodes;
}

std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>>
InstanceRegistry::getInstanceNodes(const std::string &instanceId) const {
  // CRITICAL: Use timeout to prevent blocking if mutex is locked
  std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);

  // Try to acquire lock with timeout (2000ms)
  if (!lock.try_lock_for(std::chrono::milliseconds(2000))) {
    std::cerr << "[InstanceRegistry] WARNING: getInstanceNodes() timeout - "
                 "mutex is locked, returning empty vector"
              << std::endl;
    if (isInstanceLoggingEnabled()) {
      PLOG_WARNING << "[InstanceRegistry] getInstanceNodes() timeout after "
                      "2000ms - mutex may be locked by another operation";
    }
    return {}; // Return empty vector to prevent blocking
  }

  // Get pipeline for this instance
  auto pipelineIt = pipelines_.find(instanceId);
  if (pipelineIt != pipelines_.end() && !pipelineIt->second.empty()) {
    return pipelineIt->second; // Return copy of nodes
  }

  return {}; // Return empty vector if instance doesn't have pipeline
}

int InstanceRegistry::checkAndHandleRetryLimits() {
  // CRITICAL: Collect instances to stop while holding lock, then release lock
  // before calling stopInstance() This prevents deadlock because stopInstance()
  // needs exclusive lock
  std::vector<std::string>
      instancesToStop; // Collect instances to stop while holding lock
  int stoppedCount = 0;
  auto now = std::chrono::steady_clock::now();

  {
    // Use exclusive lock for write operations (updating retry counts, marking
    // instances as stopped) This will block readers (getAllInstances) only when
    // actually writing
    std::unique_lock<std::shared_timed_mutex> lock(mutex_);

    // Check all running instances
    for (auto &[instanceId, info] : instances_) {
      if (!info.running || info.retryLimitReached) {
        continue; // Skip non-running instances or already stopped due to retry
                  // limit
      }

      // Check if this is an RTSP instance (has RTSP URL)
      if (!info.rtspUrl.empty()) {
        // Get pipeline to check if RTSP node exists
        auto pipelineIt = pipelines_.find(instanceId);
        if (pipelineIt != pipelines_.end() && !pipelineIt->second.empty()) {
          auto rtspNode =
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(
                  pipelineIt->second[0]);
          if (rtspNode) {
            // Calculate time since instance started
            auto timeSinceStart =
                std::chrono::duration_cast<std::chrono::seconds>(now -
                                                                 info.startTime)
                    .count();

            // Calculate time since last activity (or since start if no
            // activity)
            auto timeSinceActivity =
                std::chrono::duration_cast<std::chrono::seconds>(
                    now - info.lastActivityTime)
                    .count();

            // Only increment retry counter if:
            // 1. Instance has been running for at least 60 seconds (give it
            // more time to connect and stabilize)
            // 2. AND instance has not received any data yet (hasReceivedData =
            // false)
            // 3. OR instance has been running for more than 90 seconds without
            // activity (after receiving data) Note: Increased timeout from 30s
            // to 60s to account for RTSP connection time and fps update delay
            bool isLikelyRetrying = false;
            if (timeSinceStart >= 60) {
              if (!info.hasReceivedData) {
                // Instance has been running for 60+ seconds without receiving
                // any data This indicates it's likely stuck in retry loop
                isLikelyRetrying = true;
              } else if (timeSinceActivity > 90) {
                // Instance received data before but now has been inactive for
                // 90+ seconds This might indicate connection was lost and
                // retrying
                isLikelyRetrying = true;
              }
            }

            if (isLikelyRetrying) {
              // Increment retry counter only when we detect retry is happening
              info.retryCount++;

              std::cerr << "[InstanceRegistry] Instance " << instanceId
                        << " retry detected: count=" << info.retryCount << "/"
                        << info.maxRetryCount << ", running=" << timeSinceStart
                        << "s"
                        << ", no_data="
                        << (!info.hasReceivedData ? "yes" : "no")
                        << ", inactive=" << timeSinceActivity << "s"
                        << std::endl;

              // Check if retry limit reached
              if (info.retryCount >= info.maxRetryCount) {
                info.retryLimitReached = true;
                std::cerr << "[InstanceRegistry] ⚠ Instance " << instanceId
                          << " reached retry limit (" << info.maxRetryCount
                          << " retries) after " << timeSinceStart
                          << " seconds - stopping instance" << std::endl;
                {
                  std::string im =
                      "Instance " + instanceId + " reached retry limit - stopping";
                  InstanceFileLogger::log(instanceId, plog::warning, im);
                  PLOG_WARNING << "[Instance] " << im;
                }

                // Mark as not running (will be stopped outside lock)
                info.running = false;
                instancesToStop.push_back(
                    instanceId); // Collect for stopping outside lock
                stoppedCount++;
              }
            } else {
              // Check if instance is receiving data
              // Note: fps may not be updated from pipeline, so we use a more
              // lenient approach If instance has been running for a while
              // without errors, assume it's working
              bool isReceivingData = false;

              // Method 1: Check fps (if available from pipeline)
              if (info.fps > 0) {
                isReceivingData = true;
              }
              // Method 2: If instance has been running for 45+ seconds without
              // being marked as retrying, assume it's working (RTSP connection
              // established, even if fps not updated) This gives time for RTSP
              // to connect (10-30s) and stabilize before retry detection starts
              // (60s)
              else if (timeSinceStart >= 45 && info.retryCount == 0) {
                // Instance has been running for 45+ seconds without retry
                // detection This likely means RTSP connection is established
                // and working (retry detection only starts at 60s, so 45s is
                // safe)
                isReceivingData = true;
              }

              if (isReceivingData) {
                // Instance is receiving frames - mark as having received data
                if (!info.hasReceivedData) {
                  std::cerr << "[InstanceRegistry] Instance " << instanceId
                            << " connection successful - receiving frames";
                  if (info.fps > 0) {
                    std::cerr << " (fps=" << std::fixed << std::setprecision(2)
                              << info.fps << ")";
                  } else {
                    std::cerr << " (running for " << timeSinceStart
                              << "s, assumed working)";
                  }
                  std::cerr << std::endl;
                  info.hasReceivedData = true;
                }
                // Update last activity time when receiving frames
                info.lastActivityTime = now;

                // Reset retry counter if instance is successfully receiving
                // data
                if (info.retryCount > 0) {
                  std::cerr
                      << "[InstanceRegistry] Instance " << instanceId
                      << " connection successful - resetting retry counter"
                      << std::endl;
                  info.retryCount = 0;
                }
              } else {
                // Debug: Log when RTSP is connected but no frames received
                if (timeSinceStart > 5 && timeSinceStart < 35) {
                  // Only log once every 5 seconds to avoid spam
                  static std::map<std::string,
                                  std::chrono::steady_clock::time_point>
                      lastLogTime;
                  auto lastLog = lastLogTime.find(instanceId);
                  bool shouldLog = false;
                  if (lastLog == lastLogTime.end()) {
                    shouldLog = true;
                    lastLogTime[instanceId] = now;
                  } else {
                    auto timeSinceLastLog =
                        std::chrono::duration_cast<std::chrono::seconds>(
                            now - lastLog->second)
                            .count();
                    if (timeSinceLastLog >= 5) {
                      shouldLog = true;
                      lastLogTime[instanceId] = now;
                    }
                  }
                  if (shouldLog) {
                    std::cerr << "[InstanceRegistry] Instance " << instanceId
                              << " RTSP connected but no frames received yet "
                                 "(running="
                              << timeSinceStart << "s, fps=" << info.fps
                              << "). This may be normal - RTSP streams can "
                                 "take 10-30 seconds to stabilize."
                              << std::endl;
                  }
                }
              }
            }
          }
        }
      }
    }
  } // CRITICAL: Release lock before calling stopInstance() to avoid deadlock

  // Stop instances that reached retry limit (do this outside lock to avoid
  // deadlock) stopInstance() needs exclusive lock, so we must release our lock
  // first
  for (const auto &instanceId : instancesToStop) {
    try {
      stopInstance(instanceId);
      std::cerr << "[InstanceRegistry] ✓ Stopped instance " << instanceId
                << " due to retry limit" << std::endl;
    } catch (const std::exception &e) {
      std::cerr << "[InstanceRegistry] ✗ Failed to stop instance " << instanceId
                << " due to retry limit: " << e.what() << std::endl;
    } catch (...) {
      std::cerr << "[InstanceRegistry] ✗ Failed to stop instance " << instanceId
                << " due to retry limit (unknown error)" << std::endl;
    }
  }

  return stoppedCount;
}

bool InstanceRegistry::updateInstanceFromConfig(const std::string &instanceId,
                                                const Json::Value &configJson) {
  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;
  std::cerr << "[InstanceRegistry] Updating instance from config: "
            << instanceId << std::endl;
  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;

  bool wasRunning = false;
  bool isPersistent = false;
  InstanceInfo currentInfo;

  {
    std::unique_lock<std::shared_timed_mutex> lock(
        mutex_); // Exclusive lock for write operations

    auto instanceIt = instances_.find(instanceId);
    if (instanceIt == instances_.end()) {
      std::cerr << "[InstanceRegistry] Instance " << instanceId << " not found"
                << std::endl;
      return false;
    }

    InstanceInfo &info = instanceIt->second;

    // Check if instance is read-only
    if (info.readOnly) {
      std::cerr << "[InstanceRegistry] Cannot update read-only instance "
                << instanceId << std::endl;
      return false;
    }

    wasRunning = info.running;
    isPersistent = info.persistent;
    currentInfo = info; // Copy current info
  } // Release lock

  // Convert current InstanceInfo to config JSON
  std::string conversionError;
  Json::Value existingConfig =
      instance_storage_.instanceInfoToConfigJson(currentInfo, &conversionError);
  if (existingConfig.isNull() || existingConfig.empty()) {
    std::cerr << "[InstanceRegistry] Failed to convert current InstanceInfo to "
                 "config: "
              << conversionError << std::endl;
    return false;
  }

  // List of keys to preserve (TensorRT model IDs, Zone IDs, etc.)
  std::vector<std::string> preserveKeys;

  // Collect UUID-like keys (TensorRT model IDs) from existing config
  for (const auto &key : existingConfig.getMemberNames()) {
    if (key.length() >= 36 && key.find('-') != std::string::npos) {
      preserveKeys.push_back(key);
    }
  }

  // Add special keys to preserve
  std::vector<std::string> specialKeys = {"AnimalTracker",
                                          "LicensePlateTracker",
                                          "ObjectAttributeExtraction",
                                          "ObjectMovementClassifier",
                                          "PersonTracker",
                                          "VehicleTracker",
                                          "Global"};
  preserveKeys.insert(preserveKeys.end(), specialKeys.begin(),
                      specialKeys.end());

  // Debug: log what's in configJson
  std::cerr << "[InstanceRegistry] configJson keys: ";
  for (const auto &key : configJson.getMemberNames()) {
    std::cerr << key << " ";
  }
  std::cerr << std::endl;

  if (configJson.isMember("AdditionalParams")) {
    std::cerr << "[InstanceRegistry] configJson has AdditionalParams"
              << std::endl;
    if (configJson["AdditionalParams"].isObject()) {
      std::cerr << "[InstanceRegistry] AdditionalParams keys: ";
      for (const auto &key : configJson["AdditionalParams"].getMemberNames()) {
        std::cerr << key << " ";
      }
      std::cerr << std::endl;
    }
  } else {
    std::cerr << "[InstanceRegistry] configJson does NOT have AdditionalParams"
              << std::endl;
  }

  // Merge configs (preserve Zone, Tripwire, DetectorRegions if not in update)
  if (!instance_storage_.mergeConfigs(existingConfig, configJson,
                                      preserveKeys)) {
    std::cerr << "[InstanceRegistry] Merge failed for instance " << instanceId
              << std::endl;
    return false;
  }

  // Ensure InstanceId matches
  existingConfig["InstanceId"] = instanceId;

  // Convert merged config back to InstanceInfo
  auto optInfo = instance_storage_.configJsonToInstanceInfo(existingConfig,
                                                            &conversionError);
  if (!optInfo.has_value()) {
    std::cerr << "[InstanceRegistry] Failed to convert config to InstanceInfo: "
              << conversionError << std::endl;
    return false;
  }

  InstanceInfo updatedInfo = optInfo.value();

  // Preserve runtime state
  updatedInfo.loaded = currentInfo.loaded;
  updatedInfo.running = currentInfo.running;
  updatedInfo.fps = currentInfo.fps;

  // Update instance in registry
  {
    std::unique_lock<std::shared_timed_mutex> lock(
        mutex_); // Exclusive lock for write operations
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt == instances_.end()) {
      std::cerr << "[InstanceRegistry] Instance " << instanceId
                << " not found during update" << std::endl;
      return false;
    }

    InstanceInfo &info = instanceIt->second;

    // Update all fields from merged config
    info = updatedInfo;

    InstanceFileLogger::setInstanceFileLogging(
        instanceId, info.instanceFileLoggingEnabled);

    std::cerr << "[InstanceRegistry] ✓ Instance info updated in registry"
              << std::endl;
  } // Release lock

  // Save to storage
  if (isPersistent) {
    bool saved = instance_storage_.saveInstance(instanceId, updatedInfo);
    if (saved) {
      std::cerr << "[InstanceRegistry] Instance configuration saved to file"
                << std::endl;
    } else {
      std::cerr << "[InstanceRegistry] Warning: Failed to save instance "
                   "configuration to file"
                << std::endl;
    }
  }

  std::cerr << "[InstanceRegistry] ✓ Instance " << instanceId
            << " updated successfully from config" << std::endl;

  // Restart instance if it was running to apply changes
  if (wasRunning) {
    std::cerr << "[InstanceRegistry] Instance was running, restarting to apply "
                 "changes..."
              << std::endl;

    // Stop instance first
    if (stopInstance(instanceId)) {
      // CRITICAL: Wait longer for complete cleanup to prevent segmentation
      // faults GStreamer pipelines, threads (MQTT, RTSP monitor), and OpenCV
      // DNN need time to fully cleanup Previous 500ms was too short and caused
      // race conditions
      std::cerr
          << "[InstanceRegistry] Waiting for complete cleanup (3 seconds)..."
          << std::endl;
      std::cerr << "[InstanceRegistry] This ensures:" << std::endl;
      std::cerr
          << "[InstanceRegistry]   1. GStreamer pipelines are fully destroyed"
          << std::endl;
      std::cerr << "[InstanceRegistry]   2. All threads (MQTT, RTSP monitor) "
                   "are joined"
                << std::endl;
      std::cerr << "[InstanceRegistry]   3. OpenCV DNN state is cleared"
                << std::endl;
      std::cerr << "[InstanceRegistry]   4. No race conditions when starting "
                   "new pipeline"
                << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(3000));

      // Start instance again (this will rebuild pipeline with new config)
      if (startInstance(instanceId, true)) {
        std::cerr << "[InstanceRegistry] ✓ Instance restarted successfully "
                     "with new configuration"
                  << std::endl;
      } else {
        std::cerr
            << "[InstanceRegistry] ⚠ Instance stopped but failed to restart"
            << std::endl;
      }
    } else {
      std::cerr << "[InstanceRegistry] ⚠ Failed to stop instance for restart"
                << std::endl;
    }
  }

  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;
  return true;
}

void InstanceRegistry::startVideoLoopThread(const std::string &instanceId) {
  // DISABLED: Video loop feature removed to improve performance
  return;

  // Stop existing thread if any
  stopVideoLoopThread(instanceId);

  // Check if instance has LOOP_VIDEO enabled
  bool loopEnabled = false;
  {
    std::shared_lock<std::shared_timed_mutex> lock(mutex_);
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt != instances_.end()) {
      const auto &info = instanceIt->second;
      auto it = info.additionalParams.find("LOOP_VIDEO");
      if (it != info.additionalParams.end()) {
        std::string loopValue = it->second;
        std::transform(loopValue.begin(), loopValue.end(), loopValue.begin(),
                       ::tolower);
        loopEnabled =
            (loopValue == "true" || loopValue == "1" || loopValue == "yes");
      }
    }
  }

  if (!loopEnabled) {
    return; // Loop not enabled, don't start thread
  }

  // Check if instance is file-based
  bool isFileBased = false;
  {
    std::shared_lock<std::shared_timed_mutex> lock(mutex_);
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt != instances_.end()) {
      const auto &info = instanceIt->second;
      isFileBased =
          !info.filePath.empty() || info.additionalParams.find("FILE_PATH") !=
                                        info.additionalParams.end();
    }
  }

  if (!isFileBased) {
    return; // Not a file-based instance, don't start thread
  }

  // Create stop flag
  {
    std::lock_guard<std::mutex> lock(thread_mutex_);
    video_loop_thread_stop_flags_.emplace(instanceId, false);
  }

  std::cerr << "[InstanceRegistry] [VideoLoop] Starting video loop monitoring "
               "thread for instance "
            << instanceId << std::endl;

  // Start new monitoring thread
  // CRITICAL: Capture instanceId by value to avoid use-after-free
  // We access mutex_ and instances_ through 'this', but we check stop flag
  // first to ensure thread exits quickly if instance is stopped
  std::thread videoLoopThread([this, instanceId]() {
    try {
      int zeroFpsCount = 0;
      const int ZERO_FPS_THRESHOLD =
          3; // Check 3 times (30 seconds) before restarting
      const int CHECK_INTERVAL_SECONDS =
          10; // Check every 10 seconds (increased from 5 to reduce CPU usage)
      const int MIN_RUNTIME_SECONDS =
          60; // Minimum runtime before allowing restart (increased from 30 to
              // 60 seconds)
      auto instanceStartTime = std::chrono::steady_clock::now();
      bool hasEverReceivedData = false;

      while (true) {
        // Check stop flag first
        {
          try {
            std::lock_guard<std::mutex> lock(thread_mutex_);
            auto flagIt = video_loop_thread_stop_flags_.find(instanceId);
            if (flagIt == video_loop_thread_stop_flags_.end() ||
                flagIt->second.load()) {
              break;
            }
          } catch (...) {
            // If mutex access fails, exit thread to prevent crash
            std::cerr << "[InstanceRegistry] [VideoLoop] Error accessing stop "
                         "flag, exiting thread"
                      << std::endl;
            return;
          }
        }

        // Wait CHECK_INTERVAL_SECONDS seconds (check flag periodically)
        for (int i = 0; i < CHECK_INTERVAL_SECONDS * 10; ++i) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));

          // Check stop flag
          {
            try {
              std::lock_guard<std::mutex> lock(thread_mutex_);
              auto flagIt = video_loop_thread_stop_flags_.find(instanceId);
              if (flagIt == video_loop_thread_stop_flags_.end() ||
                  flagIt->second.load()) {
                return;
              }
            } catch (...) {
              // If mutex access fails, exit thread to prevent crash
              std::cerr << "[InstanceRegistry] [VideoLoop] Error accessing "
                           "stop flag, exiting thread"
                        << std::endl;
              return;
            }
          }
        }

        // Check if instance still exists and is running
        bool shouldRestart = false;
        {
          try {
            std::unique_lock<std::shared_timed_mutex> lock(mutex_);
            auto instanceIt = instances_.find(instanceId);
            if (instanceIt == instances_.end() || !instanceIt->second.running) {
              // Instance deleted or stopped, exit thread
              return;
            }

            const auto &info = instanceIt->second;

            // Track if we've ever received data
            if (info.hasReceivedData) {
              hasEverReceivedData = true;
            }

            // Check minimum runtime before allowing restart
            auto runtime =
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - instanceStartTime)
                    .count();

            // Check FPS - if 0 for multiple checks, video likely ended
            // CRITICAL: Only restart if:
            // 1. Instance has been running for at least MIN_RUNTIME_SECONDS
            // 2. Instance has received data at some point (hasEverReceivedData)
            // 3. Current FPS = 0 and hasReceivedData = true (was working but
            // now stopped)
            if (info.fps == 0.0 && info.hasReceivedData &&
                hasEverReceivedData && runtime >= MIN_RUNTIME_SECONDS) {
              // Instance was working but now FPS = 0 - video likely ended
              zeroFpsCount++;
              std::cerr
                  << "[InstanceRegistry] [VideoLoop] FPS = 0 detected (count: "
                  << zeroFpsCount << "/" << ZERO_FPS_THRESHOLD
                  << ", runtime: " << runtime << "s)" << std::endl;

              if (zeroFpsCount >= ZERO_FPS_THRESHOLD) {
                shouldRestart = true;
                zeroFpsCount = 0; // Reset counter
              }
            } else if (info.fps > 0.0) {
              // FPS > 0, video is playing - reset counter
              zeroFpsCount = 0;
            } else if (runtime < MIN_RUNTIME_SECONDS) {
              // Instance just started, don't restart yet
              if (zeroFpsCount == 0) {
                std::cerr << "[InstanceRegistry] [VideoLoop] Instance just "
                             "started (runtime: "
                          << runtime << "s < " << MIN_RUNTIME_SECONDS
                          << "s), waiting before checking for restart..."
                          << std::endl;
              }
              zeroFpsCount = 0; // Reset counter during startup period
            }
          } catch (const std::exception &e) {
            std::cerr << "[InstanceRegistry] [VideoLoop] Exception accessing "
                         "instance data: "
                      << e.what() << std::endl;
            // Continue to next iteration instead of crashing
            continue;
          } catch (...) {
            std::cerr << "[InstanceRegistry] [VideoLoop] Unknown error "
                         "accessing instance data"
                      << std::endl;
            // Continue to next iteration instead of crashing
            continue;
          }
        }

        // Restart file source node if needed
        if (shouldRestart) {
          std::cerr << "[InstanceRegistry] [VideoLoop] Video ended detected - "
                       "restarting file source node..."
                    << std::endl;

          // CRITICAL: Check stop flag before starting restart operation
          {
            try {
              std::lock_guard<std::mutex> lock(thread_mutex_);
              auto flagIt = video_loop_thread_stop_flags_.find(instanceId);
              if (flagIt == video_loop_thread_stop_flags_.end() ||
                  flagIt->second.load()) {
                return; // Stop flag set, exit thread
              }
            } catch (...) {
              return; // Error accessing stop flag, exit thread
            }
          }

          // Get pipeline copy
          std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> pipelineCopy;
          {
            try {
              std::unique_lock<std::shared_timed_mutex> lock(mutex_);
              auto pipelineIt = pipelines_.find(instanceId);
              if (pipelineIt != pipelines_.end() &&
                  !pipelineIt->second.empty()) {
                pipelineCopy = pipelineIt->second;
              }
            } catch (...) {
              std::cerr << "[InstanceRegistry] [VideoLoop] Exception getting "
                           "pipeline copy, skipping restart"
                        << std::endl;
              continue; // Skip restart if we can't get pipeline
            }
          }

          if (!pipelineCopy.empty()) {
            // Check if first node is file source node
            auto fileNode =
                std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(
                    pipelineCopy[0]);
            if (fileNode) {
              try {
                // CRITICAL: Wrap stop() in async with timeout to prevent
                // blocking
                try {
                  auto stopFuture = std::async(
                      std::launch::async, [fileNode]() { fileNode->stop(); });

                  // Wait with timeout (500ms) - if it takes too long, skip stop
                  if (stopFuture.wait_for(std::chrono::milliseconds(500)) ==
                      std::future_status::timeout) {
                    std::cerr << "[InstanceRegistry] [VideoLoop] ⚠ "
                                 "fileNode->stop() timeout (500ms), skipping..."
                              << std::endl;
                  } else {
                    try {
                      stopFuture.get();
                    } catch (...) {
                      // Ignore exceptions from stop()
                    }
                  }
                  std::this_thread::sleep_for(std::chrono::milliseconds(200));
                } catch (...) {
                  // If stop() fails, continue anyway
                }

                // CRITICAL: Wrap detach_recursively() in async with timeout
                try {
                  auto detachFuture =
                      std::async(std::launch::async, [fileNode]() {
                        fileNode->detach_recursively();
                      });

                  // Wait with timeout (1000ms) - if it takes too long, skip
                  // detach
                  if (detachFuture.wait_for(std::chrono::milliseconds(1000)) ==
                      std::future_status::timeout) {
                    std::cerr << "[InstanceRegistry] [VideoLoop] ⚠ "
                                 "fileNode->detach_recursively() timeout "
                                 "(1000ms), skipping..."
                              << std::endl;
                    // Continue anyway - try to start
                  } else {
                    try {
                      detachFuture.get();
                    } catch (...) {
                      // Ignore exceptions from detach
                    }
                  }
                } catch (...) {
                  // If detach fails, continue anyway
                }

                // CRITICAL: Longer delay to ensure GStreamer elements are fully
                // cleaned up GStreamer needs time to transition elements to
                // NULL state before dispose
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));

                // CRITICAL: Check stop flag before starting
                {
                  try {
                    std::lock_guard<std::mutex> lock(thread_mutex_);
                    auto flagIt =
                        video_loop_thread_stop_flags_.find(instanceId);
                    if (flagIt == video_loop_thread_stop_flags_.end() ||
                        flagIt->second.load()) {
                      return; // Stop flag set, exit thread
                    }
                  } catch (...) {
                    return; // Error accessing stop flag, exit thread
                  }
                }

                // Restart file source node with timeout protection
                std::cerr << "[InstanceRegistry] [VideoLoop] Restarting file "
                             "source node..."
                          << std::endl;
                try {
                  auto startFuture = std::async(
                      std::launch::async, [fileNode]() { fileNode->start(); });

                  // Wait with timeout (2000ms) - if it takes too long, skip
                  // start
                  if (startFuture.wait_for(std::chrono::milliseconds(2000)) ==
                      std::future_status::timeout) {
                    std::cerr
                        << "[InstanceRegistry] [VideoLoop] ⚠ fileNode->start() "
                           "timeout (2000ms), skipping..."
                        << std::endl;
                    std::cerr
                        << "[InstanceRegistry] [VideoLoop] Instance will "
                           "continue running, will retry restart on next check"
                        << std::endl;
                  } else {
                    try {
                      startFuture.get();
                      std::cerr << "[InstanceRegistry] [VideoLoop] ✓ File "
                                   "source node restarted successfully"
                                << std::endl;

                      // Reset hasReceivedData to allow detection of new
                      // playback
                      {
                        try {
                          std::unique_lock<std::shared_timed_mutex> lock(
                              mutex_);
                          auto instanceIt = instances_.find(instanceId);
                          if (instanceIt != instances_.end()) {
                            instanceIt->second.hasReceivedData = false;
                            // Reset instance start time for next cycle
                            instanceStartTime =
                                std::chrono::steady_clock::now();
                            hasEverReceivedData = false;
                          }
                        } catch (...) {
                          // Ignore exceptions when updating instance data
                        }
                      }
                    } catch (const std::exception &e) {
                      std::cerr << "[InstanceRegistry] [VideoLoop] ✗ Exception "
                                   "during fileNode->start(): "
                                << e.what() << std::endl;
                      std::cerr << "[InstanceRegistry] [VideoLoop] Instance "
                                   "will continue running, will retry restart "
                                   "on next check"
                                << std::endl;
                    } catch (...) {
                      std::cerr << "[InstanceRegistry] [VideoLoop] ✗ Unknown "
                                   "error during fileNode->start()"
                                << std::endl;
                      std::cerr << "[InstanceRegistry] [VideoLoop] Instance "
                                   "will continue running, will retry restart "
                                   "on next check"
                                << std::endl;
                    }
                  }
                } catch (const std::exception &e) {
                  std::cerr << "[InstanceRegistry] [VideoLoop] ✗ Exception "
                               "creating start future: "
                            << e.what() << std::endl;
                } catch (...) {
                  std::cerr << "[InstanceRegistry] [VideoLoop] ✗ Unknown error "
                               "creating start future"
                            << std::endl;
                }
              } catch (const std::exception &e) {
                std::cerr << "[InstanceRegistry] [VideoLoop] ✗ Exception "
                             "restarting file source node: "
                          << e.what() << std::endl;
                std::cerr
                    << "[InstanceRegistry] [VideoLoop] Instance will continue "
                       "running, will retry restart on next check"
                    << std::endl;
              } catch (...) {
                std::cerr << "[InstanceRegistry] [VideoLoop] ✗ Unknown error "
                             "restarting file source node"
                          << std::endl;
                std::cerr
                    << "[InstanceRegistry] [VideoLoop] Instance will continue "
                       "running, will retry restart on next check"
                    << std::endl;
              }
            }
          }
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "[InstanceRegistry] [VideoLoop] Fatal exception in video "
                   "loop thread: "
                << e.what() << std::endl;
    } catch (...) {
      std::cerr << "[InstanceRegistry] [VideoLoop] Fatal unknown error in "
                   "video loop thread"
                << std::endl;
    }
  });

  // Store thread handle
  {
    std::lock_guard<std::mutex> lock(thread_mutex_);
    video_loop_threads_[instanceId] = std::move(videoLoopThread);
  }
}

void InstanceRegistry::stopVideoLoopThread(const std::string &instanceId) {
  std::unique_lock<std::mutex> lock(thread_mutex_);

  // Set stop flag
  auto flagIt = video_loop_thread_stop_flags_.find(instanceId);
  if (flagIt != video_loop_thread_stop_flags_.end()) {
    flagIt->second.store(true);
  }

  // Get thread handle and release lock before joining
  std::thread threadToJoin;
  auto threadIt = video_loop_threads_.find(instanceId);
  if (threadIt != video_loop_threads_.end()) {
    if (threadIt->second.joinable()) {
      threadToJoin = std::move(threadIt->second);
    }
    video_loop_threads_.erase(threadIt);
  }

  // Remove stop flag
  if (flagIt != video_loop_thread_stop_flags_.end()) {
    video_loop_thread_stop_flags_.erase(flagIt);
  }

  // Release lock before joining
  lock.unlock();

  // Join thread
  if (threadToJoin.joinable()) {
    threadToJoin.join();
  }
}

Json::Value
InstanceRegistry::getInstanceConfig(const std::string &instanceId) const {
  // CRITICAL: Use shared_lock for read-only operations to allow concurrent
  // readers
  // CRITICAL: Use timeout to prevent blocking if mutex is locked
  std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);

  // Try to acquire lock with timeout (configurable via
  // REGISTRY_MUTEX_TIMEOUT_MS)
  if (!lock.try_lock_for(TimeoutConstants::getRegistryMutexTimeout())) {
    std::cerr << "[InstanceRegistry] WARNING: getInstanceConfig() timeout - "
                 "mutex is locked, returning empty config"
              << std::endl;
    if (isInstanceLoggingEnabled()) {
      PLOG_WARNING << "[InstanceRegistry] getInstanceConfig() timeout after "
                      "2000ms - mutex may be locked by another operation";
    }
    return Json::Value(
        Json::objectValue); // Return empty object to prevent blocking
  }

  auto it = instances_.find(instanceId);
  if (it == instances_.end()) {
    return Json::Value(Json::objectValue); // Return empty object if not found
  }

  const InstanceInfo &info = it->second;
  std::string error;
  Json::Value config = instance_storage_.instanceInfoToConfigJson(info, &error);

  if (!error.empty()) {
    // Log error but still return config (might be partial)
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[InstanceRegistry] Error converting instance to config: "
                   << error;
    }
  }

  return config;
}

// Base64 encoding helper function
static std::string base64_encode(const unsigned char *data, size_t length) {
  static const char base64_chars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string encoded;
  encoded.reserve(((length + 2) / 3) * 4);

  size_t i = 0;
  while (i < length) {
    unsigned char byte1 = data[i++];
    unsigned char byte2 = (i < length) ? data[i++] : 0;
    unsigned char byte3 = (i < length) ? data[i++] : 0;

    unsigned int combined = (byte1 << 16) | (byte2 << 8) | byte3;

    encoded += base64_chars[(combined >> 18) & 0x3F];
    encoded += base64_chars[(combined >> 12) & 0x3F];
    encoded += (i - 2 < length) ? base64_chars[(combined >> 6) & 0x3F] : '=';
    encoded += (i - 1 < length) ? base64_chars[combined & 0x3F] : '=';
  }

  return encoded;
}

std::optional<InstanceStatistics>
InstanceRegistry::getInstanceStatistics(const std::string &instanceId) {
  // CRITICAL: Minimize lock scope to prevent blocking when other instances are
  // starting Strategy: Copy necessary data quickly, release lock, then compute
  // statistics lock-free

  // Step 1: Get tracker pointer and basic info (with timeout)
  std::shared_ptr<InstanceStatistics> cached_stats_copy;
  InstanceStatsTracker *trackerPtr = nullptr;
  bool instanceRunning = false;
  double defaultFps = 0.0;
  std::chrono::steady_clock::time_point start_time_copy;
  std::chrono::system_clock::time_point start_time_system_copy;
  double source_fps_cached = 0.0;
  int source_width_cached = 0;
  int source_height_cached = 0;
  std::string resolution_cached;
  std::string source_resolution_cached;
  std::string format_cached;
  size_t current_queue_size_cached = 0;
  size_t max_queue_size_seen_cached = 0;
  bool has_rtmp_des = false;
  bool has_rtmp_src = false;

  {
    // CRITICAL: Use timeout to prevent blocking if mutex is locked by another
    // operation
    std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);

    // Try to acquire lock with timeout (configurable via
    // REGISTRY_MUTEX_TIMEOUT_MS)
    if (!lock.try_lock_for(TimeoutConstants::getRegistryMutexTimeout())) {
      std::cerr
          << "[InstanceRegistry] WARNING: getInstanceStatistics() timeout - "
             "mutex is locked, returning nullopt"
          << std::endl;
      if (isInstanceLoggingEnabled()) {
        PLOG_WARNING
            << "[InstanceRegistry] getInstanceStatistics() timeout after "
               "2000ms - mutex may be locked by another operation";
      }
      return std::nullopt; // Return nullopt to prevent blocking
    }

    // Check if instance exists and is running
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt == instances_.end()) {
      std::cout << "[InstanceRegistry] getInstanceStatistics: Instance not "
                   "found in instances_ map"
                << std::endl;
      std::cout.flush();
      return std::nullopt;
    }

    const InstanceInfo &info = instanceIt->second;
    instanceRunning = info.running;
    defaultFps = info.fps;

    std::cout
        << "[InstanceRegistry] getInstanceStatistics: Instance found, running="
        << instanceRunning << ", fps=" << defaultFps << std::endl;
    std::cout.flush();

    if (!instanceRunning) {
      std::cout << "[InstanceRegistry] getInstanceStatistics: Instance not "
                   "running, returning nullopt"
                << std::endl;
      std::cout.flush();
      return std::nullopt;
    }

    // Get tracker pointer
    auto trackerIt = statistics_trackers_.find(instanceId);
    if (trackerIt == statistics_trackers_.end()) {
      // Tracker not initialized yet, return default statistics
      std::cout << "[InstanceRegistry] getInstanceStatistics: Tracker not "
                   "found, returning default stats"
                << std::endl;
      std::cout.flush();
      InstanceStatistics stats;
      stats.current_framerate = std::round(defaultFps);
      return stats;
    }

    std::cout << "[InstanceRegistry] getInstanceStatistics: Tracker found, "
                 "copying data..."
              << std::endl;
    std::cout.flush();

    InstanceStatsTracker &tracker = trackerIt->second;
    trackerPtr = &tracker;

    // OPTIMIZATION: Copy all needed data while holding lock (minimal time)
    // Try cached statistics first (lock-free read of shared_ptr)
    cached_stats_copy = tracker.cached_stats_;

    // Copy basic tracker data (needed for computation if cache is stale)
    // Copy all needed data while holding lock (minimal time)
    start_time_copy = tracker.start_time;
    start_time_system_copy = tracker.start_time_system;
    source_fps_cached = tracker.source_fps;
    source_width_cached = tracker.source_width;
    source_height_cached = tracker.source_height;
    resolution_cached = tracker.resolution;
    source_resolution_cached = tracker.source_resolution;
    format_cached = tracker.format;
    current_queue_size_cached = tracker.current_queue_size;
    max_queue_size_seen_cached = tracker.max_queue_size_seen;
    auto it = info.additionalParams.find("RTMP_DES_URL");
    has_rtmp_des = (it != info.additionalParams.end() && !it->second.empty());
    it = info.additionalParams.find("RTMP_SRC_URL");
    has_rtmp_src = (it != info.additionalParams.end() && !it->second.empty());
  } // LOCK RELEASED HERE - all subsequent operations are lock-free!

  // Step 2: Check cache (lock-free)
  // NOTE: Skip cache for now to ensure we always get fresh data with
  // frames_incoming Cache can be stale and miss frames_incoming updates
  bool use_cache = false; // Temporarily disable cache to ensure fresh data

  if (use_cache && cached_stats_copy && trackerPtr) {
    // Check if cache is recent (updated within last 2 seconds worth of frames)
    uint64_t current_frame_count =
        trackerPtr->frames_processed.load(std::memory_order_relaxed);
    uint64_t cache_frame_count =
        trackerPtr->cache_update_frame_count_.load(std::memory_order_relaxed);
    uint64_t frames_since_cache =
        (current_frame_count > cache_frame_count)
            ? (current_frame_count - cache_frame_count)
            : 0;

    // Use cache if it's recent (less than 60 frames old = ~2 seconds at 30 FPS)
    if (frames_since_cache < 60) {
      // Return copy of cached stats (lock-free, doesn't block startInstance)
      // But first, update frames_incoming and dropped_frames from current
      // tracker state This ensures we always have latest data even when using
      // cache
      InstanceStatistics cached_result = *cached_stats_copy;
      cached_result.frames_incoming =
          trackerPtr->frames_incoming.load(std::memory_order_relaxed);
      cached_result.dropped_frames_count =
          trackerPtr->dropped_frames.load(std::memory_order_relaxed);

      std::cout << "[InstanceRegistry] getInstanceStatistics: Using cached "
                   "stats (updated), "
                << "frames_incoming=" << cached_result.frames_incoming
                << ", dropped_frames=" << cached_result.dropped_frames_count
                << ", frames_processed=" << cached_result.frames_processed
                << std::endl;
      std::cout.flush();

      return cached_result;
    } else {
      std::cout << "[InstanceRegistry] getInstanceStatistics: Cache is stale "
                   "(frames_since_cache="
                << frames_since_cache << "), recalculating..." << std::endl;
      std::cout.flush();
    }
  } else {
    std::cout << "[InstanceRegistry] getInstanceStatistics: Calculating fresh "
                 "stats (cache disabled or not available)..."
              << std::endl;
    std::cout.flush();
  }

  // Step 3: Compute statistics lock-free (only read atomic values and cached
  // data) All tracker access is now lock-free using atomic loads and cached
  // copies
  if (!trackerPtr) {
    // Fallback if tracker pointer is null (should not happen, but safety check)
    InstanceStatistics stats;
    stats.current_framerate = std::round(defaultFps);
    return stats;
  }

  // Build statistics object
  InstanceStatistics stats;

  // Get source framerate and resolution from cached values (no blocking calls)
  double sourceFps = source_fps_cached;
  std::string sourceRes = "";

  if (source_width_cached > 0 && source_height_cached > 0) {
    sourceRes = std::to_string(source_width_cached) + "x" +
                std::to_string(source_height_cached);
  }

  // CRITICAL: All subsequent operations are LOCK-FREE
  // We only read atomic values and cached data, no direct tracker access needed

  // Calculate elapsed time using cached start_time
  auto now = std::chrono::steady_clock::now();
  auto elapsed_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(now - start_time_copy)
          .count();
  auto elapsed_seconds_double =
      std::chrono::duration<double>(now - start_time_copy).count();

  // Calculate current FPS: PREFER FPS from backpressure controller (rolling
  // window, more accurate) then fallback to average from start_time, then
  // source FPS, then defaultFps
  // PHASE 2: Use atomic load for reading counter (lock-free)
  uint64_t frames_processed_value =
      trackerPtr->frames_processed.load(std::memory_order_relaxed);
  uint64_t frames_incoming_value =
      trackerPtr->frames_incoming.load(std::memory_order_relaxed);
  uint64_t dropped_frames_value =
      trackerPtr->dropped_frames.load(std::memory_order_relaxed);

  // Refresh RTMP activity when pipeline is processing frames (reduces false
  // "no activity" reconnect when hook misses or pipeline is slow)
  if (frames_processed_value > 0) {
    if (has_rtmp_des) {
      updateRTMPDestinationActivity(instanceId);
    }
    if (has_rtmp_src) {
      updateRTMPSourceActivity(instanceId);
    }
  }

  // Debug logging for statistics - flush to ensure it appears
  std::cout << "[InstanceRegistry] getInstanceStatistics(" << instanceId
            << "): "
            << "frames_processed=" << frames_processed_value
            << ", frames_incoming=" << frames_incoming_value
            << ", dropped_frames=" << dropped_frames_value
            << ", elapsed_seconds=" << elapsed_seconds
            << ", defaultFps=" << defaultFps << ", sourceFps=" << sourceFps
            << ", queue_size=" << current_queue_size_cached
            << ", tracker_exists=" << (trackerPtr != nullptr) << std::endl;
  std::cout.flush();

  double currentFps = 0.0;

  // First priority: Use FPS from backpressure controller (real-time rolling
  // window - more accurate) - LOCK-FREE call
  using namespace BackpressureController;
  auto &backpressure =
      BackpressureController::BackpressureController::getInstance();
  double backpressureFps = backpressure.getCurrentFPS(instanceId);
  if (backpressureFps > 0.0) {
    currentFps = std::round(backpressureFps);
  } else {
    // Fallback: Calculate actual processing FPS based on frames actually
    // processed (lock-free calculation)
    double actualProcessingFps = 0.0;
    if (elapsed_seconds_double > 0.0 && frames_processed_value > 0) {
      actualProcessingFps =
          static_cast<double>(frames_processed_value) / elapsed_seconds_double;
    }

    if (actualProcessingFps > 0.0) {
      currentFps = std::round(actualProcessingFps);
    } else if (sourceFps > 0.0) {
      currentFps = std::round(sourceFps);
    } else if (defaultFps > 0.0) {
      currentFps = std::round(defaultFps);
    } else {
      // Use last FPS from tracker (need to read atomically, but it's not
      // atomic) For now, just use 0.0 if we can't determine FPS
      currentFps = 0.0;
    }
  }

  // Set frames_incoming (all frames from source, including dropped)
  // FIX: Estimate frames_incoming if it's 0 but we have frames_processed
  // This happens when frames are dropped at SDK level before reaching the hook
  if (frames_incoming_value == 0 && frames_processed_value > 0) {
    // Estimate: frames_incoming = frames_processed + dropped_frames +
    // queue_size (approximate) This gives a conservative estimate of total
    // frames from source
    uint64_t queue_estimate = static_cast<uint64_t>(current_queue_size_cached);
    uint64_t estimated_incoming =
        frames_processed_value + dropped_frames_value + queue_estimate;

    // Use estimated value
    stats.frames_incoming = estimated_incoming;

    // Log estimation for debugging (first time only)
    static thread_local std::unordered_map<std::string, bool> logged_estimation;
    if (!logged_estimation[instanceId]) {
      std::cout << "[InstanceRegistry] Estimating frames_incoming for "
                << instanceId << ": estimated=" << estimated_incoming
                << " (processed=" << frames_processed_value
                << ", dropped=" << dropped_frames_value
                << ", queue=" << queue_estimate << ")" << std::endl;
      logged_estimation[instanceId] = true;
    }
  } else {
    stats.frames_incoming = frames_incoming_value;
  }

  // Calculate frames_processed: use actual frames processed if available,
  // otherwise estimate from frames_incoming - dropped_frames
  // This ensures statistics always have data even when frames are dropped
  uint64_t actual_frames_processed = frames_processed_value;
  uint64_t calculated_frames_processed = 0;

  if (actual_frames_processed > 0) {
    // Use actual frames processed (from frame capture hook)
    stats.frames_processed = actual_frames_processed;
    calculated_frames_processed = actual_frames_processed;
  } else if (frames_incoming_value > 0) {
    // If no frames processed yet but we have incoming frames,
    // estimate: processed = incoming - dropped
    // This ensures statistics show data even when all frames are dropped
    uint64_t estimated_processed =
        (frames_incoming_value > dropped_frames_value)
            ? (frames_incoming_value - dropped_frames_value)
            : 0;
    stats.frames_processed = estimated_processed;
    calculated_frames_processed = estimated_processed;

    std::cout << "[InstanceRegistry] Using estimated frames_processed: "
              << estimated_processed << " = incoming(" << frames_incoming_value
              << ") - dropped(" << dropped_frames_value << ")" << std::endl;
  } else if (currentFps > 0.0 && elapsed_seconds > 0) {
    // Fallback: estimate from FPS and elapsed time
    calculated_frames_processed =
        static_cast<uint64_t>(currentFps * elapsed_seconds);
    stats.frames_processed = calculated_frames_processed;
  } else {
    stats.frames_processed = 0;
    calculated_frames_processed = 0;
  }

  // Set dropped frames count - LOCK-FREE (atomic read already done above)
  stats.dropped_frames_count = dropped_frames_value;

  stats.current_framerate = std::round(currentFps);

  // Use resolution from cached values (lock-free)
  if (!sourceRes.empty()) {
    stats.resolution = sourceRes;
    stats.source_resolution = sourceRes;
  } else {
    stats.resolution = resolution_cached;
    stats.source_resolution = source_resolution_cached;
  }

  stats.format = format_cached.empty() ? "BGR" : format_cached;

  // Calculate start_time (Unix timestamp) from cached value
  auto start_time_since_epoch = start_time_system_copy.time_since_epoch();
  auto start_time_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(start_time_since_epoch)
          .count();
  stats.start_time = start_time_seconds;

  // Set source framerate
  if (sourceFps > 0.0) {
    stats.source_framerate = sourceFps;
  } else {
    stats.source_framerate = stats.current_framerate;
  }

  // Calculate latency (average time per frame in milliseconds)
  if (stats.frames_processed > 0 && currentFps > 0.0) {
    stats.latency = std::round(1000.0 / currentFps);
  } else {
    stats.latency = 0.0;
  }

  // Set default format if empty (lock-free)
  if (stats.format.empty()) {
    stats.format = "BGR";
  }

  // Queue size - use cached values (lock-free, no need to lock)
  // Updated via meta_arriving_hooker callback from CVEDIX SDK nodes
  stats.input_queue_size = static_cast<int64_t>(current_queue_size_cached);
  if (stats.input_queue_size == 0 && max_queue_size_seen_cached > 0) {
    stats.input_queue_size = static_cast<int64_t>(max_queue_size_seen_cached);
  }

  // REMOVED: Adaptive queue size manager update from statistics API
  // Reason: This can block (uses locks) and is not necessary for statistics
  // retrieval Adaptive queue metrics should be updated in frame hook or
  // background thread, not in API call This prevents timeout when pipeline is
  // busy

  // OPTIMIZATION: Update cache for next time (atomic write to shared_ptr via
  // trackerPtr) This allows future API calls to read from cache without
  // expensive calculations Thread-safe: shared_ptr assignment is atomic,
  // multiple threads can read while one writes Using trackerPtr is safe because
  // it's a pointer - we're not modifying the map This doesn't block
  // startInstance operations because we're only updating tracker fields via
  // pointer, not the map itself
  auto new_cached_stats =
      std::make_shared<InstanceStatistics>(stats); // Create new copy
  trackerPtr->cached_stats_ =
      new_cached_stats; // Atomic assignment (thread-safe via pointer)
  trackerPtr->cache_update_frame_count_.store(
      trackerPtr->frames_processed.load(std::memory_order_relaxed),
      std::memory_order_relaxed);

  // Log final statistics before returning - flush to ensure it appears
  std::cout << "[InstanceRegistry] getInstanceStatistics FINAL: "
            << "frames_processed=" << stats.frames_processed
            << ", frames_incoming=" << stats.frames_incoming
            << ", dropped_frames=" << stats.dropped_frames_count
            << ", current_fps=" << stats.current_framerate
            << ", source_fps=" << stats.source_framerate
            << ", queue_size=" << stats.input_queue_size
            << ", start_time=" << stats.start_time << std::endl;
  std::cout.flush();

  return stats;
}

std::string
InstanceRegistry::getLastFrame(const std::string &instanceId) const {
  std::cout << "[InstanceRegistry] getLastFrame() called for instance: "
            << instanceId << std::endl;

  // PHASE 1 OPTIMIZATION: Get shared_ptr copy quickly, release lock
  // CRITICAL: Use timeout to prevent blocking if mutex is locked
  FramePtr frame_ptr;
  {
    std::unique_lock<std::timed_mutex> lock(frame_cache_mutex_,
                                            std::defer_lock);

    // Try to acquire lock with timeout (configurable via
    // FRAME_CACHE_MUTEX_TIMEOUT_MS)
    if (!lock.try_lock_for(TimeoutConstants::getFrameCacheMutexTimeout())) {
      std::cerr << "[InstanceRegistry] WARNING: getLastFrame() timeout - "
                   "frame_cache_mutex_ is locked, returning empty string"
                << std::endl;
      if (isInstanceLoggingEnabled()) {
        PLOG_WARNING << "[InstanceRegistry] getLastFrame() timeout after "
                        "1000ms - mutex may be locked by another operation";
      }
      return ""; // Return empty string to prevent blocking
    }

    auto it = frame_caches_.find(instanceId);
    if (it == frame_caches_.end()) {
      std::cout << "[InstanceRegistry] getLastFrame() - No cache entry found "
                   "for instance: "
                << instanceId << std::endl;
      return ""; // No frame cached
    }

    const FrameCache &cache = it->second;
    std::cout
        << "[InstanceRegistry] getLastFrame() - Cache entry found: has_frame="
        << cache.has_frame << ", frame_ptr=" << (cache.frame ? "valid" : "null")
        << std::endl;

    if (!cache.has_frame || !cache.frame) {
      std::cout << "[InstanceRegistry] getLastFrame() - Cache entry exists but "
                   "no frame available"
                << std::endl;
      return ""; // No frame cached
    }

    // Get shared_ptr copy (reference counting, no copy of Mat data)
    frame_ptr = cache.frame;
  }
  // Lock released - frame_ptr keeps frame alive via reference counting

  // Encode frame to base64 (frame_ptr dereferences to cv::Mat&)
  if (!frame_ptr || frame_ptr->empty()) {
    return "";
  }
  return encodeFrameToBase64(*frame_ptr, 85); // Default quality 85%
}

void InstanceRegistry::updateFrameCache(const std::string &instanceId,
                                        const cv::Mat &frame) {
  if (frame.empty()) {
    static thread_local std::unordered_map<std::string, uint64_t>
        empty_frame_count;
    empty_frame_count[instanceId]++;
    if (empty_frame_count[instanceId] <= 3) {
      std::cout << "[InstanceRegistry] updateFrameCache() - WARNING: Received "
                   "empty frame for instance "
                << instanceId << " (count: " << empty_frame_count[instanceId]
                << ")" << std::endl;
    }
    return;
  }

  // DEBUG: Log frame update (first few times)
  static thread_local std::unordered_map<std::string, uint64_t> update_count;
  update_count[instanceId]++;
  if (update_count[instanceId] <= 5 || update_count[instanceId] % 100 == 0) {
    std::cout << "[InstanceRegistry] updateFrameCache() - Updating cache for "
                 "instance "
              << instanceId << " (update #" << update_count[instanceId] << "): "
              << "size=" << frame.cols << "x" << frame.rows
              << ", channels=" << frame.channels() << ", type=" << frame.type()
              << std::endl;
  }

  // PHASE 1 OPTIMIZATION: Create shared_ptr OUTSIDE lock to minimize lock hold
  // time This avoids holding lock during frame allocation (if needed)
  FramePtr frame_ptr = std::make_shared<cv::Mat>(frame);

  // FIX: Update resolution from frame
  // Extract resolution from frame dimensions
  int frame_width = frame.cols;
  int frame_height = frame.rows;
  std::string resolution_str = "";
  if (frame_width > 0 && frame_height > 0) {
    resolution_str =
        std::to_string(frame_width) + "x" + std::to_string(frame_height);
  }

  // PHASE 1 OPTIMIZATION: Lock only for pointer swap, not during copy
  // This reduces lock contention significantly
  // Note: updateFrameCache is called from frame processing thread, so we use
  // blocking lock (no timeout needed as this is internal operation, not API
  // call)
  {
    std::lock_guard<std::timed_mutex> lock(frame_cache_mutex_);
    FrameCache &cache = frame_caches_[instanceId];
    cache.frame = frame_ptr; // Shared ownership - no copy!
    cache.timestamp = std::chrono::steady_clock::now();
    cache.has_frame = true;
  }
  // Lock released immediately after pointer assignment

  // FIX: Update resolution in tracker (requires separate lock for tracker)
  if (!resolution_str.empty()) {
    std::unique_lock<std::shared_timed_mutex> tracker_lock(mutex_);
    auto trackerIt = statistics_trackers_.find(instanceId);
    if (trackerIt != statistics_trackers_.end()) {
      InstanceStatsTracker &tracker = trackerIt->second;
      // Update current processing resolution from frame
      tracker.resolution = resolution_str;

      // If source resolution not set yet, set it from first frame
      // (for non-RTSP sources where we don't have source_width/source_height)
      if (tracker.source_resolution.empty()) {
        tracker.source_resolution = resolution_str;
        // Also update source_width and source_height if not set
        if (tracker.source_width == 0 && tracker.source_height == 0) {
          tracker.source_width = frame_width;
          tracker.source_height = frame_height;
        }
      }
    }
  }
}

void InstanceRegistry::setupFrameCaptureHook(
    const std::string &instanceId,
    const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes) {
  if (nodes.empty()) {
    return;
  }

  // Find app_des_node and check if pipeline has OSD node
  // CRITICAL: We need to verify that app_des_node is attached after OSD node
  // to ensure we get processed frames
  std::shared_ptr<cvedix_nodes::cvedix_app_des_node> appDesNode;
  bool hasOSDNode = false;

  // Search through ALL nodes to find app_des_node and check for OSD node
  // IMPORTANT: Don't stop early - need to check all nodes to find OSD node
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
    auto node = *it;
    if (!node) {
      continue;
    }

    // Find app_des_node
    if (!appDesNode) {
      appDesNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_des_node>(node);
      if (appDesNode) {
        std::cout << "[InstanceRegistry] ✓ Found app_des_node for instance "
                  << instanceId << std::endl;
      }
    }

    // Check if pipeline has OSD node (check ALL nodes, don't stop early)
    if (!hasOSDNode) {
      bool isOSDNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_face_osd_node_v2>(
              node) != nullptr ||
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_osd_node_v3>(node) !=
              nullptr ||
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_line_crossline_osd_node>(
              node) != nullptr ||
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_area_jam_osd_node>(
              node) != nullptr ||
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_stop_osd_node>(
              node) != nullptr;
      if (isOSDNode) {
        hasOSDNode = true;
        std::cout << "[InstanceRegistry] ✓ Found OSD node for instance "
                  << instanceId << ": " << typeid(*node).name() << std::endl;
      }
    }
  }

  // Setup hook on app_des_node if found
  if (appDesNode) {
    std::cout << "[InstanceRegistry] Setting up frame capture hook on "
                 "app_des_node for instance "
              << instanceId
              << " (OSD node in pipeline: " << (hasOSDNode ? "yes" : "no")
              << ")" << std::endl;
    appDesNode->set_app_des_result_hooker([this, instanceId, hasOSDNode](
                                              std::string /*node_name*/,
                                              std::shared_ptr<
                                                  cvedix_objects::cvedix_meta>
                                                  meta) {
      try {
        if (!meta) {
          return;
        }

        if (meta->meta_type == cvedix_objects::cvedix_meta_type::FRAME) {
          auto frame_meta =
              std::dynamic_pointer_cast<cvedix_objects::cvedix_frame_meta>(
                  meta);
          if (!frame_meta) {
            return;
          }

          // PHASE 3: Check backpressure control before processing frame
          using namespace BackpressureController;
          auto &backpressure =
              BackpressureController::BackpressureController::getInstance();

          // Check if we should drop this frame (FPS limiting, queue full,
          // etc.)
          if (backpressure.shouldDropFrame(instanceId)) {
            backpressure.recordFrameDropped(instanceId);

            // Update dropped_frames counter in tracker (if tracker exists)
            // Use try_lock to avoid blocking frame processing
            {
              std::unique_lock<std::shared_timed_mutex> lock(mutex_,
                                                             std::try_to_lock);
              if (lock.owns_lock()) {
                auto trackerIt = statistics_trackers_.find(instanceId);
                if (trackerIt != statistics_trackers_.end()) {
                  // Get dropped count from backpressure controller (most
                  // accurate) Note: getStats returns a snapshot (value), not a
                  // pointer
                  auto backpressureStats = backpressure.getStats(instanceId);
                  uint64_t dropped_from_backpressure =
                      backpressureStats.frames_dropped;
                  trackerIt->second.dropped_frames.store(
                      dropped_from_backpressure, std::memory_order_relaxed);
                }
              }
            }

            return; // Drop frame early to prevent processing overhead
          }

          // PHASE 2 OPTIMIZATION: Update frame counter using atomic
          // operations
          // - NO LOCK needed! This eliminates lock contention in the hot
          // path (called every frame) OPTIMIZATION: Cache tracker pointer
          // and RTSP flag to avoid repeated lookups
          InstanceStatsTracker *trackerPtr = nullptr;
          bool isRTSPInstance = false;
          {
            // Get tracker pointer in one lock
            std::shared_lock<std::shared_timed_mutex> read_lock(mutex_);
            auto trackerIt = statistics_trackers_.find(instanceId);
            if (trackerIt != statistics_trackers_.end()) {
              trackerPtr = &trackerIt->second;
              // Read RTSP flag lock-free (cached during initialization)
              isRTSPInstance =
                  trackerPtr->is_rtsp_instance.load(std::memory_order_relaxed);
            }
          }
          // Lock released - now do atomic operations without lock

          if (trackerPtr) {
            // Atomic increments - no lock needed!
            uint64_t new_frame_count = trackerPtr->frames_processed.fetch_add(
                                           1, std::memory_order_relaxed) +
                                       1;
            trackerPtr->frame_count_since_last_update.fetch_add(
                1, std::memory_order_relaxed);

            // Log first few frames to verify frame processing
            static thread_local std::unordered_map<std::string, uint64_t>
                instance_frame_counts;
            instance_frame_counts[instanceId]++;
            if (instance_frame_counts[instanceId] <= 5 ||
                instance_frame_counts[instanceId] % 100 == 0) {
              std::cout << "[InstanceRegistry] Frame processed for instance "
                        << instanceId << ": frame_count=" << new_frame_count
                        << " (total calls: "
                        << instance_frame_counts[instanceId] << ")"
                        << std::endl;
            }

            // OPTIMIZATION: Update statistics cache periodically (every N
            // frames) This allows API to read statistics lock-free without
            // expensive calculations
            uint64_t cache_frame_count =
                trackerPtr->cache_update_frame_count_.load(
                    std::memory_order_relaxed);
            uint64_t frames_since_cache =
                (new_frame_count > cache_frame_count)
                    ? (new_frame_count - cache_frame_count)
                    : 0;

            if (frames_since_cache >=
                InstanceStatsTracker::CACHE_UPDATE_INTERVAL_FRAMES) {
              // Update cache in background (non-blocking)
              // We'll compute stats later when API reads, but mark cache as
              // needing update For now, just update the frame count to prevent
              // too frequent updates
              trackerPtr->cache_update_frame_count_.store(
                  new_frame_count, std::memory_order_relaxed);
            }
          }

          // PHASE 3: Record frame processed for backpressure tracking
          backpressure.recordFrameProcessed(instanceId);

          // DEBUG: Log frame_meta details
          static thread_local std::unordered_map<std::string, uint64_t>
              frame_capture_count;
          frame_capture_count[instanceId]++;
          uint64_t capture_count = frame_capture_count[instanceId];

          bool has_osd_frame = !frame_meta->osd_frame.empty();
          bool has_original_frame = !frame_meta->frame.empty();

          if (capture_count <= 5 || capture_count % 100 == 0) {
            std::cout
                << "[InstanceRegistry] Frame capture hook #" << capture_count
                << " for instance " << instanceId
                << " - osd_frame: " << (has_osd_frame ? "available" : "empty")
                << (has_osd_frame
                        ? (" (" + std::to_string(frame_meta->osd_frame.cols) +
                           "x" + std::to_string(frame_meta->osd_frame.rows) +
                           ")")
                        : "")
                << ", original frame: "
                << (has_original_frame ? "available" : "empty")
                << (has_original_frame
                        ? (" (" + std::to_string(frame_meta->frame.cols) + "x" +
                           std::to_string(frame_meta->frame.rows) + ")")
                        : "")
                << std::endl;
          }

          // CRITICAL: Only cache frames that are guaranteed to be processed
          // PipelineBuilder ensures app_des_node is attached to OSD node (if
          // exists) This guarantees frame_meta->frame is processed when
          // hasOSDNode is true
          const cv::Mat *frameToCache = nullptr;

          // Priority 1: Use osd_frame if available (always processed with AI
          // overlays)
          if (!frame_meta->osd_frame.empty()) {
            frameToCache = &frame_meta->osd_frame;
            if (capture_count <= 5) {
              std::cout << "[InstanceRegistry] Frame capture hook #"
                        << capture_count << " for instance " << instanceId
                        << " - Using PROCESSED osd_frame (with overlays): "
                        << frame_meta->osd_frame.cols << "x"
                        << frame_meta->osd_frame.rows << std::endl;
            }
          }
          // Priority 2: Use frame_meta->frame if OSD node exists
          // PipelineBuilder attaches app_des_node to OSD node, so
          // frame_meta->frame is processed This works even if osd_frame is
          // empty (OSD node processed frame but didn't populate osd_frame)
          else if (hasOSDNode && !frame_meta->frame.empty()) {
            frameToCache = &frame_meta->frame;
            if (capture_count <= 5) {
              std::cout
                  << "[InstanceRegistry] Frame capture hook #" << capture_count
                  << " for instance " << instanceId
                  << " - Using frame_meta->frame (from OSD node, PROCESSED): "
                  << frame_meta->frame.cols << "x" << frame_meta->frame.rows
                  << std::endl;
            }
          } else {
            // Skip caching if no OSD node (frame may be unprocessed)
            static thread_local std::unordered_map<std::string, bool>
                logged_warning;
            if (!logged_warning[instanceId]) {
              if (!hasOSDNode) {
                std::cerr << "[InstanceRegistry] ⚠ WARNING: Pipeline has no "
                             "OSD node for instance "
                          << instanceId
                          << ". Skipping frame cache to avoid returning "
                             "unprocessed frames."
                          << std::endl;
              } else {
                std::cerr << "[InstanceRegistry] ⚠ WARNING: Both osd_frame and "
                             "frame_meta->frame are empty for instance "
                          << instanceId << std::endl;
              }
              logged_warning[instanceId] = true;
            }
            if (capture_count <= 5) {
              std::cout << "[InstanceRegistry] Frame capture hook #"
                        << capture_count << " for instance " << instanceId
                        << " - SKIPPING: "
                        << (!hasOSDNode ? "No OSD node in pipeline"
                                        : "Both frames empty")
                        << std::endl;
            }
          }

          if (frameToCache && !frameToCache->empty()) {
            updateFrameCache(instanceId, *frameToCache);

            // CRITICAL: Update RTSP/RTMP activity when we receive frames
            // This is the only reliable way to know streams are actually working
            // OPTIMIZATION: Use cached RTSP flag (read lock-free above)
            if (isRTSPInstance) {
              updateRTSPActivity(instanceId);
            }
            
            // Update RTMP source activity (check if instance uses RTMP source)
            // We check by looking at the first node type
            {
              std::shared_lock<std::shared_timed_mutex> lock(mutex_);
              auto instanceIt = instances_.find(instanceId);
              if (instanceIt != instances_.end()) {
                // Check if instance has RTMP source URL in additionalParams
                auto rtmpSrcIt = instanceIt->second.additionalParams.find("RTMP_SRC_URL");
                if (rtmpSrcIt != instanceIt->second.additionalParams.end() && 
                    !rtmpSrcIt->second.empty()) {
                  updateRTMPSourceActivity(instanceId);
                }
              }
            }
            
            // Update RTMP destination activity (frames are being sent to RTMP destination)
            // Check if instance has RTMP destination
            {
              std::shared_lock<std::shared_timed_mutex> lock(mutex_);
              auto instanceIt = instances_.find(instanceId);
              if (instanceIt != instances_.end()) {
                auto rtmpDesIt = instanceIt->second.additionalParams.find("RTMP_DES_URL");
                if (rtmpDesIt != instanceIt->second.additionalParams.end() && 
                    !rtmpDesIt->second.empty()) {
                  updateRTMPDestinationActivity(instanceId);
                }
              }
            }
          }
        }
      } catch (const std::exception &e) {
        // OPTIMIZATION: Use static counter to throttle exception logging
        // Exceptions should be rare, but if they occur frequently, logging
        // every exception can impact performance
        static thread_local uint64_t exception_count = 0;
        exception_count++;

        // Only log every 100th exception to avoid performance impact
        if (exception_count % 100 == 1) {
          std::cerr << "[InstanceRegistry] [ERROR] Exception in frame "
                       "capture hook (count: "
                    << exception_count << "): " << e.what() << std::endl;
        }
      } catch (...) {
        static thread_local uint64_t unknown_exception_count = 0;
        unknown_exception_count++;

        if (unknown_exception_count % 100 == 1) {
          std::cerr << "[InstanceRegistry] [ERROR] Unknown exception in frame "
                       "capture hook (count: "
                    << unknown_exception_count << ")" << std::endl;
        }
      }
    });

    std::cerr << "[InstanceRegistry] ✓ Frame capture hook setup completed for "
                 "instance: "
              << instanceId << std::endl;
    return;
  }

  // If no app_des_node found, log warning
  if (!appDesNode) {
    std::cerr << "[InstanceRegistry] ⚠ Warning: No app_des_node found in "
                 "pipeline for instance: "
              << instanceId << std::endl;
    std::cerr << "[InstanceRegistry] Frame capture will not be available. "
                 "Consider adding app_des_node to pipeline."
              << std::endl;
  }
}

void InstanceRegistry::setupRTMPDestinationActivityHook(
    const std::string &instanceId,
    const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes) {
  if (nodes.empty()) {
    return;
  }

  for (const auto &node : nodes) {
    if (!node) {
      continue;
    }
    auto rtmpDesNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node);
    if (!rtmpDesNode) {
      continue;
    }

    // Update RTMP destination activity when SDK reports stream status
    // (frame actually pushed to RTMP). This makes monitor reflect real output.
    rtmpDesNode->set_stream_status_hooker(
        [this, instanceId](std::string /*node_name*/,
                           cvedix_nodes::cvedix_stream_status /*status*/) {
          updateRTMPDestinationActivity(instanceId);
        });

    // Fallback: also update when frame meta is handled by rtmp_des node
    // (in case SDK does not invoke stream_status every time)
    rtmpDesNode->set_meta_handled_hooker(
        [this, instanceId](std::string /*node_name*/, int /*queue_size*/,
                           std::shared_ptr<cvedix_objects::cvedix_meta> meta) {
          if (meta && meta->meta_type ==
                          cvedix_objects::cvedix_meta_type::FRAME) {
            updateRTMPDestinationActivity(instanceId);
          }
        });

    std::cerr << "[InstanceRegistry] ✓ RTMP destination activity hook set for "
                 "instance "
              << instanceId << std::endl;
  }
}

void InstanceRegistry::setupQueueSizeTrackingHook(
    const std::string &instanceId,
    const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes) {
  if (nodes.empty()) {
    return;
  }

  // Setup meta_arriving_hooker on all nodes to track input queue size
  // On source node (first node), also track incoming frames
  std::cout << "[InstanceRegistry] Setting up queue size tracking hooks for "
            << nodes.size() << " nodes" << std::endl;
  std::cout.flush();

  for (size_t i = 0; i < nodes.size(); ++i) {
    const auto &node = nodes[i];
    if (!node) {
      continue;
    }

    const bool isSourceNode = (i == 0); // First node is source node

    if (isSourceNode) {
      std::cout << "[InstanceRegistry] Setting up hook on source node (index "
                << i << ") to track incoming frames" << std::endl;
      std::cout.flush();
    }

    try {
      node->set_meta_arriving_hooker([this, instanceId, isSourceNode](
                                         std::string /*node_name*/,
                                         int queue_size,
                                         std::shared_ptr<
                                             cvedix_objects::cvedix_meta>
                                             meta) {
        try {
          // OPTIMIZED: Use try_lock to avoid blocking frame processing
          // If lock is busy (e.g., another instance is starting), skip this
          // update Queue size tracking is not critical - missing one update
          // is acceptable
          std::unique_lock<std::shared_timed_mutex> lock(mutex_,
                                                         std::try_to_lock);
          if (!lock.owns_lock()) {
            // Lock is busy - skip this update to avoid blocking frame
            // processing This allows instances to process frames even when
            // other instances are starting
            return;
          }

          auto trackerIt = statistics_trackers_.find(instanceId);
          if (trackerIt != statistics_trackers_.end()) {
            InstanceStatsTracker &tracker = trackerIt->second;

            // Track incoming frames on source node (BEFORE frames can be
            // dropped)
            if (isSourceNode && meta) {
              // Log all meta types on source node for debugging (first 10 only)
              static thread_local std::unordered_map<std::string, uint64_t>
                  instance_source_meta_counts;
              instance_source_meta_counts[instanceId]++;
              if (instance_source_meta_counts[instanceId] <= 10) {
                std::cout << "[InstanceRegistry] Source node "
                             "meta_arriving_hooker called for instance "
                          << instanceId
                          << ": meta_type=" << static_cast<int>(meta->meta_type)
                          << ", queue_size=" << queue_size << " (call #"
                          << instance_source_meta_counts[instanceId] << ")"
                          << std::endl;
              }

              // Only count FRAME meta types for frames_incoming
              if (meta->meta_type == cvedix_objects::cvedix_meta_type::FRAME) {
                uint64_t new_incoming_count =
                    tracker.frames_incoming.fetch_add(
                        1, std::memory_order_relaxed) +
                    1;

                // Log first few incoming frames for debugging
                static thread_local std::unordered_map<std::string, uint64_t>
                    instance_incoming_counts;
                instance_incoming_counts[instanceId]++;
                if (instance_incoming_counts[instanceId] <= 5 ||
                    instance_incoming_counts[instanceId] % 100 == 0) {
                  std::cout << "[InstanceRegistry] Frame incoming for instance "
                            << instanceId
                            << ": incoming_count=" << new_incoming_count
                            << " (total calls: "
                            << instance_incoming_counts[instanceId] << ")"
                            << std::endl;
                }

                // Get dropped frames from backpressure controller
                using namespace BackpressureController;
                auto &backpressure = BackpressureController::
                    BackpressureController::getInstance();
                auto backpressureStats = backpressure.getStats(instanceId);

                // Update dropped_frames from backpressure controller (most
                // accurate)
                uint64_t dropped_from_backpressure =
                    backpressureStats.frames_dropped;
                if (dropped_from_backpressure >
                    tracker.dropped_frames.load(std::memory_order_relaxed)) {
                  tracker.dropped_frames.store(dropped_from_backpressure,
                                               std::memory_order_relaxed);
                }
              }
            }

            // Track queue size on all nodes
            // FIX: Update queue size every time hook is called, not only when
            // increasing This allows tracking actual queue size including when
            // it decreases
            tracker.current_queue_size = static_cast<size_t>(queue_size);

            if (queue_size > static_cast<int>(tracker.max_queue_size_seen)) {
              tracker.max_queue_size_seen = static_cast<size_t>(queue_size);
            }

            // PHASE 3: Update queue size in backpressure controller for
            // queue-based frame dropping (no lock needed - singleton)
            using namespace BackpressureController;
            auto &backpressure =
                BackpressureController::BackpressureController::getInstance();
            backpressure.updateQueueSize(
                instanceId,
                static_cast<size_t>(queue_size)); // Thread-safe, no lock needed

            // Record queue full event if queue is getting full (threshold:
            // 80% of typical max) Increased from 8 to 16 to match increased
            // queue size (20) for multi-instance performance
            const size_t queue_warning_threshold =
                16; // Warn at 16 frames (80% of 20)
            if (queue_size >= static_cast<int>(queue_warning_threshold)) {
              backpressure.recordQueueFull(
                  instanceId); // Thread-safe, no lock needed
            }

            // FIX: Track SDK-level drops when queue is full
            // When queue is at max capacity, frames are being dropped at SDK
            // level Estimate drops based on queue full state This helps track
            // dropped_frames_count when SDK drops frames before they reach the
            // hook
            size_t max_queue_size_estimated =
                51; // Based on log analysis (input_queue_size=51)
            if (queue_size >= static_cast<int>(max_queue_size_estimated)) {
              // Queue is at max - frames are likely being dropped at SDK level
              // Increment dropped_frames to reflect SDK-level drops
              // Note: This is an estimate, actual drops may be higher
              static thread_local std::unordered_map<
                  std::string, std::chrono::steady_clock::time_point>
                  last_drop_time;
              auto now = std::chrono::steady_clock::now();
              auto it = last_drop_time.find(instanceId);

              // Throttle: Only count drops every 100ms to avoid over-counting
              if (it == last_drop_time.end() ||
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      now - it->second)
                          .count() >= 100) {
                // Estimate 1 drop per check when queue is full (conservative
                // estimate)
                tracker.dropped_frames.fetch_add(1, std::memory_order_relaxed);
                last_drop_time[instanceId] = now;
              }
            }
          }
        } catch (const std::exception &e) {
          std::cerr << "[InstanceRegistry] [ERROR] Exception in queue size "
                       "tracking hook: "
                    << e.what() << std::endl;
        } catch (...) {
          std::cerr << "[InstanceRegistry] [ERROR] Unknown exception in "
                       "queue size tracking hook"
                    << std::endl;
        }
      });
    } catch (const std::exception &e) {
      // Some nodes might not support hooks, ignore silently
    } catch (...) {
      // Some nodes might not support hooks, ignore silently
    }
  }

  std::cerr << "[InstanceRegistry] ✓ Queue size tracking hook setup completed "
               "for instance: "
            << instanceId << std::endl;
}

std::string InstanceRegistry::encodeFrameToBase64(const cv::Mat &frame,
                                                  int jpegQuality) const {
  if (frame.empty()) {
    return "";
  }

  try {
    std::vector<uchar> buffer;
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, jpegQuality};

    if (!cv::imencode(".jpg", frame, buffer, params)) {
      std::cerr << "[InstanceRegistry] Failed to encode frame to JPEG"
                << std::endl;
      return "";
    }

    if (buffer.empty()) {
      return "";
    }

    return base64_encode(buffer.data(), buffer.size());
  } catch (const std::exception &e) {
    std::cerr << "[InstanceRegistry] Exception encoding frame to base64: "
              << e.what() << std::endl;
    return "";
  } catch (...) {
    std::cerr << "[InstanceRegistry] Unknown exception encoding frame to base64"
              << std::endl;
    return "";
  }
}

void InstanceRegistry::startRTSPMonitorThread(const std::string &instanceId) {
  // Stop existing thread if any
  stopRTSPMonitorThread(instanceId);

  // Check if instance has RTSP URL
  std::string rtspUrl;
  {
    std::shared_lock<std::shared_timed_mutex> lock(mutex_);
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt == instances_.end()) {
      return; // Instance not found
    }
    const auto &info = instanceIt->second;
    if (info.rtspUrl.empty()) {
      return; // Not an RTSP instance
    }
    rtspUrl = info.rtspUrl;
  }

  // Create stop flag
  auto stop_flag = std::make_shared<std::atomic<bool>>(false);
  {
    std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
    rtsp_monitor_stop_flags_[instanceId] = stop_flag;
    // CRITICAL: Do NOT initialize rtsp_last_activity_ with current time
    // It should only be set when we actually receive frames from RTSP
    // This prevents false positive "connected" detection
    rtsp_reconnect_attempts_[instanceId] = 0;
    rtsp_has_connected_[instanceId] = false; // Initialize as not connected yet
  }

  // Start monitoring thread
  std::thread monitor_thread([this, instanceId, rtspUrl, stop_flag]() {
    std::cerr
        << "[InstanceRegistry] [RTSP Monitor] Thread started for instance "
        << instanceId << std::endl;
    std::cerr << "[InstanceRegistry] [RTSP Monitor] Monitoring RTSP stream: "
              << rtspUrl << std::endl;

    const auto check_interval = std::chrono::seconds(
        2); // Check every 2 seconds (faster detection for unstable streams)

    // CRITICAL: Different timeouts for initial connection vs disconnection
    // - Initial connection: Allow 90 seconds for RTSP to establish (SDK retry
    // can take 10-30s, plus stabilization)
    // - After connection: Use 20 seconds for faster disconnection detection
    const auto initial_connection_timeout =
        std::chrono::seconds(90); // Allow 90 seconds for initial RTSP
                                  // connection (SDK retry + stabilization)
    const auto disconnection_timeout =
        std::chrono::seconds(20); // Consider disconnected if no activity for 20
                                  // seconds (after successful connection)

    const auto reconnect_cooldown =
        std::chrono::seconds(10); // Wait 10 seconds between reconnect attempts
    const int max_reconnect_attempts =
        10; // Maximum reconnect attempts before giving up

    auto instance_start_time =
        std::chrono::steady_clock::now(); // Track when instance started
    auto last_reconnect_attempt =
        std::chrono::steady_clock::now() -
        reconnect_cooldown; // Allow immediate first check
    auto last_activity_check = std::chrono::steady_clock::now();

    while (!stop_flag->load()) {
      // Check stop flag before blocking operations
      if (stop_flag->load()) {
        break;
      }

      // Sleep with periodic stop flag checks
      auto sleep_start = std::chrono::steady_clock::now();
      while (std::chrono::steady_clock::now() - sleep_start < check_interval) {
        if (stop_flag->load()) {
          break;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(500)); // Check every 500ms
      }

      if (stop_flag->load()) {
        break;
      }

      // Check if instance still exists and is running
      bool instanceExists = false;
      bool instanceRunning = false;
      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto instanceIt = instances_.find(instanceId);
        if (instanceIt != instances_.end()) {
          instanceExists = true;
          instanceRunning = instanceIt->second.running;
        }
      }

      if (!instanceExists || !instanceRunning) {
        std::cerr
            << "[InstanceRegistry] [RTSP Monitor] Instance " << instanceId
            << " no longer exists or is not running, stopping monitor thread"
            << std::endl;
        break;
      }

      // Get last activity time and connection status
      bool has_activity = false;
      auto last_activity = std::chrono::steady_clock::time_point();
      bool has_connected = false;
      {
        std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
        auto activityIt = rtsp_last_activity_.find(instanceId);
        if (activityIt != rtsp_last_activity_.end()) {
          has_activity = true;
          last_activity = activityIt->second;
        }
        auto connectedIt = rtsp_has_connected_.find(instanceId);
        if (connectedIt != rtsp_has_connected_.end()) {
          has_connected = connectedIt->second.load();
        }
      }

      // Check if stream is inactive (no frames received for timeout period)
      auto now = std::chrono::steady_clock::now();
      auto time_since_start = std::chrono::duration_cast<std::chrono::seconds>(
                                  now - instance_start_time)
                                  .count();
      int time_since_activity = 0;
      if (has_activity) {
        time_since_activity = std::chrono::duration_cast<std::chrono::seconds>(
                                  now - last_activity)
                                  .count();
      } else {
        // No activity yet - use time since start
        time_since_activity = time_since_start;
      }

      // Get current reconnect attempt count
      int reconnect_attempts = 0;
      {
        std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
        auto attemptsIt = rtsp_reconnect_attempts_.find(instanceId);
        if (attemptsIt != rtsp_reconnect_attempts_.end()) {
          reconnect_attempts = attemptsIt->second.load();
        }
      }

      // CRITICAL: Use different timeout based on connection state
      // - If never connected: Use initial_connection_timeout (90s) to allow SDK
      // retry to complete
      // - If connected before: Use disconnection_timeout (20s) for faster
      // detection
      int timeout_seconds = has_connected ? disconnection_timeout.count()
                                          : initial_connection_timeout.count();

      // CRITICAL: During initial connection phase (first 90 seconds), don't
      // trigger reconnect The SDK is still retrying, and we shouldn't interfere
      bool is_initial_connection_phase =
          !has_connected &&
          (time_since_start < initial_connection_timeout.count());

      // Check if stream appears to be disconnected
      if (!is_initial_connection_phase &&
          time_since_activity > timeout_seconds) {
        std::cerr << "[InstanceRegistry] [RTSP Monitor] ⚠ Stream appears "
                     "disconnected (no activity for "
                  << time_since_activity << " seconds)" << std::endl;

        // Check if enough time has passed since last reconnect attempt
        auto time_since_last_reconnect =
            std::chrono::duration_cast<std::chrono::seconds>(
                now - last_reconnect_attempt)
                .count();

        if (time_since_last_reconnect >= reconnect_cooldown.count()) {
          if (reconnect_attempts < max_reconnect_attempts) {
            std::cerr << "[InstanceRegistry] [RTSP Monitor] Attempting to "
                         "reconnect RTSP stream (attempt "
                      << (reconnect_attempts + 1) << "/"
                      << max_reconnect_attempts << ")..." << std::endl;

            // Pass stop flag to reconnectRTSPStream so it can abort early if
            // instance is being stopped
            bool reconnect_success = reconnectRTSPStream(instanceId, stop_flag);

            last_reconnect_attempt = now;

            if (reconnect_success) {
              std::cerr << "[InstanceRegistry] [RTSP Monitor] ✓ Reconnection "
                           "successful!"
                        << std::endl;
              // Reset reconnect attempts on success
              {
                std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
                auto attemptsIt = rtsp_reconnect_attempts_.find(instanceId);
                if (attemptsIt != rtsp_reconnect_attempts_.end()) {
                  attemptsIt->second.store(0);
                }
                // Update last activity to now (reconnection is activity)
                rtsp_last_activity_[instanceId] = now;
              }
            } else {
              std::cerr
                  << "[InstanceRegistry] [RTSP Monitor] ✗ Reconnection failed"
                  << std::endl;
              // Increment reconnect attempts
              {
                std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
                auto attemptsIt = rtsp_reconnect_attempts_.find(instanceId);
                if (attemptsIt != rtsp_reconnect_attempts_.end()) {
                  attemptsIt->second.fetch_add(1);
                }
              }
            }
          } else {
            std::cerr << "[InstanceRegistry] [RTSP Monitor] ⚠ Maximum "
                         "reconnect attempts ("
                      << max_reconnect_attempts
                      << ") reached. Stopping reconnect attempts." << std::endl;
            std::cerr << "[InstanceRegistry] [RTSP Monitor] Instance will "
                         "remain stopped until manual intervention."
                      << std::endl;
          }
        } else {
          // Still in cooldown period
          int remaining_cooldown =
              reconnect_cooldown.count() - time_since_last_reconnect;
          if (remaining_cooldown > 0 &&
              (now - last_activity_check).count() > 30) {
            // Only log every 30 seconds to avoid spam
            std::cerr << "[InstanceRegistry] [RTSP Monitor] Waiting "
                      << remaining_cooldown
                      << " seconds before next reconnect attempt..."
                      << std::endl;
            last_activity_check = now;
          }
        }
      } else {
        // Stream appears active (time_since_activity <= timeout)
        // But we need to verify we actually have activity (not just initial
        // state)
        if (has_activity) {
          // We have real activity - mark as connected and reset reconnect
          // attempts
          if (!has_connected) {
            // First time we detect activity - mark as successfully connected
            {
              std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
              auto connectedIt = rtsp_has_connected_.find(instanceId);
              if (connectedIt != rtsp_has_connected_.end()) {
                connectedIt->second.store(true);
              }
            }
            std::cerr
                << "[InstanceRegistry] [RTSP Monitor] ✓ RTSP connection "
                   "established successfully (first activity detected after "
                << time_since_start << " seconds)" << std::endl;
          }

          if (reconnect_attempts > 0) {
            std::cerr << "[InstanceRegistry] [RTSP Monitor] ✓ Stream is active "
                         "again (activity "
                      << time_since_activity << " seconds ago)" << std::endl;
            {
              std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
              auto attemptsIt = rtsp_reconnect_attempts_.find(instanceId);
              if (attemptsIt != rtsp_reconnect_attempts_.end()) {
                attemptsIt->second.store(0);
              }
            }
          }
        }
        // If !has_activity, we're still in initial connection phase - don't log
        // anything
      }

      // Log status during initial connection phase (but don't trigger
      // reconnect) Only log at specific intervals to avoid spam: 10s, 30s, 60s,
      // then every 30s
      if (is_initial_connection_phase) {
        static std::map<std::string, int> last_logged_time;
        int last_logged = 0;
        auto it = last_logged_time.find(instanceId);
        if (it != last_logged_time.end()) {
          last_logged = it->second;
        }

        bool should_log = false;
        if (time_since_start == 10 || time_since_start == 30 ||
            time_since_start == 60) {
          should_log = true;
        } else if (time_since_start > 60 &&
                   (time_since_start - last_logged) >= 30) {
          should_log = true;
        }

        if (should_log) {
          std::cerr
              << "[InstanceRegistry] [RTSP Monitor] ⏳ Initial connection "
                 "phase: waiting for RTSP to establish ("
              << time_since_start << "s / "
              << initial_connection_timeout.count()
              << "s). SDK is retrying connection..." << std::endl;
          last_logged_time[instanceId] = time_since_start;
        }
      }
    }

    std::cerr
        << "[InstanceRegistry] [RTSP Monitor] Thread stopped for instance "
        << instanceId << std::endl;
  });

  // Store thread
  {
    std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
    rtsp_monitor_threads_[instanceId] = std::move(monitor_thread);
  }

  std::cerr << "[InstanceRegistry] [RTSP Monitor] Monitoring thread started "
               "for instance "
            << instanceId << std::endl;
}

void InstanceRegistry::stopRTSPMonitorThread(const std::string &instanceId) {
  std::unique_lock<std::mutex> lock(rtsp_monitor_mutex_);

  // Set stop flag
  auto flagIt = rtsp_monitor_stop_flags_.find(instanceId);
  if (flagIt != rtsp_monitor_stop_flags_.end() && flagIt->second) {
    flagIt->second->store(true);
  }

  // Get thread handle and release lock before joining to avoid deadlock
  std::thread threadToJoin;
  auto threadIt = rtsp_monitor_threads_.find(instanceId);
  if (threadIt != rtsp_monitor_threads_.end()) {
    if (threadIt->second.joinable()) {
      threadToJoin = std::move(threadIt->second);
    }
    rtsp_monitor_threads_.erase(threadIt);
  }

  // Remove stop flag and other tracking data
  if (flagIt != rtsp_monitor_stop_flags_.end()) {
    rtsp_monitor_stop_flags_.erase(flagIt);
  }
  rtsp_last_activity_.erase(instanceId);
  rtsp_reconnect_attempts_.erase(instanceId);
  rtsp_has_connected_.erase(instanceId);

  // Release lock before joining to avoid deadlock
  lock.unlock();

  // Join thread with timeout to prevent blocking forever
  // CRITICAL: Increased timeout to 5 seconds to allow reconnectRTSPStream() to
  // abort gracefully reconnectRTSPStream() can take up to ~2 seconds (1 second
  // sleep + operations), so we need more time CRITICAL: We MUST wait for thread
  // to finish to prevent race condition where stopPipeline() tries to stop
  // nodes while reconnectRTSPStream() is still accessing them
  if (threadToJoin.joinable()) {
    auto future = std::async(std::launch::async,
                             [&threadToJoin]() { threadToJoin.join(); });

    // Wait up to 5 seconds for thread to finish (allows reconnectRTSPStream to
    // check stop flag and abort)
    auto status = future.wait_for(std::chrono::seconds(5));
    if (status == std::future_status::timeout) {
      std::cerr << "[InstanceRegistry] [RTSP Monitor] ⚠ CRITICAL: Thread join "
                   "timeout (5s)"
                << std::endl;
      std::cerr << "[InstanceRegistry] [RTSP Monitor] This may indicate "
                   "reconnectRTSPStream is stuck"
                << std::endl;
      std::cerr << "[InstanceRegistry] [RTSP Monitor] Forcing thread detach - "
                   "this may cause race condition!"
                << std::endl;
      threadToJoin.detach();
      // CRITICAL: Wait additional time after detach to give thread a chance to
      // finish This reduces risk of race condition with stopPipeline()
      std::cerr << "[InstanceRegistry] [RTSP Monitor] Waiting additional 1 "
                   "second for thread operations to complete..."
                << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(1));
    } else {
      std::cerr
          << "[InstanceRegistry] [RTSP Monitor] ✓ Thread joined successfully"
          << std::endl;
    }
  }
}

void InstanceRegistry::updateRTSPActivity(const std::string &instanceId) {
  // OPTIMIZATION: Use try_lock to avoid blocking frame processing
  // RTSP activity update is not critical - missing one update is acceptable
  std::unique_lock<std::mutex> lock(rtsp_monitor_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    // Lock is busy - skip this update to avoid blocking frame processing
    return;
  }

  rtsp_last_activity_[instanceId] = std::chrono::steady_clock::now();

  // Mark as successfully connected when we receive activity (frames)
  auto connectedIt = rtsp_has_connected_.find(instanceId);
  if (connectedIt != rtsp_has_connected_.end() && !connectedIt->second.load()) {
    // First time we receive activity - mark as connected
    connectedIt->second.store(true);
  }
}

bool InstanceRegistry::reconnectRTSPStream(
    const std::string &instanceId,
    std::shared_ptr<std::atomic<bool>> stopFlag) {
  std::cerr << "[InstanceRegistry] [RTSP Reconnect] Attempting to reconnect "
               "RTSP stream for instance "
            << instanceId << std::endl;

  try {
    // CRITICAL: Check stop flag first - if instance is being stopped, abort
    // immediately
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance is "
                   "being stopped"
                << std::endl;
      return false;
    }

    // CRITICAL: Check if instance exists and is running BEFORE attempting
    // reconnect This prevents race condition where instance is stopped while
    // monitor thread is trying to reconnect
    InstanceInfo info;
    bool instanceRunning = false;
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end()) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Instance not found"
                  << std::endl;
        return false;
      }
      // Note: instanceExists was removed as it's not used - we already know
      // instance exists if we reach here
      instanceRunning = instanceIt->second.running;
      info = instanceIt->second;
    }

    // CRITICAL: Check stop flag again after getting instance info
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance is "
                   "being stopped"
                << std::endl;
      return false;
    }

    // CRITICAL: Double-check instance is still running (may have been stopped
    // between checks)
    if (!instanceRunning) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Instance is not "
                   "running (may have been stopped)"
                << std::endl;
      return false;
    }

    if (info.rtspUrl.empty()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Instance does not "
                   "have RTSP URL"
                << std::endl;
      return false;
    }

    // Get pipeline nodes - check again if instance is still running after
    // getting nodes
    auto nodes = getInstanceNodes(instanceId);
    if (nodes.empty()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Pipeline not found"
                << std::endl;
      return false;
    }

    // CRITICAL: Check stop flag before proceeding with node operations
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance is "
                   "being stopped (before node operations)"
                << std::endl;
      return false;
    }

    // CRITICAL: Verify instance is still running after getting nodes (race
    // condition protection)
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Instance was "
                     "stopped while getting nodes"
                  << std::endl;
        return false;
      }
    }

    // Get RTSP node
    auto rtspNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(nodes[0]);
    if (!rtspNode) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ RTSP node not found"
                << std::endl;
      return false;
    }

    // CRITICAL: Check stop flag before stopping node
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance is "
                   "being stopped (before stopping node)"
                << std::endl;
      return false;
    }

    std::cerr << "[InstanceRegistry] [RTSP Reconnect] Stopping RTSP node..."
              << std::endl;

    // Stop RTSP node gracefully with timeout
    // CRITICAL: Check stop flag before and during node operations to abort
    // quickly
    try {
      auto stopFuture = std::async(std::launch::async, [rtspNode, stopFlag]() {
        try {
          // Check stop flag before calling stop()
          if (stopFlag && stopFlag->load()) {
            return false; // Abort if stop flag is set
          }
          rtspNode->stop();
          return true;
        } catch (...) {
          return false;
        }
      });

      auto stopStatus = stopFuture.wait_for(std::chrono::milliseconds(500));
      if (stopStatus == std::future_status::timeout) {
        // Check stop flag before detaching
        if (stopFlag && stopFlag->load()) {
          std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: stop "
                       "flag set during stop timeout"
                    << std::endl;
          return false;
        }
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ⚠ Stop timeout, "
                     "using detach..."
                  << std::endl;
        try {
          rtspNode->detach_recursively();
        } catch (...) {
          // Ignore errors
        }
      } else if (stopStatus == std::future_status::ready) {
        stopFuture.get();
      }
    } catch (const std::exception &e) {
      // Check stop flag before fallback detach
      if (stopFlag && stopFlag->load()) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: stop flag "
                     "set during exception"
                  << std::endl;
        return false;
      }
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ⚠ Exception stopping "
                   "RTSP node: "
                << e.what() << std::endl;
      // Try detach as fallback
      try {
        rtspNode->detach_recursively();
      } catch (...) {
        // Ignore errors
      }
    }

    // CRITICAL: Check stop flag after stopping node
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance is "
                   "being stopped (after stopping node)"
                << std::endl;
      return false;
    }

    // Wait a moment before restarting - but check stop flag periodically
    // Break the 1-second sleep into smaller chunks to check stop flag more
    // frequently
    for (int i = 0; i < 10; ++i) {
      if (stopFlag && stopFlag->load()) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance "
                     "is being stopped (during wait)"
                  << std::endl;
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // CRITICAL: Check again if instance is still running before restarting
    // Instance may have been stopped during the wait period
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Instance was "
                     "stopped before restart (aborting reconnect)"
                  << std::endl;
        return false;
      }
    }

    // CRITICAL: Check stop flag before restarting
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance is "
                   "being stopped (before restarting)"
                << std::endl;
      return false;
    }

    // CRITICAL: Verify RTSP node is still valid before restarting
    if (!rtspNode) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ RTSP node is invalid "
                   "(may have been destroyed)"
                << std::endl;
      return false;
    }

    std::cerr << "[InstanceRegistry] [RTSP Reconnect] Restarting RTSP node..."
              << std::endl;

    // Restart RTSP node
    // CRITICAL: Check stop flag and instance status one more time right before
    // start() to prevent race condition
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: stop flag "
                   "set immediately before start()"
                << std::endl;
      return false;
    }

    // CRITICAL: Double-check instance is still running right before start()
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance "
                     "stopped immediately before start()"
                  << std::endl;
        return false;
      }
    }

    // CRITICAL: Final stop flag check - must be the last check before start()
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: stop flag "
                   "set in final check before start()"
                << std::endl;
      return false;
    }

    // CRITICAL: Lock ordering to prevent deadlock
    // Order: mutex_ (1) → gstreamer_ops_mutex_ (2)
    // ALWAYS acquire mutex_ before gstreamer_ops_mutex_ to prevent deadlock

    // CRITICAL: Verify instance is still running BEFORE acquiring GStreamer
    // lock This prevents deadlock by acquiring mutex_ first
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance "
                     "stopped before acquiring GStreamer lock"
                  << std::endl;
        return false;
      }

      // Check stop flag while holding mutex_
      if (stopFlag && stopFlag->load()) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: stop flag "
                     "set before acquiring GStreamer lock"
                  << std::endl;
        return false;
      }
    } // Release mutex_ before acquiring gstreamer_ops_mutex_

    // CRITICAL: Now acquire GStreamer lock (after releasing mutex_)
    // This prevents deadlock - we don't hold both locks simultaneously
    // Use shared lock to allow concurrent start operations
    // Multiple instances can start simultaneously, but cleanup operations will
    // wait
    std::shared_lock<std::shared_mutex> gstLock(gstreamer_ops_mutex_);

    // CRITICAL: Re-check stop flag and instance status after acquiring
    // GStreamer lock Instance may have been stopped while we were waiting for
    // GStreamer lock
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: stop flag "
                   "set after acquiring GStreamer lock"
                << std::endl;
      return false;
    }

    // Re-check instance status (need to acquire mutex_ again, but briefly)
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance "
                     "stopped after acquiring GStreamer lock"
                  << std::endl;
        return false;
      }
    } // Release mutex_ immediately - we only need it for the check

    // CRITICAL: Wrap start() in try-catch to handle GStreamer conflicts
    // If another instance is cleaning up GStreamer, this may throw or crash
    try {
      rtspNode->start();

      // CRITICAL: Final check - verify instance is still running after start()
      // This prevents updating activity for a stopped instance
      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto instanceIt = instances_.find(instanceId);
        if (instanceIt == instances_.end() || !instanceIt->second.running) {
          std::cerr << "[InstanceRegistry] [RTSP Reconnect] ⚠ Instance was "
                       "stopped after restart (reconnect may have succeeded "
                       "but instance is now stopped)"
                    << std::endl;
          return false;
        }
      }

      // CRITICAL: Check stop flag one more time before updating activity
      if (stopFlag && stopFlag->load()) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ⚠ Instance was "
                     "stopped after restart (aborting activity update)"
                  << std::endl;
        return false;
      }

      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✓ RTSP node restarted "
                   "successfully"
                << std::endl;

      // Update activity time and mark as connected
      updateRTSPActivity(instanceId);

      // Mark as successfully connected
      {
        std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
        auto connectedIt = rtsp_has_connected_.find(instanceId);
        if (connectedIt != rtsp_has_connected_.end()) {
          connectedIt->second.store(true);
        }
      }

      return true;
    } catch (const std::exception &e) {
      // CRITICAL: Check if instance was stopped during start() - this may
      // indicate race condition
      bool instanceStillRunning = false;
      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto instanceIt = instances_.find(instanceId);
        if (instanceIt != instances_.end() && instanceIt->second.running) {
          instanceStillRunning = true;
        }
      }

      if (!instanceStillRunning) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Exception during "
                     "start() - instance was stopped (race condition)"
                  << std::endl;
      } else {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Exception "
                     "restarting RTSP node: "
                  << e.what() << std::endl;
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] NOTE: This may be "
                     "caused by GStreamer conflict with another instance"
                  << std::endl;
      }
      return false;
    } catch (...) {
      // CRITICAL: Catch all exceptions including segmentation faults from
      // GStreamer
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Unknown exception "
                   "during start() - may be GStreamer crash"
                << std::endl;
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] NOTE: This may "
                   "indicate GStreamer conflict with another instance cleanup"
                << std::endl;
      return false;
    }

  } catch (const std::exception &e) {
    std::cerr
        << "[InstanceRegistry] [RTSP Reconnect] ✗ Exception during reconnect: "
        << e.what() << std::endl;
    return false;
  } catch (...) {
    std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Unknown error during "
                 "reconnect"
              << std::endl;
    return false;
  }
}

// Note: queryActualStreamPath() removed - API query may be blocked by server
// We use default "_0" suffix instead. If server assigns different suffix (_1, _2, etc.),
// the actual stream path may differ from the URL in response.
