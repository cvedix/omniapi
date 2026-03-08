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
static std::string base64_encode(const unsigned char *data, size_t length) {
  static const char base64_chars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string encoded;
  encoded.reserve(((length + 2) / 3) * 4);

  size_t i = 0;
  while (i < length) {
    unsigned char byte1 = data[i++];
    unsigned char byte2 = (i < length) ? data[i++] : 0;
    unsigned char byte3 = (i < length) ? data[i++] : 0;

    unsigned int combined = (byte1 << 16) | (byte2 << 8) | byte3;

    encoded += base64_chars[(combined >> 18) & 0x3F];
    encoded += base64_chars[(combined >> 12) & 0x3F];
    encoded += (i - 2 < length) ? base64_chars[(combined >> 6) & 0x3F] : '=';
    encoded += (i - 1 < length) ? base64_chars[combined & 0x3F] : '=';
  }

  return encoded;
}

std::optional<InstanceStatistics>
InstanceRegistry::getInstanceStatistics(const std::string &instanceId) {
  // CRITICAL: Minimize lock scope to prevent blocking when other instances are
  // starting Strategy: Copy necessary data quickly, release lock, then compute
  // statistics lock-free

  // Step 1: Get tracker pointer and basic info (with timeout)
  std::shared_ptr<InstanceStatistics> cached_stats_copy;
  InstanceStatsTracker *trackerPtr = nullptr;
  bool instanceRunning = false;
  double defaultFps = 0.0;
  std::chrono::steady_clock::time_point start_time_copy;
  std::chrono::system_clock::time_point start_time_system_copy;
  double source_fps_cached = 0.0;
  int source_width_cached = 0;
  int source_height_cached = 0;
  std::string resolution_cached;
  std::string source_resolution_cached;
  std::string format_cached;
  size_t current_queue_size_cached = 0;
  size_t max_queue_size_seen_cached = 0;
  bool has_rtmp_des = false;
  bool has_rtmp_src = false;

  {
    // CRITICAL: Use timeout to prevent blocking if mutex is locked by another
    // operation
    std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);

    // Try to acquire lock with timeout (configurable via
    // REGISTRY_MUTEX_TIMEOUT_MS)
    if (!lock.try_lock_for(TimeoutConstants::getRegistryMutexTimeout())) {
      std::cerr
          << "[InstanceRegistry] WARNING: getInstanceStatistics() timeout - "
             "mutex is locked, returning nullopt"
          << std::endl;
      if (isInstanceLoggingEnabled()) {
        PLOG_WARNING
            << "[InstanceRegistry] getInstanceStatistics() timeout after "
               "2000ms - mutex may be locked by another operation";
      }
      return std::nullopt; // Return nullopt to prevent blocking
    }

    // Check if instance exists and is running
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt == instances_.end()) {
      std::cout << "[InstanceRegistry] getInstanceStatistics: Instance not "
                   "found in instances_ map"
                << std::endl;
      std::cout.flush();
      return std::nullopt;
    }

    const InstanceInfo &info = instanceIt->second;
    instanceRunning = info.running;
    defaultFps = info.fps;

    std::cout
        << "[InstanceRegistry] getInstanceStatistics: Instance found, running="
        << instanceRunning << ", fps=" << defaultFps << std::endl;
    std::cout.flush();

    if (!instanceRunning) {
      std::cout << "[InstanceRegistry] getInstanceStatistics: Instance not "
                   "running, returning nullopt"
                << std::endl;
      std::cout.flush();
      return std::nullopt;
    }

    // Get tracker pointer
    auto trackerIt = statistics_trackers_.find(instanceId);
    if (trackerIt == statistics_trackers_.end()) {
      // Tracker not initialized yet, return default statistics
      std::cout << "[InstanceRegistry] getInstanceStatistics: Tracker not "
                   "found, returning default stats"
                << std::endl;
      std::cout.flush();
      InstanceStatistics stats;
      stats.current_framerate = std::round(defaultFps);
      return stats;
    }

    std::cout << "[InstanceRegistry] getInstanceStatistics: Tracker found, "
                 "copying data..."
              << std::endl;
    std::cout.flush();

    InstanceStatsTracker &tracker = trackerIt->second;
    trackerPtr = &tracker;

    // OPTIMIZATION: Copy all needed data while holding lock (minimal time)
    // Try cached statistics first (lock-free read of shared_ptr)
    cached_stats_copy = tracker.cached_stats_;

    // Copy basic tracker data (needed for computation if cache is stale)
    // Copy all needed data while holding lock (minimal time)
    start_time_copy = tracker.start_time;
    start_time_system_copy = tracker.start_time_system;
    source_fps_cached = tracker.source_fps;
    source_width_cached = tracker.source_width;
    source_height_cached = tracker.source_height;
    resolution_cached = tracker.resolution;
    source_resolution_cached = tracker.source_resolution;
    format_cached = tracker.format;
    current_queue_size_cached = tracker.current_queue_size;
    max_queue_size_seen_cached = tracker.max_queue_size_seen;
    auto it = info.additionalParams.find("RTMP_DES_URL");
    has_rtmp_des = (it != info.additionalParams.end() && !it->second.empty());
    it = info.additionalParams.find("RTMP_SRC_URL");
    has_rtmp_src = (it != info.additionalParams.end() && !it->second.empty());
  } // LOCK RELEASED HERE - all subsequent operations are lock-free!

  // Step 2: Check cache (lock-free)
  // NOTE: Skip cache for now to ensure we always get fresh data with
  // frames_incoming Cache can be stale and miss frames_incoming updates
  bool use_cache = false; // Temporarily disable cache to ensure fresh data

  if (use_cache && cached_stats_copy && trackerPtr) {
    // Check if cache is recent (updated within last 2 seconds worth of frames)
    uint64_t current_frame_count =
        trackerPtr->frames_processed.load(std::memory_order_relaxed);
    uint64_t cache_frame_count =
        trackerPtr->cache_update_frame_count_.load(std::memory_order_relaxed);
    uint64_t frames_since_cache =
        (current_frame_count > cache_frame_count)
            ? (current_frame_count - cache_frame_count)
            : 0;

    // Use cache if it's recent (less than 60 frames old = ~2 seconds at 30 FPS)
    if (frames_since_cache < 60) {
      // Return copy of cached stats (lock-free, doesn't block startInstance)
      // But first, update frames_incoming and dropped_frames from current
      // tracker state This ensures we always have latest data even when using
      // cache
      InstanceStatistics cached_result = *cached_stats_copy;
      cached_result.frames_incoming =
          trackerPtr->frames_incoming.load(std::memory_order_relaxed);
      cached_result.dropped_frames_count =
          trackerPtr->dropped_frames.load(std::memory_order_relaxed);

      std::cout << "[InstanceRegistry] getInstanceStatistics: Using cached "
                   "stats (updated), "
                << "frames_incoming=" << cached_result.frames_incoming
                << ", dropped_frames=" << cached_result.dropped_frames_count
                << ", frames_processed=" << cached_result.frames_processed
                << std::endl;
      std::cout.flush();

      return cached_result;
    } else {
      std::cout << "[InstanceRegistry] getInstanceStatistics: Cache is stale "
                   "(frames_since_cache="
                << frames_since_cache << "), recalculating..." << std::endl;
      std::cout.flush();
    }
  } else {
    std::cout << "[InstanceRegistry] getInstanceStatistics: Calculating fresh "
                 "stats (cache disabled or not available)..."
              << std::endl;
    std::cout.flush();
  }

  // Step 3: Compute statistics lock-free (only read atomic values and cached
  // data) All tracker access is now lock-free using atomic loads and cached
  // copies
  if (!trackerPtr) {
    // Fallback if tracker pointer is null (should not happen, but safety check)
    InstanceStatistics stats;
    stats.current_framerate = std::round(defaultFps);
    return stats;
  }

  // Build statistics object
  InstanceStatistics stats;

  // Get source framerate and resolution from cached values (no blocking calls)
  double sourceFps = source_fps_cached;
  std::string sourceRes = "";

  if (source_width_cached > 0 && source_height_cached > 0) {
    sourceRes = std::to_string(source_width_cached) + "x" +
                std::to_string(source_height_cached);
  }

  // CRITICAL: All subsequent operations are LOCK-FREE
  // We only read atomic values and cached data, no direct tracker access needed

  // Calculate elapsed time using cached start_time
  auto now = std::chrono::steady_clock::now();
  auto elapsed_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(now - start_time_copy)
          .count();
  auto elapsed_seconds_double =
      std::chrono::duration<double>(now - start_time_copy).count();

  // Calculate current FPS: PREFER FPS from backpressure controller (rolling
  // window, more accurate) then fallback to average from start_time, then
  // source FPS, then defaultFps
  // PHASE 2: Use atomic load for reading counter (lock-free)
  uint64_t frames_processed_value =
      trackerPtr->frames_processed.load(std::memory_order_relaxed);
  uint64_t frames_incoming_value =
      trackerPtr->frames_incoming.load(std::memory_order_relaxed);
  uint64_t dropped_frames_value =
      trackerPtr->dropped_frames.load(std::memory_order_relaxed);

  // Refresh RTMP activity when pipeline is processing frames (reduces false
  // "no activity" reconnect when hook misses or pipeline is slow)
  if (frames_processed_value > 0) {
    if (has_rtmp_des) {
      updateRTMPDestinationActivity(instanceId);
    }
    if (has_rtmp_src) {
      updateRTMPSourceActivity(instanceId);
    }
  }

  // Debug logging for statistics - flush to ensure it appears
  std::cout << "[InstanceRegistry] getInstanceStatistics(" << instanceId
            << "): "
            << "frames_processed=" << frames_processed_value
            << ", frames_incoming=" << frames_incoming_value
            << ", dropped_frames=" << dropped_frames_value
            << ", elapsed_seconds=" << elapsed_seconds
            << ", defaultFps=" << defaultFps << ", sourceFps=" << sourceFps
            << ", queue_size=" << current_queue_size_cached
            << ", tracker_exists=" << (trackerPtr != nullptr) << std::endl;
  std::cout.flush();

  double currentFps = 0.0;

  // First priority: Use FPS from backpressure controller (real-time rolling
  // window - more accurate) - LOCK-FREE call
  using namespace BackpressureController;
  auto &backpressure =
      BackpressureController::BackpressureController::getInstance();
  double backpressureFps = backpressure.getCurrentFPS(instanceId);
  if (backpressureFps > 0.0) {
    currentFps = std::round(backpressureFps);
  } else {
    // Fallback: Calculate actual processing FPS based on frames actually
    // processed (lock-free calculation)
    double actualProcessingFps = 0.0;
    if (elapsed_seconds_double > 0.0 && frames_processed_value > 0) {
      actualProcessingFps =
          static_cast<double>(frames_processed_value) / elapsed_seconds_double;
    }

    if (actualProcessingFps > 0.0) {
      currentFps = std::round(actualProcessingFps);
    } else if (sourceFps > 0.0) {
      currentFps = std::round(sourceFps);
    } else if (defaultFps > 0.0) {
      currentFps = std::round(defaultFps);
    } else {
      // Use last FPS from tracker (need to read atomically, but it's not
      // atomic) For now, just use 0.0 if we can't determine FPS
      currentFps = 0.0;
    }
  }

  // Set frames_incoming (all frames from source, including dropped)
  // FIX: Estimate frames_incoming if it's 0 but we have frames_processed
  // This happens when frames are dropped at SDK level before reaching the hook
  if (frames_incoming_value == 0 && frames_processed_value > 0) {
    // Estimate: frames_incoming = frames_processed + dropped_frames +
    // queue_size (approximate) This gives a conservative estimate of total
    // frames from source
    uint64_t queue_estimate = static_cast<uint64_t>(current_queue_size_cached);
    uint64_t estimated_incoming =
        frames_processed_value + dropped_frames_value + queue_estimate;

    // Use estimated value
    stats.frames_incoming = estimated_incoming;

    // Log estimation for debugging (first time only)
    static thread_local std::unordered_map<std::string, bool> logged_estimation;
    if (!logged_estimation[instanceId]) {
      std::cout << "[InstanceRegistry] Estimating frames_incoming for "
                << instanceId << ": estimated=" << estimated_incoming
                << " (processed=" << frames_processed_value
                << ", dropped=" << dropped_frames_value
                << ", queue=" << queue_estimate << ")" << std::endl;
      logged_estimation[instanceId] = true;
    }
  } else {
    stats.frames_incoming = frames_incoming_value;
  }

  // Calculate frames_processed: use actual frames processed if available,
  // otherwise estimate from frames_incoming - dropped_frames
  // This ensures statistics always have data even when frames are dropped
  uint64_t actual_frames_processed = frames_processed_value;
  uint64_t calculated_frames_processed = 0;

  if (actual_frames_processed > 0) {
    // Use actual frames processed (from frame capture hook)
    stats.frames_processed = actual_frames_processed;
    calculated_frames_processed = actual_frames_processed;
  } else if (frames_incoming_value > 0) {
    // If no frames processed yet but we have incoming frames,
    // estimate: processed = incoming - dropped
    // This ensures statistics show data even when all frames are dropped
    uint64_t estimated_processed =
        (frames_incoming_value > dropped_frames_value)
            ? (frames_incoming_value - dropped_frames_value)
            : 0;
    stats.frames_processed = estimated_processed;
    calculated_frames_processed = estimated_processed;

    std::cout << "[InstanceRegistry] Using estimated frames_processed: "
              << estimated_processed << " = incoming(" << frames_incoming_value
              << ") - dropped(" << dropped_frames_value << ")" << std::endl;
  } else if (currentFps > 0.0 && elapsed_seconds > 0) {
    // Fallback: estimate from FPS and elapsed time
    calculated_frames_processed =
        static_cast<uint64_t>(currentFps * elapsed_seconds);
    stats.frames_processed = calculated_frames_processed;
  } else {
    stats.frames_processed = 0;
    calculated_frames_processed = 0;
  }

  // Set dropped frames count - LOCK-FREE (atomic read already done above)
  stats.dropped_frames_count = dropped_frames_value;

  stats.current_framerate = std::round(currentFps);

  // Use resolution from cached values (lock-free)
  if (!sourceRes.empty()) {
    stats.resolution = sourceRes;
    stats.source_resolution = sourceRes;
  } else {
    stats.resolution = resolution_cached;
    stats.source_resolution = source_resolution_cached;
  }

  stats.format = format_cached.empty() ? "BGR" : format_cached;

  // Calculate start_time (Unix timestamp) from cached value
  auto start_time_since_epoch = start_time_system_copy.time_since_epoch();
  auto start_time_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(start_time_since_epoch)
          .count();
  stats.start_time = start_time_seconds;

  // Set source framerate
  if (sourceFps > 0.0) {
    stats.source_framerate = sourceFps;
  } else {
    stats.source_framerate = stats.current_framerate;
  }

  // Calculate latency (average time per frame in milliseconds)
  if (stats.frames_processed > 0 && currentFps > 0.0) {
    stats.latency = std::round(1000.0 / currentFps);
  } else {
    stats.latency = 0.0;
  }

  // Set default format if empty (lock-free)
  if (stats.format.empty()) {
    stats.format = "BGR";
  }

  // Queue size - use cached values (lock-free, no need to lock)
  // Updated via meta_arriving_hooker callback from CVEDIX SDK nodes
  stats.input_queue_size = static_cast<int64_t>(current_queue_size_cached);
  if (stats.input_queue_size == 0 && max_queue_size_seen_cached > 0) {
    stats.input_queue_size = static_cast<int64_t>(max_queue_size_seen_cached);
  }

  // REMOVED: Adaptive queue size manager update from statistics API
  // Reason: This can block (uses locks) and is not necessary for statistics
  // retrieval Adaptive queue metrics should be updated in frame hook or
  // background thread, not in API call This prevents timeout when pipeline is
  // busy

  // OPTIMIZATION: Update cache for next time (atomic write to shared_ptr via
  // trackerPtr) This allows future API calls to read from cache without
  // expensive calculations Thread-safe: shared_ptr assignment is atomic,
  // multiple threads can read while one writes Using trackerPtr is safe because
  // it's a pointer - we're not modifying the map This doesn't block
  // startInstance operations because we're only updating tracker fields via
  // pointer, not the map itself
  auto new_cached_stats =
      std::make_shared<InstanceStatistics>(stats); // Create new copy
  trackerPtr->cached_stats_ =
      new_cached_stats; // Atomic assignment (thread-safe via pointer)
  trackerPtr->cache_update_frame_count_.store(
      trackerPtr->frames_processed.load(std::memory_order_relaxed),
      std::memory_order_relaxed);

  // Log final statistics before returning - flush to ensure it appears
  std::cout << "[InstanceRegistry] getInstanceStatistics FINAL: "
            << "frames_processed=" << stats.frames_processed
            << ", frames_incoming=" << stats.frames_incoming
            << ", dropped_frames=" << stats.dropped_frames_count
            << ", current_fps=" << stats.current_framerate
            << ", source_fps=" << stats.source_framerate
            << ", queue_size=" << stats.input_queue_size
            << ", start_time=" << stats.start_time << std::endl;
  std::cout.flush();

  return stats;
}
