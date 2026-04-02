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
void InstanceRegistry::startRTSPMonitorThread(const std::string &instanceId) {
  // Stop existing thread if any
  stopRTSPMonitorThread(instanceId);

  // Check if instance has RTSP URL
  std::string rtspUrl;
  {
    std::shared_lock<std::shared_timed_mutex> lock(mutex_);
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt == instances_.end()) {
      return; // Instance not found
    }
    const auto &info = instanceIt->second;
    if (info.rtspUrl.empty()) {
      return; // Not an RTSP instance
    }
    rtspUrl = info.rtspUrl;
  }

  // Create stop flag
  auto stop_flag = std::make_shared<std::atomic<bool>>(false);
  {
    std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
    rtsp_monitor_stop_flags_[instanceId] = stop_flag;
    // CRITICAL: Do NOT initialize rtsp_last_activity_ with current time
    // It should only be set when we actually receive frames from RTSP
    // This prevents false positive "connected" detection
    rtsp_reconnect_attempts_[instanceId] = 0;
    rtsp_has_connected_[instanceId] = false; // Initialize as not connected yet
  }

  // Start monitoring thread
  std::thread monitor_thread([this, instanceId, rtspUrl, stop_flag]() {
    std::cerr
        << "[InstanceRegistry] [RTSP Monitor] Thread started for instance "
        << instanceId << std::endl;
    std::cerr << "[InstanceRegistry] [RTSP Monitor] Monitoring RTSP stream: "
              << rtspUrl << std::endl;

    const auto check_interval = std::chrono::seconds(
        2); // Check every 2 seconds (faster detection for unstable streams)

    // CRITICAL: Different timeouts for initial connection vs disconnection
    // - Initial connection: Allow 90 seconds for RTSP to establish (SDK retry
    // can take 10-30s, plus stabilization)
    // - After connection: Use 20 seconds for faster disconnection detection
    const auto initial_connection_timeout =
        std::chrono::seconds(90); // Allow 90 seconds for initial RTSP
                                  // connection (SDK retry + stabilization)
    const auto disconnection_timeout =
        std::chrono::seconds(20); // Consider disconnected if no activity for 20
                                  // seconds (after successful connection)

    const auto reconnect_cooldown =
        std::chrono::seconds(10); // Wait 10 seconds between reconnect attempts
    const int max_reconnect_attempts =
        10; // Maximum reconnect attempts before giving up

    auto instance_start_time =
        std::chrono::steady_clock::now(); // Track when instance started
    auto last_reconnect_attempt =
        std::chrono::steady_clock::now() -
        reconnect_cooldown; // Allow immediate first check
    auto last_activity_check = std::chrono::steady_clock::now();

    while (!stop_flag->load()) {
      // Check stop flag before blocking operations
      if (stop_flag->load()) {
        break;
      }

      // Sleep with periodic stop flag checks
      auto sleep_start = std::chrono::steady_clock::now();
      while (std::chrono::steady_clock::now() - sleep_start < check_interval) {
        if (stop_flag->load()) {
          break;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(500)); // Check every 500ms
      }

      if (stop_flag->load()) {
        break;
      }

      // Check if instance still exists and is running
      bool instanceExists = false;
      bool instanceRunning = false;
      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto instanceIt = instances_.find(instanceId);
        if (instanceIt != instances_.end()) {
          instanceExists = true;
          instanceRunning = instanceIt->second.running;
        }
      }

      if (!instanceExists || !instanceRunning) {
        std::cerr
            << "[InstanceRegistry] [RTSP Monitor] Instance " << instanceId
            << " no longer exists or is not running, stopping monitor thread"
            << std::endl;
        break;
      }

      // Get last activity time and connection status
      bool has_activity = false;
      auto last_activity = std::chrono::steady_clock::time_point();
      bool has_connected = false;
      {
        std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
        auto activityIt = rtsp_last_activity_.find(instanceId);
        if (activityIt != rtsp_last_activity_.end()) {
          has_activity = true;
          last_activity = activityIt->second;
        }
        auto connectedIt = rtsp_has_connected_.find(instanceId);
        if (connectedIt != rtsp_has_connected_.end()) {
          has_connected = connectedIt->second.load();
        }
      }

      // Check if stream is inactive (no frames received for timeout period)
      auto now = std::chrono::steady_clock::now();
      auto time_since_start = std::chrono::duration_cast<std::chrono::seconds>(
                                  now - instance_start_time)
                                  .count();
      int time_since_activity = 0;
      if (has_activity) {
        time_since_activity = std::chrono::duration_cast<std::chrono::seconds>(
                                  now - last_activity)
                                  .count();
      } else {
        // No activity yet - use time since start
        time_since_activity = time_since_start;
      }

      // Get current reconnect attempt count
      int reconnect_attempts = 0;
      {
        std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
        auto attemptsIt = rtsp_reconnect_attempts_.find(instanceId);
        if (attemptsIt != rtsp_reconnect_attempts_.end()) {
          reconnect_attempts = attemptsIt->second.load();
        }
      }

      // CRITICAL: Use different timeout based on connection state
      // - If never connected: Use initial_connection_timeout (90s) to allow SDK
      // retry to complete
      // - If connected before: Use disconnection_timeout (20s) for faster
      // detection
      int timeout_seconds = has_connected ? disconnection_timeout.count()
                                          : initial_connection_timeout.count();

      // CRITICAL: During initial connection phase (first 90 seconds), don't
      // trigger reconnect The SDK is still retrying, and we shouldn't interfere
      bool is_initial_connection_phase =
          !has_connected &&
          (time_since_start < initial_connection_timeout.count());

      // Check if stream appears to be disconnected
      if (!is_initial_connection_phase &&
          time_since_activity > timeout_seconds) {
        std::cerr << "[InstanceRegistry] [RTSP Monitor] ⚠ Stream appears "
                     "disconnected (no activity for "
                  << time_since_activity << " seconds)" << std::endl;

        // Check if enough time has passed since last reconnect attempt
        auto time_since_last_reconnect =
            std::chrono::duration_cast<std::chrono::seconds>(
                now - last_reconnect_attempt)
                .count();

        if (time_since_last_reconnect >= reconnect_cooldown.count()) {
          if (reconnect_attempts < max_reconnect_attempts) {
            std::cerr << "[InstanceRegistry] [RTSP Monitor] Attempting to "
                         "reconnect RTSP stream (attempt "
                      << (reconnect_attempts + 1) << "/"
                      << max_reconnect_attempts << ")..." << std::endl;

            // Pass stop flag to reconnectRTSPStream so it can abort early if
            // instance is being stopped
            bool reconnect_success = reconnectRTSPStream(instanceId, stop_flag);

            last_reconnect_attempt = now;

            if (reconnect_success) {
              std::cerr << "[InstanceRegistry] [RTSP Monitor] ✓ Reconnection "
                           "successful!"
                        << std::endl;
              // Reset reconnect attempts on success
              {
                std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
                auto attemptsIt = rtsp_reconnect_attempts_.find(instanceId);
                if (attemptsIt != rtsp_reconnect_attempts_.end()) {
                  attemptsIt->second.store(0);
                }
                // Update last activity to now (reconnection is activity)
                rtsp_last_activity_[instanceId] = now;
              }
            } else {
              std::cerr
                  << "[InstanceRegistry] [RTSP Monitor] ✗ Reconnection failed"
                  << std::endl;
              // Increment reconnect attempts
              {
                std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
                auto attemptsIt = rtsp_reconnect_attempts_.find(instanceId);
                if (attemptsIt != rtsp_reconnect_attempts_.end()) {
                  attemptsIt->second.fetch_add(1);
                }
              }
            }
          } else {
            std::cerr << "[InstanceRegistry] [RTSP Monitor] ⚠ Maximum "
                         "reconnect attempts ("
                      << max_reconnect_attempts
                      << ") reached. Stopping reconnect attempts." << std::endl;
            std::cerr << "[InstanceRegistry] [RTSP Monitor] Instance will "
                         "remain stopped until manual intervention."
                      << std::endl;
          }
        } else {
          // Still in cooldown period
          int remaining_cooldown =
              reconnect_cooldown.count() - time_since_last_reconnect;
          if (remaining_cooldown > 0 &&
              (now - last_activity_check).count() > 30) {
            // Only log every 30 seconds to avoid spam
            std::cerr << "[InstanceRegistry] [RTSP Monitor] Waiting "
                      << remaining_cooldown
                      << " seconds before next reconnect attempt..."
                      << std::endl;
            last_activity_check = now;
          }
        }
      } else {
        // Stream appears active (time_since_activity <= timeout)
        // But we need to verify we actually have activity (not just initial
        // state)
        if (has_activity) {
          // We have real activity - mark as connected and reset reconnect
          // attempts
          if (!has_connected) {
            // First time we detect activity - mark as successfully connected
            {
              std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
              auto connectedIt = rtsp_has_connected_.find(instanceId);
              if (connectedIt != rtsp_has_connected_.end()) {
                connectedIt->second.store(true);
              }
            }
            std::cerr
                << "[InstanceRegistry] [RTSP Monitor] ✓ RTSP connection "
                   "established successfully (first activity detected after "
                << time_since_start << " seconds)" << std::endl;
          }

          if (reconnect_attempts > 0) {
            std::cerr << "[InstanceRegistry] [RTSP Monitor] ✓ Stream is active "
                         "again (activity "
                      << time_since_activity << " seconds ago)" << std::endl;
            {
              std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
              auto attemptsIt = rtsp_reconnect_attempts_.find(instanceId);
              if (attemptsIt != rtsp_reconnect_attempts_.end()) {
                attemptsIt->second.store(0);
              }
            }
          }
        }
        // If !has_activity, we're still in initial connection phase - don't log
        // anything
      }

      // Log status during initial connection phase (but don't trigger
      // reconnect) Only log at specific intervals to avoid spam: 10s, 30s, 60s,
      // then every 30s
      if (is_initial_connection_phase) {
        static std::map<std::string, int> last_logged_time;
        int last_logged = 0;
        auto it = last_logged_time.find(instanceId);
        if (it != last_logged_time.end()) {
          last_logged = it->second;
        }

        bool should_log = false;
        if (time_since_start == 10 || time_since_start == 30 ||
            time_since_start == 60) {
          should_log = true;
        } else if (time_since_start > 60 &&
                   (time_since_start - last_logged) >= 30) {
          should_log = true;
        }

        if (should_log) {
          std::cerr
              << "[InstanceRegistry] [RTSP Monitor] ⏳ Initial connection "
                 "phase: waiting for RTSP to establish ("
              << time_since_start << "s / "
              << initial_connection_timeout.count()
              << "s). SDK is retrying connection..." << std::endl;
          last_logged_time[instanceId] = time_since_start;
        }
      }
    }

    std::cerr
        << "[InstanceRegistry] [RTSP Monitor] Thread stopped for instance "
        << instanceId << std::endl;
  });

  // Store thread
  {
    std::lock_guard<std::mutex> lock(rtsp_monitor_mutex_);
    rtsp_monitor_threads_[instanceId] = std::move(monitor_thread);
  }

  std::cerr << "[InstanceRegistry] [RTSP Monitor] Monitoring thread started "
               "for instance "
            << instanceId << std::endl;
}
