#include "core/system_config_manager.h"
#include "core/env_config.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

SystemConfigManager &SystemConfigManager::getInstance() {
  static SystemConfigManager instance;
  return instance;
}

bool SystemConfigManager::loadConfig(const std::string &configPath) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!configPath.empty()) {
    config_path_ = configPath;
  }

  // If no path specified, try to find config file
  if (config_path_.empty()) {
    // Try to find system config file in common locations
    std::vector<std::string> possiblePaths = {
        "./config/system_config.json",
        "/opt/omniapi/config/system_config.json",
        "/etc/omniapi/system_config.json"
    };

    for (const auto &path : possiblePaths) {
      if (std::filesystem::exists(path)) {
        config_path_ = path;
        break;
      }
    }
  }

  // If config file exists, load it
  if (!config_path_.empty() && std::filesystem::exists(config_path_)) {
    return loadFromFile(config_path_);
  }

  // Otherwise, initialize with defaults
  initializeDefaults();
  loaded_ = true;
  return true;
}

bool SystemConfigManager::loadFromFile(const std::string &configPath) {
  try {
    std::ifstream file(configPath);
    if (!file.is_open()) {
      std::cerr << "[SystemConfigManager] Failed to open config file: " << configPath << std::endl;
      initializeDefaults();
      loaded_ = false;
      return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
      std::cerr << "[SystemConfigManager] Failed to parse config file: " << errors << std::endl;
      initializeDefaults();
      loaded_ = false;
      return false;
    }

    config_entities_.clear();

    if (root.isMember("systemConfig") && root["systemConfig"].isArray()) {
      for (const auto &item : root["systemConfig"]) {
        SystemConfigEntity entity;
        if (item.isMember("fieldId")) {
          entity.fieldId = item["fieldId"].asString();
        }
        if (item.isMember("displayName")) {
          entity.displayName = item["displayName"].asString();
        }
        if (item.isMember("type")) {
          entity.type = item["type"].asString();
        }
        if (item.isMember("value")) {
          entity.value = item["value"].asString();
        }
        if (item.isMember("group")) {
          entity.group = item["group"].asString();
        }
        if (item.isMember("availableValues") && item["availableValues"].isArray()) {
          for (const auto &av : item["availableValues"]) {
            DisplayableEntity de;
            if (av.isMember("displayName")) {
              de.displayName = av["displayName"].asString();
            }
            if (av.isMember("value")) {
              de.value = av["value"].asString();
            }
            entity.availableValues.push_back(de);
          }
        }
        config_entities_.push_back(entity);
      }
    }

    loaded_ = true;
    std::cerr << "[SystemConfigManager] Loaded " << config_entities_.size() << " config entities from " << configPath << std::endl;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[SystemConfigManager] Error loading config: " << e.what() << std::endl;
    initializeDefaults();
    loaded_ = false;
    return false;
  }
}

void SystemConfigManager::initializeDefaults() {
  config_entities_.clear();

  // Example default config entities
  SystemConfigEntity entity1;
  entity1.fieldId = "voice6";
  entity1.displayName = "voice n. 6";
  entity1.type = "options";
  entity1.value = "solutions";
  entity1.group = "groupBy";
  
  // Add available values - must include the default value
  DisplayableEntity de1;
  de1.displayName = "Solutions";
  de1.value = "solutions";
  entity1.availableValues.push_back(de1);
  
  DisplayableEntity de2;
  de2.displayName = "securt 1 version 1.0";
  de2.value = "securt";
  entity1.availableValues.push_back(de2);
  
  config_entities_.push_back(entity1);

  // Add more default entities as needed
}

std::vector<SystemConfigEntity> SystemConfigManager::getSystemConfig() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_entities_;
}

Json::Value SystemConfigManager::getSystemConfigJson() const {
  std::lock_guard<std::mutex> lock(mutex_);

  Json::Value result;
  Json::Value configArray(Json::arrayValue);

  for (const auto &entity : config_entities_) {
    Json::Value item;
    item["fieldId"] = entity.fieldId;
    item["displayName"] = entity.displayName;
    item["type"] = entity.type;
    item["value"] = entity.value;
    item["group"] = entity.group;

    Json::Value availableValuesArray(Json::arrayValue);
    for (const auto &av : entity.availableValues) {
      Json::Value avItem;
      avItem["displayName"] = av.displayName;
      avItem["value"] = av.value;
      availableValuesArray.append(avItem);
    }
    item["availableValues"] = availableValuesArray;

    configArray.append(item);
  }

  result["systemConfig"] = configArray;
  return result;
}

bool SystemConfigManager::updateSystemConfig(const std::vector<std::pair<std::string, std::string>> &updates) {
  std::string configPath;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto &update : updates) {
      auto it = std::find_if(config_entities_.begin(), config_entities_.end(),
                            [&update](const SystemConfigEntity &e) {
                              return e.fieldId == update.first;
                            });

      if (it != config_entities_.end()) {
        // Validate value if needed (use unlocked version since we already hold the lock)
        if (!validateConfigValueUnlocked(update.first, update.second)) {
          std::cerr << "[SystemConfigManager] Invalid value for fieldId: " << update.first << std::endl;
          return false;
        }
        it->value = update.second;
      } else {
        std::cerr << "[SystemConfigManager] FieldId not found: " << update.first << std::endl;
        return false;
      }
    }

    // Store config path before releasing lock
    configPath = config_path_;
  } // Release lock here to avoid deadlock when saveConfig calls getSystemConfigJson()

  // Save to file if path is set (outside of lock to avoid deadlock)
  if (!configPath.empty()) {
    saveConfig(configPath);
  }

  return true;
}

bool SystemConfigManager::updateSystemConfigFromJson(const Json::Value &json) {
  if (!json.isMember("systemConfig") || !json["systemConfig"].isArray()) {
    return false;
  }

  std::vector<std::pair<std::string, std::string>> updates;
  for (const auto &item : json["systemConfig"]) {
    if (item.isMember("fieldId") && item.isMember("value")) {
      updates.push_back({item["fieldId"].asString(), item["value"].asString()});
    }
  }

  return updateSystemConfig(updates);
}

const SystemConfigEntity *SystemConfigManager::getConfigEntity(const std::string &fieldId) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = std::find_if(config_entities_.begin(), config_entities_.end(),
                         [&fieldId](const SystemConfigEntity &e) {
                           return e.fieldId == fieldId;
                         });

  if (it != config_entities_.end()) {
    return &(*it);
  }

  return nullptr;
}

bool SystemConfigManager::validateConfigValueUnlocked(const std::string &fieldId, const std::string &value) const {
  auto it = std::find_if(config_entities_.begin(), config_entities_.end(),
                         [&fieldId](const SystemConfigEntity &e) {
                           return e.fieldId == fieldId;
                         });

  if (it == config_entities_.end()) {
    return false;
  }

  // If type is "options", validate against availableValues
  if (it->type == "options") {
    for (const auto &av : it->availableValues) {
      if (av.value == value) {
        return true;
      }
    }
    return false;
  }

  // For other types, accept any value (can add more validation later)
  return true;
}

bool SystemConfigManager::validateConfigValue(const std::string &fieldId, const std::string &value) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return validateConfigValueUnlocked(fieldId, value);
}

bool SystemConfigManager::saveConfig(const std::string &configPath) {
  std::string path = configPath.empty() ? config_path_ : configPath;
  if (path.empty()) {
    return false;
  }

  try {
    // Create parent directory if needed
    std::filesystem::path filePath(path);
    if (filePath.has_parent_path()) {
      std::filesystem::create_directories(filePath.parent_path());
    }

    std::ofstream file(path);
    if (!file.is_open()) {
      std::cerr << "[SystemConfigManager] Failed to open file for writing: " << path << std::endl;
      return false;
    }

    Json::Value root = getSystemConfigJson();
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);

    file.close();
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[SystemConfigManager] Error saving config: " << e.what() << std::endl;
    return false;
  }
}

bool SystemConfigManager::isLoaded() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return loaded_;
}

