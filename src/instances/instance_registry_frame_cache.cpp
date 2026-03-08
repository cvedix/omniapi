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

std::string InstanceRegistry::getLastFrame(const std::string &instanceId) const {
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
