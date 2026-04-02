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
bool InstanceRegistry::reconnectRTSPStream(
    const std::string &instanceId,
    std::shared_ptr<std::atomic<bool>> stopFlag) {
  std::cerr << "[InstanceRegistry] [RTSP Reconnect] Attempting to reconnect "
               "RTSP stream for instance "
            << instanceId << std::endl;

  try {
    // CRITICAL: Check stop flag first - if instance is being stopped, abort
    // immediately
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance is "
                   "being stopped"
                << std::endl;
      return false;
    }

    // CRITICAL: Check if instance exists and is running BEFORE attempting
    // reconnect This prevents race condition where instance is stopped while
    // monitor thread is trying to reconnect
    InstanceInfo info;
    bool instanceRunning = false;
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end()) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Instance not found"
                  << std::endl;
        return false;
      }
      // Note: instanceExists was removed as it's not used - we already know
      // instance exists if we reach here
      instanceRunning = instanceIt->second.running;
      info = instanceIt->second;
    }

    // CRITICAL: Check stop flag again after getting instance info
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance is "
                   "being stopped"
                << std::endl;
      return false;
    }

    // CRITICAL: Double-check instance is still running (may have been stopped
    // between checks)
    if (!instanceRunning) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Instance is not "
                   "running (may have been stopped)"
                << std::endl;
      return false;
    }

    if (info.rtspUrl.empty()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Instance does not "
                   "have RTSP URL"
                << std::endl;
      return false;
    }

    // Get pipeline nodes - check again if instance is still running after
    // getting nodes
    auto nodes = getInstanceNodes(instanceId);
    if (nodes.empty()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Pipeline not found"
                << std::endl;
      return false;
    }

    // CRITICAL: Check stop flag before proceeding with node operations
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance is "
                   "being stopped (before node operations)"
                << std::endl;
      return false;
    }

    // CRITICAL: Verify instance is still running after getting nodes (race
    // condition protection)
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Instance was "
                     "stopped while getting nodes"
                  << std::endl;
        return false;
      }
    }

    // Get RTSP node
    auto rtspNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(nodes[0]);
    if (!rtspNode) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ RTSP node not found"
                << std::endl;
      return false;
    }

    // CRITICAL: Check stop flag before stopping node
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance is "
                   "being stopped (before stopping node)"
                << std::endl;
      return false;
    }

    std::cerr << "[InstanceRegistry] [RTSP Reconnect] Stopping RTSP node..."
              << std::endl;

    // Stop RTSP node gracefully with timeout
    // CRITICAL: Check stop flag before and during node operations to abort
    // quickly
    try {
      auto stopFuture = std::async(std::launch::async, [rtspNode, stopFlag]() {
        try {
          // Check stop flag before calling stop()
          if (stopFlag && stopFlag->load()) {
            return false; // Abort if stop flag is set
          }
          rtspNode->stop();
          return true;
        } catch (...) {
          return false;
        }
      });

      auto stopStatus = stopFuture.wait_for(std::chrono::milliseconds(500));
      if (stopStatus == std::future_status::timeout) {
        // Check stop flag before detaching
        if (stopFlag && stopFlag->load()) {
          std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: stop "
                       "flag set during stop timeout"
                    << std::endl;
          return false;
        }
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ⚠ Stop timeout, "
                     "using detach..."
                  << std::endl;
        try {
          rtspNode->detach_recursively();
        } catch (...) {
          // Ignore errors
        }
      } else if (stopStatus == std::future_status::ready) {
        stopFuture.get();
      }
    } catch (const std::exception &e) {
      // Check stop flag before fallback detach
      if (stopFlag && stopFlag->load()) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: stop flag "
                     "set during exception"
                  << std::endl;
        return false;
      }
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ⚠ Exception stopping "
                   "RTSP node: "
                << e.what() << std::endl;
      // Try detach as fallback
      try {
        rtspNode->detach_recursively();
      } catch (...) {
        // Ignore errors
      }
    }

    // CRITICAL: Check stop flag after stopping node
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance is "
                   "being stopped (after stopping node)"
                << std::endl;
      return false;
    }

    // Wait a moment before restarting - but check stop flag periodically
    // Break the 1-second sleep into smaller chunks to check stop flag more
    // frequently
    for (int i = 0; i < 10; ++i) {
      if (stopFlag && stopFlag->load()) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance "
                     "is being stopped (during wait)"
                  << std::endl;
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // CRITICAL: Check again if instance is still running before restarting
    // Instance may have been stopped during the wait period
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Instance was "
                     "stopped before restart (aborting reconnect)"
                  << std::endl;
        return false;
      }
    }

    // CRITICAL: Check stop flag before restarting
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance is "
                   "being stopped (before restarting)"
                << std::endl;
      return false;
    }

    // CRITICAL: Verify RTSP node is still valid before restarting
    if (!rtspNode) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ RTSP node is invalid "
                   "(may have been destroyed)"
                << std::endl;
      return false;
    }

    std::cerr << "[InstanceRegistry] [RTSP Reconnect] Restarting RTSP node..."
              << std::endl;

    // Restart RTSP node
    // CRITICAL: Check stop flag and instance status one more time right before
    // start() to prevent race condition
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: stop flag "
                   "set immediately before start()"
                << std::endl;
      return false;
    }

    // CRITICAL: Double-check instance is still running right before start()
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance "
                     "stopped immediately before start()"
                  << std::endl;
        return false;
      }
    }

    // CRITICAL: Final stop flag check - must be the last check before start()
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: stop flag "
                   "set in final check before start()"
                << std::endl;
      return false;
    }

    // CRITICAL: Lock ordering to prevent deadlock
    // Order: mutex_ (1) → gstreamer_ops_mutex_ (2)
    // ALWAYS acquire mutex_ before gstreamer_ops_mutex_ to prevent deadlock

    // CRITICAL: Verify instance is still running BEFORE acquiring GStreamer
    // lock This prevents deadlock by acquiring mutex_ first
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance "
                     "stopped before acquiring GStreamer lock"
                  << std::endl;
        return false;
      }

      // Check stop flag while holding mutex_
      if (stopFlag && stopFlag->load()) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: stop flag "
                     "set before acquiring GStreamer lock"
                  << std::endl;
        return false;
      }
    } // Release mutex_ before acquiring gstreamer_ops_mutex_

    // CRITICAL: Now acquire GStreamer lock (after releasing mutex_)
    // This prevents deadlock - we don't hold both locks simultaneously
    // Use shared lock to allow concurrent start operations
    // Multiple instances can start simultaneously, but cleanup operations will
    // wait
    std::shared_lock<std::shared_mutex> gstLock(gstreamer_ops_mutex_);

    // CRITICAL: Re-check stop flag and instance status after acquiring
    // GStreamer lock Instance may have been stopped while we were waiting for
    // GStreamer lock
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: stop flag "
                   "set after acquiring GStreamer lock"
                << std::endl;
      return false;
    }

    // Re-check instance status (need to acquire mutex_ again, but briefly)
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Aborted: instance "
                     "stopped after acquiring GStreamer lock"
                  << std::endl;
        return false;
      }
    } // Release mutex_ immediately - we only need it for the check

    // CRITICAL: Wrap start() in try-catch to handle GStreamer conflicts
    // If another instance is cleaning up GStreamer, this may throw or crash
    try {
      rtspNode->start();

      // CRITICAL: Final check - verify instance is still running after start()
      // This prevents updating activity for a stopped instance
      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto instanceIt = instances_.find(instanceId);
        if (instanceIt == instances_.end() || !instanceIt->second.running) {
          std::cerr << "[InstanceRegistry] [RTSP Reconnect] ⚠ Instance was "
                       "stopped after restart (reconnect may have succeeded "
                       "but instance is now stopped)"
                    << std::endl;
          return false;
        }
      }

      // CRITICAL: Check stop flag one more time before updating activity
      if (stopFlag && stopFlag->load()) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ⚠ Instance was "
                     "stopped after restart (aborting activity update)"
                  << std::endl;
        return false;
      }

      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✓ RTSP node restarted "
                   "successfully"
                << std::endl;

      // Update activity time and mark as connected
      updateRTSPActivity(instanceId);

      // Mark as successfully connected
      {
        std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
        auto connectedIt = rtsp_has_connected_.find(instanceId);
        if (connectedIt != rtsp_has_connected_.end()) {
          connectedIt->second.store(true);
        }
      }

      return true;
    } catch (const std::exception &e) {
      // CRITICAL: Check if instance was stopped during start() - this may
      // indicate race condition
      bool instanceStillRunning = false;
      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto instanceIt = instances_.find(instanceId);
        if (instanceIt != instances_.end() && instanceIt->second.running) {
          instanceStillRunning = true;
        }
      }

      if (!instanceStillRunning) {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Exception during "
                     "start() - instance was stopped (race condition)"
                  << std::endl;
      } else {
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Exception "
                     "restarting RTSP node: "
                  << e.what() << std::endl;
        std::cerr << "[InstanceRegistry] [RTSP Reconnect] NOTE: This may be "
                     "caused by GStreamer conflict with another instance"
                  << std::endl;
      }
      return false;
    } catch (...) {
      // CRITICAL: Catch all exceptions including segmentation faults from
      // GStreamer
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Unknown exception "
                   "during start() - may be GStreamer crash"
                << std::endl;
      std::cerr << "[InstanceRegistry] [RTSP Reconnect] NOTE: This may "
                   "indicate GStreamer conflict with another instance cleanup"
                << std::endl;
      return false;
    }

  } catch (const std::exception &e) {
    std::cerr
        << "[InstanceRegistry] [RTSP Reconnect] ✗ Exception during reconnect: "
        << e.what() << std::endl;
    return false;
  } catch (...) {
    std::cerr << "[InstanceRegistry] [RTSP Reconnect] ✗ Unknown error during "
                 "reconnect"
              << std::endl;
    return false;
  }
}

// Note: queryActualStreamPath() removed - API query may be blocked by server
// We use default "_0" suffix instead. If server assigns different suffix (_1, _2, etc.),
// the actual stream path may differ from the URL in response.
