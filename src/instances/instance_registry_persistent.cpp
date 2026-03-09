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
// #include <cvedix/cvedix_version.h>  // File not available in edgeos-sdk
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
