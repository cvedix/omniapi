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

IPCMessage WorkerHandler::handlePushFrame(const IPCMessage &msg) {
  IPCMessage response;
  response.type = MessageType::PUSH_FRAME_RESPONSE;

  if (!msg.payload.isMember("frame_base64") || !msg.payload["frame_base64"].isString()) {
    response.payload = createErrorResponse("No frame_base64 string in payload",
                                           ResponseStatus::INVALID_REQUEST);
    return response;
  }

  std::string codec = "jpeg";
  if (msg.payload.isMember("codec") && msg.payload["codec"].isString()) {
    codec = msg.payload["codec"].asString();
  }
  std::transform(codec.begin(), codec.end(), codec.begin(), ::tolower);

  if (!pipeline_running_.load() || !getActivePipeline() || getActivePipeline()->empty()) {
    response.payload = createErrorResponse("Pipeline not running",
                                           ResponseStatus::ERROR);
    return response;
  }

  std::string b64 = msg.payload["frame_base64"].asString();
  std::vector<uint8_t> data = worker::base64Decode(b64);
  if (data.empty()) {
    response.payload = createErrorResponse("Invalid base64 frame data",
                                           ResponseStatus::INVALID_REQUEST);
    return response;
  }

  cv::Mat frame;
  if (codec == "jpeg" || codec == "jpg" || codec == "png" || codec == "webp") {
    frame = cv::imdecode(data, cv::IMREAD_COLOR);
  } else {
    std::cerr << "[Worker:" << instance_id_
              << "] PUSH_FRAME: Only jpeg/png supported, got codec=" << codec
              << std::endl;
    response.payload = createErrorResponse(
        "Only jpeg/png/webp codec supported for push frame in worker",
        ResponseStatus::INVALID_REQUEST);
    return response;
  }

  if (frame.empty()) {
    response.payload = createErrorResponse("Failed to decode image",
                                           ResponseStatus::INTERNAL_ERROR);
    return response;
  }

  std::shared_ptr<cvedix_nodes::cvedix_app_src_node> appSrcNode;
  auto pPush = getActivePipeline();
  if (pPush) {
    for (const auto &node : pPush->nodes()) {
      if (!node) continue;
      appSrcNode = std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_src_node>(node);
      if (appSrcNode) break;
    }
  }

  if (!appSrcNode) {
    response.payload = createErrorResponse("No app_src node in pipeline",
                                           ResponseStatus::NOT_FOUND);
    return response;
  }

  try {
    appSrcNode->push_frames(frame);
    response.payload = createResponse(ResponseStatus::OK, "Frame pushed");
  } catch (const std::exception &e) {
    std::cerr << "[Worker:" << instance_id_
              << "] PUSH_FRAME push_frames exception: " << e.what() << std::endl;
    response.payload = createErrorResponse(std::string("push_frames failed: ") + e.what(),
                                           ResponseStatus::INTERNAL_ERROR);
  }
  return response;
}

IPCMessage WorkerHandler::handleGetStatus(const IPCMessage & /*msg*/) {
  IPCMessage response;
  response.type = MessageType::GET_INSTANCE_STATUS_RESPONSE;

  // Use shared_lock to allow concurrent reads - never blocks other readers
  // Writers (state updates) still get exclusive access
  std::string state_copy;
  std::string error_copy;
  {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    state_copy = current_state_;
    error_copy = last_error_;
  }

  Json::Value data;
  data["instance_id"] = instance_id_;
  data["state"] = state_copy;
  data["running"] = pipeline_running_.load();
  data["has_pipeline"] = (getActivePipeline() && !getActivePipeline()->empty());
  if (!error_copy.empty()) {
    data["last_error"] = error_copy;
  }

  response.payload = createResponse(ResponseStatus::OK, "", data);
  return response;
}

IPCMessage WorkerHandler::handleGetStatistics(const IPCMessage & /*msg*/) {
  auto handle_start = std::chrono::steady_clock::now();
  std::cout << "[Worker:" << instance_id_
            << "] ===== handleGetStatistics START =====" << std::endl;
  std::cout << "[Worker:" << instance_id_ << "] Received GET_STATISTICS request"
            << std::endl;

  IPCMessage response;
  response.type = MessageType::GET_STATISTICS_RESPONSE;

  // CRITICAL: Check if pipeline is running before returning statistics
  // Statistics are only meaningful when pipeline is actively processing frames
  if (!pipeline_running_.load()) {
    std::cout << "[Worker:" << instance_id_
              << "] GET_STATISTICS: Pipeline not running, returning error"
              << std::endl;
    response.payload =
        createErrorResponse("Pipeline not running", ResponseStatus::NOT_FOUND);
    return response;
  }

  auto now = std::chrono::steady_clock::now();
  auto uptime =
      std::chrono::duration_cast<std::chrono::seconds>(now - start_time_)
          .count();
  auto start_unix = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();

  // Use shared_lock to allow concurrent reads - never blocks other readers
  // Multiple GET_STATISTICS requests can run simultaneously
  // Writers (state updates) still get exclusive access
  std::string state_copy;
  {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    state_copy = current_state_;
  }

  // Get source framerate and resolution from source node (if available)
  // CRITICAL: Use timeout protection to prevent blocking when source node is
  // busy This prevents API from hanging when pipeline is overloaded (queue
  // full)
  double source_fps = 0.0;
  std::string source_res = "";

  // Try to get source node from active pipeline (lock-free read via getActivePipeline)
  auto pStats = getActivePipeline();
  if (pStats && !pStats->empty()) {
    try {
      auto sourceNode = pStats->sourceNode();
      if (sourceNode) {
        auto rtspNode =
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(
                sourceNode);
        auto rtmpNode =
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(
                sourceNode);
        auto fileNode =
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(
                sourceNode);
        auto imageNode =
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_image_src_node>(
                sourceNode);
        auto udpNode =
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_udp_src_node>(
                sourceNode);

        // Use async with timeout to prevent blocking when source node is busy
        // Timeout: 100ms (fast enough for API response, prevents hanging)
        const auto source_info_timeout = std::chrono::milliseconds(100);

        if (rtspNode) {
          // Get source framerate and resolution from RTSP node with timeout
          auto sourceInfoFuture = std::async(
              std::launch::async,
              [rtspNode]() -> std::pair<int, std::pair<int, int>> {
                try {
                  int fps = rtspNode->get_original_fps();
                  int width = rtspNode->get_original_width();
                  int height = rtspNode->get_original_height();
                  return std::make_pair(fps, std::make_pair(width, height));
                } catch (...) {
                  return std::make_pair(0, std::make_pair(0, 0));
                }
              });

          auto status = sourceInfoFuture.wait_for(source_info_timeout);
          if (status == std::future_status::ready) {
            try {
              auto [fps_int, dimensions] = sourceInfoFuture.get();
              auto [width, height] = dimensions;

              if (fps_int > 0) {
                source_fps = static_cast<double>(fps_int);
              }

              if (width > 0 && height > 0) {
                source_res =
                    std::to_string(width) + "x" + std::to_string(height);
                // Update member variable for next time
                {
                  std::lock_guard<std::shared_mutex> lock(state_mutex_);
                  source_resolution_ = source_res;
                }
              }
            } catch (...) {
              // Ignore exceptions from get()
            }
          } else {
            // Timeout - use cached values or defaults
            // This prevents API from hanging when source node is busy
          }
        } else if (rtmpNode) {
          auto sourceInfoFuture = std::async(
              std::launch::async,
              [rtmpNode]() -> std::pair<int, std::pair<int, int>> {
                try {
                  int fps = rtmpNode->get_original_fps();
                  int width = rtmpNode->get_original_width();
                  int height = rtmpNode->get_original_height();
                  return std::make_pair(fps, std::make_pair(width, height));
                } catch (...) {
                  return std::make_pair(0, std::make_pair(0, 0));
                }
              });

          auto status = sourceInfoFuture.wait_for(source_info_timeout);
          if (status == std::future_status::ready) {
            try {
              auto [fps_int, dimensions] = sourceInfoFuture.get();
              auto [width, height] = dimensions;

              if (fps_int > 0) {
                source_fps = static_cast<double>(fps_int);
              }

              if (width > 0 && height > 0) {
                source_res =
                    std::to_string(width) + "x" + std::to_string(height);
                {
                  std::lock_guard<std::shared_mutex> lock(state_mutex_);
                  source_resolution_ = source_res;
                }
              }
            } catch (...) {
              // Ignore exceptions from get()
            }
          }
        } else if (fileNode) {
          // File source inherits from cvedix_src_node, so it has
          // get_original_fps/width/height methods
          auto sourceInfoFuture = std::async(
              std::launch::async,
              [fileNode]() -> std::pair<int, std::pair<int, int>> {
                try {
                  int fps = fileNode->get_original_fps();
                  int width = fileNode->get_original_width();
                  int height = fileNode->get_original_height();
                  return std::make_pair(fps, std::make_pair(width, height));
                } catch (...) {
                  return std::make_pair(0, std::make_pair(0, 0));
                }
              });

          auto status = sourceInfoFuture.wait_for(source_info_timeout);
          if (status == std::future_status::ready) {
            try {
              auto [fps_int, dimensions] = sourceInfoFuture.get();
              auto [width, height] = dimensions;

              if (fps_int > 0) {
                source_fps = static_cast<double>(fps_int);
              }

              if (width > 0 && height > 0) {
                source_res =
                    std::to_string(width) + "x" + std::to_string(height);
                // Update member variable for next time
                {
                  std::lock_guard<std::shared_mutex> lock(state_mutex_);
                  source_resolution_ = source_res;
                }
              }
            } catch (...) {
              // Ignore exceptions from get()
            }
          } else {
            // Timeout - use cached values or defaults
          }
        } else if (imageNode) {
          auto sourceInfoFuture = std::async(
              std::launch::async,
              [imageNode]() -> std::pair<int, std::pair<int, int>> {
                try {
                  int fps = imageNode->get_original_fps();
                  int width = imageNode->get_original_width();
                  int height = imageNode->get_original_height();
                  return std::make_pair(fps, std::make_pair(width, height));
                } catch (...) {
                  return std::make_pair(0, std::make_pair(0, 0));
                }
              });
          auto status = sourceInfoFuture.wait_for(source_info_timeout);
          if (status == std::future_status::ready) {
            try {
              auto [fps_int, dimensions] = sourceInfoFuture.get();
              auto [width, height] = dimensions;
              if (fps_int > 0) source_fps = static_cast<double>(fps_int);
              if (width > 0 && height > 0) {
                source_res = std::to_string(width) + "x" + std::to_string(height);
                std::lock_guard<std::shared_mutex> lock(state_mutex_);
                source_resolution_ = source_res;
              }
            } catch (...) {}
          }
        } else if (udpNode) {
          auto sourceInfoFuture = std::async(
              std::launch::async,
              [udpNode]() -> std::pair<int, std::pair<int, int>> {
                try {
                  int fps = udpNode->get_original_fps();
                  int width = udpNode->get_original_width();
                  int height = udpNode->get_original_height();
                  return std::make_pair(fps, std::make_pair(width, height));
                } catch (...) {
                  return std::make_pair(0, std::make_pair(0, 0));
                }
              });
          auto status = sourceInfoFuture.wait_for(source_info_timeout);
          if (status == std::future_status::ready) {
            try {
              auto [fps_int, dimensions] = sourceInfoFuture.get();
              auto [width, height] = dimensions;
              if (fps_int > 0) source_fps = static_cast<double>(fps_int);
              if (width > 0 && height > 0) {
                source_res = std::to_string(width) + "x" + std::to_string(height);
                std::lock_guard<std::shared_mutex> lock(state_mutex_);
                source_resolution_ = source_res;
              }
            } catch (...) {}
          }
        }
      }
    } catch (const std::exception &e) {
      // If APIs are not available or throw exceptions, use defaults
      std::cerr << "[Worker:" << instance_id_
                << "] Error getting source info: " << e.what() << std::endl;
    } catch (...) {
      // Ignore unknown exceptions
    }
  }

  // Use source_resolution_ member if source_res is empty (fallback)
  if (source_res.empty() && !source_resolution_.empty()) {
    source_res = source_resolution_;
  }

  // Calculate latency (average time per frame in milliseconds)
  double current_fps_value = current_fps_.load();
  double latency = 0.0;
  uint64_t frames_processed_value = frames_processed_.load();
  if (frames_processed_value > 0 && current_fps_value > 0.0) {
    latency = std::round(1000.0 / current_fps_value);
  }

  // Log statistics summary for debugging
  auto handle_end = std::chrono::steady_clock::now();
  auto handle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                             handle_end - handle_start)
                             .count();
  std::cout << "[Worker:" << instance_id_ << "] GET_STATISTICS: "
            << "frames_processed=" << frames_processed_value
            << ", current_fps=" << current_fps_value
            << ", source_fps=" << source_fps
            << ", queue_size=" << queue_size_.load() << ", state=" << state_copy
            << " (duration: " << handle_duration << "ms)" << std::endl;

  Json::Value data;
  data["instance_id"] = instance_id_;
  data["frames_processed"] = static_cast<Json::UInt64>(frames_processed_value);
  data["dropped_frames_count"] =
      static_cast<Json::UInt64>(dropped_frames_.load());
  data["start_time"] = static_cast<Json::Int64>(start_unix - uptime);
  data["current_framerate"] = current_fps_value;
  data["source_framerate"] = source_fps > 0.0 ? source_fps : current_fps_value;
  data["latency"] = latency;
  data["input_queue_size"] = static_cast<Json::UInt64>(queue_size_.load());
  data["resolution"] = resolution_.empty() ? source_res : resolution_;
  data["source_resolution"] =
      source_res.empty() ? source_resolution_ : source_res;
  data["format"] = "BGR";
  data["state"] = state_copy;

  // Add diagnostic info when statistics are empty (pipeline running but no
  // frames processed yet)
  if (frames_processed_value == 0 && current_fps_value == 0.0) {
    data["diagnostic"] =
        "Pipeline is running but no frames have been processed yet. "
        "This may be normal if the instance just started or the source is not "
        "providing frames.";
    std::cout << "[Worker:" << instance_id_
              << "] GET_STATISTICS: Warning - No frames processed yet. "
              << "Pipeline state: " << state_copy
              << ", Queue size: " << queue_size_.load()
              << ", Uptime: " << uptime << " seconds" << std::endl;
  }

  response.payload = createResponse(ResponseStatus::OK, "", data);
  return response;
}

IPCMessage WorkerHandler::handleGetLastFrame(const IPCMessage & /*msg*/) {
  std::cout << "[Worker:" << instance_id_ << "] handleGetLastFrame() called"
            << std::endl;

  IPCMessage response;
  response.type = MessageType::GET_LAST_FRAME_RESPONSE;

  Json::Value data;
  data["instance_id"] = instance_id_;

  std::lock_guard<std::mutex> lock(frame_mutex_);

  std::cout << "[Worker:" << instance_id_
            << "] handleGetLastFrame() - Frame cache state: "
            << "has_frame_=" << has_frame_
            << ", last_frame_=" << (last_frame_ ? "valid" : "null")
            << std::endl;

  if (has_frame_ && last_frame_ && !last_frame_->empty()) {
    // Check if frame is stale (older than configured threshold)
    // This ensures we only return recent frames from active video streams
    auto now = std::chrono::steady_clock::now();
    auto frame_age = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_frame_timestamp_);
    const int MAX_FRAME_AGE_SECONDS = TimeoutConstants::getMaxFrameAgeSeconds();

    if (frame_age.count() > MAX_FRAME_AGE_SECONDS) {
      std::cout << "[Worker:" << instance_id_
                << "] handleGetLastFrame() - Frame is stale (age: "
                << frame_age.count()
                << " seconds, max: " << MAX_FRAME_AGE_SECONDS
                << " seconds), returning empty" << std::endl;
      data["frame"] = "";
      data["has_frame"] = false;
    } else {
      std::cout << "[Worker:" << instance_id_
                << "] handleGetLastFrame() - Frame available: "
                << "size=" << last_frame_->cols << "x" << last_frame_->rows
                << ", channels=" << last_frame_->channels()
                << ", type=" << last_frame_->type()
                << ", age=" << frame_age.count() << " seconds" << std::endl;

      std::cout << "[Worker:" << instance_id_
                << "] handleGetLastFrame() - Encoding frame to base64..."
                << std::endl;
      std::string encoded = encodeFrameToBase64(*last_frame_);
      data["frame"] = encoded;
      data["has_frame"] = true;

      std::cout << "[Worker:" << instance_id_
                << "] handleGetLastFrame() - Frame encoded: "
                << "size=" << encoded.length() << " chars (base64)"
                << std::endl;
    }
  } else {
    std::cout << "[Worker:" << instance_id_
              << "] handleGetLastFrame() - No frame available" << std::endl;
    if (!has_frame_) {
      std::cout << "[Worker:" << instance_id_
                << "] handleGetLastFrame() - Reason: has_frame_=false"
                << std::endl;
    } else if (!last_frame_) {
      std::cout << "[Worker:" << instance_id_
                << "] handleGetLastFrame() - Reason: last_frame_ is null"
                << std::endl;
    } else if (last_frame_->empty()) {
      std::cout << "[Worker:" << instance_id_
                << "] handleGetLastFrame() - Reason: last_frame_ is empty"
                << std::endl;
    }
    data["frame"] = "";
    data["has_frame"] = false;
  }

  response.payload = createResponse(ResponseStatus::OK, "", data);
  std::cout << "[Worker:" << instance_id_
            << "] handleGetLastFrame() - Response prepared: "
            << "has_frame=" << data["has_frame"].asBool() << std::endl;
  return response;
}

} // namespace worker
