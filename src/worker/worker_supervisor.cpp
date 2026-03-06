#include "worker/worker_supervisor.h"
#include "core/timeout_constants.h"
#include <chrono>
#include <climits> // for PATH_MAX
#include <cstdlib> // for setenv
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <optional>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace worker {

WorkerSupervisor::WorkerSupervisor(const std::string &worker_executable)
    : worker_executable_(worker_executable) {}

WorkerSupervisor::~WorkerSupervisor() { stop(); }

void WorkerSupervisor::start() {
  if (running_.load()) {
    return;
  }

  running_.store(true);
  monitor_thread_ = std::thread(&WorkerSupervisor::monitorLoop, this);

  std::cout << "[Supervisor] Started" << std::endl;
}

void WorkerSupervisor::stop() {
  if (!running_.load()) {
    return;
  }

  running_.store(false);

  // Stop all workers
  std::vector<std::string> worker_ids;
  {
    std::lock_guard<std::timed_mutex> lock(workers_mutex_);
    for (const auto &[id, _] : workers_) {
      worker_ids.push_back(id);
    }
  }

  for (const auto &id : worker_ids) {
    terminateWorker(id, false); // Graceful shutdown
  }

  // Wait a bit for graceful shutdown
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Force kill remaining workers
  for (const auto &id : worker_ids) {
    terminateWorker(id, true); // Force kill
  }

  if (monitor_thread_.joinable()) {
    monitor_thread_.join();
  }

  std::cout << "[Supervisor] Stopped" << std::endl;
}

bool WorkerSupervisor::spawnWorker(const std::string &instance_id,
                                   const Json::Value &config, int gpu_device_id) {
  std::lock_guard<std::timed_mutex> lock(workers_mutex_);

  // Check if worker already exists
  if (workers_.find(instance_id) != workers_.end()) {
    std::cerr << "[Supervisor] Worker already exists for instance: "
              << instance_id << std::endl;
    return false;
  }

  // Find worker executable
  std::string exe_path = findWorkerExecutable();
  if (exe_path.empty()) {
    std::cerr << "[Supervisor] ========================================" << std::endl;
    std::cerr << "[Supervisor] ✗ Worker executable not found: "
              << worker_executable_ << std::endl;
    std::cerr << "[Supervisor] ========================================" << std::endl;
    std::cerr << "[Supervisor] SOLUTION:" << std::endl;
    std::cerr << "[Supervisor]   1. Build worker executable:" << std::endl;
    std::cerr << "[Supervisor]      cd build && make edgeos_worker" << std::endl;
    std::cerr << "[Supervisor]   2. Or check if executable exists in PATH" << std::endl;
    std::cerr << "[Supervisor]   3. Run diagnostic script:" << std::endl;
    std::cerr << "[Supervisor]      ./scripts/diagnose_spawn_worker.sh" << std::endl;
    std::cerr << "[Supervisor] ========================================" << std::endl;
    return false;
  }

  // Generate socket path
  std::string socket_path = generateSocketPath(instance_id);
  cleanupSocket(socket_path); // Clean up any stale socket

  // Serialize config to JSON string for passing to worker
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  std::string config_str = Json::writeString(builder, config);

  // Fork and exec worker process
  pid_t pid = fork();

  if (pid < 0) {
    std::cerr << "[Supervisor] ========================================" << std::endl;
    std::cerr << "[Supervisor] ✗ Fork failed: " << strerror(errno) << std::endl;
    std::cerr << "[Supervisor] ========================================" << std::endl;
    std::cerr << "[Supervisor] SOLUTION:" << std::endl;
    std::cerr << "[Supervisor]   1. Check process limits:" << std::endl;
    std::cerr << "[Supervisor]      ulimit -u  # Check max user processes" << std::endl;
    std::cerr << "[Supervisor]   2. Increase limits if needed:" << std::endl;
    std::cerr << "[Supervisor]      ulimit -u 4096" << std::endl;
    std::cerr << "[Supervisor]   3. Check system limits:" << std::endl;
    std::cerr << "[Supervisor]      cat /proc/sys/kernel/pid_max" << std::endl;
    std::cerr << "[Supervisor]   4. Run diagnostic script:" << std::endl;
    std::cerr << "[Supervisor]      ./scripts/diagnose_spawn_worker.sh" << std::endl;
    std::cerr << "[Supervisor] ========================================" << std::endl;
    return false;
  }

  if (pid == 0) {
    // Child process - set CUDA_VISIBLE_DEVICES if GPU device ID is specified
    if (gpu_device_id >= 0) {
      std::string cuda_visible_devices = std::to_string(gpu_device_id);
      if (setenv("CUDA_VISIBLE_DEVICES", cuda_visible_devices.c_str(), 1) != 0) {
        std::cerr << "[Worker] Warning: Failed to set CUDA_VISIBLE_DEVICES=" 
                  << cuda_visible_devices << std::endl;
      } else {
        std::cout << "[Worker] Set CUDA_VISIBLE_DEVICES=" << cuda_visible_devices 
                  << " for instance " << instance_id << std::endl;
      }
    }
    
    // Child process - exec worker
    // Arguments: worker_executable --instance-id <id> --socket <path> --config
    // <json>
    execl(exe_path.c_str(), exe_path.c_str(), "--instance-id",
          instance_id.c_str(), "--socket", socket_path.c_str(), "--config",
          config_str.c_str(), nullptr);

    // If exec fails
    std::cerr << "[Worker] Failed to exec: " << strerror(errno) << std::endl;
    _exit(1);
  }

  // Parent process
  auto worker = std::make_unique<WorkerInfo>();
  worker->instance_id = instance_id;
  worker->pid = pid;
  worker->socket_path = socket_path;
  worker->state = WorkerState::STARTING;
  worker->start_time = std::chrono::steady_clock::now();
  worker->last_heartbeat = worker->start_time;

  std::cout << "[Supervisor] Spawned worker PID " << pid
            << " for instance: " << instance_id << std::endl;

  // Wait for worker to become ready
  bool ready = waitForWorkerReady(*worker, worker_startup_timeout_ms_);

  if (!ready) {
    std::cerr << "[Supervisor] ========================================" << std::endl;
    std::cerr << "[Supervisor] ✗ Worker failed to become ready, killing PID "
              << pid << std::endl;
    std::cerr << "[Supervisor] ========================================" << std::endl;
    std::cerr << "[Supervisor] SOLUTION:" << std::endl;
    std::cerr << "[Supervisor]   1. Check worker logs for errors" << std::endl;
    std::cerr << "[Supervisor]   2. Check socket directory permissions:" << std::endl;
    std::cerr << "[Supervisor]      ls -la " << socket_path << std::endl;
    std::cerr << "[Supervisor]   3. Check socket directory exists and is writable:" << std::endl;
    std::filesystem::path socket_dir = std::filesystem::path(socket_path).parent_path();
    std::cerr << "[Supervisor]      ls -ld " << socket_dir.string() << std::endl;
    std::cerr << "[Supervisor]   4. Fix socket directory if needed:" << std::endl;
    std::cerr << "[Supervisor]      sudo mkdir -p " << socket_dir.string() 
              << " && sudo chmod 777 " << socket_dir.string() << std::endl;
    std::cerr << "[Supervisor]   5. Check CVEDIX SDK dependencies:" << std::endl;
    std::cerr << "[Supervisor]      sudo systemctl restart edgeos-api" << std::endl;
    std::cerr << "[Supervisor]   6. Run diagnostic script:" << std::endl;
    std::cerr << "[Supervisor]      ./scripts/diagnose_spawn_worker.sh" << std::endl;
    std::cerr << "[Supervisor] ========================================" << std::endl;
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    cleanupSocket(socket_path);
    return false;
  }

  workers_[instance_id] = std::move(worker);
  return true;
}

bool WorkerSupervisor::terminateWorker(const std::string &instance_id,
                                       bool force) {
  std::lock_guard<std::timed_mutex> lock(workers_mutex_);

  auto it = workers_.find(instance_id);
  if (it == workers_.end()) {
    return false;
  }

  WorkerInfo &worker = *it->second;

  if (worker.pid <= 0) {
    workers_.erase(it);
    return true;
  }

  // Try graceful shutdown via IPC first (if not force)
  if (!force && worker.client && worker.client->isConnected()) {
    IPCMessage shutdown_msg;
    shutdown_msg.type = MessageType::SHUTDOWN;
    worker.client->send(shutdown_msg);

    // Wait briefly for graceful shutdown
    for (int i = 0; i < 10; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      int status;
      pid_t result = waitpid(worker.pid, &status, WNOHANG);
      if (result > 0) {
        std::cout << "[Supervisor] Worker " << instance_id
                  << " exited gracefully" << std::endl;
        cleanupWorker(worker);
        workers_.erase(it);
        return true;
      }
    }
  }

  // Send signal
  int sig = force ? SIGKILL : SIGTERM;
  if (kill(worker.pid, sig) == 0) {
    // Wait for process to exit
    int status;
    waitpid(worker.pid, &status, 0);
    std::cout << "[Supervisor] Worker " << instance_id
              << " terminated with signal " << sig << std::endl;
  }

  cleanupWorker(worker);
  workers_.erase(it);
  return true;
}

IPCMessage WorkerSupervisor::sendToWorker(const std::string &instance_id,
                                          const IPCMessage &msg,
                                          int timeout_ms) {
  std::cout << "[WorkerSupervisor] ===== sendToWorker START =====" << std::endl;
  std::cout << "[WorkerSupervisor] Instance ID: " << instance_id << std::endl;
  std::cout << "[WorkerSupervisor] Message type: " << static_cast<int>(msg.type)
            << std::endl;
  std::cout << "[WorkerSupervisor] Timeout: " << timeout_ms << "ms"
            << std::endl;

  // CRITICAL: Get worker client pointer while holding lock, then release lock
  // before calling sendAndReceive() to prevent deadlock with getWorkerState()
  // sendAndReceive() can take up to 5 seconds, holding lock that long blocks
  // other operations
  UnixSocketClient *client_ptr = nullptr;

  {
    std::lock_guard<std::timed_mutex> lock(workers_mutex_);
    std::cout << "[WorkerSupervisor] Acquired workers_mutex_ lock" << std::endl;

    auto it = workers_.find(instance_id);
    if (it == workers_.end()) {
      std::cerr << "[WorkerSupervisor] ERROR: Worker not found for instance "
                << instance_id << std::endl;
      IPCMessage error;
      error.type = MessageType::ERROR_RESPONSE;
      error.payload =
          createErrorResponse("Worker not found", ResponseStatus::NOT_FOUND);
      return error;
    }
    std::cout << "[WorkerSupervisor] Worker found in map" << std::endl;

    WorkerInfo &worker = *it->second;

    if (!worker.client || !worker.client->isConnected()) {
      std::cerr << "[WorkerSupervisor] ERROR: Worker not connected"
                << std::endl;
      IPCMessage error;
      error.type = MessageType::ERROR_RESPONSE;
      error.payload =
          createErrorResponse("Worker not connected", ResponseStatus::ERROR);
      return error;
    }
    std::cout << "[WorkerSupervisor] Worker client is connected" << std::endl;

    if (worker.state != WorkerState::READY &&
        worker.state != WorkerState::BUSY) {
      std::cerr << "[WorkerSupervisor] ERROR: Worker state is "
                << static_cast<int>(worker.state) << " (not READY or BUSY)"
                << std::endl;
      IPCMessage error;
      error.type = MessageType::ERROR_RESPONSE;
      error.payload =
          createErrorResponse("Worker not ready", ResponseStatus::ERROR);
      return error;
    }
    std::cout << "[WorkerSupervisor] Worker state is valid: "
              << static_cast<int>(worker.state) << std::endl;

    // CRITICAL: If worker is already BUSY, reject the request to prevent
    // concurrent sendAndReceive() calls which can cause response mismatch
    // and timeout issues in production. The API layer should retry with
    // exponential backoff.
    if (worker.state == WorkerState::BUSY) {
      std::cerr << "[WorkerSupervisor] ERROR: Worker is already BUSY, "
                   "rejecting request to prevent concurrent operations"
                << std::endl;
      IPCMessage error;
      error.type = MessageType::ERROR_RESPONSE;
      error.payload = createErrorResponse(
          "Worker is busy processing another request. Please retry later.",
          ResponseStatus::ERROR);
      return error;
    }

    // Get client pointer and set state to BUSY while holding lock
    client_ptr = worker.client.get();
    setWorkerState(worker, WorkerState::BUSY);
    std::cout << "[WorkerSupervisor] Set worker state to BUSY" << std::endl;
  } // Lock released here - critical to prevent deadlock!
  std::cout << "[WorkerSupervisor] Released workers_mutex_ lock before "
               "sendAndReceive()"
            << std::endl;

  // Use try-catch to ensure worker state is always restored to READY
  // even if sendAndReceive throws exception or times out
  IPCMessage response;
  try {
    std::cout << "[WorkerSupervisor] Calling client->sendAndReceive()..."
              << std::endl;
    auto send_start = std::chrono::steady_clock::now();
    response = client_ptr->sendAndReceive(msg, timeout_ms);
    auto send_end = std::chrono::steady_clock::now();
    auto send_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                             send_end - send_start)
                             .count();
    std::cout << "[WorkerSupervisor] sendAndReceive() completed in "
              << send_duration << "ms" << std::endl;
    std::cout << "[WorkerSupervisor] Response type: "
              << static_cast<int>(response.type) << std::endl;

    // Re-acquire lock to restore state
    {
      std::lock_guard<std::timed_mutex> lock(workers_mutex_);
      auto it = workers_.find(instance_id);
      if (it != workers_.end()) {
        setWorkerState(*it->second, WorkerState::READY);
        std::cout << "[WorkerSupervisor] Set worker state back to READY"
                  << std::endl;
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "[WorkerSupervisor] EXCEPTION in sendAndReceive: " << e.what()
              << std::endl;
    // Re-acquire lock to restore state
    {
      std::lock_guard<std::timed_mutex> lock(workers_mutex_);
      auto it = workers_.find(instance_id);
      if (it != workers_.end()) {
        setWorkerState(*it->second, WorkerState::READY);
      }
    }
    // Return error response
    response.type = MessageType::ERROR_RESPONSE;
    response.payload =
        createErrorResponse("IPC communication error: " + std::string(e.what()),
                            ResponseStatus::ERROR);
  } catch (...) {
    std::cerr << "[WorkerSupervisor] UNKNOWN EXCEPTION in sendAndReceive"
              << std::endl;
    // Re-acquire lock to restore state
    {
      std::lock_guard<std::timed_mutex> lock(workers_mutex_);
      auto it = workers_.find(instance_id);
      if (it != workers_.end()) {
        setWorkerState(*it->second, WorkerState::READY);
      }
    }
    // Return error response
    response.type = MessageType::ERROR_RESPONSE;
    response.payload = createErrorResponse("Unknown IPC communication error",
                                           ResponseStatus::ERROR);
  }

  std::cout << "[WorkerSupervisor] ===== sendToWorker END =====" << std::endl;
  return response;
}

WorkerState
WorkerSupervisor::getWorkerState(const std::string &instance_id) const {
  std::cout << "[WorkerSupervisor] getWorkerState() called for instance: "
            << instance_id << std::endl;
  std::cout << "[WorkerSupervisor] Attempting to acquire workers_mutex_ lock..."
            << std::endl;
  auto lock_start = std::chrono::steady_clock::now();

  // Use timeout to prevent blocking indefinitely if mutex is held by another
  // thread This is critical to prevent deadlock when sendToWorker() is holding
  // the lock
  auto timeout_ms = TimeoutConstants::getWorkerStateMutexTimeoutMs();
  std::unique_lock<std::timed_mutex> lock(workers_mutex_, std::defer_lock);

  if (!lock.try_lock_for(std::chrono::milliseconds(timeout_ms))) {
    auto lock_end = std::chrono::steady_clock::now();
    auto lock_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                             lock_end - lock_start)
                             .count();
    std::cerr << "[WorkerSupervisor] ERROR: Failed to acquire workers_mutex_ "
                 "lock after "
              << lock_duration << "ms (timeout: " << timeout_ms << "ms)"
              << std::endl;
    std::cerr << "[WorkerSupervisor] WARNING: Worker state check timed out - "
                 "mutex may be held by sendToWorker()"
              << std::endl;
    // Return STOPPED as safe default - caller should handle this gracefully
    return WorkerState::STOPPED;
  }

  auto lock_end = std::chrono::steady_clock::now();
  auto lock_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                           lock_end - lock_start)
                           .count();
  if (lock_duration > 10) {
    std::cout << "[WorkerSupervisor] WARNING: workers_mutex_ lock took "
              << lock_duration << "ms (may indicate contention)" << std::endl;
  } else {
    std::cout << "[WorkerSupervisor] Acquired workers_mutex_ lock in "
              << lock_duration << "ms" << std::endl;
  }

  auto it = workers_.find(instance_id);
  if (it == workers_.end()) {
    std::cout << "[WorkerSupervisor] Worker not found, returning STOPPED"
              << std::endl;
    return WorkerState::STOPPED;
  }

  auto state = it->second->state;
  std::cout << "[WorkerSupervisor] Worker found, state: "
            << static_cast<int>(state) << std::endl;
  return state;
}

bool WorkerSupervisor::isWorkerReady(const std::string &instance_id) const {
  return getWorkerState(instance_id) == WorkerState::READY;
}

std::vector<std::string> WorkerSupervisor::getWorkerIds() const {
  std::lock_guard<std::timed_mutex> lock(workers_mutex_);

  std::vector<std::string> ids;
  ids.reserve(workers_.size());
  for (const auto &[id, _] : workers_) {
    ids.push_back(id);
  }
  return ids;
}

void WorkerSupervisor::setStateChangeCallback(StateChangeCallback callback) {
  state_change_callback_ = std::move(callback);
}

void WorkerSupervisor::setErrorCallback(ErrorCallback callback) {
  error_callback_ = std::move(callback);
}

std::optional<WorkerInfo>
WorkerSupervisor::getWorkerInfo(const std::string &instance_id) const {
  std::lock_guard<std::timed_mutex> lock(workers_mutex_);

  auto it = workers_.find(instance_id);
  if (it == workers_.end()) {
    return std::nullopt;
  }

  // Return a copy without the client (can't copy unique_ptr)
  WorkerInfo info;
  info.instance_id = it->second->instance_id;
  info.pid = it->second->pid;
  info.state = it->second->state;
  info.socket_path = it->second->socket_path;
  info.start_time = it->second->start_time;
  info.last_heartbeat = it->second->last_heartbeat;
  info.restart_count = it->second->restart_count;
  info.last_error = it->second->last_error;
  return info;
}

void WorkerSupervisor::monitorLoop() {
  while (running_.load()) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(heartbeat_interval_ms_));

    if (!running_.load())
      break;

    std::vector<std::string> crashed_workers;

    {
      std::lock_guard<std::timed_mutex> lock(workers_mutex_);

      for (auto &[instance_id, worker] : workers_) {
        // Check if process is still alive
        if (worker->pid > 0) {
          int status;
          pid_t result = waitpid(worker->pid, &status, WNOHANG);

          if (result > 0) {
            // Process exited
            if (WIFEXITED(status)) {
              int exit_code = WEXITSTATUS(status);
              std::cout << "[Supervisor] Worker " << instance_id
                        << " exited with code " << exit_code << std::endl;
            } else if (WIFSIGNALED(status)) {
              int sig = WTERMSIG(status);
              std::cout << "[Supervisor] Worker " << instance_id
                        << " killed by signal " << sig << std::endl;
            }

            setWorkerState(*worker, WorkerState::CRASHED);
            crashed_workers.push_back(instance_id);
            continue;
          }
        }

        // Send heartbeat ping
        if (worker->client && worker->client->isConnected()) {
          IPCMessage ping;
          ping.type = MessageType::PING;

          IPCMessage pong = worker->client->sendAndReceive(ping, 5000);

          if (pong.type == MessageType::PONG) {
            worker->last_heartbeat = std::chrono::steady_clock::now();
          } else {
            // Check heartbeat timeout
            auto now = std::chrono::steady_clock::now();
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - worker->last_heartbeat)
                    .count();

            if (elapsed > heartbeat_timeout_ms_) {
              std::cerr << "[Supervisor] Worker " << instance_id
                        << " heartbeat timeout" << std::endl;
              setWorkerState(*worker, WorkerState::CRASHED);
              crashed_workers.push_back(instance_id);
            }
          }
        }
      }
    }

    // Handle crashed workers outside the lock
    for (const auto &instance_id : crashed_workers) {
      handleWorkerCrash(instance_id);
    }
  }
}

void WorkerSupervisor::checkWorkerHealth(WorkerInfo &worker) {
  // Already implemented in monitorLoop
  (void)worker; // Suppress unused parameter warning
}

void WorkerSupervisor::handleWorkerCrash(const std::string &instance_id) {
  if (error_callback_) {
    error_callback_(instance_id, "Worker crashed");
  }

  // Attempt restart if under limit
  std::lock_guard<std::timed_mutex> lock(workers_mutex_);
  auto it = workers_.find(instance_id);
  if (it == workers_.end())
    return;

  WorkerInfo &worker = *it->second;

  if (worker.restart_count < max_restarts_) {
    std::cout << "[Supervisor] Attempting restart "
              << (worker.restart_count + 1) << "/" << max_restarts_ << " for "
              << instance_id << std::endl;

    // Clean up old resources
    cleanupWorker(worker);

    // Wait before restart
    std::this_thread::sleep_for(std::chrono::milliseconds(restart_delay_ms_));

    // Note: Full restart would need to re-read config from storage
    // For now, just increment counter and mark as stopped
    worker.restart_count++;
    setWorkerState(worker, WorkerState::STOPPED);
  } else {
    std::cerr << "[Supervisor] Max restarts reached for " << instance_id
              << std::endl;
    cleanupWorker(worker);
    workers_.erase(it);
  }
}

bool WorkerSupervisor::restartWorker(const std::string &instance_id) {
  // This would need access to the original config
  // Implementation depends on how config is stored
  (void)instance_id; // Suppress unused parameter warning
  return false;
}

bool WorkerSupervisor::waitForWorkerReady(WorkerInfo &worker, int timeout_ms) {
  auto start = std::chrono::steady_clock::now();
  int retry_delay = 100; // Start with 100ms

  while (true) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();

    if (elapsed > timeout_ms) {
      return false;
    }

    // Check if process is still alive
    int status;
    pid_t result = waitpid(worker.pid, &status, WNOHANG);
    if (result > 0) {
      // Process already exited
      return false;
    }

    // Try to connect to socket
    if (!worker.client) {
      worker.client = std::make_unique<UnixSocketClient>(worker.socket_path);
    }

    if (!worker.client->isConnected()) {
      if (worker.client->connect(1000)) {
        // Connected, wait for WORKER_READY message
        IPCMessage ready_msg = worker.client->receive(5000);
        if (ready_msg.type == MessageType::WORKER_READY) {
          setWorkerState(worker, WorkerState::READY);
          worker.last_heartbeat = std::chrono::steady_clock::now();
          std::cout << "[Supervisor] Worker " << worker.instance_id
                    << " is ready" << std::endl;
          return true;
        }
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay));
    retry_delay =
        std::min(retry_delay * 2, 1000); // Exponential backoff, max 1s
  }
}

void WorkerSupervisor::setWorkerState(WorkerInfo &worker,
                                      WorkerState new_state) {
  WorkerState old_state = worker.state;
  worker.state = new_state;

  if (state_change_callback_ && old_state != new_state) {
    state_change_callback_(worker.instance_id, old_state, new_state);
  }
}

void WorkerSupervisor::cleanupWorker(WorkerInfo &worker) {
  if (worker.client) {
    worker.client->disconnect();
    worker.client.reset();
  }

  cleanupSocket(worker.socket_path);
  worker.pid = -1;
}

std::string WorkerSupervisor::findWorkerExecutable() const {
  // 1. Check if it's an absolute path
  if (worker_executable_[0] == '/') {
    if (std::filesystem::exists(worker_executable_)) {
      return worker_executable_;
    }
    return "";
  }

  // 2. Check relative to current executable
  char exe_path[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len > 0) {
    exe_path[len] = '\0';
    std::filesystem::path exe_dir =
        std::filesystem::path(exe_path).parent_path();
    std::filesystem::path worker_path = exe_dir / worker_executable_;
    if (std::filesystem::exists(worker_path)) {
      return worker_path.string();
    }
  }

  // 3. Check in PATH
  const char *path_env = std::getenv("PATH");
  if (path_env) {
    std::string path_str(path_env);
    size_t start = 0;
    size_t end;

    while ((end = path_str.find(':', start)) != std::string::npos) {
      std::string dir = path_str.substr(start, end - start);
      std::filesystem::path worker_path =
          std::filesystem::path(dir) / worker_executable_;
      if (std::filesystem::exists(worker_path)) {
        return worker_path.string();
      }
      start = end + 1;
    }

    // Check last directory in PATH
    std::string dir = path_str.substr(start);
    std::filesystem::path worker_path =
        std::filesystem::path(dir) / worker_executable_;
    if (std::filesystem::exists(worker_path)) {
      return worker_path.string();
    }
  }

  // 4. Check current directory
  if (std::filesystem::exists(worker_executable_)) {
    return std::filesystem::absolute(worker_executable_).string();
  }

  return "";
}

} // namespace worker
