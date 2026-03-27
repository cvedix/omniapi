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
void InstanceRegistry::startVideoLoopThread(const std::string &instanceId) {
  // DISABLED: Video loop feature removed to improve performance
  return;

  // Stop existing thread if any
  stopVideoLoopThread(instanceId);

  // Check if instance has LOOP_VIDEO enabled
  bool loopEnabled = false;
  {
    std::shared_lock<std::shared_timed_mutex> lock(mutex_);
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt != instances_.end()) {
      const auto &info = instanceIt->second;
      auto it = info.additionalParams.find("LOOP_VIDEO");
      if (it != info.additionalParams.end()) {
        std::string loopValue = it->second;
        std::transform(loopValue.begin(), loopValue.end(), loopValue.begin(),
                       ::tolower);
        loopEnabled =
            (loopValue == "true" || loopValue == "1" || loopValue == "yes");
      }
    }
  }

  if (!loopEnabled) {
    return; // Loop not enabled, don't start thread
  }

  // Check if instance is file-based
  bool isFileBased = false;
  {
    std::shared_lock<std::shared_timed_mutex> lock(mutex_);
    auto instanceIt = instances_.find(instanceId);
    if (instanceIt != instances_.end()) {
      const auto &info = instanceIt->second;
      isFileBased =
          !info.filePath.empty() || info.additionalParams.find("FILE_PATH") !=
                                        info.additionalParams.end();
    }
  }

  if (!isFileBased) {
    return; // Not a file-based instance, don't start thread
  }

  // Create stop flag
  {
    std::lock_guard<std::mutex> lock(thread_mutex_);
    video_loop_thread_stop_flags_.emplace(instanceId, false);
  }

  std::cerr << "[InstanceRegistry] [VideoLoop] Starting video loop monitoring "
               "thread for instance "
            << instanceId << std::endl;

  // Start new monitoring thread
  // CRITICAL: Capture instanceId by value to avoid use-after-free
  // We access mutex_ and instances_ through 'this', but we check stop flag
  // first to ensure thread exits quickly if instance is stopped
  std::thread videoLoopThread([this, instanceId]() {
    try {
      int zeroFpsCount = 0;
      const int ZERO_FPS_THRESHOLD =
          3; // Check 3 times (30 seconds) before restarting
      const int CHECK_INTERVAL_SECONDS =
          10; // Check every 10 seconds (increased from 5 to reduce CPU usage)
      const int MIN_RUNTIME_SECONDS =
          60; // Minimum runtime before allowing restart (increased from 30 to
              // 60 seconds)
      auto instanceStartTime = std::chrono::steady_clock::now();
      bool hasEverReceivedData = false;

      while (true) {
        // Check stop flag first
        {
          try {
            std::lock_guard<std::mutex> lock(thread_mutex_);
            auto flagIt = video_loop_thread_stop_flags_.find(instanceId);
            if (flagIt == video_loop_thread_stop_flags_.end() ||
                flagIt->second.load()) {
              break;
            }
          } catch (...) {
            // If mutex access fails, exit thread to prevent crash
            std::cerr << "[InstanceRegistry] [VideoLoop] Error accessing stop "
                         "flag, exiting thread"
                      << std::endl;
            return;
          }
        }

        // Wait CHECK_INTERVAL_SECONDS seconds (check flag periodically)
        for (int i = 0; i < CHECK_INTERVAL_SECONDS * 10; ++i) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));

          // Check stop flag
          {
            try {
              std::lock_guard<std::mutex> lock(thread_mutex_);
              auto flagIt = video_loop_thread_stop_flags_.find(instanceId);
              if (flagIt == video_loop_thread_stop_flags_.end() ||
                  flagIt->second.load()) {
                return;
              }
            } catch (...) {
              // If mutex access fails, exit thread to prevent crash
              std::cerr << "[InstanceRegistry] [VideoLoop] Error accessing "
                           "stop flag, exiting thread"
                        << std::endl;
              return;
            }
          }
        }

        // Check if instance still exists and is running
        bool shouldRestart = false;
        {
          try {
            std::unique_lock<std::shared_timed_mutex> lock(mutex_);
            auto instanceIt = instances_.find(instanceId);
            if (instanceIt == instances_.end() || !instanceIt->second.running) {
              // Instance deleted or stopped, exit thread
              return;
            }

            const auto &info = instanceIt->second;

            // Track if we've ever received data
            if (info.hasReceivedData) {
              hasEverReceivedData = true;
            }

            // Check minimum runtime before allowing restart
            auto runtime =
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - instanceStartTime)
                    .count();

            // Check FPS - if 0 for multiple checks, video likely ended
            // CRITICAL: Only restart if:
            // 1. Instance has been running for at least MIN_RUNTIME_SECONDS
            // 2. Instance has received data at some point (hasEverReceivedData)
            // 3. Current FPS = 0 and hasReceivedData = true (was working but
            // now stopped)
            if (info.fps == 0.0 && info.hasReceivedData &&
                hasEverReceivedData && runtime >= MIN_RUNTIME_SECONDS) {
              // Instance was working but now FPS = 0 - video likely ended
              zeroFpsCount++;
              std::cerr
                  << "[InstanceRegistry] [VideoLoop] FPS = 0 detected (count: "
                  << zeroFpsCount << "/" << ZERO_FPS_THRESHOLD
                  << ", runtime: " << runtime << "s)" << std::endl;

              if (zeroFpsCount >= ZERO_FPS_THRESHOLD) {
                shouldRestart = true;
                zeroFpsCount = 0; // Reset counter
              }
            } else if (info.fps > 0.0) {
              // FPS > 0, video is playing - reset counter
              zeroFpsCount = 0;
            } else if (runtime < MIN_RUNTIME_SECONDS) {
              // Instance just started, don't restart yet
              if (zeroFpsCount == 0) {
                std::cerr << "[InstanceRegistry] [VideoLoop] Instance just "
                             "started (runtime: "
                          << runtime << "s < " << MIN_RUNTIME_SECONDS
                          << "s), waiting before checking for restart..."
                          << std::endl;
              }
              zeroFpsCount = 0; // Reset counter during startup period
            }
          } catch (const std::exception &e) {
            std::cerr << "[InstanceRegistry] [VideoLoop] Exception accessing "
                         "instance data: "
                      << e.what() << std::endl;
            // Continue to next iteration instead of crashing
            continue;
          } catch (...) {
            std::cerr << "[InstanceRegistry] [VideoLoop] Unknown error "
                         "accessing instance data"
                      << std::endl;
            // Continue to next iteration instead of crashing
            continue;
          }
        }

        // Restart file source node if needed
        if (shouldRestart) {
          std::cerr << "[InstanceRegistry] [VideoLoop] Video ended detected - "
                       "restarting file source node..."
                    << std::endl;

          // CRITICAL: Check stop flag before starting restart operation
          {
            try {
              std::lock_guard<std::mutex> lock(thread_mutex_);
              auto flagIt = video_loop_thread_stop_flags_.find(instanceId);
              if (flagIt == video_loop_thread_stop_flags_.end() ||
                  flagIt->second.load()) {
                return; // Stop flag set, exit thread
              }
            } catch (...) {
              return; // Error accessing stop flag, exit thread
            }
          }

          // Get pipeline copy
          std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> pipelineCopy;
          {
            try {
              std::unique_lock<std::shared_timed_mutex> lock(mutex_);
              auto pipelineIt = pipelines_.find(instanceId);
              if (pipelineIt != pipelines_.end() &&
                  !pipelineIt->second.empty()) {
                pipelineCopy = pipelineIt->second;
              }
            } catch (...) {
              std::cerr << "[InstanceRegistry] [VideoLoop] Exception getting "
                           "pipeline copy, skipping restart"
                        << std::endl;
              continue; // Skip restart if we can't get pipeline
            }
          }

          if (!pipelineCopy.empty()) {
            // Check if first node is file source node
            auto fileNode =
                std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(
                    pipelineCopy[0]);
            if (fileNode) {
              try {
                // CRITICAL: Wrap stop() in async with timeout to prevent
                // blocking
                try {
                  auto stopFuture = std::async(
                      std::launch::async, [fileNode]() { fileNode->stop(); });

                  // Wait with timeout (500ms) - if it takes too long, skip stop
                  if (stopFuture.wait_for(std::chrono::milliseconds(500)) ==
                      std::future_status::timeout) {
                    std::cerr << "[InstanceRegistry] [VideoLoop] ⚠ "
                                 "fileNode->stop() timeout (500ms), skipping..."
                              << std::endl;
                  } else {
                    try {
                      stopFuture.get();
                    } catch (...) {
                      // Ignore exceptions from stop()
                    }
                  }
                  std::this_thread::sleep_for(std::chrono::milliseconds(200));
                } catch (...) {
                  // If stop() fails, continue anyway
                }

                // CRITICAL: Wrap detach_recursively() in async with timeout
                try {
                  auto detachFuture =
                      std::async(std::launch::async, [fileNode]() {
                        fileNode->detach_recursively();
                      });

                  // Wait with timeout (1000ms) - if it takes too long, skip
                  // detach
                  if (detachFuture.wait_for(std::chrono::milliseconds(1000)) ==
                      std::future_status::timeout) {
                    std::cerr << "[InstanceRegistry] [VideoLoop] ⚠ "
                                 "fileNode->detach_recursively() timeout "
                                 "(1000ms), skipping..."
                              << std::endl;
                    // Continue anyway - try to start
                  } else {
                    try {
                      detachFuture.get();
                    } catch (...) {
                      // Ignore exceptions from detach
                    }
                  }
                } catch (...) {
                  // If detach fails, continue anyway
                }

                // CRITICAL: Longer delay to ensure GStreamer elements are fully
                // cleaned up GStreamer needs time to transition elements to
                // NULL state before dispose
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));

                // CRITICAL: Check stop flag before starting
                {
                  try {
                    std::lock_guard<std::mutex> lock(thread_mutex_);
                    auto flagIt =
                        video_loop_thread_stop_flags_.find(instanceId);
                    if (flagIt == video_loop_thread_stop_flags_.end() ||
                        flagIt->second.load()) {
                      return; // Stop flag set, exit thread
                    }
                  } catch (...) {
                    return; // Error accessing stop flag, exit thread
                  }
                }

                // Restart file source node with timeout protection
                std::cerr << "[InstanceRegistry] [VideoLoop] Restarting file "
                             "source node..."
                          << std::endl;
                try {
                  auto startFuture = std::async(
                      std::launch::async, [fileNode]() { fileNode->start(); });

                  // Wait with timeout (2000ms) - if it takes too long, skip
                  // start
                  if (startFuture.wait_for(std::chrono::milliseconds(2000)) ==
                      std::future_status::timeout) {
                    std::cerr
                        << "[InstanceRegistry] [VideoLoop] ⚠ fileNode->start() "
                           "timeout (2000ms), skipping..."
                        << std::endl;
                    std::cerr
                        << "[InstanceRegistry] [VideoLoop] Instance will "
                           "continue running, will retry restart on next check"
                        << std::endl;
                  } else {
                    try {
                      startFuture.get();
                      std::cerr << "[InstanceRegistry] [VideoLoop] ✓ File "
                                   "source node restarted successfully"
                                << std::endl;

                      // Reset hasReceivedData to allow detection of new
                      // playback
                      {
                        try {
                          std::unique_lock<std::shared_timed_mutex> lock(
                              mutex_);
                          auto instanceIt = instances_.find(instanceId);
                          if (instanceIt != instances_.end()) {
                            instanceIt->second.hasReceivedData = false;
                            // Reset instance start time for next cycle
                            instanceStartTime =
                                std::chrono::steady_clock::now();
                            hasEverReceivedData = false;
                          }
                        } catch (...) {
                          // Ignore exceptions when updating instance data
                        }
                      }
                    } catch (const std::exception &e) {
                      std::cerr << "[InstanceRegistry] [VideoLoop] ✗ Exception "
                                   "during fileNode->start(): "
                                << e.what() << std::endl;
                      std::cerr << "[InstanceRegistry] [VideoLoop] Instance "
                                   "will continue running, will retry restart "
                                   "on next check"
                                << std::endl;
                    } catch (...) {
                      std::cerr << "[InstanceRegistry] [VideoLoop] ✗ Unknown "
                                   "error during fileNode->start()"
                                << std::endl;
                      std::cerr << "[InstanceRegistry] [VideoLoop] Instance "
                                   "will continue running, will retry restart "
                                   "on next check"
                                << std::endl;
                    }
                  }
                } catch (const std::exception &e) {
                  std::cerr << "[InstanceRegistry] [VideoLoop] ✗ Exception "
                               "creating start future: "
                            << e.what() << std::endl;
                } catch (...) {
                  std::cerr << "[InstanceRegistry] [VideoLoop] ✗ Unknown error "
                               "creating start future"
                            << std::endl;
                }
              } catch (const std::exception &e) {
                std::cerr << "[InstanceRegistry] [VideoLoop] ✗ Exception "
                             "restarting file source node: "
                          << e.what() << std::endl;
                std::cerr
                    << "[InstanceRegistry] [VideoLoop] Instance will continue "
                       "running, will retry restart on next check"
                    << std::endl;
              } catch (...) {
                std::cerr << "[InstanceRegistry] [VideoLoop] ✗ Unknown error "
                             "restarting file source node"
                          << std::endl;
                std::cerr
                    << "[InstanceRegistry] [VideoLoop] Instance will continue "
                       "running, will retry restart on next check"
                    << std::endl;
              }
            }
          }
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "[InstanceRegistry] [VideoLoop] Fatal exception in video "
                   "loop thread: "
                << e.what() << std::endl;
    } catch (...) {
      std::cerr << "[InstanceRegistry] [VideoLoop] Fatal unknown error in "
                   "video loop thread"
                << std::endl;
    }
  });

  // Store thread handle
  {
    std::lock_guard<std::mutex> lock(thread_mutex_);
    video_loop_threads_[instanceId] = std::move(videoLoopThread);
  }
}

void InstanceRegistry::stopVideoLoopThread(const std::string &instanceId) {
  std::unique_lock<std::mutex> lock(thread_mutex_);

  // Set stop flag
  auto flagIt = video_loop_thread_stop_flags_.find(instanceId);
  if (flagIt != video_loop_thread_stop_flags_.end()) {
    flagIt->second.store(true);
  }

  // Get thread handle and release lock before joining
  std::thread threadToJoin;
  auto threadIt = video_loop_threads_.find(instanceId);
  if (threadIt != video_loop_threads_.end()) {
    if (threadIt->second.joinable()) {
      threadToJoin = std::move(threadIt->second);
    }
    video_loop_threads_.erase(threadIt);
  }

  // Remove stop flag
  if (flagIt != video_loop_thread_stop_flags_.end()) {
    video_loop_thread_stop_flags_.erase(flagIt);
  }

  // Release lock before joining
  lock.unlock();

  // Join thread
  if (threadToJoin.joinable()) {
    threadToJoin.join();
  }
}

Json::Value
InstanceRegistry::getInstanceConfig(const std::string &instanceId) const {
  // CRITICAL: Use shared_lock for read-only operations to allow concurrent
  // readers
  // CRITICAL: Use timeout to prevent blocking if mutex is locked
  std::shared_lock<std::shared_timed_mutex> lock(mutex_, std::defer_lock);

  // Try to acquire lock with timeout (configurable via
  // REGISTRY_MUTEX_TIMEOUT_MS)
  if (!lock.try_lock_for(TimeoutConstants::getRegistryMutexTimeout())) {
    std::cerr << "[InstanceRegistry] WARNING: getInstanceConfig() timeout - "
                 "mutex is locked, returning empty config"
              << std::endl;
    if (isInstanceLoggingEnabled()) {
      PLOG_WARNING << "[InstanceRegistry] getInstanceConfig() timeout after "
                      "2000ms - mutex may be locked by another operation";
    }
    return Json::Value(
        Json::objectValue); // Return empty object to prevent blocking
  }

  auto it = instances_.find(instanceId);
  if (it == instances_.end()) {
    return Json::Value(Json::objectValue); // Return empty object if not found
  }

  const InstanceInfo &info = it->second;
  std::string error;
  Json::Value config = instance_storage_.instanceInfoToConfigJson(info, &error);

  if (!error.empty()) {
    // Log error but still return config (might be partial)
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[InstanceRegistry] Error converting instance to config: "
                   << error;
    }
  }

  return config;
}
