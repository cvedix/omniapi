#include "core/event_queue.h"
#include <algorithm>

EventQueue &EventQueue::getInstance() {
  static EventQueue instance;
  return instance;
}

void EventQueue::pushEvent(const std::string &instanceId, const Event &event) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto &queue = instance_queues_[instanceId];

  // Prevent memory leak by limiting queue size
  if (queue.size() >= max_queue_size_) {
    // Remove oldest event (FIFO)
    queue.pop();
  }

  queue.push(event);
}

std::vector<Event> EventQueue::consumeEvents(const std::string &instanceId,
                                              size_t maxEvents) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<Event> result;

  auto it = instance_queues_.find(instanceId);
  if (it == instance_queues_.end()) {
    return result; // No events for this instance
  }

  auto &queue = it->second;

  // Consume all events if maxEvents is 0, otherwise consume up to maxEvents
  size_t count = (maxEvents == 0) ? queue.size() : std::min(maxEvents, queue.size());

  for (size_t i = 0; i < count && !queue.empty(); ++i) {
    result.push_back(queue.front());
    queue.pop();
  }

  return result;
}

size_t EventQueue::getEventCount(const std::string &instanceId) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = instance_queues_.find(instanceId);
  if (it == instance_queues_.end()) {
    return 0;
  }

  return it->second.size();
}

void EventQueue::clearEvents(const std::string &instanceId) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = instance_queues_.find(instanceId);
  if (it != instance_queues_.end()) {
    // Clear queue by swapping with empty queue
    std::queue<Event> empty;
    it->second.swap(empty);
  }
}

void EventQueue::removeInstance(const std::string &instanceId) {
  std::lock_guard<std::mutex> lock(mutex_);
  instance_queues_.erase(instanceId);
}

