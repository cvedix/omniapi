#pragma once

#include "models/solution_config.h"
#include <json/json.h>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// Forward declaration for SQLite
struct sqlite3;

/**
 * @brief Solution Storage
 *
 * Handles persistent storage of custom solutions using SQLite embedded database.
 * Default solutions are not stored, only custom user-created solutions.
 *
 * Migration: On first startup, if a solutions.json file exists in the
 * legacy storage directory, all custom solutions are automatically migrated
 * to the SQLite database and the JSON file is renamed to
 * solutions.json.migrated.
 */
class SolutionStorage {
public:
  /**
   * @brief Constructor
   * @param db SQLite database handle (from OmniDatabase)
   * @param legacy_storage_dir Legacy directory for JSON migration (optional)
   */
  explicit SolutionStorage(sqlite3 *db,
                           const std::string &legacy_storage_dir = "");

  /**
   * @brief Save solution to database
   * @param config Solution configuration
   * @return true if successful
   */
  bool saveSolution(const SolutionConfig &config);

  /**
   * @brief Load solution from database
   * @param solutionId Solution ID
   * @return Solution config if found, empty optional otherwise
   */
  std::optional<SolutionConfig> loadSolution(const std::string &solutionId);

  /**
   * @brief Load all custom solutions from database
   * @return Vector of solution configs that were loaded
   */
  std::vector<SolutionConfig> loadAllSolutions();

  /**
   * @brief Delete solution from database
   * @param solutionId Solution ID
   * @return true if successful
   */
  bool deleteSolution(const std::string &solutionId);

  /**
   * @brief Check if solution exists in database
   * @param solutionId Solution ID
   * @return true if exists
   */
  bool solutionExists(const std::string &solutionId) const;

  /**
   * @brief Get storage directory path (legacy, for backward compatibility)
   */
  std::string getStorageDir() const { return legacy_storage_dir_; }

  /**
   * @brief Create the solutions table if it doesn't exist
   * @return true if successful
   */
  bool createTable();

private:
  sqlite3 *db_;
  std::string legacy_storage_dir_;
  mutable std::mutex mutex_;

  /**
   * @brief Migrate existing solutions.json to SQLite
   */
  void migrateFromJson();

  /**
   * @brief Convert SolutionConfig to JSON string for storage
   */
  Json::Value solutionConfigToJson(const SolutionConfig &config) const;

  /**
   * @brief Convert JSON to SolutionConfig
   */
  std::optional<SolutionConfig>
  jsonToSolutionConfig(const Json::Value &json) const;
};
