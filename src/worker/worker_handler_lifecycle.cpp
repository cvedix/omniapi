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

} // namespace worker
