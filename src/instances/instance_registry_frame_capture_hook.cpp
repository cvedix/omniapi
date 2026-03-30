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
void InstanceRegistry::setupFrameCaptureHook(
    const std::string &instanceId,
    const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes) {
  if (nodes.empty()) {
    return;
  }

  // Find app_des_node and check if pipeline has OSD node
  // CRITICAL: We need to verify that app_des_node is attached after OSD node
  // to ensure we get processed frames
  std::shared_ptr<cvedix_nodes::cvedix_app_des_node> appDesNode;
  bool hasOSDNode = false;

  // Search through ALL nodes to find app_des_node and check for OSD node
  // IMPORTANT: Don't stop early - need to check all nodes to find OSD node
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
        std::cout << "[InstanceRegistry] ✓ Found app_des_node for instance "
                  << instanceId << std::endl;
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
        std::cout << "[InstanceRegistry] ✓ Found OSD node for instance "
                  << instanceId << ": " << typeid(*node).name() << std::endl;
      }
    }
  }

  // Setup hook on app_des_node if found
  if (appDesNode) {
    std::cout << "[InstanceRegistry] Setting up frame capture hook on "
                 "app_des_node for instance "
              << instanceId
              << " (OSD node in pipeline: " << (hasOSDNode ? "yes" : "no")
              << ")" << std::endl;
    appDesNode->set_app_des_result_hooker([this, instanceId, hasOSDNode](
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

          // PHASE 3: Check backpressure control before processing frame
          using namespace BackpressureController;
          auto &backpressure =
              BackpressureController::BackpressureController::getInstance();

          // Check if we should drop this frame (FPS limiting, queue full,
          // etc.)
          if (backpressure.shouldDropFrame(instanceId)) {
            backpressure.recordFrameDropped(instanceId);

            // Update dropped_frames counter in tracker (if tracker exists)
            // Use try_lock to avoid blocking frame processing
            {
              std::unique_lock<std::shared_timed_mutex> lock(mutex_,
                                                             std::try_to_lock);
              if (lock.owns_lock()) {
                auto trackerIt = statistics_trackers_.find(instanceId);
                if (trackerIt != statistics_trackers_.end()) {
                  // Get dropped count from backpressure controller (most
                  // accurate) Note: getStats returns a snapshot (value), not a
                  // pointer
                  auto backpressureStats = backpressure.getStats(instanceId);
                  uint64_t dropped_from_backpressure =
                      backpressureStats.frames_dropped;
                  trackerIt->second.dropped_frames.store(
                      dropped_from_backpressure, std::memory_order_relaxed);
                }
              }
            }

            return; // Drop frame early to prevent processing overhead
          }

          // PHASE 2 OPTIMIZATION: Update frame counter using atomic
          // operations
          // - NO LOCK needed! This eliminates lock contention in the hot
          // path (called every frame) OPTIMIZATION: Cache tracker pointer
          // and RTSP flag to avoid repeated lookups
          InstanceStatsTracker *trackerPtr = nullptr;
          bool isRTSPInstance = false;
          {
            // Get tracker pointer in one lock
            std::shared_lock<std::shared_timed_mutex> read_lock(mutex_);
            auto trackerIt = statistics_trackers_.find(instanceId);
            if (trackerIt != statistics_trackers_.end()) {
              trackerPtr = &trackerIt->second;
              // Read RTSP flag lock-free (cached during initialization)
              isRTSPInstance =
                  trackerPtr->is_rtsp_instance.load(std::memory_order_relaxed);
            }
          }
          // Lock released - now do atomic operations without lock

          if (trackerPtr) {
            // Atomic increments - no lock needed!
            uint64_t new_frame_count = trackerPtr->frames_processed.fetch_add(
                                           1, std::memory_order_relaxed) +
                                       1;
            trackerPtr->frame_count_since_last_update.fetch_add(
                1, std::memory_order_relaxed);

            // Log first few frames to verify frame processing
            static thread_local std::unordered_map<std::string, uint64_t>
                instance_frame_counts;
            instance_frame_counts[instanceId]++;
            if (instance_frame_counts[instanceId] <= 5 ||
                instance_frame_counts[instanceId] % 100 == 0) {
              std::cout << "[InstanceRegistry] Frame processed for instance "
                        << instanceId << ": frame_count=" << new_frame_count
                        << " (total calls: "
                        << instance_frame_counts[instanceId] << ")"
                        << std::endl;
            }

            // OPTIMIZATION: Update statistics cache periodically (every N
            // frames) This allows API to read statistics lock-free without
            // expensive calculations
            uint64_t cache_frame_count =
                trackerPtr->cache_update_frame_count_.load(
                    std::memory_order_relaxed);
            uint64_t frames_since_cache =
                (new_frame_count > cache_frame_count)
                    ? (new_frame_count - cache_frame_count)
                    : 0;

            if (frames_since_cache >=
                InstanceStatsTracker::CACHE_UPDATE_INTERVAL_FRAMES) {
              // Update cache in background (non-blocking)
              // We'll compute stats later when API reads, but mark cache as
              // needing update For now, just update the frame count to prevent
              // too frequent updates
              trackerPtr->cache_update_frame_count_.store(
                  new_frame_count, std::memory_order_relaxed);
            }
          }

          // PHASE 3: Record frame processed for backpressure tracking
          backpressure.recordFrameProcessed(instanceId);

          // DEBUG: Log frame_meta details
          static thread_local std::unordered_map<std::string, uint64_t>
              frame_capture_count;
          frame_capture_count[instanceId]++;
          uint64_t capture_count = frame_capture_count[instanceId];

          bool has_osd_frame = !frame_meta->osd_frame.empty();
          bool has_original_frame = !frame_meta->frame.empty();

          if (capture_count <= 5 || capture_count % 100 == 0) {
            std::cout
                << "[InstanceRegistry] Frame capture hook #" << capture_count
                << " for instance " << instanceId
                << " - osd_frame: " << (has_osd_frame ? "available" : "empty")
                << (has_osd_frame
                        ? (" (" + std::to_string(frame_meta->osd_frame.cols) +
                           "x" + std::to_string(frame_meta->osd_frame.rows) +
                           ")")
                        : "")
                << ", original frame: "
                << (has_original_frame ? "available" : "empty")
                << (has_original_frame
                        ? (" (" + std::to_string(frame_meta->frame.cols) + "x" +
                           std::to_string(frame_meta->frame.rows) + ")")
                        : "")
                << std::endl;
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
            if (capture_count <= 5) {
              std::cout << "[InstanceRegistry] Frame capture hook #"
                        << capture_count << " for instance " << instanceId
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
            if (capture_count <= 5) {
              std::cout
                  << "[InstanceRegistry] Frame capture hook #" << capture_count
                  << " for instance " << instanceId
                  << " - Using frame_meta->frame (from OSD node, PROCESSED): "
                  << frame_meta->frame.cols << "x" << frame_meta->frame.rows
                  << std::endl;
            }
          } else {
            // Skip caching if no OSD node (frame may be unprocessed)
            static thread_local std::unordered_map<std::string, bool>
                logged_warning;
            if (!logged_warning[instanceId]) {
              if (!hasOSDNode) {
                std::cerr << "[InstanceRegistry] ⚠ WARNING: Pipeline has no "
                             "OSD node for instance "
                          << instanceId
                          << ". Skipping frame cache to avoid returning "
                             "unprocessed frames."
                          << std::endl;
              } else {
                std::cerr << "[InstanceRegistry] ⚠ WARNING: Both osd_frame and "
                             "frame_meta->frame are empty for instance "
                          << instanceId << std::endl;
              }
              logged_warning[instanceId] = true;
            }
            if (capture_count <= 5) {
              std::cout << "[InstanceRegistry] Frame capture hook #"
                        << capture_count << " for instance " << instanceId
                        << " - SKIPPING: "
                        << (!hasOSDNode ? "No OSD node in pipeline"
                                        : "Both frames empty")
                        << std::endl;
            }
          }

          if (frameToCache && !frameToCache->empty()) {
            updateFrameCache(instanceId, *frameToCache);

            // CRITICAL: Update RTSP/RTMP activity when we receive frames
            // This is the only reliable way to know streams are actually working
            // OPTIMIZATION: Use cached RTSP flag (read lock-free above)
            if (isRTSPInstance) {
              updateRTSPActivity(instanceId);
            }
            
            // Update RTMP source activity (check if instance uses RTMP source)
            // We check by looking at the first node type
            {
              std::shared_lock<std::shared_timed_mutex> lock(mutex_);
              auto instanceIt = instances_.find(instanceId);
              if (instanceIt != instances_.end()) {
                // Check if instance has RTMP source URL in additionalParams
                auto rtmpSrcIt = instanceIt->second.additionalParams.find("RTMP_SRC_URL");
                if (rtmpSrcIt != instanceIt->second.additionalParams.end() && 
                    !rtmpSrcIt->second.empty()) {
                  updateRTMPSourceActivity(instanceId);
                }
              }
            }
            
            // Update RTMP destination activity (frames are being sent to RTMP destination)
            // Check if instance has RTMP destination
            {
              std::shared_lock<std::shared_timed_mutex> lock(mutex_);
              auto instanceIt = instances_.find(instanceId);
              if (instanceIt != instances_.end()) {
                auto rtmpDesIt = instanceIt->second.additionalParams.find("RTMP_DES_URL");
                if (rtmpDesIt != instanceIt->second.additionalParams.end() && 
                    !rtmpDesIt->second.empty()) {
                  updateRTMPDestinationActivity(instanceId);
                }
              }
            }
          }
        }
      } catch (const std::exception &e) {
        // OPTIMIZATION: Use static counter to throttle exception logging
        // Exceptions should be rare, but if they occur frequently, logging
        // every exception can impact performance
        static thread_local uint64_t exception_count = 0;
        exception_count++;

        // Only log every 100th exception to avoid performance impact
        if (exception_count % 100 == 1) {
          std::cerr << "[InstanceRegistry] [ERROR] Exception in frame "
                       "capture hook (count: "
                    << exception_count << "): " << e.what() << std::endl;
        }
      } catch (...) {
        static thread_local uint64_t unknown_exception_count = 0;
        unknown_exception_count++;

        if (unknown_exception_count % 100 == 1) {
          std::cerr << "[InstanceRegistry] [ERROR] Unknown exception in frame "
                       "capture hook (count: "
                    << unknown_exception_count << ")" << std::endl;
        }
      }
    });

    std::cerr << "[InstanceRegistry] ✓ Frame capture hook setup completed for "
                 "instance: "
              << instanceId << std::endl;
    return;
  }

  // If no app_des_node found, log warning
  if (!appDesNode) {
    std::cerr << "[InstanceRegistry] ⚠ Warning: No app_des_node found in "
                 "pipeline for instance: "
              << instanceId << std::endl;
    std::cerr << "[InstanceRegistry] Frame capture will not be available. "
                 "Consider adding app_des_node to pipeline."
              << std::endl;
  }
}
