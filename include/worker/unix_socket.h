#pragma once

#include "worker/ipc_protocol.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace worker {

/**
 * @brief Unix Socket Server (for worker process)
 *
 * Listens on a Unix domain socket and handles incoming messages.
 */
class UnixSocketServer {
public:
  using MessageHandler = std::function<IPCMessage(const IPCMessage &)>;
  using ClientConnectedCallback = std::function<void(int client_fd)>;

  explicit UnixSocketServer(const std::string &socket_path);
  ~UnixSocketServer();

  // Non-copyable
  UnixSocketServer(const UnixSocketServer &) = delete;
  UnixSocketServer &operator=(const UnixSocketServer &) = delete;

  /**
   * @brief Start listening for connections
   * @param handler Callback for handling messages
   * @param onClientConnected Optional callback when client connects (for sending WORKER_READY)
   * @return true if started successfully
   */
  bool start(MessageHandler handler, ClientConnectedCallback onClientConnected = nullptr);

  /**
   * @brief Stop the server
   */
  void stop();

  /**
   * @brief Check if server is running
   */
  bool isRunning() const { return running_.load(); }

  /**
   * @brief Get socket path
   */
  const std::string &getSocketPath() const { return socket_path_; }

private:
  std::string socket_path_;
  int server_fd_ = -1;
  std::atomic<bool> running_{false};
  std::thread accept_thread_;
  MessageHandler handler_;
  ClientConnectedCallback on_client_connected_;

  void acceptLoop();
  void handleClient(int client_fd);
};

/**
 * @brief Unix Socket Client (for supervisor in main API server)
 *
 * Connects to a worker's Unix socket and sends/receives messages.
 */
class UnixSocketClient {
public:
  explicit UnixSocketClient(const std::string &socket_path);
  ~UnixSocketClient();

  // Non-copyable
  UnixSocketClient(const UnixSocketClient &) = delete;
  UnixSocketClient &operator=(const UnixSocketClient &) = delete;

  /**
   * @brief Connect to the server
   * @param timeout_ms Connection timeout in milliseconds
   * @return true if connected
   */
  bool connect(int timeout_ms = 5000);

  /**
   * @brief Disconnect from server
   */
  void disconnect();

  /**
   * @brief Check if connected
   */
  bool isConnected() const { return connected_.load(); }

  /**
   * @brief Send message and wait for response
   * @param msg Message to send
   * @param timeout_ms Response timeout in milliseconds
   * @return Response message (ERROR_RESPONSE on failure)
   */
  IPCMessage sendAndReceive(const IPCMessage &msg, int timeout_ms = 30000);

  /**
   * @brief Send message without waiting for response
   * @param msg Message to send
   * @return true if sent successfully
   */
  bool send(const IPCMessage &msg);

  /**
   * @brief Receive a message (blocking with timeout)
   * @param timeout_ms Timeout in milliseconds
   * @return Received message (ERROR_RESPONSE on failure/timeout)
   */
  IPCMessage receive(int timeout_ms = 30000);

private:
  std::string socket_path_;
  int socket_fd_ = -1;
  std::atomic<bool> connected_{false};
  std::mutex send_mutex_;
  std::mutex recv_mutex_;

  bool sendRaw(const std::string &data);
  std::string receiveRaw(size_t expected_size, int timeout_ms);
};

/**
 * @brief Generate unique socket path for a worker instance
 *
 * Uses EDGE_AI_SOCKET_DIR environment variable if set, otherwise defaults to
 * /opt/omniapi/run. Falls back to /tmp if directory cannot be created.
 *
 * @param instance_id Instance ID
 * @return Socket path (e.g.,
 * /opt/omniapi/run/edgeos-worker_<instance_id>.sock)
 */
std::string generateSocketPath(const std::string &instance_id);

/**
 * @brief Clean up socket file if exists
 * @param socket_path Path to socket file
 */
void cleanupSocket(const std::string &socket_path);

} // namespace worker
