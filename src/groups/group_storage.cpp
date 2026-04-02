#include "groups/group_storage.h"
#include "core/env_config.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <set>
#include <sstream>
#include <sqlite3.h>
#include <vector>

namespace fs = std::filesystem;

// ============================================================================
// Constructor
// ============================================================================

GroupStorage::GroupStorage(sqlite3 *db, const std::string &legacy_storage_dir)
    : db_(db), legacy_storage_dir_(legacy_storage_dir) {
  if (!db_) {
    std::cerr << "[GroupStorage] WARNING: Database handle is null" << std::endl;
    return;
  }

  if (!createTable()) {
    std::cerr << "[GroupStorage] Error creating groups table" << std::endl;
    return;
  }

  // Auto-migrate from JSON files if legacy directory is provided
  if (!legacy_storage_dir_.empty()) {
    migrateFromJsonFiles();
  }

  std::cerr << "[GroupStorage] ✓ GroupStorage ready (SQLite backend)"
            << std::endl;
}

// ============================================================================
// Table Creation
// ============================================================================

bool GroupStorage::createTable() {
  if (!db_)
    return false;

  const char *sql = R"(
    CREATE TABLE IF NOT EXISTS groups (
      group_id        TEXT PRIMARY KEY,
      group_name      TEXT NOT NULL,
      description     TEXT DEFAULT '',
      is_default      INTEGER DEFAULT 0,
      read_only       INTEGER DEFAULT 0,
      instance_count  INTEGER DEFAULT 0,
      created_at      TEXT DEFAULT (datetime('now')),
      updated_at      TEXT DEFAULT (datetime('now'))
    );
  )";

  char *errmsg = nullptr;
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    std::cerr << "[GroupStorage] Error creating table: "
              << (errmsg ? errmsg : "unknown") << std::endl;
    if (errmsg)
      sqlite3_free(errmsg);
    return false;
  }
  return true;
}

// ============================================================================
// Migration from JSON files
// ============================================================================

void GroupStorage::migrateFromJsonFiles() {
  if (legacy_storage_dir_.empty())
    return;

  // Check all possible directories for legacy JSON files
  std::filesystem::path path(legacy_storage_dir_);
  std::string subdir = path.filename().string();
  if (subdir.empty())
    subdir = "groups";

  std::vector<std::string> allDirs =
      EnvConfig::getAllPossibleDirectories(subdir);

  // Also include the legacy_storage_dir_ itself
  allDirs.insert(allDirs.begin(), legacy_storage_dir_);

  int migrated = 0;
  int failed = 0;

  for (const auto &dir : allDirs) {
    if (!fs::exists(dir) || !fs::is_directory(dir))
      continue;

    try {
      for (const auto &entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
          continue;

        std::string filename = entry.path().filename().string();
        std::string groupId =
            filename.substr(0, filename.length() - 5); // Remove .json

        // Check if already migrated
        if (groupFileExists(groupId))
          continue;

        try {
          std::ifstream file(entry.path());
          if (!file.is_open())
            continue;

          Json::CharReaderBuilder builder;
          Json::Value json;
          std::string errors;
          if (!Json::parseFromStream(builder, file, &json, &errors)) {
            std::cerr
                << "[GroupStorage] Migration: failed to parse " << filename
                << ": " << errors << std::endl;
            failed++;
            continue;
          }
          file.close();

          std::string error;
          auto group = jsonToGroupInfo(json, &error);
          if (!group.has_value()) {
            std::cerr << "[GroupStorage] Migration: invalid group " << filename
                      << ": " << error << std::endl;
            failed++;
            continue;
          }

          if (saveGroup(group.value())) {
            migrated++;

            // Rename original file as backup
            std::string backupPath = entry.path().string() + ".migrated";
            try {
              fs::rename(entry.path(), backupPath);
            } catch (const std::exception &e) {
              std::cerr
                  << "[GroupStorage] Warning: Could not rename JSON file: "
                  << e.what() << std::endl;
            }
          } else {
            failed++;
          }
        } catch (const std::exception &e) {
          std::cerr << "[GroupStorage] Migration exception for " << filename
                    << ": " << e.what() << std::endl;
          failed++;
        }
      }
    } catch (const std::exception &e) {
      // Skip directory if iteration fails
      continue;
    }
  }

  if (migrated > 0 || failed > 0) {
    std::cerr << "[GroupStorage] ✓ JSON migration complete: " << migrated
              << " migrated, " << failed << " failed" << std::endl;
  }
}

// ============================================================================
// CRUD Operations
// ============================================================================

bool GroupStorage::saveGroup(const GroupInfo &group) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return false;

  try {
    const char *upsertSql =
        "INSERT OR REPLACE INTO groups "
        "(group_id, group_name, description, is_default, read_only, "
        "instance_count, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, datetime('now'))";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, upsertSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      std::cerr << "[GroupStorage] Prepare error: " << sqlite3_errmsg(db_)
                << std::endl;
      return false;
    }

    sqlite3_bind_text(stmt, 1, group.groupId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, group.groupName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, group.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, group.isDefault ? 1 : 0);
    sqlite3_bind_int(stmt, 5, group.readOnly ? 1 : 0);
    sqlite3_bind_int(stmt, 6, group.instanceCount);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
      std::cerr << "[GroupStorage] Insert error: " << sqlite3_errmsg(db_)
                << std::endl;
      return false;
    }

    std::cerr << "[GroupStorage] ✓ Saved group: " << group.groupId << std::endl;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[GroupStorage] Exception in saveGroup: " << e.what()
              << std::endl;
    return false;
  }
}

std::optional<GroupInfo> GroupStorage::loadGroup(const std::string &groupId) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return std::nullopt;

  try {
    const char *selectSql =
        "SELECT group_id, group_name, description, is_default, read_only, "
        "instance_count, created_at, updated_at "
        "FROM groups WHERE group_id = ?";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
      return std::nullopt;

    sqlite3_bind_text(stmt, 1, groupId.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
      sqlite3_finalize(stmt);
      return std::nullopt;
    }

    GroupInfo group;
    const char *text;

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    group.groupId = text ? text : "";

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    group.groupName = text ? text : "";

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    group.description = text ? text : "";

    group.isDefault = sqlite3_column_int(stmt, 3) != 0;
    group.readOnly = sqlite3_column_int(stmt, 4) != 0;
    group.instanceCount = sqlite3_column_int(stmt, 5);

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
    group.createdAt = text ? text : "";

    text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
    group.updatedAt = text ? text : "";

    sqlite3_finalize(stmt);
    return group;
  } catch (const std::exception &e) {
    std::cerr << "[GroupStorage] Exception in loadGroup: " << e.what()
              << std::endl;
    return std::nullopt;
  }
}

std::vector<GroupInfo> GroupStorage::loadAllGroups() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<GroupInfo> groups;

  if (!db_)
    return groups;

  try {
    const char *selectSql =
        "SELECT group_id, group_name, description, is_default, read_only, "
        "instance_count, created_at, updated_at FROM groups";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
      return groups;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      GroupInfo group;
      const char *text;

      text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
      group.groupId = text ? text : "";

      text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
      group.groupName = text ? text : "";

      text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
      group.description = text ? text : "";

      group.isDefault = sqlite3_column_int(stmt, 3) != 0;
      group.readOnly = sqlite3_column_int(stmt, 4) != 0;
      group.instanceCount = sqlite3_column_int(stmt, 5);

      text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
      group.createdAt = text ? text : "";

      text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
      group.updatedAt = text ? text : "";

      groups.push_back(group);
    }
    sqlite3_finalize(stmt);
  } catch (const std::exception &e) {
    std::cerr << "[GroupStorage] Exception in loadAllGroups: " << e.what()
              << std::endl;
  }

  return groups;
}

bool GroupStorage::deleteGroup(const std::string &groupId) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return false;

  try {
    const char *deleteSql = "DELETE FROM groups WHERE group_id = ?";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, deleteSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
      return false;

    sqlite3_bind_text(stmt, 1, groupId.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
      return false;

    int changes = sqlite3_changes(db_);
    if (changes > 0) {
      std::cerr << "[GroupStorage] ✓ Deleted group: " << groupId << std::endl;
    }
    return changes > 0;
  } catch (const std::exception &e) {
    std::cerr << "[GroupStorage] Exception in deleteGroup: " << e.what()
              << std::endl;
    return false;
  }
}

bool GroupStorage::groupFileExists(const std::string &groupId) const {
  if (!db_)
    return false;

  try {
    const char *selectSql =
        "SELECT 1 FROM groups WHERE group_id = ? LIMIT 1";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
      return false;

    sqlite3_bind_text(stmt, 1, groupId.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW;
  } catch (...) {
    return false;
  }
}

// ============================================================================
// JSON Conversion (unchanged from original)
// ============================================================================

Json::Value GroupStorage::groupInfoToJson(const GroupInfo &group,
                                          std::string *error) const {
  Json::Value json(Json::objectValue);

  json["groupId"] = group.groupId;
  json["groupName"] = group.groupName;
  json["description"] = group.description;
  json["isDefault"] = group.isDefault;
  json["readOnly"] = group.readOnly;
  json["instanceCount"] = group.instanceCount;
  json["createdAt"] = group.createdAt;
  json["updatedAt"] = group.updatedAt;

  return json;
}

std::optional<GroupInfo>
GroupStorage::jsonToGroupInfo(const Json::Value &json,
                              std::string *error) const {
  try {
    GroupInfo group;

    if (!json.isMember("groupId") || !json["groupId"].isString()) {
      if (error)
        *error = "Missing or invalid groupId";
      return std::nullopt;
    }
    group.groupId = json["groupId"].asString();

    if (!json.isMember("groupName") || !json["groupName"].isString()) {
      if (error)
        *error = "Missing or invalid groupName";
      return std::nullopt;
    }
    group.groupName = json["groupName"].asString();

    if (json.isMember("description") && json["description"].isString()) {
      group.description = json["description"].asString();
    }

    if (json.isMember("isDefault") && json["isDefault"].isBool()) {
      group.isDefault = json["isDefault"].asBool();
    }

    if (json.isMember("readOnly") && json["readOnly"].isBool()) {
      group.readOnly = json["readOnly"].asBool();
    }

    if (json.isMember("instanceCount") && json["instanceCount"].isInt()) {
      group.instanceCount = json["instanceCount"].asInt();
    }

    if (json.isMember("createdAt") && json["createdAt"].isString()) {
      group.createdAt = json["createdAt"].asString();
    }

    if (json.isMember("updatedAt") && json["updatedAt"].isString()) {
      group.updatedAt = json["updatedAt"].asString();
    }

    // Validate
    std::string validationError;
    if (!group.validate(validationError)) {
      if (error)
        *error = validationError;
      return std::nullopt;
    }

    return group;
  } catch (const std::exception &e) {
    if (error)
      *error = e.what();
    return std::nullopt;
  }
}
