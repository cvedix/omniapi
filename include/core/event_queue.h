#pragma once

#include <json/json.h>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>

/**
 * @brief Event structure
 */
struct Event {
  std::string dataType;   // "detection", "tracking", "analytics", etc.
  std::string jsonObject; // JSON serialized string
};

/**
 * @brief Thread-safe event queue manager
 *
 * Manages per-instance event queues for consuming events from instances.
 * Events are published by instance processing pipeline and consumed via API.
 */
class EventQueue {
public:
  /**
   * @brief Get singleton instance
   */
  static EventQueue &getInstance();

  /**
   * @brief Push event to instance queue
   * @param instanceId Instance ID
   * @param event Event to push
   */
  void pushEvent(const std::string &instanceId, const Event &event);

  /**
   * @brief Consume events from instance queue
   * @param instanceId Instance ID
   * @param maxEvents Maximum number of events to consume (0 = all)
   * @return Vector of events (removed from queue)
   */
  std::vector<Event> consumeEvents(const std::string &instanceId,
                                    size_t maxEvents = 0);

  /**
   * @brief Get event count for instance
   * @param instanceId Instance ID
   * @return Number of events in queue
   */
  size_t getEventCount(const std::string &instanceId) const;

  /**
   * @brief Clear events for instance
   * @param instanceId Instance ID
   */
  void clearEvents(const std::string &instanceId);

  /**
   * @brief Remove instance queue (cleanup)
   * @param instanceId Instance ID
   */
  void removeInstance(const std::string &instanceId);

  /**
   * @brief Set max queue size per instance (to prevent memory leak)
   * @param maxSize Maximum queue size (default: 1000)
   */
  void setMaxQueueSize(size_t maxSize) { max_queue_size_ = maxSize; }

private:
  EventQueue() = default;
  ~EventQueue() = default;
  EventQueue(const EventQueue &) = delete;
  EventQueue &operator=(const EventQueue &) = delete;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::queue<Event>> instance_queues_;
  size_t max_queue_size_ = 1000; // Max events per instance
};

