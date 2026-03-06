#pragma once

#include "instances/instance_manager.h"
#include "instances/instance_registry.h"
#include "instances/instance_state_manager.h"

/**
 * @brief In-process Instance Manager (Adapter for InstanceRegistry)
 *
 * Wraps existing InstanceRegistry to implement IInstanceManager interface.
 * This is the legacy mode where pipelines run in the main process.
 *
 * Use this for:
 * - Backward compatibility
 * - Development/debugging (easier to debug in single process)
 * - Systems where subprocess overhead is not desired
 */
class InProcessInstanceManager : public IInstanceManager {
public:
  /**
   * @brief Constructor
   * @param registry Reference to existing InstanceRegistry
   */
  explicit InProcessInstanceManager(InstanceRegistry &registry);

  ~InProcessInstanceManager() override = default;

  // ========== Instance Lifecycle ==========

  std::string createInstance(const CreateInstanceRequest &req) override;
  bool deleteInstance(const std::string &instanceId) override;
  bool startInstance(const std::string &instanceId,
                     bool skipAutoStop = false) override;
  bool stopInstance(const std::string &instanceId) override;
  bool updateInstance(const std::string &instanceId,
                      const Json::Value &configJson) override;

  // ========== Instance Query ==========

  std::optional<InstanceInfo>
  getInstance(const std::string &instanceId) const override;
  std::vector<std::string> listInstances() const override;
  std::vector<InstanceInfo> getAllInstances() const override;
  bool hasInstance(const std::string &instanceId) const override;
  int getInstanceCount() const override;

  // ========== Instance Data ==========

  std::optional<InstanceStatistics>
  getInstanceStatistics(const std::string &instanceId) override;
  std::string getLastFrame(const std::string &instanceId) const override;
  Json::Value getInstanceConfig(const std::string &instanceId) const override;
  bool updateInstanceFromConfig(const std::string &instanceId,
                                const Json::Value &configJson) override;
  bool hasRTMPOutput(const std::string &instanceId) const override;

  // ========== Instance Management Operations ==========

  void loadPersistentInstances() override;
  int checkAndHandleRetryLimits() override;

  // ========== Instance State Management ==========

  bool loadInstance(const std::string &instanceId) override;
  bool unloadInstance(const std::string &instanceId) override;
  Json::Value getInstanceState(const std::string &instanceId) override;
  bool setInstanceState(const std::string &instanceId, const std::string &path,
                        const Json::Value &value) override;

  // ========== Backend Info ==========

  std::string getBackendType() const override { return "in-process"; }
  bool isSubprocessMode() const override { return false; }

  // ========== Legacy Access ==========

  /**
   * @brief Get underlying InstanceRegistry (for legacy code that needs direct
   * access)
   */
  InstanceRegistry &getRegistry() { return registry_; }
  const InstanceRegistry &getRegistry() const { return registry_; }

private:
  InstanceRegistry &registry_;
  static InstanceStateManager state_manager_; // Shared state manager
};
