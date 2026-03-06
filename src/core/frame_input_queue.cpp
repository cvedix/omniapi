#include "core/frame_input_queue.h"
#include <algorithm>
#include <chrono>

// ========== FrameInputQueue Implementation ==========

FrameInputQueue::FrameInputQueue(size_t maxSize)
  : max_size_(maxSize), dropped_count_(0) {
}

bool FrameInputQueue::push(const FrameData& frame) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  if (max_size_ > 0 && queue_.size() >= max_size_) {
    // Queue is full, drop oldest frame
    if (!queue_.empty()) {
      queue_.pop();
      dropped_count_.fetch_add(1, std::memory_order_relaxed);
    } else {
      // Queue is full but empty? Should not happen, but handle it
      return false;
    }
  }
  
  queue_.push(frame);
  cv_.notify_one();
  return true;
}

bool FrameInputQueue::pop(FrameData& frame, int timeoutMs) {
  std::unique_lock<std::mutex> lock(mutex_);
  
  if (timeoutMs > 0) {
    auto timeout = std::chrono::milliseconds(timeoutMs);
    if (!cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
      return false; // Timeout
    }
  } else {
    cv_.wait(lock, [this] { return !queue_.empty(); });
  }
  
  if (queue_.empty()) {
    return false;
  }
  
  frame = queue_.front();
  queue_.pop();
  return true;
}

bool FrameInputQueue::tryPop(FrameData& frame) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  if (queue_.empty()) {
    return false;
  }
  
  frame = queue_.front();
  queue_.pop();
  return true;
}

size_t FrameInputQueue::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

bool FrameInputQueue::empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.empty();
}

void FrameInputQueue::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  while (!queue_.empty()) {
    queue_.pop();
  }
}

size_t FrameInputQueue::getMaxSize() const {
  return max_size_;
}

void FrameInputQueue::setMaxSize(size_t maxSize) {
  std::lock_guard<std::mutex> lock(mutex_);
  max_size_ = maxSize;
  
  // Drop frames if new max size is smaller
  while (max_size_ > 0 && queue_.size() > max_size_) {
    queue_.pop();
    dropped_count_.fetch_add(1, std::memory_order_relaxed);
  }
}

uint64_t FrameInputQueue::getDroppedCount() const {
  return dropped_count_.load(std::memory_order_relaxed);
}

// ========== FrameInputQueueManager Implementation ==========

// Real-time default: small queue to bound latency (was 1000, caused 30+ s delay at 30fps)
static constexpr size_t kDefaultFrameQueueMaxSize = 30;

FrameInputQueueManager& FrameInputQueueManager::getInstance() {
  static FrameInputQueueManager instance;
  return instance;
}

FrameInputQueue& FrameInputQueueManager::getQueue(const std::string& instanceId) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto it = queues_.find(instanceId);
  if (it == queues_.end()) {
    auto queue = std::make_unique<FrameInputQueue>(kDefaultFrameQueueMaxSize);
    auto* queuePtr = queue.get();
    queues_[instanceId] = std::move(queue);
    return *queuePtr;
  }
  
  return *(it->second);
}

void FrameInputQueueManager::removeQueue(const std::string& instanceId) {
  std::lock_guard<std::mutex> lock(mutex_);
  queues_.erase(instanceId);
}

bool FrameInputQueueManager::hasQueue(const std::string& instanceId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queues_.find(instanceId) != queues_.end();
}

void FrameInputQueueManager::clearAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  queues_.clear();
}

