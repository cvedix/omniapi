#pragma once

#include <mutex>
#include <string>
#include <vector>
#include <functional>

// Forward declaration for SQLite
struct sqlite3;

/**
 * @brief OmniDatabase — Unified SQLite database for OmniAPI
 *
 * Singleton class that manages the lifecycle of a single SQLite database
 * shared across all storage modules (instances, groups, solutions, events,
 * statistics). Provides:
 *   - WAL mode for concurrent reads/writes
 *   - Schema versioning via _meta table
 *   - Sequential migration framework
 *   - Transaction helpers
 *
 * Usage:
 *   auto& db = OmniDatabase::getInstance();
 *   db.initialize("/opt/omniapi/data");
 *   // ... pass db.getHandle() to storage modules ...
 *   db.shutdown();
 */
class OmniDatabase {
public:
  /**
   * @brief Get singleton instance
   */
  static OmniDatabase &getInstance();

  // Non-copyable
  OmniDatabase(const OmniDatabase &) = delete;
  OmniDatabase &operator=(const OmniDatabase &) = delete;

  /**
   * @brief Initialize database in the given directory
   * Opens or creates omniapi.db with WAL mode, foreign keys, etc.
   * @param storage_dir Base directory for the database file
   * @return true if successful
   */
  bool initialize(const std::string &storage_dir);

  /**
   * @brief Shutdown database — close connection
   */
  void shutdown();

  /**
   * @brief Check if database is initialized and open
   */
  bool isOpen() const;

  /**
   * @brief Get raw SQLite handle for use by storage modules
   * @return sqlite3* handle, or nullptr if not initialized
   */
  sqlite3 *getHandle() const;

  /**
   * @brief Get database file path
   */
  std::string getDbPath() const { return db_path_; }

  /**
   * @brief Get storage base directory
   */
  std::string getStorageDir() const { return storage_dir_; }

  // ========== Schema Version Management ==========

  /**
   * @brief Get current schema version
   * @return Schema version number, or 0 if not set
   */
  int getSchemaVersion() const;

  /**
   * @brief Run all pending migrations
   * @return true if all migrations completed successfully
   */
  bool migrate();

  /**
   * @brief Register a migration function
   * Migrations are run sequentially: v1→v2, v2→v3, etc.
   * @param from_version Source version
   * @param to_version Target version
   * @param migration_fn Migration function (receives sqlite3* handle)
   */
  void registerMigration(int from_version, int to_version,
                         std::function<bool(sqlite3 *)> migration_fn);

  // ========== Transaction Helpers ==========

  /**
   * @brief Begin a transaction
   * @return true if successful
   */
  bool beginTransaction();

  /**
   * @brief Commit current transaction
   * @return true if successful
   */
  bool commit();

  /**
   * @brief Rollback current transaction
   * @return true if successful
   */
  bool rollback();

  // ========== Utility ==========

  /**
   * @brief Execute a simple SQL statement (no results expected)
   * @param sql SQL statement
   * @return true if successful
   */
  bool execSql(const char *sql);

  /**
   * @brief Execute SQL and return error message if any
   * @param sql SQL statement
   * @param error Output error message
   * @return true if successful
   */
  bool execSql(const char *sql, std::string &error);

private:
  OmniDatabase() = default;
  ~OmniDatabase();

  sqlite3 *db_ = nullptr;
  std::string db_path_;
  std::string storage_dir_;
  mutable std::mutex mutex_;

  // Migration registry
  struct Migration {
    int from_version;
    int to_version;
    std::function<bool(sqlite3 *)> fn;
  };
  std::vector<Migration> migrations_;

  /**
   * @brief Create _meta table for schema versioning
   */
  bool createMetaTable();

  /**
   * @brief Set schema version in _meta table
   */
  bool setSchemaVersion(int version);

  /**
   * @brief Ensure storage directory exists (with fallback)
   */
  void ensureStorageDir();
};
