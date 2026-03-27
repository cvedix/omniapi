#include "worker/unix_socket.h"
#include "core/env_config.h"
#include "core/timeout_constants.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace worker {

// ============================================================================
// UnixSocketServer
// ============================================================================

UnixSocketServer::UnixSocketServer(const std::string &socket_path)
    : socket_path_(socket_path) {}

UnixSocketServer::~UnixSocketServer() { stop(); }

bool UnixSocketServer::start(MessageHandler handler, ClientConnectedCallback onClientConnected) {
  if (running_.load()) {
    return false;
  }

  handler_ = std::move(handler);
  on_client_connected_ = std::move(onClientConnected);

  // Clean up existing socket
  cleanupSocket(socket_path_);

  // Create socket
  server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    std::cerr << "[Worker] Failed to create socket: " << strerror(errno)
              << std::endl;
    return false;
  }

  // Bind
  struct sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

  if (bind(server_fd_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    std::cerr << "[Worker] Failed to bind socket: " << strerror(errno)
              << std::endl;
    close(server_fd_);
    server_fd_ = -1;
    return false;
  }

  // Listen
  if (listen(server_fd_, 5) < 0) {
    std::cerr << "[Worker] Failed to listen: " << strerror(errno) << std::endl;
    close(server_fd_);
    server_fd_ = -1;
    cleanupSocket(socket_path_);
    return false;
  }

  running_.store(true);
  accept_thread_ = std::thread(&UnixSocketServer::acceptLoop, this);

  std::cout << "[Worker] Server listening on " << socket_path_ << std::endl;
  return true;
}

void UnixSocketServer::stop() {
  if (!running_.load()) {
    return;
  }

  running_.store(false);

  // Close server socket to interrupt accept()
  if (server_fd_ >= 0) {
    shutdown(server_fd_, SHUT_RDWR);
    close(server_fd_);
    server_fd_ = -1;
  }

  // Wait for accept thread - it should exit quickly when server_fd_ is closed
  // poll() will return immediately, and accept() will fail, breaking the loop
  if (accept_thread_.joinable()) {
    // Accept loop should exit quickly after server_fd_ is closed
    // poll() has 1 second timeout, so thread should exit within ~1 second
    // Use configurable shutdown timeout
    auto timeout = TimeoutConstants::getShutdownTimeout();

    auto start_time = std::chrono::steady_clock::now();
    while (accept_thread_.joinable()) {
      auto elapsed = std::chrono::steady_clock::now() - start_time;

      if (elapsed >= timeout) {
        std::cerr << "[Worker] Accept thread join timeout (" << timeout.count()
                  << "ms), detaching..." << std::endl;
        accept_thread_.detach();
        break;
      }

      // Try join with small sleep - accept loop checks running_ flag every 1s
      // So thread should exit within ~1 second after server_fd_ is closed
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (!accept_thread_.joinable()) {
        break;
      }
    }

    // Final attempt to join if still joinable
    if (accept_thread_.joinable()) {
      accept_thread_.join();
    }
  }

  cleanupSocket(socket_path_);
}

void UnixSocketServer::acceptLoop() {
  while (running_.load()) {
    struct pollfd pfd;
    pfd.fd = server_fd_;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 1000); // 1 second timeout
    if (ret <= 0) {
      continue;
    }

    int client_fd = accept(server_fd_, nullptr, nullptr);
    if (client_fd < 0) {
      if (running_.load()) {
        std::cerr << "[Worker] Accept failed: " << strerror(errno) << std::endl;
      }
      continue;
    }

    // Call onClientConnected callback if set (for sending WORKER_READY message)
    if (on_client_connected_) {
      on_client_connected_(client_fd);
    }

    // Handle client in same thread (single client expected per worker)
    handleClient(client_fd);
  }
}

void UnixSocketServer::handleClient(int client_fd) {
  // CRITICAL: Use poll() with timeout for recv() to prevent blocking
  // But keep socket blocking for send() to ensure complete transmission
  // This is safe because:
  // 1. recv() with poll() timeout prevents infinite blocking
  // 2. send() blocking is OK since response is small and client is waiting
  // 3. Client uses blocking socket, so this matches client expectations

  while (running_.load()) {
    // Read header with timeout using poll()
    char header_buf[MessageHeader::HEADER_SIZE];
    ssize_t n = 0;
    size_t total_received = 0;

    // Use poll() to wait for data with timeout (1 second)
    // This allows us to check running_ flag periodically
    while (total_received < MessageHeader::HEADER_SIZE && running_.load()) {
      struct pollfd pfd;
      pfd.fd = client_fd;
      pfd.events = POLLIN;

      int ret = poll(&pfd, 1, 1000); // 1 second timeout
      if (ret <= 0) {
        if (ret == 0) {
          // Timeout - continue loop to check running_ flag
          continue;
        }
        // Error or connection closed
        close(client_fd);
        return;
      }

      // recv() with MSG_WAITALL is safe here because poll() confirmed data is
      // available But we use regular recv() and loop to handle partial reads
      // gracefully
      n = recv(client_fd, header_buf + total_received,
               MessageHeader::HEADER_SIZE - total_received, 0);
      if (n < 0) {
        // Error - connection closed or error
        close(client_fd);
        return;
      }
      if (n == 0) {
        // Connection closed
        close(client_fd);
        return;
      }
      total_received += n;
    }

    if (!running_.load() || total_received < MessageHeader::HEADER_SIZE) {
      break;
    }

    MessageHeader header;
    if (!MessageHeader::deserialize(header_buf, total_received, header)) {
      std::cerr << "[Worker] Invalid message header" << std::endl;
      break;
    }

    // Read payload with timeout
    std::string payload_buf(header.payload_size, '\0');
    if (header.payload_size > 0) {
      total_received = 0;
      while (total_received < header.payload_size && running_.load()) {
        struct pollfd pfd;
        pfd.fd = client_fd;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 1000); // 1 second timeout
        if (ret <= 0) {
          if (ret == 0) {
            continue; // Timeout - check running_ flag
          }
          close(client_fd);
          return;
        }

        n = recv(client_fd, &payload_buf[total_received],
                 header.payload_size - total_received, 0);
        if (n < 0) {
          // Error
          close(client_fd);
          return;
        }
        if (n == 0) {
          // Connection closed
          close(client_fd);
          return;
        }
        total_received += n;
      }

      if (!running_.load() || total_received < header.payload_size) {
        break;
      }
    }

    // Deserialize message
    std::string full_msg =
        std::string(header_buf, MessageHeader::HEADER_SIZE) + payload_buf;
    IPCMessage request;
    if (!IPCMessage::deserialize(full_msg, request)) {
      std::cerr << "[Worker] Failed to deserialize message" << std::endl;
      continue;
    }

    std::cout << "[Worker] ===== IPC REQUEST RECEIVED =====" << std::endl;
    std::cout << "[Worker] Message type: " << static_cast<int>(request.type)
              << std::endl;
    std::cout << "[Worker] Payload size: "
              << request.payload.toStyledString().size() << " bytes"
              << std::endl;

    // Handle message
    auto handle_start = std::chrono::steady_clock::now();
    std::cout << "[Worker] Calling handler_()..." << std::endl;
    IPCMessage response = handler_(request);
    auto handle_end = std::chrono::steady_clock::now();
    auto handle_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(handle_end -
                                                              handle_start)
            .count();
    std::cout << "[Worker] handler_() completed in " << handle_duration << "ms"
              << std::endl;
    std::cout << "[Worker] Response type: " << static_cast<int>(response.type)
              << std::endl;

    // Send response - use blocking send() since client is waiting
    // Response is typically small (< 10KB), so blocking is safe
    std::cout << "[Worker] Serializing response..." << std::endl;
    std::string response_data = response.serialize();
    std::cout << "[Worker] Response serialized, size: " << response_data.size()
              << " bytes" << std::endl;
    ssize_t total_sent = 0;
    size_t total_size = response_data.size();

    std::cout << "[Worker] Sending response, total size: " << total_size
              << " bytes" << std::endl;
    auto send_start = std::chrono::steady_clock::now();

    // Send all data - blocking is OK here since response is small
    while (total_sent < static_cast<ssize_t>(total_size) && running_.load()) {
      ssize_t sent = send(client_fd, response_data.data() + total_sent,
                          total_size - total_sent, MSG_NOSIGNAL);
      if (sent < 0) {
        // Error sending
        std::cerr << "[Worker] ERROR: Failed to send response: "
                  << strerror(errno) << std::endl;
        break;
      }
      if (sent == 0) {
        // Connection closed
        std::cerr << "[Worker] ERROR: Connection closed while sending response"
                  << std::endl;
        break;
      }
      total_sent += sent;
      std::cout << "[Worker] Sent " << total_sent << "/" << total_size
                << " bytes" << std::endl;
    }

    auto send_end = std::chrono::steady_clock::now();
    auto send_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                             send_end - send_start)
                             .count();

    if (total_sent != static_cast<ssize_t>(total_size)) {
      std::cerr << "[Worker] ERROR: Failed to send response completely ("
                << total_sent << "/" << total_size << " bytes) in "
                << send_duration << "ms" << std::endl;
      break;
    }

    std::cout << "[Worker] Response sent completely in " << send_duration
              << "ms" << std::endl;
    std::cout << "[Worker] ===== IPC REQUEST HANDLED =====" << std::endl;
  }

  close(client_fd);
}

// ============================================================================
// UnixSocketClient
// ============================================================================

UnixSocketClient::UnixSocketClient(const std::string &socket_path)
    : socket_path_(socket_path) {}

UnixSocketClient::~UnixSocketClient() { disconnect(); }

bool UnixSocketClient::connect(int timeout_ms) {
  if (connected_.load()) {
    return true;
  }

  socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket_fd_ < 0) {
    return false;
  }

  // Set non-blocking for connect with timeout
  int flags = fcntl(socket_fd_, F_GETFL, 0);
  fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);

  struct sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

  int ret = ::connect(socket_fd_, (struct sockaddr *)&addr, sizeof(addr));
  if (ret < 0 && errno != EINPROGRESS) {
    close(socket_fd_);
    socket_fd_ = -1;
    return false;
  }

  if (ret < 0) {
    // Wait for connection
    struct pollfd pfd;
    pfd.fd = socket_fd_;
    pfd.events = POLLOUT;

    ret = poll(&pfd, 1, timeout_ms);
    if (ret <= 0) {
      close(socket_fd_);
      socket_fd_ = -1;
      return false;
    }

    // Check for connection error
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &error, &len);
    if (error != 0) {
      close(socket_fd_);
      socket_fd_ = -1;
      return false;
    }
  }

  // Set back to blocking
  fcntl(socket_fd_, F_SETFL, flags);

  connected_.store(true);
  return true;
}

void UnixSocketClient::disconnect() {
  if (!connected_.load()) {
    return;
  }

  connected_.store(false);

  if (socket_fd_ >= 0) {
    shutdown(socket_fd_, SHUT_RDWR);
    close(socket_fd_);
    socket_fd_ = -1;
  }
}

IPCMessage UnixSocketClient::sendAndReceive(const IPCMessage &msg,
                                            int timeout_ms) {
  std::lock_guard<std::mutex> send_lock(send_mutex_);
  std::lock_guard<std::mutex> recv_lock(recv_mutex_);

  if (!connected_.load()) {
    IPCMessage error;
    error.type = MessageType::ERROR_RESPONSE;
    error.payload = createErrorResponse("Not connected");
    return error;
  }

  auto start_time = std::chrono::steady_clock::now();

  // Send
  std::string data = msg.serialize();
  if (!sendRaw(data)) {
    IPCMessage error;
    error.type = MessageType::ERROR_RESPONSE;
    error.payload = createErrorResponse("Send failed");
    return error;
  }

  // Calculate remaining timeout after send
  auto elapsed_after_send =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start_time)
          .count();
  int remaining_timeout = timeout_ms - static_cast<int>(elapsed_after_send);

  if (remaining_timeout <= 0) {
    IPCMessage error;
    error.type = MessageType::ERROR_RESPONSE;
    error.payload = createErrorResponse("Send timeout");
    return error;
  }

  // Receive header with remaining timeout
  std::string header_data =
      receiveRaw(MessageHeader::HEADER_SIZE, remaining_timeout);
  if (header_data.empty()) {
    IPCMessage error;
    error.type = MessageType::ERROR_RESPONSE;
    error.payload = createErrorResponse("Receive header timeout");
    return error;
  }

  MessageHeader header;
  if (!MessageHeader::deserialize(header_data.data(), header_data.size(),
                                  header)) {
    IPCMessage error;
    error.type = MessageType::ERROR_RESPONSE;
    error.payload = createErrorResponse("Invalid response header");
    return error;
  }

  // Calculate remaining timeout after receiving header
  auto elapsed_after_header =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start_time)
          .count();
  remaining_timeout = timeout_ms - static_cast<int>(elapsed_after_header);

  if (remaining_timeout <= 0) {
    IPCMessage error;
    error.type = MessageType::ERROR_RESPONSE;
    error.payload = createErrorResponse("Receive header timeout");
    return error;
  }

  // Receive payload with remaining timeout
  std::string payload_data;
  if (header.payload_size > 0) {
    payload_data = receiveRaw(header.payload_size, remaining_timeout);
    if (payload_data.empty()) {
      IPCMessage error;
      error.type = MessageType::ERROR_RESPONSE;
      error.payload = createErrorResponse("Receive payload timeout");
      return error;
    }
  }

  // Deserialize
  IPCMessage response;
  std::string full_data = header_data + payload_data;
  if (!IPCMessage::deserialize(full_data, response)) {
    IPCMessage error;
    error.type = MessageType::ERROR_RESPONSE;
    error.payload = createErrorResponse("Failed to deserialize response");
    return error;
  }

  return response;
}

bool UnixSocketClient::send(const IPCMessage &msg) {
  std::lock_guard<std::mutex> lock(send_mutex_);
  if (!connected_.load()) {
    return false;
  }
  return sendRaw(msg.serialize());
}

IPCMessage UnixSocketClient::receive(int timeout_ms) {
  std::lock_guard<std::mutex> lock(recv_mutex_);

  if (!connected_.load()) {
    IPCMessage error;
    error.type = MessageType::ERROR_RESPONSE;
    error.payload = createErrorResponse("Not connected");
    return error;
  }

  // Receive header
  std::string header_data = receiveRaw(MessageHeader::HEADER_SIZE, timeout_ms);
  if (header_data.empty()) {
    IPCMessage error;
    error.type = MessageType::ERROR_RESPONSE;
    error.payload = createErrorResponse("Receive timeout");
    return error;
  }

  MessageHeader header;
  if (!MessageHeader::deserialize(header_data.data(), header_data.size(),
                                  header)) {
    IPCMessage error;
    error.type = MessageType::ERROR_RESPONSE;
    error.payload = createErrorResponse("Invalid header");
    return error;
  }

  // Receive payload
  std::string payload_data;
  if (header.payload_size > 0) {
    payload_data = receiveRaw(header.payload_size, timeout_ms);
  }

  IPCMessage response;
  std::string full_data = header_data + payload_data;
  if (!IPCMessage::deserialize(full_data, response)) {
    IPCMessage error;
    error.type = MessageType::ERROR_RESPONSE;
    error.payload = createErrorResponse("Deserialize failed");
    return error;
  }

  return response;
}

bool UnixSocketClient::sendRaw(const std::string &data) {
  size_t total_sent = 0;
  while (total_sent < data.size()) {
    ssize_t sent = ::send(socket_fd_, data.data() + total_sent,
                          data.size() - total_sent, MSG_NOSIGNAL);
    if (sent <= 0) {
      return false;
    }
    total_sent += sent;
  }
  return true;
}

std::string UnixSocketClient::receiveRaw(size_t expected_size, int timeout_ms) {
  std::string result(expected_size, '\0');
  size_t total_received = 0;

  auto start_time = std::chrono::steady_clock::now();

  while (total_received < expected_size) {
    // Calculate remaining timeout
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start_time)
                       .count();
    int remaining_timeout = timeout_ms - static_cast<int>(elapsed);

    if (remaining_timeout <= 0) {
      // Total timeout exceeded
      return "";
    }

    struct pollfd pfd;
    pfd.fd = socket_fd_;
    pfd.events = POLLIN;

    // Use remaining timeout, but cap at reasonable value to avoid blocking too
    // long
    int poll_timeout =
        std::min(remaining_timeout, 1000); // Max 1 second per poll

    int ret = poll(&pfd, 1, poll_timeout);
    if (ret <= 0) {
      // Check if total timeout exceeded
      elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time)
                    .count();
      if (elapsed >= timeout_ms) {
        return "";
      }
      // Continue if poll timeout but total timeout not exceeded
      continue;
    }

    ssize_t received = recv(socket_fd_, &result[total_received],
                            expected_size - total_received, 0);
    if (received <= 0) {
      if (received == 0) {
        // Connection closed
        return "";
      }
      // Error - check errno
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Would block - continue polling
        continue;
      }
      // Other error
      return "";
    }
    total_received += received;
  }

  return result;
}

// ============================================================================
// Utility functions
// ============================================================================

std::string generateSocketPath(const std::string &instance_id) {
  // Check environment variable first
  const char *socket_dir = std::getenv("EDGE_AI_SOCKET_DIR");
  std::string dir;

  if (socket_dir && strlen(socket_dir) > 0) {
    dir = std::string(socket_dir);
  } else {
    dir = EnvConfig::getDataInstallRoot() + "/run";
  }

  const std::string default_run_dir = EnvConfig::getDataInstallRoot() + "/run";

  // Create directory if it doesn't exist
  try {
    if (!std::filesystem::exists(dir)) {
      std::filesystem::create_directories(dir);
      std::cout << "[Socket] Created socket directory: " << dir << std::endl;
    }
  } catch (const std::filesystem::filesystem_error &e) {
    // If can't create under install root, fallback to /tmp
    if (dir == default_run_dir) {
      std::cerr << "[Socket] ⚠ Cannot create " << dir
                << " (permission denied), falling back to /tmp" << std::endl;
      dir = "/tmp";
    } else {
      std::cerr << "[Socket] ⚠ Error creating socket directory " << dir << ": "
                << e.what() << std::endl;
      // Fallback to /tmp as last resort
      dir = "/tmp";
    }
  }

  // Return full socket path
  return dir + "/edgeos_worker_" + instance_id + ".sock";
}

void cleanupSocket(const std::string &socket_path) {
  std::error_code ec;
  std::filesystem::remove(socket_path, ec);
}

} // namespace worker
