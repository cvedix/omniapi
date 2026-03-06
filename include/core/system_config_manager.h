#pragma once

#include <json/json.h>
#include <mutex>
#include <string>
#include <vector>

/**
 * @brief Displayable entity for available values
 */
struct DisplayableEntity {
  std::string displayName;
  std::string value;
};

/**
 * @brief System configuration entity
 */
struct SystemConfigEntity {
  std::string fieldId;
  std::string displayName;
  std::string type;  // "options", "string", "number", "boolean"
  std::string value;
  std::string group;
  std::vector<DisplayableEntity> availableValues;
};

/**
 * @brief System Config Manager
 *
 * Manages system configuration entities with metadata
 * Thread-safe singleton pattern
 */
class SystemConfigManager {
public:
  /**
   * @brief Get singleton instance
   */
  static SystemConfigManager &getInstance();

  /**
   * @brief Load system config from file or initialize defaults
   * @param configPath Path to config file (optional)
   * @return true if loaded successfully
   */
  bool loadConfig(const std::string &configPath = "");

  /**
   * @brief Get all system config entities
   * @return Vector of system config entities
   */
  std::vector<SystemConfigEntity> getSystemConfig() const;

  /**
   * @brief Get system config as JSON
   * @return JSON array of config entities
   */
  Json::Value getSystemConfigJson() const;

  /**
   * @brief Update system config
   * @param updates Vector of updates (fieldId, value pairs)
   * @return true if updated successfully
   */
  bool updateSystemConfig(const std::vector<std::pair<std::string, std::string>> &updates);

  /**
   * @brief Update system config from JSON
   * @param json JSON array with fieldId and value
   * @return true if updated successfully
   */
  bool updateSystemConfigFromJson(const Json::Value &json);

  /**
   * @brief Get config entity by fieldId
   * @param fieldId Field identifier
   * @return Config entity if found, nullptr otherwise
   */
  const SystemConfigEntity *getConfigEntity(const std::string &fieldId) const;

  /**
   * @brief Validate config value
   * @param fieldId Field identifier
   * @param value Value to validate
   * @return true if value is valid
   */
  bool validateConfigValue(const std::string &fieldId, const std::string &value) const;

  /**
   * @brief Save config to file
   * @param configPath Path to save (optional, uses current path if empty)
   * @return true if saved successfully
   */
  bool saveConfig(const std::string &configPath = "");

  /**
   * @brief Check if config is loaded
   */
  bool isLoaded() const;

private:
  SystemConfigManager() = default;
  ~SystemConfigManager() = default;
  SystemConfigManager(const SystemConfigManager &) = delete;
  SystemConfigManager &operator=(const SystemConfigManager &) = delete;

  /**
   * @brief Initialize default configuration
   */
  void initializeDefaults();

  /**
   * @brief Load config from file
   */
  bool loadFromFile(const std::string &configPath);

  /**
   * @brief Validate config value without locking (assumes lock is already held)
   * @param fieldId Field identifier
   * @param value Value to validate
   * @return true if value is valid
   */
  bool validateConfigValueUnlocked(const std::string &fieldId, const std::string &value) const;

  mutable std::mutex mutex_;
  std::vector<SystemConfigEntity> config_entities_;
  std::string config_path_;
  bool loaded_ = false;
};

