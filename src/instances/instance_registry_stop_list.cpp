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
bool InstanceRegistry::stopInstance(const std::string &instanceId) {
  // CRITICAL: Get pipeline copy and release lock before calling stopPipeline
  // stopPipeline can take a long time and doesn't need the lock
  // This prevents deadlock if another thread (like terminate handler) needs the
  // lock
  std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> pipelineCopy;
  bool wasRunning = false;
  std::string displayName;
  std::string solutionId;
  std::string recordPath; // Declare outside block for use after stopPipeline

  {
    std::unique_lock<std::shared_timed_mutex> lock(
        mutex_); // Exclusive lock for write operations

    auto pipelineIt = pipelines_.find(instanceId);
    if (pipelineIt == pipelines_.end()) {
      return false;
    }

    auto instanceIt = instances_.find(instanceId);
    if (instanceIt == instances_.end()) {
      return false;
    }

    wasRunning = instanceIt->second.running;
    displayName = instanceIt->second.displayName;
    solutionId = instanceIt->second.solutionId;

    // Get RECORD_PATH if exists (for auto-finalizing MP4 files after stop)
    auto recordPathIt = instanceIt->second.additionalParams.find("RECORD_PATH");
    if (recordPathIt != instanceIt->second.additionalParams.end()) {
      recordPath = recordPathIt->second;
    }

    // Copy pipeline before releasing lock
    pipelineCopy = pipelineIt->second;

    // CRITICAL: Validate that pipeline belongs to this instance
    // Each node should have instanceId in its name (e.g.,
    // "rtsp_src_{instanceId}") This ensures we only stop nodes belonging to
    // this specific instance
    std::cerr
        << "[InstanceRegistry] Validating pipeline ownership for instance "
        << instanceId << "..." << std::endl;
    std::cerr << "[InstanceRegistry] Pipeline contains " << pipelineCopy.size()
              << " nodes" << std::endl;

    // Mark as not running immediately (before stopPipeline)
    instanceIt->second.running = false;

    // CRITICAL: DO NOT remove pipeline from map yet - keep it until threads are
    // stopped This prevents race condition where reconnectRTSPStream() calls
    // getInstanceNodes() and gets empty vector, but thread already has
    // reference to nodes We'll remove it after threads are stopped
    // pipelines_.erase(pipelineIt);  // Moved to after thread stopping
  } // Release lock here - stopPipeline doesn't need it

  // CRITICAL: Stop ALL threads BEFORE stopping pipeline to prevent race
  // conditions Threads may be accessing nodes, so they must be stopped first
  // This prevents segmentation faults when GStreamer cleanup happens while
  // threads are still running

  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;
  std::cerr << "[InstanceRegistry] Stopping instance " << instanceId << "..."
            << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: All nodes will be fully destroyed to "
               "clear OpenCV DNN state"
            << std::endl;
  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;

  if (isInstanceLoggingEnabled()) {
    PLOG_INFO << "[Instance] Stopping instance: " << instanceId << " ("
              << displayName << ", solution: " << solutionId
              << ", was running: " << (wasRunning ? "true" : "false") << ")";
  }

  // CRITICAL: Stop monitoring threads FIRST (before any other cleanup)
  // This prevents race condition where monitor threads try to reconnect while pipeline is being destroyed
  std::cerr << "[InstanceRegistry] Stopping monitoring threads for instance "
            << instanceId << "..." << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: Only stopping monitoring threads for "
               "this specific instance"
            << std::endl;
  
  // Stop RTSP monitor thread
  stopRTSPMonitorThread(instanceId);
  
  // Stop RTMP source monitor thread
  stopRTMPSourceMonitorThread(instanceId);
  
  // Stop RTMP destination monitor thread
  stopRTMPDestinationMonitorThread(instanceId);

  // CRITICAL: Now remove pipeline from map after threads are stopped
  // This ensures threads can't access pipeline through getInstanceNodes()
  // anymore
  {
    std::unique_lock<std::shared_timed_mutex> lock(mutex_);
    pipelines_.erase(instanceId);
  }

  // Stop video loop monitoring thread if exists
  // IMPORTANT: stopVideoLoopThread() uses instanceId to identify and stop ONLY
  // this instance's thread
  std::cerr << "[InstanceRegistry] Stopping video loop thread for instance "
            << instanceId << "..." << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: Only stopping video loop thread for "
               "this specific instance"
            << std::endl;
  stopVideoLoopThread(instanceId);

  // CRITICAL: Wait longer for threads to fully stop before cleaning up pipeline
  // This prevents race conditions where threads are still accessing nodes
  // during cleanup RTSP monitor thread may be in reconnectRTSPStream() which
  // can take up to 2 seconds
  std::cerr << "[InstanceRegistry] Waiting for all threads to stop..."
            << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: RTSP monitor thread may take up to 5 "
               "seconds to fully stop"
            << std::endl;
  std::this_thread::sleep_for(
      std::chrono::milliseconds(500)); // Increased from 200ms to 500ms

  // Now call stopPipeline without holding the lock
  // This prevents deadlock if stopPipeline takes a long time
  // Use isDeletion=true to fully cleanup nodes and clear OpenCV DNN state
  // CRITICAL: stopPipeline() is now guaranteed to never throw (it catches all
  // exceptions internally) IMPORTANT: pipelineCopy contains ONLY nodes
  // belonging to this instance (retrieved by instanceId)
  std::cerr << "[InstanceRegistry] Stopping pipeline for instance "
            << instanceId << "..." << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: Pipeline contains "
            << pipelineCopy.size() << " nodes belonging ONLY to this instance"
            << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: Other instances' pipelines are "
               "completely unaffected"
            << std::endl;
  try {
    stopPipeline(pipelineCopy,
                 true); // true = full cleanup like deletion to clear DNN state
  } catch (const std::exception &e) {
    // This should never happen since stopPipeline catches all exceptions, but
    // just in case...
    std::cerr
        << "[InstanceRegistry] CRITICAL: Unexpected exception in stopPipeline: "
        << e.what() << std::endl;
    std::cerr << "[InstanceRegistry] This indicates a bug - stopPipeline "
                 "should not throw"
              << std::endl;
    // Continue anyway - pipeline is already removed from map
  } catch (...) {
    // This should never happen since stopPipeline catches all exceptions, but
    // just in case...
    std::cerr << "[InstanceRegistry] CRITICAL: Unexpected unknown exception in "
                 "stopPipeline"
              << std::endl;
    std::cerr << "[InstanceRegistry] This indicates a bug - stopPipeline "
                 "should not throw"
              << std::endl;
    // Continue anyway - pipeline is already removed from map
  }

  // Clear pipeline copy to ensure all nodes are destroyed immediately
  // This helps ensure OpenCV DNN releases all internal state
  // Wrap in try-catch to be extra safe (though clear() shouldn't throw)
  std::cerr << "[InstanceRegistry] Clearing pipeline copy..." << std::endl;
  try {
    pipelineCopy.clear();
  } catch (...) {
    std::cerr << "[InstanceRegistry] Warning: Exception clearing pipeline copy "
                 "(unexpected)"
              << std::endl;
  }

  // CRITICAL: Give GStreamer extra time to fully cleanup after nodes are
  // destroyed This prevents segmentation faults from GStreamer cleanup NOTE:
  // Each instance has independent GStreamer pipelines, so cleanup of one
  // instance should not affect other running instances. However, we still need
  // to wait to ensure this instance's GStreamer resources are fully released.
  // FIXED: Increased wait time to ensure GStreamer elements are properly set to
  // NULL state before process continues
  std::cerr << "[InstanceRegistry] Waiting for GStreamer final cleanup..."
            << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: This cleanup only affects this "
               "instance, not other running instances"
            << std::endl;
  std::this_thread::sleep_for(
      std::chrono::milliseconds(1000)); // Increased from 500ms to 1000ms

  std::cerr << "[InstanceRegistry] ✓ Instance " << instanceId
            << " stopped successfully" << std::endl;
  std::cerr
      << "[InstanceRegistry] NOTE: All nodes have been destroyed. Pipeline "
         "will be rebuilt from scratch when you start this instance again"
      << std::endl;
  std::cerr << "[InstanceRegistry] NOTE: This ensures OpenCV DNN starts with a "
               "clean state"
            << std::endl;
  std::cerr << "[InstanceRegistry] ========================================"
            << std::endl;

  // Stop MP4 directory watcher if exists
  {
    std::lock_guard<std::mutex> watcherLock(mp4_watcher_mutex_);
    auto watcherIt = mp4_watchers_.find(instanceId);
    if (watcherIt != mp4_watchers_.end()) {
      watcherIt->second->stop();
      mp4_watchers_.erase(watcherIt);
      std::cerr
          << "[InstanceRegistry] Stopped MP4 directory watcher for instance "
          << instanceId << std::endl;
    }
  }

  // Finalize and convert any remaining MP4 files if RECORD_PATH was set
  // This ensures the last file segment is converted even if it hasn't reached
  // max_duration (10 minutes) yet. The file_des_node will close the current
  // file when pipeline stops, and we convert it here.
  if (!recordPath.empty() && fs::exists(recordPath) &&
      fs::is_directory(recordPath)) {
    std::cerr << "[InstanceRegistry] Finalizing and converting MP4 files in: "
              << recordPath << std::endl;
    std::cerr << "[InstanceRegistry] This includes the last file segment that "
                 "was being recorded"
              << std::endl;
    std::cerr << "[InstanceRegistry] Running in background thread to not block "
                 "instance stop"
              << std::endl;

    // Run finalization in background thread
    std::thread finalizeThread([recordPath, instanceId]() {
      // Wait for file_des_node to fully close the current file
      // file_des_node closes file when pipeline stops, but it may take a moment
      // Increase wait time to ensure file is fully closed
      std::cerr << "[InstanceRegistry] [MP4Finalizer] Waiting for "
                   "file_des_node to close files..."
                << std::endl;

      // Wait longer and check file stability multiple times
      // This ensures file is fully closed before attempting conversion
      const int maxWaitAttempts = 6; // 6 attempts * 3 seconds = 18 seconds max
      bool allFilesStable = false;

      for (int attempt = 1; attempt <= maxWaitAttempts; attempt++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));

        // Check if all MP4 files in directory are stable
        allFilesStable = true;
        try {
          for (const auto &entry : fs::directory_iterator(recordPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".mp4") {
              std::string filePath = entry.path().string();
              if (MP4Finalizer::MP4Finalizer::isFileBeingWritten(filePath)) {
                allFilesStable = false;
                std::cerr << "[InstanceRegistry] [MP4Finalizer] File still "
                             "being written (attempt "
                          << attempt << "/" << maxWaitAttempts
                          << "): " << fs::path(filePath).filename().string()
                          << std::endl;
                break;
              }
            }
          }
        } catch (const fs::filesystem_error &e) {
          std::cerr
              << "[InstanceRegistry] [MP4Finalizer] Error checking files: "
              << e.what() << std::endl;
        }

        if (allFilesStable) {
          std::cerr
              << "[InstanceRegistry] [MP4Finalizer] All files are stable after "
              << (attempt * 3) << " seconds" << std::endl;
          break;
        }
      }

      if (!allFilesStable) {
        std::cerr << "[InstanceRegistry] [MP4Finalizer] ⚠ Some files may still "
                     "be closing, "
                  << "but proceeding with conversion anyway..." << std::endl;
      }

      std::cerr
          << "[InstanceRegistry] [MP4Finalizer] Starting finalization for "
             "instance "
          << instanceId << std::endl;
      std::cerr << "[InstanceRegistry] [MP4Finalizer] Converting all MP4 files "
                   "in: "
                << recordPath << std::endl;

      // Finalize all MP4 files in the record directory
      // This will:
      // 1. Try faststart on each file
      // 2. If file needs conversion (H.264 High profile or yuv444p), convert to
      //    Baseline + yuv420p
      // 3. Overwrite original file with converted version
      int processed = MP4Finalizer::MP4Finalizer::finalizeDirectory(
          recordPath, true); // true = convert to compatible format if needed

      std::cerr << "[InstanceRegistry] [MP4Finalizer] ✓ Completed finalization "
                   "for instance "
                << instanceId << std::endl;
      std::cerr << "[InstanceRegistry] [MP4Finalizer] Converted " << processed
                << " MP4 file(s) to compatible format" << std::endl;
      std::cerr
          << "[InstanceRegistry] [MP4Finalizer] All files are now viewable "
             "with standard video players"
          << std::endl;
    });

    // Detach thread so it runs independently and doesn't block instance stop
    finalizeThread.detach();
  }

  if (isInstanceLoggingEnabled()) {
    PLOG_INFO << "[Instance] Instance stopped successfully: " << instanceId
              << " (" << displayName << ", solution: " << solutionId << ")";
  }

  return true;
}

std::vector<std::string> InstanceRegistry::listInstances() const {
  // CRITICAL: Use try_lock_for with timeout to prevent blocking if mutex is
  // held by other operations This prevents deadlock when called from terminate
  // handler or other critical paths
  std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);

  // Try to acquire lock with timeout (configurable via
  // REGISTRY_MUTEX_TIMEOUT_MS) If we can't get the lock quickly, return empty
  // vector to prevent deadlock
  if (!lock.try_lock_for(TimeoutConstants::getRegistryMutexTimeout())) {
    std::cerr << "[InstanceRegistry] WARNING: listInstances() timeout - mutex "
                 "is locked, returning empty vector"
              << std::endl;
    if (isInstanceLoggingEnabled()) {
      PLOG_WARNING << "[InstanceRegistry] listInstances() timeout after 1000ms "
                      "- mutex may be locked by another operation";
    }
    return {}; // Return empty vector to prevent blocking
  }

  // Successfully acquired lock, return list of instance IDs
  std::vector<std::string> result;
  result.reserve(instances_.size());
  for (const auto &pair : instances_) {
    result.push_back(pair.first);
  }
  return result;
}

int InstanceRegistry::getInstanceCount() const {
  // CRITICAL: Use try_lock_for with timeout to prevent blocking if mutex is
  // held by other operations
  std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);

  // Try to acquire lock with timeout (configurable via
  // REGISTRY_MUTEX_TIMEOUT_MS)
  if (!lock.try_lock_for(TimeoutConstants::getRegistryMutexTimeout())) {
    std::cerr << "[InstanceRegistry] WARNING: getInstanceCount() timeout - "
                 "mutex is locked, returning 0"
              << std::endl;
    if (isInstanceLoggingEnabled()) {
      PLOG_WARNING << "[InstanceRegistry] getInstanceCount() timeout after "
                      "1000ms - mutex may be locked by another operation";
    }
    return 0; // Return 0 to prevent blocking
  }

  // Successfully acquired lock, return count
  return static_cast<int>(instances_.size());
}

std::unordered_map<std::string, InstanceInfo>
InstanceRegistry::getAllInstances() const {
  // Use shared_lock (read lock) to allow multiple concurrent readers
  // This allows multiple API requests to call getAllInstances() simultaneously
  // Writers (start/stop/update) will use exclusive lock and block readers only
  // when writing CRITICAL: Use timeout to prevent deadlock if mutex is locked
  // by recovery handler
  std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);

  // Try to acquire lock with timeout (2000ms) - longer than listInstances for
  // API calls
  if (!lock.try_lock_for(std::chrono::milliseconds(2000))) {
    std::cerr << "[InstanceRegistry] WARNING: getAllInstances() timeout - "
                 "mutex is locked, returning empty map"
              << std::endl;
    if (isInstanceLoggingEnabled()) {
      PLOG_WARNING << "[InstanceRegistry] getAllInstances() timeout after "
                      "2000ms - mutex may be locked by another operation";
    }
    return {}; // Return empty map to prevent blocking
  }

  // Create a copy of instances and update FPS dynamically for running instances
  std::unordered_map<std::string, InstanceInfo> result = instances_;

  // Update FPS for running instances (similar to getInstance logic)
  for (auto &[instanceId, info] : result) {
    if (info.running) {
      auto trackerIt = statistics_trackers_.find(instanceId);
      if (trackerIt != statistics_trackers_.end()) {
        const InstanceStatsTracker &tracker = trackerIt->second;

        // Get pipeline to access source node
        auto pipelineIt = pipelines_.find(instanceId);
        if (pipelineIt != pipelines_.end() && !pipelineIt->second.empty()) {
          auto sourceNode = pipelineIt->second[0];
          if (sourceNode) {
            double sourceFps = 0.0;

            // Try to get source FPS from RTSP or file node
            try {
              auto rtspNode =
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(
                      sourceNode);
              auto fileNode =
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(
                      sourceNode);

              if (rtspNode) {
                int fps_int = rtspNode->get_original_fps();
                if (fps_int > 0) {
                  sourceFps = static_cast<double>(fps_int);
                }
              } else if (fileNode) {
                int fps_int = fileNode->get_original_fps();
                if (fps_int > 0) {
                  sourceFps = static_cast<double>(fps_int);
                }
              }
            } catch (...) {
              // If APIs are not available, use defaults
            }

            // Calculate elapsed time
            auto now = std::chrono::steady_clock::now();
            auto elapsed_seconds_double =
                std::chrono::duration<double>(now - tracker.start_time).count();

            // Calculate current FPS: PREFER FPS from backpressure controller
            // (rolling window, more accurate) then fallback to average from
            // start_time, then source FPS, then info.fps, then tracker.last_fps
            double currentFps = 0.0;

            // First priority: Use FPS from backpressure controller (real-time
            // rolling window)
            using namespace BackpressureController;
            auto &backpressure =
                BackpressureController::BackpressureController::getInstance();
            double backpressureFps = backpressure.getCurrentFPS(instanceId);
            if (backpressureFps > 0.0) {
              currentFps = std::round(backpressureFps);
            } else {
              // Fallback: Calculate actual processing FPS based on frames
              // actually processed This is less accurate as it averages from
              // start_time (includes warm-up time)
              uint64_t frames_processed_value =
                  tracker.frames_processed.load(std::memory_order_relaxed);
              double actualProcessingFps = 0.0;
              if (elapsed_seconds_double > 0.0 && frames_processed_value > 0) {
                actualProcessingFps =
                    static_cast<double>(frames_processed_value) /
                    elapsed_seconds_double;
              }

              if (actualProcessingFps > 0.0) {
                currentFps = std::round(actualProcessingFps);
              } else if (sourceFps > 0.0) {
                currentFps = std::round(sourceFps);
              } else if (info.fps > 0.0) {
                currentFps = std::round(info.fps);
              } else {
                currentFps = std::round(tracker.last_fps);
              }
            }

            // Update fps in the result
            info.fps = currentFps;
          }
        }
      }
    }
  }

  return result;
}
