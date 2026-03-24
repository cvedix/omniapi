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
#include <csignal>
#include <cstdlib>
#include <execinfo.h>
#include <iostream>
#include <unistd.h>

// Global handler for signal handling
static worker::WorkerHandler *g_handler = nullptr;

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

  // Create and run worker handler
  worker::WorkerHandler handler(args.instance_id, args.socket_path,
                                args.config);
  g_handler = &handler;

  int exit_code = handler.run();

  g_handler = nullptr;

  return exit_code;
}
