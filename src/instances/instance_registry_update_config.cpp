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
        mutex_, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::seconds(5))) {
      std::cerr << "[InstanceRegistry] updateInstanceFromConfig: could not acquire lock (instance may be starting); retry later"
                << std::endl;
      return false;
    }

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
        mutex_, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::seconds(5))) {
      std::cerr << "[InstanceRegistry] updateInstanceFromConfig: could not acquire write lock; retry later"
                << std::endl;
      return false;
    }
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt == instances_.end()) {
      std::cerr << "[InstanceRegistry] Instance " << instanceId
                << " not found during update" << std::endl;
      return false;
    }

    InstanceInfo &info = instanceIt->second;

    // Update all fields from merged config
    info = updatedInfo;

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

  // When update is line-only (only AdditionalParams.CrossingLines or CROSSLINE_*)
  // and instance is running, apply set_lines() at runtime to avoid restart (zero downtime).
  // Accept both PascalCase and camelCase (e.g. CrossingLines / crossingLines) so API
  // clients do not trigger full restart and stream drop.
  auto isLineParamKey = [](const std::string& k) {
    return k == "CrossingLines" || k == "crossingLines" ||
           k == "CROSSLINE_START_X" || k == "CROSSLINE_START_Y" ||
           k == "CROSSLINE_END_X" || k == "CROSSLINE_END_Y";
  };
  if (wasRunning && configJson.isMember("AdditionalParams") &&
      configJson["AdditionalParams"].isObject()) {
    const auto& ap = configJson["AdditionalParams"];
    bool onlyLineParams = true;
    for (const auto& key : ap.getMemberNames()) {
      if (!isLineParamKey(key)) {
        onlyLineParams = false;
        break;
      }
    }
    bool hasLineData = ap.isMember("CrossingLines") || ap.isMember("crossingLines") ||
                      ap.isMember("CROSSLINE_START_X");
    if (onlyLineParams && hasLineData) {
      std::cerr << "[InstanceRegistry] Line-only update: applying CrossingLines at runtime (no restart)"
                << std::endl;
      std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> pipeline_nodes;
      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);
        if (!lock.try_lock_for(std::chrono::seconds(2))) {
          std::cerr << "[InstanceRegistry] Could not lock pipelines (start may be in progress); config saved, no restart"
                    << std::endl;
          std::cerr << "[InstanceRegistry] ========================================" << std::endl;
          return true;
        }
        auto it = pipelines_.find(instanceId);
        if (it != pipelines_.end()) pipeline_nodes = it->second;
      }
      std::shared_ptr<cvedix_nodes::cvedix_ba_line_crossline_node> baCrosslineNode;
      for (const auto& node : pipeline_nodes) {
        if (!node) continue;
        baCrosslineNode =
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_line_crossline_node>(node);
        if (baCrosslineNode) break;
      }
      if (baCrosslineNode) {
        std::map<int, cvedix_objects::cvedix_line> lines;
        auto getStr = [&updatedInfo](const std::string& k) -> std::string {
          auto it = updatedInfo.additionalParams.find(k);
          return it != updatedInfo.additionalParams.end() ? it->second : "";
        };
        std::string crossingLinesStr = getStr("CrossingLines");
        if (crossingLinesStr.empty()) crossingLinesStr = getStr("crossingLines");
        if (!crossingLinesStr.empty()) {
          Json::CharReaderBuilder builder;
          Json::Value linesArray;
          std::istringstream ss(crossingLinesStr);
          std::string errs;
          if (Json::parseFromStream(builder, ss, &linesArray, &errs) && linesArray.isArray()) {
            for (Json::ArrayIndex i = 0; i < linesArray.size(); ++i) {
              const Json::Value& lineObj = linesArray[i];
              if (!lineObj.isMember("coordinates") || !lineObj["coordinates"].isArray() ||
                  lineObj["coordinates"].size() < 2) continue;
              const Json::Value& coords = lineObj["coordinates"];
              const Json::Value& startCoord = coords[0];
              const Json::Value& endCoord = coords[coords.size() - 1];
              if (!startCoord.isMember("x") || !startCoord.isMember("y") ||
                  !endCoord.isMember("x") || !endCoord.isMember("y")) continue;
              int start_x = startCoord["x"].asInt(), start_y = startCoord["y"].asInt();
              int end_x = endCoord["x"].asInt(), end_y = endCoord["y"].asInt();
              lines[static_cast<int>(i)] = cvedix_objects::cvedix_line(
                  cvedix_objects::cvedix_point(start_x, start_y),
                  cvedix_objects::cvedix_point(end_x, end_y));
            }
            if (baCrosslineNode->set_lines(lines)) {
              std::cerr << "[InstanceRegistry] ✓ CrossingLines applied at runtime (stream continuous)"
                        << std::endl;
              std::cerr << "[InstanceRegistry] ========================================" << std::endl;
              return true;
            }
          }
        } else {
          std::string sx = getStr("CROSSLINE_START_X"), sy = getStr("CROSSLINE_START_Y");
          std::string ex = getStr("CROSSLINE_END_X"), ey = getStr("CROSSLINE_END_Y");
          if (!sx.empty() && !sy.empty() && !ex.empty() && !ey.empty()) {
            try {
              int start_x = std::stoi(sx), start_y = std::stoi(sy);
              int end_x = std::stoi(ex), end_y = std::stoi(ey);
              lines[0] = cvedix_objects::cvedix_line(
                  cvedix_objects::cvedix_point(start_x, start_y),
                  cvedix_objects::cvedix_point(end_x, end_y));
              if (baCrosslineNode->set_lines(lines)) {
                std::cerr << "[InstanceRegistry] ✓ CROSSLINE_* applied at runtime (stream continuous)"
                          << std::endl;
                std::cerr << "[InstanceRegistry] ========================================" << std::endl;
                return true;
              }
            } catch (...) {}
          }
        }
      }
      std::cerr << "[InstanceRegistry] Line-only apply skipped (no ba_crossline_node or set_lines failed); config saved, no restart (stream stays up)"
                << std::endl;
      std::cerr << "[InstanceRegistry] ========================================" << std::endl;
      return true;  // Do not restart: keeps stream continuous; new lines apply on next start
    }
  }

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
