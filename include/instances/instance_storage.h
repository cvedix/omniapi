#pragma once

#include "instances/instance_info.h"
#include <filesystem>
#include <json/json.h>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// Forward declaration for SQLite
struct sqlite3;
struct sqlite3_stmt;

/**
 * @brief Instance Storage
 *
 * Handles persistent storage of instances using SQLite embedded database.
 * Thread-safe via SQLite WAL mode and internal mutex.
 *
 * Supports two construction modes:
 * 1. Legacy: Takes a storage directory, manages its own SQLite database
 * 2. OmniDatabase: Takes a shared sqlite3* handle from OmniDatabase singleton
 *
 * Migration: On first startup, if an instances.json file exists in the storage
 * directory, all instances are automatically migrated to the SQLite database
 * and the JSON file is renamed to instances.json.migrated as a backup.
 */
class InstanceStorage {
public:
  /**
   * @brief Legacy constructor — manages its own SQLite database
   * @param storage_dir Directory to store instance database (default:
   * ./instances)
   */
  explicit InstanceStorage(const std::string &storage_dir = "./instances");

  /**
   * @brief OmniDatabase constructor — uses shared database handle
   * @param db SQLite database handle (from OmniDatabase)
   * @param legacy_storage_dir Legacy directory for JSON migration (optional)
   */
  explicit InstanceStorage(sqlite3 *db,
                           const std::string &legacy_storage_dir = "");

  /**
   * @brief Destructor - closes SQLite database
   */
  ~InstanceStorage();

  // Non-copyable
  InstanceStorage(const InstanceStorage &) = delete;
  InstanceStorage &operator=(const InstanceStorage &) = delete;

  /**
   * @brief Save instance to database
   * @param instanceId Instance ID
   * @param info Instance information
   * @return true if successful
   */
  bool saveInstance(const std::string &instanceId, const InstanceInfo &info);

  /**
   * @brief Load instance from database
   * @param instanceId Instance ID
   * @return Instance info if found, empty optional otherwise
   */
  std::optional<InstanceInfo> loadInstance(const std::string &instanceId);

  /**
   * @brief Load all instance IDs from database
   * @return Vector of instance IDs that were loaded
   */
  std::vector<std::string> loadAllInstances();

  /**
   * @brief Delete instance from database
   * @param instanceId Instance ID
   * @return true if successful
   */
  bool deleteInstance(const std::string &instanceId);

  /**
   * @brief Check if instance exists in database
   * @param instanceId Instance ID
   * @return true if exists
   */
  bool instanceExists(const std::string &instanceId) const;

  /**
   * @brief Get storage directory path
   */
  std::string getStorageDir() const { return storage_dir_; }

  /**
   * @brief Validate InstanceInfo before conversion
   * @param info InstanceInfo to validate
   * @param error Error message output
   * @return true if valid, false otherwise
   */
  bool validateInstanceInfo(const InstanceInfo &info, std::string &error) const;

  /**
   * @brief Validate JSON config object before conversion
   * @param config JSON config to validate
   * @param error Error message output
   * @return true if valid, false otherwise
   */
  bool validateConfigJson(const Json::Value &config, std::string &error) const;

  /**
   * @brief Merge new config into existing config, preserving complex nested
   * structures
   * @param existingConfig Existing config (will be modified)
   * @param newConfig New config to merge
   * @param preserveKeys List of keys to preserve from existing config (e.g.,
   * "TensorRT", "Zone", etc.)
   * @return true if merge successful
   */
  bool mergeConfigs(Json::Value &existingConfig, const Json::Value &newConfig,
                    const std::vector<std::string> &preserveKeys = {}) const;

  /**
   * @brief Convert InstanceInfo to JSON config object (new format)
   * @param info InstanceInfo to convert
   * @param error Optional error message output
   * @return JSON config object, or empty object on error
   */
  Json::Value instanceInfoToConfigJson(const InstanceInfo &info,
                                       std::string *error = nullptr) const;

  /**
   * @brief Convert JSON config object to InstanceInfo
   * @param config JSON config object to convert
   * @param error Optional error message output
   * @return InstanceInfo if successful, empty optional on error
   */
  std::optional<InstanceInfo>
  configJsonToInstanceInfo(const Json::Value &config,
                           std::string *error = nullptr) const;

private:
  std::string storage_dir_;
  std::string db_path_;
  sqlite3 *db_ = nullptr;
  bool owns_db_ = true; // true if we manage db lifecycle, false for OmniDatabase
  mutable std::mutex mutex_;

  /**
   * @brief Ensure storage directory exists (with fallback if needed)
   */
  void ensureStorageDir();

  /**
   * @brief Open SQLite database and create tables
   * @return true if successful
   */
  bool openDatabase();

  /**
   * @brief Close SQLite database
   */
  void closeDatabase();

  /**
   * @brief Create required tables if they don't exist
   * @return true if successful
   */
  bool createTables();

  /**
   * @brief Migrate existing instances.json to SQLite database
   * Called automatically on first startup if instances.json exists.
   */
  void migrateFromJson();

  /**
   * @brief Execute a simple SQL statement (no results)
   * @param sql SQL statement
   * @return true if successful
   */
  bool execSql(const char *sql);
};
