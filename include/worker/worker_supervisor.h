#pragma once

#include "instances/instance_info.h"
#include "models/create_instance_request.h"
#include "worker/ipc_protocol.h"
#include "worker/unix_socket.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <sys/types.h>
#include <thread>
#include <unordered_map>

namespace worker {

/**
 * @brief Worker process state
 */
enum class WorkerState {
  STARTING, // Process spawned, waiting for ready signal
  READY,    // Ready to accept commands
  BUSY,     // Processing a command
  STOPPING, // Shutdown requested
  STOPPED,  // Process exited normally
  CRASHED   // Process crashed or killed
};

/**
 * @brief Worker process information
 */
struct WorkerInfo {
  std::string instance_id;
  pid_t pid = -1;
  WorkerState state = WorkerState::STOPPED;
  std::string socket_path;
  std::unique_ptr<UnixSocketClient> client;
  std::chrono::steady_clock::time_point start_time;
  std::chrono::steady_clock::time_point last_heartbeat;
  int restart_count = 0;
  std::string last_error;
};

/**
 * @brief Worker Supervisor - manages worker subprocess lifecycle
 *
 * Responsibilities:
 * - Spawn worker processes
 * - Monitor worker health (heartbeat)
 * - Handle worker crashes and restart
 * - Route commands to workers via Unix sockets
 */
class WorkerSupervisor {
public:
  using StateChangeCallback =
      std::function<void(const std::string &instance_id, WorkerState old_state,
                         WorkerState new_state)>;
  using ErrorCallback = std::function<void(const std::string &instance_id,
                                           const std::string &error)>;

  /**
   * @brief Constructor
   * @param worker_executable Path to worker executable
   */
  explicit WorkerSupervisor(
      const std::string &worker_executable = "edge_ai_worker");
  ~WorkerSupervisor();

  // Non-copyable
  WorkerSupervisor(const WorkerSupervisor &) = delete;
  WorkerSupervisor &operator=(const WorkerSupervisor &) = delete;

  /**
   * @brief Start the supervisor (monitoring thread)
   */
  void start();

  /**
   * @brief Stop the supervisor and all workers
   */
  void stop();

  /**
   * @brief Spawn a new worker process for an instance
   * @param instance_id Instance ID
   * @param config Instance configuration (JSON)
   * @param gpu_device_id Optional GPU device ID (-1 to disable GPU, >=0 to use specific GPU)
   * @return true if worker spawned successfully
   */
  bool spawnWorker(const std::string &instance_id, const Json::Value &config, int gpu_device_id = -1);

  /**
   * @brief Terminate a worker process
   * @param instance_id Instance ID
   * @param force If true, use SIGKILL instead of SIGTERM
   * @return true if worker was terminated
   */
  bool terminateWorker(const std::string &instance_id, bool force = false);

  /**
   * @brief Send command to worker and get response
   * @param instance_id Instance ID
   * @param msg Message to send
   * @param timeout_ms Timeout in milliseconds
   * @return Response message
   */
  IPCMessage sendToWorker(const std::string &instance_id, const IPCMessage &msg,
                          int timeout_ms = 30000);

  /**
   * @brief Get worker state
   * @param instance_id Instance ID
   * @return Worker state (STOPPED if not found)
   */
  WorkerState getWorkerState(const std::string &instance_id) const;

  /**
   * @brief Check if worker exists and is ready
   * @param instance_id Instance ID
   */
  bool isWorkerReady(const std::string &instance_id) const;

  /**
   * @brief Get all worker instance IDs
   */
  std::vector<std::string> getWorkerIds() const;

  /**
   * @brief Set callback for worker state changes
   */
  void setStateChangeCallback(StateChangeCallback callback);

  /**
   * @brief Set callback for worker errors
   */
  void setErrorCallback(ErrorCallback callback);

  /**
   * @brief Get worker info (for debugging)
   */
  std::optional<WorkerInfo> getWorkerInfo(const std::string &instance_id) const;

  // Configuration
  void setHeartbeatInterval(int ms) { heartbeat_interval_ms_ = ms; }
  void setHeartbeatTimeout(int ms) { heartbeat_timeout_ms_ = ms; }
  void setMaxRestarts(int count) { max_restarts_ = count; }
  void setRestartDelay(int ms) { restart_delay_ms_ = ms; }

private:
  std::string worker_executable_;

  mutable std::timed_mutex workers_mutex_;
  std::unordered_map<std::string, std::unique_ptr<WorkerInfo>> workers_;

  std::atomic<bool> running_{false};
  std::thread monitor_thread_;

  StateChangeCallback state_change_callback_;
  ErrorCallback error_callback_;

  // Configuration
  int heartbeat_interval_ms_ = 5000;
  int heartbeat_timeout_ms_ = 15000;
  int max_restarts_ = 3;
  int restart_delay_ms_ = 1000;
  int worker_startup_timeout_ms_ = 30000;

  /**
   * @brief Monitor thread - checks worker health
   */
  void monitorLoop();

  /**
   * @brief Check single worker health
   */
  void checkWorkerHealth(WorkerInfo &worker);

  /**
   * @brief Handle worker crash
   */
  void handleWorkerCrash(const std::string &instance_id);

  /**
   * @brief Restart a crashed worker
   */
  bool restartWorker(const std::string &instance_id);

  /**
   * @brief Wait for worker to become ready
   */
  bool waitForWorkerReady(WorkerInfo &worker, int timeout_ms);

  /**
   * @brief Update worker state and notify callback
   */
  void setWorkerState(WorkerInfo &worker, WorkerState new_state);

  /**
   * @brief Clean up worker resources
   */
  void cleanupWorker(WorkerInfo &worker);

  /**
   * @brief Find worker executable in PATH or relative to current binary
   */
  std::string findWorkerExecutable() const;
};

} // namespace worker
