#include "instances/instance_registry.h"
#include "core/adaptive_queue_size_manager.h"
#include "core/backpressure_controller.h"
#include "core/cvedix_validator.h"
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
