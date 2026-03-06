#pragma once

#include "core/securt_instance.h"
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief SecuRT Instance Registry
 *
 * Stores and manages SecuRT instances in memory.
 * Thread-safe registry for SecuRT instance data.
 */
class SecuRTInstanceRegistry {
public:
  /**
   * @brief Create a new SecuRT instance
   * @param instanceId Instance ID
   * @param instance SecuRT instance data
   * @return true if created successfully, false if instanceId already exists
   */
  bool createInstance(const std::string &instanceId,
                     const SecuRTInstance &instance);

  /**
   * @brief Get SecuRT instance by ID
   * @param instanceId Instance ID
   * @return Optional SecuRT instance if found
   */
  std::optional<SecuRTInstance> getInstance(const std::string &instanceId) const;

  /**
   * @brief Update SecuRT instance
   * @param instanceId Instance ID
   * @param updates SecuRTInstanceWrite with updates
   * @return true if updated successfully, false if instance not found
   */
  bool updateInstance(const std::string &instanceId,
                      const SecuRTInstanceWrite &updates);

  /**
   * @brief Delete SecuRT instance
   * @param instanceId Instance ID
   * @return true if deleted successfully, false if instance not found
   */
  bool deleteInstance(const std::string &instanceId);

  /**
   * @brief Check if instance exists
   * @param instanceId Instance ID
   * @return true if instance exists
   */
  bool hasInstance(const std::string &instanceId) const;

  /**
   * @brief List all instance IDs
   * @return Vector of instance IDs
   */
  std::vector<std::string> listInstances() const;

  /**
   * @brief Get instance count
   * @return Number of instances
   */
  size_t getInstanceCount() const;

private:
  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, SecuRTInstance> instances_;
};

