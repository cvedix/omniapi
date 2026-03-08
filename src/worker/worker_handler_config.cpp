#include "worker/worker_handler.h"
#include "worker/worker_json_utils.h"
#include "core/env_config.h"
#include "core/pipeline_builder.h"
#include "core/pipeline_builder_request_utils.h"
#include "core/pipeline_snapshot.h"
#include "core/runtime_update_log.h"
#include "core/timeout_constants.h"
#include "models/create_instance_request.h"
#include "solutions/solution_registry.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <cvedix/nodes/common/cvedix_node.h>
#include <cvedix/nodes/des/cvedix_app_des_node.h>
#include <cvedix/nodes/des/cvedix_rtmp_des_node.h>
#include <cvedix/nodes/osd/cvedix_ba_line_crossline_osd_node.h>
#include <cvedix/nodes/ba/cvedix_ba_line_crossline_node.h>
#include <cvedix/objects/shapes/cvedix_line.h>
#include <cvedix/objects/shapes/cvedix_point.h>
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
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <set>
#include <getopt.h>
#include <iostream>
#include <iomanip>
#include <opencv2/imgcodecs.hpp>
#include <sstream>
#include <thread>

namespace worker {

void WorkerHandler::startConfigWatcher() {
  // Determine config file path
  // Try to get from environment or use default instances.json location
  std::string storageDir =
      EnvConfig::resolveDirectory("./instances", "instances");
  config_file_path_ = storageDir + "/instances.json";

  // Check if file exists
  if (!std::filesystem::exists(config_file_path_)) {
    std::cout << "[Worker:" << instance_id_
              << "] Config file not found: " << config_file_path_
              << ", file watching disabled" << std::endl;
    return;
  }

  // Create watcher with callback
  config_watcher_ = std::make_unique<ConfigFileWatcher>(
      config_file_path_, [this](const std::string &configPath) {
        onConfigFileChanged(configPath);
      });

  // Start watching
  config_watcher_->start();

  std::cout << "[Worker:" << instance_id_
            << "] Started watching config file: " << config_file_path_
            << std::endl;
}

void WorkerHandler::stopConfigWatcher() {
  if (config_watcher_) {
    config_watcher_->stop();
    config_watcher_.reset();
    std::cout << "[Worker:" << instance_id_ << "] Stopped config file watcher"
              << std::endl;
  }
}

void WorkerHandler::onConfigFileChanged(const std::string &configPath) {
  std::cout << "[Worker:" << instance_id_
            << "] Config file changed, reloading..." << std::endl;

  // Load new config from file
  if (!loadConfigFromFile(configPath)) {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to load config from file" << std::endl;
    return;
  }

  // Apply config changes (this will trigger rebuild if needed)
  // We create a fake UPDATE message to reuse existing update logic
  worker::IPCMessage updateMsg;
  updateMsg.type = worker::MessageType::UPDATE_INSTANCE;
  updateMsg.payload["instance_id"] = instance_id_;
  updateMsg.payload["config"] = config_;

  // Handle update (this will automatically rebuild if needed)
  handleUpdateInstance(updateMsg);

  std::cout << "[Worker:" << instance_id_
            << "] Config reloaded and applied successfully" << std::endl;
}

bool WorkerHandler::loadConfigFromFile(const std::string &configPath) {
  try {
    if (!std::filesystem::exists(configPath)) {
      std::cerr << "[Worker:" << instance_id_
                << "] Config file does not exist: " << configPath << std::endl;
      return false;
    }

    std::ifstream file(configPath);
    if (!file.is_open()) {
      std::cerr << "[Worker:" << instance_id_
                << "] Failed to open config file: " << configPath << std::endl;
      return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
      std::cerr << "[Worker:" << instance_id_
                << "] Failed to parse config file: " << errors << std::endl;
      return false;
    }

    // Extract config for this instance
    if (!root.isMember("instances") || !root["instances"].isObject()) {
      std::cerr << "[Worker:" << instance_id_
                << "] Config file does not contain instances object"
                << std::endl;
      return false;
    }

    const auto &instances = root["instances"];
    if (!instances.isMember(instance_id_)) {
      std::cerr << "[Worker:" << instance_id_
                << "] Instance not found in config file" << std::endl;
      return false;
    }

    // Load instance config
    const auto &instanceConfig = instances[instance_id_];

    // Convert to worker config format
    Json::Value newConfig;

    // Extract solution
    if (instanceConfig.isMember("SolutionId")) {
      newConfig["SolutionId"] = instanceConfig["SolutionId"];
    }

    // Extract AdditionalParams
    if (instanceConfig.isMember("AdditionalParams")) {
      newConfig["AdditionalParams"] = instanceConfig["AdditionalParams"];
    } else {
      // Build AdditionalParams from various fields
      Json::Value additionalParams;
      if (instanceConfig.isMember("RtspUrl")) {
        additionalParams["RTSP_URL"] = instanceConfig["RtspUrl"];
      }
      if (instanceConfig.isMember("RtmpUrl")) {
        additionalParams["RTMP_URL"] = instanceConfig["RtmpUrl"];
      }
      if (instanceConfig.isMember("FilePath")) {
        additionalParams["FILE_PATH"] = instanceConfig["FilePath"];
      }
      newConfig["AdditionalParams"] = additionalParams;
    }

    // Extract DisplayName
    if (instanceConfig.isMember("DisplayName")) {
      newConfig["DisplayName"] = instanceConfig["DisplayName"];
    }

    // Merge with existing config
    for (const auto &key : newConfig.getMemberNames()) {
      config_[key] = newConfig[key];
    }

    std::cout << "[Worker:" << instance_id_
              << "] Config loaded successfully from file" << std::endl;
    return true;

  } catch (const std::exception &e) {
    std::cerr << "[Worker:" << instance_id_
              << "] Exception loading config: " << e.what() << std::endl;
    return false;
  }
}

} // namespace worker
