#include "instances/instance_registry.h"
#include "core/adaptive_queue_size_manager.h"
#include "core/backpressure_controller.h"
#include "core/cvedix_validator.h"
#include "core/logger.h"
#include "core/instance_logging_config.h"
#include "core/log_manager.h"
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
    PLOG_INFO << "[Instance] Starting instance: " << instanceId << " ("
              << existingInfo.displayName
              << ", solution: " << existingInfo.solutionId << ")";
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


  for (const auto &node : pipelineCopy) {
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

    // Removed Check for SFace feature encoder node along with models as they are no longer used
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
          PLOG_INFO << "[Instance] Instance started successfully: "
                    << instanceId << " (" << info.displayName
                    << ", solution: " << info.solutionId << ", running: true)";
        }
        if (InstanceLoggingConfig::isEnabled(instanceId)) {
          const auto &info = instanceIt->second;
          std::string msg = "Instance started successfully: " + instanceId + " (" +
                            info.displayName + ", solution: " + info.solutionId + ", running: true)";
          LogManager::writeInstanceLog(instanceId, "INFO", msg);
        }
      } else {
        std::cerr << "[InstanceRegistry] ✗ Failed to start instance "
                  << instanceId << std::endl;
        if (isInstanceLoggingEnabled()) {
          PLOG_ERROR << "[Instance] Failed to start instance: " << instanceId
                     << " (" << existingInfo.displayName << ")";
        }
        if (InstanceLoggingConfig::isEnabled(instanceId)) {
          LogManager::writeInstanceLog(instanceId, "ERROR",
              "Failed to start instance: " + instanceId + " (" + existingInfo.displayName + ")");
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
        PLOG_WARNING << "[Instance] Instance was deleted during start: "
                     << instanceId;
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
