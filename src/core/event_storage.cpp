#include "core/event_storage.h"
#include <iostream>
#include <sstream>
#include <sqlite3.h>

// ============================================================================
// Constructor
// ============================================================================

EventStorage::EventStorage(sqlite3 *db) : db_(db) {
  if (!db_) {
    std::cerr << "[EventStorage] WARNING: Database handle is null" << std::endl;
    return;
  }

  if (!createTable()) {
    std::cerr << "[EventStorage] Error creating events table" << std::endl;
    return;
  }

  std::cerr << "[EventStorage] ✓ EventStorage ready" << std::endl;
}

// ============================================================================
// Table Creation
// ============================================================================

bool EventStorage::createTable() {
  if (!db_)
    return false;

  const char *sql = R"(
    CREATE TABLE IF NOT EXISTS events (
      id            INTEGER PRIMARY KEY AUTOINCREMENT,
      instance_id   TEXT NOT NULL,
      event_type    TEXT NOT NULL,
      event_data    TEXT DEFAULT '{}',
      timestamp     TEXT DEFAULT (datetime('now')),
      FOREIGN KEY (instance_id) REFERENCES instances(instance_id) ON DELETE CASCADE
    );

    CREATE INDEX IF NOT EXISTS idx_events_instance ON events(instance_id);
    CREATE INDEX IF NOT EXISTS idx_events_type ON events(event_type);
    CREATE INDEX IF NOT EXISTS idx_events_timestamp ON events(timestamp);
  )";

  char *errmsg = nullptr;
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    std::cerr << "[EventStorage] Error creating table: "
              << (errmsg ? errmsg : "unknown") << std::endl;
    if (errmsg)
      sqlite3_free(errmsg);
    return false;
  }
  return true;
}

// ============================================================================
// Record Events
// ============================================================================

int64_t EventStorage::recordEvent(const std::string &instanceId,
                                  const std::string &eventType,
                                  const std::string &eventData) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return -1;

  const char *insertSql =
      "INSERT INTO events (instance_id, event_type, event_data) "
      "VALUES (?, ?, ?)";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK)
    return -1;

  sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, eventType.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, eventData.c_str(), -1, SQLITE_TRANSIENT);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE)
    return -1;

  return sqlite3_last_insert_rowid(db_);
}

int64_t EventStorage::recordEvent(const std::string &instanceId,
                                  const std::string &eventType,
                                  const Json::Value &eventData) {
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  std::string dataStr = Json::writeString(builder, eventData);
  return recordEvent(instanceId, eventType, dataStr);
}

// ============================================================================
// Query Events
// ============================================================================

std::vector<EventStorage::Event>
EventStorage::getEvents(const std::string &instanceId, int limit, int offset) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Event> events;

  if (!db_)
    return events;

  const char *selectSql =
      "SELECT id, instance_id, event_type, event_data, timestamp "
      "FROM events WHERE instance_id = ? "
      "ORDER BY timestamp DESC LIMIT ? OFFSET ?";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK)
    return events;

  sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, limit > 0 ? limit : -1);
  sqlite3_bind_int(stmt, 3, offset);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Event event;
    event.id = sqlite3_column_int64(stmt, 0);

    const char *text;
    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    event.instanceId = text ? text : "";

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    event.eventType = text ? text : "";

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    event.eventData = text ? text : "{}";

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    event.timestamp = text ? text : "";

    events.push_back(event);
  }
  sqlite3_finalize(stmt);

  return events;
}

std::vector<EventStorage::Event>
EventStorage::getEventsByType(const std::string &instanceId,
                               const std::string &eventType, int limit) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Event> events;

  if (!db_)
    return events;

  const char *selectSql =
      "SELECT id, instance_id, event_type, event_data, timestamp "
      "FROM events WHERE instance_id = ? AND event_type = ? "
      "ORDER BY timestamp DESC LIMIT ?";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK)
    return events;

  sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, eventType.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 3, limit > 0 ? limit : -1);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Event event;
    event.id = sqlite3_column_int64(stmt, 0);

    const char *text;
    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    event.instanceId = text ? text : "";

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    event.eventType = text ? text : "";

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    event.eventData = text ? text : "{}";

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    event.timestamp = text ? text : "";

    events.push_back(event);
  }
  sqlite3_finalize(stmt);

  return events;
}

std::vector<EventStorage::Event>
EventStorage::getEventsByTimeRange(const std::string &instanceId,
                                    const std::string &from,
                                    const std::string &to) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Event> events;

  if (!db_)
    return events;

  const char *selectSql =
      "SELECT id, instance_id, event_type, event_data, timestamp "
      "FROM events WHERE instance_id = ? AND timestamp BETWEEN ? AND ? "
      "ORDER BY timestamp DESC";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK)
    return events;

  sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, from.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, to.c_str(), -1, SQLITE_TRANSIENT);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Event event;
    event.id = sqlite3_column_int64(stmt, 0);

    const char *text;
    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    event.instanceId = text ? text : "";

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    event.eventType = text ? text : "";

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    event.eventData = text ? text : "{}";

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    event.timestamp = text ? text : "";

    events.push_back(event);
  }
  sqlite3_finalize(stmt);

  return events;
}

int64_t EventStorage::getEventCount(const std::string &instanceId,
                                    const std::string &eventType) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return 0;

  std::string sql;
  if (eventType.empty()) {
    sql = "SELECT COUNT(*) FROM events WHERE instance_id = ?";
  } else {
    sql = "SELECT COUNT(*) FROM events WHERE instance_id = ? AND event_type = ?";
  }

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK)
    return 0;

  sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);
  if (!eventType.empty()) {
    sqlite3_bind_text(stmt, 2, eventType.c_str(), -1, SQLITE_TRANSIENT);
  }

  int64_t count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);

  return count;
}

// ============================================================================
// Delete / Purge
// ============================================================================

int EventStorage::deleteEvents(const std::string &instanceId) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return 0;

  const char *deleteSql = "DELETE FROM events WHERE instance_id = ?";
  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, deleteSql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK)
    return 0;

  sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE)
    return 0;

  int changes = sqlite3_changes(db_);
  if (changes > 0) {
    std::cerr << "[EventStorage] Deleted " << changes << " events for "
              << instanceId << std::endl;
  }
  return changes;
}

int EventStorage::purgeOldEvents(int retentionDays) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return 0;

  std::string sql =
      "DELETE FROM events WHERE timestamp < datetime('now', '-" +
      std::to_string(retentionDays) + " days')";

  char *errmsg = nullptr;
  int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    if (errmsg) {
      std::cerr << "[EventStorage] Purge error: " << errmsg << std::endl;
      sqlite3_free(errmsg);
    }
    return 0;
  }

  int changes = sqlite3_changes(db_);
  if (changes > 0) {
    std::cerr << "[EventStorage] ✓ Purged " << changes
              << " events older than " << retentionDays << " days"
              << std::endl;
  }
  return changes;
}
