#pragma once

#include <opencv2/opencv.hpp>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include <vector>
#include <memory>

/**
 * @brief Frame type enumeration
 */
enum class FrameType {
  ENCODED,    // H.264, H.265, etc.
  COMPRESSED  // JPEG, PNG, BMP, etc.
};

/**
 * @brief Frame data structure
 */
struct FrameData {
  FrameType type;
  std::string codecId;  // "h264", "h265", etc. (for encoded)
  std::vector<uint8_t> data;
  int64_t timestamp;
  
  FrameData() : type(FrameType::COMPRESSED), timestamp(0) {}
  
  FrameData(FrameType t, const std::string& codec, const std::vector<uint8_t>& d, int64_t ts)
    : type(t), codecId(codec), data(d), timestamp(ts) {}
};

/**
 * @brief Thread-safe frame input queue for incoming frames
 * 
 * Supports both encoded (H.264/H.265) and compressed (JPEG/PNG) frames.
 * Per-instance queues with max size to prevent memory overflow.
 */
class FrameInputQueue {
public:
  /**
   * @brief Constructor
   * @param maxSize Maximum queue size (0 = unlimited). Default 30 for real-time latency.
   */
  explicit FrameInputQueue(size_t maxSize = 30);
  
  /**
   * @brief Destructor
   */
  ~FrameInputQueue() = default;
  
  /**
   * @brief Push a frame into the queue
   * @param frame Frame data to push
   * @return true if successful, false if queue is full
   */
  bool push(const FrameData& frame);
  
  /**
   * @brief Pop a frame from the queue (blocking)
   * @param frame Output frame data
   * @param timeoutMs Timeout in milliseconds (0 = wait indefinitely)
   * @return true if frame was popped, false if timeout
   */
  bool pop(FrameData& frame, int timeoutMs = 0);
  
  /**
   * @brief Try to pop a frame without blocking
   * @param frame Output frame data
   * @return true if frame was popped, false if queue is empty
   */
  bool tryPop(FrameData& frame);
  
  /**
   * @brief Get current queue size
   * @return Current number of frames in queue
   */
  size_t size() const;
  
  /**
   * @brief Check if queue is empty
   * @return true if queue is empty
   */
  bool empty() const;
  
  /**
   * @brief Clear all frames from queue
   */
  void clear();
  
  /**
   * @brief Get maximum queue size
   * @return Maximum queue size
   */
  size_t getMaxSize() const;
  
  /**
   * @brief Set maximum queue size
   * @param maxSize Maximum queue size (0 = unlimited)
   */
  void setMaxSize(size_t maxSize);
  
  /**
   * @brief Get number of frames dropped due to queue overflow
   * @return Number of dropped frames
   */
  uint64_t getDroppedCount() const;

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<FrameData> queue_;
  size_t max_size_;
  std::atomic<uint64_t> dropped_count_;
};

/**
 * @brief Global frame input queue manager
 * Manages per-instance frame queues
 */
class FrameInputQueueManager {
public:
  /**
   * @brief Get singleton instance
   */
  static FrameInputQueueManager& getInstance();
  
  /**
   * @brief Get or create queue for an instance
   * @param instanceId Instance ID
   * @return Reference to frame queue
   */
  FrameInputQueue& getQueue(const std::string& instanceId);
  
  /**
   * @brief Remove queue for an instance
   * @param instanceId Instance ID
   */
  void removeQueue(const std::string& instanceId);
  
  /**
   * @brief Check if queue exists for an instance
   * @param instanceId Instance ID
   * @return true if queue exists
   */
  bool hasQueue(const std::string& instanceId) const;
  
  /**
   * @brief Clear all queues
   */
  void clearAll();

private:
  FrameInputQueueManager() = default;
  ~FrameInputQueueManager() = default;
  FrameInputQueueManager(const FrameInputQueueManager&) = delete;
  FrameInputQueueManager& operator=(const FrameInputQueueManager&) = delete;
  
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::unique_ptr<FrameInputQueue>> queues_;
};

