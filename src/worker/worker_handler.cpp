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
#include <opencv2/dnn.hpp>
#include <sstream>
#include <thread>
#include <cstdlib>
#if defined(__CUDACC__) || defined(__CUDA_ARCH__) || defined(CUDA_VERSION)
#include <cuda_runtime.h>
#define HAVE_CUDA_RUNTIME 1
#else
#define HAVE_CUDA_RUNTIME 0
#endif

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
    std::lock_guard<std::mutex> threadLock(start_pipeline_thread_mutex_);
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
#if HAVE_CUDA_RUNTIME
    // Initialize CUDA context early to avoid cuDNN initialization errors
    // This ensures CUDA context is ready before OpenCV DNN tries to use cuDNN
    int device_count = 0;
    cudaError_t err = cudaGetDeviceCount(&device_count);
    if (err == cudaSuccess && device_count > 0) {
      // Get current device (should be set by CUDA_VISIBLE_DEVICES)
      int current_device = 0;
      cudaGetDevice(&current_device);
      
      // Force CUDA context initialization by creating a dummy allocation
      // This ensures cuDNN can be initialized properly later
      void* dummy_ptr = nullptr;
      cudaError_t malloc_err = cudaMalloc(&dummy_ptr, 1024);
      if (malloc_err == cudaSuccess && dummy_ptr != nullptr) {
        cudaFree(dummy_ptr);
        std::cout << "[Worker:" << instance_id_ 
                  << "] CUDA context initialized on device " << current_device
                  << std::endl;
      } else {
        std::cerr << "[Worker:" << instance_id_
                  << "] Warning: Failed to initialize CUDA context: "
                  << cudaGetErrorString(malloc_err) << std::endl;
      }
      
      // Set the CUDA device explicitly to ensure it's active
      cudaSetDevice(current_device);
      
      // Synchronize CUDA device to ensure context is fully initialized
      cudaDeviceSynchronize();
      
      std::cout << "[Worker:" << instance_id_ 
                << "] CUDA context ready for cuDNN on device " << current_device << std::endl;
      
      // Force cuDNN initialization by checking CUDA backend availability
      // This helps ensure cuDNN is ready before YOLO detector nodes try to use it
      try {
        // Check if CUDA backend is available for OpenCV DNN
        std::vector<std::pair<cv::dnn::Backend, cv::dnn::Target>> backends = 
            cv::dnn::getAvailableBackends();
        
        bool cuda_backend_available = false;
        for (const auto& backend : backends) {
          if (backend.first == cv::dnn::DNN_BACKEND_CUDA) {
            cuda_backend_available = true;
            std::cout << "[Worker:" << instance_id_ 
                      << "] CUDA backend available for OpenCV DNN (target: " 
                      << backend.second << ")" << std::endl;
            break;
          }
        }
        
        if (cuda_backend_available) {
          // Try to actually initialize cuDNN by attempting to use it
          // This will help detect version mismatch issues early
          try {
            cv::dnn::Net dummy_net;
            dummy_net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
            dummy_net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
            
            // Try to create a minimal blob to trigger cuDNN initialization
            // This helps detect cuDNN issues before YOLO nodes try to use it
            cv::Mat dummy_input = cv::Mat::ones(1, 3, CV_32F);
            cv::Mat dummy_blob;
            try {
              cv::dnn::blobFromImages(std::vector<cv::Mat>{dummy_input}, dummy_blob);
              // If we get here, basic DNN operations work
              std::cout << "[Worker:" << instance_id_ 
                        << "] cuDNN basic operations test passed" << std::endl;
            } catch (const cv::Exception& blob_e) {
              // Blob creation might fail if no model is loaded, that's OK
              // The important thing is that we tried to use DNN with CUDA backend
            }
            
            // Synchronize again to ensure cuDNN initialization is complete
            cudaDeviceSynchronize();
            std::cout << "[Worker:" << instance_id_ 
                      << "] cuDNN initialization prepared (will initialize when model loads)" << std::endl;
          } catch (const cv::Exception& cv_e) {
            // OpenCV errors - check if it's cuDNN-related
            std::string error_msg = cv_e.what();
            std::transform(error_msg.begin(), error_msg.end(), error_msg.begin(), ::tolower);
            
            bool is_cudnn_error = (error_msg.find("cudnn") != std::string::npos ||
                                   error_msg.find("cuda") != std::string::npos ||
                                   cv_e.code == cv::Error::GpuApiCallError);
            
            if (is_cudnn_error) {
              std::cerr << "[Worker:" << instance_id_
                        << "] ERROR: cuDNN/CUDA error detected: " << cv_e.what() << std::endl;
              std::cerr << "[Worker:" << instance_id_
                        << "] Error code: " << cv_e.code << ", Error: " << cv_e.err << std::endl;
              std::cerr << "[Worker:" << instance_id_
                        << "] This is likely due to cuDNN version mismatch" << std::endl;
              std::cerr << "[Worker:" << instance_id_
                        << "] Setting environment variables to force CPU backend" << std::endl;
              // Set environment variable to force OpenCV to use CPU backend
              setenv("OPENCV_DNN_BACKEND", "OPENCV", 1);
              setenv("OPENCV_DNN_TARGET", "CPU", 1);
            } else {
              std::cerr << "[Worker:" << instance_id_
                        << "] Warning: OpenCV DNN CUDA backend test failed: " << cv_e.what()
                        << std::endl;
              std::cerr << "[Worker:" << instance_id_
                        << "] Code: " << cv_e.code << ", Error: " << cv_e.err << std::endl;
            }
          }
        } else {
          std::cerr << "[Worker:" << instance_id_
                    << "] Warning: CUDA backend not available for OpenCV DNN" << std::endl;
          std::cerr << "[Worker:" << instance_id_
                    << "] YOLO detector nodes will use CPU backend" << std::endl;
        }
      } catch (const cv::Exception& e) {
        std::cerr << "[Worker:" << instance_id_
                  << "] Warning: Exception while checking CUDA backend: " << e.what()
                  << std::endl;
      } catch (const std::exception& e) {
        std::cerr << "[Worker:" << instance_id_
                  << "] Warning: Failed to check CUDA backend: " << e.what()
                  << std::endl;
      }
    } else {
      std::cerr << "[Worker:" << instance_id_
                << "] Warning: No CUDA devices available" << std::endl;
    }
#endif

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
    std::lock_guard<std::mutex> threadLock(start_pipeline_thread_mutex_);
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
