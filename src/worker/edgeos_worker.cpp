/**
 * @file edgeos-worker.cpp
 * @brief Edge AI Worker Process - Isolated subprocess for running AI pipelines
 *
 * This is the main entry point for worker subprocesses.
 * Each worker manages a single AI instance in isolation.
 *
 * Communication with main API server is via Unix Socket IPC.
 *
 * Usage:
 *   edgeos-worker --instance-id <id> --socket <path> [--config <json>]
 *
 * Architecture:
 *   - Main API Server spawns worker processes via WorkerSupervisor
 *   - Each worker listens on a Unix socket for commands
 *   - Worker crashes don't affect other workers or main server
 *   - Memory leaks are contained within worker process
 */

#include "worker/worker_handler.h"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cctype>
#include <execinfo.h>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <optional>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

// Global handler for signal handling
static worker::WorkerHandler *g_handler = nullptr;
static std::atomic<bool> g_log_rotation_running{false};
static std::thread g_log_rotation_thread;
static std::mutex g_log_rotation_mutex;

namespace {
constexpr size_t kDefaultLogMaxBytes = 100 * 1024 * 1024;
constexpr size_t kMinLogMaxBytes = 1024 * 1024;

size_t parseLogMaxBytes() {
  const char *raw = std::getenv("EDGEOS_WORKER_LOG_MAX_FILE_SIZE");
  if (!raw || !raw[0]) {
    return kDefaultLogMaxBytes;
  }
  try {
    std::string s(raw);
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(),
                         [](unsigned char ch) { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         [](unsigned char ch) { return !std::isspace(ch); })
                .base(),
            s.end());
    if (s.empty()) {
      return kDefaultLogMaxBytes;
    }
    unsigned long long v = std::stoull(s);
    if (v < kMinLogMaxBytes) {
      return kMinLogMaxBytes;
    }
    return static_cast<size_t>(v);
  } catch (...) {
    return kDefaultLogMaxBytes;
  }
}

std::optional<std::string> getStdoutLogPath() {
  char buf[4096];
  ssize_t n = ::readlink("/proc/self/fd/1", buf, sizeof(buf) - 1);
  if (n <= 0) {
    return std::nullopt;
  }
  buf[n] = '\0';
  std::string p(buf);
  if (p.empty()) {
    return std::nullopt;
  }
  std::error_code ec;
  if (!std::filesystem::is_regular_file(p, ec)) {
    return std::nullopt;
  }
  return p;
}

bool parseWorkerLogPart(const std::string &path, std::string &prefix_no_ext,
                        int &part) {
  std::filesystem::path p(path);
  std::string name = p.filename().string();
  if (name.size() < 11 || name.rfind("worker-", 0) != 0 ||
      name.substr(name.size() - 4) != ".log") {
    return false;
  }

  std::string base = name.substr(0, name.size() - 4); // strip ".log"
  size_t last_dot = base.find_last_of('.');
  if (last_dot != std::string::npos) {
    std::string tail = base.substr(last_dot + 1);
    if (!tail.empty() &&
        std::all_of(tail.begin(), tail.end(),
                    [](unsigned char c) { return std::isdigit(c); })) {
      try {
        part = std::stoi(tail);
        prefix_no_ext = (p.parent_path() / base.substr(0, last_dot)).string();
        return true;
      } catch (...) {
        return false;
      }
    }
  }

  part = 0;
  prefix_no_ext = (p.parent_path() / base).string();
  return true;
}

std::string buildLogPath(const std::string &prefix_no_ext, int part) {
  if (part <= 0) {
    return prefix_no_ext + ".log";
  }
  return prefix_no_ext + "." + std::to_string(part) + ".log";
}

void startWorkerLogRotationThread(const std::string &instance_id) {
  if (!std::getenv("EDGEOS_LOG_FILES")) {
    return;
  }
  auto out_path = getStdoutLogPath();
  if (!out_path.has_value()) {
    return;
  }

  std::string prefix_no_ext;
  int part = 0;
  if (!parseWorkerLogPart(*out_path, prefix_no_ext, part)) {
    return;
  }
  const size_t max_bytes = parseLogMaxBytes();

  g_log_rotation_running.store(true);
  g_log_rotation_thread = std::thread(
      [instance_id, prefix_no_ext, part, max_bytes]() mutable {
    std::cerr << "[Worker:" << instance_id
              << "] Subprocess log rotation enabled (max_file_bytes="
              << max_bytes << ", start_part=" << part << ")" << std::endl;
    while (g_log_rotation_running.load()) {
      std::this_thread::sleep_for(std::chrono::seconds(2));
      if (!g_log_rotation_running.load()) {
        break;
      }

      std::lock_guard<std::mutex> lock(g_log_rotation_mutex);
      std::string current_path = buildLogPath(prefix_no_ext, part);
      struct stat st {};
      if (::stat(current_path.c_str(), &st) != 0) {
        continue;
      }
      if (static_cast<size_t>(st.st_size) < max_bytes) {
        continue;
      }

      int next_part = part + 1;
      std::string next_path;
      for (int guard = 0; guard < 10000; ++guard, ++next_part) {
        next_path = buildLogPath(prefix_no_ext, next_part);
        struct stat nst {};
        if (::stat(next_path.c_str(), &nst) != 0) {
          break; // file does not exist
        }
        if (static_cast<size_t>(nst.st_size) < max_bytes) {
          break; // existing part still has space
        }
      }

      int fd = ::open(next_path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
      if (fd < 0) {
        continue;
      }
      ::dup2(fd, STDOUT_FILENO);
      ::dup2(fd, STDERR_FILENO);
      if (fd > STDERR_FILENO) {
        ::close(fd);
      }
      int old_part = part;
      part = next_part;
      std::cerr << "[Worker] Rotated subprocess log for instance "
                << instance_id << ": part " << old_part << " -> " << part
                << ", from '" << current_path << "' to '" << next_path
                << "' (max " << max_bytes << " bytes)" << std::endl;
    }
  });
}

void stopWorkerLogRotationThread() {
  g_log_rotation_running.store(false);
  if (g_log_rotation_thread.joinable()) {
    g_log_rotation_thread.join();
  }
}
} // namespace

static void signalHandler(int signum) {
  std::cout << "\n[Worker] Received signal " << signum << std::endl;

  if (g_handler) {
    g_handler->requestShutdown();
  }
}

static void crashSignalHandler(int signum, siginfo_t *info, void * /*ucontext*/) {
  void *frames[64];
  int frameCount = backtrace(frames, 64);
  pid_t senderPid = info ? info->si_pid : -1;
  void *faultAddr = info ? info->si_addr : nullptr;

  std::cerr << "\n[Worker] CRASH signal " << signum
            << " received. sender_pid=" << senderPid
            << ", fault_addr=" << faultAddr << std::endl;
  std::cerr << "[Worker] Backtrace (" << frameCount << " frames):" << std::endl;
  backtrace_symbols_fd(frames, frameCount, STDERR_FILENO);

  // Re-raise with default handler so OS/core-dump tooling can capture crash.
  std::signal(signum, SIG_DFL);
  raise(signum);
}

static void setupSignalHandlers() {
  struct sigaction sa;
  sa.sa_handler = signalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);

  struct sigaction crash_sa;
  crash_sa.sa_sigaction = crashSignalHandler;
  sigemptyset(&crash_sa.sa_mask);
  crash_sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigaction(SIGSEGV, &crash_sa, nullptr);
  sigaction(SIGABRT, &crash_sa, nullptr);

  // Ignore SIGPIPE (broken pipe)
  signal(SIGPIPE, SIG_IGN);
}

int main(int argc, char *argv[]) {
  // Parse command line arguments
  worker::WorkerArgs args = worker::WorkerArgs::parse(argc, argv);

  if (!args.valid) {
    std::cerr << "[Worker] Error: " << args.error << std::endl;
    std::cerr << "[Worker] Usage: edgeos-worker --instance-id <id> --socket "
                 "<path> [--config <json>]"
              << std::endl;
    return 1;
  }

  std::cout << "========================================" << std::endl;
  std::cout << "Edge AI Worker Process" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Instance ID: " << args.instance_id << std::endl;
  std::cout << "Socket:      " << args.socket_path << std::endl;
  std::cout << std::endl;

  // Setup signal handlers
  setupSignalHandlers();
  startWorkerLogRotationThread(args.instance_id);

  // Create and run worker handler
  worker::WorkerHandler handler(args.instance_id, args.socket_path,
                                args.config);
  g_handler = &handler;

  int exit_code = handler.run();
  stopWorkerLogRotationThread();

  g_handler = nullptr;

  return exit_code;
}
