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
#include <iostream>

// Global handler for signal handling
static worker::WorkerHandler *g_handler = nullptr;

static void signalHandler(int signum) {
  std::cout << "\n[Worker] Received signal " << signum << std::endl;

  if (g_handler) {
    g_handler->requestShutdown();
  }
}

static void setupSignalHandlers() {
  struct sigaction sa;
  sa.sa_handler = signalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);

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
