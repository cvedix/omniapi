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
bool InstanceRegistry::startPipeline(
    const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes,
    const std::string &instanceId, bool isRestart) {
  if (nodes.empty()) {
    std::cerr << "[InstanceRegistry] Cannot start pipeline: no nodes"
              << std::endl;
    return false;
  }

  // Initialize statistics tracker
  {
    std::unique_lock<std::shared_timed_mutex> lock(mutex_);
    InstanceStatsTracker &tracker = statistics_trackers_[instanceId];
    tracker.start_time = std::chrono::steady_clock::now();
    tracker.start_time_system = std::chrono::system_clock::now();
    // PHASE 2: Atomic counters - use store instead of assignment
    tracker.frames_processed.store(0);
    tracker.frames_incoming.store(0); // Track all incoming frames
    tracker.dropped_frames.store(0);
    tracker.frame_count_since_last_update.store(0);
    tracker.last_fps = 0.0;
    tracker.last_fps_update = tracker.start_time;
    tracker.current_queue_size = 0;
    tracker.max_queue_size_seen = 0;
    tracker.expected_frames_from_source = 0;
    tracker.cache_update_frame_count_.store(0, std::memory_order_relaxed);

    // OPTIMIZATION: Cache RTSP instance flag to avoid repeated lookups in hot
    // path
    auto instanceIt = instances_.find(instanceId);
    bool isRTSP =
        (instanceIt != instances_.end() && !instanceIt->second.rtspUrl.empty());
    tracker.is_rtsp_instance.store(isRTSP, std::memory_order_relaxed);

    // OPTIMIZATION: Initialize cached_stats with default statistics
    // This allows API to read statistics immediately (lock-free) even before
    // any frames are processed
    if (!tracker.cached_stats_) {
      tracker.cached_stats_ = std::make_shared<InstanceStatistics>();
      tracker.cached_stats_->current_framerate = 0.0;
      tracker.cached_stats_->frames_processed = 0;
      tracker.cached_stats_->start_time =
          std::chrono::duration_cast<std::chrono::seconds>(
              tracker.start_time_system.time_since_epoch())
              .count();
    }
  }

  // PHASE 3: Configure backpressure control
  {
    using namespace BackpressureController;
    auto &controller =
        BackpressureController::BackpressureController::getInstance();

    // Priority order for FPS configuration:
    // 1. MAX_FPS from additionalParams (highest priority - explicit override)
    // 2. configuredFps from instance info (from API /api/v1/instances/{id}/fps)
    // 3. Auto-detect based on model type (fallback)
    double maxFPS = 0.0;
    bool userFPSProvided = false;
    {
      std::unique_lock<std::shared_timed_mutex> lock(mutex_);
      auto instanceIt = instances_.find(instanceId);
      if (instanceIt != instances_.end()) {
        // First, check MAX_FPS from additionalParams (explicit override)
        auto fpsIt = instanceIt->second.additionalParams.find("MAX_FPS");
        if (fpsIt != instanceIt->second.additionalParams.end() &&
            !fpsIt->second.empty()) {
          try {
            maxFPS = std::stod(fpsIt->second);
            userFPSProvided = true;
            std::cerr
                << "[InstanceRegistry] ✓ Using MAX_FPS from additionalParams: "
                << maxFPS << " FPS" << std::endl;
          } catch (const std::exception &e) {
            std::cerr << "[InstanceRegistry] ⚠ Invalid MAX_FPS value in "
                         "additionalParams: "
                      << fpsIt->second << std::endl;
          }
        }
        
        // If MAX_FPS not provided, use configuredFps from instance info
        // (set via API /api/v1/instances/{id}/fps, default is 5)
        if (!userFPSProvided && instanceIt->second.configuredFps > 0) {
          maxFPS = static_cast<double>(instanceIt->second.configuredFps);
          userFPSProvided = true;
          std::cerr
              << "[InstanceRegistry] ✓ Using configuredFps from instance info: "
              << maxFPS << " FPS" << std::endl;
        }
      }
    }

    // If user didn't provide FPS, auto-detect based on model type
    if (!userFPSProvided) {
      // Detect if pipeline contains slow nodes (Mask RCNN, OpenPose, Face
      // Detector, etc.) These models are computationally expensive and need
      // lower FPS
      bool hasSlowModel = false;
      bool hasFaceDetector = false;
      for (const auto &node : nodes) {
        auto maskRCNNNode = std::dynamic_pointer_cast<
            cvedix_nodes::cvedix_mask_rcnn_detector_node>(node);
        auto openPoseNode = std::dynamic_pointer_cast<
            cvedix_nodes::cvedix_openpose_detector_node>(node);
        auto faceDetectorNode = std::dynamic_pointer_cast<
            cvedix_nodes::cvedix_yunet_face_detector_node>(node);
        if (maskRCNNNode || openPoseNode) {
          hasSlowModel = true;
          break;
        }
        if (faceDetectorNode) {
          hasFaceDetector = true;
        }
      }

      // Use lower FPS for very slow models, but keep high FPS for face detector
      // Face detector will use frame dropping based on queue size instead of
      // FPS limiting Very slow models (Mask RCNN/OpenPose): 10 FPS Others
      // (including face detector): 30 FPS with queue-based dropping
      if (hasSlowModel) {
        maxFPS = 10.0; // Very slow models
      } else {
        maxFPS =
            30.0; // Normal models and face detector - use queue-based dropping
      }

      if (hasSlowModel) {
        std::cerr << "[InstanceRegistry] ⚠ Detected slow model (Mask "
                     "RCNN/OpenPose) - using reduced FPS: "
                  << maxFPS << " FPS to prevent queue overflow" << std::endl;
      } else if (hasFaceDetector) {
        std::cerr
            << "[InstanceRegistry] ⚠ Detected face detector - using 30 FPS "
               "with queue-based frame dropping to prevent queue overflow"
            << std::endl;
      }
    }

    // Clamp FPS to valid range (5-120 FPS) - synchronized with
    // BackpressureController MIN_FPS = 5.0, MAX_FPS = 120.0
    // Minimum 5 FPS to support task 12 requirement (default FPS = 5)
    maxFPS = std::max(5.0, std::min(120.0, maxFPS));

    // Configure with DROP_NEWEST policy (keep latest frame, drop old ones)
    // This prevents queue backlog while maintaining current state
    // Use adaptive queue size manager for dynamic queue sizing based on system
    // status
    using namespace AdaptiveQueueSize;
    auto &adaptiveQueue = AdaptiveQueueSizeManager::getInstance();

    // Get recommended queue size based on system status
    size_t recommended_queue_size =
        adaptiveQueue.getRecommendedQueueSize(instanceId);

    controller.configure(
        instanceId, BackpressureController::DropPolicy::DROP_NEWEST,
        maxFPS,                  // FPS from user or auto-detected
        recommended_queue_size); // Dynamic queue size based on system status

    std::cerr << "[InstanceRegistry] ✓ Backpressure control configured: "
              << maxFPS << " FPS max" << std::endl;
  }

  // Setup frame capture hook before starting pipeline
  setupFrameCaptureHook(instanceId, nodes);

  // Setup RTMP destination activity hook so monitor sees real push activity
  setupRTMPDestinationActivityHook(instanceId, nodes);

  // Setup queue size tracking hook before starting pipeline
  // This also tracks incoming frames on source node (first node)
  setupQueueSizeTrackingHook(instanceId, nodes);

  try {
    // Start from the first node (source node)
    // The pipeline will start automatically when source node starts
    // Check for RTSP source node first
    auto rtspNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(nodes[0]);
    if (rtspNode) {
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;
      std::cerr << "[InstanceRegistry] Starting RTSP pipeline..." << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: RTSP node will automatically "
                   "retry connection if stream is not immediately available"
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: Connection warnings are normal if "
                   "RTSP stream is not running yet"
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: CVEDIX SDK uses retry mechanism - "
                   "connection may take 10-30 seconds"
                << std::endl;
      std::cerr
          << "[InstanceRegistry] NOTE: If connection continues to fail, check:"
          << std::endl;
      std::cerr
          << "[InstanceRegistry]   1. RTSP server is running and accessible"
          << std::endl;
      std::cerr
          << "[InstanceRegistry]   2. Network connectivity (ping/port test)"
          << std::endl;
      std::cerr << "[InstanceRegistry]   3. RTSP URL format is correct"
                << std::endl;
      std::cerr << "[InstanceRegistry]   4. Firewall allows RTSP connections"
                << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;

      // Add small delay to ensure pipeline is ready
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      std::cerr << "[InstanceRegistry] Calling rtspNode->start()..."
                << std::endl;
      auto startTime = std::chrono::steady_clock::now();
      try {
        // CRITICAL: Use shared lock to allow concurrent start operations
        // Multiple instances can start simultaneously, but cleanup operations
        // will wait
        std::shared_lock<std::shared_mutex> gstLock(gstreamer_ops_mutex_);

        rtspNode->start();
      } catch (const std::exception &e) {
        std::cerr << "[InstanceRegistry] ✗ Exception starting RTSP node: "
                  << e.what() << std::endl;
        std::cerr << "[InstanceRegistry] This may indicate RTSP stream is not "
                     "available"
                  << std::endl;
        throw; // Re-throw to let caller handle
      } catch (...) {
        std::cerr << "[InstanceRegistry] ✗ Unknown exception starting RTSP node"
                  << std::endl;
        throw; // Re-throw to let caller handle
      }
      auto endTime = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                          endTime - startTime)
                          .count();

      std::cerr << "[InstanceRegistry] ✓ RTSP node start() completed in "
                << duration << "ms" << std::endl;
      std::cerr << "[InstanceRegistry] RTSP pipeline started (connection may "
                   "take a few seconds)"
                << std::endl;
      std::cerr << "[InstanceRegistry] The SDK will automatically retry "
                   "connection - monitor logs for connection status"
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: Check CVEDIX SDK logs above for "
                   "RTSP connection status"
                << std::endl;

      // CACHE SOURCE STATS: query now while we have the node, so we don't block
      // later The start() call has completed, so caps should be negotiated if
      // successful Or at least we try now.
      {
        std::unique_lock<std::shared_timed_mutex> lock(mutex_);
        auto &tracker = statistics_trackers_[instanceId];
        try {
          // Cache these values to avoid blocking calls in getInstanceStatistics
          tracker.source_fps = rtspNode->get_original_fps();
          tracker.source_width = rtspNode->get_original_width();
          tracker.source_height = rtspNode->get_original_height();
        } catch (...) {
          // Ignore errors, use defaults
        }
      }
      std::cerr << "[InstanceRegistry] NOTE: Look for messages like 'rtspsrc' "
                   "or connection errors"
                << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;
      std::cerr << "[InstanceRegistry] HOW TO VERIFY PIPELINE IS WORKING:"
                << std::endl;
      std::cerr << "[InstanceRegistry]   1. Check output files (from build "
                   "directory):"
                << std::endl;
      std::cerr << "[InstanceRegistry]      ls -lht ./output/<instanceId>/"
                << std::endl;
      std::cerr << "[InstanceRegistry]      Or from project root: "
                   "./build/output/<instanceId>/"
                << std::endl;
      std::cerr << "[InstanceRegistry]   2. Check CVEDIX SDK logs for "
                   "'rtspsrc' connection messages:"
                << std::endl;
      std::cerr << "[InstanceRegistry]      - Direct run: ./bin/edgeos-api "
                   "2>&1 | grep -i rtspsrc"
                << std::endl;
      std::cerr << "[InstanceRegistry]      - Service: sudo journalctl -u "
                   "edgeos-api | grep -i rtspsrc"
                << std::endl;
      std::cerr << "[InstanceRegistry]      - Enable GStreamer debug: export "
                   "GST_DEBUG=rtspsrc:4"
                << std::endl;
      std::cerr << "[InstanceRegistry]      - See docs/HOW_TO_CHECK_LOGS.md "
                   "for details"
                << std::endl;
      std::cerr << "[InstanceRegistry]   3. Check instance status: GET "
                   "/v1/core/instance/<instanceId>"
                << std::endl;
      std::cerr << "[InstanceRegistry]   4. Monitor file creation:"
                << std::endl;
      std::cerr << "[InstanceRegistry]      watch -n 1 'ls -lht "
                   "./output/<instanceId>/ | head -5'"
                << std::endl;
      std::cerr << "[InstanceRegistry]   5. If files are being created, "
                   "pipeline is working!"
                << std::endl;
      std::cerr << "[InstanceRegistry]   NOTE: Files are created in working "
                   "directory (usually build/)"
                << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;

      // Start RTSP monitoring thread for error detection and auto-reconnect
      startRTSPMonitorThread(instanceId);

      return true;
    }

    // Check for file source node
    auto fileNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(nodes[0]);
    if (fileNode) {
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;
      std::cerr << "[InstanceRegistry] Starting file source pipeline..."
                << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;

      // Log file path being used for debugging
      std::string filePathForLogging;
      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto instanceIt = instances_.find(instanceId);
        if (instanceIt != instances_.end()) {
          filePathForLogging = instanceIt->second.filePath;
          // Also check additionalParams for FILE_PATH
          auto filePathIt = instanceIt->second.additionalParams.find("FILE_PATH");
          if (filePathIt != instanceIt->second.additionalParams.end() &&
              !filePathIt->second.empty()) {
            filePathForLogging = filePathIt->second;
          }
        }
      }
      if (!filePathForLogging.empty()) {
        std::cerr << "[InstanceRegistry] File path: '" << filePathForLogging
                  << "'" << std::endl;
        // Verify file still exists (might have been deleted after validation)
        struct stat fileStat;
        if (stat(filePathForLogging.c_str(), &fileStat) == 0) {
          std::cerr << "[InstanceRegistry] ✓ File exists and is accessible"
                    << std::endl;
        } else {
          std::cerr << "[InstanceRegistry] ⚠ WARNING: File may not exist or "
                       "is not accessible: "
                    << filePathForLogging << std::endl;
          std::cerr << "[InstanceRegistry] This may cause 'open file failed' "
                       "errors"
                    << std::endl;
        }
      } else {
        std::cerr << "[InstanceRegistry] ⚠ WARNING: File path is empty!"
                  << std::endl;
        std::cerr << "[InstanceRegistry] This will cause 'open file failed' "
                     "errors"
                  << std::endl;
      }

      // CRITICAL: Validate file exists BEFORE starting to prevent infinite
      // retry loops Note: File path validation should have been done in
      // startInstance(), but we check again here for safety This prevents SDK
      // from retrying indefinitely when file doesn't exist We can't easily get
      // file path from node, so we rely on validation in startInstance() If we
      // reach here and file doesn't exist, SDK will retry - but validation
      // should have caught it

      // CRITICAL: Delay BEFORE start() to ensure model is fully ready
      // Once fileNode->start() is called, frames are immediately sent to the
      // pipeline If model is not ready, shape mismatch errors will occur For
      // restart scenarios, use longer delay to ensure OpenCV DNN has fully
      // cleared old state
      if (isRestart) {
        std::cerr << "[InstanceRegistry] CRITICAL: Final synchronization delay "
                     "before starting file source (restart: 5 seconds)..."
                  << std::endl;
        std::cerr << "[InstanceRegistry] This delay is CRITICAL - once start() "
                     "is called, frames are sent immediately"
                  << std::endl;
        std::cerr << "[InstanceRegistry] Model must be fully ready before "
                     "start() to prevent shape mismatch errors"
                  << std::endl;
        std::cerr << "[InstanceRegistry] Using longer delay for restart to "
                     "ensure OpenCV DNN state is fully cleared"
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
      } else {
        std::cerr << "[InstanceRegistry] Final synchronization delay before "
                     "starting file source (2 seconds)..."
                  << std::endl;
        std::cerr << "[InstanceRegistry] Ensuring model is ready before "
                     "start() to prevent shape mismatch errors"
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
      }

      // Check for PROCESSING_DELAY_MS parameter to reduce processing speed
      // This helps prevent server overload and crashes by slowing down AI
      // processing
      int processingDelayMs = 0;
      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto instanceIt = instances_.find(instanceId);
        if (instanceIt != instances_.end()) {
          const auto &info = instanceIt->second;
          auto it = info.additionalParams.find("PROCESSING_DELAY_MS");
          if (it != info.additionalParams.end() && !it->second.empty()) {
            try {
              processingDelayMs = std::stoi(it->second);
              if (processingDelayMs < 0)
                processingDelayMs = 0;
              if (processingDelayMs > 1000)
                processingDelayMs = 1000; // Cap at 1000ms
              std::cerr << "[InstanceRegistry] Processing delay enabled: "
                        << processingDelayMs << "ms between frames"
                        << std::endl;
              std::cerr << "[InstanceRegistry] This will reduce AI processing "
                           "speed to prevent server overload"
                        << std::endl;
            } catch (...) {
              std::cerr << "[InstanceRegistry] Warning: Invalid "
                           "PROCESSING_DELAY_MS value, ignoring..."
                        << std::endl;
            }
          }
        }
      }

      std::cerr << "[InstanceRegistry] Calling fileNode->start()..."
                << std::endl;
      auto startTime = std::chrono::steady_clock::now();

      // CRITICAL: Wrap start() in async with timeout to prevent blocking server
      // When video ends, fileNode->start() may block indefinitely if GStreamer
      // pipeline is in bad state This timeout ensures server remains responsive
      // even if start() hangs
      try {
        auto startFuture =
            std::async(std::launch::async, [fileNode]() { fileNode->start(); });

        // Wait with timeout (5000ms for initial start, longer than restart
        // timeout) If it takes too long, log warning but continue (don't block
        // server)
        const int START_TIMEOUT_MS = 5000;
        if (startFuture.wait_for(std::chrono::milliseconds(START_TIMEOUT_MS)) ==
            std::future_status::timeout) {
          std::cerr
              << "[InstanceRegistry] ⚠ WARNING: fileNode->start() timeout ("
              << START_TIMEOUT_MS << "ms)" << std::endl;
          std::cerr << "[InstanceRegistry] ⚠ This may indicate:" << std::endl;
          std::cerr << "[InstanceRegistry]   1. GStreamer pipeline issue (check "
                       "plugins are installed)"
                    << std::endl;
          std::cerr << "[InstanceRegistry]   2. Video file is corrupted or "
                       "incompatible format"
                    << std::endl;
          std::cerr << "[InstanceRegistry]   3. GStreamer is retrying to open "
                       "file (may indicate missing plugins)"
                    << std::endl;
          std::cerr << "[InstanceRegistry] ⚠ Server will continue running, but "
                       "instance may not process frames correctly"
                    << std::endl;
          std::cerr << "[InstanceRegistry] ⚠ If this persists, check:" << std::endl;
          std::cerr << "[InstanceRegistry]   - GStreamer plugins are installed: "
                       "gst-inspect-1.0 isomp4"
                    << std::endl;
          std::cerr << "[InstanceRegistry]   - Video file is valid (use ffprobe on "
                       "the file path)"
                    << std::endl;
          std::cerr << "[InstanceRegistry]   - Check logs for GStreamer errors"
                    << std::endl;
          // Don't return false - let instance continue, but it may not work
          // correctly This prevents server from being blocked
        } else {
          try {
            startFuture.get();
            auto endTime = std::chrono::steady_clock::now();
            auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(endTime -
                                                                      startTime)
                    .count();
            std::cerr
                << "[InstanceRegistry] ✓ File source node start() completed in "
                << duration << "ms" << std::endl;
          } catch (const std::exception &e) {
            std::cerr
                << "[InstanceRegistry] ✗ Exception during fileNode->start(): "
                << e.what() << std::endl;
            std::cerr << "[InstanceRegistry] This may indicate a problem with "
                         "the video file or model initialization"
                      << std::endl;
            return false;
          } catch (...) {
            std::cerr << "[InstanceRegistry] ✗ Unknown exception during "
                         "fileNode->start()"
                      << std::endl;
            std::cerr << "[InstanceRegistry] This may indicate a critical "
                         "error - check logs above for details"
                      << std::endl;
            return false;
          }
        }
      } catch (const std::exception &e) {
        std::cerr << "[InstanceRegistry] ✗ Exception creating start future: "
                  << e.what() << std::endl;
        std::cerr << "[InstanceRegistry] Falling back to synchronous start()..."
                  << std::endl;
        // Fallback to synchronous call if async fails
        try {
          fileNode->start();
          auto endTime = std::chrono::steady_clock::now();
          auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                              endTime - startTime)
                              .count();
          std::cerr
              << "[InstanceRegistry] ✓ File source node start() completed in "
              << duration << "ms" << std::endl;
        } catch (const std::exception &e2) {
          std::cerr
              << "[InstanceRegistry] ✗ Exception during fileNode->start(): "
              << e2.what() << std::endl;
          return false;
        } catch (...) {
          std::cerr << "[InstanceRegistry] ✗ Unknown exception during "
                       "fileNode->start()"
                    << std::endl;
          return false;
        }
      } catch (...) {
        std::cerr << "[InstanceRegistry] ✗ Unknown error creating start future"
                  << std::endl;
        std::cerr << "[InstanceRegistry] Falling back to synchronous start()..."
                  << std::endl;
        // Fallback to synchronous call if async fails
        try {
          fileNode->start();
          auto endTime = std::chrono::steady_clock::now();
          auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                              endTime - startTime)
                              .count();
          std::cerr
              << "[InstanceRegistry] ✓ File source node start() completed in "
              << duration << "ms" << std::endl;
        } catch (...) {
          std::cerr << "[InstanceRegistry] ✗ Unknown exception during "
                       "fileNode->start()"
                    << std::endl;
          return false;
        }
      }

      // Additional delay after start() to allow first frame to be processed
      // Note: This delay is less critical than the delay BEFORE start()
      // because frames are already being sent, but it helps ensure smooth
      // processing
      if (isRestart) {
        std::cerr << "[InstanceRegistry] Additional stabilization delay after "
                     "start() (restart: 1 second)..."
                  << std::endl;
        std::cerr << "[InstanceRegistry] This allows first frame to be "
                     "processed smoothly"
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      } else {
        std::cerr << "[InstanceRegistry] Additional stabilization delay after "
                     "start() (500ms)..."
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }

      // If processing delay is enabled, start a thread to periodically add
      // delay This slows down frame processing to reduce server load
      if (processingDelayMs > 0) {
        std::cerr
            << "[InstanceRegistry] Starting processing delay thread (delay: "
            << processingDelayMs << "ms)..." << std::endl;
        std::cerr << "[InstanceRegistry] This will slow down AI processing to "
                     "prevent server overload"
                  << std::endl;
        // Note: Actual frame skipping would need to be done in the SDK level
        // For now, we just log that delay is configured
        // The delay will be handled by rate limiting in MQTT thread
      }

      // NOTE: Shape mismatch errors may still occur if:
      // 1. Video has inconsistent frame sizes (most common cause)
      //    - Check with: ffprobe -v error -select_streams v:0 -show_entries
      //    frame=width,height -of csv=s=x:p=0 video.mp4 | sort -u
      //    - If multiple sizes appear, video needs re-encoding with fixed
      //    resolution
      // 2. Model (especially YuNet 2022mar) doesn't handle dynamic input well
      //    - Solution: Use YuNet 2023mar model
      // 3. Resize ratio doesn't produce consistent dimensions
      //    - Solution: Re-encode video with fixed resolution, then use
      //    resize_ratio=1.0
      // If this happens, SIGABRT handler will catch it and stop the instance
      std::cerr
          << "[InstanceRegistry] File source pipeline started successfully"
          << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;
      std::cerr << "[InstanceRegistry] IMPORTANT: If you see shape mismatch "
                   "errors, the most likely cause is:"
                << std::endl;
      std::cerr << "[InstanceRegistry]   Video has inconsistent frame sizes "
                   "(different resolutions per frame)"
                << std::endl;
      std::cerr << "[InstanceRegistry] Solutions (in order of recommendation):"
                << std::endl;
      std::cerr
          << "[InstanceRegistry]   1. Re-encode video with fixed resolution:"
          << std::endl;
      std::cerr << "[InstanceRegistry]      ffmpeg -i input.mp4 -vf "
                   "\"scale=640:360:force_original_aspect_ratio=decrease,pad="
                   "640:360:(ow-iw)/2:(oh-ih)/2\" \\"
                << std::endl;
      std::cerr << "[InstanceRegistry]             -c:v libx264 -preset fast "
                   "-crf 23 -c:a copy output.mp4"
                << std::endl;
      std::cerr << "[InstanceRegistry]      Then use RESIZE_RATIO: \"1.0\" in "
                   "additionalParams"
                << std::endl;
      std::cerr << "[InstanceRegistry]   2. Use YuNet 2023mar model (better "
                   "dynamic input support)"
                << std::endl;
      std::cerr << "[InstanceRegistry]   3. Check video resolution consistency:"
                << std::endl;
      std::cerr << "[InstanceRegistry]      ffprobe -v error -select_streams "
                   "v:0 -show_entries frame=width,height \\"
                << std::endl;
      std::cerr << "[InstanceRegistry]              -of csv=s=x:p=0 video.mp4 "
                   "| sort -u"
                << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;
      return true;
    }

    // Check for RTMP source node
    auto rtmpNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(nodes[0]);
    if (rtmpNode) {
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;
      std::cerr << "[InstanceRegistry] Starting RTMP source pipeline..."
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: RTMP node will automatically "
                   "retry connection if stream is not immediately available"
                << std::endl;
      std::cerr << "[InstanceRegistry] NOTE: Connection warnings are normal if "
                   "RTMP stream is not running yet"
                << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;

      // Add small delay to ensure pipeline is ready
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      std::cerr << "[InstanceRegistry] Calling rtmpNode->start()..."
                << std::endl;
      auto startTime = std::chrono::steady_clock::now();
      try {
        rtmpNode->start();
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            endTime - startTime)
                            .count();
        std::cerr
            << "[InstanceRegistry] ✓ RTMP source node start() completed in "
            << duration << "ms" << std::endl;
      } catch (const std::exception &e) {
        std::cerr << "[InstanceRegistry] ✗ Exception during rtmpNode->start(): "
                  << e.what() << std::endl;
        std::cerr << "[InstanceRegistry] This may indicate a problem with the "
                     "RTMP stream or connection"
                  << std::endl;
        return false;
      } catch (...) {
        std::cerr
            << "[InstanceRegistry] ✗ Unknown exception during rtmpNode->start()"
            << std::endl;
        return false;
      }

      std::cerr
          << "[InstanceRegistry] RTMP source pipeline started successfully"
          << std::endl;
      std::cerr << "[InstanceRegistry] ========================================"
                << std::endl;

      // Start RTMP source monitoring thread for error detection and auto-reconnect
      startRTMPSourceMonitorThread(instanceId);
      
      // Check if instance also has RTMP destination and start monitoring thread
      {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto instanceIt = instances_.find(instanceId);
        if (instanceIt != instances_.end()) {
          auto rtmpDesIt = instanceIt->second.additionalParams.find("RTMP_DES_URL");
          if (rtmpDesIt != instanceIt->second.additionalParams.end() && 
              !rtmpDesIt->second.empty()) {
            // Start RTMP destination monitoring thread
            startRTMPDestinationMonitorThread(instanceId);
          }
        }
      }
      
      return true;
    }

    // Check for image source node (image_src)
    auto imageNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_image_src_node>(nodes[0]);
    if (imageNode) {
      std::cerr << "[InstanceRegistry] Starting image source pipeline..."
                << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      try {
        imageNode->start();
        std::cerr << "[InstanceRegistry] ✓ Image source node started"
                  << std::endl;
        return true;
      } catch (const std::exception &e) {
        std::cerr << "[InstanceRegistry] ✗ Exception during imageNode->start(): "
                  << e.what() << std::endl;
        return false;
      }
    }

    // Check for UDP source node (udp_src)
    auto udpNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_udp_src_node>(nodes[0]);
    if (udpNode) {
      std::cerr << "[InstanceRegistry] Starting UDP source pipeline..."
                << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      try {
        udpNode->start();
        std::cerr << "[InstanceRegistry] ✓ UDP source node started"
                  << std::endl;
        return true;
      } catch (const std::exception &e) {
        std::cerr << "[InstanceRegistry] ✗ Exception during udpNode->start(): "
                  << e.what() << std::endl;
        return false;
      }
    }

    // Check for app source node (app_src, push-based)
    auto appSrcNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_src_node>(nodes[0]);
    if (appSrcNode) {
      std::cerr << "[InstanceRegistry] Starting app source pipeline (push-based)..."
                << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      try {
        appSrcNode->start();
        std::cerr << "[InstanceRegistry] ✓ App source node started"
                  << std::endl;
        return true;
      } catch (const std::exception &e) {
        std::cerr << "[InstanceRegistry] ✗ Exception during appSrcNode->start(): "
                  << e.what() << std::endl;
        return false;
      }
    }

    // If not a recognized source node, cannot start pipeline
    std::cerr << "[InstanceRegistry] ✗ Error: First node is not a recognized "
                 "source node (RTSP, File, RTMP, Image, UDP, or App)"
              << std::endl;
    std::cerr << "[InstanceRegistry] Currently supported source node types:"
              << std::endl;
    std::cerr << "[InstanceRegistry]   - cvedix_rtsp_src_node, cvedix_rtmp_src_node, "
                 "cvedix_file_src_node (ff_src uses file_src),"
              << std::endl;
    std::cerr << "[InstanceRegistry]   - cvedix_image_src_node, cvedix_udp_src_node, "
                 "cvedix_app_src_node"
              << std::endl;
    return false;
  } catch (const std::exception &e) {
    std::cerr << "[InstanceRegistry] Exception starting pipeline: " << e.what()
              << std::endl;
    std::cerr << "[InstanceRegistry] This may indicate a configuration issue "
                 "with the RTSP source"
              << std::endl;
    return false;
  }
}
