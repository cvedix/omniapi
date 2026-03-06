#pragma once

#include "core/pipeline_builder.h"
#include "core/resource_manager.h"
#include "instances/instance_info.h"
#include "instances/instance_statistics.h"
#include "instances/instance_storage.h"
#include "models/create_instance_request.h"
#include "solutions/solution_registry.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Forward declarations
namespace cvedix_nodes {
class cvedix_node;
}

// Include MP4DirectoryWatcher header for unique_ptr (needs complete type)
#include "utils/mp4_directory_watcher.h"

/**
 * @brief Instance Registry
 *
 * Manages AI instances and their pipelines.
 * Handles creation, deletion, starting, and stopping of instances.
 */
class InstanceRegistry {
public:
  /**
   * @brief Constructor
   * @param solutionRegistry Reference to solution registry
   * @param pipelineBuilder Reference to pipeline builder
   * @param instanceStorage Reference to instance storage
   */
  InstanceRegistry(SolutionRegistry &solutionRegistry,
                   PipelineBuilder &pipelineBuilder,
                   InstanceStorage &instanceStorage);

  /**
   * @brief Create a new instance
   * @param req Create instance request
   * @return Instance ID if successful, empty string otherwise
   */
  std::string createInstance(const CreateInstanceRequest &req);

  /**
   * @brief Delete an instance
   * @param instanceId Instance ID
   * @return true if successful
   */
  bool deleteInstance(const std::string &instanceId);

  /**
   * @brief Get instance information
   * @param instanceId Instance ID
   * @return Instance info if found, empty optional otherwise
   */
  std::optional<InstanceInfo> getInstance(const std::string &instanceId) const;

  /**
   * @brief Start an instance (start pipeline)
   * @param instanceId Instance ID
   * @param skipAutoStop If true, skip auto-stop of running instance (for
   * restart scenario)
   * @return true if successful
   */
  bool startInstance(const std::string &instanceId, bool skipAutoStop = false);

  /**
   * @brief Stop an instance (stop pipeline)
   * @param instanceId Instance ID
   * @return true if successful
   */
  bool stopInstance(const std::string &instanceId);

  /**
   * @brief Update instance information
   * @param instanceId Instance ID
   * @param req Update instance request
   * @return true if successful
   */
  bool updateInstance(const std::string &instanceId,
                      const class UpdateInstanceRequest &req);

  /**
   * @brief Update instance from JSON config (direct config update)
   * @param instanceId Instance ID
   * @param configJson JSON config to merge (PascalCase format matching
   * instance_detail.txt)
   * @return true if successful
   */
  bool updateInstanceFromConfig(const std::string &instanceId,
                                const class Json::Value &configJson);

  /**
   * @brief List all instance IDs
   * @return Vector of instance IDs
   */
  std::vector<std::string> listInstances() const;

  /**
   * @brief Get total count of instances
   * @return Number of instances
   */
  int getInstanceCount() const;

  /**
   * @brief Get all instances info in one lock acquisition (optimized for list
   * operations)
   * @return Map of instance ID to InstanceInfo
   */
  std::unordered_map<std::string, InstanceInfo> getAllInstances() const;

  /**
   * @brief Check if instance exists
   * @param instanceId Instance ID
   * @return true if instance exists
   */
  bool hasInstance(const std::string &instanceId) const;

  /**
   * @brief Load all persistent instances from storage
   */
  void loadPersistentInstances();

  /**
   * @brief Check if instance has RTMP output
   * @param instanceId Instance ID
   * @return true if instance has RTMP output
   */
  bool hasRTMPOutput(const std::string &instanceId) const;

  /**
   * @brief Get source nodes from all running instances (for debug/analysis
   * board)
   * @return Vector of source nodes from running instances
   */
  std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>>
  getSourceNodesFromRunningInstances() const;

  /**
   * @brief Get pipeline nodes for an instance (for shutdown/force detach)
   * @param instanceId Instance ID
   * @return Vector of pipeline nodes if instance exists and has pipeline, empty
   * vector otherwise
   */
  std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>>
  getInstanceNodes(const std::string &instanceId) const;

  /**
   * @brief Check and increment retry counter for instances stuck in retry loop
   * This should be called periodically to monitor instances
   * @return Number of instances that reached retry limit and were stopped
   */
  int checkAndHandleRetryLimits();

  /**
   * @brief Get instance config as JSON (config format, not state)
   * @param instanceId Instance ID
   * @return JSON config if instance exists, empty JSON otherwise
   */
  Json::Value getInstanceConfig(const std::string &instanceId) const;

  /**
   * @brief Get instance statistics
   * @param instanceId Instance ID
   * @return Statistics info if instance exists and is running, empty optional
   * otherwise
   * @note This method may update tracker with latest FPS/resolution information
   */
  std::optional<InstanceStatistics>
  getInstanceStatistics(const std::string &instanceId);

  /**
   * @brief Get last frame from instance (cached frame)
   * @param instanceId Instance ID
   * @return Base64-encoded JPEG frame string, empty string if no frame
   * available
   */
  std::string getLastFrame(const std::string &instanceId) const;

private:
  SolutionRegistry &solution_registry_;
  PipelineBuilder &pipeline_builder_;
  InstanceStorage &instance_storage_;

  mutable std::shared_timed_mutex
      mutex_; // Use shared_timed_mutex to allow multiple concurrent readers
              // (getAllInstances) while writers (start/stop) are exclusive, and
              // support timeout operations
  std::unordered_map<std::string, InstanceInfo> instances_;
  std::unordered_map<std::string,
                     std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>>>
      pipelines_;

  // Thread management for video loop monitoring threads
  std::unordered_map<std::string, std::atomic<bool>>
      video_loop_thread_stop_flags_;
  std::unordered_map<std::string, std::thread> video_loop_threads_;
  mutable std::mutex
      thread_mutex_; // Separate mutex for thread management to avoid deadlock

  // Thread management for RTSP connection monitoring and auto-reconnect
  std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>>
      rtsp_monitor_stop_flags_;
  std::unordered_map<std::string, std::thread> rtsp_monitor_threads_;
  mutable std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      rtsp_last_activity_; // Track last frame received time (mutable for const
                           // methods)
  std::unordered_map<std::string, std::atomic<int>>
      rtsp_reconnect_attempts_; // Track reconnect attempts
  std::unordered_map<std::string, std::atomic<bool>>
      rtsp_has_connected_; // Track if RTSP has ever successfully connected (to
                           // distinguish initial connection from disconnection)
  mutable std::mutex
      rtsp_monitor_mutex_; // Separate mutex for RTSP monitor thread management

  // Thread management for RTMP source connection monitoring and auto-reconnect
  std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>>
      rtmp_src_monitor_stop_flags_;
  std::unordered_map<std::string, std::thread> rtmp_src_monitor_threads_;
  mutable std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      rtmp_src_last_activity_; // Track last frame received time
  std::unordered_map<std::string, std::atomic<int>>
      rtmp_src_reconnect_attempts_; // Track reconnect attempts
  std::unordered_map<std::string, std::atomic<bool>>
      rtmp_src_has_connected_; // Track if RTMP source has ever successfully connected
  mutable std::mutex
      rtmp_src_monitor_mutex_; // Separate mutex for RTMP source monitor thread management

  // Thread management for RTMP destination connection monitoring and auto-reconnect
  std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>>
      rtmp_des_monitor_stop_flags_;
  std::unordered_map<std::string, std::thread> rtmp_des_monitor_threads_;
  mutable std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      rtmp_des_last_activity_; // Track last frame sent time
  std::unordered_map<std::string, std::atomic<int>>
      rtmp_des_reconnect_attempts_; // Track reconnect attempts
  std::unordered_map<std::string, std::atomic<bool>>
      rtmp_des_has_connected_; // Track if RTMP destination has ever successfully connected
  mutable std::mutex
      rtmp_des_monitor_mutex_; // Separate mutex for RTMP destination monitor thread management

  // MP4 directory watchers for auto-converting recordings
  std::unordered_map<std::string,
                     std::unique_ptr<class MP4Finalizer::MP4DirectoryWatcher>>
      mp4_watchers_;
  mutable std::mutex mp4_watcher_mutex_; // Mutex for MP4 watcher management

  // GPU allocation tracking (maps instance ID to GPU allocation)
  // Note: In in-process mode, all instances share the same process,
  // so we can't set CUDA_VISIBLE_DEVICES per instance. However, we still
  // track allocations to manage GPU resources and ensure we don't exceed limits.
  std::unordered_map<std::string, std::shared_ptr<ResourceManager::Allocation>> gpu_allocations_;
  mutable std::mutex gpu_allocations_mutex_;

  // CRITICAL: Read-write lock to allow concurrent start operations but
  // serialize cleanup operations This allows multiple instances to start
  // simultaneously while preventing conflicts during cleanup
  // - Multiple start() operations can run concurrently (shared_lock)
  // - Cleanup operations (stop/detach) need exclusive lock to prevent conflicts
  mutable std::shared_mutex gstreamer_ops_mutex_;

  // Statistics tracking per instance
  // PHASE 2 OPTIMIZATION: Use atomic for frequently updated counters to avoid
  // locks in hot path
  struct InstanceStatsTracker {
    std::chrono::steady_clock::time_point
        start_time; // For elapsed time calculation
    std::chrono::system_clock::time_point
        start_time_system; // For Unix timestamp

    // PHASE 2: Atomic counters - no lock needed for increments
    std::atomic<uint64_t> frames_processed{
        0}; // Frames actually processed (from frame capture hook)
    std::atomic<uint64_t> frames_incoming{
        0}; // All frames from source (including dropped)
    std::atomic<uint64_t> dropped_frames{
        0}; // Frames dropped (queue full, backpressure, etc.)
    std::atomic<uint64_t> frame_count_since_last_update{0};

    // OPTIMIZATION: Cache RTSP instance flag to avoid repeated lookups
    // Set once during instance creation, read lock-free in hot path
    std::atomic<bool> is_rtsp_instance{false};

    // Protected by mutex (updated less frequently)
    double last_fps = 0.0;
    std::chrono::steady_clock::time_point last_fps_update;
    std::string resolution;         // Current processing resolution
    std::string source_resolution;  // Source resolution
    std::string format;             // Frame format
    size_t max_queue_size_seen = 0; // Maximum queue size observed
    size_t current_queue_size =
        0; // Current queue size (from last hook callback)
    uint64_t expected_frames_from_source =
        0; // Expected frames based on source FPS

    // Cached source statistics to avoid blocking SDK calls
    double source_fps = 0.0;
    int source_width = 0;
    int source_height = 0;

    // OPTIMIZATION: Pre-computed statistics cache for lock-free reads
    // Updated periodically in frame hook (every N frames) to avoid blocking API
    // calls API can read from this cache without any locks or expensive
    // calculations
    mutable std::shared_ptr<InstanceStatistics> cached_stats_;
    mutable std::atomic<uint64_t> cache_update_frame_count_{
        0}; // Track when cache was last updated
    static constexpr uint64_t CACHE_UPDATE_INTERVAL_FRAMES =
        30; // Update cache every 30 frames (~1 second at 30 FPS)
  };

  mutable std::unordered_map<std::string, InstanceStatsTracker>
      statistics_trackers_;

  // Frame cache per instance
  // OPTIMIZATION: Use shared_ptr to avoid deep copy (~6MB per frame)
  // This eliminates ~180MB/s memory bandwidth usage at 30 FPS
  using FramePtr = std::shared_ptr<cv::Mat>;

  struct FrameCache {
    FramePtr frame; // Shared pointer to frame (no copy)
    std::chrono::steady_clock::time_point timestamp;
    bool has_frame = false;
  };

  mutable std::unordered_map<std::string, FrameCache> frame_caches_;
  mutable std::timed_mutex
      frame_cache_mutex_; // Separate mutex for frame cache to avoid deadlock
                          // (timed_mutex to support timeout)

  /**
   * @brief Update frame cache for an instance
   * @param instanceId Instance ID
   * @param frame Frame to cache (will use shared ownership, no copy)
   */
  void updateFrameCache(const std::string &instanceId, const cv::Mat &frame);

  /**
   * @brief Setup frame capture hook for pipeline
   * @param instanceId Instance ID
   * @param nodes Pipeline nodes
   */
  void setupFrameCaptureHook(
      const std::string &instanceId,
      const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes);

  /**
   * @brief Setup RTMP destination activity hook on rtmp_des nodes
   * Updates RTMP destination activity when frames are actually pushed (stream
   * status or meta_handled), so monitor reflects real output instead of
   * frame cache availability.
   * @param instanceId Instance ID
   * @param nodes Pipeline nodes
   */
  void setupRTMPDestinationActivityHook(
      const std::string &instanceId,
      const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes);

  /**
   * @brief Setup queue size tracking hook for pipeline nodes
   * Also tracks incoming frames on source node (first node)
   * @param instanceId Instance ID
   * @param nodes Pipeline nodes
   */
  void setupQueueSizeTrackingHook(
      const std::string &instanceId,
      const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes);

  /**
   * @brief Encode cv::Mat frame to JPEG base64 string
   * @param frame Frame to encode
   * @param jpegQuality JPEG quality (1-100, default 85)
   * @return Base64-encoded JPEG string
   */
  std::string encodeFrameToBase64(const cv::Mat &frame,
                                  int jpegQuality = 85) const;

  /**
   * @brief Create InstanceInfo from request
   */
  InstanceInfo createInstanceInfo(const std::string &instanceId,
                                  const CreateInstanceRequest &req,
                                  const SolutionConfig *solution) const;

  /**
   * @brief Wait for DNN models to be ready using exponential backoff
   * @param nodes Pipeline nodes to check
   * @param maxWaitMs Maximum wait time in milliseconds. Use -1 for unlimited
   * wait (no timeout)
   */
  void waitForModelsReady(
      const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes,
      int maxWaitMs);

  /**
   * @brief Start pipeline nodes
   * @param nodes Pipeline nodes to start
   * @param instanceId Instance ID for accessing instance parameters
   * @param isRestart If true, this is a restart (use longer delays for model
   * initialization)
   */
  bool startPipeline(
      const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes,
      const std::string &instanceId, bool isRestart = false);

  /**
   * @brief Stop and cleanup pipeline nodes
   * @param nodes Pipeline nodes to stop
   * @param isDeletion If true, this is for deletion (full cleanup). If false,
   * just stop (can restart).
   */
  void stopPipeline(
      const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes,
      bool isDeletion = false);

  /**
   * @brief Rebuild pipeline from instance info (for instances loaded from
   * storage)
   * @param instanceId Instance ID
   * @return true if pipeline was rebuilt successfully
   */
  bool rebuildPipelineFromInstanceInfo(const std::string &instanceId);

  /**
   * @brief Build pipeline asynchronously in background thread
   * @param instanceId Instance ID
   * @param req Create instance request
   * @param solution Solution configuration
   * @param existingRTMPStreamKeys Existing RTMP stream keys for conflict detection
   */
  void buildPipelineAsync(
      const std::string &instanceId,
      const CreateInstanceRequest &req,
      const SolutionConfig &solution,
      const std::set<std::string> &existingRTMPStreamKeys);

  /**
   * @brief Start video loop monitoring thread for file-based instances
   * @param instanceId Instance ID
   */
  void startVideoLoopThread(const std::string &instanceId);

  /**
   * @brief Stop video loop monitoring thread for an instance
   * @param instanceId Instance ID
   */
  void stopVideoLoopThread(const std::string &instanceId);

  /**
   * @brief Start RTSP connection monitoring thread for an instance
   * Monitors RTSP connection status and auto-reconnects if stream is lost
   * @param instanceId Instance ID
   */
  void startRTSPMonitorThread(const std::string &instanceId);

  /**
   * @brief Stop RTSP connection monitoring thread for an instance
   * @param instanceId Instance ID
   */
  void stopRTSPMonitorThread(const std::string &instanceId);

  /**
   * @brief Update RTSP last activity time (called when frame is received)
   * @param instanceId Instance ID
   */
  void updateRTSPActivity(const std::string &instanceId);

  /**
   * @brief Attempt to reconnect RTSP stream for an instance
   * @param instanceId Instance ID
   * @param stopFlag Optional stop flag to check for early abort (nullptr if not
   * needed)
   * @return true if reconnection was successful
   */
  bool
  reconnectRTSPStream(const std::string &instanceId,
                      std::shared_ptr<std::atomic<bool>> stopFlag = nullptr);

  /**
   * @brief Start RTMP source monitoring thread for an instance
   * Monitors RTMP source connection status and auto-reconnects if stream is lost
   * @param instanceId Instance ID
   */
  void startRTMPSourceMonitorThread(const std::string &instanceId);

  /**
   * @brief Stop RTMP source monitoring thread for an instance
   * @param instanceId Instance ID
   */
  void stopRTMPSourceMonitorThread(const std::string &instanceId);

  /**
   * @brief Update RTMP source activity timestamp (called from frame capture hook)
   * @param instanceId Instance ID
   */
  void updateRTMPSourceActivity(const std::string &instanceId);

  /**
   * @brief Reconnect RTMP source stream
   * @param instanceId Instance ID
   * @param stopFlag Stop flag to abort early if instance is being stopped
   * @return true if reconnection was successful
   */
  bool reconnectRTMPSourceStream(const std::string &instanceId,
                                  std::shared_ptr<std::atomic<bool>> stopFlag = nullptr);

  /**
   * @brief Start RTMP destination monitoring thread for an instance
   * Monitors RTMP destination connection status and auto-reconnects if stream is lost
   * @param instanceId Instance ID
   */
  void startRTMPDestinationMonitorThread(const std::string &instanceId);

  /**
   * @brief Stop RTMP destination monitoring thread for an instance
   * @param instanceId Instance ID
   */
  void stopRTMPDestinationMonitorThread(const std::string &instanceId);

  /**
   * @brief Update RTMP destination activity timestamp (called from frame capture hook)
   * @param instanceId Instance ID
   */
  void updateRTMPDestinationActivity(const std::string &instanceId);

  /**
   * @brief Reconnect RTMP destination stream
   * @param instanceId Instance ID
   * @param stopFlag Stop flag to abort early if instance is being stopped
   * @return true if reconnection was successful
   */
  bool reconnectRTMPDestinationStream(const std::string &instanceId,
                                       std::shared_ptr<std::atomic<bool>> stopFlag = nullptr);
};
