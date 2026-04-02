#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Forward declaration for SQLite
struct sqlite3;

/**
 * @brief Statistics Storage
 *
 * Handles persistent storage of runtime statistics snapshots using SQLite.
 * Periodically records FPS, frame counts, resolution, and uptime for
 * historical analysis and performance monitoring.
 *
 * Statistics are stored in the shared OmniAPI database (omniapi.db).
 */
class StatisticsStorage {
public:
  /**
   * @brief Statistics snapshot record
   */
  struct Snapshot {
    int64_t id = 0;
    std::string instanceId;
    double fps = 0.0;
    int64_t framesProcessed = 0;
    int64_t framesDropped = 0;
    std::string resolution;
    int64_t uptimeSeconds = 0;
    std::string snapshotAt;
  };

  /**
   * @brief Constructor
   * @param db SQLite database handle (from OmniDatabase)
   */
  explicit StatisticsStorage(sqlite3 *db);

  /**
   * @brief Create the statistics table if it doesn't exist
   * @return true if successful
   */
  bool createTable();

  /**
   * @brief Record a statistics snapshot
   * @param instanceId Instance ID
   * @param fps Current FPS
   * @param framesProcessed Total frames processed
   * @param framesDropped Total frames dropped
   * @param resolution Current resolution string
   * @param uptimeSeconds Instance uptime in seconds
   * @return Snapshot ID if successful, -1 otherwise
   */
  int64_t recordSnapshot(const std::string &instanceId, double fps,
                         int64_t framesProcessed, int64_t framesDropped,
                         const std::string &resolution, int64_t uptimeSeconds);

  /**
   * @brief Get recent snapshots for an instance
   * @param instanceId Instance ID
   * @param limit Maximum snapshots to return
   * @return Vector of snapshots (newest first)
   */
  std::vector<Snapshot> getSnapshots(const std::string &instanceId,
                                     int limit = 100);

  /**
   * @brief Get snapshots within a time range
   * @param instanceId Instance ID
   * @param from Start time (ISO 8601)
   * @param to End time (ISO 8601)
   * @return Vector of snapshots
   */
  std::vector<Snapshot> getSnapshotsByTimeRange(const std::string &instanceId,
                                                const std::string &from,
                                                const std::string &to);

  /**
   * @brief Get average FPS for an instance over a time period
   * @param instanceId Instance ID
   * @param minutes Number of minutes to average over
   * @return Average FPS
   */
  double getAverageFps(const std::string &instanceId, int minutes = 60);

  /**
   * @brief Delete all snapshots for an instance
   * @param instanceId Instance ID
   * @return Number of deleted rows
   */
  int deleteSnapshots(const std::string &instanceId);

  /**
   * @brief Purge snapshots older than specified days
   * @param retentionDays Number of days to keep
   * @return Number of purged rows
   */
  int purgeOldSnapshots(int retentionDays = 7);

private:
  sqlite3 *db_;
  mutable std::mutex mutex_;
};
