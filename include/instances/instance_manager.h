#pragma once

#include "instances/instance_info.h"
#include "instances/instance_statistics.h"
#include "models/create_instance_request.h"
#include <json/json.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Instance Manager Interface
 *
 * Abstract interface for managing AI instances.
 * This allows switching between:
 * - In-process execution (InstanceRegistry - legacy)
 * - Subprocess execution (WorkerSupervisor - new)
 *
 * API handlers use this interface, making them agnostic to the backend.
 */
class IInstanceManager {
public:
  virtual ~IInstanceManager() = default;

  // ========== Instance Lifecycle ==========

  /**
   * @brief Create a new instance
   * @param req Create instance request
   * @return Instance ID if successful, empty string otherwise
   * @throws std::invalid_argument if request is invalid
   */
  virtual std::string createInstance(const CreateInstanceRequest &req) = 0;

  /**
   * @brief Delete an instance
   * @param instanceId Instance ID
   * @return true if successful
   */
  virtual bool deleteInstance(const std::string &instanceId) = 0;

  /**
   * @brief Start an instance
   * @param instanceId Instance ID
   * @param skipAutoStop Skip auto-stop of running instance (for restart)
   * @return true if successful
   */
  virtual bool startInstance(const std::string &instanceId,
                             bool skipAutoStop = false) = 0;

  /**
   * @brief Stop an instance
   * @param instanceId Instance ID
   * @return true if successful
   */
  virtual bool stopInstance(const std::string &instanceId) = 0;

  /**
   * @brief Update instance configuration
   * @param instanceId Instance ID
   * @param configJson JSON config to merge
   * @return true if successful
   */
  virtual bool updateInstance(const std::string &instanceId,
                              const Json::Value &configJson) = 0;

  // ========== Instance Query ==========

  /**
   * @brief Get instance information
   * @param instanceId Instance ID
   * @return Instance info if found
   */
  virtual std::optional<InstanceInfo>
  getInstance(const std::string &instanceId) const = 0;

  /**
   * @brief List all instance IDs
   */
  virtual std::vector<std::string> listInstances() const = 0;

  /**
   * @brief Get all instances info (returns copy)
   */
  virtual std::vector<InstanceInfo> getAllInstances() const = 0;

  /**
   * @brief Check if instance exists
   */
  virtual bool hasInstance(const std::string &instanceId) const = 0;

  /**
   * @brief Get instance count
   */
  virtual int getInstanceCount() const = 0;

  // ========== Instance Data ==========

  /**
   * @brief Get instance statistics
   * @param instanceId Instance ID
   * @return Statistics if available
   */
  virtual std::optional<InstanceStatistics>
  getInstanceStatistics(const std::string &instanceId) = 0;

  /**
   * @brief Get last frame from instance (base64 JPEG)
   * @param instanceId Instance ID
   * @return Base64-encoded frame or empty string
   */
  virtual std::string getLastFrame(const std::string &instanceId) const = 0;

  /**
   * @brief Get instance config as JSON
   * @param instanceId Instance ID
   * @return JSON config
   */
  virtual Json::Value
  getInstanceConfig(const std::string &instanceId) const = 0;

  /**
   * @brief Update instance from JSON config
   * @param instanceId Instance ID
   * @param configJson JSON config to apply
   * @return true if successful
   */
  virtual bool updateInstanceFromConfig(const std::string &instanceId,
                                        const Json::Value &configJson) = 0;

  /**
   * @brief Check if instance has RTMP output configured
   * @param instanceId Instance ID
   * @return true if RTMP output is configured
   */
  virtual bool hasRTMPOutput(const std::string &instanceId) const = 0;

  // ========== Instance Management Operations ==========

  /**
   * @brief Load all persistent instances from storage
   * Called during startup to restore instances from disk
   */
  virtual void loadPersistentInstances() = 0;

  /**
   * @brief Check and handle retry limits for instances
   * Monitors instances and stops those that exceed retry limits
   * @return Number of instances that were stopped due to retry limit
   */
  virtual int checkAndHandleRetryLimits() = 0;

  // ========== Instance State Management ==========

  /**
   * @brief Load instance into memory
   * @param instanceId Instance ID
   * @return true if successful
   */
  virtual bool loadInstance(const std::string &instanceId) = 0;

  /**
   * @brief Unload instance from memory
   * @param instanceId Instance ID
   * @return true if successful
   */
  virtual bool unloadInstance(const std::string &instanceId) = 0;

  /**
   * @brief Get runtime state of instance
   * @param instanceId Instance ID
   * @return JSON object with state (empty if instance not loaded)
   */
  virtual Json::Value getInstanceState(const std::string &instanceId) = 0;

  /**
   * @brief Set runtime state value at a specific path
   * @param instanceId Instance ID
   * @param path Path string with "/" separator (e.g., "Output/handlers/Mqtt")
   * @param value JSON value to set
   * @return true if successful
   */
  virtual bool setInstanceState(const std::string &instanceId,
                                const std::string &path,
                                const Json::Value &value) = 0;

  // ========== Backend Info ==========

  /**
   * @brief Get backend type name
   * @return "in-process" or "subprocess"
   */
  virtual std::string getBackendType() const = 0;

  /**
   * @brief Check if using subprocess isolation
   */
  virtual bool isSubprocessMode() const = 0;
};

/**
 * @brief Execution mode for instance management
 */
enum class InstanceExecutionMode {
  IN_PROCESS, // Legacy: Run pipelines in main process (InstanceRegistry)
  SUBPROCESS  // New: Run pipelines in isolated subprocesses (WorkerSupervisor)
};
