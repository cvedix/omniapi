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

void InstanceRegistry::setupRTMPDestinationActivityHook(
    const std::string &instanceId,
    const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes) {
  if (nodes.empty()) {
    return;
  }

  for (const auto &node : nodes) {
    if (!node) {
      continue;
    }
    auto rtmpDesNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node);
    if (!rtmpDesNode) {
      continue;
    }

    // Update RTMP destination activity when SDK reports stream status
    // (frame actually pushed to RTMP). This makes monitor reflect real output.
    rtmpDesNode->set_stream_status_hooker(
        [this, instanceId](std::string /*node_name*/,
                           cvedix_nodes::cvedix_stream_status /*status*/) {
          updateRTMPDestinationActivity(instanceId);
        });

    // Fallback: also update when frame meta is handled by rtmp_des node
    // (in case SDK does not invoke stream_status every time)
    rtmpDesNode->set_meta_handled_hooker(
        [this, instanceId](std::string /*node_name*/, int /*queue_size*/,
                           std::shared_ptr<cvedix_objects::cvedix_meta> meta) {
          if (meta && meta->meta_type ==
                          cvedix_objects::cvedix_meta_type::FRAME) {
            updateRTMPDestinationActivity(instanceId);
          }
        });

    std::cerr << "[InstanceRegistry] ✓ RTMP destination activity hook set for "
                 "instance "
              << instanceId << std::endl;
  }
}

void InstanceRegistry::setupQueueSizeTrackingHook(
    const std::string &instanceId,
    const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes) {
  if (nodes.empty()) {
    return;
  }

  // Setup meta_arriving_hooker on all nodes to track input queue size
  // On source node (first node), also track incoming frames
  std::cout << "[InstanceRegistry] Setting up queue size tracking hooks for "
            << nodes.size() << " nodes" << std::endl;
  std::cout.flush();

  for (size_t i = 0; i < nodes.size(); ++i) {
    const auto &node = nodes[i];
    if (!node) {
      continue;
    }

    const bool isSourceNode = (i == 0); // First node is source node

    if (isSourceNode) {
      std::cout << "[InstanceRegistry] Setting up hook on source node (index "
                << i << ") to track incoming frames" << std::endl;
      std::cout.flush();
    }

    try {
      node->set_meta_arriving_hooker([this, instanceId, isSourceNode](
                                         std::string /*node_name*/,
                                         int queue_size,
                                         std::shared_ptr<
                                             cvedix_objects::cvedix_meta>
                                             meta) {
        try {
          // OPTIMIZED: Use try_lock to avoid blocking frame processing
          // If lock is busy (e.g., another instance is starting), skip this
          // update Queue size tracking is not critical - missing one update
          // is acceptable
          std::unique_lock<std::shared_timed_mutex> lock(mutex_,
                                                         std::try_to_lock);
          if (!lock.owns_lock()) {
            // Lock is busy - skip this update to avoid blocking frame
            // processing This allows instances to process frames even when
            // other instances are starting
            return;
          }

          auto trackerIt = statistics_trackers_.find(instanceId);
          if (trackerIt != statistics_trackers_.end()) {
            InstanceStatsTracker &tracker = trackerIt->second;

            // Track incoming frames on source node (BEFORE frames can be
            // dropped)
            if (isSourceNode && meta) {
              // Log all meta types on source node for debugging (first 10 only)
              static thread_local std::unordered_map<std::string, uint64_t>
                  instance_source_meta_counts;
              instance_source_meta_counts[instanceId]++;
              if (instance_source_meta_counts[instanceId] <= 10) {
                std::cout << "[InstanceRegistry] Source node "
                             "meta_arriving_hooker called for instance "
                          << instanceId
                          << ": meta_type=" << static_cast<int>(meta->meta_type)
                          << ", queue_size=" << queue_size << " (call #"
                          << instance_source_meta_counts[instanceId] << ")"
                          << std::endl;
              }

              // Only count FRAME meta types for frames_incoming
              if (meta->meta_type == cvedix_objects::cvedix_meta_type::FRAME) {
                uint64_t new_incoming_count =
                    tracker.frames_incoming.fetch_add(
                        1, std::memory_order_relaxed) +
                    1;

                // Log first few incoming frames for debugging
                static thread_local std::unordered_map<std::string, uint64_t>
                    instance_incoming_counts;
                instance_incoming_counts[instanceId]++;
                if (instance_incoming_counts[instanceId] <= 5 ||
                    instance_incoming_counts[instanceId] % 100 == 0) {
                  std::cout << "[InstanceRegistry] Frame incoming for instance "
                            << instanceId
                            << ": incoming_count=" << new_incoming_count
                            << " (total calls: "
                            << instance_incoming_counts[instanceId] << ")"
                            << std::endl;
                }

                // Get dropped frames from backpressure controller
                using namespace BackpressureController;
                auto &backpressure = BackpressureController::
                    BackpressureController::getInstance();
                auto backpressureStats = backpressure.getStats(instanceId);

                // Update dropped_frames from backpressure controller (most
                // accurate)
                uint64_t dropped_from_backpressure =
                    backpressureStats.frames_dropped;
                if (dropped_from_backpressure >
                    tracker.dropped_frames.load(std::memory_order_relaxed)) {
                  tracker.dropped_frames.store(dropped_from_backpressure,
                                               std::memory_order_relaxed);
                }
              }
            }

            // Track queue size on all nodes
            // FIX: Update queue size every time hook is called, not only when
            // increasing This allows tracking actual queue size including when
            // it decreases
            tracker.current_queue_size = static_cast<size_t>(queue_size);

            if (queue_size > static_cast<int>(tracker.max_queue_size_seen)) {
              tracker.max_queue_size_seen = static_cast<size_t>(queue_size);
            }

            // PHASE 3: Update queue size in backpressure controller for
            // queue-based frame dropping (no lock needed - singleton)
            using namespace BackpressureController;
            auto &backpressure =
                BackpressureController::BackpressureController::getInstance();
            backpressure.updateQueueSize(
                instanceId,
                static_cast<size_t>(queue_size)); // Thread-safe, no lock needed

            // Record queue full event if queue is getting full (threshold:
            // 80% of typical max) Increased from 8 to 16 to match increased
            // queue size (20) for multi-instance performance
            const size_t queue_warning_threshold =
                16; // Warn at 16 frames (80% of 20)
            if (queue_size >= static_cast<int>(queue_warning_threshold)) {
              backpressure.recordQueueFull(
                  instanceId); // Thread-safe, no lock needed
            }

            // FIX: Track SDK-level drops when queue is full
            // When queue is at max capacity, frames are being dropped at SDK
            // level Estimate drops based on queue full state This helps track
            // dropped_frames_count when SDK drops frames before they reach the
            // hook
            size_t max_queue_size_estimated =
                51; // Based on log analysis (input_queue_size=51)
            if (queue_size >= static_cast<int>(max_queue_size_estimated)) {
              // Queue is at max - frames are likely being dropped at SDK level
              // Increment dropped_frames to reflect SDK-level drops
              // Note: This is an estimate, actual drops may be higher
              static thread_local std::unordered_map<
                  std::string, std::chrono::steady_clock::time_point>
                  last_drop_time;
              auto now = std::chrono::steady_clock::now();
              auto it = last_drop_time.find(instanceId);

              // Throttle: Only count drops every 100ms to avoid over-counting
              if (it == last_drop_time.end() ||
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      now - it->second)
                          .count() >= 100) {
                // Estimate 1 drop per check when queue is full (conservative
                // estimate)
                tracker.dropped_frames.fetch_add(1, std::memory_order_relaxed);
                last_drop_time[instanceId] = now;
              }
            }
          }
        } catch (const std::exception &e) {
          std::cerr << "[InstanceRegistry] [ERROR] Exception in queue size "
                       "tracking hook: "
                    << e.what() << std::endl;
        } catch (...) {
          std::cerr << "[InstanceRegistry] [ERROR] Unknown exception in "
                       "queue size tracking hook"
                    << std::endl;
        }
      });
    } catch (const std::exception &e) {
      // Some nodes might not support hooks, ignore silently
    } catch (...) {
      // Some nodes might not support hooks, ignore silently
    }
  }

  std::cerr << "[InstanceRegistry] ✓ Queue size tracking hook setup completed "
               "for instance: "
            << instanceId << std::endl;
}

std::string InstanceRegistry::encodeFrameToBase64(const cv::Mat &frame,
                                                  int jpegQuality) const {
  if (frame.empty()) {
    return "";
  }

  try {
    std::vector<uchar> buffer;
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, jpegQuality};

    if (!cv::imencode(".jpg", frame, buffer, params)) {
      std::cerr << "[InstanceRegistry] Failed to encode frame to JPEG"
                << std::endl;
      return "";
    }

    if (buffer.empty()) {
      return "";
    }

    return base64_encode(buffer.data(), buffer.size());
  } catch (const std::exception &e) {
    std::cerr << "[InstanceRegistry] Exception encoding frame to base64: "
              << e.what() << std::endl;
    return "";
  } catch (...) {
    std::cerr << "[InstanceRegistry] Unknown exception encoding frame to base64"
              << std::endl;
    return "";
  }
}
