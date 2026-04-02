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
void InstanceRegistry::stopPipeline(
    const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes,
    bool isDeletion) {
  if (nodes.empty()) {
    return;
  }

  // CRITICAL: This function only cleans up nodes of ONE specific instance
  // Each instance has its own independent GStreamer pipeline and nodes
  // Cleanup of one instance should NOT affect other running instances
  // However, we use mutex to serialize GStreamer operations to prevent
  // conflicts if multiple instances are being stopped/started simultaneously
  //
  // IMPORTANT: Each node should have instanceId in its name (e.g.,
  // "rtsp_src_{instanceId}") This ensures we only stop nodes belonging to this
  // specific instance

  std::cerr << "[InstanceRegistry] [stopPipeline] Cleaning up " << nodes.size()
            << " nodes for this instance only" << std::endl;
  std::cerr << "[InstanceRegistry] [stopPipeline] NOTE: These nodes are "
               "isolated from other instances"
            << std::endl;
  std::cerr << "[InstanceRegistry] [stopPipeline] NOTE: Each node has unique "
               "name with instanceId prefix to prevent conflicts"
            << std::endl;
  std::cerr << "[InstanceRegistry] [stopPipeline] NOTE: No shared state or "
               "resources between different instances"
            << std::endl;

  try {
    // Check if pipeline contains DNN models (feature encoder,
    // etc.) These need extra time to finish processing and clear internal state
    bool hasDNNModels = false;
    // Removed sface_feature_encoder_node check as it's no longer used

    // CRITICAL: Stop destination nodes (RTMP) FIRST before stopping source
    // nodes This ensures GStreamer elements are properly stopped and flushed
    // before source stops sending data. This prevents GStreamer elements from
    // being in PAUSED/READY state when disposed.
    std::cerr << "[InstanceRegistry] Stopping destination nodes first..."
              << std::endl;
    for (const auto &node : nodes) {
      if (!node) {
        continue;
      }

      // Prepare RTMP destination nodes for cleanup.
      // SDK requirement: when rtmp_des is stopped/teardown, it must call shutdown(socket_fd, SHUT_RDWR) and close(socket_fd) to send TCP FIN (ZERO_DOWNTIME_ATOMIC_PIPELINE_SWAP_DESIGN.md §12). Currently cvedix_rtmp_des_node has no stop(); teardown is via detach/destroy.
      auto rtmpDesNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node);
      if (rtmpDesNode) {
        std::cerr << "[InstanceRegistry] Preparing RTMP destination node for "
                     "cleanup..."
                  << std::endl;
        // Give it time to flush buffers before we stop source
        auto sleep_time =
            isDeletion ? TimeoutConstants::getRtmpPrepareTimeoutDeletion()
                       : TimeoutConstants::getRtmpPrepareTimeout();
        std::this_thread::sleep_for(sleep_time);
        std::cerr << "[InstanceRegistry] ✓ RTMP destination node prepared"
                  << std::endl;
      }
    }

    // Give destination nodes time to flush and finalize
    // This helps reduce GStreamer warnings during cleanup and prevent
    // segmentation faults FIXED: Increased wait time to ensure elements are
    // properly set to NULL state before dispose
    // During shutdown (isDeletion), use shorter timeout to exit faster
    if (isDeletion) {
      std::cerr << "[InstanceRegistry] Waiting for destination nodes to "
                   "finalize (shutdown mode - shorter timeout)..."
                << std::endl;
      std::this_thread::sleep_for(
          TimeoutConstants::getDestinationFinalizeTimeoutDeletion());
    }

    // Now stop the source node (typically the first node)
    // This is important to stop the connection retry loop or file reading
    if (!nodes.empty() && nodes[0]) {
      // Try RTSP source node first
      auto rtspNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(
              nodes[0]);
      if (rtspNode) {
        if (isDeletion) {
          std::cerr
              << "[InstanceRegistry] Stopping RTSP source node (deletion)..."
              << std::endl;
        } else {
          std::cerr << "[InstanceRegistry] Stopping RTSP source node..."
                    << std::endl;
        }
        try {
          auto stopTime = std::chrono::steady_clock::now();
          // CRITICAL: Try stop() first, but if it blocks due to retry loop, use
          // detach_recursively() RTSP retry loops can prevent stop() from
          // returning, so we need a fallback
          std::cerr << "[InstanceRegistry] Attempting to stop RTSP node (may "
                       "take time if retry loop is active)..."
                    << std::endl;

          // CRITICAL: Use exclusive lock for cleanup operations to prevent
          // conflicts This ensures no other instance is starting GStreamer
          // while we cleanup All start operations will wait until cleanup
          // completes NOTE: This lock only protects GStreamer operations, not
          // the nodes themselves Each instance has its own independent nodes,
          // so cleanup of one instance should not affect nodes of other
          // instances CRITICAL: Lock scope is limited to actual stop/detach
          // operations only
          {
            std::unique_lock<std::shared_mutex> gstLock(gstreamer_ops_mutex_);

            // Try stop() with timeout protection using async
            // CRITICAL: Wrap in try-catch to handle case where node is being
            // destroyed by another thread
            auto stopFuture = std::async(std::launch::async, [rtspNode]() {
              try {
                // Check if node is still valid before calling stop()
                if (!rtspNode) {
                  return false;
                }
                rtspNode->stop();
                return true;
              } catch (const std::exception &e) {
                // Node may have been destroyed by another thread (RTSP monitor
                // thread)
                std::cerr << "[InstanceRegistry] Exception in async stop(): "
                          << e.what() << std::endl;
                return false;
              } catch (...) {
                // Node may have been destroyed by another thread
                return false;
              }
            });

            // Wait max 200ms for stop() to complete (or 50ms during shutdown)
            // RTSP retry loops can block stop(), so use short timeout and
            // immediately detach
            // During shutdown, use even shorter timeout to exit faster
            auto stopTimeout =
                isDeletion ? TimeoutConstants::getRtspStopTimeoutDeletion()
                           : TimeoutConstants::getRtspStopTimeout();
            auto stopStatus = stopFuture.wait_for(stopTimeout);
            if (stopStatus == std::future_status::timeout) {
              std::cerr << "[InstanceRegistry] ⚠ RTSP stop() timeout (200ms) - "
                           "retry loop may be blocking"
                        << std::endl;
              std::cerr << "[InstanceRegistry] Attempting force stop using "
                           "detach_recursively()..."
                        << std::endl;
              // Force stop using detach - this should break retry loop
              try {
                rtspNode->detach_recursively();
                std::cerr << "[InstanceRegistry] ✓ RTSP node force stopped "
                             "using detach_recursively()"
                          << std::endl;
              } catch (const std::exception &e) {
                std::cerr << "[InstanceRegistry] ✗ Exception force stopping "
                             "RTSP node: "
                          << e.what() << std::endl;
              } catch (...) {
                std::cerr << "[InstanceRegistry] ✗ Unknown error force "
                             "stopping RTSP node"
                          << std::endl;
              }
            } else if (stopStatus == std::future_status::ready) {
              try {
                if (stopFuture.get()) {
                  auto stopEndTime = std::chrono::steady_clock::now();
                  auto stopDuration =
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          stopEndTime - stopTime)
                          .count();
                  std::cerr
                      << "[InstanceRegistry] ✓ RTSP source node stopped in "
                      << stopDuration << "ms" << std::endl;
                }
              } catch (...) {
                std::cerr
                    << "[InstanceRegistry] ✗ Exception getting stop result"
                    << std::endl;
              }
            }
          } // CRITICAL: Release lock here - cleanup wait happens without lock

          // Give it more time to fully stop (without holding lock)
          // This ensures GStreamer pipeline is properly stopped before cleanup
          std::this_thread::sleep_for(
              std::chrono::milliseconds(300)); // Increased from 100ms to 300ms
        } catch (const std::exception &e) {
          std::cerr << "[InstanceRegistry] ✗ Exception stopping RTSP node: "
                    << e.what() << std::endl;
          // Try force stop as fallback
          try {
            rtspNode->detach_recursively();
            std::cerr << "[InstanceRegistry] ✓ RTSP node force stopped using "
                         "detach_recursively() (fallback)"
                      << std::endl;
          } catch (...) {
            std::cerr << "[InstanceRegistry] ✗ Force stop also failed"
                      << std::endl;
          }
        } catch (...) {
          std::cerr << "[InstanceRegistry] ✗ Unknown error stopping RTSP node"
                    << std::endl;
          // Try force stop as fallback
          try {
            rtspNode->detach_recursively();
            std::cerr << "[InstanceRegistry] ✓ RTSP node force stopped using "
                         "detach_recursively() (fallback)"
                      << std::endl;
          } catch (...) {
            std::cerr << "[InstanceRegistry] ✗ Force stop also failed"
                      << std::endl;
          }
        }
      } else {
        // Try RTMP source node
        auto rtmpNode =
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(
                nodes[0]);
        if (rtmpNode) {
          if (isDeletion) {
            std::cerr
                << "[InstanceRegistry] Stopping RTMP source node (deletion)..."
                << std::endl;
          } else {
            std::cerr << "[InstanceRegistry] Stopping RTMP source node..."
                      << std::endl;
          }
          try {
            // CRITICAL: Use exclusive lock for cleanup operations
            std::unique_lock<std::shared_mutex> gstLock(gstreamer_ops_mutex_);

            // For RTMP source, stop first, then wait, then detach
            rtmpNode->stop();

            // Give it time to stop properly before detaching
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            rtmpNode->detach_recursively();
            std::cerr << "[InstanceRegistry] ✓ RTMP source node stopped"
                      << std::endl;
          } catch (const std::exception &e) {
            std::cerr << "[InstanceRegistry] ✗ Exception stopping RTMP node: "
                      << e.what() << std::endl;
            // Try force stop as fallback
            try {
              rtmpNode->detach_recursively();
              std::cerr << "[InstanceRegistry] ✓ RTMP node force stopped using "
                           "detach_recursively()"
                        << std::endl;
            } catch (...) {
              std::cerr << "[InstanceRegistry] ✗ Force stop also failed"
                        << std::endl;
            }
          } catch (...) {
            std::cerr << "[InstanceRegistry] ✗ Unknown error stopping RTMP node"
                      << std::endl;
            // Try force stop as fallback
            try {
              rtmpNode->detach_recursively();
              std::cerr << "[InstanceRegistry] ✓ RTMP node force stopped using "
                           "detach_recursively()"
                        << std::endl;
            } catch (...) {
              std::cerr << "[InstanceRegistry] ✗ Force stop also failed"
                        << std::endl;
            }
          }
        } else {
          // Try file source node
          auto fileNode =
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(
                  nodes[0]);
          if (fileNode) {
            if (isDeletion) {
              std::cerr << "[InstanceRegistry] Stopping file source node "
                           "(deletion)..."
                        << std::endl;
            } else {
              std::cerr << "[InstanceRegistry] Stopping file source node..."
                        << std::endl;
            }
            try {
              // CRITICAL: Stop file source first before detaching
              // This ensures GStreamer pipeline is properly stopped before
              // cleanup Use exclusive lock for cleanup operations
              std::unique_lock<std::shared_mutex> gstLock(gstreamer_ops_mutex_);

              // Try to stop the file source first
              try {
                fileNode->stop();
                std::cerr << "[InstanceRegistry] ✓ File source node stopped"
                          << std::endl;
              } catch (const std::exception &e) {
                std::cerr
                    << "[InstanceRegistry] ⚠ Exception stopping file node "
                       "(will try detach): "
                    << e.what() << std::endl;
              } catch (...) {
                std::cerr << "[InstanceRegistry] ⚠ Unknown error stopping file "
                             "node (will try detach)"
                          << std::endl;
              }

              // Give it time to stop properly
              std::this_thread::sleep_for(std::chrono::milliseconds(200));

              // Now detach to stop reading and cleanup
              // But we'll keep the nodes in memory so they can be restarted
              // (unless deletion)
              fileNode->detach_recursively();
              std::cerr << "[InstanceRegistry] ✓ File source node detached"
                        << std::endl;
            } catch (const std::exception &e) {
              std::cerr << "[InstanceRegistry] ✗ Exception stopping file node: "
                        << e.what() << std::endl;
              // Try force detach as fallback
              try {
                fileNode->detach_recursively();
                std::cerr << "[InstanceRegistry] ✓ File node force detached"
                          << std::endl;
              } catch (...) {
                std::cerr << "[InstanceRegistry] ✗ Force detach also failed"
                          << std::endl;
              }
            } catch (...) {
              std::cerr
                  << "[InstanceRegistry] ✗ Unknown error stopping file node"
                  << std::endl;
              // Try force detach as fallback
              try {
                fileNode->detach_recursively();
                std::cerr << "[InstanceRegistry] ✓ File node force detached"
                          << std::endl;
              } catch (...) {
                std::cerr << "[InstanceRegistry] ✗ Force detach also failed"
                          << std::endl;
              }
            }
          } else {
            // Generic stop for other source types
            try {
              if (nodes[0]) {
                nodes[0]->detach_recursively();
              }
            } catch (...) {
              // Ignore errors
            }
          }
        }
      }
    }

    // CRITICAL: Explicitly detach all processing nodes (face_detector, etc.)
    // to stop their internal queues from processing frames
    // This prevents "queue full, dropping meta!" warnings after instance stop
    std::cerr << "[InstanceRegistry] Detaching all processing nodes to stop "
                 "internal queues..."
              << std::endl;
    for (const auto &node : nodes) {
      if (!node) {
        continue;
      }

      // Skip source and destination nodes (already handled)
      auto rtspNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(node);
      auto rtmpSrcNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(node);
      auto fileNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(node);
      auto rtmpDesNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node);

      if (rtspNode || rtmpSrcNode || fileNode || rtmpDesNode) {
        continue; // Already handled
      }

      // Detach processing nodes (feature_encoder, etc.)
      try {
        // Removed sface_feature_encoder_node cast as it's no longer used
      } catch (const std::exception &e) {
        std::cerr << "[InstanceRegistry] ⚠ Exception detaching processing node: "
                  << e.what() << std::endl;
      } catch (...) {
        std::cerr << "[InstanceRegistry] ⚠ Unknown error detaching processing "
                     "node"
                  << std::endl;
      }
    }

    // CRITICAL: After stopping source node and detaching processing nodes, wait
    // for DNN processing nodes to finish This ensures all frames in the
    // processing queue are handled and DNN models have cleared their internal
    // state before we detach or restart This prevents shape mismatch errors
    // when restarting
    if (hasDNNModels) {
      if (isDeletion) {
        std::cerr << "[InstanceRegistry] Waiting for DNN models to finish "
                     "processing (deletion, 1 second)..."
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      } else {
        // For stop (not deletion), use longer delay to ensure DNN state is
        // fully cleared This is critical to prevent shape mismatch errors when
        // restarting
        std::cerr << "[InstanceRegistry] Waiting for DNN models to finish "
                     "processing and clear state (stop, 2 seconds)..."
                  << std::endl;
        std::cerr << "[InstanceRegistry] This ensures OpenCV DNN releases all "
                     "internal state before restart"
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
      }
    }

    // CRITICAL: Now detach all destination nodes after source is stopped
    // This ensures proper cleanup order: stop destination -> stop source ->
    // detach destination -> detach source
    std::cerr << "[InstanceRegistry] Detaching destination nodes..."
              << std::endl;
    for (const auto &node : nodes) {
      if (!node) {
        continue;
      }

      auto rtmpDesNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node);
      if (rtmpDesNode) {
        try {
          // CRITICAL: Use exclusive lock for cleanup operations
          std::unique_lock<std::shared_mutex> gstLock(gstreamer_ops_mutex_);

          // Detach the node
          rtmpDesNode->detach_recursively();

          std::cerr << "[InstanceRegistry] ✓ RTMP destination node detached"
                    << std::endl;
        } catch (const std::exception &e) {
          std::cerr
              << "[InstanceRegistry] ✗ Exception detaching RTMP destination "
                 "node: "
              << e.what() << std::endl;
        } catch (...) {
          std::cerr << "[InstanceRegistry] ✗ Unknown error detaching RTMP "
                       "destination node"
                    << std::endl;
        }
      }
    }

    // Give GStreamer time to properly cleanup after detach
    // This helps reduce warnings about VideoWriter finalization and prevent
    // segmentation faults FIXED: Increased wait time to ensure GStreamer
    // elements are properly set to NULL state
    if (isDeletion) {
      std::cerr << "[InstanceRegistry] Waiting for GStreamer cleanup..."
                << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(
          800)); // Increased from 500ms to 800ms for better cleanup
      std::cerr << "[InstanceRegistry] Pipeline stopped and fully destroyed "
                   "(all nodes cleared)"
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: All nodes have been destroyed to "
                   "ensure clean state (especially OpenCV DNN)"
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: GStreamer warnings about "
                   "VideoWriter finalization are normal during cleanup"
                << std::endl;
    } else {
      // Note: We detach nodes but keep them in the pipeline vector
      // This allows the pipeline to be rebuilt when restarting
      // The nodes will be recreated when startInstance is called if needed
      std::cerr << "[InstanceRegistry] Pipeline stopped (nodes detached but "
                   "kept for potential restart)"
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: Pipeline will be automatically "
                   "rebuilt when restarting"
                << std::endl;
      if (hasDNNModels) {
        std::cerr << "[InstanceRegistry] NOTE: DNN models have been given time "
                     "to clear internal state"
                  << std::endl;
        std::cerr << "[InstanceRegistry] NOTE: This helps prevent shape "
                     "mismatch errors when restarting"
                  << std::endl;
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "[InstanceRegistry] Exception in stopPipeline: " << e.what()
              << std::endl;
    std::cerr << "[InstanceRegistry] NOTE: GStreamer warnings during cleanup "
                 "are usually harmless"
              << std::endl;
    // Swallow exception - don't let it propagate to prevent terminate handler
    // deadlock
  } catch (...) {
    std::cerr << "[InstanceRegistry] Unknown exception in stopPipeline - "
                 "caught and ignored"
              << std::endl;
    // Swallow exception - don't let it propagate to prevent terminate handler
    // deadlock
  }
  // Ensure function never throws - this prevents deadlock in terminate handler
}
