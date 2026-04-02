#include "core/statistics_storage.h"
#include <iostream>
#include <sqlite3.h>

// ============================================================================
// Constructor
// ============================================================================

StatisticsStorage::StatisticsStorage(sqlite3 *db) : db_(db) {
  if (!db_) {
    std::cerr << "[StatisticsStorage] WARNING: Database handle is null"
              << std::endl;
    return;
  }

  if (!createTable()) {
    std::cerr << "[StatisticsStorage] Error creating statistics table"
              << std::endl;
    return;
  }

  std::cerr << "[StatisticsStorage] ✓ StatisticsStorage ready" << std::endl;
}

// ============================================================================
// Table Creation
// ============================================================================

bool StatisticsStorage::createTable() {
  if (!db_)
    return false;

  const char *sql = R"(
    CREATE TABLE IF NOT EXISTS statistics_snapshots (
      id                INTEGER PRIMARY KEY AUTOINCREMENT,
      instance_id       TEXT NOT NULL,
      fps               REAL DEFAULT 0,
      frames_processed  INTEGER DEFAULT 0,
      frames_dropped    INTEGER DEFAULT 0,
      resolution        TEXT DEFAULT '',
      uptime_seconds    INTEGER DEFAULT 0,
      snapshot_at       TEXT DEFAULT (datetime('now')),
      FOREIGN KEY (instance_id) REFERENCES instances(instance_id) ON DELETE CASCADE
    );

    CREATE INDEX IF NOT EXISTS idx_stats_instance ON statistics_snapshots(instance_id);
    CREATE INDEX IF NOT EXISTS idx_stats_time ON statistics_snapshots(snapshot_at);
  )";

  char *errmsg = nullptr;
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    std::cerr << "[StatisticsStorage] Error creating table: "
              << (errmsg ? errmsg : "unknown") << std::endl;
    if (errmsg)
      sqlite3_free(errmsg);
    return false;
  }
  return true;
}

// ============================================================================
// Record Snapshot
// ============================================================================

int64_t StatisticsStorage::recordSnapshot(const std::string &instanceId,
                                          double fps,
                                          int64_t framesProcessed,
                                          int64_t framesDropped,
                                          const std::string &resolution,
                                          int64_t uptimeSeconds) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return -1;

  const char *insertSql =
      "INSERT INTO statistics_snapshots "
      "(instance_id, fps, frames_processed, frames_dropped, resolution, "
      "uptime_seconds) "
      "VALUES (?, ?, ?, ?, ?, ?)";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK)
    return -1;

  sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 2, fps);
  sqlite3_bind_int64(stmt, 3, framesProcessed);
  sqlite3_bind_int64(stmt, 4, framesDropped);
  sqlite3_bind_text(stmt, 5, resolution.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 6, uptimeSeconds);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE)
    return -1;

  return sqlite3_last_insert_rowid(db_);
}

// ============================================================================
// Query Snapshots
// ============================================================================

std::vector<StatisticsStorage::Snapshot>
StatisticsStorage::getSnapshots(const std::string &instanceId, int limit) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Snapshot> snapshots;

  if (!db_)
    return snapshots;

  const char *selectSql =
      "SELECT id, instance_id, fps, frames_processed, frames_dropped, "
      "resolution, uptime_seconds, snapshot_at "
      "FROM statistics_snapshots WHERE instance_id = ? "
      "ORDER BY snapshot_at DESC LIMIT ?";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK)
    return snapshots;

  sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, limit > 0 ? limit : -1);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Snapshot s;
    s.id = sqlite3_column_int64(stmt, 0);

    const char *text =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    s.instanceId = text ? text : "";

    s.fps = sqlite3_column_double(stmt, 2);
    s.framesProcessed = sqlite3_column_int64(stmt, 3);
    s.framesDropped = sqlite3_column_int64(stmt, 4);

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
    s.resolution = text ? text : "";

    s.uptimeSeconds = sqlite3_column_int64(stmt, 6);

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
    s.snapshotAt = text ? text : "";

    snapshots.push_back(s);
  }
  sqlite3_finalize(stmt);

  return snapshots;
}

std::vector<StatisticsStorage::Snapshot>
StatisticsStorage::getSnapshotsByTimeRange(const std::string &instanceId,
                                           const std::string &from,
                                           const std::string &to) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Snapshot> snapshots;

  if (!db_)
    return snapshots;

  const char *selectSql =
      "SELECT id, instance_id, fps, frames_processed, frames_dropped, "
      "resolution, uptime_seconds, snapshot_at "
      "FROM statistics_snapshots "
      "WHERE instance_id = ? AND snapshot_at BETWEEN ? AND ? "
      "ORDER BY snapshot_at DESC";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK)
    return snapshots;

  sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, from.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, to.c_str(), -1, SQLITE_TRANSIENT);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Snapshot s;
    s.id = sqlite3_column_int64(stmt, 0);

    const char *text =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    s.instanceId = text ? text : "";

    s.fps = sqlite3_column_double(stmt, 2);
    s.framesProcessed = sqlite3_column_int64(stmt, 3);
    s.framesDropped = sqlite3_column_int64(stmt, 4);

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
    s.resolution = text ? text : "";

    s.uptimeSeconds = sqlite3_column_int64(stmt, 6);

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
    s.snapshotAt = text ? text : "";

    snapshots.push_back(s);
  }
  sqlite3_finalize(stmt);

  return snapshots;
}

double StatisticsStorage::getAverageFps(const std::string &instanceId,
                                        int minutes) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return 0.0;

  std::string sql =
      "SELECT AVG(fps) FROM statistics_snapshots "
      "WHERE instance_id = ? AND snapshot_at >= datetime('now', '-" +
      std::to_string(minutes) + " minutes')";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK)
    return 0.0;

  sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);

  double avg = 0.0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    avg = sqlite3_column_double(stmt, 0);
  }
  sqlite3_finalize(stmt);

  return avg;
}

// ============================================================================
// Delete / Purge
// ============================================================================

int StatisticsStorage::deleteSnapshots(const std::string &instanceId) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return 0;

  const char *deleteSql =
      "DELETE FROM statistics_snapshots WHERE instance_id = ?";
  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, deleteSql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK)
    return 0;

  sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE)
    return 0;

  return sqlite3_changes(db_);
}

int StatisticsStorage::purgeOldSnapshots(int retentionDays) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return 0;

  std::string sql =
      "DELETE FROM statistics_snapshots WHERE snapshot_at < datetime('now', '-" +
      std::to_string(retentionDays) + " days')";

  char *errmsg = nullptr;
  int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    if (errmsg) {
      std::cerr << "[StatisticsStorage] Purge error: " << errmsg << std::endl;
      sqlite3_free(errmsg);
    }
    return 0;
  }

  int changes = sqlite3_changes(db_);
  if (changes > 0) {
    std::cerr << "[StatisticsStorage] ✓ Purged " << changes
              << " snapshots older than " << retentionDays << " days"
              << std::endl;
  }
  return changes;
}
