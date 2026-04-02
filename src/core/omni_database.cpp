#include "core/omni_database.h"
#include "core/env_config.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sqlite3.h>

// ============================================================================
// Singleton
// ============================================================================

OmniDatabase &OmniDatabase::getInstance() {
  static OmniDatabase instance;
  return instance;
}

OmniDatabase::~OmniDatabase() { shutdown(); }

// ============================================================================
// Lifecycle
// ============================================================================

bool OmniDatabase::initialize(const std::string &storage_dir) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (db_) {
    std::cerr << "[OmniDatabase] Already initialized: " << db_path_
              << std::endl;
    return true;
  }

  storage_dir_ = storage_dir;
  ensureStorageDir();

  db_path_ = storage_dir_ + "/omniapi.db";
  std::cerr << "[OmniDatabase] Opening unified database: " << db_path_
            << std::endl;

  int rc = sqlite3_open(db_path_.c_str(), &db_);
  if (rc != SQLITE_OK) {
    std::cerr << "[OmniDatabase] Error opening database: "
              << sqlite3_errmsg(db_) << std::endl;
    db_ = nullptr;
    return false;
  }

  // Enable WAL mode for better concurrent read/write performance
  execSql("PRAGMA journal_mode=WAL");
  // Enable foreign keys
  execSql("PRAGMA foreign_keys=ON");
  // Synchronous NORMAL for balance of safety and speed
  execSql("PRAGMA synchronous=NORMAL");

  // Create meta table for schema versioning
  if (!createMetaTable()) {
    std::cerr << "[OmniDatabase] Error creating _meta table" << std::endl;
    shutdown();
    return false;
  }

  std::cerr << "[OmniDatabase] ✓ Unified database ready: " << db_path_
            << " (schema v" << getSchemaVersion() << ")" << std::endl;
  return true;
}

void OmniDatabase::shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_) {
    std::cerr << "[OmniDatabase] Closing database: " << db_path_ << std::endl;
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool OmniDatabase::isOpen() const { return db_ != nullptr; }

sqlite3 *OmniDatabase::getHandle() const { return db_; }

// ============================================================================
// Schema Version Management
// ============================================================================

bool OmniDatabase::createMetaTable() {
  const char *sql = R"(
    CREATE TABLE IF NOT EXISTS _meta (
      key   TEXT PRIMARY KEY,
      value TEXT NOT NULL
    );
  )";
  if (!execSql(sql)) {
    return false;
  }

  // Insert initial schema version if not present
  const char *insertSql =
      "INSERT OR IGNORE INTO _meta (key, value) VALUES ('schema_version', '0')";
  return execSql(insertSql);
}

int OmniDatabase::getSchemaVersion() const {
  if (!db_)
    return 0;

  const char *sql = "SELECT value FROM _meta WHERE key = 'schema_version'";
  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK)
    return 0;

  int version = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *text =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    if (text) {
      try {
        version = std::stoi(text);
      } catch (...) {
        version = 0;
      }
    }
  }
  sqlite3_finalize(stmt);
  return version;
}

bool OmniDatabase::setSchemaVersion(int version) {
  if (!db_)
    return false;

  const char *sql =
      "INSERT OR REPLACE INTO _meta (key, value) VALUES ('schema_version', ?)";
  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK)
    return false;

  std::string versionStr = std::to_string(version);
  sqlite3_bind_text(stmt, 1, versionStr.c_str(), -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE;
}

void OmniDatabase::registerMigration(
    int from_version, int to_version,
    std::function<bool(sqlite3 *)> migration_fn) {
  migrations_.push_back({from_version, to_version, std::move(migration_fn)});

  // Keep migrations sorted by from_version
  std::sort(migrations_.begin(), migrations_.end(),
            [](const Migration &a, const Migration &b) {
              return a.from_version < b.from_version;
            });
}

bool OmniDatabase::migrate() {
  if (!db_) {
    std::cerr << "[OmniDatabase] Cannot migrate: database not open"
              << std::endl;
    return false;
  }

  int currentVersion = getSchemaVersion();
  std::cerr << "[OmniDatabase] Current schema version: " << currentVersion
            << std::endl;

  if (migrations_.empty()) {
    std::cerr << "[OmniDatabase] No migrations registered" << std::endl;
    return true;
  }

  bool anyRun = false;
  for (const auto &migration : migrations_) {
    if (migration.from_version == currentVersion) {
      std::cerr << "[OmniDatabase] Running migration v" << migration.from_version
                << " → v" << migration.to_version << "..." << std::endl;

      // Run migration in a transaction
      if (!beginTransaction()) {
        std::cerr << "[OmniDatabase] Failed to begin transaction for migration"
                  << std::endl;
        return false;
      }

      if (!migration.fn(db_)) {
        std::cerr << "[OmniDatabase] Migration v" << migration.from_version
                  << " → v" << migration.to_version << " FAILED, rolling back"
                  << std::endl;
        rollback();
        return false;
      }

      if (!setSchemaVersion(migration.to_version)) {
        std::cerr << "[OmniDatabase] Failed to update schema version"
                  << std::endl;
        rollback();
        return false;
      }

      if (!commit()) {
        std::cerr << "[OmniDatabase] Failed to commit migration" << std::endl;
        return false;
      }

      currentVersion = migration.to_version;
      anyRun = true;
      std::cerr << "[OmniDatabase] ✓ Migration to v" << currentVersion
                << " complete" << std::endl;
    }
  }

  if (!anyRun) {
    std::cerr << "[OmniDatabase] ✓ Schema is up to date (v" << currentVersion
              << ")" << std::endl;
  }

  return true;
}

// ============================================================================
// Transaction Helpers
// ============================================================================

bool OmniDatabase::beginTransaction() { return execSql("BEGIN TRANSACTION"); }

bool OmniDatabase::commit() { return execSql("COMMIT"); }

bool OmniDatabase::rollback() { return execSql("ROLLBACK"); }

// ============================================================================
// Utility
// ============================================================================

bool OmniDatabase::execSql(const char *sql) {
  if (!db_)
    return false;
  char *errmsg = nullptr;
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    std::cerr << "[OmniDatabase] SQL error: "
              << (errmsg ? errmsg : "unknown") << std::endl;
    if (errmsg)
      sqlite3_free(errmsg);
    return false;
  }
  return true;
}

bool OmniDatabase::execSql(const char *sql, std::string &error) {
  if (!db_) {
    error = "Database not open";
    return false;
  }
  char *errmsg = nullptr;
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    error = errmsg ? errmsg : "unknown SQL error";
    if (errmsg)
      sqlite3_free(errmsg);
    return false;
  }
  return true;
}

void OmniDatabase::ensureStorageDir() {
  // Extract subdir name from storage_dir_ for fallback
  std::filesystem::path path(storage_dir_);
  std::string subdir = path.filename().string();
  if (subdir.empty()) {
    subdir = "data";
  }

  // Use resolveDirectory with 3-tier fallback strategy
  std::string resolved_dir = EnvConfig::resolveDirectory(storage_dir_, subdir);

  if (resolved_dir != storage_dir_) {
    std::cerr << "[OmniDatabase] ⚠ Storage directory changed from "
              << storage_dir_ << " to " << resolved_dir << " (fallback)"
              << std::endl;
    storage_dir_ = resolved_dir;
  }
}
