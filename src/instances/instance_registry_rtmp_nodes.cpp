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
