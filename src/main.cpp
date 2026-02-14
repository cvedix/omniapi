#include "api/create_instance_handler.h"
#include "api/health_handler.h"
#include "api/instance_handler.h"
#include "api/quick_instance_handler.h"
#include "api/swagger_handler.h"
#include "api/scalar_handler.h"
#include "api/hls_handler.h"
#include "api/version_handler.h"
#include "api/watchdog_handler.h"
#include <drogon/drogon.h>
#ifdef ENABLE_SYSTEM_INFO_HANDLER
#include "api/system_info_handler.h"
#endif
#include "api/license_handler.h"
#include "api/ai_websocket.h"
#include "api/config_handler.h"
#include "api/endpoints_handler.h"
#include "api/group_handler.h"
#include "api/jams_handler.h"
#include "api/lines_handler.h"
#include "api/log_handler.h"
#include "api/node_handler.h"
#include "api/onvif_handler.h"
#include "api/recognition_handler.h"
#include "api/solution_handler.h"
#include "api/stops_handler.h"
#include "api/securt_handler.h"
#include "api/securt_line_handler.h"
#include "api/area_handler.h"
#include "api/system_handler.h"
#include "core/exclusion_area_manager.h"
#include "core/securt_feature_manager.h"
#include "core/securt_instance_manager.h"
#include "core/securt_line_manager.h"
#include "core/analytics_entities_manager.h"
#include "core/area_storage.h"
#include "core/area_manager.h"
#ifdef ENABLE_METRICS_HANDLER
#include "api/metrics_handler.h"
#endif
#include "config/system_config.h"
#include "core/system_config_manager.h"
#include "core/preferences_manager.h"
#include "core/decoder_detector.h"
#include "core/categorized_logger.h"
#include "core/cors_filter.h"
#include "core/env_config.h"
#include "core/health_monitor.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/metrics_interceptor.h"
#include "core/node_pool_manager.h"
#include "core/node_storage.h"
#include "core/pipeline_builder.h"
#include "core/request_middleware.h"
#include "core/timeout_constants.h"
#include "core/watchdog.h"
#include "fonts/font_upload_handler.h"
#include "groups/group_registry.h"
#include "groups/group_storage.h"
#include "instances/inprocess_instance_manager.h"
#include "instances/instance_manager_factory.h"
#include "instances/instance_registry.h"
#include "instances/instance_storage.h"
#include "instances/queue_monitor.h"
#include "models/model_upload_handler.h"
#include "solutions/solution_registry.h"
#include "solutions/solution_storage.h"
#include "utils/gstreamer_checker.h"
#include "videos/video_upload_handler.h"
#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <csetjmp>
#include <csignal>
#include <cstdlib>
#include <cvedix/nodes/src/cvedix_file_src_node.h>
#include <cvedix/nodes/src/cvedix_rtsp_src_node.h>
#include <cvedix/utils/analysis_board/cvedix_analysis_board.h>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

/**
 * @brief Edge AI API Server
 *
 * REST API server using Drogon framework
 * Provides health check and version endpoints
 * Includes watchdog and health monitoring on separate threads
 */

// Global flag for graceful shutdown
static bool g_shutdown = false;

// Timestamp when shutdown was requested (for watchdog)
static std::chrono::steady_clock::time_point g_shutdown_request_time;
static std::atomic<bool> g_shutdown_requested{false};

// Global watchdog and health monitor instances
static std::unique_ptr<Watchdog> g_watchdog;
static std::unique_ptr<HealthMonitor> g_health_monitor;

// Global instance registry pointer for error recovery
static InstanceRegistry *g_instance_registry = nullptr;

// Flag to prevent multiple handlers from stopping instances simultaneously
static std::atomic<bool> g_cleanup_in_progress{false};

// Flag to indicate cleanup has been completed (prevents abort after cleanup)
static std::atomic<bool> g_cleanup_completed{false};

// Flag to indicate force exit requested (bypass SIGABRT recovery)
static std::atomic<bool> g_force_exit{false};

// Global debug flag
static std::atomic<bool> g_debug_mode{false};

// Global logging flags (exported via logging_flags.h)
std::atomic<bool> g_log_api{false};
std::atomic<bool> g_log_instance{false};
std::atomic<bool> g_log_sdk_output{false};

// Global analysis board thread management
static std::unique_ptr<std::thread> g_analysis_board_display_thread;
static std::atomic<bool> g_stop_analysis_board{false};
static std::atomic<bool> g_analysis_board_running{false};
static std::atomic<bool> g_analysis_board_disabled{
    false}; // Flag to disable after Qt abort

// Signal handler for segmentation fault (catch GStreamer crashes)
// Rate limiting to prevent log spam when crashes occur repeatedly
static std::atomic<int> g_segfault_count{0};
static std::atomic<std::chrono::steady_clock::time_point> g_last_segfault_log{
    std::chrono::steady_clock::now()};
static std::atomic<std::chrono::steady_clock::time_point> g_first_segfault_time{
    std::chrono::steady_clock::now()};
static constexpr auto SEGFAULT_LOG_INTERVAL =
    std::chrono::seconds(5); // Log at most once every 5 seconds
static constexpr int MAX_SEGFAULTS_BEFORE_FORCE_EXIT =
    100; // Force exit if > 100 crashes (reduced from 10000 for faster response)
static constexpr auto SEGFAULT_TIME_WINDOW =
    std::chrono::minutes(1); // Reset counter after 1 minute

void segfaultHandler(int /*signal*/) // Parameter name commented to avoid
                                     // unused-parameter warning
{
  // SIGSEGV handler - catch segmentation faults from GStreamer pipeline crashes
  auto now = std::chrono::steady_clock::now();
  auto last_log = g_last_segfault_log.load();
  auto time_since_last_log =
      std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count();

  // Increment counter (atomic operation to prevent race conditions)
  int count = g_segfault_count.fetch_add(1, std::memory_order_relaxed) + 1;

  // Check if we need to reset counter (after time window)
  auto first_crash = g_first_segfault_time.load(std::memory_order_relaxed);
  auto time_since_first =
      std::chrono::duration_cast<std::chrono::minutes>(now - first_crash)
          .count();
  if (time_since_first >= SEGFAULT_TIME_WINDOW.count()) {
    // Reset counter if more than 1 minute has passed since first crash
    g_segfault_count.store(1, std::memory_order_relaxed);
    g_first_segfault_time.store(now, std::memory_order_relaxed);
    count = 1;
  } else if (count == 1) {
    // First crash in this window - record the time
    g_first_segfault_time.store(now, std::memory_order_relaxed);
  }

  // CRITICAL: Force exit if too many crashes (prevents infinite crash loop)
  // FIXED: Check BEFORE logging to prevent counter from being reset incorrectly
  if (count > MAX_SEGFAULTS_BEFORE_FORCE_EXIT) {
    std::cerr << "\n[CRITICAL] ========================================"
              << std::endl;
    std::cerr << "[CRITICAL] Too many segmentation faults (" << count
              << ") - forcing exit to prevent infinite crash loop" << std::endl;
    std::cerr << "[CRITICAL] This indicates RTSP stream is completely unstable"
              << std::endl;
    std::cerr
        << "[CRITICAL] Process will exit immediately to prevent system hang"
        << std::endl;
    std::cerr << "[CRITICAL] ========================================"
              << std::endl;
    std::fflush(stdout);
    std::fflush(stderr);

    // Set force exit flag
    g_force_exit.store(true, std::memory_order_relaxed);

    // Try to stop all instances quickly (with timeout)
    // FIX: Capture g_instance_registry pointer value to avoid use-after-free
    InstanceRegistry *registry_ptr =
        g_instance_registry;             // Capture pointer value
    auto force_exit_ref = &g_force_exit; // Capture address of atomic (safe)
    if (registry_ptr) {
      std::thread([registry_ptr, force_exit_ref]() {
        try {
          auto instances = registry_ptr->listInstances();
          for (const auto &instanceId : instances) {
            if (force_exit_ref->load(std::memory_order_relaxed))
              break;
            try {
              auto optInfo = registry_ptr->getInstance(instanceId);
              if (optInfo.has_value() && optInfo.value().running) {
                // Use async with very short timeout (100ms) - just try to stop,
                // don't wait FIX: Capture registry_ptr by value
                InstanceRegistry *reg_ptr = registry_ptr;
                // ✅ Store future to avoid unused-result warning (we don't wait
                // for it)
                auto future =
                    std::async(std::launch::async, [reg_ptr, instanceId]() {
                      try {
                        if (reg_ptr) {
                          reg_ptr->stopInstance(instanceId);
                        }
                      } catch (...) {
                        // Ignore errors
                      }
                    });
                (void)future; // Explicitly ignore future to suppress warning
              }
            } catch (...) {
              // Ignore errors
            }
          }
        } catch (...) {
          // Ignore errors
        }
      }).detach();
    }

    // Wait a moment then force exit
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::_Exit(1); // Force exit immediately
    return;
  }

  // Only log if enough time has passed since last log (rate limiting)
  // FIXED: Do NOT reset counter after logging - this was causing the bug
  // Counter should only reset after time window expires, not after each log
  if (time_since_last_log >= SEGFAULT_LOG_INTERVAL.count()) {
    // Update last log time
    g_last_segfault_log.store(now, std::memory_order_relaxed);

    std::cerr << "\n[CRITICAL] ========================================"
              << std::endl;
    std::cerr << "[CRITICAL] Segmentation fault (SIGSEGV) detected! (count: "
              << count << ")" << std::endl;
    std::cerr << "[CRITICAL] This is likely caused by GStreamer pipeline crash "
                 "when RTSP stream is lost"
              << std::endl;
    std::cerr << "[CRITICAL] Monitoring thread will attempt to reconnect "
                 "automatically"
              << std::endl;
    if (count > 1000) {
      std::cerr << "[CRITICAL] WARNING: Very high crash count (" << count
                << ") - consider stopping problematic instances" << std::endl;
      std::cerr
          << "[CRITICAL] If crashes continue, process will auto-exit after "
          << MAX_SEGFAULTS_BEFORE_FORCE_EXIT << " crashes" << std::endl;
    } else if (count > 10) {
      std::cerr << "[CRITICAL] WARNING: Multiple crashes detected (" << count
                << ") - RTSP stream may be unstable" << std::endl;
      std::cerr
          << "[CRITICAL] Consider stopping the problematic instance manually"
          << std::endl;
    } else if (count > 1) {
      std::cerr << "[CRITICAL] Note: Multiple crashes detected (" << count
                << ") - RTSP stream may be unstable" << std::endl;
    }
    std::cerr << "[CRITICAL] ========================================"
              << std::endl;

    // FIXED: Do NOT reset counter here - this was the bug!
    // Counter should only reset after time window expires (checked above)
    // Resetting here caused race conditions when multiple SIGSEGV occurred
    // quickly
  }

  // CRITICAL: Do NOT stop instances here - let monitoring thread handle
  // reconnection Stopping instances here would prevent auto-reconnect The
  // monitoring thread will detect the crash and attempt to reconnect

  // Flush all output
  std::fflush(stdout);
  std::fflush(stderr);

  // Note: We don't exit here - let the process continue and monitoring thread
  // will handle reconnection However, if this is a critical crash, the process
  // may still exit The monitoring thread should detect inactivity and reconnect
}

// Signal handler for graceful shutdown
void signalHandler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    static std::atomic<int> signal_count{0};
    static std::atomic<bool> signal_handling{false};

    // Prevent multiple signal handlers from running simultaneously
    // Use compare_exchange to ensure only one handler runs at a time
    bool expected = false;
    if (!signal_handling.compare_exchange_strong(expected, true)) {
      // Another handler is already running, just increment count and return
      signal_count.fetch_add(1);
      return;
    }

    int count = signal_count.fetch_add(1) + 1;

    if (count == 1) {
      // First signal - attempt graceful shutdown
      PLOG_INFO << "Received signal " << signal
                << ", shutting down gracefully...";
      std::cerr << "[SHUTDOWN] Received signal " << signal
                << ", shutting down gracefully..." << std::endl;
      std::cerr << "[SHUTDOWN] Press Ctrl+C again to force immediate exit"
                << std::endl;
      g_shutdown = true;
      g_shutdown_requested = true;
      g_shutdown_request_time = std::chrono::steady_clock::now();

      // CRITICAL: Release signal handling lock IMMEDIATELY after setting
      // g_shutdown This allows subsequent signals to be processed quickly
      signal_handling.store(false);

      // CRITICAL: Call quit() immediately in signal handler (thread-safe in
      // Drogon) This ensures the main event loop exits even if cleanup threads
      // are slow
      try {
        auto &app = drogon::app();
        app.quit();
        // Also try to stop the event loop explicitly
        auto *loop = app.getLoop();
        if (loop) {
          loop->quit();
        }
      } catch (...) {
        // Ignore errors - try to exit anyway
      }

      // CRITICAL: Force exit immediately - RTSP retry loops run in SDK threads
      // and cannot be stopped gracefully Use a separate thread to force exit
      // after very short delay (50ms) This ensures we don't wait forever for
      // RTSP retry loops FIX: Capture g_force_exit and g_shutdown by value to
      // avoid race conditions
      auto force_exit_ref = &g_force_exit; // Capture address of atomic (safe)
      bool shutdown_value = g_shutdown;    // Capture value
      std::thread([force_exit_ref, shutdown_value]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (shutdown_value && !force_exit_ref->load()) {
          // If still shutting down after 50ms, force exit immediately
          std::cerr << "[CRITICAL] Force exit after 50ms - RTSP retry loops "
                       "blocking shutdown"
                    << std::endl;
          std::fflush(stdout);
          std::fflush(stderr);
          force_exit_ref->store(true);
          // Use abort() to force immediate termination - more aggressive than
          // _Exit()
          std::abort();
        }
      }).detach();

      // Stop all instances first (this is critical for clean shutdown)
      // Use a separate thread with timeout to avoid blocking
      // FIX: Capture g_instance_registry pointer value to avoid use-after-free
      // NOTE: This is still not 100% safe, but better than capturing by
      // reference Better solution would be to use std::shared_ptr, but requires
      // more changes
      InstanceRegistry *registry_ptr =
          g_instance_registry; // Capture pointer value
      auto force_exit_ref2 =
          &g_force_exit; // Capture address of atomic (safe) - reuse same
                         // variable name from above
      std::thread([registry_ptr, force_exit_ref2]() {
        if (registry_ptr) {
          try {
            std::cerr << "[SHUTDOWN] Stopping all instances (timeout: 200ms "
                         "per instance)..."
                      << std::endl;
            std::cerr << "[SHUTDOWN] RTSP retry loops may prevent graceful "
                         "stop - will force exit if timeout"
                      << std::endl;
            PLOG_INFO << "Stopping all instances before shutdown...";

            // Get list of instances (with timeout protection)
            std::vector<std::string> instances;
            try {
              instances = registry_ptr->listInstances();
            } catch (...) {
              std::cerr
                  << "[SHUTDOWN] Warning: Cannot list instances, skipping..."
                  << std::endl;
              instances.clear();
            }

            // Stop instances with shorter timeout - RTSP retry loops can block
            // indefinitely Use async with timeout to prevent blocking
            int stopped_count = 0;
            for (const auto &instanceId : instances) {
              if (force_exit_ref2->load())
                break; // Check if force exit was requested

              try {
                auto optInfo = registry_ptr->getInstance(instanceId);
                if (optInfo.has_value() && optInfo.value().running) {
                  std::cerr << "[SHUTDOWN] Stopping instance: " << instanceId
                            << std::endl;
                  PLOG_INFO << "Stopping instance: " << instanceId;

                  // Use async with shorter timeout (200ms) - RTSP retry loops
                  // may block FIX: Capture registry_ptr and instanceId by value
                  InstanceRegistry *reg_ptr = registry_ptr; // Capture for async
                  auto future = std::async(
                      std::launch::async, [reg_ptr, instanceId]() -> bool {
                        try {
                          if (reg_ptr) {
                            reg_ptr->stopInstance(instanceId);
                            return true;
                          }
                          return false;
                        } catch (...) {
                          return false;
                        }
                      });

                  // Wait with shorter timeout (200ms per instance)
                  // RTSP retry loops can prevent stop() from returning, so we
                  // don't wait long
                  auto status = future.wait_for(std::chrono::milliseconds(200));
                  if (status == std::future_status::timeout) {
                    std::cerr << "[SHUTDOWN] Warning: Instance " << instanceId
                              << " stop timeout (200ms), skipping..."
                              << std::endl;
                    std::cerr << "[SHUTDOWN] RTSP retry loop may be blocking "
                                 "stop() - will force exit"
                              << std::endl;
                    // Don't wait for it - continue with other instances
                  } else if (status == std::future_status::ready) {
                    try {
                      if (future.get()) {
                        stopped_count++;
                      }
                    } catch (...) {
                      // Ignore errors from get()
                    }
                  }
                }
              } catch (const std::exception &e) {
                PLOG_WARNING << "Failed to stop instance " << instanceId << ": "
                             << e.what();
                std::cerr << "[SHUTDOWN] Warning: Failed to stop instance "
                          << instanceId << ": " << e.what() << std::endl;
              } catch (...) {
                PLOG_WARNING << "Failed to stop instance " << instanceId
                             << " (unknown error)";
                std::cerr << "[SHUTDOWN] Warning: Failed to stop instance "
                          << instanceId << " (unknown error)" << std::endl;
              }
            }
            std::cerr << "[SHUTDOWN] Stopped " << stopped_count
                      << " instance(s)" << std::endl;
            PLOG_INFO << "All instances stopped";
          } catch (const std::exception &e) {
            PLOG_WARNING << "Error stopping instances: " << e.what();
            std::cerr << "[SHUTDOWN] Warning: Error stopping instances: "
                      << e.what() << std::endl;
          } catch (...) {
            PLOG_WARNING << "Error stopping instances (unknown error)";
            std::cerr << "[SHUTDOWN] Warning: Error stopping instances "
                         "(unknown error)"
                      << std::endl;
          }
        }

        // Stop watchdog and health monitor (quick, should not block)
        if (g_health_monitor) {
          try {
            g_health_monitor->stop();
          } catch (...) {
            // Ignore errors
          }
        }
        if (g_watchdog) {
          try {
            g_watchdog->stop();
          } catch (...) {
            // Ignore errors
          }
        }
      }).detach();

      // CRITICAL: Immediately try to force-stop RTSP nodes to break retry loops
      // This runs synchronously in signal handler context (but in separate
      // thread) to be as fast as possible RTSP retry loops run in SDK threads
      // and cannot be stopped gracefully, so we must force detach
      std::thread([]() {
        // Run immediately without delay - this is critical to break retry loops
        if (g_instance_registry) {
          try {
            std::cerr << "[SHUTDOWN] Force-detaching RTSP nodes immediately..."
                      << std::endl;
            // Get all running instances and immediately detach RTSP nodes
            auto instances = g_instance_registry->listInstances();
            for (const auto &instanceId : instances) {
              if (g_force_exit.load())
                break;
              try {
                auto optInfo = g_instance_registry->getInstance(instanceId);
                if (optInfo.has_value() && optInfo.value().running) {
                  // Get nodes from registry and immediately detach RTSP source
                  // nodes
                  auto nodes =
                      g_instance_registry->getInstanceNodes(instanceId);
                  if (!nodes.empty() && nodes[0]) {
                    auto rtspNode = std::dynamic_pointer_cast<
                        cvedix_nodes::cvedix_rtsp_src_node>(nodes[0]);
                    if (rtspNode) {
                      std::cerr << "[SHUTDOWN] Force-detaching RTSP node for "
                                   "instance: "
                                << instanceId << std::endl;
                      try {
                        // Force detach immediately - this should break retry
                        // loop
                        rtspNode->detach_recursively();
                        std::cerr
                            << "[SHUTDOWN] ✓ RTSP node detached for instance: "
                            << instanceId << std::endl;
                      } catch (const std::exception &e) {
                        std::cerr
                            << "[SHUTDOWN] ⚠ Exception detaching RTSP node: "
                            << e.what() << std::endl;
                      } catch (...) {
                        // Ignore errors - just try to break the retry loop
                      }
                    }
                  }
                }
              } catch (...) {
                // Ignore errors - continue with other instances
              }
            }
          } catch (...) {
            // Ignore errors - timeout thread will force exit anyway
          }
        }
      }).detach();

      // Start shutdown timer thread - force exit after configurable timeout
      // (default: 500ms) if still running
      // RTSP retry loops may prevent graceful shutdown, but we need enough time
      // for stopInstance() to complete
      // CRITICAL: This thread MUST run and force exit, even if instances are
      // blocking
      std::thread([]() {
        // Use configurable timeout (default: 500ms) - allows stopInstance() to
        // complete but still forces exit if RTSP retry loops block indefinitely
        auto shutdownTimeout = TimeoutConstants::getShutdownTimeout();
        std::this_thread::sleep_for(shutdownTimeout);

        // Force exit regardless of shutdown state - RTSP retry loops prevent
        // cleanup
        PLOG_WARNING << "Shutdown timeout reached - forcing exit";
        std::cerr << "[CRITICAL] Shutdown timeout ("
                  << TimeoutConstants::getShutdownTimeoutMs()
                  << "ms) - KILLING PROCESS NOW" << std::endl;
        std::cerr << "[CRITICAL] RTSP retry loops prevented graceful shutdown "
                     "- forcing exit"
                  << std::endl;
        std::fflush(stdout);
        std::fflush(stderr);

        // Set force exit flag
        g_force_exit = true;

        // Unregister all signal handlers to prevent recovery
        std::signal(SIGINT, SIG_DFL);
        std::signal(SIGTERM, SIG_DFL);
        std::signal(SIGABRT, SIG_DFL);

        // Use abort() to force immediate termination - more aggressive than
        // _Exit() This is necessary because RTSP retry loops in SDK threads can
        // block forever abort() sends SIGABRT which will be caught by our
        // handler and force exit
        std::abort();
      }).detach();
    } else {
      // Second signal (Ctrl+C again) - force immediate exit
      // This should happen immediately, no delays, no cleanup
      PLOG_WARNING << "Received signal " << signal << " again (" << count
                   << " times) - forcing immediate exit";
      std::cerr << "[CRITICAL] Force exit requested (signal received " << count
                << " times) - KILLING PROCESS NOW" << std::endl;
      std::cerr << "[CRITICAL] Bypassing all cleanup - RTSP retry loops will "
                   "be killed"
                << std::endl;
      std::cerr << "[CRITICAL] Total segfaults before exit: "
                << g_segfault_count.load() << std::endl;
      std::fflush(stdout);
      std::fflush(stderr);

      // Set force exit flag to bypass SIGABRT recovery
      g_force_exit = true;

      // Unregister all signal handlers to prevent any recovery logic
      std::signal(SIGINT, SIG_DFL);
      std::signal(SIGTERM, SIG_DFL);
      std::signal(SIGABRT, SIG_DFL);
      std::signal(SIGSEGV, SIG_DFL); // Also unregister SIGSEGV handler

      // Use abort() to force immediate termination - most aggressive
      // RTSP retry loops in SDK threads will be killed by OS
      // abort() sends SIGABRT which will be caught and force exit immediately
      // No delay, no cleanup - just exit NOW
      std::abort();
      // Note: signal_handling lock not released here because we exit
      // immediately
    }
  } else if (signal == SIGABRT) {
    // If force exit was requested, exit immediately without recovery
    if (g_force_exit.load()) {
      std::cerr << "[CRITICAL] Force exit confirmed - terminating immediately"
                << std::endl;
      std::fflush(stdout);
      std::fflush(stderr);
      // Use abort() for most aggressive termination
      std::abort();
    }

    // SIGABRT can be triggered by:
    // 1. OpenCV DNN shape mismatch (recover by stopping instances)
    // 2. Qt display error in analysis board (don't stop instances, just disable
    // board)

    // Check if analysis board is running - if so, this is likely a Qt display
    // error
    if (g_analysis_board_running.load() || g_debug_mode.load()) {
      std::cerr << "[RECOVERY] Received SIGABRT signal - likely due to Qt "
                   "display error in analysis board"
                << std::endl;
      std::cerr << "[RECOVERY] Analysis board cannot connect to display - "
                   "disabling analysis board"
                << std::endl;
      std::cerr << "[RECOVERY] Server will continue running - instances are "
                   "NOT affected"
                << std::endl;

      // Disable analysis board permanently
      g_analysis_board_disabled = true;
      g_analysis_board_running = false;

      // Don't stop instances - this is just a display error, not a pipeline
      // error Return to allow server to continue
      return;
    }

    // SIGABRT can be triggered by:
    // 1. OpenCV DNN shape mismatch (recover by stopping instances)
    // 2. Queue full causing deadlock (recover by stopping instances)
    // 3. Resource deadlock (mutex locked due to queue full)
    std::cerr << "[RECOVERY] Received SIGABRT signal - possible causes:"
              << std::endl;
    std::cerr << "[RECOVERY]   1. OpenCV DNN shape mismatch (frames with "
                 "inconsistent sizes)"
              << std::endl;
    std::cerr << "[RECOVERY]   2. Queue full causing deadlock (MQTT/processing "
                 "too slow)"
              << std::endl;
    std::cerr
        << "[RECOVERY]   3. Resource deadlock (mutex locked by blocked threads)"
        << std::endl;
    std::cerr << "[RECOVERY] Attempting to recover by stopping problematic "
                 "instances..."
              << std::endl;

    // Check if cleanup is already in progress (from terminate handler)
    // This prevents deadlock if both handlers try to stop instances
    // simultaneously
    bool expected = false;
    if (g_cleanup_in_progress.compare_exchange_strong(expected, true)) {
      if (g_instance_registry) {
        try {
          // Get all instances and stop them
          // listInstances() has timeout protection - may return empty if mutex
          // is locked
          std::vector<std::string> instances;
          try {
            instances = g_instance_registry->listInstances();
          } catch (const std::exception &e) {
            std::cerr << "[RECOVERY] Error listing instances: " << e.what()
                      << std::endl;
            instances.clear();
          } catch (...) {
            std::cerr << "[RECOVERY] Unknown error listing instances"
                      << std::endl;
            instances.clear();
          }

          if (instances.empty()) {
            std::cerr << "[RECOVERY] WARNING: Cannot list instances (mutex may "
                         "be locked due to queue full/deadlock)"
                      << std::endl;
            std::cerr << "[RECOVERY] This usually means:" << std::endl;
            std::cerr << "[RECOVERY]   - Queue is full and threads are blocked"
                      << std::endl;
            std::cerr
                << "[RECOVERY]   - MQTT publish is too slow or network is slow"
                << std::endl;
            std::cerr << "[RECOVERY]   - Processing pipeline cannot keep up "
                         "with frame rate"
                      << std::endl;
            std::cerr << "[RECOVERY] Application will continue, but instances "
                         "may need manual restart"
                      << std::endl;
            std::cerr << "[RECOVERY] Solutions:" << std::endl;
            std::cerr << "[RECOVERY]   1. Reduce frame rate (use RESIZE_RATIO "
                         "or frameRateLimit)"
                      << std::endl;
            std::cerr << "[RECOVERY]   2. Check MQTT broker connection and "
                         "network speed"
                      << std::endl;
            std::cerr << "[RECOVERY]   3. Use faster hardware or reduce "
                         "processing load"
                      << std::endl;
          } else {
            std::cerr << "[RECOVERY] Found " << instances.size()
                      << " instance(s) to stop" << std::endl;
            int stopped_count = 0;
            for (const auto &instanceId : instances) {
              try {
                std::cerr << "[RECOVERY] Stopping instance " << instanceId
                          << "..." << std::endl;

                // Use async with timeout to prevent deadlock if stopInstance()
                // is blocked
                auto future =
                    std::async(std::launch::async, [instanceId]() -> bool {
                      try {
                        if (g_instance_registry) {
                          g_instance_registry->stopInstance(instanceId);
                          return true;
                        }
                        return false;
                      } catch (...) {
                        return false;
                      }
                    });

                // Wait with timeout (1000ms) - if timeout, skip this instance
                // to avoid deadlock
                auto status = future.wait_for(std::chrono::milliseconds(1000));
                if (status == std::future_status::timeout) {
                  std::cerr << "[RECOVERY] Timeout stopping instance "
                            << instanceId
                            << " (1000ms) - instance may be stuck, skipping to "
                               "avoid deadlock"
                            << std::endl;
                } else if (status == std::future_status::ready) {
                  try {
                    future.get(); // Get result (may throw)
                    stopped_count++;
                    std::cerr << "[RECOVERY] Successfully stopped instance "
                              << instanceId << std::endl;
                  } catch (...) {
                    std::cerr << "[RECOVERY] Failed to stop instance "
                              << instanceId << std::endl;
                  }
                }
              } catch (...) {
                std::cerr << "[RECOVERY] Exception stopping instance "
                          << instanceId << std::endl;
              }
            }
            std::cerr << "[RECOVERY] Stopped " << stopped_count << " out of "
                      << instances.size() << " instance(s)" << std::endl;
          }
          std::cerr << "[RECOVERY] Application will continue running."
                    << std::endl;
          std::cerr
              << "[RECOVERY] Please check logs above and fix the root cause:"
              << std::endl;
          std::cerr << "[RECOVERY]   1. If queue full: Reduce frame rate or "
                       "check MQTT/network speed"
                    << std::endl;
          std::cerr << "[RECOVERY]   2. If shape mismatch: Ensure video has "
                       "consistent resolution"
                    << std::endl;
          std::cerr << "[RECOVERY]   3. Try different RESIZE_RATIO values or "
                       "use faster hardware"
                    << std::endl;
        } catch (...) {
          std::cerr << "[RECOVERY] Error accessing instance registry"
                    << std::endl;
        }
      }
      // Mark cleanup as completed to prevent terminate handler from aborting
      g_cleanup_completed.store(true);
      // Reset cleanup flag after a delay to allow recovery
      g_cleanup_in_progress.store(false);
    } else {
      std::cerr << "[RECOVERY] Cleanup already in progress (from terminate "
                   "handler), skipping..."
                << std::endl;
      // Wait a bit to see if cleanup completes
      for (int i = 0; i < 10 && g_cleanup_in_progress.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (!g_cleanup_in_progress.load()) {
        g_cleanup_completed.store(true);
      }
    }

    // For shape mismatch errors, don't exit - let the application continue
    // This is non-standard but allows recovery from OpenCV DNN errors
    // Note: This may cause undefined behavior, but it's better than crashing
    return; // Return from signal handler to continue execution
  }
}

// Terminate handler for uncaught exceptions
void terminateHandler() {
  std::string error_msg;

  // Get exception message first (before doing anything else)
  try {
    auto exception_ptr = std::current_exception();
    if (exception_ptr) {
      std::rethrow_exception(exception_ptr);
    }
  } catch (const std::exception &e) {
    error_msg = e.what();
    PLOG_ERROR << "[CRITICAL] Uncaught exception: " << error_msg;
  } catch (...) {
    error_msg = "Unknown exception";
    PLOG_ERROR << "[CRITICAL] Unknown exception type";
  }

  // Debug: Log exception message to verify it's being captured correctly
  std::cerr << "[DEBUG] Exception message in terminate handler: '" << error_msg
            << "'" << std::endl;

  // Check if this is an OpenCV DNN shape mismatch error
  bool is_shape_mismatch =
      (error_msg.find("getMemoryShapes") != std::string::npos ||
       error_msg.find("eltwise_layer") != std::string::npos ||
       error_msg.find("Assertion failed") != std::string::npos ||
       error_msg.find("inputs[vecIdx][j]") != std::string::npos ||
       error_msg.find("inputs[0] = [") != std::string::npos ||
       error_msg.find("inputs[1] = [") != std::string::npos);

  std::cerr << "[DEBUG] is_shape_mismatch = "
            << (is_shape_mismatch ? "true" : "false") << std::endl;

  if (is_shape_mismatch) {
    std::cerr << "[RECOVERY] Detected OpenCV DNN shape mismatch error - "
                 "attempting recovery..."
              << std::endl;
    std::cerr << "[RECOVERY] This error usually occurs when model receives "
                 "frames with inconsistent sizes"
              << std::endl;
    std::cerr << "[RECOVERY] Exception: " << error_msg << std::endl;

    // Try to stop all instances before aborting
    // NOTE: Use atomic flag to prevent deadlock if SIGABRT handler is also
    // trying to stop instances
    bool expected = false;
    if (g_cleanup_in_progress.compare_exchange_strong(expected, true)) {
      if (g_instance_registry) {
        try {
          // Get list of instances (this acquires lock briefly)
          // If this fails due to deadlock, skip cleanup to avoid crash
          std::vector<std::string> instances;
          try {
            instances = g_instance_registry->listInstances();
          } catch (const std::exception &e) {
            std::cerr
                << "[RECOVERY] Cannot list instances (possible deadlock): "
                << e.what() << std::endl;
            std::cerr
                << "[RECOVERY] Skipping instance cleanup to avoid deadlock"
                << std::endl;
            instances.clear(); // Skip cleanup
          } catch (...) {
            std::cerr << "[RECOVERY] Cannot list instances (unknown error) - "
                         "skipping cleanup"
                      << std::endl;
            instances.clear(); // Skip cleanup
          }

          // Release lock before stopping each instance
          // Use async with timeout to prevent blocking if stopInstance() is
          // stuck
          for (const auto &instanceId : instances) {
            try {
              std::cerr << "[RECOVERY] Stopping instance " << instanceId
                        << " due to shape mismatch..." << std::endl;

              // Use async with timeout to prevent deadlock if stopInstance() is
              // blocked FIX: Capture g_instance_registry pointer value to avoid
              // use-after-free
              InstanceRegistry *reg_ptr = g_instance_registry;
              auto future = std::async(std::launch::async,
                                       [reg_ptr, instanceId]() -> bool {
                                         try {
                                           if (reg_ptr) {
                                             reg_ptr->stopInstance(instanceId);
                                             return true;
                                           }
                                           return false;
                                         } catch (...) {
                                           return false;
                                         }
                                       });

              // Wait with timeout (500ms) - if timeout, skip this instance to
              // avoid deadlock
              auto status = future.wait_for(std::chrono::milliseconds(500));
              if (status == std::future_status::timeout) {
                std::cerr << "[RECOVERY] Timeout stopping instance "
                          << instanceId
                          << " (500ms) - skipping to avoid deadlock"
                          << std::endl;
              } else if (status == std::future_status::ready) {
                try {
                  future.get(); // Get result (may throw)
                } catch (const std::exception &e) {
                  std::cerr << "[RECOVERY] Failed to stop instance "
                            << instanceId << ": " << e.what() << std::endl;
                } catch (...) {
                  std::cerr << "[RECOVERY] Failed to stop instance "
                            << instanceId << " (unknown error)" << std::endl;
                }
              }
            } catch (const std::exception &e) {
              std::cerr << "[RECOVERY] Exception stopping instance "
                        << instanceId << ": " << e.what() << std::endl;
            } catch (...) {
              std::cerr << "[RECOVERY] Unknown exception stopping instance "
                        << instanceId << std::endl;
            }
          }
          std::cerr << "[RECOVERY] All instances stopped. Application will "
                       "continue running."
                    << std::endl;
          std::cerr
              << "[RECOVERY] Please check logs above and fix the root cause:"
              << std::endl;
          std::cerr << "[RECOVERY]   1. Ensure video has consistent resolution "
                       "(re-encode if needed)"
                    << std::endl;
          std::cerr << "[RECOVERY]   2. Use YuNet 2023mar model (better "
                       "dynamic input support)"
                    << std::endl;
          std::cerr << "[RECOVERY]   3. Try different RESIZE_RATIO values"
                    << std::endl;
        } catch (const std::exception &e) {
          std::cerr << "[RECOVERY] Error accessing instance registry: "
                    << e.what() << std::endl;
        } catch (...) {
          std::cerr
              << "[RECOVERY] Error accessing instance registry (unknown error)"
              << std::endl;
        }
      }
      // Mark cleanup as completed to prevent abort
      g_cleanup_completed.store(true);
      // Reset cleanup flag to allow recovery
      g_cleanup_in_progress.store(false);
    } else {
      std::cerr << "[RECOVERY] Cleanup already in progress (from SIGABRT "
                   "handler), skipping..."
                << std::endl;
      // Wait a bit to see if cleanup completes
      for (int i = 0; i < 10 && g_cleanup_in_progress.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (!g_cleanup_in_progress.load()) {
        g_cleanup_completed.store(true);
      }
    }

    // CRITICAL FIX: According to C++ standard, terminate handler MUST terminate
    // We cannot return from terminate handler - this causes undefined behavior
    // Attempt cleanup with timeout, then terminate
    std::cerr << "[CRITICAL] OpenCV DNN shape mismatch detected - terminating "
                 "after cleanup"
              << std::endl;
    std::cerr
        << "[CRITICAL] Instance has been stopped. Please fix the root cause:"
        << std::endl;
    std::cerr << "[CRITICAL]   1. Ensure video has consistent resolution "
                 "(re-encode if needed)"
              << std::endl;
    std::cerr << "[CRITICAL]   2. Use YuNet 2023mar model (better dynamic "
                 "input support)"
              << std::endl;
    std::cerr << "[CRITICAL]   3. Try different RESIZE_RATIO values"
              << std::endl;

    // Wait maximum 1 second for cleanup to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // CRITICAL: Must terminate according to C++ standard
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(1); // Exit immediately without calling destructors (safer in
                   // corrupted state)
  }

  // For other exceptions, try to stop instances gracefully
  // BUT: Be very careful to avoid deadlock - if we're already in stopInstance,
  // don't call it again
  bool expected = false;
  if (g_cleanup_in_progress.compare_exchange_strong(expected, true)) {
    if (g_instance_registry) {
      try {
        // Use a timeout or non-blocking approach to avoid deadlock
        // If listInstances() fails (e.g., due to deadlock), just skip cleanup
        std::vector<std::string> instances;
        try {
          instances = g_instance_registry->listInstances();
        } catch (const std::exception &e) {
          std::cerr << "[CRITICAL] Cannot list instances (possible deadlock): "
                    << e.what() << std::endl;
          std::cerr << "[CRITICAL] Skipping instance cleanup to avoid deadlock"
                    << std::endl;
          instances.clear(); // Skip cleanup
        } catch (...) {
          std::cerr << "[CRITICAL] Cannot list instances (unknown error) - "
                       "skipping cleanup"
                    << std::endl;
          instances.clear(); // Skip cleanup
        }

        // Only try to stop instances if we successfully got the list
        // Use async with timeout to prevent blocking if stopInstance() is stuck
        for (const auto &instanceId : instances) {
          try {
            // Use async with timeout to prevent deadlock if stopInstance() is
            // blocked
            auto future =
                std::async(std::launch::async, [instanceId]() -> bool {
                  try {
                    if (g_instance_registry) {
                      g_instance_registry->stopInstance(instanceId);
                      return true;
                    }
                    return false;
                  } catch (...) {
                    return false;
                  }
                });

            // Wait with timeout (500ms) - if timeout, skip this instance to
            // avoid deadlock
            auto status = future.wait_for(std::chrono::milliseconds(500));
            if (status == std::future_status::timeout) {
              std::cerr << "[CRITICAL] Timeout stopping instance " << instanceId
                        << " (500ms) - skipping to avoid deadlock" << std::endl;
              // Continue with other instances
            } else if (status == std::future_status::ready) {
              try {
                future.get(); // Get result (may throw)
              } catch (const std::exception &e) {
                std::cerr << "[CRITICAL] Failed to stop instance " << instanceId
                          << ": " << e.what() << std::endl;
              } catch (...) {
                std::cerr << "[CRITICAL] Failed to stop instance " << instanceId
                          << " (unknown error)" << std::endl;
              }
            }
          } catch (const std::exception &e) {
            std::cerr << "[CRITICAL] Exception stopping instance " << instanceId
                      << ": " << e.what() << std::endl;
            // Continue with other instances
          } catch (...) {
            std::cerr << "[CRITICAL] Unknown exception stopping instance "
                      << instanceId << std::endl;
            // Continue with other instances
          }
        }
      } catch (const std::exception &e) {
        std::cerr << "[CRITICAL] Error during cleanup: " << e.what()
                  << std::endl;
        // Don't crash - just log and continue to exit
      } catch (...) {
        std::cerr << "[CRITICAL] Unknown error during cleanup" << std::endl;
        // Don't crash - just log and continue to exit
      }
    }
    // Mark cleanup as completed to prevent abort
    g_cleanup_completed.store(true);
    g_cleanup_in_progress.store(false);
  } else {
    // Cleanup already in progress - this means we're being called recursively
    // This can happen if an exception occurs during cleanup (e.g., "Resource
    // deadlock avoided") OR if SIGABRT handler already started cleanup In this
    // case, wait for cleanup to finish and then return (don't abort)
    std::cerr << "[CRITICAL] Cleanup already in progress - exception occurred "
                 "during cleanup"
              << std::endl;
    std::cerr << "[CRITICAL] Exception: " << error_msg << std::endl;
    std::cerr << "[CRITICAL] This may be due to assertion failure - SIGABRT "
                 "handler is cleaning up"
              << std::endl;
    std::cerr << "[CRITICAL] Waiting for original cleanup to complete..."
              << std::endl;

    // Wait a bit for cleanup to complete (but don't wait forever)
    // After 2 seconds, give up and return (don't abort - let SIGABRT handler
    // finish)
    for (int i = 0; i < 20 && g_cleanup_in_progress.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Check if cleanup completed
    if (g_cleanup_completed.load() || !g_cleanup_in_progress.load()) {
      std::cerr << "[CRITICAL] Cleanup completed - terminating according to "
                   "C++ standard"
                << std::endl;
    } else {
      std::cerr << "[CRITICAL] Cleanup taking too long - terminating anyway"
                << std::endl;
    }

    // CRITICAL: Must terminate according to C++ standard
    // Cannot return from terminate handler
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(1);
  }

  // CRITICAL: Must terminate according to C++ standard
  // Check if cleanup was already done by SIGABRT handler
  if (g_cleanup_completed.load()) {
    std::cerr << "[CRITICAL] Cleanup already completed by SIGABRT handler - "
                 "terminating"
              << std::endl;
  }

  if (error_msg.find("Resource deadlock avoided") != std::string::npos) {
    // This is likely from a deadlock due to queue full or mutex lock
    // The SIGABRT handler should have already attempted recovery
    std::cerr << "[CRITICAL] Deadlock detected - likely due to queue full or "
                 "mutex lock"
              << std::endl;
    std::cerr << "[CRITICAL] This usually happens when:" << std::endl;
    std::cerr << "[CRITICAL]   - Queue is full and threads are blocked"
              << std::endl;
    std::cerr << "[CRITICAL]   - MQTT publish is too slow or network is slow"
              << std::endl;
    std::cerr << "[CRITICAL]   - Processing cannot keep up with frame rate"
              << std::endl;
    std::cerr << "[CRITICAL] Terminating after recovery attempt" << std::endl;
  }

  std::cerr << "[CRITICAL] Terminating application due to uncaught exception"
            << std::endl;
  std::cerr << "[CRITICAL] Exception: " << error_msg << std::endl;
  std::_Exit(1); // Exit immediately without calling destructors (safer in
                 // terminate handler)
}

// Recovery callback for watchdog
void recoveryAction() {
  PLOG_ERROR << "[Recovery] Application detected as unresponsive. Attempting "
                "recovery...";
  // In production, you might want to:
  // - Restart specific components
  // - Clear caches
  // - Reconnect to external services
  // - Log critical error
  // For now, we just log the event
}

/**
 * @brief Parse command line arguments
 * @param argc Argument count
 * @param argv Argument vector
 * @return true if parsing successful, false if help requested
 */
bool parseArguments(int argc, char *argv[]) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--debug" || arg == "-d") {
      g_debug_mode = true;
      std::cerr
          << "[Main] Debug mode enabled - analysis board will be displayed"
          << std::endl;
    } else if (arg == "--log-api" || arg == "--debug-api") {
      g_log_api = true;
      std::cerr << "[Main] API logging enabled" << std::endl;
    } else if (arg == "--log-instance" || arg == "--debug-instance") {
      g_log_instance = true;
      std::cerr << "[Main] Instance execution logging enabled" << std::endl;
    } else if (arg == "--log-sdk-output" || arg == "--debug-sdk-output") {
      g_log_sdk_output = true;
      std::cerr << "[Main] SDK output logging enabled" << std::endl;
    } else if (arg == "--help" || arg == "-h") {
      std::cerr << "Usage: " << argv[0] << " [OPTIONS]" << std::endl;
      std::cerr << "Options:" << std::endl;
      std::cerr << "  --debug, -d                    Enable debug mode "
                   "(display analysis board)"
                << std::endl;
      std::cerr << "  --log-api, --debug-api              Enable API "
                   "request/response logging"
                << std::endl;
      std::cerr << "  --log-instance, --debug-instance     Enable instance "
                   "execution logging (start/stop/status)"
                << std::endl;
      std::cerr << "  --log-sdk-output, --debug-sdk-output    Enable SDK "
                   "output logging (when SDK returns results)"
                << std::endl;
      std::cerr << "  --help, -h                     Show this help message"
                << std::endl;
      return false;
    } else {
      std::cerr << "Unknown option: " << arg << std::endl;
      std::cerr << "Use --help for usage information" << std::endl;
      return false;
    }
  }
  return true;
}

/**
 * @brief Check if display is available and actually working
 */
static bool has_display() {
#if defined(_WIN32)
  return true;
#else
  const char *display = std::getenv("DISPLAY");
  const char *wayland = std::getenv("WAYLAND_DISPLAY");

  // Check if DISPLAY or WAYLAND_DISPLAY is set
  if (!display && !wayland) {
    return false;
  }

  // Try to verify X server is actually running (for X11)
  if (display && display[0] != '\0') {
    // Check if X11 socket exists
    std::string displayStr(display);
    if (displayStr[0] == ':') {
      std::string socketPath = "/tmp/.X11-unix/X" + displayStr.substr(1);
      // Check if socket file exists
      std::ifstream socketFile(socketPath);
      if (!socketFile.good()) {
        // Socket doesn't exist - X server is not running
        return false;
      }
    }

    // Try to test X server connection using xdpyinfo if available
    // This is a more reliable check
    std::string testCmd = "timeout 1 xdpyinfo -display " +
                          std::string(display) + " >/dev/null 2>&1";
    int status = std::system(testCmd.c_str());
    if (status != 0) {
      // xdpyinfo failed or not available - assume display is not accessible
      // Note: If xdpyinfo is not installed, this will fail but we'll still try
      // The actual Qt connection test will catch the real error
      return false;
    }
  }

  return true;
#endif
}

/**
 * @brief Thread function to run analysis board display (blocking call)
 */
void runAnalysisBoardDisplay(
    const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>>
        &sourceNodes) {
// Set thread name for debugging
#ifdef __GLIBC__
  pthread_setname_np(pthread_self(), "analysis-board");
#endif

  // Note: We can't easily catch SIGABRT in a thread-specific way
  // Qt will abort if display is not accessible, which will trigger global
  // SIGABRT handler The best we can do is catch exceptions during board
  // creation

  // Mark thread as running
  g_analysis_board_running = true;

  try {
    PLOG_INFO << "[Debug] Creating analysis board for " << sourceNodes.size()
              << " running instance(s)";

    // Check if analysis board is disabled (due to previous Qt abort)
    if (g_analysis_board_disabled.load()) {
      PLOG_WARNING << "[Debug] Analysis board is disabled (Qt display error "
                      "detected previously)";
      g_analysis_board_running = false;
      return;
    }

    // Double-check display before creating board
    if (!has_display()) {
      PLOG_WARNING
          << "[Debug] Display not available when creating board - skipping";
      g_analysis_board_running = false;
      return;
    }

    // Try to create board - this may throw if display is not accessible
    std::unique_ptr<cvedix_utils::cvedix_analysis_board> board;
    try {
      board =
          std::make_unique<cvedix_utils::cvedix_analysis_board>(sourceNodes);
      PLOG_INFO << "[Debug] Analysis board created successfully";
    } catch (const std::exception &e) {
      std::cerr << "[Debug] Failed to create analysis board: " << e.what()
                << std::endl;
      PLOG_ERROR << "[Debug] Failed to create analysis board: " << e.what();
      PLOG_WARNING << "[Debug] This usually means X server is not accessible. "
                      "Analysis board disabled.";
      g_analysis_board_disabled = true; // Disable permanently to prevent retry
      g_analysis_board_running = false;
      return;
    } catch (...) {
      std::cerr << "[Debug] Unknown error creating analysis board" << std::endl;
      PLOG_ERROR << "[Debug] Unknown error creating analysis board";
      g_analysis_board_disabled = true; // Disable permanently to prevent retry
      g_analysis_board_running = false;
      return;
    }

    // CRITICAL: Do NOT call board.display() if display is not accessible
    // Qt will abort if it cannot connect to display, and we can't catch that
    // abort reliably So we must verify display is accessible BEFORE calling
    // display()

    // Final check: Test X server connection before calling display()
    const char *display = std::getenv("DISPLAY");
    if (display && display[0] != '\0') {
      // Try to test X server connection using xdpyinfo
      std::string testCmd = "timeout 1 xdpyinfo -display " +
                            std::string(display) + " >/dev/null 2>&1";
      int status = std::system(testCmd.c_str());
      if (status != 0) {
        PLOG_WARNING << "[Debug] X server connection test failed - not calling "
                        "board.display()";
        PLOG_WARNING
            << "[Debug] This prevents Qt abort. Analysis board disabled.";
        g_analysis_board_disabled = true;
        g_analysis_board_running = false;
        return;
      }
      PLOG_INFO << "[Debug] X server connection test passed";
    }

    // If we reach here, display is verified to be accessible
    // Now we can safely call board.display()
    PLOG_INFO << "[Debug] Starting analysis board display (blocking call)";
    PLOG_INFO << "[Debug] Display verified - calling board.display()";

    // Try to display - this should work now since we verified display
    try {
      // display() refreshes every 1 second and doesn't auto-close
      // This is a blocking call that should run until shutdown
      board->display(1, false);

      // If we reach here, display() returned (shouldn't happen for blocking
      // call)
      PLOG_WARNING << "[Debug] Analysis board display() returned unexpectedly";
      g_analysis_board_disabled = true;
    } catch (const std::exception &e) {
      std::cerr << "[Debug] Exception displaying analysis board: " << e.what()
                << std::endl;
      PLOG_ERROR << "[Debug] Exception displaying analysis board: " << e.what();
      g_analysis_board_disabled = true;
    } catch (...) {
      std::cerr << "[Debug] Unknown exception displaying analysis board"
                << std::endl;
      PLOG_ERROR << "[Debug] Unknown exception displaying analysis board";
      g_analysis_board_disabled = true;
    }
  } catch (const std::exception &e) {
    std::cerr << "[Debug] Error in analysis board thread: " << e.what()
              << std::endl;
    PLOG_ERROR << "[Debug] Error in analysis board thread: " << e.what();
    // Don't rethrow - just log and exit thread
  } catch (...) {
    std::cerr << "[Debug] Unknown error in analysis board thread" << std::endl;
    PLOG_ERROR << "[Debug] Unknown error in analysis board thread";
    // Don't rethrow - just log and exit thread
  }

  // Mark thread as stopped
  g_analysis_board_running = false;
  PLOG_INFO << "[Debug] Analysis board display thread stopped";
}

/**
 * @brief Debug thread function to display analysis board for running instances
 */
void debugAnalysisBoardThread() {
  if (!g_instance_registry) {
    return;
  }

  // Check if display is available
  bool display_available = has_display();
  if (!display_available) {
    PLOG_WARNING << "[Debug] DISPLAY/WAYLAND not found. Analysis board "
                    "requires display to work.";
    PLOG_WARNING << "[Debug] Analysis board will be disabled. Set DISPLAY or "
                    "WAYLAND_DISPLAY environment variable to enable.";
    PLOG_WARNING << "[Debug] Example: export DISPLAY=:0 before running server";
    // Keep thread running but don't try to display board
    while (!g_shutdown && g_debug_mode.load()) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    return;
  }

  PLOG_INFO << "[Debug] Analysis board thread started (display available)";

  size_t lastSourceNodeCount = 0;

  while (!g_shutdown && g_debug_mode.load()) {
    try {
      // Get source nodes from all running instances
      auto sourceNodes =
          g_instance_registry->getSourceNodesFromRunningInstances();

      // If we have source nodes, create and display analysis board
      if (!sourceNodes.empty()) {
        // Check if analysis board is disabled
        if (g_analysis_board_disabled.load()) {
          // Analysis board is disabled - just sleep and check again
          std::this_thread::sleep_for(std::chrono::seconds(5));
          continue;
        }

        // Check if we need to create/update the board
        bool needUpdate = false;

        if (sourceNodes.size() != lastSourceNodeCount) {
          needUpdate = true;
          PLOG_INFO << "[Debug] Instance count changed: " << lastSourceNodeCount
                    << " -> " << sourceNodes.size();
        }

        // Check if board thread is actually running (using atomic flag)
        bool threadRunning = g_analysis_board_running.load();

        // Only create new thread if not running and we have instances
        if (!threadRunning) {
          // Create new board with current source nodes
          auto sourceNodesCopy = sourceNodes; // Copy for thread
          g_stop_analysis_board = false;
          g_analysis_board_display_thread = std::make_unique<std::thread>(
              runAnalysisBoardDisplay, sourceNodesCopy);
          g_analysis_board_display_thread
              ->detach(); // Detach so it runs independently

          lastSourceNodeCount = sourceNodes.size();
          PLOG_INFO << "[Debug] Analysis board display thread started";

          // Wait a bit for thread to initialize and set running flag
          std::this_thread::sleep_for(std::chrono::milliseconds(500));

          // Check if thread is still running after initialization
          // Wait a bit longer to see if thread exits due to Qt abort
          std::this_thread::sleep_for(std::chrono::milliseconds(2000));

          if (!g_analysis_board_running.load()) {
            PLOG_WARNING << "[Debug] Analysis board thread exited early "
                            "(likely Qt display error)";
            PLOG_WARNING << "[Debug] Analysis board disabled - Qt cannot "
                            "connect to display";
            PLOG_WARNING << "[Debug] To enable: ensure X server is running and "
                            "DISPLAY is set correctly";
            // Disable analysis board permanently to prevent retry
            g_analysis_board_disabled = true;
            g_analysis_board_running.store(
                true); // Set to true to prevent retry
            lastSourceNodeCount =
                sourceNodes.size(); // Update count to prevent retry
          } else {
            PLOG_INFO
                << "[Debug] Analysis board thread is running successfully";
          }
        } else {
          // Thread is running - just update count
          if (needUpdate) {
            PLOG_INFO << "[Debug] Analysis board already running with "
                      << lastSourceNodeCount
                      << " instance(s) - current: " << sourceNodes.size();
          }
          lastSourceNodeCount = sourceNodes.size();
        }

        // Sleep a bit before checking again
        std::this_thread::sleep_for(std::chrono::seconds(2));
      } else {
        // No running instances
        if (lastSourceNodeCount > 0) {
          PLOG_INFO << "[Debug] No running instances - waiting for instances "
                       "to start...";
          lastSourceNodeCount = 0;
        }

        // Stop board display thread if running
        // Note: board.display() is blocking, so we can't easily stop it
        // It will stop when instances are stopped or when Qt error occurs
        // Just reset the flag - thread will set g_analysis_board_running =
        // false when it exits
        if (g_analysis_board_running.load()) {
          g_stop_analysis_board = true;
          // Thread will exit on its own when board.display() fails or instances
          // stop
        }
        g_analysis_board_display_thread.reset();

        // Sleep a bit before checking again
        std::this_thread::sleep_for(std::chrono::seconds(2));
      }
    } catch (const std::exception &e) {
      std::cerr << "[Debug] Error in analysis board thread: " << e.what()
                << std::endl;
      PLOG_ERROR << "[Debug] Error in analysis board thread: " << e.what();
      std::this_thread::sleep_for(std::chrono::seconds(5));
    } catch (...) {
      std::cerr << "[Debug] Unknown error in analysis board thread"
                << std::endl;
      PLOG_ERROR << "[Debug] Unknown error in analysis board thread";
      std::this_thread::sleep_for(std::chrono::seconds(5));
    }
  }

  // Cleanup: stop board display thread
  if (g_analysis_board_display_thread &&
      g_analysis_board_display_thread->joinable()) {
    g_stop_analysis_board = true;
    g_analysis_board_display_thread.reset();
  }

  PLOG_INFO << "[Debug] Analysis board thread stopped";
}

/**
 * @brief Auto-start instances with autoStart flag in a separate thread
 * This function runs in a separate thread to avoid blocking the main program
 * if instances fail to start, hang, or crash.
 */
void autoStartInstances(IInstanceManager *instanceManager) {
// Set thread name for debugging
#ifdef __GLIBC__
  pthread_setname_np(pthread_self(), "auto-start");
#endif

  try {
    PLOG_INFO << "[AutoStart] Thread started - checking for instances with "
                 "autoStart flag...";

    // Get all instances (with error handling)
    std::vector<InstanceInfo> instancesToCheck;
    try {
      instancesToCheck = instanceManager->getAllInstances();
    } catch (const std::exception &e) {
      PLOG_ERROR << "[AutoStart] Failed to get instances list: " << e.what();
      return;
    } catch (...) {
      PLOG_ERROR << "[AutoStart] Failed to get instances list (unknown error)";
      return;
    }

    // Filter instances with autoStart flag
    std::vector<std::pair<std::string, InstanceInfo>> instancesToStart;
    for (const auto &info : instancesToCheck) {
      if (info.autoStart) {
        instancesToStart.push_back({info.instanceId, info});
      }
    }

    if (instancesToStart.empty()) {
      PLOG_INFO << "[AutoStart] No instances with autoStart flag found";
      return;
    }

    PLOG_INFO << "[AutoStart] Found " << instancesToStart.size()
              << " instance(s) to auto-start";

    int autoStartSuccessCount = 0;
    int autoStartFailedCount = 0;

    // Start each instance in sequence (with timeout protection)
    for (const auto &[instanceId, info] : instancesToStart) {
      // Check if shutdown was requested
      if (g_shutdown || g_force_exit.load()) {
        PLOG_INFO
            << "[AutoStart] Shutdown requested, stopping auto-start process";
        break;
      }

      PLOG_INFO << "[AutoStart] Auto-starting instance: " << instanceId << " ("
                << info.displayName << ")";

      // Start instance with timeout protection using async
      try {
        auto future = std::async(
            std::launch::async, [instanceManager, instanceId]() -> bool {
              try {
                return instanceManager->startInstance(instanceId);
              } catch (const std::exception &e) {
                PLOG_ERROR << "[AutoStart] Exception starting instance "
                           << instanceId << ": " << e.what();
                return false;
              } catch (...) {
                PLOG_ERROR << "[AutoStart] Unknown exception starting instance "
                           << instanceId;
                return false;
              }
            });

        // Wait with timeout (30 seconds per instance)
        auto status = future.wait_for(std::chrono::seconds(30));
        if (status == std::future_status::timeout) {
          PLOG_WARNING << "[AutoStart] ✗ Timeout starting instance: "
                       << instanceId << " (30s timeout)";
          PLOG_WARNING << "[AutoStart] Instance may be hanging - you can start "
                          "it manually later";
          autoStartFailedCount++;
        } else if (status == std::future_status::ready) {
          try {
            bool success = future.get();
            if (success) {
              autoStartSuccessCount++;
              PLOG_INFO << "[AutoStart] ✓ Successfully auto-started instance: "
                        << instanceId;
            } else {
              autoStartFailedCount++;
              PLOG_WARNING << "[AutoStart] ✗ Failed to auto-start instance: "
                           << instanceId;
              PLOG_WARNING << "[AutoStart] Instance created but not started - "
                              "you can start it manually later";
            }
          } catch (const std::exception &e) {
            autoStartFailedCount++;
            PLOG_ERROR << "[AutoStart] ✗ Exception getting result for instance "
                       << instanceId << ": " << e.what();
          } catch (...) {
            autoStartFailedCount++;
            PLOG_ERROR << "[AutoStart] ✗ Unknown exception getting result for "
                          "instance "
                       << instanceId;
          }
        }
      } catch (const std::exception &e) {
        autoStartFailedCount++;
        PLOG_ERROR
            << "[AutoStart] ✗ Exception creating async task for instance "
            << instanceId << ": " << e.what();
      } catch (...) {
        autoStartFailedCount++;
        PLOG_ERROR << "[AutoStart] ✗ Unknown exception creating async task for "
                      "instance "
                   << instanceId;
      }

      // Small delay between instances to avoid overwhelming the system
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Summary
    int totalCount = instancesToStart.size();
    PLOG_INFO << "[AutoStart] Auto-start summary: " << autoStartSuccessCount
              << "/" << totalCount << " instances started successfully";
    if (autoStartFailedCount > 0) {
      PLOG_WARNING
          << "[AutoStart] " << autoStartFailedCount
          << " instance(s) failed to start - check logs above for details";
      PLOG_WARNING << "[AutoStart] Failed instances can be started manually "
                      "using the startInstance API";
    }
  } catch (const std::exception &e) {
    PLOG_ERROR << "[AutoStart] Fatal error in auto-start thread: " << e.what();
    PLOG_ERROR << "[AutoStart] Auto-start failed but server continues running";
  } catch (...) {
    PLOG_ERROR
        << "[AutoStart] Fatal error in auto-start thread (unknown exception)";
    PLOG_ERROR << "[AutoStart] Auto-start failed but server continues running";
  }

  PLOG_INFO << "[AutoStart] Thread finished";
}

/**
 * @brief Validate and parse port number
 */
uint16_t parsePort(const char *port_str, uint16_t default_port) {
  if (!port_str) {
    return default_port;
  }

  try {
    int port_int = std::stoi(port_str);
    if (port_int < 1 || port_int > 65535) {
      throw std::invalid_argument("Port must be between 1 and 65535");
    }
    return static_cast<uint16_t>(port_int);
  } catch (const std::invalid_argument &e) {
    // Note: Logger might not be initialized yet, so use std::cerr
    std::cerr << "Error: Invalid port number '" << port_str << "': " << e.what()
              << std::endl;
    std::cerr << "Using default port: " << default_port << std::endl;
    return default_port;
  } catch (const std::out_of_range &e) {
    // Note: Logger might not be initialized yet, so use std::cerr
    std::cerr << "Error: Port number out of range: " << e.what() << std::endl;
    std::cerr << "Using default port: " << default_port << std::endl;
    return default_port;
  }
}

/**
 * @brief Auto-detect and set GST_PLUGIN_PATH if not already set
 * This ensures GStreamer can find plugins even when running directly (not via
 * service)
 *
 * PRODUCTION NOTE: In production, GST_PLUGIN_PATH should be set in service file
 * or .env This auto-detect is only a fallback for development/testing
 * scenarios.
 *
 * @param enable_find_search If false, skip slow find command (recommended for
 * production)
 */
static void setupGStreamerPluginPath(bool enable_find_search = false) {
  // Check if GST_PLUGIN_PATH is already set
  const char *existing_path = std::getenv("GST_PLUGIN_PATH");
  if (existing_path && strlen(existing_path) > 0) {
    // In production, this should always be the case (set in service file)
    std::cerr << "[Main] GST_PLUGIN_PATH already set: " << existing_path
              << std::endl;
    return;
  }

  std::string plugin_path;

  // Method 1: Check for bundled plugins FIRST (ALL-IN-ONE package)
  // This is the most reliable for all-in-one packages
  std::vector<std::string> bundled_paths = {
      "/opt/edge_ai_api/lib/gstreamer-1.0",
      "/usr/local/edge_ai_api/lib/gstreamer-1.0"};
  
  for (const auto &path : bundled_paths) {
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path) &&
        std::filesystem::exists(path + "/libgstcoreelements.so")) {
      plugin_path = path;
      std::cerr << "[Main] ✓ Found bundled GStreamer plugins: " << plugin_path
                << std::endl;
      break;
    }
  }

  // Method 2: Try common system paths (fallback if bundled plugins not found)
  // This covers 99% of production cases (Debian/Ubuntu x86_64, ARM64, ARM32,
  // Fedora, etc.)
  if (plugin_path.empty()) {
    std::vector<std::string> common_paths = {
        "/usr/lib/x86_64-linux-gnu/gstreamer-1.0",
        "/usr/lib/aarch64-linux-gnu/gstreamer-1.0",
        "/usr/lib/arm-linux-gnueabihf/gstreamer-1.0",
        "/usr/lib64/gstreamer-1.0",
        "/usr/lib/gstreamer-1.0",
        "/usr/local/lib/gstreamer-1.0"};

    for (const auto &path : common_paths) {
      if (std::filesystem::exists(path) && std::filesystem::is_directory(path) &&
          std::filesystem::exists(path + "/libgstcoreelements.so")) {
        plugin_path = path;
        break;
      }
    }
  }

  // Method 3: Try pkg-config (only if bundled and common paths failed)
  // NOTE: This requires pkg-config to be installed and accessible
  if (plugin_path.empty()) {
    FILE *pipe = popen(
        "pkg-config --variable=pluginsdir gstreamer-1.0 2>/dev/null", "r");
    if (pipe) {
      char buffer[512];
      if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string path(buffer);
        // Remove trailing newline
        if (!path.empty() && path.back() == '\n') {
          path.pop_back();
        }
        if (!path.empty() && std::filesystem::exists(path) &&
            std::filesystem::is_directory(path)) {
          plugin_path = path;
        }
      }
      pclose(pipe);
    }
  }

  // Method 4: Try to find libgstcoreelements.so (SLOW - only for development)
  // PRODUCTION: Skip this by default (enable_find_search=false)
  // This can scan entire /usr filesystem which is slow and may fail in
  // restricted environments
  if (plugin_path.empty() && enable_find_search) {
    FILE *pipe = popen(
        "find /usr -name 'libgstcoreelements.so' 2>/dev/null | head -1", "r");
    if (pipe) {
      char buffer[512];
      if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string file_path(buffer);
        // Remove trailing newline
        if (!file_path.empty() && file_path.back() == '\n') {
          file_path.pop_back();
        }
        if (!file_path.empty()) {
          std::filesystem::path p(file_path);
          plugin_path = p.parent_path().string();
        }
      }
      pclose(pipe);
    }
  }

  // Set GST_PLUGIN_PATH if found
  if (!plugin_path.empty()) {
    // SECURITY: Validate plugin_path to prevent command injection
    // Only allow safe characters: alphanumeric, forward slash, dash, underscore, dot
    bool is_safe_path = true;
    for (char c : plugin_path) {
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
            (c >= '0' && c <= '9') || c == '/' || c == '-' || 
            c == '_' || c == '.' || c == ':')) {
        is_safe_path = false;
        break;
      }
    }
    
    if (!is_safe_path) {
      std::cerr << "[Main] ⚠ Detected unsafe characters in plugin_path, "
                   "skipping GStreamer registry update for security"
                << std::endl;
      return;
    }
    
    if (setenv("GST_PLUGIN_PATH", plugin_path.c_str(), 0) == 0) {
      std::cerr << "[Main] ✓ Auto-detected and set GST_PLUGIN_PATH: "
                << plugin_path << std::endl;
      
      // CRITICAL: Force GStreamer registry update by running gst-inspect-1.0
      // This ensures bundled plugins are discovered before OpenCV uses GStreamer
      // Note: This is a best-effort attempt - if gst-inspect-1.0 is not available,
      // GStreamer will scan plugins on first use (which may be too late for OpenCV)
      // SECURITY: Use environment variable already set via setenv() instead of
      // embedding plugin_path in shell command to prevent command injection
      int ret = system("gst-inspect-1.0 filesrc >/dev/null 2>&1");
      if (ret == 0) {
        std::cerr << "[Main] ✓ GStreamer registry updated successfully"
                  << std::endl;
      } else {
        std::cerr << "[Main] ⚠ Could not force GStreamer registry update "
                     "(gst-inspect-1.0 may not be available)"
                  << std::endl;
        std::cerr << "[Main]   Registry will be updated on first GStreamer use"
                  << std::endl;
      }
      
      std::cerr << "[Main]   NOTE: For production, set GST_PLUGIN_PATH in "
                   "service file or .env"
                << std::endl;
    } else {
      std::cerr << "[Main] ⚠ Failed to set GST_PLUGIN_PATH" << std::endl;
    }
  } else {
    std::cerr << "[Main] ⚠ Could not auto-detect GST_PLUGIN_PATH. "
                 "GStreamer plugins may not be found."
              << std::endl;
    std::cerr << "[Main]   For production: Set GST_PLUGIN_PATH in service file "
                 "or .env file"
              << std::endl;
    std::cerr << "[Main]   For development: export "
                 "GST_PLUGIN_PATH=/path/to/gstreamer-1.0"
              << std::endl;
  }
}

int main(int argc, char *argv[]) {
  try {
    // CRITICAL: Disable problematic GStreamer plugins FIRST, before anything
    // else VA plugin (libgstva.so) can crash on systems with broken GPU drivers
    // (nouveau, etc.) Must be done BEFORE GStreamer initializes (which happens
    // when CVEDIX SDK loads)

    // Method 1: Set rank to NONE - must be done before GStreamer init
    const char *existing_rank = std::getenv("GST_PLUGIN_FEATURE_RANK");
    std::string rank_env = existing_rank ? existing_rank : "";

    // Add VA plugin blacklist if not already present
    if (rank_env.find("va:") == std::string::npos &&
        rank_env.find("vaapi:") == std::string::npos &&
        rank_env.find("vaapidecode:") == std::string::npos &&
        rank_env.find("vaapipostproc:") == std::string::npos) {
      // Set rank to NONE (0) to disable VA plugins
      if (!rank_env.empty()) {
        rank_env += ",";
      }
      rank_env += "va:NONE,vaapi:NONE,vaapidecode:NONE,vaapipostproc:NONE";
      setenv("GST_PLUGIN_FEATURE_RANK", rank_env.c_str(), 0);
    }

    // Method 2: Rename problematic plugin file to prevent loading
    // This is more reliable - GStreamer won't find the file even if rank
    // setting fails
    const char *plugin_path_env = std::getenv("GST_PLUGIN_PATH");
    std::string plugin_dir = plugin_path_env
                                 ? plugin_path_env
                                 : "/usr/lib/x86_64-linux-gnu/gstreamer-1.0";

    std::string va_plugin_path = plugin_dir + "/libgstva.so";
    std::string va_plugin_disabled = plugin_dir + "/libgstva.so.disabled";

    if (std::filesystem::exists(va_plugin_path) &&
        !std::filesystem::exists(va_plugin_disabled)) {
      try {
        std::filesystem::rename(va_plugin_path, va_plugin_disabled);
        std::cerr << "[Main] ✓ Disabled VA plugin by renaming: "
                  << va_plugin_path << " -> " << va_plugin_disabled
                  << std::endl;
        std::cerr << "[Main]   This prevents crashes on systems with broken "
                     "GPU drivers (nouveau, etc.)"
                  << std::endl;
      } catch (const std::exception &e) {
        // If rename fails (permission denied, etc.), just log warning
        std::cerr << "[Main] ⚠ Could not rename VA plugin (may need root): "
                  << e.what() << std::endl;
        std::cerr << "[Main]   VA plugin may still cause crashes. Consider: "
                  << "sudo mv " << va_plugin_path << " " << va_plugin_disabled
                  << std::endl;
        std::cerr << "[Main]   Or set in service file: "
                     "GST_PLUGIN_FEATURE_RANK=va:NONE,vaapi:NONE,..."
                  << std::endl;
      }
    } else if (std::filesystem::exists(va_plugin_disabled)) {
      std::cerr << "[Main] ✓ VA plugin already disabled: " << va_plugin_disabled
                << std::endl;
    }

    // Parse command line arguments
    if (!parseArguments(argc, argv)) {
      return 0; // Help was requested, exit normally
    }

    // CRITICAL: Setup GStreamer plugin path BEFORE any GStreamer operations
    // This ensures plugins can be found even when running directly (not via
    // service) PRODUCTION: In production, GST_PLUGIN_PATH should be set in
    // service file or .env This auto-detect is only a fallback. Skip slow find
    // search for production.
    bool enable_find_search = g_debug_mode.load(); // Only enable in debug mode
    setupGStreamerPluginPath(enable_find_search);

    // Log rank setting if applied
    if (!rank_env.empty() && rank_env.find("va:NONE") != std::string::npos) {
      std::cerr << "[Main] ✓ Set GST_PLUGIN_FEATURE_RANK to disable VA plugins"
                << std::endl;
    }

    // Initialize categorized logger first (before any logging)
    // This sets up log directories, daily rotation, and cleanup
    CategorizedLogger::init();

    PLOG_INFO << "========================================";
    PLOG_INFO << "Edge AI API Server";
    PLOG_INFO << "========================================";
    if (g_debug_mode.load()) {
      PLOG_INFO << "Debug mode: ENABLED";
    }
    if (g_log_api.load()) {
      PLOG_INFO << "API logging: ENABLED";
    }
    if (g_log_instance.load()) {
      PLOG_INFO << "Instance execution logging: ENABLED";
    }
    if (g_log_sdk_output.load()) {
      PLOG_INFO << "SDK output logging: ENABLED";
    }
    PLOG_INFO << "Starting REST API server...";

    // Check GStreamer plugins availability
    std::cerr << "\n[Main] Checking GStreamer plugins..." << std::endl;
    bool pluginsOk = GStreamerChecker::validatePlugins(true);
    if (!pluginsOk) {
      std::cerr
          << "[Main] ⚠ WARNING: Some required GStreamer plugins are missing!"
          << std::endl;
      std::cerr << "[Main] The application will continue, but some features "
                   "may not work."
                << std::endl;
      std::cerr << "[Main] Please install missing plugins before using "
                   "RTSP/RTMP/File source nodes.\n"
                << std::endl;
    } else {
      std::cerr << "[Main] ✓ All required GStreamer plugins are available\n"
                << std::endl;
    }

    // Register signal handlers for graceful shutdown and crash prevention
    // CRITICAL: Register BEFORE Drogon initializes to ensure our handler is
    // used Drogon may register its own handlers, but ours should take
    // precedence
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGABRT, signalHandler); // Catch assertion failures (like
                                         // OpenCV DNN shape mismatch)

    // Register terminate handler for uncaught exceptions
    std::set_terminate(terminateHandler);

    // Load system configuration first (needed for web_server config)
    // Use intelligent path resolution with 3-tier fallback
    std::string configPath = EnvConfig::resolveConfigPath();

    auto &systemConfig = SystemConfig::getInstance();
    systemConfig.loadConfig(configPath);

    // Get server configuration (config.json with env var override handled by
    // SystemConfig)
    auto webServerConfig = systemConfig.getWebServerConfig();
    std::string host = webServerConfig.ipAddress;
    int port = static_cast<int>(webServerConfig.port); // Use int for port retry logic
    int original_port = port; // Store original for comparison

    PLOG_INFO << "Server will attempt to listen on: " << host << ":" << port;
    PLOG_INFO << "Available endpoints:";
    PLOG_INFO << "  GET /v1/core/health  - Health check";
    PLOG_INFO << "  GET /v1/core/version - Version information";
    PLOG_INFO << "  POST /v1/core/instance - Create new instance";
    PLOG_INFO << "  GET /v1/core/instance - List all instances";
    PLOG_INFO << "  GET /v1/core/instance/{id} - Get instance details";
    PLOG_INFO << "  POST /v1/core/instance/{id}/input - Set input source";
    PLOG_INFO
        << "  POST /v1/core/instance/{id}/config - Set config value at path";
    PLOG_INFO << "  POST /v1/core/instance/{id}/start - Start instance";
    PLOG_INFO << "  POST /v1/core/instance/{id}/stop - Stop instance";
    PLOG_INFO << "  DELETE /v1/core/instance/{id} - Delete instance";
    PLOG_INFO << "  GET /v1/core/instance/{id}/frame - Get last frame";
    PLOG_INFO
        << "  GET /v1/core/instance/{id}/statistics - Get instance statistics";
    PLOG_INFO << "  POST /v1/core/model/upload - Upload model file";
    PLOG_INFO << "  GET /v1/core/model/list - List uploaded models";
    PLOG_INFO << "  DELETE /v1/core/model/{modelName} - Delete model file";
    PLOG_INFO << "  POST /v1/core/video/upload - Upload video file";
    PLOG_INFO << "  GET /v1/core/video/list - List uploaded videos";
    PLOG_INFO << "  DELETE /v1/core/video/{videoName} - Delete video file";
    PLOG_INFO << "  POST /v1/core/font/upload - Upload font file";
    PLOG_INFO << "  GET /v1/core/font/list - List uploaded fonts";
    PLOG_INFO << "  DELETE /v1/core/font/{fontName} - Delete font file";
    PLOG_INFO << "  GET /swagger         - Swagger UI (all versions)";
    PLOG_INFO << "  GET /v1/swagger      - Swagger UI for API v1";
    PLOG_INFO << "  GET /v2/swagger      - Swagger UI for API v2";
    PLOG_INFO << "  GET /v1/document     - Scalar API documentation for v1";
    PLOG_INFO << "  GET /openapi.yaml    - OpenAPI spec (all versions)";
    PLOG_INFO << "  GET /v1/openapi.yaml - OpenAPI spec for v1";
    PLOG_INFO << "  GET /v2/openapi.yaml - OpenAPI spec for v2";

    // Controllers are auto-registered via Drogon's HttpController system
    // when headers are included and METHOD_LIST_BEGIN/END macros are used
    // Create instances to ensure registration
    static HealthHandler healthHandler;
    static VersionHandler versionHandler;
    static WatchdogHandler watchdogHandler;
    static SwaggerHandler swaggerHandler;
    static ScalarHandler scalarHandler;
    static HlsHandler hlsHandler;
    static EndpointsHandler endpointsHandler;
    static LogHandler logHandler;
#ifdef ENABLE_SYSTEM_INFO_HANDLER
    static SystemInfoHandler systemInfoHandler;
#endif
    static LicenseHandler licenseHandler;
#ifdef ENABLE_METRICS_HANDLER
    static MetricsHandler metricsHandler;
#endif

    // Initialize instance management components
    static SolutionRegistry &solutionRegistry = SolutionRegistry::getInstance();
    static PipelineBuilder pipelineBuilder;

    // Initialize instance storage with configurable directory
    // Priority: 1. INSTANCES_DIR env var, 2. /opt/edge_ai_api/instances (with
    // auto-fallback)
    std::string instancesDir;
    const char *env_instances_dir = std::getenv("INSTANCES_DIR");
    if (env_instances_dir && strlen(env_instances_dir) > 0) {
      instancesDir = std::string(env_instances_dir);
      std::cerr << "[Main] Using INSTANCES_DIR from environment: "
                << instancesDir << std::endl;
    } else {
      // Try /opt/edge_ai_api/instances first, fallback to user directory if
      // needed
      instancesDir = "/opt/edge_ai_api/instances";
      std::cerr << "[Main] Attempting to use: " << instancesDir << std::endl;
    }

    // Try to create directory if it doesn't exist
    // Strategy: Try /opt first, if fails, auto-fallback to user directory
    bool directory_ready = false;
    if (!std::filesystem::exists(instancesDir)) {
      std::cerr << "[Main] Directory does not exist, attempting to create: "
                << instancesDir << std::endl;

      try {
        // Try to create directory (will create parent dirs if we have
        // permission)
        bool created = std::filesystem::create_directories(instancesDir);
        if (created) {
          std::cerr << "[Main] ✓ Successfully created instances directory: "
                    << instancesDir << std::endl;
          directory_ready = true;
        } else {
          // Directory might have been created by another process
          if (std::filesystem::exists(instancesDir)) {
            std::cerr << "[Main] ✓ Instances directory exists (created by "
                         "another process): "
                      << instancesDir << std::endl;
            directory_ready = true;
          }
        }
      } catch (const std::filesystem::filesystem_error &e) {
        if (e.code() == std::errc::permission_denied) {
          std::cerr << "[Main] ⚠ Cannot create " << instancesDir
                    << " (permission denied)" << std::endl;

          // Auto-fallback: Try user directory (works without sudo)
          if (env_instances_dir == nullptr || strlen(env_instances_dir) == 0) {
            const char *home = std::getenv("HOME");
            if (home) {
              std::string fallback_path =
                  std::string(home) + "/.local/share/edge_ai_api/instances";
              std::cerr << "[Main] Auto-fallback: Trying user directory: "
                        << fallback_path << std::endl;
              try {
                std::filesystem::create_directories(fallback_path);
                instancesDir = fallback_path;
                directory_ready = true;
                std::cerr << "[Main] ✓ Using fallback directory: "
                          << instancesDir << std::endl;
                std::cerr
                    << "[Main] ℹ Note: To use /opt/edge_ai_api/instances, "
                       "create parent directory:"
                    << std::endl;
                std::cerr << "[Main] ℹ   sudo mkdir -p /opt/edge_ai_api && "
                             "sudo chown $USER:$USER /opt/edge_ai_api"
                          << std::endl;
              } catch (const std::exception &fallback_e) {
                std::cerr << "[Main] ⚠ Fallback also failed: "
                          << fallback_e.what() << std::endl;
                // Last resort: current directory
                instancesDir = "./instances";
                try {
                  std::filesystem::create_directories(instancesDir);
                  directory_ready = true;
                  std::cerr
                      << "[Main] ✓ Using current directory: " << instancesDir
                      << std::endl;
                } catch (...) {
                  std::cerr
                      << "[Main] ✗ ERROR: Cannot create any instances directory"
                      << std::endl;
                }
              }
            } else {
              // No HOME, use current directory
              instancesDir = "./instances";
              try {
                std::filesystem::create_directories(instancesDir);
                directory_ready = true;
                std::cerr << "[Main] ✓ Using current directory: "
                          << instancesDir << std::endl;
              } catch (...) {
                std::cerr << "[Main] ✗ ERROR: Cannot create ./instances"
                          << std::endl;
              }
            }
          } else {
            // User specified INSTANCES_DIR but can't create it
            std::cerr
                << "[Main] ✗ ERROR: Cannot create user-specified directory: "
                << instancesDir << std::endl;
            std::cerr
                << "[Main] ✗ Please check permissions or use a different path"
                << std::endl;
          }
        } else {
          std::cerr << "[Main] ✗ ERROR creating " << instancesDir << ": "
                    << e.what() << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "[Main] ✗ Exception creating " << instancesDir << ": "
                  << e.what() << std::endl;
      }
    } else {
      // Check if it's actually a directory
      if (std::filesystem::is_directory(instancesDir)) {
        std::cerr << "[Main] ✓ Instances directory already exists: "
                  << instancesDir << std::endl;
        directory_ready = true;
      } else {
        std::cerr << "[Main] ✗ ERROR: Path exists but is not a directory: "
                  << instancesDir << std::endl;
      }
    }

    if (directory_ready) {
      std::cerr << "[Main] ✓ Instances directory is ready: " << instancesDir
                << std::endl;
    } else {
      std::cerr << "[Main] ⚠ WARNING: Instances directory may not be ready"
                << std::endl;
    }

    PLOG_INFO << "[Main] Instances directory: " << instancesDir;
    static InstanceStorage instanceStorage(instancesDir);

    // ============================================
    // EXECUTION MODE SELECTION
    // ============================================
    // Check environment variable for execution mode:
    // - EDGE_AI_EXECUTION_MODE=subprocess : Run pipelines in isolated
    // subprocesses
    // - EDGE_AI_EXECUTION_MODE=inprocess (default) : Run pipelines in main
    // process
    auto executionMode = InstanceManagerFactory::getExecutionModeFromEnv();
    std::cerr << "[Main] Execution mode: "
              << InstanceManagerFactory::getModeName(executionMode)
              << std::endl;
    PLOG_INFO << "[Main] Execution mode: "
              << InstanceManagerFactory::getModeName(executionMode);

    // Create instance registry (always needed for legacy compatibility)
    static InstanceRegistry instanceRegistry(solutionRegistry, pipelineBuilder,
                                             instanceStorage);

    // Store instance registry pointer for error recovery
    g_instance_registry = &instanceRegistry;

    // Create instance manager based on execution mode
    // Note: For subprocess mode, we still use instanceRegistry for API handlers
    // but the actual pipeline execution happens in worker processes
    static std::unique_ptr<IInstanceManager> instanceManager;
    if (executionMode == InstanceExecutionMode::SUBPROCESS) {
      // Subprocess mode: Create SubprocessInstanceManager
      // Workers will be spawned for each instance
      
      // Resolve worker executable path from environment variable or use default
      const char *worker_path_env = std::getenv("EDGE_AI_WORKER_PATH");
      std::string worker_executable;
      if (worker_path_env && strlen(worker_path_env) > 0) {
        worker_executable = std::string(worker_path_env);
        std::cerr << "[Main] Using worker executable from EDGE_AI_WORKER_PATH: "
                  << worker_executable << std::endl;
      } else {
        // Default: try absolute path first, then fallback to just executable name
        // This allows supervisor's findWorkerExecutable() to search in PATH
        worker_executable = "edge_ai_worker";
        std::cerr << "[Main] Using default worker executable: " << worker_executable
                  << " (set EDGE_AI_WORKER_PATH to override)" << std::endl;
      }
      
      instanceManager = InstanceManagerFactory::createSubprocess(
          solutionRegistry, instanceStorage, worker_executable);
      std::cerr << "[Main] ✓ Subprocess instance manager initialized"
                << std::endl;
      PLOG_INFO << "[Main] Subprocess instance manager initialized";
      PLOG_INFO << "[Main] Each instance will run in isolated worker process";
    } else {
      // In-process mode: Use existing InstanceRegistry directly
      instanceManager =
          std::make_unique<InProcessInstanceManager>(instanceRegistry);
      std::cerr
          << "[Main] ✓ In-process instance manager initialized (legacy mode)"
          << std::endl;
      PLOG_INFO
          << "[Main] In-process instance manager initialized (legacy mode)";
    }

    // Initialize default solutions (face_detection, etc.)
    solutionRegistry.initializeDefaultSolutions();

    // Initialize node pool manager and default templates
    static NodePoolManager &nodePool = NodePoolManager::getInstance();
    nodePool.initializeDefaultTemplates();
    PLOG_INFO << "[Main] Node pool manager initialized with default templates";

    // Initialize node storage and load persisted nodes
    // Priority: 1. NODES_DIR env var, 2. /opt/edge_ai_api/nodes (with
    // auto-fallback)
    std::string nodesDir;
    const char *env_nodes_dir = std::getenv("NODES_DIR");
    if (env_nodes_dir && strlen(env_nodes_dir) > 0) {
      nodesDir = std::string(env_nodes_dir);
      std::cerr << "[Main] Using NODES_DIR from environment: " << nodesDir
                << std::endl;
    } else {
      // Try /opt/edge_ai_api/nodes first, fallback to user directory if needed
      nodesDir = "/opt/edge_ai_api/nodes";
      std::cerr << "[Main] Attempting to use: " << nodesDir << std::endl;
    }

    // Try to create directory if it doesn't exist
    // Strategy: Try /opt first, if fails, auto-fallback to user directory
    bool nodes_directory_ready = false;
    if (!std::filesystem::exists(nodesDir)) {
      std::cerr
          << "[Main] Nodes directory does not exist, attempting to create: "
          << nodesDir << std::endl;

      try {
        // Try to create directory (will create parent dirs if we have
        // permission)
        bool created = std::filesystem::create_directories(nodesDir);
        if (created) {
          std::cerr << "[Main] ✓ Successfully created nodes directory: "
                    << nodesDir << std::endl;
          nodes_directory_ready = true;
        } else {
          // Directory might have been created by another process
          if (std::filesystem::exists(nodesDir)) {
            std::cerr << "[Main] ✓ Nodes directory exists (created by another "
                         "process): "
                      << nodesDir << std::endl;
            nodes_directory_ready = true;
          }
        }
      } catch (const std::filesystem::filesystem_error &e) {
        if (e.code() == std::errc::permission_denied) {
          std::cerr << "[Main] ⚠ Cannot create " << nodesDir
                    << " (permission denied)" << std::endl;

          // Auto-fallback: Try user directory (works without sudo)
          if (env_nodes_dir == nullptr || strlen(env_nodes_dir) == 0) {
            const char *home = std::getenv("HOME");
            if (home) {
              std::string fallback_path =
                  std::string(home) + "/.local/share/edge_ai_api/nodes";
              std::cerr << "[Main] Auto-fallback: Trying user directory: "
                        << fallback_path << std::endl;
              try {
                std::filesystem::create_directories(fallback_path);
                nodesDir = fallback_path;
                nodes_directory_ready = true;
                std::cerr << "[Main] ✓ Using fallback directory: " << nodesDir
                          << std::endl;
                std::cerr << "[Main] ℹ Note: To use /opt/edge_ai_api/nodes, "
                             "create parent directory:"
                          << std::endl;
                std::cerr << "[Main] ℹ   sudo mkdir -p /opt/edge_ai_api && "
                             "sudo chown $USER:$USER /opt/edge_ai_api"
                          << std::endl;
              } catch (const std::exception &fallback_e) {
                std::cerr << "[Main] ⚠ Fallback also failed: "
                          << fallback_e.what() << std::endl;
                // Last resort: current directory
                nodesDir = "./nodes";
                try {
                  std::filesystem::create_directories(nodesDir);
                  nodes_directory_ready = true;
                  std::cerr << "[Main] ✓ Using current directory: " << nodesDir
                            << std::endl;
                } catch (...) {
                  std::cerr
                      << "[Main] ✗ ERROR: Cannot create any nodes directory"
                      << std::endl;
                }
              }
            } else {
              // No HOME env var, use current directory
              nodesDir = "./nodes";
              try {
                std::filesystem::create_directories(nodesDir);
                nodes_directory_ready = true;
                std::cerr << "[Main] ✓ Using current directory: " << nodesDir
                          << std::endl;
              } catch (...) {
                std::cerr << "[Main] ✗ ERROR: Cannot create nodes directory"
                          << std::endl;
              }
            }
          }
        } else {
          std::cerr << "[Main] ✗ Exception creating " << nodesDir << ": "
                    << e.what() << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "[Main] ✗ Exception creating " << nodesDir << ": "
                  << e.what() << std::endl;
      }
    } else {
      // Check if it's actually a directory
      if (std::filesystem::is_directory(nodesDir)) {
        std::cerr << "[Main] ✓ Nodes directory already exists: " << nodesDir
                  << std::endl;
        nodes_directory_ready = true;
      } else {
        std::cerr << "[Main] ✗ ERROR: Path exists but is not a directory: "
                  << nodesDir << std::endl;
      }
    }

    if (nodes_directory_ready) {
      std::cerr << "[Main] ✓ Nodes directory is ready: " << nodesDir
                << std::endl;
    } else {
      std::cerr << "[Main] ⚠ WARNING: Nodes directory may not be ready"
                << std::endl;
    }

    PLOG_INFO << "[Main] Nodes directory: " << nodesDir;
    static NodeStorage nodeStorage(nodesDir);

    // Step 1: Load persisted nodes from storage (if any)
    size_t loadedFromStorage = nodePool.loadNodesFromStorage(nodeStorage);
    PLOG_INFO << "[Main] Loaded " << loadedFromStorage << " nodes from storage";

    // Step 2: Create default nodes from all available templates
    // This ensures all supported node types have default nodes, not just those
    // from solutions
    size_t createdFromTemplates = nodePool.createDefaultNodesFromTemplates();
    PLOG_INFO << "[Main] Created " << createdFromTemplates
              << " default nodes from templates";

    // Step 3: Also create nodes from default solutions (for nodes with specific
    // configurations) This adds nodes with solution-specific parameters
    size_t createdFromSolutions =
        nodePool.createNodesFromDefaultSolutions(solutionRegistry);
    PLOG_INFO << "[Main] Created " << createdFromSolutions
              << " nodes from default solutions";

    size_t totalCreated = createdFromTemplates + createdFromSolutions;

    // Step 4: Load user-created nodes again (in case storage was updated)
    // This ensures we have all nodes: defaults + user-created
    size_t loadedUserNodes = nodePool.loadNodesFromStorage(nodeStorage);

    // Step 5: Get total count for reporting
    auto stats = nodePool.getStats();
    std::cerr << "[Main] ========================================" << std::endl;
    std::cerr << "[Main] Node Pool Status:" << std::endl;
    std::cerr << "[Main]   Total nodes: " << stats.totalPreConfiguredNodes
              << std::endl;
    std::cerr << "[Main]   Available: " << stats.availableNodes << std::endl;
    std::cerr << "[Main]   In use: " << stats.inUseNodes << std::endl;
    std::cerr << "[Main]   Default nodes (from templates): "
              << createdFromTemplates << std::endl;
    std::cerr << "[Main]   Nodes from solutions: " << createdFromSolutions
              << std::endl;
    std::cerr << "[Main]   User-created nodes: " << loadedUserNodes
              << std::endl;
    std::cerr << "[Main] ========================================" << std::endl;
    PLOG_INFO << "[Main] Node pool initialized: "
              << stats.totalPreConfiguredNodes << " total nodes ("
              << stats.availableNodes << " available, " << stats.inUseNodes
              << " in use)";

    // Step 6: Save nodes to storage only if we created new nodes
    if (totalCreated > 0) {
      if (nodePool.saveNodesToStorage(nodeStorage)) {
        PLOG_INFO << "[Main] Saved nodes to storage (including " << totalCreated
                  << " new nodes)";
      } else {
        PLOG_WARNING << "[Main] Failed to save nodes to storage";
      }
    }

    // Initialize solution storage and load custom solutions
    // Default: /opt/edge_ai_api/solutions (auto-created if needed, with
    // fallback)
    std::string solutionsDir =
        EnvConfig::resolveDataDir("SOLUTIONS_DIR", "solutions");
    PLOG_INFO << "[Main] Solutions directory: " << solutionsDir;
    static SolutionStorage solutionStorage(solutionsDir);

    // Load persisted custom solutions
    auto customSolutions = solutionStorage.loadAllSolutions();
    for (const auto &config : customSolutions) {
      // Check if solution ID conflicts with default solution
      if (solutionRegistry.isDefaultSolution(config.solutionId)) {
        PLOG_WARNING << "[Main] Skipping custom solution '" << config.solutionId
                     << "': ID conflicts with default system solution. "
                     << "Please rename the solution to use a different ID.";
        continue;
      }

      // Register solution (registerSolution will also check for default
      // solutions)
      solutionRegistry.registerSolution(config);

      // Create nodes for node types in this custom solution (if they don't
      // exist) Note: These are user-created nodes, not default nodes
      size_t nodesCreated = nodePool.createNodesFromSolution(config);
      if (nodesCreated > 0) {
        PLOG_INFO << "[Main] Created " << nodesCreated
                  << " nodes for custom solution: " << config.solutionId;
      }

      PLOG_INFO << "[Main] Loaded custom solution: " << config.solutionId
                << " (" << config.solutionName << ")";
    }

    // Load persistent instances
    instanceManager->loadPersistentInstances();

    // ============================================
    // AUTO-START FUNCTIONALITY
    // ============================================
    // Auto-start will be scheduled to run AFTER the server starts
    // This ensures the server is ready before instances start
    // Instances will start in a separate thread to avoid blocking the main
    // program if instances fail to start, hang, or crash
    PLOG_INFO << "[Main] Auto-start will run after server is ready";

    // Register instance manager and solution registry with handlers
    CreateInstanceHandler::setInstanceManager(instanceManager.get());
    CreateInstanceHandler::setSolutionRegistry(&solutionRegistry);
    QuickInstanceHandler::setInstanceManager(instanceManager.get());
    QuickInstanceHandler::setSolutionRegistry(&solutionRegistry);
    InstanceHandler::setInstanceManager(instanceManager.get());

    // Register solution registry and storage with solution handler
    SolutionHandler::setSolutionRegistry(&solutionRegistry);
    SolutionHandler::setSolutionStorage(&solutionStorage);

    // Initialize group registry and storage
    // Default: /var/lib/edge_ai_api/groups (auto-created if needed)
    std::string groupsDir = EnvConfig::resolveDataDir("GROUPS_DIR", "groups");
    PLOG_INFO << "[Main] Groups directory: " << groupsDir;
    static GroupStorage groupStorage(groupsDir);
    static GroupRegistry &groupRegistry = GroupRegistry::getInstance();

    // Initialize default groups
    groupRegistry.initializeDefaultGroups();

    // Load persisted groups
    auto persistedGroups = groupStorage.loadAllGroups();
    for (const auto &group : persistedGroups) {
      if (!groupRegistry.groupExists(group.groupId)) {
        groupRegistry.registerGroup(group.groupId, group.groupName,
                                    group.description);
        PLOG_INFO << "[Main] Loaded group: " << group.groupId << " ("
                  << group.groupName << ")";
      }
    }

    // Sync groups with instances after loading
    // This ensures groups have correct instance counts and instance IDs
    auto allInstances = instanceManager->getAllInstances();
    std::map<std::string, std::vector<std::string>> groupInstancesMap;
    for (const auto &info : allInstances) {
      if (!info.group.empty()) {
        // Auto-create group if it doesn't exist
        if (!groupRegistry.groupExists(info.group)) {
          groupRegistry.registerGroup(info.group, info.group, "");
          PLOG_INFO << "[Main] Auto-created group from instance: "
                    << info.group;
        }
        groupInstancesMap[info.group].push_back(info.instanceId);
      }
    }
    // Update group registry with instance IDs
    for (const auto &[groupId, instanceIds] : groupInstancesMap) {
      groupRegistry.setInstanceIds(groupId, instanceIds);
    }

    // Register group registry, storage, and instance manager with group
    // handler
    GroupHandler::setGroupRegistry(&groupRegistry);
    GroupHandler::setGroupStorage(&groupStorage);
    GroupHandler::setInstanceManager(instanceManager.get());

    // Register instance manager with lines handler (supports both InProcess and
    // Subprocess modes)
    LinesHandler::setInstanceManager(instanceManager.get());

    // Register instance manager with stops handler (ba_stop)
    StopsHandler::setInstanceManager(instanceManager.get());

    // Register instance manager with jams handler (ba_jam)
    JamsHandler::setInstanceManager(instanceManager.get());

    // Register instance manager with WebSocket controller
    AIWebSocketController::setInstanceManager(instanceManager.get());

    // Initialize SecuRT instance manager and analytics entities manager
    static SecuRTInstanceManager securtInstanceManager(instanceManager.get());
    static AnalyticsEntitiesManager analyticsEntitiesManager;
    static SecuRTLineManager securtLineManager;

    // Initialize Area storage and manager
    static AreaStorage areaStorage;
    static AreaManager areaManager(&areaStorage, &securtInstanceManager);

    // Initialize SecuRT feature managers
    static SecuRTFeatureManager securtFeatureManager;
    static ExclusionAreaManager exclusionAreaManager;

    // Register SecuRT managers with handlers
    SecuRTHandler::setInstanceManager(&securtInstanceManager);
    SecuRTHandler::setAnalyticsEntitiesManager(&analyticsEntitiesManager);
    SecuRTHandler::setFeatureManager(&securtFeatureManager);
    SecuRTHandler::setExclusionAreaManager(&exclusionAreaManager);
    SecuRTHandler::setCoreInstanceManager(instanceManager.get());
    SecuRTLineHandler::setInstanceManager(&securtInstanceManager);
    SecuRTLineHandler::setLineManager(&securtLineManager);
    analyticsEntitiesManager.setLineManager(&securtLineManager);

    // Register Area manager with handler and analytics entities manager
    AreaHandler::setAreaManager(&areaManager);
    AnalyticsEntitiesManager::setAreaManager(&areaManager);

    // Register Area and Line managers with PipelineBuilder for SecuRT integration
    PipelineBuilder::setAreaManager(&areaManager);
    PipelineBuilder::setLineManager(&securtLineManager);

    // CRITICAL: Create handler instances AFTER dependencies are set
    // This ensures handlers are ready when Drogon registers routes
    // Handlers created here depend on dependencies set above
    static CreateInstanceHandler createInstanceHandler;
    static QuickInstanceHandler quickInstanceHandler;
    static InstanceHandler instanceHandler;
    static SolutionHandler solutionHandler;
    static GroupHandler groupHandler;
    static NodeHandler nodeHandler;
    static ONVIFHandler onvifHandler;
    static LinesHandler linesHandler;
    static JamsHandler jamsHandler;
    static StopsHandler stopsHandler;
    static SecuRTHandler securtHandler;
    static SecuRTLineHandler securtLineHandler;
    static AreaHandler areaHandler;

    // Initialize model upload handler with configurable directory
    // Priority: 1. MODELS_DIR env var, 2. /opt/edge_ai_api/models (with
    // auto-fallback)
    std::string modelsDir;
    const char *env_models_dir = std::getenv("MODELS_DIR");
    if (env_models_dir && strlen(env_models_dir) > 0) {
      modelsDir = std::string(env_models_dir);
      std::cerr << "[Main] Using MODELS_DIR from environment: " << modelsDir
                << std::endl;
    } else {
      // Try /opt/edge_ai_api/models first, fallback to user directory if needed
      modelsDir = "/opt/edge_ai_api/models";
      std::cerr << "[Main] Attempting to use: " << modelsDir << std::endl;
    }

    // Try to create directory if it doesn't exist
    // Strategy: Try /opt first, if fails, auto-fallback to user directory
    bool models_directory_ready = false;
    if (!std::filesystem::exists(modelsDir)) {
      std::cerr
          << "[Main] Models directory does not exist, attempting to create: "
          << modelsDir << std::endl;

      try {
        // Try to create directory (will create parent dirs if we have
        // permission)
        bool created = std::filesystem::create_directories(modelsDir);
        if (created) {
          std::cerr << "[Main] ✓ Successfully created models directory: "
                    << modelsDir << std::endl;
          models_directory_ready = true;
        } else {
          // Directory might have been created by another process
          if (std::filesystem::exists(modelsDir)) {
            std::cerr << "[Main] ✓ Models directory exists (created by another "
                         "process): "
                      << modelsDir << std::endl;
            models_directory_ready = true;
          }
        }
      } catch (const std::filesystem::filesystem_error &e) {
        if (e.code() == std::errc::permission_denied) {
          std::cerr << "[Main] ⚠ Cannot create " << modelsDir
                    << " (permission denied)" << std::endl;

          // Auto-fallback: Try user directory (works without sudo)
          if (env_models_dir == nullptr || strlen(env_models_dir) == 0) {
            const char *home = std::getenv("HOME");
            if (home) {
              std::string fallback_path =
                  std::string(home) + "/.local/share/edge_ai_api/models";
              std::cerr << "[Main] Auto-fallback: Trying user directory: "
                        << fallback_path << std::endl;
              try {
                std::filesystem::create_directories(fallback_path);
                modelsDir = fallback_path;
                models_directory_ready = true;
                std::cerr << "[Main] ✓ Using fallback directory: " << modelsDir
                          << std::endl;
                std::cerr << "[Main] ℹ Note: To use /opt/edge_ai_api/models, "
                             "create parent directory:"
                          << std::endl;
                std::cerr << "[Main] ℹ   sudo mkdir -p /opt/edge_ai_api && "
                             "sudo chown $USER:$USER /opt/edge_ai_api"
                          << std::endl;
              } catch (const std::exception &fallback_e) {
                std::cerr << "[Main] ⚠ Fallback also failed: "
                          << fallback_e.what() << std::endl;
                // Last resort: current directory
                modelsDir = "./models";
                try {
                  std::filesystem::create_directories(modelsDir);
                  models_directory_ready = true;
                  std::cerr << "[Main] ✓ Using current directory: " << modelsDir
                            << std::endl;
                } catch (...) {
                  std::cerr
                      << "[Main] ✗ ERROR: Cannot create any models directory"
                      << std::endl;
                }
              }
            } else {
              // No HOME, use current directory
              modelsDir = "./models";
              try {
                std::filesystem::create_directories(modelsDir);
                models_directory_ready = true;
                std::cerr << "[Main] ✓ Using current directory: " << modelsDir
                          << std::endl;
              } catch (...) {
                std::cerr << "[Main] ✗ ERROR: Cannot create ./models"
                          << std::endl;
              }
            }
          } else {
            // User specified MODELS_DIR but can't create it
            std::cerr
                << "[Main] ✗ ERROR: Cannot create user-specified directory: "
                << modelsDir << std::endl;
            std::cerr
                << "[Main] ✗ Please check permissions or use a different path"
                << std::endl;
          }
        } else {
          std::cerr << "[Main] ✗ ERROR creating " << modelsDir << ": "
                    << e.what() << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "[Main] ✗ Exception creating " << modelsDir << ": "
                  << e.what() << std::endl;
      }
    } else {
      // Check if it's actually a directory
      if (std::filesystem::is_directory(modelsDir)) {
        std::cerr << "[Main] ✓ Models directory already exists: " << modelsDir
                  << std::endl;
        models_directory_ready = true;
      } else {
        std::cerr << "[Main] ✗ ERROR: Path exists but is not a directory: "
                  << modelsDir << std::endl;
      }
    }

    if (models_directory_ready) {
      std::cerr << "[Main] ✓ Models directory is ready: " << modelsDir
                << std::endl;
    } else {
      std::cerr << "[Main] ⚠ WARNING: Models directory may not be ready"
                << std::endl;
    }

    PLOG_INFO << "[Main] Models directory: " << modelsDir;
    ModelUploadHandler::setModelsDirectory(modelsDir);
    static ModelUploadHandler modelUploadHandler;

    // Initialize video upload handler with configurable directory
    // Priority: 1. VIDEOS_DIR env var, 2. /opt/edge_ai_api/videos (with
    // auto-fallback)
    std::string videosDir;
    const char *env_videos_dir = std::getenv("VIDEOS_DIR");
    if (env_videos_dir && strlen(env_videos_dir) > 0) {
      videosDir = std::string(env_videos_dir);
      std::cerr << "[Main] Using VIDEOS_DIR from environment: " << videosDir
                << std::endl;
    } else {
      // Try /opt/edge_ai_api/videos first, fallback to user directory if needed
      videosDir = "/opt/edge_ai_api/videos";
      std::cerr << "[Main] Attempting to use: " << videosDir << std::endl;
    }

    // Try to create directory if it doesn't exist
    // Strategy: Try /opt first, if fails, auto-fallback to user directory
    bool videos_directory_ready = false;
    if (!std::filesystem::exists(videosDir)) {
      std::cerr
          << "[Main] Videos directory does not exist, attempting to create: "
          << videosDir << std::endl;

      try {
        // Try to create directory (will create parent dirs if we have
        // permission)
        bool created = std::filesystem::create_directories(videosDir);
        if (created) {
          std::cerr << "[Main] ✓ Successfully created videos directory: "
                    << videosDir << std::endl;
          videos_directory_ready = true;
        } else {
          // Directory might have been created by another process
          if (std::filesystem::exists(videosDir)) {
            std::cerr << "[Main] ✓ Videos directory exists (created by another "
                         "process): "
                      << videosDir << std::endl;
            videos_directory_ready = true;
          }
        }
      } catch (const std::filesystem::filesystem_error &e) {
        if (e.code() == std::errc::permission_denied) {
          std::cerr << "[Main] ⚠ Cannot create " << videosDir
                    << " (permission denied)" << std::endl;

          // Auto-fallback: Try user directory (works without sudo)
          if (env_videos_dir == nullptr || strlen(env_videos_dir) == 0) {
            const char *home = std::getenv("HOME");
            if (home) {
              std::string fallback_path =
                  std::string(home) + "/.local/share/edge_ai_api/videos";
              std::cerr << "[Main] Auto-fallback: Trying user directory: "
                        << fallback_path << std::endl;
              try {
                std::filesystem::create_directories(fallback_path);
                videosDir = fallback_path;
                videos_directory_ready = true;
                std::cerr << "[Main] ✓ Using fallback directory: " << videosDir
                          << std::endl;
                std::cerr << "[Main] ℹ Note: To use /opt/edge_ai_api/videos, "
                             "create parent directory:"
                          << std::endl;
                std::cerr << "[Main] ℹ   sudo mkdir -p /opt/edge_ai_api && "
                             "sudo chown $USER:$USER /opt/edge_ai_api"
                          << std::endl;
              } catch (const std::exception &fallback_e) {
                std::cerr << "[Main] ⚠ Fallback also failed: "
                          << fallback_e.what() << std::endl;
                // Last resort: current directory
                videosDir = "./videos";
                try {
                  std::filesystem::create_directories(videosDir);
                  videos_directory_ready = true;
                  std::cerr << "[Main] ✓ Using current directory: " << videosDir
                            << std::endl;
                } catch (...) {
                  std::cerr
                      << "[Main] ✗ ERROR: Cannot create any videos directory"
                      << std::endl;
                }
              }
            } else {
              // No HOME, use current directory
              videosDir = "./videos";
              try {
                std::filesystem::create_directories(videosDir);
                videos_directory_ready = true;
                std::cerr << "[Main] ✓ Using current directory: " << videosDir
                          << std::endl;
              } catch (...) {
                std::cerr << "[Main] ✗ ERROR: Cannot create ./videos"
                          << std::endl;
              }
            }
          } else {
            // User specified VIDEOS_DIR but can't create it
            std::cerr
                << "[Main] ✗ ERROR: Cannot create user-specified directory: "
                << videosDir << std::endl;
            std::cerr
                << "[Main] ✗ Please check permissions or use a different path"
                << std::endl;
          }
        } else {
          std::cerr << "[Main] ✗ ERROR creating " << videosDir << ": "
                    << e.what() << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "[Main] ✗ Exception creating " << videosDir << ": "
                  << e.what() << std::endl;
      }
    } else {
      // Check if it's actually a directory
      if (std::filesystem::is_directory(videosDir)) {
        std::cerr << "[Main] ✓ Videos directory already exists: " << videosDir
                  << std::endl;
        videos_directory_ready = true;
      } else {
        std::cerr << "[Main] ✗ ERROR: Path exists but is not a directory: "
                  << videosDir << std::endl;
      }
    }

    if (videos_directory_ready) {
      std::cerr << "[Main] ✓ Videos directory is ready: " << videosDir
                << std::endl;
    } else {
      std::cerr << "[Main] ⚠ WARNING: Videos directory may not be ready"
                << std::endl;
    }

    PLOG_INFO << "[Main] Videos directory: " << videosDir;
    VideoUploadHandler::setVideosDirectory(videosDir);
    static VideoUploadHandler videoUploadHandler;
    static RecognitionHandler recognitionHandler;

    // Initialize font upload handler with configurable directory
    // Priority: 1. FONTS_DIR env var, 2. /opt/edge_ai_api/fonts (with
    // auto-fallback)
    std::string fontsDir;
    const char *env_fonts_dir = std::getenv("FONTS_DIR");
    if (env_fonts_dir && strlen(env_fonts_dir) > 0) {
      fontsDir = std::string(env_fonts_dir);
      std::cerr << "[Main] Using FONTS_DIR from environment: " << fontsDir
                << std::endl;
    } else {
      // Try /opt/edge_ai_api/fonts first, fallback to user directory if needed
      fontsDir = "/opt/edge_ai_api/fonts";
      std::cerr << "[Main] Attempting to use: " << fontsDir << std::endl;
    }

    // Try to create directory if it doesn't exist
    // Strategy: Try /opt first, if fails, auto-fallback to user directory
    bool fonts_directory_ready = false;
    if (!std::filesystem::exists(fontsDir)) {
      std::cerr
          << "[Main] Fonts directory does not exist, attempting to create: "
          << fontsDir << std::endl;

      try {
        // Try to create directory (will create parent dirs if we have
        // permission)
        bool created = std::filesystem::create_directories(fontsDir);
        if (created) {
          std::cerr << "[Main] ✓ Successfully created fonts directory: "
                    << fontsDir << std::endl;
          fonts_directory_ready = true;
        } else {
          // Directory might have been created by another process
          if (std::filesystem::exists(fontsDir)) {
            std::cerr << "[Main] ✓ Fonts directory exists (created by another "
                         "process): "
                      << fontsDir << std::endl;
            fonts_directory_ready = true;
          }
        }
      } catch (const std::filesystem::filesystem_error &e) {
        if (e.code() == std::errc::permission_denied) {
          std::cerr << "[Main] ⚠ Cannot create " << fontsDir
                    << " (permission denied)" << std::endl;

          // Auto-fallback: Try user directory (works without sudo)
          if (env_fonts_dir == nullptr || strlen(env_fonts_dir) == 0) {
            const char *home = std::getenv("HOME");
            if (home) {
              std::string fallback_path =
                  std::string(home) + "/.local/share/edge_ai_api/fonts";
              std::cerr << "[Main] Auto-fallback: Trying user directory: "
                        << fallback_path << std::endl;
              try {
                std::filesystem::create_directories(fallback_path);
                fontsDir = fallback_path;
                fonts_directory_ready = true;
                std::cerr << "[Main] ✓ Using fallback directory: " << fontsDir
                          << std::endl;
                std::cerr << "[Main] ℹ Note: To use /opt/edge_ai_api/fonts, "
                             "create parent directory:"
                          << std::endl;
                std::cerr << "[Main] ℹ   sudo mkdir -p /opt/edge_ai_api && "
                             "sudo chown $USER:$USER /opt/edge_ai_api"
                          << std::endl;
              } catch (const std::exception &fallback_e) {
                std::cerr << "[Main] ⚠ Fallback also failed: "
                          << fallback_e.what() << std::endl;
                // Last resort: current directory
                fontsDir = "./fonts";
                try {
                  std::filesystem::create_directories(fontsDir);
                  fonts_directory_ready = true;
                  std::cerr << "[Main] ✓ Using current directory: " << fontsDir
                            << std::endl;
                } catch (...) {
                  std::cerr
                      << "[Main] ✗ ERROR: Cannot create any fonts directory"
                      << std::endl;
                }
              }
            } else {
              // No HOME, use current directory
              fontsDir = "./fonts";
              try {
                std::filesystem::create_directories(fontsDir);
                fonts_directory_ready = true;
                std::cerr << "[Main] ✓ Using current directory: " << fontsDir
                          << std::endl;
              } catch (...) {
                std::cerr << "[Main] ✗ ERROR: Cannot create ./fonts"
                          << std::endl;
              }
            }
          } else {
            // User specified FONTS_DIR but can't create it
            std::cerr
                << "[Main] ✗ ERROR: Cannot create user-specified directory: "
                << fontsDir << std::endl;
            std::cerr
                << "[Main] ✗ Please check permissions or use a different path"
                << std::endl;
          }
        } else {
          std::cerr << "[Main] ✗ ERROR creating " << fontsDir << ": "
                    << e.what() << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "[Main] ✗ Exception creating " << fontsDir << ": "
                  << e.what() << std::endl;
      }
    } else {
      // Check if it's actually a directory
      if (std::filesystem::is_directory(fontsDir)) {
        std::cerr << "[Main] ✓ Fonts directory already exists: " << fontsDir
                  << std::endl;
        fonts_directory_ready = true;
      } else {
        std::cerr << "[Main] ✗ ERROR: Path exists but is not a directory: "
                  << fontsDir << std::endl;
      }
    }

    if (fonts_directory_ready) {
      std::cerr << "[Main] ✓ Fonts directory is ready: " << fontsDir
                << std::endl;

      // Copy default font from include/fonts/ to fonts directory if it doesn't
      // exist or is corrupted
      std::string defaultFontName = "NotoSansCJKsc-Medium.otf";
      std::string defaultFontDest = fontsDir + "/" + defaultFontName;
      std::string defaultFontSource = "include/fonts/" + defaultFontName;

      bool needCopy = false;
      if (!std::filesystem::exists(defaultFontDest)) {
        needCopy = true;
        std::cerr << "[Main] Default font does not exist, will copy from source"
                  << std::endl;
      } else {
        // Check if file size matches (simple corruption check)
        try {
          auto sourceSize = std::filesystem::file_size(defaultFontSource);
          auto destSize = std::filesystem::file_size(defaultFontDest);
          if (sourceSize != destSize) {
            needCopy = true;
            std::cerr << "[Main] ⚠ Default font file size mismatch (source: "
                      << sourceSize << ", dest: " << destSize
                      << "), will recopy" << std::endl;
          }
        } catch (...) {
          // If can't check size, try to copy anyway
          needCopy = true;
        }
      }

      if (needCopy) {
        if (std::filesystem::exists(defaultFontSource)) {
          try {
            std::filesystem::copy_file(
                defaultFontSource, defaultFontDest,
                std::filesystem::copy_options::overwrite_existing);
            std::cerr << "[Main] ✓ Copied default font from "
                      << defaultFontSource << " to " << defaultFontDest
                      << std::endl;
            PLOG_INFO << "[Main] Copied default font: " << defaultFontDest;
          } catch (const std::filesystem::filesystem_error &e) {
            std::cerr << "[Main] ⚠ WARNING: Could not copy default font: "
                      << e.what() << std::endl;
            PLOG_WARNING << "[Main] Could not copy default font: " << e.what();
          }
        } else {
          std::cerr << "[Main] ℹ Default font source not found: "
                    << defaultFontSource
                    << " (this is OK if font is already uploaded)" << std::endl;
        }
      } else {
        std::cerr << "[Main] ✓ Default font already exists and appears valid: "
                  << defaultFontDest << std::endl;
      }
    } else {
      std::cerr << "[Main] ⚠ WARNING: Fonts directory may not be ready"
                << std::endl;
    }

    PLOG_INFO << "[Main] Fonts directory: " << fontsDir;
    FontUploadHandler::setFontsDirectory(fontsDir);
    static FontUploadHandler fontUploadHandler;

    // System configuration already loaded above (for web_server config)
    // Log additional configuration details
    if (systemConfig.isLoaded()) {
      int maxInstances = systemConfig.getMaxRunningInstances();
      if (maxInstances == 0) {
        PLOG_INFO << "[Main] Max running instances: unlimited";
      } else {
        PLOG_INFO << "[Main] Max running instances: " << maxInstances;
      }

      // Log decoder priority list
      auto decoderList = systemConfig.getDecoderPriorityList();
      if (!decoderList.empty()) {
        std::string decoderStr;
        for (size_t i = 0; i < decoderList.size(); ++i) {
          if (i > 0)
            decoderStr += ", ";
          decoderStr += decoderList[i];
        }
        PLOG_INFO << "[Main] Decoder priority list: " << decoderStr;
      }
    } else {
      PLOG_WARNING << "[Main] System configuration not loaded, using defaults";
    }

    // Create config handler instance to register endpoints
    static ConfigHandler configHandler;

    // Initialize system managers
    auto &systemConfigManager = SystemConfigManager::getInstance();
    systemConfigManager.loadConfig();

    auto &preferencesManager = PreferencesManager::getInstance();
    preferencesManager.loadPreferences();

    auto &decoderDetector = DecoderDetector::getInstance();
    decoderDetector.detectDecoders();

    // Register system handler
    static SystemHandler systemHandler;

    PLOG_INFO << "[Main] Instance management initialized";
    PLOG_INFO << "  POST /v1/core/instance - Create new instance";
    PLOG_INFO << "  GET /v1/core/instance - List all instances";
    PLOG_INFO << "  GET /v1/core/instance/{instanceId} - Get instance details";
    PLOG_INFO
        << "  POST /v1/core/instance/{instanceId}/input - Set input source";
    PLOG_INFO << "  POST /v1/core/instance/{instanceId}/config - Set config "
                 "value at path";
    PLOG_INFO << "  POST /v1/core/instance/{instanceId}/start - Start instance";
    PLOG_INFO << "  POST /v1/core/instance/{instanceId}/stop - Stop instance";
    PLOG_INFO << "  DELETE /v1/core/instance/{instanceId} - Delete instance";

    PLOG_INFO << "[Main] Solution management initialized";
    PLOG_INFO << "  GET /v1/core/solution - List all solutions";
    PLOG_INFO << "  GET /v1/core/solution/{solutionId} - Get solution details";
    PLOG_INFO << "  POST /v1/core/solution - Create new solution";
    PLOG_INFO << "  PUT /v1/core/solution/{solutionId} - Update solution";
    PLOG_INFO << "  DELETE /v1/core/solution/{solutionId} - Delete solution";
    PLOG_INFO << "  Instances directory: " << instancesDir;

    PLOG_INFO << "[Main] Group management initialized";
    PLOG_INFO << "  GET /v1/core/groups - List all groups";
    PLOG_INFO << "  GET /v1/core/groups/{groupId} - Get group details";
    PLOG_INFO << "  POST /v1/core/groups - Create new group";
    PLOG_INFO << "  PUT /v1/core/groups/{groupId} - Update group";
    PLOG_INFO << "  DELETE /v1/core/groups/{groupId} - Delete group";
    PLOG_INFO
        << "  GET /v1/core/groups/{groupId}/instances - Get instances in group";
    PLOG_INFO << "  Groups directory: " << groupsDir;
    PLOG_INFO << "[Main] Model upload handler initialized";
    PLOG_INFO << "  POST /v1/core/model/upload - Upload model file";
    PLOG_INFO << "  GET /v1/core/model/list - List uploaded models";
    PLOG_INFO << "  PUT /v1/core/model/{modelName} - Rename model file";
    PLOG_INFO << "  DELETE /v1/core/model/{modelName} - Delete model file";
    PLOG_INFO << "  Models directory: " << modelsDir;
    PLOG_INFO << "[Main] Video upload handler initialized";
    PLOG_INFO << "  POST /v1/core/video/upload - Upload video file";
    PLOG_INFO << "  GET /v1/core/video/list - List uploaded videos";
    PLOG_INFO << "  PUT /v1/core/video/{videoName} - Rename video file";
    PLOG_INFO << "  DELETE /v1/core/video/{videoName} - Delete video file";
    PLOG_INFO << "  Videos directory: " << videosDir;
    PLOG_INFO << "[Main] Font upload handler initialized";
    PLOG_INFO << "  POST /v1/core/font/upload - Upload font file";
    PLOG_INFO << "  GET /v1/core/font/list - List uploaded fonts";
    PLOG_INFO << "  PUT /v1/core/font/{fontName} - Rename font file";
    PLOG_INFO << "  DELETE /v1/core/font/{fontName} - Delete font file";
    PLOG_INFO << "  Fonts directory: " << fontsDir;

    PLOG_INFO << "[Main] Video upload handler initialized";
    PLOG_INFO << "  POST /v1/core/video/upload - Upload video file";
    PLOG_INFO << "  GET /v1/core/video/list - List uploaded videos";
    PLOG_INFO << "  PUT /v1/core/video/{videoName} - Rename video file";
    PLOG_INFO << "  DELETE /v1/core/video/{videoName} - Delete video file";
    PLOG_INFO << "  Videos directory: " << videosDir;

    PLOG_INFO << "[Main] Font upload handler initialized";
    PLOG_INFO << "  POST /v1/core/font/upload - Upload font file";
    PLOG_INFO << "  GET /v1/core/font/list - List uploaded fonts";
    PLOG_INFO << "  PUT /v1/core/font/{fontName} - Rename font file";
    PLOG_INFO << "  DELETE /v1/core/font/{fontName} - Delete font file";
    PLOG_INFO << "  Fonts directory: " << fontsDir;

    PLOG_INFO << "[Main] Configuration management initialized";
    PLOG_INFO << "  GET /v1/core/config - Get full configuration";
    PLOG_INFO << "  GET /v1/core/config/{path} - Get configuration section";
    PLOG_INFO << "  POST /v1/core/config - Create/update configuration (merge)";
    PLOG_INFO << "  PUT /v1/core/config - Replace entire configuration";
    PLOG_INFO
        << "  PATCH /v1/core/config/{path} - Update configuration section";
    PLOG_INFO
        << "  DELETE /v1/core/config/{path} - Delete configuration section";
    PLOG_INFO
        << "  POST /v1/core/config/reset - Reset configuration to defaults";
    PLOG_INFO << "  Config file: " << configPath;

    // Note: Infrastructure components (rate limiter, cache, resource manager,
    // etc.) are available but not initialized here since AI processing
    // endpoints are not needed yet. They can be enabled later when needed.

    // Initialize watchdog and health monitor from config.json (with env var fallback)
    auto monitoringConfig = systemConfig.getMonitoringConfig();
    uint32_t watchdog_check_interval = monitoringConfig.watchdogCheckIntervalMs;
    uint32_t watchdog_timeout = monitoringConfig.watchdogTimeoutMs;
    uint32_t health_monitor_interval = monitoringConfig.healthMonitorIntervalMs;

    g_watchdog =
        std::make_unique<Watchdog>(watchdog_check_interval, watchdog_timeout);
    g_watchdog->start(recoveryAction);

    g_health_monitor = std::make_unique<HealthMonitor>(health_monitor_interval);
    g_health_monitor->start(*g_watchdog);

    // Register watchdog and health monitor with handler
    WatchdogHandler::setWatchdog(g_watchdog.get());
    WatchdogHandler::setHealthMonitor(g_health_monitor.get());

    PLOG_INFO << "[Main] Watchdog and health monitor started";
    PLOG_INFO << "  GET /v1/core/watchdog - Watchdog status";

    // Start debug analysis board thread if debug mode is enabled
    std::thread debugThread;
    if (g_debug_mode.load()) {
      PLOG_INFO << "[Main] Starting debug analysis board thread...";
      debugThread = std::thread(debugAnalysisBoardThread);
      debugThread.detach(); // Detach so it runs independently
    }

    // Start retry limit monitoring thread
    // This thread periodically checks instances stuck in retry loops and stops
    // them
    std::thread retryMonitorThread([&instanceRegistry]() {
#ifdef __GLIBC__
      pthread_setname_np(pthread_self(), "retry-monitor");
#endif

      PLOG_INFO << "[RetryMonitor] Thread started - monitoring instances for "
                   "retry limits";

      while (!g_shutdown && !g_force_exit.load()) {
        try {
          // Check retry limits every 30 seconds
          std::this_thread::sleep_for(std::chrono::seconds(30));

          if (g_shutdown || g_force_exit.load()) {
            break;
          }

          // Check and handle retry limits
          int stoppedCount = instanceManager->checkAndHandleRetryLimits();
          if (stoppedCount > 0) {
            PLOG_INFO << "[RetryMonitor] Stopped " << stoppedCount
                      << " instance(s) due to retry limit";
          }
        } catch (const std::exception &e) {
          PLOG_WARNING << "[RetryMonitor] Error: " << e.what();
        } catch (...) {
          PLOG_WARNING << "[RetryMonitor] Unknown error";
        }
      }

      PLOG_INFO << "[RetryMonitor] Thread stopped";
    });
    retryMonitorThread.detach(); // Detach so it runs independently
    PLOG_INFO << "[Main] Retry limit monitoring thread started";

    // TEMPORARILY DISABLED: Queue monitoring thread
    // This thread monitors instance FPS and queue status to proactively prevent
    // deadlock When FPS drops to 0 or queue warnings are excessive,
    // automatically restart instance
    /*
    std::thread queueMonitorThread([&instanceRegistry]() {
        #ifdef __GLIBC__
        pthread_setname_np(pthread_self(), "queue-monitor");
        #endif

        PLOG_INFO << "[QueueMonitor] Thread started - monitoring queue status
    and FPS";

        auto& queueMonitor = QueueMonitor::getInstance();
        queueMonitor.startMonitoring();
        queueMonitor.setAutoClearThreshold(20.0);  // 20 warnings per second
    (reduced for faster response) queueMonitor.setMonitoringWindow(3);  // 3
    seconds window (reduced for faster response)

        // Track FPS history for each instance
        std::map<std::string, std::vector<double>> fps_history;
        std::map<std::string, int> zero_fps_count;
        std::map<std::string, std::chrono::steady_clock::time_point>
    last_restart_time;

        while (!g_shutdown && !g_force_exit.load()) {
            try {
                // Check queue status every 1 second (very aggressive to detect
    issues immediately)
                // This is critical to prevent deadlock when queue fills up
    quickly std::this_thread::sleep_for(std::chrono::seconds(1));

                if (g_shutdown || g_force_exit.load()) {
                    break;
                }

                // Get all running instances
                auto instances = instanceManager->listInstances();
                for (const auto& instanceId : instances) {
                    auto optInfo = instanceManager->getInstance(instanceId);
                    if (!optInfo.has_value() || !optInfo.value().running) {
                        continue;
                    }

                    const auto& info = optInfo.value();
                    double current_fps = info.fps;

                    // CRITICAL: Manually record queue full warnings if FPS = 0
                    // This helps detect issues even if warnings aren't being
    recorded elsewhere
                    // FPS = 0 usually indicates queue is full and processing is
    blocked if (current_fps == 0.0 && info.hasReceivedData) {
                        // Instance was working but now FPS = 0 - likely queue
    full queueMonitor.recordQueueFullWarning(instanceId, "fps_zero_detected");
                    }

                    // Calculate time since instance started
                    auto now = std::chrono::steady_clock::now();
                    auto time_since_start =
    std::chrono::duration_cast<std::chrono::seconds>( now -
    info.startTime).count();

                    // Grace period: Skip FPS checks for instances that just
    started (15 seconds)
                    // This prevents false positives when instance is still
    initializing const int GRACE_PERIOD_SECONDS = 15; if (time_since_start <
    GRACE_PERIOD_SECONDS) {
                        // Instance is still in grace period - skip FPS checks
                        continue;
                    }

                    // Prevent restart loops: Don't restart if we just restarted
    this instance recently (30 seconds) auto last_restart_it =
    last_restart_time.find(instanceId); if (last_restart_it !=
    last_restart_time.end()) { auto time_since_restart =
    std::chrono::duration_cast<std::chrono::seconds>( now -
    last_restart_it->second).count(); if (time_since_restart < 30) {
                            // Just restarted recently - skip to prevent restart
    loop continue;
                        }
                    }

                    // Track FPS history (keep last 6 readings = 60 seconds)
                    auto& history = fps_history[instanceId];
                    history.push_back(current_fps);
                    if (history.size() > 6) {
                        history.erase(history.begin());
                    }

                    // Detect queue full issues:
                    // 1. FPS = 0 for extended period (9+ seconds = 3 checks)
    while instance is running
                    // 2. Excessive queue full warnings
                    bool should_restart = false;
                    std::string reason;

                    if (current_fps == 0.0) {
                        zero_fps_count[instanceId]++;
                        // Record warning when FPS = 0 (indicates possible queue
    full)
                        // Extract node name from instance (use first node as
    indicator) queueMonitor.recordQueueFullWarning(instanceId,
    "fps_zero_indicator");

                        // If FPS = 0 for 2 checks (2 seconds with 1s interval),
    restart
                        // Very aggressive to prevent deadlock - queue full can
    cause FPS = 0 quickly
                        // Also check if instance has received data (indicates
    it was working before) if (zero_fps_count[instanceId] >= 2 &&
    info.hasReceivedData) { should_restart = true; reason = "FPS = 0 for 2+
    seconds (possible queue full - restarting immediately)";
                        }
                    } else {
                        zero_fps_count[instanceId] = 0;  // Reset counter if FPS
    > 0
                    }

                    // Check queue full warnings - restart immediately if too
    many warnings
                    // This is critical to prevent deadlock when queue is full
                    auto stats = queueMonitor.getStats(instanceId);
                    if (stats && stats->warning_count.load() > 0) {
                        // EXTREMELY aggressive: restart if 1 warning to prevent
    deadlock
                        // Queue full at json_mqtt_broker or yolo_detector
    indicates processing/MQTT is too slow
                        // Need to restart IMMEDIATELY before deadlock occurs
                        // With 1-second check interval, 1 warning = immediate
    restart if (stats->warning_count.load() >= 1) { should_restart = true;
                            reason = "Queue full warning detected (" +
                                    std::to_string(stats->warning_count.load())
    + " warnings) - restarting IMMEDIATELY to prevent deadlock";
                        }
                    }

                    // Also check shouldClearQueue for additional validation
                    if (!should_restart &&
    queueMonitor.shouldClearQueue(instanceId)) { if (stats &&
    stats->warning_count.load() >= 10) {  // Reduced from 20 to 10 for faster
    response should_restart = true; reason = "Queue clearing recommended (" +
                                    std::to_string(stats->warning_count.load())
    + " warnings)";
                        }
                    }

                    // CRITICAL: If we detect any queue full warnings at all,
    record them immediately
                    // This helps detect issues even if warnings aren't being
    recorded elsewhere
                    // Check if there are any recent warnings (within last 5
    seconds) if (stats && stats->warning_count.load() > 0) { auto
    time_since_last_warning = std::chrono::duration_cast<std::chrono::seconds>(
                            now - stats->last_warning_time).count();
                        // If warnings are very recent (within 5 seconds), this
    is a critical situation if (time_since_last_warning < 5 &&
    stats->warning_count.load() >= 2) {
                            // Very recent warnings indicate active queue full
    issue
                            // Restart immediately to prevent deadlock
                            if (!should_restart) {
                                should_restart = true;
                                reason = "Active queue full warnings detected ("
    + std::to_string(stats->warning_count.load()) + " warnings in last 5s) -
    restarting IMMEDIATELY";
                            }
                        }
                    }

                    // Restart instance if needed
                    if (should_restart) {
                        PLOG_WARNING << "[QueueMonitor] Instance " << instanceId
                                   << " needs restart: " << reason;

                        try {
                            PLOG_INFO << "[QueueMonitor] Restarting instance "
    << instanceId
                                     << " to clear queue and prevent deadlock";

                            // Stop instance with timeout protection
                            try {
                                instanceManager->stopInstance(instanceId);
                            } catch (const std::exception& e) {
                                PLOG_ERROR << "[QueueMonitor] Error stopping
    instance "
                                          << instanceId << ": " << e.what();
                                // Continue anyway - instance might already be
    stopped } catch (...) { PLOG_ERROR << "[QueueMonitor] Unknown error stopping
    instance " << instanceId;
                                // Continue anyway
                            }

                            // Wait longer to ensure cleanup completes
                            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    // Increased delay

                            // Start instance with timeout protection
                            try {
                                instanceManager->startInstance(instanceId);

                                // Record restart time to prevent restart loops
                                last_restart_time[instanceId] =
    std::chrono::steady_clock::now();

                                // Clear stats after restart
                                queueMonitor.clearStats(instanceId);
                                zero_fps_count[instanceId] = 0;
                                fps_history[instanceId].clear();

                                PLOG_INFO << "[QueueMonitor] Instance " <<
    instanceId
                                         << " restarted successfully";
                            } catch (const std::exception& e) {
                                PLOG_ERROR << "[QueueMonitor] Failed to start
    instance "
                                          << instanceId << ": " << e.what();
                                // Don't record restart time if start failed
                            } catch (...) {
                                PLOG_ERROR << "[QueueMonitor] Unknown error
    starting instance " << instanceId;
                            }
                        } catch (const std::exception& e) {
                            PLOG_ERROR << "[QueueMonitor] Failed to restart
    instance "
                                      << instanceId << ": " << e.what();
                        } catch (...) {
                            PLOG_ERROR << "[QueueMonitor] Unknown error during
    restart of instance " << instanceId;
                        }
                    }
                }
            } catch (const std::exception& e) {
                PLOG_WARNING << "[QueueMonitor] Error: " << e.what();
            } catch (...) {
                PLOG_WARNING << "[QueueMonitor] Unknown error";
            }
        }

        queueMonitor.stopMonitoring();
        PLOG_INFO << "[QueueMonitor] Thread stopped";
    });
    queueMonitorThread.detach(); // Detach so it runs independently
    PLOG_INFO << "[Main] Queue monitoring thread started";
    */
    PLOG_INFO << "[Main] Queue monitoring thread DISABLED (temporarily)";

    // CRITICAL: Start shutdown watchdog thread
    // This thread monitors shutdown state and forces exit if shutdown is stuck
    // This is necessary because:
    // 1. RTSP retry loops can prevent normal shutdown
    // 2. Blocked API requests can block main event loop (e.g., Swagger API
    // calls that hang)
    // 3. Any other blocking operation can prevent shutdown
    std::thread shutdownWatchdogThread([]() {
#ifdef __GLIBC__
      pthread_setname_np(pthread_self(), "shutdown-watchdog");
#endif

      PLOG_INFO
          << "[ShutdownWatchdog] Thread started - monitoring shutdown state";

      while (!g_force_exit.load()) {
        try {
          // Check every 100ms if shutdown is stuck (faster check for blocked
          // API requests)
          std::this_thread::sleep_for(std::chrono::milliseconds(100));

          // If shutdown was requested but process is still running after 300ms,
          // force exit Reduced from 500ms to 300ms to handle blocked API
          // requests faster
          if (g_shutdown_requested.load() && !g_force_exit.load()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - g_shutdown_request_time)
                    .count();

            if (elapsed > 300) {
              // Shutdown requested but process still running after 300ms
              // This could be due to:
              // 1. RTSP retry loops blocking shutdown
              // 2. Blocked API requests in main event loop
              // 3. Any other blocking operation
              PLOG_WARNING << "[ShutdownWatchdog] Shutdown stuck for "
                           << elapsed
                           << "ms - forcing exit (blocked API request or RTSP "
                              "retry loop)";
              std::cerr << "[CRITICAL] Shutdown watchdog: Process stuck for "
                        << elapsed << "ms - FORCING EXIT NOW" << std::endl;
              std::cerr << "[CRITICAL] Possible causes: blocked API request, "
                           "RTSP retry loop, or other blocking operation"
                        << std::endl;
              std::fflush(stdout);
              std::fflush(stderr);

              g_force_exit = true;

              // Unregister signal handlers to prevent recovery
              std::signal(SIGINT, SIG_DFL);
              std::signal(SIGTERM, SIG_DFL);
              std::signal(SIGABRT, SIG_DFL);

              // CRITICAL: Use kill() with SIGKILL to force immediate
              // termination SIGKILL cannot be caught or ignored - it will kill
              // the process immediately This works even if main event loop is
              // blocked by an API request
              kill(getpid(), SIGKILL);

              // If kill() somehow fails (shouldn't happen), use _Exit() as
              // fallback _Exit() terminates immediately without calling
              // destructors
              std::_Exit(1);
            }
          }
        } catch (const std::exception &e) {
          PLOG_WARNING << "[ShutdownWatchdog] Error: " << e.what();
          // Even if there's an error, try to force exit if shutdown was
          // requested
          if (g_shutdown_requested.load() && !g_force_exit.load()) {
            std::cerr
                << "[CRITICAL] Shutdown watchdog error - forcing exit anyway"
                << std::endl;
            g_force_exit = true;
            kill(getpid(), SIGKILL);
            std::_Exit(1);
          }
        } catch (...) {
          PLOG_WARNING << "[ShutdownWatchdog] Unknown error";
          // Even if there's an error, try to force exit if shutdown was
          // requested
          if (g_shutdown_requested.load() && !g_force_exit.load()) {
            std::cerr << "[CRITICAL] Shutdown watchdog unknown error - forcing "
                         "exit anyway"
                      << std::endl;
            g_force_exit = true;
            kill(getpid(), SIGKILL);
            std::_Exit(1);
          }
        }
      }

      PLOG_INFO << "[ShutdownWatchdog] Thread stopped";
    });
    shutdownWatchdogThread.detach(); // Detach so it runs independently
    PLOG_INFO << "[Main] Shutdown watchdog thread started";

    // Set HTTP server configuration from config.json (with env var fallback)
    // Priority: config.json > Environment Variables > Defaults
    // Note: webServerConfig was already loaded at line 2049, reuse it
    auto performanceConfig = systemConfig.getPerformanceConfig();
    auto loggingConfig = systemConfig.getLoggingConfig();
    
    // Get full web server config with all settings (reload to get new fields)
    auto fullWebServerConfig = systemConfig.getWebServerConfig();

    size_t max_body_size = fullWebServerConfig.maxBodySize;
    size_t max_memory_body_size = fullWebServerConfig.maxMemoryBodySize;
    size_t keepalive_requests = fullWebServerConfig.keepaliveRequests;
    size_t keepalive_timeout = fullWebServerConfig.keepaliveTimeout;
    bool enable_reuse_port = fullWebServerConfig.reusePort;

    int thread_num = performanceConfig.threadNum;

    // Parse log level
    trantor::Logger::LogLevel log_level = trantor::Logger::kInfo;
    std::string log_upper = loggingConfig.logLevel;
    std::transform(log_upper.begin(), log_upper.end(), log_upper.begin(),
                   ::toupper);
    if (log_upper == "TRACE")
      log_level = trantor::Logger::kTrace;
    else if (log_upper == "DEBUG")
      log_level = trantor::Logger::kDebug;
    else if (log_upper == "INFO")
      log_level = trantor::Logger::kInfo;
    else if (log_upper == "WARN")
      log_level = trantor::Logger::kWarn;
    else if (log_upper == "ERROR")
      log_level = trantor::Logger::kError;

    // Use hardware_concurrency if thread_num is 0
    // For AI workloads, recommend 2-4x CPU cores for I/O-bound operations
    // IMPORTANT: Each API request runs on a separate thread from the pool
    // This ensures RTSP retry loops don't block other API requests
    unsigned int actual_thread_num =
        (thread_num == 0) ? std::thread::hardware_concurrency() : thread_num;

    // Optimize thread count for AI workloads if auto-detected
    // Use more threads to handle concurrent requests and prevent blocking
    // RTSP retry loops run in SDK threads and won't block API thread pool
    if (thread_num == 0) {
      // For AI server with RTSP/file sources, use at least min_threads from config
      actual_thread_num = std::max(actual_thread_num, performanceConfig.minThreads);
      // Cap at reasonable maximum to avoid too many threads
      actual_thread_num = std::min(actual_thread_num, performanceConfig.maxThreads);
    }

    PLOG_INFO << "[Performance] Thread pool size: " << actual_thread_num;
    PLOG_INFO << "[Performance] Keep-alive: " << keepalive_requests
              << " requests, " << keepalive_timeout << "s timeout";
    PLOG_INFO << "[Performance] Max body size: "
              << (max_body_size / 1024 / 1024) << "MB";
    PLOG_INFO << "[Performance] Reuse port: "
              << (enable_reuse_port ? "enabled" : "disabled");

    auto &app = drogon::app();
    app.setClientMaxBodySize(max_body_size)
        .setClientMaxMemoryBodySize(max_memory_body_size)
        .setLogLevel(log_level)
        .setThreadNum(actual_thread_num);

    // Register CORS filter to handle OPTIONS preflight requests
    // This intercepts OPTIONS requests before Drogon's automatic handling
    // Note: Filter is defined with isAutoCreation=false to allow manual
    // registration
    auto corsFilter = std::make_shared<CorsFilter>();
    app.registerFilter(corsFilter);
    PLOG_INFO
        << "[Config] CORS filter registered for OPTIONS preflight handling";

    // Register metrics middleware to track request metrics
    // Note: The middleware stores timing info in request attributes.
    // Metrics are recorded via MetricsInterceptor which should be called
    // when responses are created. However, since Drogon's filter mechanism
    // doesn't easily support callback wrapping, we'll need to ensure
    // metrics are recorded. For now, the middleware stores timing info
    // and handlers or other mechanisms can call MetricsInterceptor::intercept()
    // to record metrics.
    auto metricsMiddleware = std::make_shared<RequestMetricsMiddleware>();
    app.registerFilter(metricsMiddleware);
    PLOG_INFO
        << "[Config] Metrics middleware registered for endpoint monitoring";

    // Explicitly disable HTTPS - we only use HTTP
    // With useSSL=false, Drogon will not check for SSL certificates
    PLOG_INFO << "[Config] Using HTTP only (HTTPS disabled)";

    // Enable keep-alive for better connection reuse
    // Note: Drogon handles keep-alive automatically, but we can configure it
    // addListener with HTTP only (useSSL=false explicitly - no SSL certificates
    // needed)
    // Try to find available port before starting Drogon
    // Drogon's addListener doesn't throw - it fails at run() time
    // So we check port availability manually first
    // Note: When SO_REUSEPORT is enabled, multiple sockets can bind to the same
    // port So we need to handle both cases
    // Improved: Check if port is actually available by trying to bind
    // This works even when SO_REUSEPORT is enabled (we check without it first)
    auto isPortAvailable = [enable_reuse_port](const std::string &host,
                                               int port) -> bool {
      // First, try without SO_REUSEPORT to check if port is truly available
      // This catches cases where port is already bound by another process
      int sock = socket(AF_INET, SOCK_STREAM, 0);
      if (sock < 0)
        return false;

      // Always allow address reuse to handle TIME_WAIT sockets
      int opt = 1;
      setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

      // Don't set SO_REUSEPORT for the check - we want to know if port is
      // available for a new process
      // Only if SO_REUSEPORT is enabled AND we can bind, then it's available

      struct sockaddr_in addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);

      if (host == "0.0.0.0" || host.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
      } else {
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
      }

      int result = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
      close(sock);
      
      // If bind failed, port is in use
      if (result != 0) {
        return false;
      }
      
      // If SO_REUSEPORT is enabled, we also need to check with SO_REUSEPORT
      // because multiple processes can bind to the same port
      if (enable_reuse_port) {
#ifdef SO_REUSEPORT
        int sock2 = socket(AF_INET, SOCK_STREAM, 0);
        if (sock2 >= 0) {
          setsockopt(sock2, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
          setsockopt(sock2, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
          
          int result2 = bind(sock2, (struct sockaddr *)&addr, sizeof(addr));
          close(sock2);
          // If we can bind with SO_REUSEPORT, port is available
          return result2 == 0;
        }
#endif
      }
      
      return true;
    };

    int max_port_retries = 10;
    bool port_changed = false;

    // Try to find available port with retry mechanism
    for (int retry = 0; retry < max_port_retries; ++retry) {
      if (isPortAvailable(host, port)) {
        if (retry > 0) {
          port_changed = true;
          std::cerr << "[Server] ⚠️  Port " << original_port
                    << " is already in use, automatically switched to port "
                    << port << std::endl;
          PLOG_WARNING << "[Server] Original port " << original_port
                       << " was in use, using port " << port << " instead";
        }
        break;
      }
      std::cerr << "[Server] ⚠️  Port " << port
                << " is in use, trying port " << (port + 1) << "..." << std::endl;
      PLOG_WARNING << "[Server] Port " << port << " is in use, trying port "
                   << (port + 1);
      ++port;

      if (retry == max_port_retries - 1) {
        std::cerr << "[Server] ❌ Error: Could not find available port after "
                  << max_port_retries << " retries (tried ports " << original_port
                  << "-" << port << ")" << std::endl;
        PLOG_ERROR << "[Error] Could not find available port after "
                   << max_port_retries << " retries (tried ports "
                   << original_port << "-" << port << ")";
        throw std::runtime_error("No available port found");
      }
    }

    // Now add listener with the available port
    // Note: Drogon framework handles SO_REUSEPORT internally based on thread
    // pool We just need to call addListener once - Drogon will create multiple
    // listeners if needed for load balancing when using multiple threads
    // Wrap in try-catch to handle potential binding errors
    bool listener_added = false;
    for (int retry = 0; retry < max_port_retries; ++retry) {
      try {
        // Ensure port is within valid range and convert to uint16_t
        if (port < 1 || port > 65535) {
          throw std::runtime_error("Port out of valid range (1-65535)");
        }
        app.addListener(host, static_cast<uint16_t>(port), false, "", "");
        listener_added = true;
        break;
      } catch (const std::exception &e) {
        // If addListener fails, try next port
        std::cerr << "[Server] ⚠️  Failed to bind to port " << port
                  << ": " << e.what() << std::endl;
        PLOG_WARNING << "[Server] Failed to bind to port " << port << ": "
                    << e.what();
        
        if (retry < max_port_retries - 1) {
          ++port;
          port_changed = true;
          std::cerr << "[Server] ⚠️  Trying port " << port << "..." << std::endl;
          PLOG_WARNING << "[Server] Trying port " << port;
        } else {
          std::cerr << "[Server] ❌ Error: Could not bind to any port after "
                    << max_port_retries << " attempts" << std::endl;
          PLOG_ERROR << "[Error] Could not bind to any port after "
                     << max_port_retries << " attempts";
          throw;
        }
      }
    }

    if (!listener_added) {
      throw std::runtime_error("Failed to add listener to any available port");
    }

    // Display final port information
    if (port_changed) {
      std::cerr << "[Server] ✅ Server will use port " << port
                << " (original port " << original_port << " was in use)"
                << std::endl;
    }
    PLOG_INFO << "[Server] Starting HTTP server on " << host << ":" << port;
    PLOG_INFO << "[Server] Access http://" << host << ":" << port
              << "/v1/swagger to view all APIs";
    PLOG_INFO << "[Server] Access http://" << host << ":" << port
              << "/v1/document to view API documentation";

    // Initialize current server config for auto-reload detection
    systemConfig.initializeCurrentServerConfig(webServerConfig);

    // Schedule auto-start to run after server is ready (2 seconds delay)
    // This runs on the event loop thread but starts instances in a separate
    // thread to avoid blocking if instances fail to start, hang, or crash
    auto *loop = app.getLoop();
    if (loop) {
      // Capture pointer to static variable to avoid warning about capturing
      // non-automatic storage duration variable
      IInstanceManager *instanceManagerPtr = instanceManager.get();
      loop->runAfter(2.0, [instanceManagerPtr]() {
        PLOG_INFO << "[Main] Server is ready - starting auto-start process in "
                     "separate thread";
        // Start auto-start in a separate thread to avoid blocking the event
        // loop Even if instances fail, hang, or crash, the main program
        // continues running
        std::thread autoStartThread([instanceManagerPtr]() {
          autoStartInstances(instanceManagerPtr);
        });
        autoStartThread.detach(); // Detach so it runs independently
      });
    } else {
      PLOG_WARNING
          << "[Main] Event loop not available - auto-start will be skipped";
    }

    // CRITICAL: Re-register signal handlers AFTER Drogon setup to ensure
    // they're not overridden Drogon may register its own handlers during
    // initialization, so we register again here
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGABRT, signalHandler);
    std::signal(
        SIGSEGV,
        segfaultHandler); // Catch segmentation faults from GStreamer crashes
    PLOG_INFO << "[Main] Signal handlers registered (SIGINT, SIGTERM, SIGABRT, "
                 "SIGSEGV)";

    // Suppress HTTPS warning - we're intentionally using HTTP only
    // The warning "You can't use https without cert file or key file"
    // is expected when HTTPS is not configured, but we only want HTTP
    try {
      // Run server - this blocks until quit() is called
      // CRITICAL: If app.run() blocks even after quit(), shutdown timer will
      // force exit
      app.run();

      // After app.run() returns, ensure we exit cleanly
      // If we're here, quit() was called, so proceed with cleanup
      if (g_shutdown || g_force_exit.load()) {
        PLOG_INFO << "[Server] Shutdown signal received, cleaning up...";
      }

      // If force exit was requested, skip cleanup and exit immediately
      if (g_force_exit.load()) {
        std::cerr << "[SHUTDOWN] Force exit requested, skipping cleanup..."
                  << std::endl;
        _exit(0);
      }
    } catch (const std::exception &e) {
      // Check if it's just the HTTPS warning
      std::string error_msg = e.what();
      if (error_msg.find("https") != std::string::npos &&
          error_msg.find("cert") != std::string::npos) {
        PLOG_WARNING << "[Warning] HTTPS warning detected but ignored (using "
                        "HTTP only): "
                     << error_msg;
        // Continue anyway - this is expected when not using HTTPS
        return 0;
      }
      throw; // Re-throw if it's a different error
    }

    // Cleanup - but only if not force exit
    if (!g_force_exit.load()) {
      // Note: Debug thread will stop automatically when g_shutdown is true
      if (g_health_monitor) {
        g_health_monitor->stop();
      }
      if (g_watchdog) {
        g_watchdog->stop();
      }

      // Stop log cleanup thread (with timeout protection)
      CategorizedLogger::shutdown();
    }

    PLOG_INFO << "Server stopped.";
    return 0;
  } catch (const std::exception &e) {
    PLOG_FATAL << "Fatal error: " << e.what();
    return 1;
  } catch (...) {
    PLOG_FATAL << "Fatal error: Unknown exception";
    return 1;
  }
}
