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
  bool requiresRestart = false;

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
      requiresRestart = true;
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
      requiresRestart = true;
    }

    if (req.configuredFps != -1) {
      std::cerr << "[InstanceRegistry] Updating configuredFps: "
                << info.configuredFps << " -> " << req.configuredFps
                << std::endl;
      info.configuredFps = req.configuredFps;
      hasChanges = true;
      requiresRestart = true;
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
      requiresRestart = true;
    }

    if (req.inputPixelLimit != -1) {
      std::cerr << "[InstanceRegistry] Updating inputPixelLimit: "
                << info.inputPixelLimit << " -> " << req.inputPixelLimit
                << std::endl;
      info.inputPixelLimit = req.inputPixelLimit;
      hasChanges = true;
      requiresRestart = true;
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
        
        if (pair.first != "CrossingLines" && 
            pair.first != "CROSSLINE_START_X" && pair.first != "CROSSLINE_START_Y" &&
            pair.first != "CROSSLINE_END_X" && pair.first != "CROSSLINE_END_Y") {
          requiresRestart = true;
        }
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
    if (!requiresRestart) {
      std::cerr << "[InstanceRegistry] Instance is running, applying runtime line changes (no restart)..." << std::endl;
      bool ok = false;
      std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> pipeline_nodes;
      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto it = pipelines_.find(instanceId);
        if (it != pipelines_.end()) {
          pipeline_nodes = it->second;
        }
      }
      
      std::shared_ptr<cvedix_nodes::cvedix_ba_line_crossline_node> baCrosslineNode;
      for (const auto &node : pipeline_nodes) {
        if (!node) continue;
        baCrosslineNode = std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_line_crossline_node>(node);
        if (baCrosslineNode) break;
      }
      
      if (!baCrosslineNode) {
        std::cerr << "[InstanceRegistry] No ba_crossline_node found in pipeline" << std::endl;
        ok = true; // Nothing to apply for lines, so it's nominally OK
      } else {
        std::map<int, cvedix_objects::cvedix_line> lines;
        
        auto getParamStr = [&](const std::string& key) -> std::string {
          auto it = req.additionalParams.find(key);
          if (it != req.additionalParams.end()) return it->second;
          auto it2 = infoCopy.additionalParams.find(key);
          if (it2 != infoCopy.additionalParams.end()) return it2->second;
          return "";
        };

        std::string crossingLinesStr = getParamStr("CrossingLines");
        if (!crossingLinesStr.empty()) {
          Json::CharReaderBuilder builder;
          Json::Value linesArray;
          std::istringstream ss(crossingLinesStr);
          std::string errs;
          if (!Json::parseFromStream(builder, ss, &linesArray, &errs) || !linesArray.isArray()) {
            std::cerr << "[InstanceRegistry] Failed to parse CrossingLines JSON" << std::endl;
            ok = false;
          } else {
            for (Json::ArrayIndex i = 0; i < linesArray.size(); ++i) {
              const Json::Value &lineObj = linesArray[i];
              if (!lineObj.isMember("coordinates") || !lineObj["coordinates"].isArray() || lineObj["coordinates"].size() < 2) continue;
              const Json::Value &coords = lineObj["coordinates"];
              const Json::Value &startCoord = coords[0];
              const Json::Value &endCoord = coords[coords.size() - 1];
              if (!startCoord.isMember("x") || !startCoord.isMember("y") || !endCoord.isMember("x") || !endCoord.isMember("y")) continue;
              int start_x = startCoord["x"].asInt();
              int start_y = startCoord["y"].asInt();
              int end_x = endCoord["x"].asInt();
              int end_y = endCoord["y"].asInt();
              lines[static_cast<int>(i)] = cvedix_objects::cvedix_line(cvedix_objects::cvedix_point(start_x, start_y), cvedix_objects::cvedix_point(end_x, end_y));
            }
            ok = baCrosslineNode->set_lines(lines);
          }
        } else {
          std::string sx = getParamStr("CROSSLINE_START_X");
          std::string sy = getParamStr("CROSSLINE_START_Y");
          std::string ex = getParamStr("CROSSLINE_END_X");
          std::string ey = getParamStr("CROSSLINE_END_Y");
          if (!sx.empty() && !sy.empty() && !ex.empty() && !ey.empty()) {
            try {
              int start_x = std::stoi(sx);
              int start_y = std::stoi(sy);
              int end_x = std::stoi(ex);
              int end_y = std::stoi(ey);
              lines[0] = cvedix_objects::cvedix_line(cvedix_objects::cvedix_point(start_x, start_y), cvedix_objects::cvedix_point(end_x, end_y));
              ok = baCrosslineNode->set_lines(lines);
            } catch (...) {
              std::cerr << "[InstanceRegistry] Invalid legacy crossline coordinates" << std::endl;
              ok = false;
            }
          } else {
             ok = true; // no lines config
          }
        }
      }
      
      if (!ok) {
        std::cerr << "[InstanceRegistry] Failed to apply runtime changes, falling back to restart..." << std::endl;
        requiresRestart = true;
      }
    }
    
    if (requiresRestart) {
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
