#include "worker/worker_handler.h"
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

// Deep-merge source into target (objects merged recursively, other types replace).
// Body gửi gì thì update cái đó, còn lại giữ nguyên: only keys present in source
// are updated; target keys not in source are left unchanged.
static void mergeJsonInto(Json::Value &target, const Json::Value &source) {
  if (!source.isObject()) return;
  for (const auto &key : source.getMemberNames()) {
    const Json::Value &srcVal = source[key];
    if (srcVal.isObject() && target.isMember(key) && target[key].isObject()) {
      mergeJsonInto(target[key], srcVal);
    } else {
      target[key] = srcVal;
    }
  }
}

// Base64 encoding table
static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Base64 decode (returns empty vector on error)
static std::vector<uint8_t> base64_decode(const std::string &encoded) {
  std::vector<uint8_t> out;
  if (encoded.empty()) return out;
  std::string safe;
  for (char c : encoded) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=')
      safe += c;
  }
  size_t len = safe.size();
  if (len == 0 || (len % 4) != 0) return out;
  out.reserve((len / 4) * 3);
  for (size_t i = 0; i < len; i += 4) {
    auto idx = [&safe](size_t j) -> int {
      char c = safe[j];
      if (c >= 'A' && c <= 'Z') return c - 'A';
      if (c >= 'a' && c <= 'z') return c - 'a' + 26;
      if (c >= '0' && c <= '9') return c - '0' + 52;
      if (c == '+') return 62;
      if (c == '/') return 63;
      return -1;
    };
    if (safe[i] == '=' || safe[i + 1] == '=') break;
    int n0 = idx(i), n1 = idx(i + 1), n2 = (safe[i + 2] == '=') ? -1 : idx(i + 2),
        n3 = (safe[i + 3] == '=') ? -1 : idx(i + 3);
    if (n0 < 0 || n1 < 0) break;
    out.push_back(static_cast<uint8_t>((n0 << 2) | (n1 >> 4)));
    if (n2 >= 0) {
      out.push_back(static_cast<uint8_t>(((n1 & 15) << 4) | (n2 >> 2)));
      if (n3 >= 0)
        out.push_back(static_cast<uint8_t>(((n2 & 3) << 6) | n3));
    }
  }
  return out;
}

namespace worker {

WorkerHandler::WorkerHandler(const std::string &instance_id,
                             const std::string &socket_path,
                             const Json::Value &config)
    : instance_id_(instance_id), socket_path_(socket_path), config_(config) {
  start_time_ = std::chrono::steady_clock::now();
  last_fps_update_ = start_time_;
}

WorkerHandler::~WorkerHandler() {
  // Request shutdown first
  shutdown_requested_.store(true);

  // Wait for start pipeline thread to finish if running
  // Use condition variable to wait for completion naturally
  if (starting_pipeline_.load()) {
    if (start_pipeline_thread_.joinable()) {
      std::unique_lock<std::mutex> lock(start_pipeline_mutex_);
      // Wait for start pipeline to complete with configurable timeout
      // This allows operation to finish naturally but prevents infinite
      // blocking
      auto shutdownTimeout = TimeoutConstants::getShutdownTimeout();
      if (start_pipeline_cv_.wait_for(lock, shutdownTimeout, [this] {
            return !starting_pipeline_.load();
          })) {
        // Start pipeline completed successfully
        lock.unlock();
        if (start_pipeline_thread_.joinable()) {
          start_pipeline_thread_.join();
        }
      } else {
        // Timeout - detach thread to allow cleanup to continue
        lock.unlock();
        std::cerr << "[Worker:" << instance_id_
                  << "] Warning: Start pipeline thread timeout ("
                  << shutdownTimeout.count() << "ms), detaching..."
                  << std::endl;
        if (start_pipeline_thread_.joinable()) {
          start_pipeline_thread_.detach();
        }
      }
    }
  }

  cleanupPipeline();
  if (server_) {
    server_->stop();
  }
}

bool WorkerHandler::initializeDependencies() {
  std::cout << "[Worker:" << instance_id_ << "] Initializing dependencies..."
            << std::endl;

  try {
    // Get solution registry singleton and initialize default solutions
    SolutionRegistry::getInstance().initializeDefaultSolutions();

    // Create pipeline builder (uses default constructor)
    pipeline_builder_ = std::make_unique<PipelineBuilder>();

    std::cout << "[Worker:" << instance_id_ << "] Dependencies initialized"
              << std::endl;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to initialize dependencies: " << e.what()
              << std::endl;
    last_error_ = e.what();
    return false;
  }
}

int WorkerHandler::run() {
  std::cout << "[Worker:" << instance_id_ << "] Starting..." << std::endl;

  // Initialize dependencies first
  if (!initializeDependencies()) {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to initialize dependencies" << std::endl;
    return 1;
  }

  // Create and start IPC server
  server_ = std::make_unique<UnixSocketServer>(socket_path_);

  // Callback to send WORKER_READY message when client connects
  auto onClientConnected = [this](int client_fd) {
    std::cout << "[Worker:" << instance_id_ << "] Client connected, sending WORKER_READY message" << std::endl;
    IPCMessage ready_msg;
    ready_msg.type = MessageType::WORKER_READY;
    ready_msg.payload = "{}"; // Empty JSON payload
    
    std::string data = ready_msg.serialize();
    size_t total_sent = 0;
    while (total_sent < data.size()) {
      ssize_t sent = send(client_fd, data.data() + total_sent,
                          data.size() - total_sent, MSG_NOSIGNAL);
      if (sent <= 0) {
        std::cerr << "[Worker:" << instance_id_ << "] Failed to send WORKER_READY: " << strerror(errno) << std::endl;
        break;
      }
      total_sent += sent;
    }
    std::cout << "[Worker:" << instance_id_ << "] WORKER_READY message sent (" << total_sent << " bytes)" << std::endl;
  };

  if (!server_->start(
          [this](const IPCMessage &msg) { return handleMessage(msg); },
          onClientConnected)) {
    std::cerr << "[Worker:" << instance_id_ << "] Failed to start IPC server"
              << std::endl;
    return 1;
  }

  // Build pipeline from initial config if provided
  if (!config_.isNull() && config_.isMember("Solution")) {
    if (!buildPipeline()) {
      std::cerr << "[Worker:" << instance_id_
                << "] Failed to build initial pipeline" << std::endl;
      // Continue anyway - supervisor can send CREATE_INSTANCE later
    }
  }

  // Start config file watcher if config file path is available
  startConfigWatcher();

  // Send ready signal to supervisor
  sendReadySignal();

  logRuntimeUpdate(instance_id_, "worker ready, runtime_update.log active");
  std::cout << "[Worker:" << instance_id_ << "] Ready and listening on "
            << socket_path_ << std::endl;

  // Main loop - just wait for shutdown
  while (!shutdown_requested_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "[Worker:" << instance_id_ << "] Shutting down..." << std::endl;

  // Wait for start pipeline thread to finish if running
  // Use condition variable to wait for completion naturally
  if (starting_pipeline_.load()) {
    if (start_pipeline_thread_.joinable()) {
      std::cout << "[Worker:" << instance_id_
                << "] Waiting for start pipeline thread to finish..."
                << std::endl;
      std::unique_lock<std::mutex> lock(start_pipeline_mutex_);
      // Wait for start pipeline to complete with configurable timeout
      auto shutdownTimeout = TimeoutConstants::getShutdownTimeout();
      if (start_pipeline_cv_.wait_for(lock, shutdownTimeout, [this] {
            return !starting_pipeline_.load();
          })) {
        // Start pipeline completed successfully
        lock.unlock();
        if (start_pipeline_thread_.joinable()) {
          start_pipeline_thread_.join();
        }
      } else {
        // Timeout - detach thread to allow cleanup to continue
        lock.unlock();
        std::cerr << "[Worker:" << instance_id_
                  << "] Warning: Start pipeline thread timeout ("
                  << shutdownTimeout.count() << "ms), detaching..."
                  << std::endl;
        if (start_pipeline_thread_.joinable()) {
          start_pipeline_thread_.detach();
        }
      }
    }
  }

  // Cleanup - wait for each step to complete naturally
  std::cout << "[Worker:" << instance_id_ << "] Stopping config watcher..."
            << std::endl;
  stopConfigWatcher();

  std::cout << "[Worker:" << instance_id_ << "] Stopping pipeline..."
            << std::endl;
  stopPipeline(); // Will signal condition variable when complete

  // Wait for pipeline to fully stop
  {
    std::unique_lock<std::mutex> lock(stop_pipeline_mutex_);
    auto shutdownTimeout = TimeoutConstants::getShutdownTimeout();
    if (stop_pipeline_cv_.wait_for(lock, shutdownTimeout, [this] {
          return !stopping_pipeline_.load() && !pipeline_running_.load();
        })) {
      // Pipeline stopped successfully
    } else {
      std::cerr << "[Worker:" << instance_id_
                << "] Warning: Pipeline stop timeout ("
                << shutdownTimeout.count() << "ms), continuing..." << std::endl;
    }
  }

  cleanupPipeline();

  std::cout << "[Worker:" << instance_id_ << "] Stopping IPC server..."
            << std::endl;
  if (server_) {
    server_->stop();
  }

  std::cout << "[Worker:" << instance_id_ << "] Exited cleanly" << std::endl;
  return 0;
}

void WorkerHandler::requestShutdown() { shutdown_requested_.store(true); }

IPCMessage WorkerHandler::handleMessage(const IPCMessage &msg) {
  switch (msg.type) {
  case MessageType::PING:
    return handlePing(msg);
  case MessageType::SHUTDOWN:
    return handleShutdown(msg);
  case MessageType::CREATE_INSTANCE:
    return handleCreateInstance(msg);
  case MessageType::DELETE_INSTANCE:
    return handleDeleteInstance(msg);
  case MessageType::START_INSTANCE:
    return handleStartInstance(msg);
  case MessageType::STOP_INSTANCE:
    return handleStopInstance(msg);
  case MessageType::UPDATE_INSTANCE:
    return handleUpdateInstance(msg);
  case MessageType::UPDATE_LINES:
    return handleUpdateLines(msg);
  case MessageType::UPDATE_JAMS:
    return handleUpdateJams(msg);
  case MessageType::UPDATE_STOPS:
    return handleUpdateStops(msg);
  case MessageType::PUSH_FRAME:
    return handlePushFrame(msg);
  case MessageType::GET_INSTANCE_STATUS:
    return handleGetStatus(msg);
  case MessageType::GET_STATISTICS:
    return handleGetStatistics(msg);
  case MessageType::GET_LAST_FRAME:
    return handleGetLastFrame(msg);
  default: {
    IPCMessage error;
    error.type = MessageType::ERROR_RESPONSE;
    error.payload = createErrorResponse("Unknown message type",
                                        ResponseStatus::INVALID_REQUEST);
    return error;
  }
  }
}

IPCMessage WorkerHandler::handlePing(const IPCMessage & /*msg*/) {
  IPCMessage response;
  response.type = MessageType::PONG;

  // Use shared_lock to allow concurrent reads - never blocks other readers
  // Writers (state updates) still get exclusive access
  std::string state_copy;
  {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    state_copy = current_state_;
  }

  response.payload["instance_id"] = instance_id_;
  response.payload["state"] = state_copy;
  response.payload["uptime_ms"] = static_cast<Json::Int64>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start_time_)
          .count());
  return response;
}

IPCMessage WorkerHandler::handleShutdown(const IPCMessage & /*msg*/) {
  IPCMessage response;
  response.type = MessageType::SHUTDOWN_ACK;
  response.payload = createResponse(ResponseStatus::OK, "Shutting down");

  // Request shutdown after sending response
  shutdown_requested_.store(true);

  return response;
}

IPCMessage WorkerHandler::handleCreateInstance(const IPCMessage &msg) {
  IPCMessage response;
  response.type = MessageType::CREATE_INSTANCE_RESPONSE;

  if (getActivePipeline() && !getActivePipeline()->empty()) {
    response.payload = createErrorResponse("Instance already exists",
                                           ResponseStatus::ALREADY_EXISTS);
    return response;
  }

  // Update config from message
  if (msg.payload.isMember("config")) {
    config_ = msg.payload["config"];
  }

  if (!buildPipeline()) {
    response.payload =
        createErrorResponse("Failed to build pipeline: " + last_error_,
                            ResponseStatus::INTERNAL_ERROR);
    return response;
  }

  response.payload = createResponse(ResponseStatus::OK, "Instance created");
  response.payload["data"]["instance_id"] = instance_id_;
  return response;
}

IPCMessage WorkerHandler::handleDeleteInstance(const IPCMessage & /*msg*/) {
  IPCMessage response;
  response.type = MessageType::DELETE_INSTANCE_RESPONSE;

  stopPipeline();
  cleanupPipeline();

  response.payload = createResponse(ResponseStatus::OK, "Instance deleted");

  // Request shutdown after delete
  shutdown_requested_.store(true);

  return response;
}

IPCMessage WorkerHandler::handleStartInstance(const IPCMessage & /*msg*/) {
  std::cout << "[Worker:" << instance_id_ << "] Received START_INSTANCE request"
            << std::endl;

  IPCMessage response;
  response.type = MessageType::START_INSTANCE_RESPONSE;

  // If no pipeline yet but we have config with Solution, build now then start
  // (single "start" = build-if-needed then run, no separate create step)
  if (!getActivePipeline() || getActivePipeline()->empty()) {
    if (!config_.isNull() &&
        (config_.isMember("Solution") || config_.isMember("SolutionId") ||
         (config_.isMember("AdditionalParams") && config_["AdditionalParams"].isObject()) ||
         (config_.isMember("additionalParams") && config_["additionalParams"].isObject()))) {
      std::cout << "[Worker:" << instance_id_
                << "] START_INSTANCE: No pipeline yet, building from config first"
                << std::endl;
      if (!buildPipeline()) {
        std::cerr << "[Worker:" << instance_id_
                  << "] START_INSTANCE: Build failed: " << last_error_
                  << std::endl;
        response.payload = createErrorResponse(
            "Failed to build pipeline: " + last_error_,
            ResponseStatus::INTERNAL_ERROR);
        return response;
      }
      std::cout << "[Worker:" << instance_id_
                << "] START_INSTANCE: Pipeline built, proceeding to start"
                << std::endl;
    } else {
      std::cout << "[Worker:" << instance_id_
                << "] START_INSTANCE: No pipeline configured and no config to build"
                << std::endl;
      response.payload = createErrorResponse("No pipeline configured",
                                             ResponseStatus::NOT_FOUND);
      return response;
    }
  }

  // Use start_pipeline_mutex_ to atomically check pipeline state and
  // starting_pipeline_ flag to prevent race conditions when multiple
  // START_INSTANCE requests come in simultaneously
  // CRITICAL: Keep lock scope minimal to avoid blocking GET_STATISTICS
  bool already_running = false;
  bool already_starting = false;
  {
    std::lock_guard<std::mutex> lock(start_pipeline_mutex_);

    // Check if pipeline is already running
    already_running = pipeline_running_.load();
    if (already_running) {
      std::cout << "[Worker:" << instance_id_
                << "] START_INSTANCE: Pipeline already running, returning error"
                << std::endl;
      // Release lock immediately before creating response
    } else {
      // Check if already starting (must check within same lock)
      already_starting = starting_pipeline_.load();
      if (already_starting) {
        std::cout
            << "[Worker:" << instance_id_
            << "] START_INSTANCE: Pipeline already starting, returning error"
            << std::endl;
        // Release lock immediately before creating response
      } else {
        // Mark as starting
        starting_pipeline_.store(true);
        std::cout << "[Worker:" << instance_id_
                  << "] START_INSTANCE: Starting pipeline async" << std::endl;
      }
    }
  } // Lock released here - critical to avoid blocking other requests

  // Handle error cases after releasing lock
  if (already_running) {
    response.payload = createErrorResponse("Pipeline already running",
                                           ResponseStatus::ALREADY_EXISTS);
    return response;
  }

  if (already_starting) {
    response.payload = createErrorResponse("Pipeline is already starting",
                                           ResponseStatus::ALREADY_EXISTS);
    return response;
  }

  // Update state with exclusive lock (blocks readers only briefly)
  {
    std::lock_guard<std::shared_mutex> lock(state_mutex_);
    current_state_ = "starting";
  }

  // Start pipeline in background thread to avoid blocking IPC server
  // This allows GET_STATISTICS and GET_LAST_FRAME requests to be processed
  // even while pipeline is starting
  startPipelineAsync();

  response.payload = createResponse(ResponseStatus::OK, "Instance starting");
  std::cout
      << "[Worker:" << instance_id_
      << "] START_INSTANCE: Response sent, pipeline starting in background"
      << std::endl;
  return response;
}

IPCMessage WorkerHandler::handleStopInstance(const IPCMessage & /*msg*/) {
  std::cout << "[Worker:" << instance_id_ << "] Received STOP_INSTANCE request"
            << std::endl;

  IPCMessage response;
  response.type = MessageType::STOP_INSTANCE_RESPONSE;

  // Check if already stopping or not running
  bool already_stopping = false;
  bool not_running = false;
  {
    std::lock_guard<std::mutex> lock(stop_pipeline_mutex_);
    not_running = !pipeline_running_.load();
    if (!not_running) {
      already_stopping = stopping_pipeline_.load();
      if (!already_stopping) {
        stopping_pipeline_.store(true);
        std::cout << "[Worker:" << instance_id_
                  << "] STOP_INSTANCE: Stopping pipeline async" << std::endl;
      }
    }
  } // Lock released here - critical to avoid blocking other requests

  if (not_running) {
    response.payload =
        createErrorResponse("Pipeline not running", ResponseStatus::NOT_FOUND);
    return response;
  }

  if (already_stopping) {
    response.payload = createErrorResponse("Pipeline is already stopping",
                                           ResponseStatus::ALREADY_EXISTS);
    return response;
  }

  // Update state with exclusive lock (blocks readers only briefly)
  {
    std::lock_guard<std::shared_mutex> lock(state_mutex_);
    current_state_ = "stopping";
  }

  // Stop pipeline in background thread to avoid blocking IPC server
  // This allows GET_STATISTICS and GET_LAST_FRAME requests to be processed
  // even while pipeline is stopping
  stopPipelineAsync();

  response.payload = createResponse(ResponseStatus::OK, "Instance stopping");
  std::cout << "[Worker:" << instance_id_
            << "] STOP_INSTANCE: Response sent, pipeline stopping in background"
            << std::endl;
  return response;
}

IPCMessage WorkerHandler::handleUpdateInstance(const IPCMessage &msg) {
  // RUNTIME UPDATE GUARANTEE (no restart): When (1) the only changed params
  // are line-related (CrossingLines, CROSSLINE_START_X/Y, CROSSLINE_END_X/Y)
  // and (2) pipeline has ba_crossline_node, we apply via set_lines() and
  // return OK without rebuild/hot-swap. All other changes go through
  // checkIfNeedsRebuild / applyConfigToPipeline and may trigger hot-swap or
  // rebuild. See INSTANCE_UPDATE_HOT_RELOAD_MANUAL_TEST.md "Phân tích thuật toán".
  logRuntimeUpdate(instance_id_, "UPDATE_INSTANCE received");
  IPCMessage response;
  response.type = MessageType::UPDATE_INSTANCE_RESPONSE;

  if (!msg.payload.isMember("config")) {
    response.payload = createErrorResponse("No config provided",
                                           ResponseStatus::INVALID_REQUEST);
    return response;
  }

  // Store old config for comparison
  Json::Value oldConfig = config_;

  // PATCH semantics: only update fields present in the body; leave everything else unchanged.
  // Deep-merge new config so partial updates (e.g. only CrossingLines or
  // CROSSLINE_*) do not wipe entire AdditionalParams/Input/Output and trigger
  // spurious rebuild or hot-swap.
  const auto &newConfig = msg.payload["config"];
  mergeJsonInto(config_, newConfig);
  // Ensure getParamsFromConfig sees merged params: sync additionalParams <-> AdditionalParams
  // (API may send camelCase, storage may use PascalCase; both must see the same flattened params).
  if (config_.isMember("additionalParams") && config_["additionalParams"].isObject()) {
    if (!config_.isMember("AdditionalParams") || !config_["AdditionalParams"].isObject()) {
      config_["AdditionalParams"] = Json::Value(Json::objectValue);
    }
    mergeJsonInto(config_["AdditionalParams"], config_["additionalParams"]);
    mergeJsonInto(config_["additionalParams"], config_["AdditionalParams"]);
  }
  // Preserve output URLs if update wiped them (e.g. PUT with partial body or empty RtmpUrl).
  // Otherwise hot-swap builds pipeline without rtmp_des and stream output is lost.
  if (oldConfig.isMember("RtmpUrl") && !oldConfig["RtmpUrl"].asString().empty()) {
    if (!config_.isMember("RtmpUrl") || config_["RtmpUrl"].asString().empty()) {
      config_["RtmpUrl"] = oldConfig["RtmpUrl"];
      if (config_["AdditionalParams"].isObject()) config_["AdditionalParams"]["RTMP_URL"] = oldConfig["RtmpUrl"];
      if (config_["additionalParams"].isObject()) config_["additionalParams"]["RTMP_URL"] = oldConfig["RtmpUrl"];
    }
  }
  if (oldConfig.isMember("RtspUrl") && !oldConfig["RtspUrl"].asString().empty()) {
    if (!config_.isMember("RtspUrl") || config_["RtspUrl"].asString().empty()) {
      config_["RtspUrl"] = oldConfig["RtspUrl"];
      if (config_["AdditionalParams"].isObject()) config_["AdditionalParams"]["RTSP_URL"] = oldConfig["RtspUrl"];
      if (config_["additionalParams"].isObject()) config_["additionalParams"]["RTSP_URL"] = oldConfig["RtspUrl"];
    }
  }
  if (oldConfig.isMember("AdditionalParams") && oldConfig["AdditionalParams"].isObject()) {
    for (const char* key : {"RTMP_URL", "RTMP_DES_URL", "RTSP_URL"}) {
      if (oldConfig["AdditionalParams"].isMember(key) && !oldConfig["AdditionalParams"][key].asString().empty()) {
        if (!config_["AdditionalParams"].isMember(key) || config_["AdditionalParams"][key].asString().empty()) {
          config_["AdditionalParams"][key] = oldConfig["AdditionalParams"][key];
          if (config_["additionalParams"].isObject()) config_["additionalParams"][key] = oldConfig["AdditionalParams"][key];
        }
      }
    }
  }
  // Preserve nested additionalParams.output (e.g. output.RTMP_DES_URL) when update wipes it
  // or omits it. Otherwise hot-swap builds without RTMP output and stream is lost after update.
  if (oldConfig.isMember("additionalParams") && oldConfig["additionalParams"].isObject() &&
      oldConfig["additionalParams"].isMember("output") && oldConfig["additionalParams"]["output"].isObject()) {
    const auto& oldOutput = oldConfig["additionalParams"]["output"];
    bool oldHasRtmp = (oldOutput.isMember("RTMP_DES_URL") && !oldOutput["RTMP_DES_URL"].asString().empty()) ||
                      (oldOutput.isMember("RTMP_URL") && !oldOutput["RTMP_URL"].asString().empty());
    if (!oldHasRtmp) { /* nothing to preserve */ }
    else {
      if (!config_.isMember("additionalParams") || !config_["additionalParams"].isObject()) {
        config_["additionalParams"] = Json::Value(Json::objectValue);
      }
      Json::Value& newOutput = config_["additionalParams"]["output"];
      std::string newRtmp = newOutput.isObject() && newOutput.isMember("RTMP_DES_URL")
          ? newOutput["RTMP_DES_URL"].asString()
          : (newOutput.isObject() && newOutput.isMember("RTMP_URL") ? newOutput["RTMP_URL"].asString() : "");
      bool needPreserve = !newOutput.isObject() || newOutput.empty() ||
                          (newRtmp.empty() && oldHasRtmp);
      if (needPreserve) {
        config_["additionalParams"]["output"] = oldOutput;
        if (config_.isMember("AdditionalParams") && config_["AdditionalParams"].isObject()) {
          if (oldOutput.isMember("RTMP_DES_URL") && !oldOutput["RTMP_DES_URL"].asString().empty()) {
            config_["AdditionalParams"]["RTMP_DES_URL"] = oldOutput["RTMP_DES_URL"];
            config_["AdditionalParams"]["RTMP_URL"] = oldOutput["RTMP_DES_URL"];
          }
          if (oldOutput.isMember("RTMP_URL") && !oldOutput["RTMP_URL"].asString().empty()) {
            config_["AdditionalParams"]["RTMP_URL"] = oldOutput["RTMP_URL"];
          }
        }
        if (!config_.isMember("RtmpUrl") || config_["RtmpUrl"].asString().empty()) {
          if (oldOutput.isMember("RTMP_DES_URL") && !oldOutput["RTMP_DES_URL"].asString().empty()) {
            config_["RtmpUrl"] = oldOutput["RTMP_DES_URL"];
          } else if (oldOutput.isMember("RTMP_URL") && !oldOutput["RTMP_URL"].asString().empty()) {
            config_["RtmpUrl"] = oldOutput["RTMP_URL"];
          }
        }
      }
    }
  }
  logRuntimeUpdate(instance_id_, "config merged (deep merge)");

  // If pipeline is not running, just update config (will apply on next start)
  if (!pipeline_running_.load() || !getActivePipeline() || getActivePipeline()->empty()) {
    std::cout << "[Worker:" << instance_id_
              << "] Config updated (pipeline not running, will apply on start)"
              << std::endl;
    response.payload = createResponse(ResponseStatus::OK, "Instance updated");
    return response;
  }

  // Early path: if the ONLY change is line-related (CrossingLines / CROSSLINE_*),
  // apply via set_lines() and return immediately. Never hot-swap for line-only
  // (avoids RTMP stream drop and no response / long wait).
  {
    Json::Value oldParams = getParamsFromConfig(oldConfig);
    Json::Value newParams = getParamsFromConfig(config_);
    static const std::set<std::string> lineKeys = {
        "CrossingLines", "CROSSLINE_START_X", "CROSSLINE_START_Y",
        "CROSSLINE_END_X", "CROSSLINE_END_Y"};
    bool onlyLineParamsChanged = true;
    // Compare only non-line keys: any non-line key with different value -> not line-only.
    for (const auto &key : newParams.getMemberNames()) {
      if (lineKeys.count(key)) {
        continue;
      }
      std::string oldVal = oldParams.isMember(key) ? oldParams[key].asString() : "";
      std::string newVal = newParams[key].asString();
      if (oldVal != newVal) {
        onlyLineParamsChanged = false;
        break;
      }
    }
    if (onlyLineParamsChanged) {
      for (const auto &key : oldParams.getMemberNames()) {
        if (lineKeys.count(key)) continue;
        std::string oldVal = oldParams[key].asString();
        if (!newParams.isMember(key)) {
          // Key only in old (e.g. RTMP_DES_URL not in payload) -> treat as unchanged
          continue;
        }
        if (newParams[key].asString() != oldVal) {
          onlyLineParamsChanged = false;
          break;
        }
      }
    }
    if (!onlyLineParamsChanged) {
      logRuntimeUpdate(instance_id_,
                       "update=not_line_only (non-line param diff or structure) -> may hot-swap");
    }
    if (onlyLineParamsChanged) {
      std::cout << "[Worker:" << instance_id_
                << "] Line-only update: applying CrossingLines at runtime (no hot-swap)"
                << std::endl;
      logRuntimeUpdate(instance_id_, "update=line_only (no hot swap)");
      if (applyLinesFromParamsToPipeline(newParams)) {
        response.payload =
            createResponse(ResponseStatus::OK, "Instance updated (runtime)");
        return response;
      }
      // set_lines failed; config already saved, pipeline keeps running
      std::cerr << "[Worker:" << instance_id_
                << "] Failed to apply lines at runtime (config saved, will apply on next start)"
                << std::endl;
      response.payload =
          createResponse(ResponseStatus::OK, "Instance updated (lines will apply on restart)");
      return response;
    }
  }

  // Pipeline is running - apply config changes automatically
  std::cout << "[Worker:" << instance_id_
            << "] Applying config changes to running pipeline..." << std::endl;

  // Check if this is a structural change that requires rebuild
  bool needsRebuild = checkIfNeedsRebuild(oldConfig, config_);
  logRuntimeUpdate(instance_id_,
                   "checkIfNeedsRebuild=" + std::string(needsRebuild ? "true" : "false"));

  // Try to apply config changes runtime first (for parameters that can be
  // updated)
  bool canApplyRuntime = applyConfigToPipeline(oldConfig, config_);
  logRuntimeUpdate(instance_id_,
                   "applyConfigToPipeline=" + std::string(canApplyRuntime ? "true" : "false"));

  // If rebuild is needed OR runtime update failed, use hot swap for zero
  // downtime
  if (needsRebuild || !canApplyRuntime) {
    std::cout << "[Worker:" << instance_id_
              << "] Config changes require pipeline rebuild, using hot swap..."
              << std::endl;
    logRuntimeUpdate(instance_id_, "decision=hot_swap_or_rebuild (needsRebuild or !canApplyRuntime)");

    // Use hot swap for zero downtime
    if (pipeline_running_.load()) {
      if (hotSwapPipeline(config_)) {
        std::cout << "[Worker:" << instance_id_
                  << "] ✓ Pipeline hot-swapped successfully (zero downtime)"
                  << std::endl;
        logRuntimeUpdate(instance_id_, "result=hot_swap_ok");
        response.payload =
            createResponse(ResponseStatus::OK, "Instance updated (hot swap)");
      } else {
        // Fallback to traditional rebuild if hot swap failed
        std::cerr << "[Worker:" << instance_id_
                  << "] Hot swap failed, falling back to traditional rebuild"
                  << std::endl;

        stopPipeline();
        if (!buildPipeline()) {
          last_error_ = "Failed to rebuild pipeline: " + last_error_;
          std::cerr << "[Worker:" << instance_id_ << "] " << last_error_
                    << std::endl;
          response.payload =
              createErrorResponse(last_error_, ResponseStatus::INTERNAL_ERROR);
          return response;
        }
        if (!startPipeline()) {
          last_error_ = "Failed to restart pipeline: " + last_error_;
          std::cerr << "[Worker:" << instance_id_ << "] " << last_error_
                    << std::endl;
          response.payload =
              createErrorResponse(last_error_, ResponseStatus::INTERNAL_ERROR);
          return response;
        }
        logRuntimeUpdate(instance_id_, "result=rebuild_fallback (hot_swap failed)");
        response.payload = createResponse(
            ResponseStatus::OK, "Instance updated (rebuild fallback)");
      }
    } else {
      // Pipeline not running, just rebuild
      std::cout << "[Worker:" << instance_id_ << "] Rebuilding pipeline..."
                << std::endl;
      if (!buildPipeline()) {
        last_error_ = "Failed to rebuild pipeline: " + last_error_;
        std::cerr << "[Worker:" << instance_id_ << "] " << last_error_
                  << std::endl;
        response.payload =
            createErrorResponse(last_error_, ResponseStatus::INTERNAL_ERROR);
        return response;
      }
      std::cout << "[Worker:" << instance_id_
                << "] ✓ Pipeline rebuilt successfully (was not running)"
                << std::endl;
      response.payload = createResponse(
          ResponseStatus::OK, "Instance updated and pipeline rebuilt");
    }
    return response;
  }

  // Config changes applied runtime without rebuild
  std::cout << "[Worker:" << instance_id_
            << "] ✓ Config changes applied successfully (runtime update)"
            << std::endl;
  logRuntimeUpdate(instance_id_, "result=runtime_ok (no restart)");
  response.payload =
      createResponse(ResponseStatus::OK, "Instance updated (runtime)");

  return response;
}

IPCMessage WorkerHandler::handleUpdateLines(const IPCMessage &msg) {
  IPCMessage response;
  response.type = MessageType::UPDATE_LINES_RESPONSE;

  if (!msg.payload.isMember("lines")) {
    response.payload = createErrorResponse("No lines provided",
                                           ResponseStatus::INVALID_REQUEST);
    return response;
  }

  // Check if pipeline is running
  if (!pipeline_running_.load() || !getActivePipeline() || getActivePipeline()->empty()) {
    std::cout << "[Worker:" << instance_id_
              << "] Cannot update lines: pipeline not running" << std::endl;
    response.payload = createErrorResponse("Pipeline not running",
                                           ResponseStatus::ERROR);
    return response;
  }

  // Find ba_crossline_node in pipeline
  std::shared_ptr<cvedix_nodes::cvedix_ba_line_crossline_node> baCrosslineNode = nullptr;
  auto pipeline = getActivePipeline();
  if (pipeline) {
    for (const auto &node : pipeline->nodes()) {
      if (!node)
        continue;

      auto crosslineNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_line_crossline_node>(node);
    if (crosslineNode) {
      baCrosslineNode = crosslineNode;
      break;
    }
  }
  }

  if (!baCrosslineNode) {
    std::cout << "[Worker:" << instance_id_
              << "] ba_crossline_node not found in pipeline" << std::endl;
    response.payload = createErrorResponse("ba_crossline_node not found",
                                           ResponseStatus::NOT_FOUND);
    return response;
  }

  // Parse lines from JSON
  const Json::Value &linesArray = msg.payload["lines"];
  if (!linesArray.isArray()) {
    response.payload = createErrorResponse("Lines must be an array",
                                           ResponseStatus::INVALID_REQUEST);
    return response;
  }

  // Convert JSON lines to map<int, cvedix_line>
  std::map<int, cvedix_objects::cvedix_line> lines;
  for (Json::ArrayIndex i = 0; i < linesArray.size(); ++i) {
    const Json::Value &lineObj = linesArray[i];

    // Check if line has coordinates
    if (!lineObj.isMember("coordinates") || !lineObj["coordinates"].isArray()) {
      std::cout << "[Worker:" << instance_id_ << "] Line at index " << i
                << " missing or invalid 'coordinates' field, skipping" << std::endl;
      continue;
    }

    const Json::Value &coordinates = lineObj["coordinates"];
    if (coordinates.size() < 2) {
      std::cout << "[Worker:" << instance_id_ << "] Line at index " << i
                << " has less than 2 coordinates, skipping" << std::endl;
      continue;
    }

    // Get first and last coordinates
    const Json::Value &startCoord = coordinates[0];
    const Json::Value &endCoord = coordinates[coordinates.size() - 1];

    if (!startCoord.isMember("x") || !startCoord.isMember("y") ||
        !endCoord.isMember("x") || !endCoord.isMember("y")) {
      std::cout << "[Worker:" << instance_id_ << "] Line at index " << i
                << " has invalid coordinate format, skipping" << std::endl;
      continue;
    }

    if (!startCoord["x"].isNumeric() || !startCoord["y"].isNumeric() ||
        !endCoord["x"].isNumeric() || !endCoord["y"].isNumeric()) {
      std::cout << "[Worker:" << instance_id_ << "] Line at index " << i
                << " has non-numeric coordinates, skipping" << std::endl;
      continue;
    }

    // Convert to cvedix_line
    int start_x = startCoord["x"].asInt();
    int start_y = startCoord["y"].asInt();
    int end_x = endCoord["x"].asInt();
    int end_y = endCoord["y"].asInt();

    cvedix_objects::cvedix_point start(start_x, start_y);
    cvedix_objects::cvedix_point end(end_x, end_y);

    // Use array index as channel (0, 1, 2, ...)
    int channel = static_cast<int>(i);
    lines[channel] = cvedix_objects::cvedix_line(start, end);
  }

  // Update lines via SDK API
  try {
    std::cout << "[Worker:" << instance_id_ << "] Updating " << lines.size()
              << " line(s) via SDK set_lines() API" << std::endl;

    bool success = baCrosslineNode->set_lines(lines);
    if (success) {
      std::cout << "[Worker:" << instance_id_
                << "] ✓ Successfully updated lines via hot reload (no restart needed)"
                << std::endl;
      response.payload = createResponse(ResponseStatus::OK,
                                        "Lines updated successfully (runtime)");
      Json::Value data;
      data["lines_count"] = static_cast<int>(lines.size());
      response.payload["data"] = data;
    } else {
      std::cout << "[Worker:" << instance_id_
                << "] Failed to update lines via SDK API" << std::endl;
      response.payload = createErrorResponse("Failed to update lines via SDK API",
                                              ResponseStatus::INTERNAL_ERROR);
    }
  } catch (const std::exception &e) {
    std::cerr << "[Worker:" << instance_id_
              << "] Exception updating lines: " << e.what() << std::endl;
    response.payload = createErrorResponse("Exception updating lines: " +
                                               std::string(e.what()),
                                           ResponseStatus::INTERNAL_ERROR);
  } catch (...) {
    std::cerr << "[Worker:" << instance_id_
              << "] Unknown exception updating lines" << std::endl;
    response.payload = createErrorResponse("Unknown exception updating lines",
                                           ResponseStatus::INTERNAL_ERROR);
  }

  return response;
}

IPCMessage WorkerHandler::handleUpdateJams(const IPCMessage &msg) {
  IPCMessage response;
  response.type = MessageType::UPDATE_JAMS_RESPONSE;

  if (!msg.payload.isMember("jams")) {
    response.payload = createErrorResponse("No jams provided",
                                           ResponseStatus::INVALID_REQUEST);
    return response;
  }

  if (!pipeline_running_.load() || !getActivePipeline() || getActivePipeline()->empty()) {
    std::cout << "[Worker:" << instance_id_
              << "] Cannot update jams: pipeline not running" << std::endl;
    response.payload = createErrorResponse("Pipeline not running",
                                           ResponseStatus::ERROR);
    return response;
  }

  Json::Value oldConfig = config_;
  if (!config_.isMember("AdditionalParams") || !config_["AdditionalParams"].isObject()) {
    config_["AdditionalParams"] = Json::Value(Json::objectValue);
  }
  Json::StreamWriterBuilder wb;
  wb["indentation"] = "";
  config_["AdditionalParams"]["JamZones"] =
      Json::writeString(wb, msg.payload["jams"]);

  std::cout << "[Worker:" << instance_id_
            << "] Updating jam zones via hot swap (config merge)" << std::endl;
  if (hotSwapPipeline(config_)) {
    std::cout << "[Worker:" << instance_id_
              << "] ✓ Jam zones updated successfully (hot swap)" << std::endl;
    response.payload = createResponse(ResponseStatus::OK,
                                      "Jam zones updated successfully (runtime)");
    Json::Value data;
    data["jams_count"] = msg.payload["jams"].isArray() ?
        static_cast<int>(msg.payload["jams"].size()) : 0;
    response.payload["data"] = data;
  } else {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to hot swap pipeline for jam zones update" << std::endl;
    config_ = oldConfig;
    response.payload = createErrorResponse("Failed to apply jam zones (hot swap failed)",
                                           ResponseStatus::INTERNAL_ERROR);
  }
  return response;
}

IPCMessage WorkerHandler::handleUpdateStops(const IPCMessage &msg) {
  IPCMessage response;
  response.type = MessageType::UPDATE_STOPS_RESPONSE;

  if (!msg.payload.isMember("stops")) {
    response.payload = createErrorResponse("No stops provided",
                                           ResponseStatus::INVALID_REQUEST);
    return response;
  }

  if (!pipeline_running_.load() || !getActivePipeline() || getActivePipeline()->empty()) {
    std::cout << "[Worker:" << instance_id_
              << "] Cannot update stops: pipeline not running" << std::endl;
    response.payload = createErrorResponse("Pipeline not running",
                                           ResponseStatus::ERROR);
    return response;
  }

  Json::Value oldConfig = config_;
  if (!config_.isMember("AdditionalParams") || !config_["AdditionalParams"].isObject()) {
    config_["AdditionalParams"] = Json::Value(Json::objectValue);
  }
  Json::StreamWriterBuilder wb;
  wb["indentation"] = "";
  config_["AdditionalParams"]["StopZones"] =
      Json::writeString(wb, msg.payload["stops"]);

  std::cout << "[Worker:" << instance_id_
            << "] Updating stop zones via hot swap (config merge)" << std::endl;
  if (hotSwapPipeline(config_)) {
    std::cout << "[Worker:" << instance_id_
              << "] ✓ Stop zones updated successfully (hot swap)" << std::endl;
    response.payload = createResponse(ResponseStatus::OK,
                                      "Stop zones updated successfully (runtime)");
    Json::Value data;
    data["stops_count"] = msg.payload["stops"].isArray() ?
        static_cast<int>(msg.payload["stops"].size()) : 0;
    response.payload["data"] = data;
  } else {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to hot swap pipeline for stop zones update" << std::endl;
    config_ = oldConfig;
    response.payload = createErrorResponse("Failed to apply stop zones (hot swap failed)",
                                           ResponseStatus::INTERNAL_ERROR);
  }
  return response;
}

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
  std::vector<uint8_t> data = base64_decode(b64);
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

CreateInstanceRequest
WorkerHandler::parseCreateRequest(const Json::Value &config) const {
  CreateInstanceRequest req;

  req.name = config.get("Name", instance_id_).asString();
  req.group = config.get("Group", "").asString();

  // Get solution ID
  if (config.isMember("Solution")) {
    if (config["Solution"].isString()) {
      req.solution = config["Solution"].asString();
    } else if (config["Solution"].isObject()) {
      req.solution = config["Solution"].get("SolutionId", "").asString();
    }
  }
  req.solution = config.get("SolutionId", req.solution).asString();

  // Flags
  req.persistent = config.get("Persistent", false).asBool();
  req.autoStart = config.get("AutoStart", false).asBool();
  req.autoRestart = config.get("AutoRestart", false).asBool();
  req.frameRateLimit = config.get("FrameRateLimit", 0).asInt();

  // Additional parameters (source URLs, model paths, etc.)
  // Support both nested structure (input/output) and flat structure
  // Support both "additionalParams" (lowercase, API format) and "AdditionalParams" (capital, internal format)
  Json::Value params;
  if (config.isMember("additionalParams") &&
      config["additionalParams"].isObject()) {
    params = config["additionalParams"];
  } else if (config.isMember("AdditionalParams") &&
             config["AdditionalParams"].isObject()) {
    params = config["AdditionalParams"];
  }
  
  if (!params.isNull()) {
    
    // Check if using new structure (input/output)
    if (params.isMember("input") && params["input"].isObject()) {
      // New structure: parse input section
      for (const auto &key : params["input"].getMemberNames()) {
        if (params["input"][key].isString()) {
          req.additionalParams[key] = params["input"][key].asString();
        }
      }
    }

    if (params.isMember("output") && params["output"].isObject()) {
      // New structure: parse output section
      for (const auto &key : params["output"].getMemberNames()) {
        if (params["output"][key].isString()) {
          req.additionalParams[key] = params["output"][key].asString();
        }
      }
    }

    // Backward compatibility: if no input/output sections, parse as flat
    // structure. Also parse flat keys (RTMP_URL, RTSP_URL, etc.) even when input/output exist.
    if (!params.isMember("input") && !params.isMember("output")) {
      for (const auto &key : params.getMemberNames()) {
        if (params[key].isString()) {
          req.additionalParams[key] = params[key].asString();
        }
      }
    } else {
      for (const auto &key : params.getMemberNames()) {
        if (key != "input" && key != "output" && params[key].isString()) {
          req.additionalParams[key] = params[key].asString();
        }
      }
    }
  }

  // Direct URL parameters (top-level fields for backward compatibility)
  // CRITICAL: These are set by SubprocessInstanceManager to ensure RTMP_URL is available
  if (config.isMember("RtspUrl")) {
    req.additionalParams["RTSP_URL"] = config["RtspUrl"].asString();
    std::cout << "[Worker:" << instance_id_ << "] Found RTSP_URL in top-level RtspUrl: '"
              << req.additionalParams["RTSP_URL"] << "'" << std::endl;
  }
  if (config.isMember("RtmpUrl")) {
    req.additionalParams["RTMP_URL"] = config["RtmpUrl"].asString();
    std::cout << "[Worker:" << instance_id_ << "] Found RTMP_URL in top-level RtmpUrl: '"
              << req.additionalParams["RTMP_URL"] << "'" << std::endl;
  }
  if (config.isMember("FilePath")) {
    req.additionalParams["FILE_PATH"] = config["FilePath"].asString();
    std::cout << "[Worker:" << instance_id_ << "] Found FILE_PATH in top-level FilePath: '"
              << req.additionalParams["FILE_PATH"] << "'" << std::endl;
  }

  // Debug: Log final additionalParams to verify RTMP_URL is present
  std::cout << "[Worker:" << instance_id_ << "] Final additionalParams keys: ";
  for (const auto &[key, value] : req.additionalParams) {
    if (key.find("RTMP") != std::string::npos || key.find("RTSP") != std::string::npos) {
      std::cout << key << "='" << value << "' ";
    }
  }
  std::cout << std::endl;

  return req;
}

bool WorkerHandler::buildPipeline() {
  std::cout << "[Worker:" << instance_id_
            << "] Building pipeline from config..." << std::endl;

  if (!pipeline_builder_) {
    last_error_ = "Pipeline builder not initialized";
    return false;
  }

  try {
    // Parse config to CreateInstanceRequest
    CreateInstanceRequest req = parseCreateRequest(config_);

    if (req.solution.empty()) {
      last_error_ = "No solution specified in config";
      return false;
    }

    // Get solution config from singleton
    auto optSolution =
        SolutionRegistry::getInstance().getSolution(req.solution);
    if (!optSolution.has_value()) {
      last_error_ = "Solution not found: " + req.solution;
      return false;
    }

    // CVEDIX logger must be initialized before creating any cvedix nodes (e.g. PersistentOutputLeg/rtmp_des)
    if (pipeline_builder_) {
      pipeline_builder_->ensureCVEDIXInitialized();
    }
    if (!ensureOutputLegForRtmp(req)) {
      return false;
    }

    // Build pipeline (with frame_router_ when RTMP → zero-downtime swap)
    std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> nodes =
        pipeline_builder_->buildPipeline(optSolution.value(), req,
                                         instance_id_, {}, frame_router_.get());

    if (nodes.empty()) {
      last_error_ = "Pipeline builder returned empty pipeline";
      return false;
    }

    setActivePipeline(
        std::make_shared<PipelineSnapshot>(std::move(nodes)));
    {
      std::lock_guard<std::shared_mutex> lock(state_mutex_);
      current_state_ = "created";
    }
    std::cout << "[Worker:" << instance_id_ << "] Pipeline built with "
              << getActivePipeline()->size() << " nodes" << std::endl;
    return true;

  } catch (const char* msg) {
    last_error_ = std::string("Pipeline build (SDK/lib threw string): ") + (msg ? msg : "(null)");
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to build pipeline: " << (msg ? msg : "(null)") << std::endl;
    return false;
  } catch (const std::exception &e) {
    last_error_ = e.what();
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to build pipeline: " << e.what() << std::endl;
    return false;
  } catch (...) {
    last_error_ = "Pipeline build failed (unknown exception, e.g. SDK threw non-std::exception)";
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to build pipeline: unknown exception" << std::endl;
    return false;
  }
}

bool WorkerHandler::startPipeline() {
  // Check if shutdown was requested
  if (shutdown_requested_.load()) {
    last_error_ = "Shutdown requested, cannot start pipeline";
    return false;
  }

  auto pipeline = getActivePipeline();
  if (!pipeline || pipeline->empty()) {
    last_error_ = "No pipeline to start";
    return false;
  }

  std::cout << "[Worker:" << instance_id_ << "] Starting pipeline..."
            << std::endl;

  try {
    // CRITICAL: Setup frame capture hook BEFORE starting pipeline
    setupFrameCaptureHook();
    setupQueueSizeTrackingHook();

    // Find and start source node (first node in pipeline)
    auto sourceNode = pipeline->sourceNode();
    auto rtspNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(
            sourceNode);
    if (rtspNode) {
      std::cout << "[Worker:" << instance_id_ << "] Starting RTSP source node"
                << std::endl;

      // Check shutdown flag before starting (which may block)
      if (shutdown_requested_.load()) {
        last_error_ = "Shutdown requested before starting source node";
        return false;
      }

      rtspNode->start();
    } else {
      auto rtmpNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(
              sourceNode);
      if (rtmpNode) {
        std::cout << "[Worker:" << instance_id_
                  << "] Starting RTMP source node" << std::endl;

        if (shutdown_requested_.load()) {
          last_error_ = "Shutdown requested before starting source node";
          return false;
        }

        rtmpNode->start();
      } else {
        auto fileNode =
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(
                sourceNode);
        if (fileNode) {
          std::cout << "[Worker:" << instance_id_
                    << "] Starting file source node" << std::endl;

          if (shutdown_requested_.load()) {
            last_error_ = "Shutdown requested before starting source node";
            return false;
          }

          fileNode->start();
        } else {
          auto imageNode =
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_image_src_node>(
                  sourceNode);
          if (imageNode) {
            std::cout << "[Worker:" << instance_id_
                      << "] Starting image source node" << std::endl;
            if (shutdown_requested_.load()) {
              last_error_ = "Shutdown requested before starting source node";
              return false;
            }
            imageNode->start();
          } else {
            auto udpNode =
                std::dynamic_pointer_cast<cvedix_nodes::cvedix_udp_src_node>(
                    sourceNode);
            if (udpNode) {
              std::cout << "[Worker:" << instance_id_
                        << "] Starting UDP source node" << std::endl;
              if (shutdown_requested_.load()) {
                last_error_ = "Shutdown requested before starting source node";
                return false;
              }
              udpNode->start();
            } else {
              auto appSrcNode =
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_src_node>(
                      sourceNode);
              if (appSrcNode) {
                std::cout << "[Worker:" << instance_id_
                          << "] Starting app source node (push-based)"
                          << std::endl;
                if (shutdown_requested_.load()) {
                  last_error_ = "Shutdown requested before starting source node";
                  return false;
                }
                appSrcNode->start();
              } else {
                last_error_ = "No supported source node found in pipeline "
                             "(rtsp, rtmp, file, image, udp, app_src)";
                return false;
              }
            }
          }
        }
      }
    }

    {
      std::lock_guard<std::mutex> lock(start_pipeline_mutex_);
      pipeline_running_.store(true);
      start_time_ = std::chrono::steady_clock::now();
      last_fps_update_ = start_time_;
      frames_processed_.store(0);
      dropped_frames_.store(0);
      current_fps_.store(0.0); // Reset FPS
    }

    // Update state separately using state_mutex_ (quick operation, won't block)
    {
      std::lock_guard<std::shared_mutex> lock(state_mutex_);
      current_state_ = "running";
    }

    std::cout << "[Worker:" << instance_id_ << "] Pipeline started"
              << std::endl;
    return true;

  } catch (const std::exception &e) {
    {
      std::lock_guard<std::shared_mutex> lock(state_mutex_);
      last_error_ = e.what();
      current_state_ = "stopped";
    }
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to start pipeline: " << e.what() << std::endl;
    return false;
  }
}

void WorkerHandler::startPipelineAsync() {
  // Note: starting_pipeline_ is already set to true by handleStartInstance
  // before this function is called, so we don't need to set it again here

  // Check if shutdown was requested
  if (shutdown_requested_.load()) {
    {
      std::lock_guard<std::mutex> lock(start_pipeline_mutex_);
      starting_pipeline_.store(false);
    }
    {
      std::lock_guard<std::shared_mutex> lock(state_mutex_);
      current_state_ = "stopped";
    }
    start_pipeline_cv_.notify_all();
    return;
  }

  // Start pipeline in background thread
  start_pipeline_thread_ = std::thread([this]() {
    try {
      // Check shutdown flag before starting pipeline
      if (shutdown_requested_.load()) {
        {
          std::lock_guard<std::mutex> lock(start_pipeline_mutex_);
          starting_pipeline_.store(false);
        }
        start_pipeline_cv_.notify_all(); // Notify waiters
        return;
      }

      bool success = startPipeline();
      {
        std::lock_guard<std::mutex> lock(start_pipeline_mutex_);
        starting_pipeline_.store(false);
      }
      if (!success) {
        std::lock_guard<std::shared_mutex> lock(state_mutex_);
        current_state_ = "stopped";
      }
      start_pipeline_cv_.notify_all(); // Notify waiters that start completed
    } catch (const std::exception &e) {
      {
        std::lock_guard<std::mutex> lock(start_pipeline_mutex_);
        starting_pipeline_.store(false);
      }
      {
        std::lock_guard<std::shared_mutex> lock(state_mutex_);
        last_error_ = "Exception in startPipeline: " + std::string(e.what());
        current_state_ = "stopped";
      }
      std::cerr << "[Worker:" << instance_id_ << "] " << last_error_
                << std::endl;
      start_pipeline_cv_.notify_all(); // Notify waiters even on error
    }
  });
}

void WorkerHandler::stopPipelineAsync() {
  // Note: stopping_pipeline_ is already set to true by handleStopInstance
  // before this function is called, so we don't need to set it again here

  // Check if shutdown was requested
  if (shutdown_requested_.load()) {
    {
      std::lock_guard<std::mutex> lock(stop_pipeline_mutex_);
      stopping_pipeline_.store(false);
    }
    {
      std::lock_guard<std::shared_mutex> lock(state_mutex_);
      current_state_ = "stopped";
    }
    stop_pipeline_cv_.notify_all();
    return;
  }

  // Stop pipeline in background thread
  std::thread stop_thread([this]() {
    try {
      // Check shutdown flag before stopping pipeline
      if (shutdown_requested_.load()) {
        {
          std::lock_guard<std::mutex> lock(stop_pipeline_mutex_);
          stopping_pipeline_.store(false);
        }
        {
          std::lock_guard<std::shared_mutex> lock(state_mutex_);
          current_state_ = "stopped";
        }
        stop_pipeline_cv_.notify_all(); // Notify waiters
        return;
      }

      // Call stopPipeline() which will handle all the stopping logic
      // Note: stopping_pipeline_ is already true, stopPipeline() will handle
      // setting it to false and signaling condition variable when done
      stopPipeline();
    } catch (const std::exception &e) {
      {
        std::lock_guard<std::mutex> lock(stop_pipeline_mutex_);
        stopping_pipeline_.store(false);
      }
      {
        std::lock_guard<std::shared_mutex> lock(state_mutex_);
        last_error_ = "Exception in stopPipeline: " + std::string(e.what());
        current_state_ = "stopped";
      }
      std::cerr << "[Worker:" << instance_id_ << "] " << last_error_
                << std::endl;
      stop_pipeline_cv_.notify_all(); // Notify waiters even on error
    }
  });
  stop_thread.detach(); // Detach thread - stop runs in background
}

void WorkerHandler::stopPipeline() {
  if (!pipeline_running_.load()) {
    {
      std::lock_guard<std::mutex> lock(stop_pipeline_mutex_);
      stopping_pipeline_.store(false);
    }
    stop_pipeline_cv_.notify_all();
    return;
  }

  // Note: stopping_pipeline_ should already be set to true by caller

  std::cout << "[Worker:" << instance_id_ << "] Stopping pipeline..."
            << std::endl;

  try {
    auto pStop = getActivePipeline();
    if (!pStop) {
      std::lock_guard<std::mutex> lock(stop_pipeline_mutex_);
      stopping_pipeline_.store(false);
      stop_pipeline_cv_.notify_all();
      return;
    }
    // Stop source nodes first
    for (const auto &node : pStop->nodes()) {
      if (node) {
        // Check shutdown flag before stopping
        if (shutdown_requested_.load()) {
          std::cerr << "[Worker:" << instance_id_
                    << "] Shutdown requested, force detaching pipeline..."
                    << std::endl;
          // Force detach if shutdown is requested
          try {
            node->detach_recursively();
          } catch (...) {
            // Ignore errors during force detach
          }
          break;
        }

        // Try to stop gracefully first
        try {
          auto rtspNode =
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(
                  node);
          if (rtspNode) {
            // Use configurable timeout for RTSP stop
            auto stopTimeout =
                shutdown_requested_.load()
                    ? TimeoutConstants::getRtspStopTimeoutDeletion()
                    : TimeoutConstants::getRtspStopTimeout();

            auto stopFuture = std::async(std::launch::async, [rtspNode]() {
              try {
                rtspNode->stop();
                return true;
              } catch (...) {
                return false;
              }
            });

            auto stopStatus = stopFuture.wait_for(stopTimeout);
            if (stopStatus == std::future_status::timeout) {
              std::cerr << "[Worker:" << instance_id_ << "] Stop timeout ("
                        << stopTimeout.count() << "ms), using detach..."
                        << std::endl;
            } else if (stopStatus == std::future_status::ready) {
              stopFuture.get();
            }
          } else {
            auto rtmpNode =
                std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(
                    node);
            if (rtmpNode) {
              try {
                rtmpNode->stop();
              } catch (...) {
                // Ignore; detach_recursively below will clean up
              }
            } else {
              auto imageNode =
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_image_src_node>(
                      node);
              if (imageNode) {
                try {
                  imageNode->stop();
                } catch (...) {}
              } else {
                auto udpNode =
                    std::dynamic_pointer_cast<cvedix_nodes::cvedix_udp_src_node>(
                        node);
                if (udpNode) {
                  try {
                    udpNode->stop();
                  } catch (...) {}
                } else {
                  auto appSrcNode =
                      std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_src_node>(
                          node);
                  if (appSrcNode) {
                    try {
                      appSrcNode->stop();
                    } catch (...) {}
                  } else {
              auto fileNode =
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(
                      node);
              if (fileNode) {
                // Use configurable timeout for file stop
                auto stopTimeout =
                    shutdown_requested_.load()
                        ? TimeoutConstants::getRtspStopTimeoutDeletion()
                        : TimeoutConstants::getRtspStopTimeout();

                auto stopFuture = std::async(std::launch::async, [fileNode]() {
                  try {
                    fileNode->stop();
                    return true;
                  } catch (...) {
                    return false;
                  }
                });

                auto stopStatus = stopFuture.wait_for(stopTimeout);
                if (stopStatus == std::future_status::timeout) {
                  std::cerr << "[Worker:" << instance_id_
                            << "] Stop timeout (" << stopTimeout.count()
                            << "ms), using detach..." << std::endl;
                } else if (stopStatus == std::future_status::ready) {
                  stopFuture.get();
                }
              }
                  }
                }
              }
            }
          }

          // Check shutdown flag again before detaching
          if (shutdown_requested_.load()) {
            std::cerr << "[Worker:" << instance_id_
                      << "] Shutdown requested during stop, force detaching..."
                      << std::endl;
          }

          // Detach recursively - wait for it to complete naturally
          // Use configurable timeout as fallback
          auto detachTimeout =
              shutdown_requested_.load()
                  ? TimeoutConstants::getDestinationFinalizeTimeoutDeletion()
                  : TimeoutConstants::getDestinationFinalizeTimeout();

          auto detachFuture = std::async(std::launch::async, [node]() {
            try {
              node->detach_recursively();
              return true;
            } catch (...) {
              return false;
            }
          });

          auto detachStatus = detachFuture.wait_for(detachTimeout);
          if (detachStatus == std::future_status::timeout) {
            std::cerr << "[Worker:" << instance_id_ << "] Detach timeout ("
                      << detachTimeout.count()
                      << "ms), pipeline cleanup may be incomplete" << std::endl;
          } else if (detachStatus == std::future_status::ready) {
            detachFuture.get();
          }
        } catch (const std::exception &e) {
          std::cerr << "[Worker:" << instance_id_
                    << "] Exception during stop: " << e.what() << std::endl;
          // Try force detach as fallback
          try {
            node->detach_recursively();
          } catch (...) {
            // Ignore errors during force detach
          }
        }

        break; // Only need to stop source node
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "[Worker:" << instance_id_
              << "] Error stopping pipeline: " << e.what() << std::endl;
  }

  {
    std::lock_guard<std::mutex> lock(start_pipeline_mutex_);
    pipeline_running_.store(false);
  }

  {
    std::lock_guard<std::shared_mutex> lock(state_mutex_);
    current_state_ = "stopped";
  }

  {
    std::lock_guard<std::mutex> lock(stop_pipeline_mutex_);
    stopping_pipeline_.store(false);
  }
  stop_pipeline_cv_.notify_all(); // Notify waiters that stop completed

  std::cout << "[Worker:" << instance_id_ << "] Pipeline stopped" << std::endl;
}

void WorkerHandler::cleanupPipeline() {
  stopPipeline();
  setActivePipeline(nullptr);
  {
    std::lock_guard<std::shared_mutex> lock(state_mutex_);
    current_state_ = "stopped";
  }
}

WorkerHandler::PipelineSnapshotPtr WorkerHandler::getActivePipeline() const {
  if (frame_router_) {
    return frame_router_->getActivePipeline();
  }
  std::shared_lock<std::shared_mutex> lock(active_pipeline_mutex_);
  return active_pipeline_;
}

WorkerHandler::PipelineSnapshotPtr
WorkerHandler::setActivePipeline(PipelineSnapshotPtr newPipeline) {
  if (frame_router_) {
    return frame_router_->setActivePipeline(std::move(newPipeline));
  }
  std::unique_lock<std::shared_mutex> lock(active_pipeline_mutex_);
  PipelineSnapshotPtr old = std::move(active_pipeline_);
  active_pipeline_ = std::move(newPipeline);
  return old;
}

bool WorkerHandler::ensureOutputLegForRtmp(const CreateInstanceRequest &req) {
  std::string rtmpUrl = PipelineBuilderRequestUtils::getRTMPUrl(req);
  if (rtmpUrl.empty()) {
    return true;
  }
  if (output_leg_) {
    return true;
  }
  try {
    edgeos::PersistentOutputLegParams params;
    params.channel = 0;
    params.enableOSD = true;
    params.bitrate = 1024;
    output_leg_ = std::make_shared<edgeos::PersistentOutputLeg>(
        instance_id_, rtmpUrl, params);
    frame_router_ = std::make_unique<edgeos::FrameRouter>(output_leg_);
    std::cout << "[Worker:" << instance_id_
              << "] Created persistent output leg + frame router (zero-downtime RTMP)"
              << std::endl;
    return true;
  } catch (const char* msg) {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to create output leg (SDK/lib threw string): "
              << (msg ? msg : "(null)") << std::endl;
    last_error_ = std::string("RTMP output leg: ") + (msg ? msg : "unknown");
    return false;
  } catch (const std::exception &e) {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to create output leg: " << e.what() << std::endl;
    last_error_ = e.what();
    return false;
  } catch (...) {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to create output leg: unknown exception (e.g. SDK threw non-std::exception)"
              << std::endl;
    last_error_ = "Failed to create RTMP output leg (SDK/library threw non-standard exception)";
    return false;
  }
}

void WorkerHandler::startLastFramePumpThread() {
  if (last_frame_pump_running_.exchange(true)) {
    return;
  }
  last_frame_pump_stop_.store(false);
  last_frame_pump_thread_ = std::thread([this]() {
    const auto interval = std::chrono::milliseconds(33);  // ~30 fps
    while (!last_frame_pump_stop_.load() && output_leg_ && frame_router_) {
      cv::Mat frame = frame_router_->getLastFrameCopy();
      if (!frame.empty()) {
        output_leg_->injectFrame(frame);
      }
      std::this_thread::sleep_for(interval);
    }
    last_frame_pump_running_.store(false);
  });
  std::cout << "[Worker:" << instance_id_ << "] Last-frame pump started"
            << std::endl;
}

void WorkerHandler::stopLastFramePumpThread() {
  last_frame_pump_stop_.store(true);
  if (last_frame_pump_thread_.joinable()) {
    last_frame_pump_thread_.join();
  }
  std::cout << "[Worker:" << instance_id_ << "] Last-frame pump stopped"
            << std::endl;
}

void WorkerHandler::drainPipelineSnapshot(PipelineSnapshotPtr &snapshot) {
  if (!snapshot || snapshot->empty()) {
    return;
  }
  const auto drainMs = std::chrono::milliseconds(150);
  std::this_thread::sleep_for(drainMs);
}

void WorkerHandler::sendReadySignal() {
  std::cout << "[Worker:" << instance_id_ << "] Sending ready signal"
            << std::endl;
}

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

    result += base64_chars[(triple >> 18) & 0x3F];
    result += base64_chars[(triple >> 12) & 0x3F];
    result += base64_chars[(triple >> 6) & 0x3F];
    result += base64_chars[triple & 0x3F];
  }

  // Add padding
  size_t padding = (3 - (buffer.size() % 3)) % 3;
  for (size_t p = 0; p < padding; ++p) {
    result[result.size() - 1 - p] = '=';
  }

  return result;
}

Json::Value WorkerHandler::getParamsFromConfig(const Json::Value &config) const {
  Json::Value result(Json::objectValue);
  Json::Value base;
  if (config.isMember("AdditionalParams") &&
      config["AdditionalParams"].isObject()) {
    base = config["AdditionalParams"];
  } else if (config.isMember("additionalParams") &&
             config["additionalParams"].isObject()) {
    base = config["additionalParams"];
  }
  if (base.isObject()) {
    for (const auto &key : base.getMemberNames()) {
      if (key == "input" && base[key].isObject()) {
        for (const auto &k : base[key].getMemberNames()) {
          result[k] = base[key][k];
        }
      } else if (key == "output" && base[key].isObject()) {
        for (const auto &k : base[key].getMemberNames()) {
          result[k] = base[key][k];
        }
      } else {
        result[key] = base[key];
      }
    }
  }
  // Support direct PascalCase Input/Output (e.g. PUT with top-level Input)
  if (config.isMember("Input") && config["Input"].isObject()) {
    for (const auto &k : config["Input"].getMemberNames()) {
      result[k] = config["Input"][k];
    }
  }
  if (config.isMember("Output") && config["Output"].isObject()) {
    for (const auto &k : config["Output"].getMemberNames()) {
      result[k] = config["Output"][k];
    }
  }
  return result;
}

bool WorkerHandler::applyLinesFromParamsToPipeline(const Json::Value &params) {
  if (!pipeline_running_.load() || !getActivePipeline() || getActivePipeline()->empty()) {
    return true; // No pipeline, nothing to apply
  }
  std::shared_ptr<cvedix_nodes::cvedix_ba_line_crossline_node> baCrosslineNode;
  auto pLines = getActivePipeline();
  if (pLines) {
    for (const auto &node : pLines->nodes()) {
      if (!node) continue;
      auto crosslineNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_line_crossline_node>(
              node);
      if (crosslineNode) {
        baCrosslineNode = crosslineNode;
        break;
      }
    }
  }
  if (!baCrosslineNode) {
    std::cout << "[Worker:" << instance_id_
              << "] applyLinesFromParamsToPipeline: no ba_crossline_node"
              << std::endl;
    return true; // Not a crossline pipeline, no lines to apply
  }

  std::map<int, cvedix_objects::cvedix_line> lines;

  // Priority 1: CrossingLines (JSON array string)
  if (params.isMember("CrossingLines") && params["CrossingLines"].isString()) {
    std::string linesStr = params["CrossingLines"].asString();
    if (linesStr.empty()) {
      bool ok = baCrosslineNode->set_lines(lines);
      if (ok) {
        std::cout << "[Worker:" << instance_id_
                  << "] Applied empty CrossingLines (runtime, no restart)"
                  << std::endl;
      }
      return ok;
    }
    Json::CharReaderBuilder builder;
    Json::Value linesArray;
    std::istringstream ss(linesStr);
    std::string errs;
    if (!Json::parseFromStream(builder, ss, &linesArray, &errs) ||
        !linesArray.isArray()) {
      std::cerr << "[Worker:" << instance_id_
                << "] Failed to parse CrossingLines JSON" << std::endl;
      return false;
    }
    for (Json::ArrayIndex i = 0; i < linesArray.size(); ++i) {
      const Json::Value &lineObj = linesArray[i];
      if (!lineObj.isMember("coordinates") ||
          !lineObj["coordinates"].isArray() ||
          lineObj["coordinates"].size() < 2) {
        continue;
      }
      const Json::Value &coords = lineObj["coordinates"];
      const Json::Value &startCoord = coords[0];
      const Json::Value &endCoord = coords[coords.size() - 1];
      if (!startCoord.isMember("x") || !startCoord.isMember("y") ||
          !endCoord.isMember("x") || !endCoord.isMember("y")) {
        continue;
      }
      int start_x = startCoord["x"].asInt();
      int start_y = startCoord["y"].asInt();
      int end_x = endCoord["x"].asInt();
      int end_y = endCoord["y"].asInt();
      cvedix_objects::cvedix_point start(start_x, start_y);
      cvedix_objects::cvedix_point end(end_x, end_y);
      lines[static_cast<int>(i)] = cvedix_objects::cvedix_line(start, end);
    }
  } else {
    // Priority 2: Legacy CROSSLINE_START_X/Y, CROSSLINE_END_X/Y (single line)
    auto sx = params.get("CROSSLINE_START_X", Json::Value());
    auto sy = params.get("CROSSLINE_START_Y", Json::Value());
    auto ex = params.get("CROSSLINE_END_X", Json::Value());
    auto ey = params.get("CROSSLINE_END_Y", Json::Value());
    if (sx.isString() && sy.isString() && ex.isString() && ey.isString()) {
      int start_x = std::stoi(sx.asString());
      int start_y = std::stoi(sy.asString());
      int end_x = std::stoi(ex.asString());
      int end_y = std::stoi(ey.asString());
      cvedix_objects::cvedix_point start(start_x, start_y);
      cvedix_objects::cvedix_point end(end_x, end_y);
      lines[0] = cvedix_objects::cvedix_line(start, end);
    }
  }

  if (lines.empty()) {
    logRuntimeUpdate(instance_id_, "applyLinesFromParamsToPipeline: lines.empty() -> skip");
    return true; // No line config, nothing to apply
  }

  logRuntimeUpdate(instance_id_, "applyLinesFromParamsToPipeline: lines.size()=" + std::to_string(lines.size()) + " calling set_lines()");
  bool ok = baCrosslineNode->set_lines(lines);
  if (ok) {
    std::cout << "[Worker:" << instance_id_ << "] Applied " << lines.size()
              << " line(s) at runtime (no restart)" << std::endl;
    logRuntimeUpdate(instance_id_, "applyLinesFromParamsToPipeline: set_lines()=ok");
  } else {
    std::cerr << "[Worker:" << instance_id_
              << "] set_lines() failed in applyLinesFromParamsToPipeline"
              << std::endl;
    logRuntimeUpdate(instance_id_, "applyLinesFromParamsToPipeline: set_lines()=failed");
  }
  return ok;
}

bool WorkerHandler::checkIfNeedsRebuild(const Json::Value &oldConfig,
                                        const Json::Value &newConfig) const {
  // Check if solution changed (structural change)
  if (oldConfig.isMember("Solution") && newConfig.isMember("Solution")) {
    std::string oldSolutionId;
    std::string newSolutionId;

    if (oldConfig["Solution"].isString()) {
      oldSolutionId = oldConfig["Solution"].asString();
    } else if (oldConfig["Solution"].isObject()) {
      oldSolutionId = oldConfig["Solution"].get("SolutionId", "").asString();
    }

    if (newConfig["Solution"].isString()) {
      newSolutionId = newConfig["Solution"].asString();
    } else if (newConfig["Solution"].isObject()) {
      newSolutionId = newConfig["Solution"].get("SolutionId", "").asString();
    }

    if (oldSolutionId != newSolutionId) {
      std::cout << "[Worker:" << instance_id_
                << "] Solution changed: " << oldSolutionId << " -> "
                << newSolutionId << " (requires rebuild)" << std::endl;
      return true;
    }
  } else if (oldConfig.isMember("Solution") != newConfig.isMember("Solution")) {
    // Solution added or removed
    std::cout << "[Worker:" << instance_id_
              << "] Solution presence changed (requires rebuild)" << std::endl;
    return true;
  }

  // Check if SolutionId changed (alternative field)
  if (oldConfig.isMember("SolutionId") && newConfig.isMember("SolutionId")) {
    if (oldConfig["SolutionId"].asString() !=
        newConfig["SolutionId"].asString()) {
      std::cout << "[Worker:" << instance_id_
                << "] SolutionId changed (requires rebuild)" << std::endl;
      return true;
    }
  }

  // Check for model path changes (requires rebuild as models are loaded at
  // pipeline creation). Use getParamsFromConfig to support both
  // AdditionalParams and additionalParams (API format).
  Json::Value oldParams = getParamsFromConfig(oldConfig);
  Json::Value newParams = getParamsFromConfig(newConfig);

  if (oldParams.isObject() && newParams.isObject()) {
    // Line params (CrossingLines, CROSSLINE_*) are runtime-updatable via
    // set_lines() in applyConfigToPipeline; do NOT require rebuild here.
    // Check detector model file
    if (oldParams.isMember("DETECTOR_MODEL_FILE") !=
            newParams.isMember("DETECTOR_MODEL_FILE") ||
        (oldParams.isMember("DETECTOR_MODEL_FILE") &&
         newParams.isMember("DETECTOR_MODEL_FILE") &&
         oldParams["DETECTOR_MODEL_FILE"].asString() !=
             newParams["DETECTOR_MODEL_FILE"].asString())) {
      std::cout << "[Worker:" << instance_id_
                << "] Detector model file changed (requires rebuild)"
                << std::endl;
      return true;
    }

    // Check thermal model file
    if (oldParams.isMember("DETECTOR_THERMAL_MODEL_FILE") !=
            newParams.isMember("DETECTOR_THERMAL_MODEL_FILE") ||
        (oldParams.isMember("DETECTOR_THERMAL_MODEL_FILE") &&
         newParams.isMember("DETECTOR_THERMAL_MODEL_FILE") &&
         oldParams["DETECTOR_THERMAL_MODEL_FILE"].asString() !=
             newParams["DETECTOR_THERMAL_MODEL_FILE"].asString())) {
      std::cout << "[Worker:" << instance_id_
                << "] Thermal model file changed (requires rebuild)"
                << std::endl;
      return true;
    }

    // Check other model paths (MODEL_PATH, SFACE_MODEL_PATH, etc.)
    std::vector<std::string> modelPathKeys = {"MODEL_PATH", "SFACE_MODEL_PATH",
                                              "WEIGHTS_PATH", "CONFIG_PATH"};
    for (const auto &key : modelPathKeys) {
      if (oldParams.isMember(key) != newParams.isMember(key) ||
          (oldParams.isMember(key) && newParams.isMember(key) &&
           oldParams[key].asString() != newParams[key].asString())) {
        std::cout << "[Worker:" << instance_id_ << "] " << key
                  << " changed (requires rebuild)" << std::endl;
        return true;
      }
    }

    // CrossingLines and CROSSLINE_* are runtime-updatable via set_lines() - do
    // NOT require rebuild (applied in applyConfigToPipeline for 99% uptime).
  }

  // Other structural changes that require rebuild can be added here
  return false;
}

bool WorkerHandler::applyConfigToPipeline(const Json::Value &oldConfig,
                                          const Json::Value &newConfig) {
  // Decides whether config can be applied at runtime (true) or must trigger
  // rebuild/hot-swap (false). For line-only changes + ba_crossline_node we
  // apply via set_lines() and return true (no restart).
  try {
    // Extract params (supports AdditionalParams and additionalParams + nested input)
    Json::Value oldParams = getParamsFromConfig(oldConfig);
    Json::Value newParams = getParamsFromConfig(newConfig);

    // Check for source URL changes (RTSP, RTMP, FILE_PATH)
    bool sourceUrlChanged = false;
    std::string newRtspUrl;
    std::string newRtmpUrl;
    std::string newFilePath;

    if (newParams.isMember("RTSP_SRC_URL") || newParams.isMember("RTSP_URL")) {
      std::string oldUrl = "";
      if (oldParams.isMember("RTSP_SRC_URL")) {
        oldUrl = oldParams["RTSP_SRC_URL"].asString();
      } else if (oldParams.isMember("RTSP_URL")) {
        oldUrl = oldParams["RTSP_URL"].asString();
      }

      if (newParams.isMember("RTSP_SRC_URL")) {
        newRtspUrl = newParams["RTSP_SRC_URL"].asString();
      } else if (newParams.isMember("RTSP_URL")) {
        newRtspUrl = newParams["RTSP_URL"].asString();
      }

      if (oldUrl != newRtspUrl) {
        sourceUrlChanged = true;
        std::cout << "[Worker:" << instance_id_
                  << "] RTSP URL changed: " << oldUrl << " -> " << newRtspUrl
                  << std::endl;
      }
    }

    if (newParams.isMember("RTMP_SRC_URL") || newParams.isMember("RTMP_URL")) {
      std::string oldUrl = "";
      if (oldParams.isMember("RTMP_SRC_URL")) {
        oldUrl = oldParams["RTMP_SRC_URL"].asString();
      } else if (oldParams.isMember("RTMP_URL")) {
        oldUrl = oldParams["RTMP_URL"].asString();
      }

      if (newParams.isMember("RTMP_SRC_URL")) {
        newRtmpUrl = newParams["RTMP_SRC_URL"].asString();
      } else if (newParams.isMember("RTMP_URL")) {
        newRtmpUrl = newParams["RTMP_URL"].asString();
      }

      if (oldUrl != newRtmpUrl) {
        sourceUrlChanged = true;
        std::cout << "[Worker:" << instance_id_
                  << "] RTMP URL changed: " << oldUrl << " -> " << newRtmpUrl
                  << std::endl;
      }
    }

    if (newParams.isMember("FILE_PATH")) {
      std::string oldPath = oldParams.get("FILE_PATH", "").asString();
      newFilePath = newParams["FILE_PATH"].asString();

      if (oldPath != newFilePath) {
        sourceUrlChanged = true;
        std::cout << "[Worker:" << instance_id_
                  << "] FILE_PATH changed: " << oldPath << " -> " << newFilePath
                  << std::endl;
      }
    }

    // If source URL changed, we need to rebuild pipeline
    // CVEDIX source nodes don't support changing URL/path at runtime
    if (sourceUrlChanged) {
      std::cout << "[Worker:" << instance_id_
                << "] Source URL changed, requires pipeline rebuild"
                << std::endl;
      logRuntimeUpdate(instance_id_, "applyConfigToPipeline: sourceUrlChanged=true -> rebuild");
      return false; // Trigger rebuild
    }

    // CrossingLines / CROSSLINE_*: apply at runtime (no restart)
    bool linesChanged = false;
    if (newParams.isMember("CrossingLines") &&
        oldParams.get("CrossingLines", "").asString() !=
            newParams["CrossingLines"].asString()) {
      linesChanged = true;
    }
    if (!linesChanged &&
        (newParams.isMember("CROSSLINE_START_X") ||
         newParams.isMember("CROSSLINE_END_X"))) {
      if (oldParams.get("CROSSLINE_START_X", "").asString() !=
              newParams.get("CROSSLINE_START_X", "").asString() ||
          oldParams.get("CROSSLINE_START_Y", "").asString() !=
              newParams.get("CROSSLINE_START_Y", "").asString() ||
          oldParams.get("CROSSLINE_END_X", "").asString() !=
              newParams.get("CROSSLINE_END_X", "").asString() ||
          oldParams.get("CROSSLINE_END_Y", "").asString() !=
              newParams.get("CROSSLINE_END_Y", "").asString()) {
        linesChanged = true;
      }
    }
    if (linesChanged) {
      logRuntimeUpdate(instance_id_, "applyConfigToPipeline: linesChanged=true, calling applyLinesFromParamsToPipeline");
      if (applyLinesFromParamsToPipeline(newParams)) {
        std::cout << "[Worker:" << instance_id_
                  << "] CrossingLines/CROSSLINE_* applied at runtime (no restart)"
                  << std::endl;
        logRuntimeUpdate(instance_id_, "applyConfigToPipeline: applyLinesFromParamsToPipeline=ok");
        // Continue to check other params; if only lines changed we'll return true below
      } else {
        // Only line change but set_lines() failed: do NOT trigger rebuild so the
        // instance keeps running. Config is already merged; new lines will apply
        // on next start. Avoids stopping the pipeline when SDK rejects the line update.
        std::cerr << "[Worker:" << instance_id_
                  << "] Failed to apply lines at runtime (config saved, will apply on next start)"
                  << std::endl;
        logRuntimeUpdate(instance_id_, "applyConfigToPipeline: applyLinesFromParamsToPipeline=failed (config saved, no restart)");
        // Fall through: if no other param changes we return true (no restart)
      }
    }

    // Check for Zone changes (if applicable)
    if (newConfig.isMember("Zone") || oldConfig.isMember("Zone")) {
      if (newConfig["Zone"].toStyledString() !=
          oldConfig["Zone"].toStyledString()) {
        std::cout << "[Worker:" << instance_id_
                  << "] Zone configuration changed, requires rebuild"
                  << std::endl;
        logRuntimeUpdate(instance_id_, "applyConfigToPipeline: Zone changed -> rebuild");
        return false; // Trigger rebuild
      }
    }

    // For other parameter changes, log them (exclude runtime-updatable: lines)
    // Most CVEDIX nodes don't support runtime parameter updates.
    // Use normalized string comparison so int vs string or missing key don't
    // cause false "changed" and spurious rebuild for line-only updates.
    static const std::set<std::string> runtimeLineKeys = {
        "CrossingLines", "CROSSLINE_START_X", "CROSSLINE_START_Y",
        "CROSSLINE_END_X", "CROSSLINE_END_Y"};
    std::vector<std::string> changedParams;
    for (const auto &key : newParams.getMemberNames()) {
      if (key != "RTSP_SRC_URL" && key != "RTSP_URL" && key != "RTMP_SRC_URL" &&
          key != "RTMP_URL" && key != "FILE_PATH" &&
          runtimeLineKeys.count(key) == 0) {
        std::string oldStr =
            oldParams.isMember(key) ? oldParams[key].asString() : "";
        std::string newStr = newParams[key].asString();
        if (oldStr != newStr) {
          changedParams.push_back(key);
        }
      }
    }

    if (!changedParams.empty()) {
      std::cout << "[Worker:" << instance_id_
                << "] Parameter changes detected: ";
      for (size_t i = 0; i < changedParams.size(); ++i) {
        std::cout << changedParams[i];
        if (i < changedParams.size() - 1) {
          std::cout << ", ";
        }
      }
      std::cout << std::endl;
    }

    // For parameters that don't require rebuild, config is merged
    // However, since CVEDIX nodes don't support runtime updates for most
    // params, we return false to trigger rebuild so changes take effect
    // immediately This ensures ALL config changes are applied, not just merged
    if (!changedParams.empty()) {
      std::cout << "[Worker:" << instance_id_
                << "] Parameters changed, rebuilding to apply changes"
                << std::endl;
      std::string changedStr;
      for (size_t i = 0; i < changedParams.size(); ++i) {
        if (i) changedStr += ",";
        changedStr += changedParams[i];
      }
      logRuntimeUpdate(instance_id_, "applyConfigToPipeline: changedParams=[" + changedStr + "] -> rebuild");
      return false; // Trigger rebuild to apply parameter changes
    }

    // No significant changes detected
    std::cout << "[Worker:" << instance_id_
              << "] No significant parameter changes detected" << std::endl;
    logRuntimeUpdate(instance_id_, "applyConfigToPipeline: no significant changes -> return true");
    return true;

  } catch (const std::exception &e) {
    std::cerr << "[Worker:" << instance_id_
              << "] Error applying config to pipeline: " << e.what()
              << std::endl;
    return false;
  }
}

void WorkerHandler::startConfigWatcher() {
  // Determine config file path
  // Try to get from environment or use default instances.json location
  std::string storageDir =
      EnvConfig::resolveDirectory("./instances", "instances");
  config_file_path_ = storageDir + "/instances.json";

  // Check if file exists
  if (!std::filesystem::exists(config_file_path_)) {
    std::cout << "[Worker:" << instance_id_
              << "] Config file not found: " << config_file_path_
              << ", file watching disabled" << std::endl;
    return;
  }

  // Create watcher with callback
  config_watcher_ = std::make_unique<ConfigFileWatcher>(
      config_file_path_, [this](const std::string &configPath) {
        onConfigFileChanged(configPath);
      });

  // Start watching
  config_watcher_->start();

  std::cout << "[Worker:" << instance_id_
            << "] Started watching config file: " << config_file_path_
            << std::endl;
}

void WorkerHandler::stopConfigWatcher() {
  if (config_watcher_) {
    config_watcher_->stop();
    config_watcher_.reset();
    std::cout << "[Worker:" << instance_id_ << "] Stopped config file watcher"
              << std::endl;
  }
}

void WorkerHandler::onConfigFileChanged(const std::string &configPath) {
  std::cout << "[Worker:" << instance_id_
            << "] Config file changed, reloading..." << std::endl;

  // Load new config from file
  if (!loadConfigFromFile(configPath)) {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to load config from file" << std::endl;
    return;
  }

  // Apply config changes (this will trigger rebuild if needed)
  // We create a fake UPDATE message to reuse existing update logic
  worker::IPCMessage updateMsg;
  updateMsg.type = worker::MessageType::UPDATE_INSTANCE;
  updateMsg.payload["instance_id"] = instance_id_;
  updateMsg.payload["config"] = config_;

  // Handle update (this will automatically rebuild if needed)
  handleUpdateInstance(updateMsg);

  std::cout << "[Worker:" << instance_id_
            << "] Config reloaded and applied successfully" << std::endl;
}

bool WorkerHandler::loadConfigFromFile(const std::string &configPath) {
  try {
    if (!std::filesystem::exists(configPath)) {
      std::cerr << "[Worker:" << instance_id_
                << "] Config file does not exist: " << configPath << std::endl;
      return false;
    }

    std::ifstream file(configPath);
    if (!file.is_open()) {
      std::cerr << "[Worker:" << instance_id_
                << "] Failed to open config file: " << configPath << std::endl;
      return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
      std::cerr << "[Worker:" << instance_id_
                << "] Failed to parse config file: " << errors << std::endl;
      return false;
    }

    // Extract config for this instance
    if (!root.isMember("instances") || !root["instances"].isObject()) {
      std::cerr << "[Worker:" << instance_id_
                << "] Config file does not contain instances object"
                << std::endl;
      return false;
    }

    const auto &instances = root["instances"];
    if (!instances.isMember(instance_id_)) {
      std::cerr << "[Worker:" << instance_id_
                << "] Instance not found in config file" << std::endl;
      return false;
    }

    // Load instance config
    const auto &instanceConfig = instances[instance_id_];

    // Convert to worker config format
    Json::Value newConfig;

    // Extract solution
    if (instanceConfig.isMember("SolutionId")) {
      newConfig["SolutionId"] = instanceConfig["SolutionId"];
    }

    // Extract AdditionalParams
    if (instanceConfig.isMember("AdditionalParams")) {
      newConfig["AdditionalParams"] = instanceConfig["AdditionalParams"];
    } else {
      // Build AdditionalParams from various fields
      Json::Value additionalParams;
      if (instanceConfig.isMember("RtspUrl")) {
        additionalParams["RTSP_URL"] = instanceConfig["RtspUrl"];
      }
      if (instanceConfig.isMember("RtmpUrl")) {
        additionalParams["RTMP_URL"] = instanceConfig["RtmpUrl"];
      }
      if (instanceConfig.isMember("FilePath")) {
        additionalParams["FILE_PATH"] = instanceConfig["FilePath"];
      }
      newConfig["AdditionalParams"] = additionalParams;
    }

    // Extract DisplayName
    if (instanceConfig.isMember("DisplayName")) {
      newConfig["DisplayName"] = instanceConfig["DisplayName"];
    }

    // Merge with existing config
    for (const auto &key : newConfig.getMemberNames()) {
      config_[key] = newConfig[key];
    }

    std::cout << "[Worker:" << instance_id_
              << "] Config loaded successfully from file" << std::endl;
    return true;

  } catch (const std::exception &e) {
    std::cerr << "[Worker:" << instance_id_
              << "] Exception loading config: " << e.what() << std::endl;
    return false;
  }
}

bool WorkerHandler::startSourceNodeForSnapshot(PipelineSnapshotPtr snapshot) {
  if (!snapshot || snapshot->empty()) {
    return false;
  }
  auto sourceNode = snapshot->sourceNode();
  auto rtspNode =
      std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(sourceNode);
  if (rtspNode) {
    rtspNode->start();
    return true;
  }
  auto rtmpNode =
      std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(sourceNode);
  if (rtmpNode) {
    rtmpNode->start();
    return true;
  }
  auto fileNode =
      std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(sourceNode);
  if (fileNode) {
    fileNode->start();
    return true;
  }
  auto imageNode =
      std::dynamic_pointer_cast<cvedix_nodes::cvedix_image_src_node>(sourceNode);
  if (imageNode) {
    imageNode->start();
    return true;
  }
  auto udpNode =
      std::dynamic_pointer_cast<cvedix_nodes::cvedix_udp_src_node>(sourceNode);
  if (udpNode) {
    udpNode->start();
    return true;
  }
  auto appSrcNode =
      std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_src_node>(sourceNode);
  if (appSrcNode) {
    appSrcNode->start();
    return true;
  }
  return false;
}

void WorkerHandler::stopSourceNodeForSnapshot(PipelineSnapshotPtr &snapshot,
                                              bool releaseSnapshot) {
  if (!snapshot || snapshot->empty()) {
    return;
  }
  auto node = snapshot->sourceNode();
  if (!node) {
    if (releaseSnapshot) snapshot.reset();
    return;
  }
  try {
    auto rtspNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(node);
    if (rtspNode) {
      rtspNode->stop();
    } else {
      auto rtmpNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(node);
      if (rtmpNode) {
        rtmpNode->stop();
      } else {
        auto imageNode =
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_image_src_node>(node);
        if (imageNode) {
          imageNode->stop();
        } else {
          auto udpNode =
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_udp_src_node>(node);
          if (udpNode) {
            udpNode->stop();
          } else {
            auto appSrcNode =
                std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_src_node>(node);
            if (appSrcNode) {
              appSrcNode->stop();
            } else {
              auto fileNode =
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(node);
              if (fileNode) {
                fileNode->stop();
              }
            }
          }
        }
      }
    }
    node->detach_recursively();
  } catch (const std::exception &e) {
    std::cerr << "[Worker:" << instance_id_
              << "] Error stopping old pipeline source: " << e.what() << std::endl;
  }
  if (releaseSnapshot) {
    snapshot.reset();
  }
}

bool WorkerHandler::hotSwapPipeline(const Json::Value &newConfig) {
  if (!pipeline_running_.load() || !getActivePipeline() || getActivePipeline()->empty()) {
    return buildPipeline();
  }

  // Merge newConfig into a copy of config_ so nested objects (e.g. AdditionalParams)
  // are merged, not replaced. Avoids losing params when PATCH sends only
  // AdditionalParams.CrossingLines and hot-swap path is taken.
  Json::Value tempConfig = config_;
  mergeJsonInto(tempConfig, newConfig);
  // CRITICAL: Ensure tempConfig has RTMP output so preBuildPipeline builds with
  // FrameRouterSinkNode / output_leg_ and stream is not lost after swap.
  if (config_.isMember("additionalParams") && config_["additionalParams"].isObject() &&
      config_["additionalParams"].isMember("output") &&
      config_["additionalParams"]["output"].isObject()) {
    const auto& out = config_["additionalParams"]["output"];
    bool hasRtmp = (out.isMember("RTMP_DES_URL") && !out["RTMP_DES_URL"].asString().empty()) ||
                   (out.isMember("RTMP_URL") && !out["RTMP_URL"].asString().empty());
    if (hasRtmp) {
      if (!tempConfig.isMember("additionalParams") || !tempConfig["additionalParams"].isObject()) {
        tempConfig["additionalParams"] = Json::Value(Json::objectValue);
      }
      Json::Value& tOut = tempConfig["additionalParams"]["output"];
      if (!tOut.isObject() || (tOut.get("RTMP_DES_URL", "").asString().empty() &&
                               tOut.get("RTMP_URL", "").asString().empty())) {
        tempConfig["additionalParams"]["output"] = config_["additionalParams"]["output"];
        std::cout << "[Worker:" << instance_id_
                  << "] Hot-swap: preserved RTMP output in tempConfig (avoid stream loss)"
                  << std::endl;
      }
    }
  }
  if (config_.isMember("RtmpUrl") && !config_["RtmpUrl"].asString().empty() &&
      (!tempConfig.isMember("RtmpUrl") || tempConfig["RtmpUrl"].asString().empty())) {
    tempConfig["RtmpUrl"] = config_["RtmpUrl"];
    if (tempConfig["additionalParams"].isObject()) {
      tempConfig["additionalParams"]["RTMP_URL"] = config_["RtmpUrl"];
    }
  }

  // Zero-downtime path: persistent output leg + atomic swap (no RTMP reconnect)
  if (frame_router_ && output_leg_) {
    std::cout << "[Worker:" << instance_id_
              << "] Zero-downtime pipeline swap (build new → atomic swap → drain old)..."
              << std::endl;
    auto totalStartTime = std::chrono::steady_clock::now();

    // 1) Capture last frame and start pump so RTMP never sees gap
    frame_router_->setLastFramePumpActive(true);
    if (has_frame_ && last_frame_ && !last_frame_->empty()) {
      frame_router_->setLastFrame(*last_frame_);
      startLastFramePumpThread();
    }

    // 2) Build new pipeline (reuses frame_router_ → FrameRouterSinkNode)
    building_new_pipeline_.store(true);
    new_pipeline_nodes_.clear();
    if (!preBuildPipeline(tempConfig)) {
      building_new_pipeline_.store(false);
      stopLastFramePumpThread();
      frame_router_->setLastFramePumpActive(false);
      return false;
    }
    building_new_pipeline_.store(false);
    PipelineSnapshotPtr newSnapshot =
        std::make_shared<PipelineSnapshot>(std::move(new_pipeline_nodes_));
    new_pipeline_nodes_.clear();

    // 3) Atomic swap: new becomes active; old is returned for stop + drain
    PipelineSnapshotPtr oldSnapshot = frame_router_->setActivePipeline(newSnapshot);

    // 4) Stop old pipeline source (no more frames into old graph); do not release yet
    stopSourceNodeForSnapshot(oldSnapshot, false);

    // 5) Drain in-flight frames in old graph, then release
    drainPipelineSnapshot(oldSnapshot);
    oldSnapshot.reset();

    // 6) Start new pipeline source immediately (no delay here: delay before start
    //    caused 5s of only last-frame pump -> server could close RTMP -> stream lost)
    config_ = tempConfig;
    setupFrameCaptureHook();
    setupQueueSizeTrackingHook();
    {
      std::lock_guard<std::mutex> lock(start_pipeline_mutex_);
      start_time_ = std::chrono::steady_clock::now();
      last_fps_update_ = start_time_;
      frames_processed_.store(0);
      dropped_frames_.store(0);
      current_fps_.store(0.0);
    }
    if (!startSourceNodeForSnapshot(newSnapshot)) {
      stopLastFramePumpThread();
      frame_router_->setLastFramePumpActive(false);
      return false;
    }

    // 7) Keep last-frame pump running briefly so new pipeline has time to produce first frame
    //    (avoids gap where neither pump nor new pipeline sends -> stream stays continuous)
    constexpr std::chrono::milliseconds kPumpOverlapMs(500);
    std::cout << "[Worker:" << instance_id_
              << "] Pump overlap " << kPumpOverlapMs.count() << "ms (ensure new pipeline feeds output before stopping pump)"
              << std::endl;
    std::this_thread::sleep_for(kPumpOverlapMs);
    stopLastFramePumpThread();
    frame_router_->setLastFramePumpActive(false);

    // Optional test delay AFTER new pipeline is running (so RTMP keeps receiving real frames; delay is for testing only)
    {
      const char* env = std::getenv("EDGE_AI_HOTSWAP_DELAY_SEC");
      int delaySec = env ? std::atoi(env) : 0;
      if (delaySec > 0) {
        std::cout << "[Worker:" << instance_id_
                  << "] Hot-swap test delay: sleeping " << delaySec << "s (new pipeline already running)..."
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(delaySec));
        std::cout << "[Worker:" << instance_id_
                  << "] Hot-swap test delay: done"
                  << std::endl;
      }
    }

    {
      std::lock_guard<std::shared_mutex> lock(state_mutex_);
      current_state_ = "running";
    }

    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - totalStartTime).count();
    std::cout << "[Worker:" << instance_id_
              << "] ✓ Zero-downtime swap done in " << totalDuration << "ms (no RTMP reconnect)"
              << std::endl;
    std::cout << "[Worker:" << instance_id_
              << "] New pipeline nodes=" << newSnapshot->size()
              << ", output_leg_ active -> RTMP stream via FrameRouter"
              << std::endl;
    return true;
  }

  // Legacy path: stop old → [optional delay] → build new → start new (short gap, RTMP reconnects)
  std::cout << "[Worker:" << instance_id_
            << "] Pipeline swap (stop old → build new → start new)..." << std::endl;

  auto totalStartTime = std::chrono::steady_clock::now();
  PipelineSnapshotPtr oldSnapshot = setActivePipeline(nullptr);
  {
    std::lock_guard<std::mutex> lock(start_pipeline_mutex_);
    pipeline_running_.store(false);
  }
  stopSourceNodeForSnapshot(oldSnapshot);

  // Optional test delay BEFORE build so gap is explicit; new pipeline connects to RTMP only when started (no idle connection during delay)
  {
    const char* env = std::getenv("EDGE_AI_HOTSWAP_DELAY_SEC");
    int delaySec = env ? std::atoi(env) : 0;
    if (delaySec > 0) {
      std::cout << "[Worker:" << instance_id_
                << "] Hot-swap test delay: sleeping " << delaySec << "s before building new pipeline..."
                << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(delaySec));
      std::cout << "[Worker:" << instance_id_
                << "] Hot-swap test delay: done, building new pipeline"
                << std::endl;
    }
  }

  building_new_pipeline_.store(true);
  new_pipeline_nodes_.clear();
  if (!preBuildPipeline(tempConfig)) {
    building_new_pipeline_.store(false);
    std::cerr << "[Worker:" << instance_id_
              << "] Pre-build failed after stopping old pipeline; instance needs restart"
              << std::endl;
    return false;
  }
  building_new_pipeline_.store(false);
  auto buildEndTime = std::chrono::steady_clock::now();

  PipelineSnapshotPtr newSnapshot =
      std::make_shared<PipelineSnapshot>(std::move(new_pipeline_nodes_));
  new_pipeline_nodes_.clear();

  setActivePipeline(newSnapshot);
  config_ = tempConfig;
  setupFrameCaptureHook();
  setupQueueSizeTrackingHook();

  if (!startSourceNodeForSnapshot(newSnapshot)) {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to start new pipeline source after swap" << std::endl;
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(start_pipeline_mutex_);
    pipeline_running_.store(true);
    start_time_ = std::chrono::steady_clock::now();
    last_fps_update_ = start_time_;
    frames_processed_.store(0);
    dropped_frames_.store(0);
    current_fps_.store(0.0);
  }
  {
    std::lock_guard<std::shared_mutex> lock(state_mutex_);
    current_state_ = "running";
  }

  auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - totalStartTime).count();
  auto buildDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                           buildEndTime - totalStartTime).count();
  std::cout << "[Worker:" << instance_id_
            << "] ✓ Pipeline swap done in " << totalDuration << "ms (build: "
            << buildDuration << "ms); stream output reconnects after old pipeline stopped)"
            << std::endl;
  return true;
}

bool WorkerHandler::preBuildPipeline(const Json::Value &newConfig) {
  if (!pipeline_builder_) {
    last_error_ = "Pipeline builder not initialized";
    return false;
  }

  try {
    // Parse config to CreateInstanceRequest
    CreateInstanceRequest req = parseCreateRequest(newConfig);

    // Zero-downtime: when we already have frame_router_ (persistent output leg),
    // the new pipeline MUST feed it via FrameRouterSinkNode. The builder only
    // adds FrameRouterSinkNode when req has RTMP URL. If newConfig omitted or
    // wiped output (e.g. PATCH only line but payload shape dropped RTMP), inject
    // RTMP URL from current running config_ so stream is not lost after swap.
    if (frame_router_ && PipelineBuilderRequestUtils::getRTMPUrl(req).empty()) {
      std::string fromConfig = PipelineBuilderRequestUtils::getRTMPUrl(parseCreateRequest(config_));
      if (!fromConfig.empty()) {
        req.additionalParams["RTMP_URL"] = fromConfig;
        req.additionalParams["RTMP_DES_URL"] = fromConfig;
        std::cout << "[Worker:" << instance_id_
                  << "] Pre-build: injected RTMP URL from running config (zero-downtime stream preservation)"
                  << std::endl;
      }
    }

    if (req.solution.empty()) {
      last_error_ = "No solution specified in config";
      return false;
    }

    // Get solution config from singleton
    auto optSolution =
        SolutionRegistry::getInstance().getSolution(req.solution);
    if (!optSolution.has_value()) {
      last_error_ = "Solution not found: " + req.solution;
      return false;
    }

    if (pipeline_builder_) {
      pipeline_builder_->ensureCVEDIXInitialized();
    }
    if (!ensureOutputLegForRtmp(req)) {
      return false;
    }

    // Build new pipeline (don't start it); use frame_router_ when RTMP for zero-downtime
    new_pipeline_nodes_ = pipeline_builder_->buildPipeline(optSolution.value(),
                                                           req, instance_id_, {},
                                                           frame_router_.get());

    if (new_pipeline_nodes_.empty()) {
      last_error_ = "Pipeline builder returned empty pipeline";
      return false;
    }

    std::cout << "[Worker:" << instance_id_ << "] Pre-built pipeline with "
              << new_pipeline_nodes_.size() << " nodes" << std::endl;
    return true;

  } catch (const char* msg) {
    last_error_ = std::string("Pre-build (SDK/lib threw string): ") + (msg ? msg : "(null)");
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to pre-build pipeline: " << (msg ? msg : "(null)") << std::endl;
    return false;
  } catch (const std::exception &e) {
    last_error_ = e.what();
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to pre-build pipeline: " << e.what() << std::endl;
    return false;
  } catch (...) {
    last_error_ = "Pre-build failed (unknown exception)";
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to pre-build pipeline: unknown exception" << std::endl;
    return false;
  }
}

// ============================================================================
// WorkerArgs parsing
// ============================================================================

WorkerArgs WorkerArgs::parse(int argc, char *argv[]) {
  WorkerArgs args;

  static struct option long_options[] = {
      {"instance-id", required_argument, 0, 'i'},
      {"socket", required_argument, 0, 's'},
      {"config", required_argument, 0, 'c'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

  int opt;
  int option_index = 0;

  // Reset getopt
  optind = 1;

  while ((opt = getopt_long(argc, argv, "i:s:c:h", long_options,
                            &option_index)) != -1) {
    switch (opt) {
    case 'i':
      args.instance_id = optarg;
      break;
    case 's':
      args.socket_path = optarg;
      break;
    case 'c': {
      // Parse JSON config
      Json::CharReaderBuilder builder;
      std::istringstream stream(optarg);
      std::string errors;
      if (!Json::parseFromStream(builder, stream, &args.config, &errors)) {
        args.error = "Failed to parse config JSON: " + errors;
        return args;
      }
      break;
    }
    case 'h':
      args.error = "Usage: edgeos-worker --instance-id <id> --socket <path> "
                   "[--config <json>]";
      return args;
    default:
      args.error = "Unknown option";
      return args;
    }
  }

  // Validate required arguments
  if (args.instance_id.empty()) {
    args.error = "Missing required argument: --instance-id";
    return args;
  }

  if (args.socket_path.empty()) {
    args.error = "Missing required argument: --socket";
    return args;
  }

  args.valid = true;
  return args;
}

} // namespace worker
