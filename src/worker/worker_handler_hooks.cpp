#include "worker/worker_handler.h"
#include "worker/worker_json_utils.h"
#include "core/env_config.h"
#include "core/pipeline_builder.h"
#include "core/pipeline_builder_request_utils.h"
#include "core/pipeline_snapshot.h"
#include "core/runtime_update_log.h"
#include "core/timeout_constants.h"
#include "models/create_instance_request.h"
#include "solutions/solution_registry.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <cvedix/nodes/common/cvedix_node.h>
#include <cvedix/nodes/des/cvedix_app_des_node.h>
#include <cvedix/nodes/des/cvedix_rtmp_des_node.h>
#include <cvedix/nodes/osd/cvedix_ba_line_crossline_osd_node.h>
#include <cvedix/nodes/ba/cvedix_ba_line_crossline_node.h>
#include <cvedix/objects/shapes/cvedix_line.h>
#include <cvedix/objects/shapes/cvedix_point.h>
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
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <set>
#include <getopt.h>
#include <iostream>
#include <iomanip>
#include <opencv2/imgcodecs.hpp>
#include <sstream>
#include <thread>

namespace worker {

void WorkerHandler::setupFrameCaptureHook() {
  std::cout << "[Worker:" << instance_id_
            << "] Setting up frame capture hook..." << std::endl;

  auto pHook = getActivePipeline();
  if (!pHook || pHook->empty()) {
    std::cerr << "[Worker:" << instance_id_
              << "] ⚠ Warning: No pipeline nodes to setup frame capture hook"
              << std::endl;
    return;
  }

  std::cout << "[Worker:" << instance_id_ << "] Searching for app_des_node in "
            << pHook->size() << " pipeline nodes..." << std::endl;

  std::shared_ptr<cvedix_nodes::cvedix_app_des_node> appDesNode;
  bool hasOSDNode = false;

  const auto &nodes = pHook->nodes();
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
    auto node = *it;
    if (!node) {
      continue;
    }

    // Find app_des_node
    if (!appDesNode) {
      appDesNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_des_node>(node);
      if (appDesNode) {
        std::cout << "[Worker:" << instance_id_
                  << "] ✓ Found app_des_node in pipeline" << std::endl;
      }
    }

    // Check if pipeline has OSD node (check ALL nodes, don't stop early)
    if (!hasOSDNode) {
      bool isOSDNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_face_osd_node_v2>(
              node) != nullptr ||
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_osd_node_v3>(node) !=
              nullptr ||
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_line_crossline_osd_node>(
              node) != nullptr ||
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_area_jam_osd_node>(
              node) != nullptr ||
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_stop_osd_node>(
              node) != nullptr;
      if (isOSDNode) {
        hasOSDNode = true;
        std::cout << "[Worker:" << instance_id_
                  << "] ✓ Found OSD node in pipeline: " << typeid(*node).name()
                  << std::endl;
      }
    }
  }

  // Setup hook on app_des_node if found
  if (appDesNode) {
    std::cout << "[Worker:" << instance_id_
              << "] Configuring frame capture hook on app_des_node"
              << " (OSD node in pipeline: " << (hasOSDNode ? "yes" : "no")
              << ")" << std::endl;

    appDesNode->set_app_des_result_hooker([this, hasOSDNode](
                                              std::string /*node_name*/,
                                              std::shared_ptr<
                                                  cvedix_objects::cvedix_meta>
                                                  meta) {
      try {
        if (!meta) {
          return;
        }

        if (meta->meta_type == cvedix_objects::cvedix_meta_type::FRAME) {
          auto frame_meta =
              std::dynamic_pointer_cast<cvedix_objects::cvedix_frame_meta>(
                  meta);
          if (!frame_meta) {
            return;
          }

          // DEBUG: Log frame_meta details
          static thread_local uint64_t frame_count = 0;
          frame_count++;

          bool has_osd_frame = !frame_meta->osd_frame.empty();
          bool has_original_frame = !frame_meta->frame.empty();

          if (frame_count <= 5 || frame_count % 100 == 0) {
            std::cout << "[Worker:" << instance_id_ << "] Frame capture hook #"
                      << frame_count << " - osd_frame: "
                      << (has_osd_frame ? "available" : "empty") << " ("
                      << (has_osd_frame
                              ? std::to_string(frame_meta->osd_frame.cols) +
                                    "x" +
                                    std::to_string(frame_meta->osd_frame.rows)
                              : "0x0")
                      << ")"
                      << ", original frame: "
                      << (has_original_frame ? "available" : "empty") << " ("
                      << (has_original_frame
                              ? std::to_string(frame_meta->frame.cols) + "x" +
                                    std::to_string(frame_meta->frame.rows)
                              : "0x0")
                      << ")" << std::endl;
          }

          // CRITICAL: Only cache frames that are guaranteed to be processed
          // PipelineBuilder ensures app_des_node is attached to OSD node (if
          // exists) This guarantees frame_meta->frame is processed when
          // hasOSDNode is true
          const cv::Mat *frameToCache = nullptr;

          // Priority 1: Use osd_frame if available (always processed with AI
          // overlays)
          if (!frame_meta->osd_frame.empty()) {
            frameToCache = &frame_meta->osd_frame;
            if (frame_count <= 5) {
              std::cout << "[Worker:" << instance_id_
                        << "] Frame capture hook #" << frame_count
                        << " - Using PROCESSED osd_frame (with overlays): "
                        << frame_meta->osd_frame.cols << "x"
                        << frame_meta->osd_frame.rows << std::endl;
            }
          }
          // Priority 2: Use frame_meta->frame if OSD node exists
          // PipelineBuilder attaches app_des_node to OSD node, so
          // frame_meta->frame is processed This works even if osd_frame is
          // empty (OSD node processed frame but didn't populate osd_frame)
          else if (hasOSDNode && !frame_meta->frame.empty()) {
            frameToCache = &frame_meta->frame;
            if (frame_count <= 5) {
              std::cout
                  << "[Worker:" << instance_id_ << "] Frame capture hook #"
                  << frame_count
                  << " - Using frame_meta->frame (from OSD node, PROCESSED): "
                  << frame_meta->frame.cols << "x" << frame_meta->frame.rows
                  << std::endl;
            }
          } else {
            // Skip caching if no OSD node (frame may be unprocessed)
            static thread_local bool logged_warning = false;
            if (!logged_warning) {
              if (!hasOSDNode) {
                std::cerr
                    << "[Worker:" << instance_id_
                    << "] ⚠ WARNING: Pipeline has no OSD node. Skipping frame "
                       "cache to avoid returning unprocessed frames."
                    << std::endl;
              } else {
                std::cerr << "[Worker:" << instance_id_
                          << "] ⚠ WARNING: Both osd_frame and "
                             "frame_meta->frame are empty"
                          << std::endl;
              }
              logged_warning = true;
            }
            if (frame_count <= 5) {
              std::cout << "[Worker:" << instance_id_
                        << "] Frame capture hook #" << frame_count
                        << " - SKIPPING: "
                        << (!hasOSDNode ? "No OSD node in pipeline"
                                        : "Both frames empty")
                        << std::endl;
            }
          }

          if (frameToCache && !frameToCache->empty()) {
            updateFrameCache(*frameToCache);
          } else {
            // Log if we get empty frames (shouldn't happen but helps debug)
            static thread_local uint64_t empty_frame_count = 0;
            empty_frame_count++;
            if (empty_frame_count <= 3) {
              std::cout << "[Worker:" << instance_id_
                        << "] Frame capture hook received empty frame (count: "
                        << empty_frame_count << ")" << std::endl;
            }
          }
        }
      } catch (const std::exception &e) {
        // Throttle exception logging to avoid performance impact
        static thread_local uint64_t exception_count = 0;
        exception_count++;

        // Only log every 100th exception
        if (exception_count % 100 == 1) {
          std::cerr << "[Worker:" << instance_id_
                    << "] [ERROR] Exception in frame capture hook (count: "
                    << exception_count << "): " << e.what() << std::endl;
        }
      } catch (...) {
        static thread_local uint64_t unknown_exception_count = 0;
        unknown_exception_count++;

        if (unknown_exception_count % 100 == 1) {
          std::cerr << "[Worker:" << instance_id_
                    << "] [ERROR] Unknown exception in frame capture hook "
                       "(count: "
                    << unknown_exception_count << ")" << std::endl;
        }
      }
    });

    std::cout << "[Worker:" << instance_id_
              << "] ✓ Frame capture hook setup completed on app_des_node"
              << std::endl;
    return;
  }

  // If no app_des_node found, log warning
  if (!appDesNode) {
    std::cerr << "[Worker:" << instance_id_
              << "] ⚠ Warning: No app_des_node found in pipeline" << std::endl;
    std::cerr << "[Worker:" << instance_id_
              << "] Frame capture will not be available. "
                 "Consider adding app_des_node to pipeline."
              << std::endl;
    std::cerr << "[Worker:" << instance_id_
              << "] Pipeline should have app_des_node automatically added by "
                 "PipelineBuilder, but it wasn't found."
              << std::endl;
  }
}

void WorkerHandler::setupQueueSizeTrackingHook() {
  auto pQueue = getActivePipeline();
  if (!pQueue || pQueue->empty()) {
    return;
  }

  for (const auto &node : pQueue->nodes()) {
    if (!node) {
      continue;
    }

    try {
      node->set_meta_arriving_hooker(
          [this](std::string /*node_name*/, int queue_size,
                 std::shared_ptr<cvedix_objects::cvedix_meta> /* meta */) {
            try {
              // Update queue_size_ atomically (thread-safe)
              // Track maximum queue size seen
              size_t current_size = queue_size_.load();
              if (queue_size > static_cast<int>(current_size)) {
                queue_size_.store(static_cast<size_t>(queue_size),
                                  std::memory_order_relaxed);
              }
            } catch (const std::exception &e) {
              // Throttle exception logging to avoid performance impact
              static thread_local uint64_t exception_count = 0;
              exception_count++;
              if (exception_count % 100 == 1) {
                std::cerr << "[Worker:" << instance_id_
                          << "] [ERROR] Exception in queue size tracking hook "
                             "(count: "
                          << exception_count << "): " << e.what() << std::endl;
              }
            } catch (...) {
              // Ignore unknown exceptions
            }
          });
    } catch (const std::exception &e) {
      // Some nodes might not support hooks, ignore silently
    } catch (...) {
      // Ignore unknown exceptions
    }
  }
}

void WorkerHandler::updateFrameCache(const cv::Mat &frame) {
  // OPTIMIZATION: Use shared_ptr instead of clone() to avoid expensive memory
  // copy This eliminates ~6MB copy per frame update, significantly improving
  // FPS for multiple instances Create shared_ptr outside lock to minimize lock
  // hold time
  auto frame_ptr = std::make_shared<cv::Mat>(frame);
  auto now = std::chrono::steady_clock::now();

  {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    last_frame_ = frame_ptr; // Shared ownership - no copy!
    has_frame_ = true;
    last_frame_timestamp_ = now; // Update timestamp when frame is cached
  }
  // Lock released immediately after pointer assignment

  frames_processed_.fetch_add(1);

  // Update FPS calculation using rolling window (similar to
  // backpressure_controller) This calculates FPS based on frames processed in
  // the last 1 second
  static thread_local uint64_t frame_count_since_update = 0;
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - last_fps_update_)
                     .count();

  frame_count_since_update++;

  if (elapsed >= 1000) { // Update FPS every second
    // Calculate FPS: frames in last second / elapsed time
    double fps = (frame_count_since_update * 1000.0) / elapsed;
    current_fps_.store(std::round(fps), std::memory_order_relaxed);
    frame_count_since_update = 0;
    last_fps_update_ = now;
  }

  // Update resolution
  if (!frame.empty()) {
    resolution_ = std::to_string(frame.cols) + "x" + std::to_string(frame.rows);
  }
}

std::string WorkerHandler::encodeFrameToBase64(const cv::Mat &frame,
                                               int quality) const {
  if (frame.empty()) {
    return "";
  }

  // Encode to JPEG
  std::vector<uchar> buffer;
  std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, quality};
  if (!cv::imencode(".jpg", frame, buffer, params)) {
    return "";
  }

  // Base64 encode
  std::string result;
  result.reserve(((buffer.size() + 2) / 3) * 4);

  size_t i = 0;
  while (i < buffer.size()) {
    uint32_t octet_a = i < buffer.size() ? buffer[i++] : 0;
    uint32_t octet_b = i < buffer.size() ? buffer[i++] : 0;
    uint32_t octet_c = i < buffer.size() ? buffer[i++] : 0;

    uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

    result += worker::kBase64Chars[(triple >> 18) & 0x3F];
    result += worker::kBase64Chars[(triple >> 12) & 0x3F];
    result += worker::kBase64Chars[(triple >> 6) & 0x3F];
    result += worker::kBase64Chars[triple & 0x3F];
  }

  // Add padding
  size_t padding = (3 - (buffer.size() % 3)) % 3;
  for (size_t p = 0; p < padding; ++p) {
    result[result.size() - 1 - p] = '=';
  }

  return result;
}

} // namespace worker
