#include "instances/instance_registry.h"
#include "core/adaptive_queue_size_manager.h"
#include "core/backpressure_controller.h"
#include "core/cvedix_validator.h"
#include "core/instance_file_logger.h"
#include "core/logger.h"
#include "core/instance_logging_config.h"
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
// #include <cvedix/cvedix_version.h>  // File not available in edgeos-sdk
#include <cvedix/nodes/broker/cvedix_json_console_broker_node.h>
#include <cvedix/nodes/des/cvedix_app_des_node.h>
#include <cvedix/nodes/des/cvedix_rtmp_des_node.h>
#include <cvedix/nodes/infers/cvedix_mask_rcnn_detector_node.h>
#ifdef CVEDIX_HAS_OPENPOSE
#include <cvedix/nodes/infers/cvedix_openpose_detector_node.h>
#endif
#ifdef CVEDIX_USE_SFACE_FEATURE_ENCODER
#include <cvedix/nodes/infers/cvedix_sface_feature_encoder_node.h>
#include <cvedix/nodes/infers/cvedix_yunet_face_detector_node.h>
#include <cvedix/nodes/ba/cvedix_ba_line_crossline_node.h>
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

  // Clear per-instance logging cache
  InstanceLoggingConfig::remove(instanceId);

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
