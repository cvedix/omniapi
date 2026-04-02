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
