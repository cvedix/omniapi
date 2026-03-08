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


} // namespace worker
