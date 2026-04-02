#include "instances/instance_storage.h"
#include "core/env_config.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <json/json.h>
#include <sstream>
#include <sqlite3.h>
#include <vector>

// ============================================================================
// Constructor / Destructor
// ============================================================================

InstanceStorage::InstanceStorage(const std::string &storage_dir)
    : storage_dir_(storage_dir), owns_db_(true) {
  ensureStorageDir();
  if (!openDatabase()) {
    std::cerr << "[InstanceStorage] CRITICAL: Failed to open SQLite database!"
              << std::endl;
  }
}

InstanceStorage::InstanceStorage(sqlite3 *db,
                                 const std::string &legacy_storage_dir)
    : db_(db), owns_db_(false), storage_dir_(legacy_storage_dir) {
  if (!db_) {
    std::cerr << "[InstanceStorage] WARNING: Database handle is null"
              << std::endl;
    return;
  }

  if (!createTables()) {
    std::cerr << "[InstanceStorage] Error creating tables" << std::endl;
    return;
  }

  // Auto-migrate from instances.json if legacy directory is provided
  if (!legacy_storage_dir.empty()) {
    migrateFromJson();
  }

  // Also check for legacy instances.db and migrate data from it
  if (!legacy_storage_dir.empty()) {
    std::string legacyDbPath = legacy_storage_dir + "/instances.db";
    if (std::filesystem::exists(legacyDbPath)) {
      std::cerr << "[InstanceStorage] Found legacy instances.db, migrating: "
                << legacyDbPath << std::endl;

      sqlite3 *legacyDb = nullptr;
      int rc = sqlite3_open_v2(legacyDbPath.c_str(), &legacyDb,
                               SQLITE_OPEN_READONLY, nullptr);
      if (rc == SQLITE_OK && legacyDb) {
        // Read all instances from legacy db
        const char *selectSql =
            "SELECT instance_id, display_name, group_name, solution_id, "
            "solution_name, persistent, auto_start, auto_restart, "
            "system_instance, read_only, config_json FROM instances";
        sqlite3_stmt *stmt = nullptr;
        rc = sqlite3_prepare_v2(legacyDb, selectSql, -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
          int migrated = 0;
          while (sqlite3_step(stmt) == SQLITE_ROW) {
            // Read from legacy
            const char *instanceId = reinterpret_cast<const char *>(
                sqlite3_column_text(stmt, 0));
            if (!instanceId)
              continue;

            // Check if already exists in new db
            if (instanceExists(instanceId))
              continue;

            // Copy row into new db using INSERT
            const char *insertSql =
                "INSERT OR IGNORE INTO instances "
                "(instance_id, display_name, group_name, solution_id, "
                "solution_name, persistent, auto_start, auto_restart, "
                "system_instance, read_only, config_json) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
            sqlite3_stmt *insertStmt = nullptr;
            int irc =
                sqlite3_prepare_v2(db_, insertSql, -1, &insertStmt, nullptr);
            if (irc == SQLITE_OK) {
              for (int i = 0; i < 11; i++) {
                int colType = sqlite3_column_type(stmt, i);
                if (colType == SQLITE_NULL) {
                  sqlite3_bind_null(insertStmt, i + 1);
                } else if (colType == SQLITE_INTEGER) {
                  sqlite3_bind_int(insertStmt, i + 1,
                                   sqlite3_column_int(stmt, i));
                } else {
                  const char *text = reinterpret_cast<const char *>(
                      sqlite3_column_text(stmt, i));
                  sqlite3_bind_text(insertStmt, i + 1, text ? text : "", -1,
                                    SQLITE_TRANSIENT);
                }
              }
              if (sqlite3_step(insertStmt) == SQLITE_DONE) {
                migrated++;
              }
              sqlite3_finalize(insertStmt);
            }
          }
          sqlite3_finalize(stmt);

          if (migrated > 0) {
            std::cerr << "[InstanceStorage] ✓ Migrated " << migrated
                      << " instances from legacy instances.db" << std::endl;
          }
        }
        sqlite3_close(legacyDb);

        // Rename legacy db
        std::string backupPath = legacyDbPath + ".migrated";
        try {
          std::filesystem::rename(legacyDbPath, backupPath);
          std::cerr << "[InstanceStorage] ✓ Renamed legacy db → "
                    << backupPath << std::endl;
        } catch (const std::exception &e) {
          std::cerr << "[InstanceStorage] Warning: Could not rename legacy db: "
                    << e.what() << std::endl;
        }
      }
    }
  }

  std::cerr << "[InstanceStorage] ✓ InstanceStorage ready (OmniDatabase mode)"
            << std::endl;
}

InstanceStorage::~InstanceStorage() {
  if (owns_db_) {
    closeDatabase();
  }
}

// ============================================================================
// SQLite Database Lifecycle
// ============================================================================

void InstanceStorage::ensureStorageDir() {
  // Extract subdir name from storage_dir_ for fallback
  std::filesystem::path path(storage_dir_);
  std::string subdir = path.filename().string();
  if (subdir.empty()) {
    subdir = "instances";
  }

  // Use resolveDirectory with 3-tier fallback strategy
  std::string resolved_dir = EnvConfig::resolveDirectory(storage_dir_, subdir);

  if (resolved_dir != storage_dir_) {
    std::cerr << "[InstanceStorage] ⚠ Storage directory changed from "
              << storage_dir_ << " to " << resolved_dir << " (fallback)"
              << std::endl;
    storage_dir_ = resolved_dir;
  }
}

bool InstanceStorage::openDatabase() {
  db_path_ = storage_dir_ + "/instances.db";
  std::cerr << "[InstanceStorage] Opening SQLite database: " << db_path_
            << std::endl;

  int rc = sqlite3_open(db_path_.c_str(), &db_);
  if (rc != SQLITE_OK) {
    std::cerr << "[InstanceStorage] Error opening database: "
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

  if (!createTables()) {
    std::cerr << "[InstanceStorage] Error creating tables" << std::endl;
    closeDatabase();
    return false;
  }

  // Auto-migrate from instances.json if it exists
  migrateFromJson();

  std::cerr << "[InstanceStorage] ✓ SQLite database ready: " << db_path_
            << std::endl;
  return true;
}

void InstanceStorage::closeDatabase() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool InstanceStorage::createTables() {
  const char *sql = R"(
    CREATE TABLE IF NOT EXISTS instances (
      instance_id     TEXT PRIMARY KEY,
      instance_name   TEXT DEFAULT '',
      display_name    TEXT DEFAULT '',
      group_name      TEXT DEFAULT '',
      solution_id     TEXT DEFAULT '',
      solution_name   TEXT DEFAULT '',
      persistent      INTEGER DEFAULT 1,
      auto_start      INTEGER DEFAULT 0,
      auto_restart    INTEGER DEFAULT 0,
      system_instance INTEGER DEFAULT 0,
      read_only       INTEGER DEFAULT 0,
      config_json     TEXT NOT NULL DEFAULT '{}',
      created_at      TEXT DEFAULT (datetime('now')),
      updated_at      TEXT DEFAULT (datetime('now'))
    );
  )";
  return execSql(sql);
}

bool InstanceStorage::execSql(const char *sql) {
  if (!db_) return false;
  char *errmsg = nullptr;
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    std::cerr << "[InstanceStorage] SQL error: " << (errmsg ? errmsg : "unknown")
              << std::endl;
    if (errmsg) sqlite3_free(errmsg);
    return false;
  }
  return true;
}

// ============================================================================
// Migration from instances.json
// ============================================================================

void InstanceStorage::migrateFromJson() {
  std::string jsonPath = storage_dir_ + "/instances.json";
  if (!std::filesystem::exists(jsonPath)) {
    // Also check fallback tiers for instances.json
    std::filesystem::path path(storage_dir_);
    std::string subdir = path.filename().string();
    if (subdir.empty()) subdir = "instances";

    std::vector<std::string> allDirs =
        EnvConfig::getAllPossibleDirectories(subdir);
    bool found = false;
    for (const auto &dir : allDirs) {
      std::string tierPath = dir + "/instances.json";
      if (std::filesystem::exists(tierPath)) {
        jsonPath = tierPath;
        found = true;
        break;
      }
    }
    if (!found) return; // No JSON file to migrate
  }

  std::cerr << "[InstanceStorage] Found instances.json, starting migration to "
               "SQLite: "
            << jsonPath << std::endl;

  try {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
      std::cerr << "[InstanceStorage] Cannot open instances.json for migration"
                << std::endl;
      return;
    }

    Json::CharReaderBuilder builder;
    std::string errors;
    Json::Value root(Json::objectValue);
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
      std::cerr << "[InstanceStorage] JSON parse error during migration: "
                << errors << std::endl;
      return;
    }
    file.close();

    int migrated = 0;
    int failed = 0;

    // Begin transaction for bulk insert
    execSql("BEGIN TRANSACTION");

    for (const auto &key : root.getMemberNames()) {
      const Json::Value &value = root[key];
      if (!value.isObject()) continue;

      // Check if this is an instance config
      bool isInstance = false;
      if (value.isMember("InstanceId") && value["InstanceId"].isString()) {
        isInstance = true;
      } else if (key.length() >= 36 && key.find('-') != std::string::npos) {
        isInstance = true;
      }

      if (!isInstance) continue;

      // Convert JSON to InstanceInfo to validate
      std::string conversionError;
      auto info = configJsonToInstanceInfo(value, &conversionError);
      if (!info.has_value()) {
        std::cerr << "[InstanceStorage] Migration: skipping invalid instance "
                  << key << ": " << conversionError << std::endl;
        failed++;
        continue;
      }

      // Ensure instanceId matches key
      if (info->instanceId != key) {
        info->instanceId = key;
      }

      // Save to SQLite (serialize the full config JSON blob)
      Json::StreamWriterBuilder writerBuilder;
      writerBuilder["indentation"] = "";
      std::string configStr = Json::writeString(writerBuilder, value);

      // Extract metadata fields
      std::string displayName = info->displayName;
      std::string groupName = info->group;
      std::string solutionId = info->solutionId;
      std::string solutionName = info->solutionName;
      int autoStart = info->autoStart ? 1 : 0;
      int autoRestart = info->autoRestart ? 1 : 0;
      int systemInstance = info->systemInstance ? 1 : 0;
      int readOnly = info->readOnly ? 1 : 0;

      const char *insertSql =
          "INSERT OR REPLACE INTO instances "
          "(instance_id, display_name, group_name, solution_id, solution_name, "
          "persistent, auto_start, auto_restart, system_instance, read_only, "
          "config_json, updated_at) "
          "VALUES (?, ?, ?, ?, ?, 1, ?, ?, ?, ?, ?, datetime('now'))";

      sqlite3_stmt *stmt = nullptr;
      int rc = sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr);
      if (rc != SQLITE_OK) {
        std::cerr << "[InstanceStorage] Migration prepare error: "
                  << sqlite3_errmsg(db_) << std::endl;
        failed++;
        continue;
      }

      sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, displayName.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 3, groupName.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 4, solutionId.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 5, solutionName.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(stmt, 6, autoStart);
      sqlite3_bind_int(stmt, 7, autoRestart);
      sqlite3_bind_int(stmt, 8, systemInstance);
      sqlite3_bind_int(stmt, 9, readOnly);
      sqlite3_bind_text(stmt, 10, configStr.c_str(), -1, SQLITE_TRANSIENT);

      rc = sqlite3_step(stmt);
      sqlite3_finalize(stmt);

      if (rc != SQLITE_DONE) {
        std::cerr << "[InstanceStorage] Migration insert error for " << key
                  << ": " << sqlite3_errmsg(db_) << std::endl;
        failed++;
      } else {
        migrated++;
      }
    }

    execSql("COMMIT");

    std::cerr << "[InstanceStorage] ✓ Migration complete: " << migrated
              << " instances migrated, " << failed << " failed" << std::endl;

    // Rename original JSON file as backup
    if (migrated > 0) {
      std::string backupPath = jsonPath + ".migrated";
      try {
        std::filesystem::rename(jsonPath, backupPath);
        std::cerr << "[InstanceStorage] ✓ Renamed " << jsonPath << " → "
                  << backupPath << std::endl;
      } catch (const std::exception &e) {
        std::cerr << "[InstanceStorage] Warning: Could not rename JSON file: "
                  << e.what() << std::endl;
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "[InstanceStorage] Migration exception: " << e.what()
              << std::endl;
    execSql("ROLLBACK");
  }
}

// ============================================================================
// CRUD Operations (SQLite backend)
// ============================================================================

bool InstanceStorage::saveInstance(const std::string &instanceId,
                                   const InstanceInfo &info) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::cerr << "[InstanceStorage] saveInstance called for instance: "
            << instanceId << std::endl;

  if (!db_) {
    std::cerr << "[InstanceStorage] Error: Database not open" << std::endl;
    return false;
  }

  try {
    // Validate instanceId matches
    if (info.instanceId != instanceId) {
      std::cerr << "[InstanceStorage] Error: InstanceId mismatch. Expected: "
                << instanceId << ", Got: " << info.instanceId << std::endl;
      return false;
    }

    // Validate InstanceInfo
    std::string validationError;
    if (!validateInstanceInfo(info, validationError)) {
      std::cerr << "[InstanceStorage] Validation error: " << validationError
                << std::endl;
      return false;
    }

    // Convert InstanceInfo to config JSON format
    std::string conversionError;
    Json::Value config = instanceInfoToConfigJson(info, &conversionError);
    if (config.isNull() || config.empty()) {
      std::cerr << "[InstanceStorage] Conversion error: " << conversionError
                << std::endl;
      return false;
    }

    // If instance already exists, merge with existing config to preserve
    // TensorRT and other nested configs
    std::string existingConfigStr;
    {
      const char *selectSql =
          "SELECT config_json FROM instances WHERE instance_id = ?";
      sqlite3_stmt *stmt = nullptr;
      int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);
      if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
          const char *text =
              reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
          if (text) existingConfigStr = text;
        }
      }
      sqlite3_finalize(stmt);
    }

    if (!existingConfigStr.empty()) {
      // Parse existing config and merge
      Json::CharReaderBuilder readerBuilder;
      std::string parseErrors;
      Json::Value existingConfig;
      std::istringstream stream(existingConfigStr);
      if (Json::parseFromStream(readerBuilder, stream, &existingConfig,
                                &parseErrors)) {
        // List of keys to preserve (TensorRT model IDs, Zone IDs, etc.)
        std::vector<std::string> preserveKeys;

        // Collect UUID-like keys (TensorRT model IDs)
        for (const auto &key : existingConfig.getMemberNames()) {
          if (key.length() >= 36 && key.find('-') != std::string::npos) {
            preserveKeys.push_back(key);
          }
        }

        // Add special keys to preserve
        std::vector<std::string> specialKeys = {
            "AnimalTracker",     "DetectorRegions",
            "DetectorThermal",   "Global",
            "LicensePlateTracker", "ObjectAttributeExtraction",
            "ObjectMovementClassifier", "PersonTracker",
            "Tripwire",          "VehicleTracker",
            "Zone"};
        preserveKeys.insert(preserveKeys.end(), specialKeys.begin(),
                            specialKeys.end());

        // Merge configs
        if (!mergeConfigs(existingConfig, config, preserveKeys)) {
          std::cerr << "[InstanceStorage] Merge failed for instance "
                    << instanceId << std::endl;
          return false;
        }

        config = existingConfig;
      }
    }

    // Serialize config to compact JSON string
    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    std::string configStr = Json::writeString(writerBuilder, config);

    // Insert or replace in database
    const char *upsertSql =
        "INSERT OR REPLACE INTO instances "
        "(instance_id, instance_name, display_name, group_name, solution_id, solution_name, "
        "persistent, auto_start, auto_restart, system_instance, read_only, "
        "config_json, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, 1, ?, ?, ?, ?, ?, datetime('now'))";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, upsertSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      std::cerr << "[InstanceStorage] Prepare error: " << sqlite3_errmsg(db_)
                << std::endl;
      return false;
    }

    sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, info.instanceName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, info.displayName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, info.group.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, info.solutionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, info.solutionName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, info.autoStart ? 1 : 0);
    sqlite3_bind_int(stmt, 8, info.autoRestart ? 1 : 0);
    sqlite3_bind_int(stmt, 9, info.systemInstance ? 1 : 0);
    sqlite3_bind_int(stmt, 10, info.readOnly ? 1 : 0);
    sqlite3_bind_text(stmt, 11, configStr.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
      std::cerr << "[InstanceStorage] Insert error: " << sqlite3_errmsg(db_)
                << std::endl;
      return false;
    }

    std::cerr << "[InstanceStorage] ✓ Successfully saved instance: "
              << instanceId << std::endl;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[InstanceStorage] Exception in saveInstance: " << e.what()
              << std::endl;
    return false;
  }
}

std::optional<InstanceInfo>
InstanceStorage::loadInstance(const std::string &instanceId) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_) return std::nullopt;

  try {
    if (instanceId.empty()) {
      std::cerr << "[InstanceStorage] Error: Empty instanceId provided"
                << std::endl;
      return std::nullopt;
    }

    const char *selectSql =
        "SELECT config_json FROM instances WHERE instance_id = ?";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      std::cerr << "[InstanceStorage] Prepare error: " << sqlite3_errmsg(db_)
                << std::endl;
      return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
      sqlite3_finalize(stmt);
      std::cerr << "[InstanceStorage] Instance " << instanceId
                << " not found in database" << std::endl;
      return std::nullopt;
    }

    const char *configText =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    std::string configStr = configText ? configText : "{}";
    sqlite3_finalize(stmt);

    // Parse JSON config
    Json::CharReaderBuilder readerBuilder;
    std::string parseErrors;
    Json::Value config;
    std::istringstream stream(configStr);
    if (!Json::parseFromStream(readerBuilder, stream, &config, &parseErrors)) {
      std::cerr << "[InstanceStorage] JSON parse error for instance "
                << instanceId << ": " << parseErrors << std::endl;
      return std::nullopt;
    }

    // Convert config JSON to InstanceInfo
    std::string conversionError;
    auto info = configJsonToInstanceInfo(config, &conversionError);
    if (!info.has_value()) {
      std::cerr << "[InstanceStorage] Conversion error for instance "
                << instanceId << ": " << conversionError << std::endl;
      return std::nullopt;
    }

    // Verify instanceId matches
    if (info->instanceId != instanceId) {
      info->instanceId = instanceId;
    }

    return info;
  } catch (const std::exception &e) {
    std::cerr << "[InstanceStorage] Exception in loadInstance: " << e.what()
              << std::endl;
    return std::nullopt;
  }
}

std::vector<std::string> InstanceStorage::loadAllInstances() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> loaded;

  if (!db_) return loaded;

  try {
    const char *selectSql = "SELECT instance_id FROM instances";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      std::cerr << "[InstanceStorage] Prepare error: " << sqlite3_errmsg(db_)
                << std::endl;
      return loaded;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const char *id =
          reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
      if (id) {
        loaded.push_back(std::string(id));
      }
    }
    sqlite3_finalize(stmt);
  } catch (const std::exception &e) {
    std::cerr << "[InstanceStorage] Exception in loadAllInstances: " << e.what()
              << std::endl;
  }

  return loaded;
}

bool InstanceStorage::deleteInstance(const std::string &instanceId) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_) return false;

  try {
    const char *deleteSql = "DELETE FROM instances WHERE instance_id = ?";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, deleteSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      std::cerr << "[InstanceStorage] Prepare error: " << sqlite3_errmsg(db_)
                << std::endl;
      return false;
    }

    sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
      std::cerr << "[InstanceStorage] Delete error: " << sqlite3_errmsg(db_)
                << std::endl;
      return false;
    }

    int changes = sqlite3_changes(db_);
    if (changes > 0) {
      std::cerr << "[InstanceStorage] ✓ Deleted instance: " << instanceId
                << std::endl;
    } else {
      std::cerr << "[InstanceStorage] Instance " << instanceId
                << " not found for deletion" << std::endl;
    }

    return changes > 0;
  } catch (const std::exception &e) {
    std::cerr << "[InstanceStorage] Exception in deleteInstance: " << e.what()
              << std::endl;
    return false;
  }
}

bool InstanceStorage::instanceExists(const std::string &instanceId) const {
  if (!db_) return false;

  try {
    const char *selectSql =
        "SELECT 1 FROM instances WHERE instance_id = ? LIMIT 1";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, instanceId.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_ROW;
  } catch (const std::exception &e) {
    return false;
  }
}

// ============================================================================
// Validation Methods (unchanged from original)
// ============================================================================

bool InstanceStorage::validateInstanceInfo(const InstanceInfo &info,
                                           std::string &error) const {
  if (info.instanceId.empty()) {
    error = "InstanceId cannot be empty";
    return false;
  }

  // Validate instanceId format (should be UUID-like)
  if (info.instanceId.length() < 10) {
    error = "InstanceId format appears invalid (too short)";
    return false;
  }

  // Validate displayName if provided
  if (!info.displayName.empty() && info.displayName.length() > 255) {
    error = "DisplayName too long (max 255 characters)";
    return false;
  }

  // Validate frameRateLimit
  if (info.frameRateLimit < 0 || info.frameRateLimit > 1000) {
    error = "frameRateLimit must be between 0 and 1000";
    return false;
  }

  // Validate inputOrientation
  if (info.inputOrientation < 0 || info.inputOrientation > 3) {
    error = "inputOrientation must be between 0 and 3";
    return false;
  }

  // Validate inputPixelLimit
  if (info.inputPixelLimit < 0) {
    error = "inputPixelLimit cannot be negative";
    return false;
  }

  return true;
}

bool InstanceStorage::validateConfigJson(const Json::Value &config,
                                         std::string &error) const {
  if (!config.isObject()) {
    error = "Config must be a JSON object";
    return false;
  }

  // InstanceId is required
  if (!config.isMember("InstanceId") || !config["InstanceId"].isString()) {
    error = "Config must contain 'InstanceId' as a string";
    return false;
  }

  std::string instanceId = config["InstanceId"].asString();
  if (instanceId.empty()) {
    error = "InstanceId cannot be empty";
    return false;
  }

  // Validate nested structures if present
  if (config.isMember("Input") && !config["Input"].isObject()) {
    error = "Input must be a JSON object";
    return false;
  }

  if (config.isMember("SolutionManager") &&
      !config["SolutionManager"].isObject()) {
    error = "SolutionManager must be a JSON object";
    return false;
  }

  if (config.isMember("Detector") && !config["Detector"].isObject()) {
    error = "Detector must be a JSON object";
    return false;
  }

  if (config.isMember("Logging") && !config["Logging"].isObject()) {
    error = "Logging must be a JSON object";
    return false;
  }

  return true;
}

// ============================================================================
// Merge Configs (unchanged from original)
// ============================================================================

bool InstanceStorage::mergeConfigs(
    Json::Value &existingConfig, const Json::Value &newConfig,
    const std::vector<std::string> &preserveKeys) const {
  if (!existingConfig.isObject() || !newConfig.isObject()) {
    return false;
  }

  // List of keys that should be completely replaced (not merged)
  std::vector<std::string> replaceKeys = {
      "InstanceId",  "DisplayName", "Solution",       "SolutionName",
      "Group",       "ReadOnly",    "SystemInstance", "AutoStart",
      "AutoRestart", "logging",     "loaded",         "running",
      "fps",         "version"};

  // List of keys that should be merged (nested objects)
  std::vector<std::string> mergeKeys = {
      "Input",           "SolutionManager",  "Detector",
      "DetectorRegions", "DetectorThermal",  "Movement",
      "OriginatorInfo",  "AdditionalParams", "Output",
      "PerformanceMode", "Tripwire",         "Zone"};

  // Replace simple fields
  for (const auto &key : replaceKeys) {
    if (newConfig.isMember(key)) {
      existingConfig[key] = newConfig[key];
    }
  }

  // Merge nested objects
  for (const auto &key : mergeKeys) {
    if (newConfig.isMember(key) && newConfig[key].isObject()) {
      if (!existingConfig.isMember(key)) {
        existingConfig[key] = Json::Value(Json::objectValue);
      }

      // Special handling for AdditionalParams: flatten nested input/output
      // structure
      if (key == "AdditionalParams") {
        Json::Value flattenedParams(Json::objectValue);

        // Check if newParams has nested input/output structure
        const Json::Value &newParams = newConfig[key];
        bool hasNestedStructure =
            newParams.isMember("input") || newParams.isMember("output");

        if (hasNestedStructure) {
          // If nested structure, start with existing params (to preserve keys
          // not in the update)
          flattenedParams = existingConfig[key];
          if (!flattenedParams.isObject()) {
            flattenedParams = Json::Value(Json::objectValue);
          }

          // Flatten input section - only replace keys that appear in the
          // request
          if (newParams.isMember("input") && newParams["input"].isObject()) {
            for (const auto &inputKey : newParams["input"].getMemberNames()) {
              if (newParams["input"][inputKey].isString()) {
                flattenedParams[inputKey] = newParams["input"][inputKey];
              }
            }
          }

          // Flatten output section - only replace keys that appear in the
          // request
          if (newParams.isMember("output") && newParams["output"].isObject()) {
            for (const auto &outputKey : newParams["output"].getMemberNames()) {
              if (newParams["output"][outputKey].isString()) {
                flattenedParams[outputKey] = newParams["output"][outputKey];
              }
            }
          }

          // Also merge flat keys (backward compatibility) - only replace keys
          // in request
          for (const auto &flatKey : newParams.getMemberNames()) {
            if (flatKey != "input" && flatKey != "output" &&
                newParams[flatKey].isString()) {
              flattenedParams[flatKey] = newParams[flatKey];
            }
          }
        } else {
          // If flat structure, merge with existing params (preserve keys not in
          // update) Start with existing params to preserve keys like
          // WEIGHTS_PATH, CONFIG_PATH, etc.
          flattenedParams = existingConfig[key];
          if (!flattenedParams.isObject()) {
            flattenedParams = Json::Value(Json::objectValue);
          }

          // Only update keys that appear in the new params (merge, don't
          // replace)
          std::cerr << "[InstanceStorage] Merging flat AdditionalParams: "
                       "preserving existing keys, updating "
                    << newParams.size() << " keys from update" << std::endl;
          for (const auto &flatKey : newParams.getMemberNames()) {
            if (newParams[flatKey].isString()) {
              std::cerr << "[InstanceStorage] Updating key: " << flatKey
                        << " = " << newParams[flatKey].asString() << std::endl;
              flattenedParams[flatKey] = newParams[flatKey];
            }
          }
        }

        // Update AdditionalParams with merged/flattened version
        std::cerr << "[InstanceStorage] Final AdditionalParams has "
                  << flattenedParams.size() << " keys" << std::endl;
        existingConfig[key] = flattenedParams;
      } else {
        // For other nested objects, do deep merge as before
        for (const auto &nestedKey : newConfig[key].getMemberNames()) {
          existingConfig[key][nestedKey] = newConfig[key][nestedKey];
        }
      }
    }
  }

  // Preserve special keys (TensorRT, Zone, Tripwire, etc.) from existing config
  for (const auto &preserveKey : preserveKeys) {
    if (existingConfig.isMember(preserveKey) &&
        !newConfig.isMember(preserveKey)) {
      // Keep existing value
      continue;
    }
  }

  // Also preserve any UUID-like keys (TensorRT model IDs, Zone IDs, etc.)
  // These are typically UUIDs that map to complex config objects
  for (const auto &key : existingConfig.getMemberNames()) {
    // Check if key looks like a UUID (contains dashes and is long enough)
    if (key.length() >= 36 && key.find('-') != std::string::npos) {
      // This is likely a TensorRT model ID or similar - preserve it
      if (!newConfig.isMember(key)) {
        // Keep existing UUID-keyed configs
        continue;
      }
    }

    // Preserve special non-instance keys
    std::vector<std::string> specialKeys = {"AnimalTracker",
                                            "AutoRestart",
                                            "AutoStart",
                                            "Detector",
                                            "DetectorRegions",
                                            "DetectorThermal",
                                            "Global",
                                            "LicensePlateTracker",
                                            "ObjectAttributeExtraction",
                                            "ObjectMovementClassifier",
                                            "PersonTracker",
                                            "Tripwire",
                                            "VehicleTracker",
                                            "Zone"};

    for (const auto &specialKey : specialKeys) {
      if (key == specialKey && existingConfig.isMember(key) &&
          !newConfig.isMember(key)) {
        // Preserve special key from existing config
        continue;
      }
    }
  }

  return true;
}

// ============================================================================
// JSON Conversion: InstanceInfo ↔ Config JSON (unchanged from original)
// ============================================================================

Json::Value
InstanceStorage::instanceInfoToConfigJson(const InstanceInfo &info,
                                          std::string *error) const {
  Json::Value config(Json::objectValue);

  // Validate input
  std::string validationError;
  if (!validateInstanceInfo(info, validationError)) {
    if (error) {
      *error = "Validation failed: " + validationError;
    }
    return Json::Value(Json::objectValue); // Return empty object on error
  }

  // Store InstanceId
  config["InstanceId"] = info.instanceId;

  // Store InstanceName
  if (!info.instanceName.empty()) {
    config["InstanceName"] = info.instanceName;
  }

  // Store DisplayName
  if (!info.displayName.empty()) {
    config["DisplayName"] = info.displayName;
  }

  // Store Solution
  if (!info.solutionId.empty()) {
    config["Solution"] = info.solutionId;
  }

  // Store SolutionName (if available)
  if (!info.solutionName.empty()) {
    config["SolutionName"] = info.solutionName;
  }

  // Store Group (if available)
  if (!info.group.empty()) {
    config["Group"] = info.group;
  }

  // Store ReadOnly
  config["ReadOnly"] = info.readOnly;

  // Store SystemInstance
  config["SystemInstance"] = info.systemInstance;

  // Store AutoStart
  config["AutoStart"] = info.autoStart;

  // Store AutoRestart
  config["AutoRestart"] = info.autoRestart;

  // Store per-instance logging (API path: logging.enabled)
  config["logging"] = Json::objectValue;
  config["logging"]["enabled"] = info.instanceLoggingEnabled;

  // Store Input configuration (always include)
  Json::Value input(Json::objectValue);

  // Input media_format
  Json::Value mediaFormat(Json::objectValue);
  mediaFormat["color_format"] = 0;
  mediaFormat["default_format"] = true;
  mediaFormat["height"] = 0;
  mediaFormat["is_software"] = false;
  mediaFormat["name"] = "Same as Source";
  input["media_format"] = mediaFormat;

  // Input media_type and uri
  if (!info.rtspUrl.empty()) {
    input["media_type"] = "IP Camera";

    bool useUrisourcebin = false;
    auto urisourcebinIt = info.additionalParams.find("USE_URISOURCEBIN");
    if (urisourcebinIt != info.additionalParams.end()) {
      useUrisourcebin =
          (urisourcebinIt->second == "true" || urisourcebinIt->second == "1");
    }

    std::string decoderName = "avdec_h264";
    auto decoderIt = info.additionalParams.find("GST_DECODER_NAME");
    if (decoderIt != info.additionalParams.end() &&
        !decoderIt->second.empty()) {
      decoderName = decoderIt->second;
      if (decoderName == "decodebin") {
        useUrisourcebin = true;
      }
    }

    if (useUrisourcebin) {
      input["uri"] = "gstreamer:///urisourcebin uri=" + info.rtspUrl +
                     " ! decodebin ! videoconvert ! video/x-raw, format=NV12 ! "
                     "appsink drop=true name=cvdsink";
    } else {
      std::string protocolsParam = "";
      auto rtspTransportIt = info.additionalParams.find("RTSP_TRANSPORT");
      if (rtspTransportIt != info.additionalParams.end() &&
          !rtspTransportIt->second.empty()) {
        std::string transport = rtspTransportIt->second;
        std::transform(transport.begin(), transport.end(), transport.begin(),
                       ::tolower);
        if (transport == "tcp" || transport == "udp") {
          protocolsParam = " protocols=" + transport;
        }
      }
      input["uri"] =
          "rtspsrc location=" + info.rtspUrl + protocolsParam +
          " ! application/x-rtp,media=video ! rtph264depay ! h264parse ! " +
          decoderName +
          " ! videoconvert ! video/x-raw,format=NV12 ! appsink drop=true "
          "name=cvdsink";
    }
  } else if (!info.filePath.empty()) {
    input["media_type"] = "File";
    input["uri"] = info.filePath;
  } else {
    input["media_type"] = "IP Camera";
    input["uri"] = "";
  }

  config["Input"] = input;

  // Store Output configuration (always include)
  Json::Value output(Json::objectValue);
  output["JSONExport"]["enabled"] = info.metadataMode;
  output["NXWitness"]["enabled"] = false;

  // Output handlers (RTSP output if available)
  Json::Value handlers(Json::objectValue);
  if (!info.rtspUrl.empty() || !info.rtmpUrl.empty()) {
    Json::Value rtspHandler(Json::objectValue);
    Json::Value handlerConfig(Json::objectValue);
    handlerConfig["debug"] = info.debugMode ? "4" : "0";
    handlerConfig["fps"] = info.configuredFps > 0 ? info.configuredFps : 5;
    handlerConfig["pipeline"] =
        "( appsrc name=cvedia-rt ! videoconvert ! videoscale ! x264enc ! "
        "video/x-h264,profile=high ! rtph264pay name=pay0 pt=96 )";
    rtspHandler["config"] = handlerConfig;
    rtspHandler["enabled"] = info.running;
    rtspHandler["sink"] = "output-image";

    std::string outputUrl = info.rtspUrl;
    if (outputUrl.empty() && !info.rtmpUrl.empty()) {
      outputUrl = "rtsp://0.0.0.0:8554/stream1";
    }
    if (outputUrl.empty()) {
      outputUrl = "rtsp://0.0.0.0:8554/stream1";
    }
    rtspHandler["uri"] = outputUrl;
    handlers["rtsp:--0.0.0.0:8554-stream1"] = rtspHandler;
  }
  output["handlers"] = handlers;
  output["render_preset"] = "Default";
  config["Output"] = output;

  // Store OriginatorInfo
  if (!info.originator.address.empty()) {
    Json::Value originator(Json::objectValue);
    originator["address"] = info.originator.address;
    config["OriginatorInfo"] = originator;
  }

  // Store SolutionManager settings
  Json::Value solutionManager(Json::objectValue);
  solutionManager["frame_rate_limit"] =
      info.configuredFps > 0 ? info.configuredFps : info.frameRateLimit;
  solutionManager["send_metadata"] = info.metadataMode;
  solutionManager["run_statistics"] = info.statisticsMode;
  solutionManager["send_diagnostics"] = info.diagnosticsMode;
  solutionManager["enable_debug"] = info.debugMode;
  if (info.inputPixelLimit > 0) {
    solutionManager["input_pixel_limit"] = info.inputPixelLimit;
  }
  if (info.recommendedFrameRate > 0) {
    solutionManager["recommended_frame_rate"] = info.recommendedFrameRate;
  }
  config["SolutionManager"] = solutionManager;

  // Store Detector settings (always include, with defaults if needed)
  Json::Value detector(Json::objectValue);
  detector["animal_confidence_threshold"] = info.animalConfidenceThreshold > 0.0
                                                ? info.animalConfidenceThreshold
                                                : 0.3;
  detector["conf_threshold"] =
      info.confThreshold > 0.0 ? info.confThreshold : 0.2;
  detector["current_preset"] =
      info.detectorMode.empty() ? "FullRegionInference" : info.detectorMode;
  detector["current_sensitivity_preset"] =
      info.detectionSensitivity.empty() ? "High" : info.detectionSensitivity;
  detector["face_confidence_threshold"] =
      info.faceConfidenceThreshold > 0.0 ? info.faceConfidenceThreshold : 0.1;
  detector["license_plate_confidence_threshold"] =
      info.licensePlateConfidenceThreshold > 0.0
          ? info.licensePlateConfidenceThreshold
          : 0.1;
  detector["model_file"] = info.detectorModelFile.empty()
                               ? "pva_det_full_frame_512"
                               : info.detectorModelFile;
  detector["person_confidence_threshold"] = info.personConfidenceThreshold > 0.0
                                                ? info.personConfidenceThreshold
                                                : 0.3;
  detector["vehicle_confidence_threshold"] =
      info.vehicleConfidenceThreshold > 0.0 ? info.vehicleConfidenceThreshold
                                            : 0.3;

  // Preset values
  Json::Value presetValues(Json::objectValue);
  Json::Value mosaicInference(Json::objectValue);
  mosaicInference["Detector/model_file"] = "pva_det_mosaic_320";
  presetValues["MosaicInference"] = mosaicInference;
  detector["preset_values"] = presetValues;

  config["Detector"] = detector;

  // DetectorRegions (always include, empty by default)
  config["DetectorRegions"] = Json::Value(Json::objectValue);

  // Store DetectorThermal settings (always include)
  Json::Value detectorThermal(Json::objectValue);
  detectorThermal["model_file"] = info.detectorThermalModelFile.empty()
                                      ? "pva_det_mosaic_320"
                                      : info.detectorThermalModelFile;
  config["DetectorThermal"] = detectorThermal;

  // Store PerformanceMode (always include)
  Json::Value performanceMode(Json::objectValue);
  performanceMode["current_preset"] =
      info.performanceMode.empty() ? "Balanced" : info.performanceMode;
  config["PerformanceMode"] = performanceMode;

  // Store Tripwire (always include, empty by default)
  Json::Value tripwire(Json::objectValue);
  tripwire["Tripwires"] = Json::Value(Json::objectValue);
  config["Tripwire"] = tripwire;

  // Store Zone (always include, empty by default)
  Json::Value zone(Json::objectValue);
  zone["Zones"] = Json::Value(Json::objectValue);
  config["Zone"] = zone;

  // Store additional parameters as nested config
  if (!info.additionalParams.empty()) {
    std::cerr << "[InstanceStorage] Converting " << info.additionalParams.size()
              << " additionalParams to config JSON" << std::endl;
    for (const auto &pair : info.additionalParams) {
      // Skip internal flags
      if (pair.first == "__REPLACE_INPUT_OUTPUT_PARAMS__") {
        continue;
      }

      // Store model paths and other configs
      if (!config.isMember("AdditionalParams")) {
        config["AdditionalParams"] = Json::Value(Json::objectValue);
      }
      std::cerr << "[InstanceStorage] Adding to config: " << pair.first
                << " = " << pair.second << std::endl;
      config["AdditionalParams"][pair.first] = pair.second;
    }
  }

  // Store runtime info (not persisted, but included for completeness)
  config["loaded"] = info.loaded;
  config["running"] = info.running;
  config["fps"] = info.fps;
  config["version"] = info.version;

  // Validate output config
  std::string configError;
  if (!validateConfigJson(config, configError)) {
    if (error) {
      *error = "Generated config validation failed: " + configError;
    }
    return Json::Value(Json::objectValue);
  }

  return config;
}

std::optional<InstanceInfo>
InstanceStorage::configJsonToInstanceInfo(const Json::Value &config,
                                          std::string *error) const {
  try {
    // Validate input config
    std::string validationError;
    if (!validateConfigJson(config, validationError)) {
      if (error) {
        *error = "Config validation failed: " + validationError;
      }
      return std::nullopt;
    }

    InstanceInfo info;

    // Extract InstanceId
    if (config.isMember("InstanceId") && config["InstanceId"].isString()) {
      info.instanceId = config["InstanceId"].asString();
    } else {
      if (error) {
        *error = "InstanceId is required but missing or invalid";
      }
      return std::nullopt; // InstanceId is required
    }

    // Extract InstanceName
    if (config.isMember("InstanceName") && config["InstanceName"].isString()) {
      info.instanceName = config["InstanceName"].asString();
    }

    // Extract DisplayName
    if (config.isMember("DisplayName") && config["DisplayName"].isString()) {
      info.displayName = config["DisplayName"].asString();
    }

    // Fallback logic for legacy configs without InstanceName
    if (info.instanceName.empty()) {
      info.instanceName = info.displayName.empty() ? info.instanceId : info.displayName;
    }

    // Extract Solution
    if (config.isMember("Solution") && config["Solution"].isString()) {
      info.solutionId = config["Solution"].asString();
    }

    // Extract SolutionName
    if (config.isMember("SolutionName") && config["SolutionName"].isString()) {
      info.solutionName = config["SolutionName"].asString();
    }

    // Extract Group
    if (config.isMember("Group") && config["Group"].isString()) {
      info.group = config["Group"].asString();
    }

    // Extract ReadOnly
    if (config.isMember("ReadOnly") && config["ReadOnly"].isBool()) {
      info.readOnly = config["ReadOnly"].asBool();
    }

    // Extract SystemInstance
    if (config.isMember("SystemInstance") &&
        config["SystemInstance"].isBool()) {
      info.systemInstance = config["SystemInstance"].asBool();
    }

    // Extract AutoStart
    if (config.isMember("AutoStart") && config["AutoStart"].isBool()) {
      info.autoStart = config["AutoStart"].asBool();
    }

    // Extract AutoRestart
    if (config.isMember("AutoRestart") && config["AutoRestart"].isBool()) {
      info.autoRestart = config["AutoRestart"].asBool();
    }

    // Extract per-instance logging (API path: logging.enabled)
    if (config.isMember("logging") && config["logging"].isObject() &&
        config["logging"].isMember("enabled") &&
        config["logging"]["enabled"].isBool()) {
      info.instanceLoggingEnabled = config["logging"]["enabled"].asBool();
    }

    // Extract Input configuration
    if (config.isMember("Input") && config["Input"].isObject()) {
      const Json::Value &input = config["Input"];

      // Extract URI
      if (input.isMember("uri") && input["uri"].isString()) {
        std::string uri = input["uri"].asString();
        // Parse RTSP URL from GStreamer URI - support both old format (uri=)
        // and new format (location=)
        size_t rtspPos = uri.find("location=");
        if (rtspPos != std::string::npos) {
          // New format: rtspsrc location=...
          size_t start = rtspPos + 9;
          size_t end = uri.find(" ", start);
          if (end == std::string::npos) {
            end = uri.find(" !", start);
          }
          if (end == std::string::npos) {
            end = uri.length();
          }
          info.rtspUrl = uri.substr(start, end - start);
        } else {
          // Old format: gstreamer:///urisourcebin uri=...
          rtspPos = uri.find("uri=");
          if (rtspPos != std::string::npos) {
            size_t start = rtspPos + 4;
            size_t end = uri.find(" !", start);
            if (end == std::string::npos) {
              end = uri.length();
            }
            info.rtspUrl = uri.substr(start, end - start);
          } else if (uri.find("://") == std::string::npos) {
            // Direct file path (no protocol)
            info.filePath = uri;
          } else {
            // URL with protocol
            info.filePath = uri;
          }
        }
      }

      // Extract media_type
      if (input.isMember("media_type") && input["media_type"].isString()) {
        std::string mediaType = input["media_type"].asString();
        if (mediaType == "File" && info.filePath.empty() &&
            input.isMember("uri")) {
          info.filePath = input["uri"].asString();
        }
      }
    }

    // Extract RTMP URL from Output section if available
    if (config.isMember("Output") && config["Output"].isObject()) {
      const Json::Value &output = config["Output"];
      if (output.isMember("rtmpUrl") && output["rtmpUrl"].isString()) {
        info.rtmpUrl = output["rtmpUrl"].asString();
      }
    }

    // Extract OriginatorInfo
    if (config.isMember("OriginatorInfo") &&
        config["OriginatorInfo"].isObject()) {
      const Json::Value &originator = config["OriginatorInfo"];
      if (originator.isMember("address") && originator["address"].isString()) {
        info.originator.address = originator["address"].asString();
      }
    }

    // Extract SolutionManager settings
    if (config.isMember("SolutionManager") &&
        config["SolutionManager"].isObject()) {
      const Json::Value &sm = config["SolutionManager"];
      if (sm.isMember("frame_rate_limit") && sm["frame_rate_limit"].isInt()) {
        info.frameRateLimit = sm["frame_rate_limit"].asInt();
      }
      if (sm.isMember("send_metadata") && sm["send_metadata"].isBool()) {
        info.metadataMode = sm["send_metadata"].asBool();
      }
      if (sm.isMember("run_statistics") && sm["run_statistics"].isBool()) {
        info.statisticsMode = sm["run_statistics"].asBool();
      }
      if (sm.isMember("send_diagnostics") && sm["send_diagnostics"].isBool()) {
        info.diagnosticsMode = sm["send_diagnostics"].asBool();
      }
      if (sm.isMember("enable_debug") && sm["enable_debug"].isBool()) {
        info.debugMode = sm["enable_debug"].asBool();
      }
      if (sm.isMember("input_pixel_limit") && sm["input_pixel_limit"].isInt()) {
        info.inputPixelLimit = sm["input_pixel_limit"].asInt();
      }
      if (sm.isMember("recommended_frame_rate") &&
          sm["recommended_frame_rate"].isInt()) {
        info.recommendedFrameRate = sm["recommended_frame_rate"].asInt();
      }
    }

    // Extract Detector settings
    if (config.isMember("Detector") && config["Detector"].isObject()) {
      const Json::Value &detector = config["Detector"];
      if (detector.isMember("current_preset") &&
          detector["current_preset"].isString()) {
        info.detectorMode = detector["current_preset"].asString();
      }
      if (detector.isMember("current_sensitivity_preset") &&
          detector["current_sensitivity_preset"].isString()) {
        info.detectionSensitivity =
            detector["current_sensitivity_preset"].asString();
      }
      if (detector.isMember("model_file") &&
          detector["model_file"].isString()) {
        info.detectorModelFile = detector["model_file"].asString();
      }
      if (detector.isMember("animal_confidence_threshold") &&
          detector["animal_confidence_threshold"].isNumeric()) {
        info.animalConfidenceThreshold =
            detector["animal_confidence_threshold"].asDouble();
      }
      if (detector.isMember("person_confidence_threshold") &&
          detector["person_confidence_threshold"].isNumeric()) {
        info.personConfidenceThreshold =
            detector["person_confidence_threshold"].asDouble();
      }
      if (detector.isMember("vehicle_confidence_threshold") &&
          detector["vehicle_confidence_threshold"].isNumeric()) {
        info.vehicleConfidenceThreshold =
            detector["vehicle_confidence_threshold"].asDouble();
      }
      if (detector.isMember("face_confidence_threshold") &&
          detector["face_confidence_threshold"].isNumeric()) {
        info.faceConfidenceThreshold =
            detector["face_confidence_threshold"].asDouble();
      }
      if (detector.isMember("license_plate_confidence_threshold") &&
          detector["license_plate_confidence_threshold"].isNumeric()) {
        info.licensePlateConfidenceThreshold =
            detector["license_plate_confidence_threshold"].asDouble();
      }
      if (detector.isMember("conf_threshold") &&
          detector["conf_threshold"].isNumeric()) {
        info.confThreshold = detector["conf_threshold"].asDouble();
      }
    }

    // Extract DetectorThermal settings
    if (config.isMember("DetectorThermal") &&
        config["DetectorThermal"].isObject()) {
      const Json::Value &detectorThermal = config["DetectorThermal"];
      if (detectorThermal.isMember("model_file") &&
          detectorThermal["model_file"].isString()) {
        info.detectorThermalModelFile =
            detectorThermal["model_file"].asString();
      }
    }

    // Extract PerformanceMode
    if (config.isMember("PerformanceMode") &&
        config["PerformanceMode"].isObject()) {
      const Json::Value &performanceMode = config["PerformanceMode"];
      if (performanceMode.isMember("current_preset") &&
          performanceMode["current_preset"].isString()) {
        info.performanceMode = performanceMode["current_preset"].asString();
      }
    }

    // Extract Movement settings
    if (config.isMember("Movement") && config["Movement"].isObject()) {
      const Json::Value &movement = config["Movement"];
      if (movement.isMember("current_sensitivity_preset") &&
          movement["current_sensitivity_preset"].isString()) {
        info.movementSensitivity =
            movement["current_sensitivity_preset"].asString();
      }
    }

    // Extract AdditionalParams
    if (config.isMember("AdditionalParams") &&
        config["AdditionalParams"].isObject()) {
      const Json::Value &additionalParams = config["AdditionalParams"];
      for (const auto &key : additionalParams.getMemberNames()) {
        if (additionalParams[key].isString()) {
          info.additionalParams[key] = additionalParams[key].asString();

          // Extract RTSP_URL from additionalParams if not already set
          if (key == "RTSP_URL" && info.rtspUrl.empty()) {
            info.rtspUrl = additionalParams[key].asString();
          }

          // Extract FILE_PATH from additionalParams if not already set
          if (key == "FILE_PATH" && info.filePath.empty()) {
            info.filePath = additionalParams[key].asString();
          }

          // Extract RTMP output URL: prefer RTMP_DES_URL (output), else RTMP_URL if still unset
          if (key == "RTMP_DES_URL") {
            info.rtmpUrl = additionalParams[key].asString();
          } else if (key == "RTMP_URL" && info.rtmpUrl.empty()) {
            info.rtmpUrl = additionalParams[key].asString();
          }
        }
      }
    }

    // Extract runtime info
    if (config.isMember("loaded") && config["loaded"].isBool()) {
      info.loaded = config["loaded"].asBool();
    }
    if (config.isMember("running") && config["running"].isBool()) {
      info.running = config["running"].asBool();
    }
    if (config.isMember("fps") && config["fps"].isNumeric()) {
      info.fps = config["fps"].asDouble();
    }
    if (config.isMember("version") && config["version"].isString()) {
      info.version = config["version"].asString();
    }

    // Set defaults
    info.persistent = true; // All instances in database are persistent
    info.loaded = true;
    info.running = false; // Will be set when started

    // Validate output InstanceInfo
    std::string infoError;
    if (!validateInstanceInfo(info, infoError)) {
      if (error) {
        *error = "Converted InstanceInfo validation failed: " + infoError;
      }
      return std::nullopt;
    }

    return info;
  } catch (const std::exception &e) {
    if (error) {
      *error = "Exception during conversion: " + std::string(e.what());
    }
    return std::nullopt;
  } catch (...) {
    if (error) {
      *error = "Unknown exception during conversion";
    }
    return std::nullopt;
  }
}
