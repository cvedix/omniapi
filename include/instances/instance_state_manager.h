#pragma once

#include <json/json.h>
#include <mutex>
#include <string>
#include <unordered_map>

/**
 * @brief Instance State Manager
 *
 * Manages runtime state for instances (separate from config).
 * State is in-memory only and exists only when instance is loaded/running.
 *
 * State vs Config:
 * - Config: Persistent settings, stored in files/database
 * - State: Runtime settings, only in memory, cleared when instance unloaded
 */
class InstanceStateManager {
public:
  /**
   * @brief Get runtime state for an instance
   * @param instanceId Instance ID
   * @return JSON object with state (empty if instance not loaded or no state)
   */
  Json::Value getState(const std::string &instanceId) const;

  /**
   * @brief Set state value at a specific path
   * @param instanceId Instance ID
   * @param path Path string with "/" separator (e.g., "Output/handlers/Mqtt")
   * @param value JSON value to set
   * @return true if successful, false otherwise
   */
  bool setState(const std::string &instanceId, const std::string &path,
                const Json::Value &value);

  /**
   * @brief Clear state for an instance (called when instance is unloaded)
   * @param instanceId Instance ID
   */
  void clearState(const std::string &instanceId);

  /**
   * @brief Initialize state storage for an instance (called when instance is loaded)
   * @param instanceId Instance ID
   */
  void initializeState(const std::string &instanceId);

  /**
   * @brief Check if instance has state (is loaded)
   * @param instanceId Instance ID
   * @return true if instance has state storage
   */
  bool hasState(const std::string &instanceId) const;

private:
  // Thread-safe state storage: map<instanceId, map<path, value>>
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Json::Value> states_;

  /**
   * @brief Set nested JSON value at a path
   * @param root Root JSON object to modify
   * @param path Path string with "/" separator
   * @param value JSON value to set
   * @return true if successful, false otherwise
   */
  bool setNestedJsonValue(Json::Value &root, const std::string &path,
                          const Json::Value &value) const;
};

