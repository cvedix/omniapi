#pragma once

#include "core/frame_router.h"
#include "core/persistent_output_leg.h"
#include "core/pipeline_snapshot.h"
#include "worker/config_file_watcher.h"
#include "worker/ipc_protocol.h"
#include "worker/unix_socket.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <json/json.h>
#include <memory>
#include <mutex>
#include <opencv2/core.hpp>
#include <shared_mutex>
#include <string>

// Forward declarations for CVEDIX types
namespace cvedix_nodes {
class cvedix_node;
}

// Forward declarations for dependencies
class SolutionRegistry;
class PipelineBuilder;
class InstanceStorage;
struct CreateInstanceRequest;
struct SolutionConfig;

namespace worker {

/**
 * @brief Worker Handler - handles IPC commands in worker process
 *
 * This class runs inside the worker subprocess and:
 * - Manages a single AI instance pipeline
 * - Handles commands from supervisor via Unix socket
 * - Reports status and errors back to supervisor
 */
class WorkerHandler {
public:
  /**
   * @brief Constructor
   * @param instance_id Instance ID this worker manages
   * @param socket_path Unix socket path for IPC
   * @param config Initial configuration JSON
   */
  WorkerHandler(const std::string &instance_id, const std::string &socket_path,
                const Json::Value &config);

  ~WorkerHandler();

  // Non-copyable
  WorkerHandler(const WorkerHandler &) = delete;
  WorkerHandler &operator=(const WorkerHandler &) = delete;

  /**
   * @brief Run the worker (blocking)
   * Starts the IPC server and processes commands until shutdown
   * @return Exit code (0 = success)
   */
  int run();

  /**
   * @brief Request shutdown
   */
  void requestShutdown();

  /**
   * @brief Check if shutdown was requested
   */
  bool isShutdownRequested() const { return shutdown_requested_.load(); }

private:
  std::string instance_id_;
  std::string socket_path_;
  Json::Value config_;

  std::unique_ptr<UnixSocketServer> server_;
  std::atomic<bool> shutdown_requested_{false};

  // Dependencies (initialized in worker process)
  std::unique_ptr<PipelineBuilder> pipeline_builder_;

  // Config file watcher for automatic reload
  std::unique_ptr<ConfigFileWatcher> config_watcher_;
  std::string config_file_path_;

  // Pipeline state: atomic pipeline pointer for zero-downtime swap (Atomic Pipeline Swap pattern)
  using PipelineSnapshotPtr = std::shared_ptr<PipelineSnapshot>;
  mutable std::shared_mutex active_pipeline_mutex_;
  PipelineSnapshotPtr active_pipeline_;
  std::atomic<bool> pipeline_running_{false};

  // Zero-downtime: persistent RTMP output leg + frame router (when RTMP URL is set)
  std::shared_ptr<edgeos::PersistentOutputLeg> output_leg_;
  std::unique_ptr<edgeos::FrameRouter> frame_router_;
  // Last-frame pump during swap (keeps RTMP alive)
  std::thread last_frame_pump_thread_;
  std::atomic<bool> last_frame_pump_running_{false};
  std::atomic<bool> last_frame_pump_stop_{false};

  // State management - use shared_mutex to allow concurrent reads
  // (GET_STATISTICS/GET_STATUS) while writes (state updates) are exclusive
  // This ensures GET_STATISTICS never blocks, even when pipeline is
  // starting/stopping
  mutable std::shared_mutex state_mutex_; // Shared mutex for concurrent reads
  std::string current_state_ = "stopped";
  std::string last_error_;
  /// Full RTMP URL for players (stream key includes rtmp_des_node "_0" suffix).
  std::string published_rtmp_playback_url_;

  // Hot swap: pre-build new pipeline, then atomic swap (no mutex blocking worker loop)
  std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> new_pipeline_nodes_;
  std::atomic<bool> building_new_pipeline_{false};

  // Background thread for starting pipeline (to avoid blocking IPC server)
  std::thread start_pipeline_thread_;
  std::mutex start_pipeline_thread_mutex_; // Protect start thread lifecycle
  std::atomic<bool> starting_pipeline_{false};
  std::mutex start_pipeline_mutex_;
  std::condition_variable
      start_pipeline_cv_; // Signal when start pipeline completes

  // Pipeline stopping state
  std::atomic<bool> stopping_pipeline_{false};
  std::mutex stop_pipeline_mutex_;
  std::condition_variable
      stop_pipeline_cv_; // Signal when pipeline stop completes

  // Statistics
  std::atomic<uint64_t> frames_processed_{0};
  std::atomic<uint64_t> dropped_frames_{0};
  std::chrono::steady_clock::time_point start_time_;
  std::chrono::steady_clock::time_point last_fps_update_;
  std::atomic<double> current_fps_{0.0};
  std::atomic<size_t> queue_size_{0};
  std::string resolution_;
  std::string source_resolution_;

  // Frame cache - use shared_ptr to avoid expensive clone() operations
  // This optimization eliminates ~6MB memory copy per frame update
  // Similar to InstanceRegistry optimization for better multi-instance
  // performance
  mutable std::mutex frame_mutex_;
  std::shared_ptr<cv::Mat> last_frame_; // Changed from cv::Mat to shared_ptr
  bool has_frame_ = false;
  std::chrono::steady_clock::time_point
      last_frame_timestamp_; // Track when frame was last updated

  /**
   * @brief Initialize dependencies (solution registry, pipeline builder)
   */
  bool initializeDependencies();

  /**
   * @brief Handle incoming IPC message
   * @param msg Incoming message
   * @return Response message
   */
  IPCMessage handleMessage(const IPCMessage &msg);

  // Command handlers
  IPCMessage handlePing(const IPCMessage &msg);
  IPCMessage handleShutdown(const IPCMessage &msg);
  IPCMessage handleCreateInstance(const IPCMessage &msg);
  IPCMessage handleDeleteInstance(const IPCMessage &msg);
  IPCMessage handleStartInstance(const IPCMessage &msg);
  IPCMessage handleStopInstance(const IPCMessage &msg);
  IPCMessage handleUpdateInstance(const IPCMessage &msg);
  IPCMessage handleUpdateLines(const IPCMessage &msg);
  IPCMessage handleUpdateJams(const IPCMessage &msg);
  IPCMessage handleUpdateStops(const IPCMessage &msg);
  IPCMessage handlePushFrame(const IPCMessage &msg);
  IPCMessage handleGetStatus(const IPCMessage &msg);
  IPCMessage handleGetStatistics(const IPCMessage &msg);
  IPCMessage handleGetLastFrame(const IPCMessage &msg);

  /**
   * @brief Build pipeline from config
   * @return true if successful
   */
  bool buildPipeline();

  /**
   * @brief Start the pipeline
   * @return true if successful
   */
  bool startPipeline();

  /**
   * @brief Start the pipeline in background thread (non-blocking)
   */
  void startPipelineAsync();

  /**
   * @brief Stop the pipeline in background thread (non-blocking)
   */
  void stopPipelineAsync();

  /**
   * @brief Stop the pipeline (blocking)
   */
  void stopPipeline();

  /**
   * @brief Cleanup pipeline resources
   */
  void cleanupPipeline();

  /**
   * @brief Get active pipeline snapshot (shared lock; never blocks swap long)
   * Used by worker runtime loop and all readers.
   */
  PipelineSnapshotPtr getActivePipeline() const;

  /**
   * @brief Set active pipeline (unique lock). Returns previous pipeline for graceful stop.
   * Swap is O(1); callers must stop old pipeline's source and release returned ptr.
   */
  PipelineSnapshotPtr setActivePipeline(PipelineSnapshotPtr newPipeline);

  /**
   * @brief Send WORKER_READY message to supervisor
   */
  void sendReadySignal();

  /**
   * @brief Parse CreateInstanceRequest from JSON config
   */
  CreateInstanceRequest parseCreateRequest(const Json::Value &config) const;

  /**
   * @brief Setup frame capture hook for statistics
   */
  void setupFrameCaptureHook();

  /**
   * @brief Setup queue size tracking hook for statistics
   */
  void setupQueueSizeTrackingHook();

  /**
   * @brief Update frame cache
   */
  void updateFrameCache(const cv::Mat &frame);

  /**
   * @brief Encode frame to base64 JPEG
   */
  std::string encodeFrameToBase64(const cv::Mat &frame, int quality = 85) const;

  /**
   * @brief Handle config file change (called by ConfigFileWatcher)
   */
  void onConfigFileChanged(const std::string &configPath);

  /**
   * @brief Load config from file
   */
  bool loadConfigFromFile(const std::string &configPath);

  /**
   * @brief Start config file watcher
   */
  void startConfigWatcher();

  /**
   * @brief Stop config file watcher
   */
  void stopConfigWatcher();

  /**
   * @brief Hot swap pipeline - pre-build new pipeline and swap seamlessly
   * @param newConfig New configuration to build pipeline with
   * @return true if successful
   */
  bool hotSwapPipeline(const Json::Value &newConfig);

  /**
   * @brief Pre-build pipeline in background (for hot swap)
   * @param newConfig New configuration
   * @return true if successful
   */
  bool preBuildPipeline(const Json::Value &newConfig);

  /**
   * @brief Get flattened AdditionalParams from config (supports
   * AdditionalParams, additionalParams, and nested input/output).
   */
  Json::Value getParamsFromConfig(const Json::Value &config) const;

  /**
   * @brief Apply CrossingLines or CROSSLINE_* from params to running pipeline
   * (runtime update, no restart). Used for instance update hot-reload.
   * @param params Flattened params (from getParamsFromConfig)
   * @return true if lines were applied or no line config present
   */
  bool applyLinesFromParamsToPipeline(const Json::Value &params);

  /**
   * @brief Start source node of a pipeline snapshot (for atomic swap: start new before swap)
   */
  bool startSourceNodeForSnapshot(PipelineSnapshotPtr snapshot);

  /**
   * @brief Stop source node of a pipeline snapshot (after swap: stop old pipeline).
   * @param releaseSnapshot If true (default), resets snapshot after stop; if false, caller drains then resets.
   */
  void stopSourceNodeForSnapshot(PipelineSnapshotPtr &snapshot, bool releaseSnapshot = true);

  /**
   * @brief Ensure output_leg_ and frame_router_ exist when config has RTMP URL (for zero-downtime).
   * @param req Parsed create request (for RTMP URL and params)
   * @return true if created or already exist; false on error
   */
  bool ensureOutputLegForRtmp(const CreateInstanceRequest &req);

  /**
   * @brief Start last-frame pump thread (injects last frame into proxy during swap).
   */
  void startLastFramePumpThread();

  /**
   * @brief Stop last-frame pump thread.
   */
  void stopLastFramePumpThread();

  /**
   * @brief Drain in-flight frames in old pipeline snapshot before destruction.
   */
  void drainPipelineSnapshot(PipelineSnapshotPtr &snapshot);

  /**
   * @brief Check if config changes require pipeline rebuild
   * @param oldConfig Old configuration
   * @param newConfig New configuration
   * @return true if rebuild is needed
   */
  bool checkIfNeedsRebuild(const Json::Value &oldConfig,
                           const Json::Value &newConfig) const;

  /**
   * @brief Apply config changes to running pipeline without rebuild
   * @param oldConfig Old configuration
   * @param newConfig New configuration
   * @return true if successful
   */
  bool applyConfigToPipeline(const Json::Value &oldConfig,
                             const Json::Value &newConfig);
};

/**
 * @brief Parse command line arguments for worker process
 */
struct WorkerArgs {
  std::string instance_id;
  std::string socket_path;
  Json::Value config;
  bool valid = false;
  std::string error;

  static WorkerArgs parse(int argc, char *argv[]);
};

} // namespace worker
