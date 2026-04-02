#include "solutions/solution_storage.h"
#include "core/env_config.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <sstream>
#include <sqlite3.h>

// ============================================================================
// Constructor
// ============================================================================

SolutionStorage::SolutionStorage(sqlite3 *db,
                                 const std::string &legacy_storage_dir)
    : db_(db), legacy_storage_dir_(legacy_storage_dir) {
  if (!db_) {
    std::cerr << "[SolutionStorage] WARNING: Database handle is null"
              << std::endl;
    return;
  }

  if (!createTable()) {
    std::cerr << "[SolutionStorage] Error creating solutions table" << std::endl;
    return;
  }

  // Auto-migrate from JSON file if legacy directory is provided
  if (!legacy_storage_dir_.empty()) {
    migrateFromJson();
  }

  std::cerr << "[SolutionStorage] ✓ SolutionStorage ready (SQLite backend)"
            << std::endl;
}

// ============================================================================
// Table Creation
// ============================================================================

bool SolutionStorage::createTable() {
  if (!db_)
    return false;

  const char *sql = R"(
    CREATE TABLE IF NOT EXISTS solutions (
      solution_id   TEXT PRIMARY KEY,
      solution_name TEXT NOT NULL,
      solution_type TEXT DEFAULT '',
      config_json   TEXT NOT NULL DEFAULT '{}',
      is_custom     INTEGER DEFAULT 1,
      created_at    TEXT DEFAULT (datetime('now')),
      updated_at    TEXT DEFAULT (datetime('now'))
    );
  )";

  char *errmsg = nullptr;
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    std::cerr << "[SolutionStorage] Error creating table: "
              << (errmsg ? errmsg : "unknown") << std::endl;
    if (errmsg)
      sqlite3_free(errmsg);
    return false;
  }
  return true;
}

// ============================================================================
// Migration from solutions.json
// ============================================================================

void SolutionStorage::migrateFromJson() {
  if (legacy_storage_dir_.empty())
    return;

  // Check all possible directories for legacy solutions.json
  std::filesystem::path path(legacy_storage_dir_);
  std::string subdir = path.filename().string();
  if (subdir.empty())
    subdir = "solutions";

  std::vector<std::string> allDirs =
      EnvConfig::getAllPossibleDirectories(subdir);
  allDirs.insert(allDirs.begin(), legacy_storage_dir_);

  for (const auto &dir : allDirs) {
    std::string jsonPath = dir + "/solutions.json";
    if (!std::filesystem::exists(jsonPath))
      continue;

    std::cerr << "[SolutionStorage] Found solutions.json, migrating: "
              << jsonPath << std::endl;

    try {
      std::ifstream file(jsonPath);
      if (!file.is_open())
        continue;

      Json::CharReaderBuilder builder;
      std::string errors;
      Json::Value root(Json::objectValue);
      if (!Json::parseFromStream(builder, file, &root, &errors)) {
        std::cerr << "[SolutionStorage] JSON parse error: " << errors
                  << std::endl;
        continue;
      }
      file.close();

      int migrated = 0;
      int failed = 0;

      for (const auto &key : root.getMemberNames()) {
        auto config = jsonToSolutionConfig(root[key]);
        if (!config.has_value()) {
          failed++;
          continue;
        }

        // Skip default solutions
        if (config->isDefault) {
          continue;
        }

        // Check if already exists
        if (solutionExists(config->solutionId))
          continue;

        if (saveSolution(config.value())) {
          migrated++;
        } else {
          failed++;
        }
      }

      if (migrated > 0) {
        std::cerr << "[SolutionStorage] ✓ Migrated " << migrated
                  << " solutions from JSON (" << failed << " failed)"
                  << std::endl;

        // Rename original file
        std::string backupPath = jsonPath + ".migrated";
        try {
          std::filesystem::rename(jsonPath, backupPath);
          std::cerr << "[SolutionStorage] ✓ Renamed " << jsonPath << " → "
                    << backupPath << std::endl;
        } catch (const std::exception &e) {
          std::cerr << "[SolutionStorage] Warning: Could not rename JSON: "
                    << e.what() << std::endl;
        }
      }

      break; // Only migrate from the first found file
    } catch (const std::exception &e) {
      std::cerr << "[SolutionStorage] Migration exception: " << e.what()
                << std::endl;
    }
  }
}

// ============================================================================
// CRUD Operations
// ============================================================================

bool SolutionStorage::saveSolution(const SolutionConfig &config) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return false;

  // SECURITY: Never save default solutions to storage
  if (config.isDefault) {
    std::cerr
        << "[SolutionStorage] Security: Attempted to save default solution '"
        << config.solutionId << "'. Ignoring." << std::endl;
    return false;
  }

  try {
    // Serialize full config to JSON string
    Json::Value configJson = solutionConfigToJson(config);
    configJson["isDefault"] = false; // Double protection
    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    std::string configStr = Json::writeString(writerBuilder, configJson);

    const char *upsertSql =
        "INSERT OR REPLACE INTO solutions "
        "(solution_id, solution_name, solution_type, config_json, is_custom, "
        "updated_at) "
        "VALUES (?, ?, ?, ?, 1, datetime('now'))";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, upsertSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      std::cerr << "[SolutionStorage] Prepare error: " << sqlite3_errmsg(db_)
                << std::endl;
      return false;
    }

    sqlite3_bind_text(stmt, 1, config.solutionId.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, config.solutionName.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, config.solutionType.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, configStr.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
      std::cerr << "[SolutionStorage] Insert error: " << sqlite3_errmsg(db_)
                << std::endl;
      return false;
    }

    std::cerr << "[SolutionStorage] ✓ Saved solution: " << config.solutionId
              << std::endl;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[SolutionStorage] Exception in saveSolution: " << e.what()
              << std::endl;
    return false;
  }
}

std::optional<SolutionConfig>
SolutionStorage::loadSolution(const std::string &solutionId) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return std::nullopt;

  try {
    const char *selectSql =
        "SELECT config_json FROM solutions WHERE solution_id = ?";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
      return std::nullopt;

    sqlite3_bind_text(stmt, 1, solutionId.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
      sqlite3_finalize(stmt);
      return std::nullopt;
    }

    const char *configText =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    std::string configStr = configText ? configText : "{}";
    sqlite3_finalize(stmt);

    // Parse JSON
    Json::CharReaderBuilder readerBuilder;
    std::string parseErrors;
    Json::Value config;
    std::istringstream stream(configStr);
    if (!Json::parseFromStream(readerBuilder, stream, &config, &parseErrors)) {
      return std::nullopt;
    }

    return jsonToSolutionConfig(config);
  } catch (const std::exception &e) {
    std::cerr << "[SolutionStorage] Exception in loadSolution: " << e.what()
              << std::endl;
    return std::nullopt;
  }
}

std::vector<SolutionConfig> SolutionStorage::loadAllSolutions() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<SolutionConfig> result;

  if (!db_)
    return result;

  try {
    const char *selectSql = "SELECT config_json FROM solutions WHERE is_custom = 1";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
      return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const char *configText =
          reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
      std::string configStr = configText ? configText : "{}";

      Json::CharReaderBuilder readerBuilder;
      std::string parseErrors;
      Json::Value config;
      std::istringstream stream(configStr);
      if (Json::parseFromStream(readerBuilder, stream, &config, &parseErrors)) {
        auto solutionConfig = jsonToSolutionConfig(config);
        if (solutionConfig.has_value()) {
          // SECURITY: Skip default solutions
          if (solutionConfig->isDefault) {
            continue;
          }
          solutionConfig->isDefault = false;
          result.push_back(solutionConfig.value());
        }
      }
    }
    sqlite3_finalize(stmt);
  } catch (const std::exception &e) {
    std::cerr << "[SolutionStorage] Exception in loadAllSolutions: " << e.what()
              << std::endl;
  }

  return result;
}

bool SolutionStorage::deleteSolution(const std::string &solutionId) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!db_)
    return false;

  try {
    const char *deleteSql = "DELETE FROM solutions WHERE solution_id = ?";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, deleteSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
      return false;

    sqlite3_bind_text(stmt, 1, solutionId.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
      return false;

    int changes = sqlite3_changes(db_);
    if (changes > 0) {
      std::cerr << "[SolutionStorage] ✓ Deleted solution: " << solutionId
                << std::endl;
    }
    return changes > 0;
  } catch (const std::exception &e) {
    std::cerr << "[SolutionStorage] Exception in deleteSolution: " << e.what()
              << std::endl;
    return false;
  }
}

bool SolutionStorage::solutionExists(const std::string &solutionId) const {
  if (!db_)
    return false;

  try {
    const char *selectSql =
        "SELECT 1 FROM solutions WHERE solution_id = ? LIMIT 1";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, selectSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
      return false;

    sqlite3_bind_text(stmt, 1, solutionId.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW;
  } catch (...) {
    return false;
  }
}

// ============================================================================
// JSON Conversion (preserved from original)
// ============================================================================

Json::Value
SolutionStorage::solutionConfigToJson(const SolutionConfig &config) const {
  Json::Value json(Json::objectValue);

  json["solutionId"] = config.solutionId;
  json["solutionName"] = config.solutionName;
  json["solutionType"] = config.solutionType;
  json["isDefault"] = config.isDefault;

  // Convert pipeline
  Json::Value pipeline(Json::arrayValue);
  for (const auto &node : config.pipeline) {
    Json::Value nodeJson(Json::objectValue);
    nodeJson["nodeType"] = node.nodeType;
    nodeJson["nodeName"] = node.nodeName;

    Json::Value params(Json::objectValue);
    for (const auto &param : node.parameters) {
      params[param.first] = param.second;
    }
    nodeJson["parameters"] = params;

    pipeline.append(nodeJson);
  }
  json["pipeline"] = pipeline;

  // Convert defaults
  Json::Value defaults(Json::objectValue);
  for (const auto &def : config.defaults) {
    defaults[def.first] = def.second;
  }
  json["defaults"] = defaults;

  return json;
}

std::optional<SolutionConfig>
SolutionStorage::jsonToSolutionConfig(const Json::Value &json) const {
  try {
    SolutionConfig config;

    if (!json.isMember("solutionId") || !json["solutionId"].isString()) {
      return std::nullopt;
    }
    config.solutionId = json["solutionId"].asString();

    if (!json.isMember("solutionName") || !json["solutionName"].isString()) {
      return std::nullopt;
    }
    config.solutionName = json["solutionName"].asString();

    if (!json.isMember("solutionType") || !json["solutionType"].isString()) {
      return std::nullopt;
    }
    config.solutionType = json["solutionType"].asString();

    // SECURITY: Always set isDefault to false when loading from storage
    config.isDefault = false;

    // Parse pipeline
    if (json.isMember("pipeline") && json["pipeline"].isArray()) {
      for (const auto &nodeJson : json["pipeline"]) {
        SolutionConfig::NodeConfig node;

        if (!nodeJson.isMember("nodeType") ||
            !nodeJson["nodeType"].isString()) {
          continue;
        }
        node.nodeType = nodeJson["nodeType"].asString();

        if (!nodeJson.isMember("nodeName") ||
            !nodeJson["nodeName"].isString()) {
          continue;
        }
        node.nodeName = nodeJson["nodeName"].asString();

        if (nodeJson.isMember("parameters") &&
            nodeJson["parameters"].isObject()) {
          for (const auto &key : nodeJson["parameters"].getMemberNames()) {
            if (nodeJson["parameters"][key].isString()) {
              node.parameters[key] = nodeJson["parameters"][key].asString();
            }
          }
        }

        config.pipeline.push_back(node);
      }
    }

    // Parse defaults
    if (json.isMember("defaults") && json["defaults"].isObject()) {
      for (const auto &key : json["defaults"].getMemberNames()) {
        if (json["defaults"][key].isString()) {
          config.defaults[key] = json["defaults"][key].asString();
        }
      }
    }

    return config;
  } catch (const std::exception &e) {
    std::cerr
        << "[SolutionStorage] Exception converting JSON to SolutionConfig: "
        << e.what() << std::endl;
    return std::nullopt;
  }
}
