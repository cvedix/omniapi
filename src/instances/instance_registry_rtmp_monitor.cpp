// RTMP Source and Destination Monitoring and Auto-Reconnect Implementation
// This file contains the implementation of RTMP monitoring threads and reconnect logic

#include "instances/instance_registry.h"
#include "core/timeout_constants.h"
#include "core/rtmp_lastframe_fallback_proxy_node.h"
#include <cvedix/nodes/src/cvedix_rtmp_src_node.h>
#include <cvedix/nodes/des/cvedix_rtmp_des_node.h>
#include <cvedix/nodes/osd/cvedix_face_osd_node_v2.h>
#include <cvedix/nodes/osd/cvedix_osd_node_v3.h>
#include <cvedix/nodes/osd/cvedix_ba_line_crossline_osd_node.h>
#include <cvedix/nodes/osd/cvedix_ba_area_jam_osd_node.h>
#include <cvedix/nodes/osd/cvedix_ba_stop_osd_node.h>
#include <cvedix/nodes/ba/cvedix_ba_area_loitering_node.h>
#include <cvedix/nodes/ba/cvedix_ba_line_crossline_node.h>
#include <chrono>
#include <thread>
#include <future>
#include <typeinfo>

// ========== RTMP Source Monitoring ==========

void InstanceRegistry::startRTMPSourceMonitorThread(const std::string &instanceId) {
  // Stop existing thread if any
  stopRTMPSourceMonitorThread(instanceId);

  // Check if instance has RTMP source URL
  std::string rtmpUrl;
  {
    std::shared_lock<std::shared_timed_mutex> lock(mutex_);
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt == instances_.end()) {
      return; // Instance not found
    }
    const auto &info = instanceIt->second;
    // Check if instance uses RTMP source (check additionalParams or rtmpUrl)
    if (info.additionalParams.find("RTMP_SRC_URL") == info.additionalParams.end() &&
        info.rtmpUrl.empty()) {
      // Check if pipeline has RTMP source node
      auto nodes = getInstanceNodes(instanceId);
      bool hasRTMPSource = false;
      for (const auto &node : nodes) {
        if (std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(node)) {
          hasRTMPSource = true;
          break;
        }
      }
      if (!hasRTMPSource) {
        return; // Not an RTMP source instance
      }
    }
    // Get RTMP URL from additionalParams or rtmpUrl
    auto rtmpSrcIt = info.additionalParams.find("RTMP_SRC_URL");
    if (rtmpSrcIt != info.additionalParams.end() && !rtmpSrcIt->second.empty()) {
      rtmpUrl = rtmpSrcIt->second;
    } else if (!info.rtmpUrl.empty()) {
      rtmpUrl = info.rtmpUrl;
    } else {
      return; // No RTMP URL found
    }
  }

  // Create stop flag
  auto stop_flag = std::make_shared<std::atomic<bool>>(false);
  {
    std::lock_guard<std::mutex> lock(rtmp_src_monitor_mutex_);
    rtmp_src_monitor_stop_flags_[instanceId] = stop_flag;
    rtmp_src_reconnect_attempts_[instanceId] = 0;
    rtmp_src_has_connected_[instanceId] = false; // Initialize as not connected yet
  }

  // Start monitoring thread
  std::thread monitor_thread([this, instanceId, rtmpUrl, stop_flag]() {
    std::cerr
        << "[InstanceRegistry] [RTMP Source Monitor] Thread started for instance "
        << instanceId << std::endl;
    std::cerr << "[InstanceRegistry] [RTMP Source Monitor] Monitoring RTMP source stream: "
              << rtmpUrl << std::endl;

    const auto check_interval = std::chrono::seconds(
        2); // Check every 2 seconds (faster detection for unstable streams)

    // CRITICAL: Different timeouts for initial connection vs disconnection
    // - Initial connection: Allow 60 seconds for RTMP to establish
    // - After connection: Use 15 seconds for faster disconnection detection
    const auto initial_connection_timeout =
        std::chrono::seconds(60); // Allow 60 seconds for initial RTMP connection
    const auto disconnection_timeout =
        std::chrono::seconds(15); // Consider disconnected if no activity for 15 seconds

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
            << "[InstanceRegistry] [RTMP Source Monitor] Instance " << instanceId
            << " no longer exists or is not running, stopping monitor thread"
            << std::endl;
        break;
      }

      // Get last activity time and connection status
      bool has_activity = false;
      auto last_activity = std::chrono::steady_clock::time_point();
      bool has_connected = false;
      {
        std::lock_guard<std::mutex> lock(rtmp_src_monitor_mutex_);
        auto activityIt = rtmp_src_last_activity_.find(instanceId);
        if (activityIt != rtmp_src_last_activity_.end()) {
          has_activity = true;
          last_activity = activityIt->second;
        }
        auto connectedIt = rtmp_src_has_connected_.find(instanceId);
        if (connectedIt != rtmp_src_has_connected_.end()) {
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
        std::lock_guard<std::mutex> lock(rtmp_src_monitor_mutex_);
        auto attemptsIt = rtmp_src_reconnect_attempts_.find(instanceId);
        if (attemptsIt != rtmp_src_reconnect_attempts_.end()) {
          reconnect_attempts = attemptsIt->second.load();
        }
      }

      // CRITICAL: Use different timeout based on connection state
      int timeout_seconds = has_connected ? disconnection_timeout.count()
                                          : initial_connection_timeout.count();

      // CRITICAL: During initial connection phase, don't trigger reconnect
      bool is_initial_connection_phase =
          !has_connected &&
          (time_since_start < initial_connection_timeout.count());

      // Check if stream appears to be disconnected
      if (!is_initial_connection_phase &&
          time_since_activity > timeout_seconds) {
        std::cerr << "[InstanceRegistry] [RTMP Source Monitor] ⚠ Stream appears "
                     "disconnected (no activity for "
                  << time_since_activity << " seconds)" << std::endl;

        // CRITICAL FIX: Check if RTMP destination is having issues before reconnecting source
        // If destination is failing, don't reconnect source (destination issue, not source issue)
        bool destination_has_issues = false;
        {
          std::lock_guard<std::mutex> des_lock(rtmp_des_monitor_mutex_);
          auto des_activityIt = rtmp_des_last_activity_.find(instanceId);
          if (des_activityIt != rtmp_des_last_activity_.end()) {
            // Destination exists - check if it's also having issues
            auto des_time_since_activity = std::chrono::duration_cast<std::chrono::seconds>(
                now - des_activityIt->second).count();
            // If destination also has no activity for > 15 seconds, likely destination issue
            if (des_time_since_activity > 15) {
              destination_has_issues = true;
              std::cerr << "[InstanceRegistry] [RTMP Source Monitor] ⚠ RTMP destination also has no activity for "
                        << des_time_since_activity << " seconds - likely destination issue, not source issue"
                        << std::endl;
              std::cerr << "[InstanceRegistry] [RTMP Source Monitor] ⚠ Skipping source reconnect - destination monitor should handle this"
                        << std::endl;
            }
          }
        }

        // Only reconnect source if destination is healthy (or doesn't exist)
        if (destination_has_issues) {
          // Destination has issues - don't reconnect source
          // Destination monitor will handle reconnecting destination
          // Just log and continue monitoring
          std::cerr << "[InstanceRegistry] [RTMP Source Monitor] ⏳ Waiting for destination to recover before considering source reconnect"
                    << std::endl;
          continue; // Skip reconnect attempt
        }

        // Check if enough time has passed since last reconnect attempt
        auto time_since_last_reconnect =
            std::chrono::duration_cast<std::chrono::seconds>(
                now - last_reconnect_attempt)
                .count();

        if (time_since_last_reconnect >= reconnect_cooldown.count()) {
          if (reconnect_attempts < max_reconnect_attempts) {
            std::cerr << "[InstanceRegistry] [RTMP Source Monitor] Attempting to "
                         "reconnect RTMP source stream (attempt "
                      << (reconnect_attempts + 1) << "/"
                      << max_reconnect_attempts << ")..." << std::endl;

            // Pass stop flag to reconnectRTMPSourceStream so it can abort early if
            // instance is being stopped
            bool reconnect_success = reconnectRTMPSourceStream(instanceId, stop_flag);

            last_reconnect_attempt = now;

            if (reconnect_success) {
              std::cerr << "[InstanceRegistry] [RTMP Source Monitor] ✓ Reconnection "
                           "successful!"
                        << std::endl;
              // Reset reconnect attempts on success
              {
                std::lock_guard<std::mutex> lock(rtmp_src_monitor_mutex_);
                auto attemptsIt = rtmp_src_reconnect_attempts_.find(instanceId);
                if (attemptsIt != rtmp_src_reconnect_attempts_.end()) {
                  attemptsIt->second.store(0);
                }
                // Update last activity to now (reconnection is activity)
                rtmp_src_last_activity_[instanceId] = now;
              }
            } else {
              std::cerr
                  << "[InstanceRegistry] [RTMP Source Monitor] ✗ Reconnection failed"
                  << std::endl;
              // Increment reconnect attempts
              {
                std::lock_guard<std::mutex> lock(rtmp_src_monitor_mutex_);
                auto attemptsIt = rtmp_src_reconnect_attempts_.find(instanceId);
                if (attemptsIt != rtmp_src_reconnect_attempts_.end()) {
                  attemptsIt->second.fetch_add(1);
                }
              }
            }
          } else {
            std::cerr << "[InstanceRegistry] [RTMP Source Monitor] ⚠ Maximum "
                         "reconnect attempts ("
                      << max_reconnect_attempts
                      << ") reached. Stopping reconnect attempts." << std::endl;
            std::cerr << "[InstanceRegistry] [RTMP Source Monitor] Instance will "
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
            std::cerr << "[InstanceRegistry] [RTMP Source Monitor] Waiting "
                      << remaining_cooldown
                      << " seconds before next reconnect attempt..."
                      << std::endl;
            last_activity_check = now;
          }
        }
      } else {
        // Stream appears active
        if (has_activity) {
          // We have real activity - mark as connected and reset reconnect attempts
          if (!has_connected) {
            // First time we detect activity - mark as successfully connected
            {
              std::lock_guard<std::mutex> lock(rtmp_src_monitor_mutex_);
              auto connectedIt = rtmp_src_has_connected_.find(instanceId);
              if (connectedIt != rtmp_src_has_connected_.end()) {
                connectedIt->second.store(true);
              }
            }
            std::cerr
                << "[InstanceRegistry] [RTMP Source Monitor] ✓ RTMP source connection "
                   "established successfully (first activity detected after "
                << time_since_start << " seconds)" << std::endl;
          }

          if (reconnect_attempts > 0) {
            std::cerr << "[InstanceRegistry] [RTMP Source Monitor] ✓ Stream is active "
                         "again (activity "
                      << time_since_activity << " seconds ago)" << std::endl;
            {
              std::lock_guard<std::mutex> lock(rtmp_src_monitor_mutex_);
              auto attemptsIt = rtmp_src_reconnect_attempts_.find(instanceId);
              if (attemptsIt != rtmp_src_reconnect_attempts_.end()) {
                attemptsIt->second.store(0);
              }
            }
          }
        }
      }

      // Log status during initial connection phase
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
              << "[InstanceRegistry] [RTMP Source Monitor] ⏳ Initial connection "
                 "phase: waiting for RTMP source to establish ("
              << time_since_start << "s / "
              << initial_connection_timeout.count()
              << "s)..." << std::endl;
          last_logged_time[instanceId] = time_since_start;
        }
      }
    }

    std::cerr
        << "[InstanceRegistry] [RTMP Source Monitor] Thread stopped for instance "
        << instanceId << std::endl;
  });

  // Store thread
  {
    std::lock_guard<std::mutex> lock(rtmp_src_monitor_mutex_);
    rtmp_src_monitor_threads_[instanceId] = std::move(monitor_thread);
  }

  std::cerr << "[InstanceRegistry] [RTMP Source Monitor] Monitoring thread started "
               "for instance "
            << instanceId << std::endl;
}

void InstanceRegistry::stopRTMPSourceMonitorThread(const std::string &instanceId) {
  std::unique_lock<std::mutex> lock(rtmp_src_monitor_mutex_);

  // Set stop flag
  auto flagIt = rtmp_src_monitor_stop_flags_.find(instanceId);
  if (flagIt != rtmp_src_monitor_stop_flags_.end() && flagIt->second) {
    flagIt->second->store(true);
  }

  // Get thread handle and release lock before joining to avoid deadlock
  std::thread threadToJoin;
  auto threadIt = rtmp_src_monitor_threads_.find(instanceId);
  if (threadIt != rtmp_src_monitor_threads_.end()) {
    if (threadIt->second.joinable()) {
      threadToJoin = std::move(threadIt->second);
    }
    rtmp_src_monitor_threads_.erase(threadIt);
  }

  // Remove stop flag and other tracking data
  if (flagIt != rtmp_src_monitor_stop_flags_.end()) {
    rtmp_src_monitor_stop_flags_.erase(flagIt);
  }
  rtmp_src_last_activity_.erase(instanceId);
  rtmp_src_reconnect_attempts_.erase(instanceId);
  rtmp_src_has_connected_.erase(instanceId);

  // Release lock before joining to avoid deadlock
  lock.unlock();

  // Join thread with timeout
  if (threadToJoin.joinable()) {
    auto future = std::async(std::launch::async,
                             [&threadToJoin]() { threadToJoin.join(); });

    auto status = future.wait_for(std::chrono::seconds(5));
    if (status == std::future_status::timeout) {
      std::cerr << "[InstanceRegistry] [RTMP Source Monitor] ⚠ CRITICAL: Thread join "
                   "timeout (5s)"
                << std::endl;
      threadToJoin.detach();
      std::this_thread::sleep_for(std::chrono::seconds(1));
    } else {
      std::cerr
          << "[InstanceRegistry] [RTMP Source Monitor] ✓ Thread joined successfully"
          << std::endl;
    }
  }
}

void InstanceRegistry::updateRTMPSourceActivity(const std::string &instanceId) {
  // OPTIMIZATION: Use try_lock to avoid blocking frame processing
  std::unique_lock<std::mutex> lock(rtmp_src_monitor_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return; // Lock is busy - skip this update
  }

  rtmp_src_last_activity_[instanceId] = std::chrono::steady_clock::now();

  // Mark as successfully connected when we receive activity (frames)
  auto connectedIt = rtmp_src_has_connected_.find(instanceId);
  if (connectedIt != rtmp_src_has_connected_.end() && !connectedIt->second.load()) {
    // First time we receive activity - mark as connected
    connectedIt->second.store(true);
  }
}

bool InstanceRegistry::reconnectRTMPSourceStream(
    const std::string &instanceId,
    std::shared_ptr<std::atomic<bool>> stopFlag) {
  try {
    std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] Starting reconnect for instance "
              << instanceId << std::endl;

    // Check if instance still exists and is running
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Instance not found or not running"
                  << std::endl;
        return false;
      }
    }

    // Get pipeline nodes
    auto nodes = getInstanceNodes(instanceId);
    if (nodes.empty()) {
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Pipeline not found"
                << std::endl;
      return false;
    }

    // CRITICAL: Check stop flag before proceeding
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Aborted: instance is being stopped"
                << std::endl;
      return false;
    }

    // Get RTMP source node
    auto rtmpNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(nodes[0]);
    if (!rtmpNode) {
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ RTMP source node not found"
                << std::endl;
      return false;
    }

    // CRITICAL: Check stop flag before stopping node
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Aborted: instance is being stopped (before stopping node)"
                << std::endl;
      return false;
    }

    std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] Stopping RTMP source node..."
              << std::endl;

    // CRITICAL FIX: Stop RTMP source node gracefully with longer timeout
    // Avoid using detach_recursively() as it can affect the entire pipeline
    // including RTMP destination nodes. Use longer timeout to allow stop() to complete.
    try {
      auto stopFuture = std::async(std::launch::async, [rtmpNode, stopFlag]() {
        try {
          if (stopFlag && stopFlag->load()) {
            return false;
          }
          rtmpNode->stop();
          return true;
        } catch (...) {
          return false;
        }
      });

      // CRITICAL: Use configurable timeout to avoid detach_recursively()
      // detach_recursively() can disconnect RTMP destination stream
      auto stopTimeout = TimeoutConstants::getRtmpSourceStopTimeout();
      auto stopStatus = stopFuture.wait_for(stopTimeout);
      if (stopStatus == std::future_status::timeout) {
        if (stopFlag && stopFlag->load()) {
          std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Aborted: stop flag set during stop timeout"
                    << std::endl;
          return false;
        }
        std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ⚠ Stop timeout after " 
                  << stopTimeout.count() << "ms"
                  << std::endl;
        std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ⚠ WARNING: Not using detach_recursively() to avoid affecting RTMP destination"
                  << std::endl;
        std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ⚠ Proceeding with restart - destination may be affected"
                  << std::endl;
        // CRITICAL: Don't use detach_recursively() as it can disconnect destination
        // Instead, proceed with restart and let GStreamer handle cleanup
      } else if (stopStatus == std::future_status::ready) {
        stopFuture.get();
        std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✓ RTMP source node stopped successfully"
                  << std::endl;
      }
    } catch (const std::exception &e) {
      if (stopFlag && stopFlag->load()) {
        std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Aborted: stop flag set during exception"
                  << std::endl;
        return false;
      }
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ⚠ Exception stopping RTMP source node: "
                << e.what() << std::endl;
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ⚠ WARNING: Not using detach_recursively() to avoid affecting RTMP destination"
                << std::endl;
      // CRITICAL: Don't use detach_recursively() as it can disconnect destination
      // Proceed with restart attempt
    }

    // CRITICAL: Check stop flag after stopping node
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Aborted: instance is being stopped (after stopping node)"
                << std::endl;
      return false;
    }

    // CRITICAL FIX: Wait before restarting to allow GStreamer pipeline to fully release resources
    // This prevents "gst_sample_get_caps: assertion 'GST_IS_SAMPLE (sample)' failed" errors
    // after reconnect by ensuring GStreamer has time to clean up old pipeline state
    // Also allows RTMP destination to stabilize if it was affected
    auto stabilizationTimeout = TimeoutConstants::getRtmpSourceReconnectStabilization();
    int stabilizationMs = stabilizationTimeout.count();
    int sleepIterations = (stabilizationMs + 99) / 100; // Round up to nearest 100ms
    std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] Waiting for GStreamer pipeline to stabilize (" 
              << (stabilizationMs / 1000.0) << " seconds)..."
              << std::endl;
    for (int i = 0; i < sleepIterations; ++i) {
      if (stopFlag && stopFlag->load()) {
        std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Aborted: instance is being stopped (during wait)"
                  << std::endl;
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // CRITICAL: Check again if instance is still running before restarting
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Instance was stopped before restart"
                  << std::endl;
        return false;
      }
    }

    // CRITICAL: Check stop flag before restarting
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Aborted: instance is being stopped (before restarting)"
                << std::endl;
      return false;
    }

    // CRITICAL: Verify RTMP source node is still valid before restarting
    if (!rtmpNode) {
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ RTMP source node is invalid"
                << std::endl;
      return false;
    }

    std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] Restarting RTMP source node..."
              << std::endl;

    // Restart RTMP source node
    // CRITICAL: Check stop flag and instance status one more time right before start()
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Aborted: stop flag set immediately before start()"
                << std::endl;
      return false;
    }

    // CRITICAL: Double-check instance is still running right before start()
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Aborted: instance stopped immediately before start()"
                  << std::endl;
        return false;
      }
    }

    // CRITICAL: Final stop flag check
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Aborted: stop flag set in final check before start()"
                << std::endl;
      return false;
    }

    // CRITICAL: Lock ordering to prevent deadlock
    // Order: mutex_ (1) → gstreamer_ops_mutex_ (2)
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Instance stopped before acquiring GStreamer lock"
                  << std::endl;
        return false;
      }
    }

    // Acquire GStreamer lock for node operations
    std::unique_lock<std::shared_mutex> gstLock(gstreamer_ops_mutex_);

    // CRITICAL: Final check before start()
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Aborted: stop flag set after acquiring GStreamer lock"
                << std::endl;
      return false;
    }

    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Instance stopped after acquiring GStreamer lock"
                  << std::endl;
        return false;
      }
    }

    // Start RTMP source node
    try {
      rtmpNode->start();
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✓ RTMP source node restarted successfully"
                << std::endl;

      // CRITICAL FIX: Wait additional time after start() to allow GStreamer to initialize
      // This prevents invalid sample errors immediately after reconnect
      auto initializationTimeout = TimeoutConstants::getRtmpSourceReconnectInitialization();
      int initializationMs = initializationTimeout.count();
      int initSleepIterations = (initializationMs + 99) / 100; // Round up to nearest 100ms
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] Waiting for GStreamer pipeline to initialize (" 
                << (initializationMs / 1000.0) << " seconds)..."
                << std::endl;
      for (int i = 0; i < initSleepIterations; ++i) {
        if (stopFlag && stopFlag->load()) {
          std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ⚠ Stop flag set during initialization wait"
                    << std::endl;
          // Continue anyway - node is started
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      // Update activity time and mark as connected
      updateRTMPSourceActivity(instanceId);

      // Mark as successfully connected
      {
        std::lock_guard<std::mutex> lock(rtmp_src_monitor_mutex_);
        auto connectedIt = rtmp_src_has_connected_.find(instanceId);
        if (connectedIt != rtmp_src_has_connected_.end()) {
          connectedIt->second.store(true);
        }
      }

      return true;
    } catch (const std::exception &e) {
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Exception restarting RTMP source node: "
                << e.what() << std::endl;
      return false;
    } catch (...) {
      std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Unknown error during reconnect"
                << std::endl;
      return false;
    }
  } catch (const std::exception &e) {
    std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Exception in reconnectRTMPSourceStream: "
              << e.what() << std::endl;
    return false;
  } catch (...) {
    std::cerr << "[InstanceRegistry] [RTMP Source Reconnect] ✗ Unknown error during reconnect"
              << std::endl;
    return false;
  }
}

// ========== RTMP Destination Monitoring ==========

void InstanceRegistry::startRTMPDestinationMonitorThread(const std::string &instanceId) {
  // Stop existing thread if any
  stopRTMPDestinationMonitorThread(instanceId);

  // Check if instance has RTMP destination URL
  std::string rtmpUrl;
  {
    std::shared_lock<std::shared_timed_mutex> lock(mutex_);
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt == instances_.end()) {
      return; // Instance not found
    }
    const auto &info = instanceIt->second;
    // Check if instance uses RTMP destination
    auto rtmpDesIt = info.additionalParams.find("RTMP_DES_URL");
    if (rtmpDesIt != info.additionalParams.end() && !rtmpDesIt->second.empty()) {
      rtmpUrl = rtmpDesIt->second;
    } else {
      // Check if pipeline has RTMP destination node
      auto nodes = getInstanceNodes(instanceId);
      bool hasRTMPDestination = false;
      for (const auto &node : nodes) {
        if (std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node)) {
          hasRTMPDestination = true;
          // Try to get URL from node or instance info
          if (!info.rtmpUrl.empty()) {
            rtmpUrl = info.rtmpUrl;
          }
          break;
        }
      }
      if (!hasRTMPDestination || rtmpUrl.empty()) {
        return; // Not an RTMP destination instance
      }
    }
  }

  // Create stop flag
  auto stop_flag = std::make_shared<std::atomic<bool>>(false);
  {
    std::lock_guard<std::mutex> lock(rtmp_des_monitor_mutex_);
    rtmp_des_monitor_stop_flags_[instanceId] = stop_flag;
    rtmp_des_reconnect_attempts_[instanceId] = 0;
    rtmp_des_has_connected_[instanceId] = false; // Initialize as not connected yet
  }

  // Start monitoring thread
  std::thread monitor_thread([this, instanceId, rtmpUrl, stop_flag]() {
    std::cerr
        << "[InstanceRegistry] [RTMP Destination Monitor] Thread started for instance "
        << instanceId << std::endl;
    std::cerr << "[InstanceRegistry] [RTMP Destination Monitor] Monitoring RTMP destination stream: "
              << rtmpUrl << std::endl;

    const auto check_interval = std::chrono::seconds(
        2); // Check every 2 seconds

    // CRITICAL: Different timeouts for initial connection vs disconnection
    // - Initial connection: Allow 30 seconds for RTMP destination to establish
    // - After connection: Use 20 seconds for disconnection detection (reduced from 30s for faster error detection)
    //   This allows reasonable time for frames to be processed through the pipeline,
    //   but detects write failures faster to prevent queue backup
    const auto initial_connection_timeout =
        std::chrono::seconds(30); // Allow 30 seconds for initial RTMP destination connection
    // Base disconnection timeout - will be reduced adaptively if errors detected
    auto disconnection_timeout =
        std::chrono::seconds(20); // Consider disconnected if no activity for 20 seconds (faster detection of write failures)

    const auto reconnect_cooldown =
        std::chrono::seconds(10); // Wait 10 seconds between reconnect attempts
    const auto reconnect_grace_period =
        std::chrono::seconds(30); // Grace period after successful reconnect to allow connection establishment
    const int max_reconnect_attempts =
        10; // Maximum reconnect attempts before giving up

    auto instance_start_time =
        std::chrono::steady_clock::now();
    auto last_reconnect_attempt =
        std::chrono::steady_clock::now() -
        reconnect_cooldown;
    auto last_successful_reconnect =
        std::chrono::steady_clock::now() -
        reconnect_grace_period; // Track last successful reconnect for grace period
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
            std::chrono::milliseconds(500));
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
            << "[InstanceRegistry] [RTMP Destination Monitor] Instance " << instanceId
            << " no longer exists or is not running, stopping monitor thread"
            << std::endl;
        break;
      }

      // Get last activity time and connection status
      bool has_activity = false;
      auto last_activity = std::chrono::steady_clock::time_point();
      bool has_connected = false;
      {
        std::lock_guard<std::mutex> lock(rtmp_des_monitor_mutex_);
        auto activityIt = rtmp_des_last_activity_.find(instanceId);
        if (activityIt != rtmp_des_last_activity_.end()) {
          has_activity = true;
          last_activity = activityIt->second;
        }
        auto connectedIt = rtmp_des_has_connected_.find(instanceId);
        if (connectedIt != rtmp_des_has_connected_.end()) {
          has_connected = connectedIt->second.load();
        }
      }

      // Check if stream is inactive (no frames sent for timeout period)
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
        time_since_activity = time_since_start;
      }

      // Get current reconnect attempt count
      int reconnect_attempts = 0;
      {
        std::lock_guard<std::mutex> lock(rtmp_des_monitor_mutex_);
        auto attemptsIt = rtmp_des_reconnect_attempts_.find(instanceId);
        if (attemptsIt != rtmp_des_reconnect_attempts_.end()) {
          reconnect_attempts = attemptsIt->second.load();
        }
      }

      // CRITICAL: Use different timeout based on connection state
      // Adaptive timeout: reduce if activity stopped after being connected (likely errors)
      int timeout_seconds = has_connected ? disconnection_timeout.count()
                                          : initial_connection_timeout.count();
      
      // CRITICAL FIX: If activity stopped after being connected, likely GStreamer errors accumulating
      // Reduce timeout to detect and recover faster from "Error pushing buffer" issues
      // This helps when errors accumulate over time (frame 7000+)
      if (has_connected && has_activity && time_since_activity > 5) {
        // Activity stopped after connection - likely errors accumulating
        // Reduce timeout to 15s for faster detection (from 20s)
        timeout_seconds = 15;
      }

      // CRITICAL: During initial connection phase, don't trigger reconnect
      bool is_initial_connection_phase =
          !has_connected &&
          (time_since_start < initial_connection_timeout.count());

      // CRITICAL FIX: Early detection and queue clearing
      // If destination has no activity for > threshold, detach destination node immediately
      // to clear queue and prevent backup. This prevents "queue full, dropping meta!" warnings
      // and allows faster recovery.
      // Use adaptive threshold: shorter for consecutive errors (when activity stops suddenly)
      // This helps detect GStreamer "Error pushing buffer" issues that accumulate over time
      int early_detection_threshold = 15; // Default: 15s
      
      // CRITICAL: If we had activity before but now stopped, likely errors accumulating
      // Use shorter threshold to detect GStreamer buffer errors faster
      if (has_connected && has_activity && time_since_activity > 5) {
        // Activity stopped after being connected - likely errors
        // Use shorter threshold (10s) to detect and recover faster
        early_detection_threshold = 10;
      }
      
      // Check if we're in grace period after successful reconnect
      auto time_since_successful_reconnect = std::chrono::duration_cast<std::chrono::seconds>(
          now - last_successful_reconnect).count();
      bool in_grace_period = time_since_successful_reconnect < reconnect_grace_period.count();
      
      bool should_clear_queue_early = false;
      if (has_activity && time_since_activity > early_detection_threshold && 
          time_since_activity < timeout_seconds && !in_grace_period) {
        // Destination is failing but hasn't reached full timeout yet
        // Detach now to clear queue, then reconnect
        // BUT: Skip if we're in grace period after successful reconnect
        should_clear_queue_early = true;
        std::cerr << "[InstanceRegistry] [RTMP Destination Monitor] ⚠ Early detection: "
                     "Destination has no activity for " << time_since_activity 
                  << " seconds (threshold: " << early_detection_threshold 
                  << "s). Detaching destination node to clear queue..." << std::endl;
      } else if (in_grace_period && time_since_activity > early_detection_threshold) {
        std::cerr << "[InstanceRegistry] [RTMP Destination Monitor] ⚠ Skipping early detection: "
                     "In grace period after successful reconnect ("
                  << time_since_successful_reconnect << "s / " << reconnect_grace_period.count() 
                  << "s). Allowing connection to establish..." << std::endl;
      }

      // Check if stream appears to be disconnected
      // For RTMP destination, we detect disconnection by checking if frames are being sent
      // If no activity for timeout period, assume connection is lost
      if (!is_initial_connection_phase &&
          (time_since_activity > timeout_seconds || should_clear_queue_early)) {
        std::cerr << "[InstanceRegistry] [RTMP Destination Monitor] ⚠ Stream appears "
                     "disconnected (no activity for "
                  << time_since_activity << " seconds)" << std::endl;

        // Check if enough time has passed since last reconnect attempt
        auto time_since_last_reconnect =
            std::chrono::duration_cast<std::chrono::seconds>(
                now - last_reconnect_attempt)
                .count();

        if (time_since_last_reconnect >= reconnect_cooldown.count() || should_clear_queue_early) {
          if (reconnect_attempts < max_reconnect_attempts) {
            std::cerr << "[InstanceRegistry] [RTMP Destination Monitor] Attempting to "
                         "reconnect RTMP destination stream (attempt "
                      << (reconnect_attempts + 1) << "/"
                      << max_reconnect_attempts << ")..." << std::endl;
            if (should_clear_queue_early) {
              std::cerr << "[InstanceRegistry] [RTMP Destination Monitor] Early reconnect "
                           "to clear queue and prevent backup" << std::endl;
            }

            bool reconnect_success = reconnectRTMPDestinationStream(instanceId, stop_flag);

            last_reconnect_attempt = now;

            if (reconnect_success) {
              std::cerr << "[InstanceRegistry] [RTMP Destination Monitor] ✓ Reconnection "
                           "successful!"
                        << std::endl;
              // Reset reconnect attempts on success
              {
                std::lock_guard<std::mutex> lock(rtmp_des_monitor_mutex_);
                auto attemptsIt = rtmp_des_reconnect_attempts_.find(instanceId);
                if (attemptsIt != rtmp_des_reconnect_attempts_.end()) {
                  attemptsIt->second.store(0);
                }
                rtmp_des_last_activity_[instanceId] = now;
              }
              // CRITICAL: Update last successful reconnect time to start grace period
              // This prevents immediate reconnects and allows connection to establish
              last_successful_reconnect = now;
              std::cerr << "[InstanceRegistry] [RTMP Destination Monitor] ✓ Grace period started: "
                           "Will not reconnect for " << reconnect_grace_period.count() 
                        << " seconds to allow connection establishment" << std::endl;
            } else {
              std::cerr
                  << "[InstanceRegistry] [RTMP Destination Monitor] ✗ Reconnection failed"
                  << std::endl;
              // Increment reconnect attempts
              {
                std::lock_guard<std::mutex> lock(rtmp_des_monitor_mutex_);
                auto attemptsIt = rtmp_des_reconnect_attempts_.find(instanceId);
                if (attemptsIt != rtmp_des_reconnect_attempts_.end()) {
                  attemptsIt->second.fetch_add(1);
                }
              }
            }
          } else {
            std::cerr << "[InstanceRegistry] [RTMP Destination Monitor] ⚠ Maximum "
                         "reconnect attempts ("
                      << max_reconnect_attempts
                      << ") reached. Stopping reconnect attempts." << std::endl;
          }
        } else {
          // Still in cooldown period
          int remaining_cooldown =
              reconnect_cooldown.count() - time_since_last_reconnect;
          if (remaining_cooldown > 0 &&
              (now - last_activity_check).count() > 30) {
            std::cerr << "[InstanceRegistry] [RTMP Destination Monitor] Waiting "
                      << remaining_cooldown
                      << " seconds before next reconnect attempt..."
                      << std::endl;
            last_activity_check = now;
          }
        }
      } else {
        // Stream appears active
        if (has_activity) {
          if (!has_connected) {
            {
              std::lock_guard<std::mutex> lock(rtmp_des_monitor_mutex_);
              auto connectedIt = rtmp_des_has_connected_.find(instanceId);
              if (connectedIt != rtmp_des_has_connected_.end()) {
                connectedIt->second.store(true);
              }
            }
            std::cerr
                << "[InstanceRegistry] [RTMP Destination Monitor] ✓ RTMP destination connection "
                   "established successfully (first activity detected after "
                << time_since_start << " seconds)" << std::endl;
          }

          if (reconnect_attempts > 0) {
            std::cerr << "[InstanceRegistry] [RTMP Destination Monitor] ✓ Stream is active "
                         "again (activity "
                      << time_since_activity << " seconds ago)" << std::endl;
            {
              std::lock_guard<std::mutex> lock(rtmp_des_monitor_mutex_);
              auto attemptsIt = rtmp_des_reconnect_attempts_.find(instanceId);
              if (attemptsIt != rtmp_des_reconnect_attempts_.end()) {
                attemptsIt->second.store(0);
              }
            }
          }
        }
      }

      // Log status during initial connection phase
      if (is_initial_connection_phase) {
        static std::map<std::string, int> last_logged_time;
        int last_logged = 0;
        auto it = last_logged_time.find(instanceId);
        if (it != last_logged_time.end()) {
          last_logged = it->second;
        }

        bool should_log = false;
        if (time_since_start == 10 || time_since_start == 30) {
          should_log = true;
        } else if (time_since_start > 30 &&
                   (time_since_start - last_logged) >= 30) {
          should_log = true;
        }

        if (should_log) {
          std::cerr
              << "[InstanceRegistry] [RTMP Destination Monitor] ⏳ Initial connection "
                 "phase: waiting for RTMP destination to establish ("
              << time_since_start << "s / "
              << initial_connection_timeout.count()
              << "s)..." << std::endl;
          last_logged_time[instanceId] = time_since_start;
        }
      }
    }

    std::cerr
        << "[InstanceRegistry] [RTMP Destination Monitor] Thread stopped for instance "
        << instanceId << std::endl;
  });

  // Store thread
  {
    std::lock_guard<std::mutex> lock(rtmp_des_monitor_mutex_);
    rtmp_des_monitor_threads_[instanceId] = std::move(monitor_thread);
  }

  std::cerr << "[InstanceRegistry] [RTMP Destination Monitor] Monitoring thread started "
               "for instance "
            << instanceId << std::endl;
}

void InstanceRegistry::stopRTMPDestinationMonitorThread(const std::string &instanceId) {
  std::unique_lock<std::mutex> lock(rtmp_des_monitor_mutex_);

  // Set stop flag
  auto flagIt = rtmp_des_monitor_stop_flags_.find(instanceId);
  if (flagIt != rtmp_des_monitor_stop_flags_.end() && flagIt->second) {
    flagIt->second->store(true);
  }

  // Get thread handle and release lock before joining to avoid deadlock
  std::thread threadToJoin;
  auto threadIt = rtmp_des_monitor_threads_.find(instanceId);
  if (threadIt != rtmp_des_monitor_threads_.end()) {
    if (threadIt->second.joinable()) {
      threadToJoin = std::move(threadIt->second);
    }
    rtmp_des_monitor_threads_.erase(threadIt);
  }

  // Remove stop flag and other tracking data
  if (flagIt != rtmp_des_monitor_stop_flags_.end()) {
    rtmp_des_monitor_stop_flags_.erase(flagIt);
  }
  rtmp_des_last_activity_.erase(instanceId);
  rtmp_des_reconnect_attempts_.erase(instanceId);
  rtmp_des_has_connected_.erase(instanceId);

  // Release lock before joining to avoid deadlock
  lock.unlock();

  // Join thread with timeout
  if (threadToJoin.joinable()) {
    auto future = std::async(std::launch::async,
                             [&threadToJoin]() { threadToJoin.join(); });

    auto status = future.wait_for(std::chrono::seconds(5));
    if (status == std::future_status::timeout) {
      std::cerr << "[InstanceRegistry] [RTMP Destination Monitor] ⚠ CRITICAL: Thread join "
                   "timeout (5s)"
                << std::endl;
      threadToJoin.detach();
      std::this_thread::sleep_for(std::chrono::seconds(1));
    } else {
      std::cerr
          << "[InstanceRegistry] [RTMP Destination Monitor] ✓ Thread joined successfully"
          << std::endl;
    }
  }
}

void InstanceRegistry::updateRTMPDestinationActivity(const std::string &instanceId) {
  // OPTIMIZATION: Use try_lock to avoid blocking frame processing
  std::unique_lock<std::mutex> lock(rtmp_des_monitor_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return; // Lock is busy - skip this update
  }

  rtmp_des_last_activity_[instanceId] = std::chrono::steady_clock::now();

  // Mark as successfully connected when we send frames
  auto connectedIt = rtmp_des_has_connected_.find(instanceId);
  if (connectedIt != rtmp_des_has_connected_.end() && !connectedIt->second.load()) {
    // First time we send frames - mark as connected
    connectedIt->second.store(true);
  }
}

bool InstanceRegistry::reconnectRTMPDestinationStream(
    const std::string &instanceId,
    std::shared_ptr<std::atomic<bool>> stopFlag) {
  try {
    std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] Starting reconnect for instance "
              << instanceId << std::endl;

    // Check if instance still exists and is running
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Instance not found or not running"
                  << std::endl;
        return false;
      }
    }

    // Get pipeline nodes
    auto nodes = getInstanceNodes(instanceId);
    if (nodes.empty()) {
      std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Pipeline not found"
                << std::endl;
      return false;
    }

    // CRITICAL: Check stop flag before proceeding
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Aborted: instance is being stopped"
                << std::endl;
      return false;
    }

    // CRITICAL: Find ALL RTMP destination nodes (including nodes from previous reconnects)
    // We need to detach ALL of them, not just one
    std::vector<std::shared_ptr<cvedix_nodes::cvedix_rtmp_des_node>> oldRtmpNodes;
    for (const auto &node : nodes) {
      auto rtmpDesNode = std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node);
      if (rtmpDesNode) {
        oldRtmpNodes.push_back(rtmpDesNode);
      }
    }

    if (oldRtmpNodes.empty()) {
      std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ RTMP destination node not found"
                << std::endl;
      return false;
    }

    std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] Found " << oldRtmpNodes.size() 
              << " RTMP destination node(s) to detach" << std::endl;

    // CRITICAL: Check stop flag before stopping node
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Aborted: instance is being stopped (before stopping node)"
                << std::endl;
      return false;
    }

    std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] Reconnecting RTMP destination node..."
              << std::endl;
    std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] CRITICAL: Recreating RTMP destination node to fully reset GStreamer pipeline and ensure uptime"
              << std::endl;

    // Get RTMP URL from instance info
    std::string rtmpUrl;
    int channel = 0; // Default channel
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt != instances_.end()) {
        const auto &info = instanceIt->second;
        // Get RTMP URL from RTMP_DES_URL or RTMP_URL
        auto rtmpDesIt = info.additionalParams.find("RTMP_DES_URL");
        if (rtmpDesIt != info.additionalParams.end() && !rtmpDesIt->second.empty()) {
          rtmpUrl = rtmpDesIt->second;
        } else if (!info.rtmpUrl.empty()) {
          rtmpUrl = info.rtmpUrl;
        } else {
          auto rtmpIt = info.additionalParams.find("RTMP_URL");
          if (rtmpIt != info.additionalParams.end() && !rtmpIt->second.empty()) {
            rtmpUrl = rtmpIt->second;
          }
        }
        // Get channel from params if available
        auto channelIt = info.additionalParams.find("channel");
        if (channelIt != info.additionalParams.end()) {
          try {
            channel = std::stoi(channelIt->second);
          } catch (...) {
            channel = 0;
          }
        }
      }
    }

    if (rtmpUrl.empty()) {
      std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ RTMP URL not found in instance config"
                << std::endl;
      return false;
    }

    std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] RTMP URL: " << rtmpUrl << ", Channel: " << channel << std::endl;

    // CRITICAL: Find parent node that feeds into RTMP destination
    // Pipeline structure: ... -> osd -> rtmp_lastframe_proxy -> rtmp_des
    std::shared_ptr<cvedix_nodes::cvedix_node> parentNode = nullptr;

    // First, try to find the last-frame proxy (rtmp_des is attached to proxy)
    for (const auto &node : nodes) {
      if (!node) continue;
      if (std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node))
        continue;
      if (std::dynamic_pointer_cast<edgeos::RtmpLastFrameFallbackProxyNode>(node)) {
        parentNode = node;
        std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] Found last-frame proxy parent node" << std::endl;
        break;
      }
    }

    // If no proxy, try OSD node (legacy pipeline: ... -> osd -> rtmp_des)
    if (!parentNode) {
      for (const auto &node : nodes) {
        if (!node) continue;
        if (std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node))
          continue;
        bool isOSDNode =
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_face_osd_node_v2>(node) != nullptr ||
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_osd_node_v3>(node) != nullptr ||
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_line_crossline_osd_node>(node) != nullptr ||
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_area_jam_osd_node>(node) != nullptr ||
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_stop_osd_node>(node) != nullptr;
        if (isOSDNode) {
          parentNode = node;
          std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] Found OSD parent node: " << typeid(*node).name() << std::endl;
          break;
        }
      }
    }
    
    // If OSD node not found, try other possible parents as fallback
    // Check for ba_loitering or ba_crossline nodes (less common, but possible)
    if (!parentNode) {
      for (const auto &node : nodes) {
        if (!node) {
          continue;
        }
        // Skip RTMP destination nodes (we're looking for parent, not destination)
        auto rtmpDesNode = std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node);
        if (rtmpDesNode) {
          continue;
        }
        // Check if this is a BA node using dynamic_pointer_cast
        auto baLoiteringNode = std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_area_loitering_node>(node);
        auto baCrosslineNode = std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_line_crossline_node>(node);
        
        if (baLoiteringNode || baCrosslineNode) {
          parentNode = node;
          std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] Found fallback parent node: " << typeid(*node).name() << std::endl;
          break;
        }
      }
    }

    if (!parentNode) {
      std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ⚠ Warning: Could not find parent node, will try detach anyway"
                << std::endl;
    }

    // CRITICAL: Check stop flag before detaching
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Aborted: instance is being stopped (before detaching node)"
                << std::endl;
      return false;
    }

    // CRITICAL: Lock ordering to prevent deadlock
    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Instance stopped before acquiring GStreamer lock"
                  << std::endl;
        return false;
      }
    }

    // Acquire GStreamer lock for node operations
    std::unique_lock<std::shared_mutex> gstLock(gstreamer_ops_mutex_);

    // CRITICAL: Final check before detaching
    if (stopFlag && stopFlag->load()) {
      std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Aborted: stop flag set after acquiring GStreamer lock"
                << std::endl;
      return false;
    }

    {
      std::shared_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt == instances_.end() || !instanceIt->second.running) {
        std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Instance stopped after acquiring GStreamer lock"
                  << std::endl;
        return false;
      }
    }

    try {
      // Step 1: Detach ALL old RTMP destination nodes to clear queues and release GStreamer resources
      // CRITICAL: Detach ALL old nodes to prevent OSD node from dispatching frames to them
      std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] Step 1/3: Detaching ALL old RTMP destination nodes..."
                << std::endl;
      
      for (const auto &oldRtmpNode : oldRtmpNodes) {
        if (oldRtmpNode) {
          try {
            oldRtmpNode->detach_recursively();
            std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✓ Detached old RTMP destination node"
                      << std::endl;
          } catch (const std::exception &e) {
            std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ⚠ Warning: Exception detaching old node: " 
                      << e.what() << std::endl;
          }
        }
      }
      
      std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✓ All " << oldRtmpNodes.size() 
                << " old RTMP destination node(s) detached" << std::endl;

      // CRITICAL: Wait for GStreamer pipeline to fully release resources
      // AND wait for OSD node to update its internal destination node list
      // OSD node may need time to remove detached nodes from its dispatch list
      // AND clear any queued frames that were waiting to be dispatched to the old destination
      // Increased wait time to ensure queues are fully cleared before creating new destination
      std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] Waiting for queues to clear (3 seconds)..."
                << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(3000));

      // CRITICAL: Check stop flag before creating new node
      if (stopFlag && stopFlag->load()) {
        std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Aborted: instance is being stopped (after detaching)"
                  << std::endl;
        return false;
      }

      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto instanceIt = instances_.find(instanceId);
        if (instanceIt == instances_.end() || !instanceIt->second.running) {
          std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Instance stopped after detaching"
                    << std::endl;
          return false;
        }
      }

      // Step 2: Create new RTMP destination node with same config to fully reset GStreamer pipeline
      std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] Step 2/3: Creating new RTMP destination node..."
                << std::endl;
      
      // Generate new node name with timestamp to ensure uniqueness
      std::string newNodeName = "rtmp_des_" + instanceId + "_reconnect_" + 
                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
      
      // Create new RTMP destination node with same config
      std::shared_ptr<cvedix_nodes::cvedix_rtmp_des_node> newRtmpNode = 
          std::make_shared<cvedix_nodes::cvedix_rtmp_des_node>(
              newNodeName, channel, rtmpUrl);
      
      std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✓ New RTMP destination node created: " << newNodeName << std::endl;

      // CRITICAL: Check stop flag before attaching new node
      if (stopFlag && stopFlag->load()) {
        std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Aborted: instance is being stopped (after creating new node)"
                  << std::endl;
        return false;
      }

      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto instanceIt = instances_.find(instanceId);
        if (instanceIt == instances_.end() || !instanceIt->second.running) {
          std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Instance stopped after creating new node"
                    << std::endl;
          return false;
        }
      }

      // Step 3: Attach new RTMP destination node to parent node
      if (parentNode && newRtmpNode) {
        std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] Step 3/3: Attaching new RTMP destination node to parent..."
                  << std::endl;
        newRtmpNode->attach_to({parentNode});
        std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✓ New RTMP destination node attached successfully"
                  << std::endl;

        // Set activity hook on new node so monitor sees real push activity
        newRtmpNode->set_stream_status_hooker(
            [this, instanceId](std::string /*node_name*/,
                               cvedix_nodes::cvedix_stream_status /*status*/) {
              updateRTMPDestinationActivity(instanceId);
            });
        newRtmpNode->set_meta_handled_hooker(
            [this, instanceId](std::string /*node_name*/, int /*queue_size*/,
                               std::shared_ptr<cvedix_objects::cvedix_meta> meta) {
              if (meta && meta->meta_type ==
                              cvedix_objects::cvedix_meta_type::FRAME) {
                updateRTMPDestinationActivity(instanceId);
              }
            });

        // CRITICAL: Update pipelines_ map to remove old RTMP destination nodes and add new one
        // This prevents OSD node from dispatching frames to old nodes
        {
          // Release GStreamer lock before acquiring mutex_ with unique_lock
          gstLock.unlock();
          
          std::unique_lock<std::shared_timed_mutex> lock(mutex_);
          auto pipelineIt = pipelines_.find(instanceId);
          if (pipelineIt != pipelines_.end()) {
            auto &pipelineNodes = pipelineIt->second;
            
            // Remove ALL old RTMP destination nodes (including nodes from previous reconnects)
            pipelineNodes.erase(
              std::remove_if(pipelineNodes.begin(), pipelineNodes.end(),
                [](const std::shared_ptr<cvedix_nodes::cvedix_node> &node) {
                  if (!node) return false;
                  // Check if this is an RTMP destination node
                  auto rtmpDesNode = std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node);
                  return rtmpDesNode != nullptr;
                }),
              pipelineNodes.end()
            );
            
            // Add new RTMP destination node
            pipelineNodes.push_back(newRtmpNode);
            
            std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✓ Updated pipelines_ map: removed old RTMP destination nodes, added new node"
                      << std::endl;
          } else {
            std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ⚠ Warning: Pipeline not found in pipelines_ map"
                      << std::endl;
          }
        }
        // Note: gstLock is released above and not re-acquired since we're done with GStreamer operations
        
        std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✓ GStreamer pipeline fully reset - new connection established"
                  << std::endl;
      } else {
        std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ⚠ Warning: Could not attach new RTMP destination node (parent or node is null)"
                  << std::endl;
        return false;
      }

      // Update activity time and mark as connected
      updateRTMPDestinationActivity(instanceId);

      // Mark as successfully connected
      {
        std::lock_guard<std::mutex> lock(rtmp_des_monitor_mutex_);
        auto connectedIt = rtmp_des_has_connected_.find(instanceId);
        if (connectedIt != rtmp_des_has_connected_.end()) {
          connectedIt->second.store(true);
        }
      }

      return true;
    } catch (const std::exception &e) {
      std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Exception restarting RTMP destination node: "
                << e.what() << std::endl;
      return false;
    } catch (...) {
      std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Unknown error during reconnect"
                << std::endl;
      return false;
    }
  } catch (const std::exception &e) {
    std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Exception in reconnectRTMPDestinationStream: "
              << e.what() << std::endl;
    return false;
  } catch (...) {
    std::cerr << "[InstanceRegistry] [RTMP Destination Reconnect] ✗ Unknown error during reconnect"
              << std::endl;
    return false;
  }
}

