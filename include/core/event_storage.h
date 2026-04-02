#pragma once

#include <json/json.h>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// Forward declaration for SQLite
struct sqlite3;

/**
 * @brief Event Storage
 *
 * Handles persistent storage of AI analytics events using SQLite.
 * Events include crossline counts, zone intrusions, jams, stops, etc.
 *
 * Events are stored in the shared OmniAPI database (omniapi.db)
 * and can be queried by instance, type, and time range.
 */
class EventStorage {
public:
  /**
   * @brief Event record structure
   */
  struct Event {
    int64_t id = 0;
    std::string instanceId;
    std::string eventType;   // "crossline", "zone_intrusion", "jam", "stop"
    std::string eventData;   // JSON blob with event-specific data
    std::string timestamp;   // ISO 8601 timestamp
  };

  /**
   * @brief Constructor
   * @param db SQLite database handle (from OmniDatabase)
   */
  explicit EventStorage(sqlite3 *db);

  /**
   * @brief Create the events table if it doesn't exist
   * @return true if successful
   */
  bool createTable();

  /**
   * @brief Record a new event
   * @param instanceId Instance that generated the event
   * @param eventType Type of event
   * @param eventData JSON data for the event
   * @return Event ID if successful, -1 otherwise
   */
  int64_t recordEvent(const std::string &instanceId,
                      const std::string &eventType,
                      const std::string &eventData = "{}");

  /**
   * @brief Record a new event (JSON overload)
   */
  int64_t recordEvent(const std::string &instanceId,
                      const std::string &eventType,
                      const Json::Value &eventData);

  /**
   * @brief Get events for an instance
   * @param instanceId Instance ID
   * @param limit Maximum events to return (0 = no limit)
   * @param offset Pagination offset
   * @return Vector of events
   */
  std::vector<Event> getEvents(const std::string &instanceId, int limit = 100,
                               int offset = 0);

  /**
   * @brief Get events by type for an instance
   * @param instanceId Instance ID
   * @param eventType Event type filter
   * @param limit Maximum events to return
   * @return Vector of events
   */
  std::vector<Event> getEventsByType(const std::string &instanceId,
                                      const std::string &eventType,
                                      int limit = 100);

  /**
   * @brief Get events within a time range
   * @param instanceId Instance ID
   * @param from Start time (ISO 8601)
   * @param to End time (ISO 8601)
   * @return Vector of events
   */
  std::vector<Event> getEventsByTimeRange(const std::string &instanceId,
                                           const std::string &from,
                                           const std::string &to);

  /**
   * @brief Get event count for an instance
   * @param instanceId Instance ID
   * @param eventType Optional type filter
   * @return Event count
   */
  int64_t getEventCount(const std::string &instanceId,
                        const std::string &eventType = "");

  /**
   * @brief Delete events for an instance
   * @param instanceId Instance ID
   * @return Number of deleted events
   */
  int deleteEvents(const std::string &instanceId);

  /**
   * @brief Purge events older than specified days
   * @param retentionDays Number of days to keep
   * @return Number of purged events
   */
  int purgeOldEvents(int retentionDays = 30);

private:
  sqlite3 *db_;
  mutable std::mutex mutex_;
};
