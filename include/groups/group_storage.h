#pragma once

#include "models/group_info.h"
#include <json/json.h>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// Forward declaration for SQLite
struct sqlite3;

/**
 * @brief Group Storage
 *
 * Handles persistent storage of groups using SQLite embedded database.
 * Groups are stored in the shared OmniAPI database (omniapi.db).
 *
 * Migration: On first startup, if individual group JSON files exist in the
 * legacy storage directory, they are automatically migrated to the SQLite
 * database and renamed to *.json.migrated as backup.
 */
class GroupStorage {
public:
  /**
   * @brief Constructor
   * @param db SQLite database handle (from OmniDatabase)
   * @param legacy_storage_dir Legacy directory for JSON migration (optional)
   */
  explicit GroupStorage(sqlite3 *db,
                        const std::string &legacy_storage_dir = "");

  /**
   * @brief Save a group to database
   * @param group Group information
   * @return true if successful
   */
  bool saveGroup(const GroupInfo &group);

  /**
   * @brief Load a group from database
   * @param groupId Group ID
   * @return GroupInfo if found, nullopt otherwise
   */
  std::optional<GroupInfo> loadGroup(const std::string &groupId);

  /**
   * @brief Load all groups from database
   * @return Vector of GroupInfo
   */
  std::vector<GroupInfo> loadAllGroups();

  /**
   * @brief Delete a group from database
   * @param groupId Group ID
   * @return true if successful
   */
  bool deleteGroup(const std::string &groupId);

  /**
   * @brief Check if group exists in database
   * @param groupId Group ID
   * @return true if exists
   */
  bool groupFileExists(const std::string &groupId) const;

  /**
   * @brief Convert GroupInfo to JSON
   * @param group Group information
   * @param error Optional error message output
   * @return JSON value
   */
  Json::Value groupInfoToJson(const GroupInfo &group,
                              std::string *error = nullptr) const;

  /**
   * @brief Convert JSON to GroupInfo
   * @param json JSON value
   * @param error Optional error message output
   * @return GroupInfo if valid, nullopt otherwise
   */
  std::optional<GroupInfo> jsonToGroupInfo(const Json::Value &json,
                                           std::string *error = nullptr) const;

  /**
   * @brief Create the groups table if it doesn't exist
   * @return true if successful
   */
  bool createTable();

private:
  sqlite3 *db_;
  std::string legacy_storage_dir_;
  mutable std::mutex mutex_;

  /**
   * @brief Migrate existing group JSON files to SQLite
   * Called automatically on first startup if legacy directory is provided.
   */
  void migrateFromJsonFiles();
};
