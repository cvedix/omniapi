#include "core/preferences_manager.h"
#include "core/env_config.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

PreferencesManager &PreferencesManager::getInstance() {
  static PreferencesManager instance;
  return instance;
}

bool PreferencesManager::loadPreferences(const std::string &configPath) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!configPath.empty()) {
    config_path_ = configPath;
  }

  // If no path specified, try to find rtconfig.json
  if (config_path_.empty()) {
    std::vector<std::string> possiblePaths = {
        "./config/rtconfig.json",
        "./rtconfig.json",
        "/opt/edgeos-api/config/rtconfig.json",
        "/etc/edgeos-api/rtconfig.json"
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

void PreferencesManager::flattenJson(const Json::Value &input, Json::Value &output, const std::string &prefix) {
  if (input.isObject()) {
    for (const auto &key : input.getMemberNames()) {
      std::string newKey = prefix.empty() ? key : prefix + "." + key;
      flattenJson(input[key], output, newKey);
    }
  } else {
    output[prefix] = input;
  }
}

bool PreferencesManager::loadFromFile(const std::string &configPath) {
  try {
    std::ifstream file(configPath);
    if (!file.is_open()) {
      std::cerr << "[PreferencesManager] Failed to open preferences file: " << configPath << std::endl;
      initializeDefaults();
      loaded_ = false;
      return false;
    }

    Json::Value rawJson;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &rawJson, &errors)) {
      std::cerr << "[PreferencesManager] Failed to parse preferences file: " << errors << std::endl;
      initializeDefaults();
      loaded_ = false;
      return false;
    }

    // Flatten nested JSON to dot-notation keys
    preferences_.clear();
    flattenJson(rawJson, preferences_, "");

    loaded_ = true;
    std::cerr << "[PreferencesManager] Loaded preferences from " << configPath << std::endl;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[PreferencesManager] Error loading preferences: " << e.what() << std::endl;
    initializeDefaults();
    loaded_ = false;
    return false;
  }
}

void PreferencesManager::initializeDefaults() {
  preferences_.clear();

  // Initialize default preferences based on task specification
  preferences_["vms.show_area_crossing"] = true;
  preferences_["vms.show_line_crossing"] = true;
  preferences_["vms.show_intrusion"] = true;
  preferences_["vms.show_crowding"] = true;
  preferences_["vms.show_loitering"] = true;
  preferences_["vms.show_object_left"] = true;
  preferences_["vms.show_object_guarding"] = true;
  preferences_["vms.show_tailgating"] = true;
  preferences_["vms.show_fallen_person"] = true;
  preferences_["vms.show_armed_person"] = true;
  preferences_["vms.show_access_control_plate"] = true;
  preferences_["vms.show_access_control_face"] = true;
  preferences_["vms.show_appearance_search"] = true;
  preferences_["vms.show_identities"] = true;
  preferences_["vms.show_remote_mode"] = true;
  preferences_["vms.show_thermal_support"] = true;
  preferences_["vms.show_active_detection"] = true;
  preferences_["vms.show_error_reporting"] = true;
  preferences_["vms.show_trial_licenses"] = true;
  preferences_["vms.hardware_decoding"] = true;
  preferences_["global.default_performance_mode"] = "Performance";
}

Json::Value PreferencesManager::getPreferences() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return preferences_;
}

Json::Value PreferencesManager::getPreference(const std::string &key) const {
  std::lock_guard<std::mutex> lock(mutex_);

  // For const method, directly access preferences_ with flat key
  if (preferences_.isMember(key)) {
    return preferences_[key];
  }

  return Json::nullValue;
}

bool PreferencesManager::setPreference(const std::string &key, const Json::Value &value) {
  std::lock_guard<std::mutex> lock(mutex_);

  Json::Value *target = getJsonValueByKey(key, true);
  if (!target) {
    return false;
  }

  *target = value;
  return true;
}

bool PreferencesManager::updatePreferences(const Json::Value &json) {
  if (!json.isObject()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  // Merge JSON objects
  for (const auto &key : json.getMemberNames()) {
    preferences_[key] = json[key];
  }

  return true;
}

bool PreferencesManager::savePreferences(const std::string &configPath) {
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
      std::cerr << "[PreferencesManager] Failed to open file for writing: " << path << std::endl;
      return false;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(preferences_, &file);

    file.close();
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[PreferencesManager] Error saving preferences: " << e.what() << std::endl;
    return false;
  }
}

bool PreferencesManager::reloadPreferences() {
  if (config_path_.empty()) {
    return false;
  }

  return loadFromFile(config_path_);
}

bool PreferencesManager::isLoaded() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return loaded_;
}

std::string PreferencesManager::getPreferencesPath() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_path_;
}

Json::Value *PreferencesManager::getJsonValueByKey(const std::string &key, bool createIfNotExists) {
  // After flattening, preferences_ has flat keys like "vms.show_area_crossing"
  // So we can directly look up the key
  if (preferences_.isMember(key)) {
    return &preferences_[key];
  }

  if (createIfNotExists) {
    preferences_[key] = Json::Value();
    return &preferences_[key];
  }

  return nullptr;
}

