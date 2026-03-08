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
int InstanceRegistry::checkAndHandleRetryLimits() {
  // CRITICAL: Collect instances to stop while holding lock, then release lock
  // before calling stopInstance() This prevents deadlock because stopInstance()
  // needs exclusive lock
  std::vector<std::string>
      instancesToStop; // Collect instances to stop while holding lock
  int stoppedCount = 0;
  auto now = std::chrono::steady_clock::now();

  {
    // Use exclusive lock for write operations (updating retry counts, marking
    // instances as stopped) This will block readers (getAllInstances) only when
    // actually writing
    std::unique_lock<std::shared_timed_mutex> lock(mutex_);

    // Check all running instances
    for (auto &[instanceId, info] : instances_) {
      if (!info.running || info.retryLimitReached) {
        continue; // Skip non-running instances or already stopped due to retry
                  // limit
      }

      // Check if this is an RTSP instance (has RTSP URL)
      if (!info.rtspUrl.empty()) {
        // Get pipeline to check if RTSP node exists
        auto pipelineIt = pipelines_.find(instanceId);
        if (pipelineIt != pipelines_.end() && !pipelineIt->second.empty()) {
          auto rtspNode =
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(
                  pipelineIt->second[0]);
          if (rtspNode) {
            // Calculate time since instance started
            auto timeSinceStart =
                std::chrono::duration_cast<std::chrono::seconds>(now -
                                                                 info.startTime)
                    .count();

            // Calculate time since last activity (or since start if no
            // activity)
            auto timeSinceActivity =
                std::chrono::duration_cast<std::chrono::seconds>(
                    now - info.lastActivityTime)
                    .count();

            // Only increment retry counter if:
            // 1. Instance has been running for at least 60 seconds (give it
            // more time to connect and stabilize)
            // 2. AND instance has not received any data yet (hasReceivedData =
            // false)
            // 3. OR instance has been running for more than 90 seconds without
            // activity (after receiving data) Note: Increased timeout from 30s
            // to 60s to account for RTSP connection time and fps update delay
            bool isLikelyRetrying = false;
            if (timeSinceStart >= 60) {
              if (!info.hasReceivedData) {
                // Instance has been running for 60+ seconds without receiving
                // any data This indicates it's likely stuck in retry loop
                isLikelyRetrying = true;
              } else if (timeSinceActivity > 90) {
                // Instance received data before but now has been inactive for
                // 90+ seconds This might indicate connection was lost and
                // retrying
                isLikelyRetrying = true;
              }
            }

            if (isLikelyRetrying) {
              // Increment retry counter only when we detect retry is happening
              info.retryCount++;

              std::cerr << "[InstanceRegistry] Instance " << instanceId
                        << " retry detected: count=" << info.retryCount << "/"
                        << info.maxRetryCount << ", running=" << timeSinceStart
                        << "s"
                        << ", no_data="
                        << (!info.hasReceivedData ? "yes" : "no")
                        << ", inactive=" << timeSinceActivity << "s"
                        << std::endl;

              // Check if retry limit reached
              if (info.retryCount >= info.maxRetryCount) {
                info.retryLimitReached = true;
                std::cerr << "[InstanceRegistry] ⚠ Instance " << instanceId
                          << " reached retry limit (" << info.maxRetryCount
                          << " retries) after " << timeSinceStart
                          << " seconds - stopping instance" << std::endl;
                PLOG_WARNING << "[Instance] Instance " << instanceId
                             << " reached retry limit - stopping";

                // Mark as not running (will be stopped outside lock)
                info.running = false;
                instancesToStop.push_back(
                    instanceId); // Collect for stopping outside lock
                stoppedCount++;
              }
            } else {
              // Check if instance is receiving data
              // Note: fps may not be updated from pipeline, so we use a more
              // lenient approach If instance has been running for a while
              // without errors, assume it's working
              bool isReceivingData = false;

              // Method 1: Check fps (if available from pipeline)
              if (info.fps > 0) {
                isReceivingData = true;
              }
              // Method 2: If instance has been running for 45+ seconds without
              // being marked as retrying, assume it's working (RTSP connection
              // established, even if fps not updated) This gives time for RTSP
              // to connect (10-30s) and stabilize before retry detection starts
              // (60s)
              else if (timeSinceStart >= 45 && info.retryCount == 0) {
                // Instance has been running for 45+ seconds without retry
                // detection This likely means RTSP connection is established
                // and working (retry detection only starts at 60s, so 45s is
                // safe)
                isReceivingData = true;
              }

              if (isReceivingData) {
                // Instance is receiving frames - mark as having received data
                if (!info.hasReceivedData) {
                  std::cerr << "[InstanceRegistry] Instance " << instanceId
                            << " connection successful - receiving frames";
                  if (info.fps > 0) {
                    std::cerr << " (fps=" << std::fixed << std::setprecision(2)
                              << info.fps << ")";
                  } else {
                    std::cerr << " (running for " << timeSinceStart
                              << "s, assumed working)";
                  }
                  std::cerr << std::endl;
                  info.hasReceivedData = true;
                }
                // Update last activity time when receiving frames
                info.lastActivityTime = now;

                // Reset retry counter if instance is successfully receiving
                // data
                if (info.retryCount > 0) {
                  std::cerr
                      << "[InstanceRegistry] Instance " << instanceId
                      << " connection successful - resetting retry counter"
                      << std::endl;
                  info.retryCount = 0;
                }
              } else {
                // Debug: Log when RTSP is connected but no frames received
                if (timeSinceStart > 5 && timeSinceStart < 35) {
                  // Only log once every 5 seconds to avoid spam
                  static std::map<std::string,
                                  std::chrono::steady_clock::time_point>
                      lastLogTime;
                  auto lastLog = lastLogTime.find(instanceId);
                  bool shouldLog = false;
                  if (lastLog == lastLogTime.end()) {
                    shouldLog = true;
                    lastLogTime[instanceId] = now;
                  } else {
                    auto timeSinceLastLog =
                        std::chrono::duration_cast<std::chrono::seconds>(
                            now - lastLog->second)
                            .count();
                    if (timeSinceLastLog >= 5) {
                      shouldLog = true;
                      lastLogTime[instanceId] = now;
                    }
                  }
                  if (shouldLog) {
                    std::cerr << "[InstanceRegistry] Instance " << instanceId
                              << " RTSP connected but no frames received yet "
                                 "(running="
                              << timeSinceStart << "s, fps=" << info.fps
                              << "). This may be normal - RTSP streams can "
                                 "take 10-30 seconds to stabilize."
                              << std::endl;
                  }
                }
              }
            }
          }
        }
      }
    }
  } // CRITICAL: Release lock before calling stopInstance() to avoid deadlock

  // Stop instances that reached retry limit (do this outside lock to avoid
  // deadlock) stopInstance() needs exclusive lock, so we must release our lock
  // first
  for (const auto &instanceId : instancesToStop) {
    try {
      stopInstance(instanceId);
      std::cerr << "[InstanceRegistry] ✓ Stopped instance " << instanceId
                << " due to retry limit" << std::endl;
    } catch (const std::exception &e) {
      std::cerr << "[InstanceRegistry] ✗ Failed to stop instance " << instanceId
                << " due to retry limit: " << e.what() << std::endl;
    } catch (...) {
      std::cerr << "[InstanceRegistry] ✗ Failed to stop instance " << instanceId
                << " due to retry limit (unknown error)" << std::endl;
    }
  }

  return stoppedCount;
}
