#pragma once

#include <json/json.h>
#include <mutex>
#include <string>

/**
 * @brief Preferences Manager
 *
 * Manages system preferences loaded from rtconfig.json
 * Thread-safe singleton pattern
 */
class PreferencesManager {
public:
  /**
   * @brief Get singleton instance
   */
  static PreferencesManager &getInstance();

  /**
   * @brief Load preferences from rtconfig.json
   * @param configPath Path to rtconfig.json (optional)
   * @return true if loaded successfully
   */
  bool loadPreferences(const std::string &configPath = "");

  /**
   * @brief Get all preferences as JSON
   * @return JSON object with preferences
   */
  Json::Value getPreferences() const;

  /**
   * @brief Get preference value by key (supports dot notation)
   * @param key Preference key (e.g., "vms.show_area_crossing" or "global.default_performance_mode")
   * @return JSON value if found, Json::nullValue otherwise
   */
  Json::Value getPreference(const std::string &key) const;

  /**
   * @brief Set preference value by key (supports dot notation)
   * @param key Preference key
   * @param value New value
   * @return true if set successfully
   */
  bool setPreference(const std::string &key, const Json::Value &value);

  /**
   * @brief Update preferences from JSON (merge)
   * @param json JSON object with preferences to update
   * @return true if updated successfully
   */
  bool updatePreferences(const Json::Value &json);

  /**
   * @brief Save preferences to file
   * @param configPath Path to save (optional, uses current path if empty)
   * @return true if saved successfully
   */
  bool savePreferences(const std::string &configPath = "");

  /**
   * @brief Reload preferences from file
   * @return true if reloaded successfully
   */
  bool reloadPreferences();

  /**
   * @brief Check if preferences are loaded
   */
  bool isLoaded() const;

  /**
   * @brief Get preferences file path
   */
  std::string getPreferencesPath() const;

private:
  PreferencesManager() = default;
  ~PreferencesManager() = default;
  PreferencesManager(const PreferencesManager &) = delete;
  PreferencesManager &operator=(const PreferencesManager &) = delete;

  /**
   * @brief Initialize default preferences
   */
  void initializeDefaults();

  /**
   * @brief Load preferences from file
   */
  bool loadFromFile(const std::string &configPath);

  /**
   * @brief Get JSON value by dot-notation key
   */
  Json::Value *getJsonValueByKey(const std::string &key, bool createIfNotExists = false);

  /**
   * @brief Flatten nested JSON to dot-notation keys
   */
  void flattenJson(const Json::Value &input, Json::Value &output, const std::string &prefix = "");

  mutable std::mutex mutex_;
  Json::Value preferences_;
  std::string config_path_;
  bool loaded_ = false;
};

