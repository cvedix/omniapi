#pragma once

#include "instances/instance_manager.h"
#include "instances/instance_state_manager.h"
#include "instances/instance_storage.h"
#include "solutions/solution_registry.h"
#include "worker/worker_supervisor.h"
#include "core/resource_manager.h"
#include <memory>
#include <mutex>
#include <unordered_map>

/**
 * @brief Subprocess-based Instance Manager
 *
 * Implements IInstanceManager using WorkerSupervisor for subprocess isolation.
 * Each instance runs in its own worker process, providing:
 * - Memory isolation (leaks don't affect main server)
 * - Crash isolation (one instance crash doesn't affect others)
 * - Hot reload capability (restart worker without restarting API)
 */
class SubprocessInstanceManager : public IInstanceManager {
public:
  /**
   * @brief Constructor
   * @param solutionRegistry Reference to solution registry
   * @param instanceStorage Reference to instance storage
   * @param workerExecutable Path to worker executable (default:
   * "edgeos-worker")
   */
  SubprocessInstanceManager(
      SolutionRegistry &solutionRegistry, InstanceStorage &instanceStorage,
      const std::string &workerExecutable = "edgeos-worker");

  ~SubprocessInstanceManager() override;

  // ========== Instance Lifecycle ==========

  std::string createInstance(const CreateInstanceRequest &req) override;
  bool deleteInstance(const std::string &instanceId) override;
  bool startInstance(const std::string &instanceId,
                     bool skipAutoStop = false) override;
  bool stopInstance(const std::string &instanceId) override;
  bool updateInstance(const std::string &instanceId,
                      const Json::Value &configJson) override;

  /**
   * @brief Update crossing lines runtime (without restart)
   * @param instanceId Instance ID
   * @param linesArray JSON array of line objects
   * @return true if successful
   */
  bool updateLines(const std::string &instanceId, const Json::Value &linesArray);

  /**
   * @brief Update jam zones runtime (without restart) – subprocess only
   * @param instanceId Instance ID
   * @param jamsJson JSON array of jam zone objects
   * @return true if successful
   */
  bool updateJams(const std::string &instanceId, const Json::Value &jamsJson);

  /**
   * @brief Update stop zones runtime (without restart) – subprocess only
   * @param instanceId Instance ID
   * @param stopsJson JSON array of stop zone objects
   * @return true if successful
   */
  bool updateStops(const std::string &instanceId, const Json::Value &stopsJson);

  /**
   * @brief Push a frame into instance pipeline (app_src) – subprocess only
   * @param instanceId Instance ID
   * @param frameBase64 Base64-encoded frame (JPEG/PNG bytes)
   * @param codec Codec id: "jpeg", "png", etc.
   * @return true if successful
   */
  bool pushFrame(const std::string &instanceId, const std::string &frameBase64,
                 const std::string &codec);

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

  std::string getBackendType() const override { return "subprocess"; }
  bool isSubprocessMode() const override { return true; }

  // ========== Subprocess-specific ==========

  /**
   * @brief Get worker supervisor (for advanced operations)
   */
  worker::WorkerSupervisor &getSupervisor() { return *supervisor_; }

  /**
   * @brief Stop all workers gracefully
   */
  void stopAllWorkers();

private:
  SolutionRegistry &solution_registry_;
  InstanceStorage &instance_storage_;
  std::unique_ptr<worker::WorkerSupervisor> supervisor_;
  static InstanceStateManager state_manager_; // Shared state manager

  // Local cache of instance info (synced with workers)
  // Both mutex and map are mutable to allow cache updates in const methods
  mutable std::mutex instances_mutex_;
  mutable std::unordered_map<std::string, InstanceInfo> instances_;
  
  // GPU allocation tracking (maps instance ID to GPU allocation)
  std::unordered_map<std::string, std::shared_ptr<ResourceManager::Allocation>> gpu_allocations_;
  mutable std::mutex gpu_allocations_mutex_;

  /**
   * @brief Build config JSON from CreateInstanceRequest
   */
  Json::Value buildWorkerConfig(const CreateInstanceRequest &req) const;

  /**
   * @brief Build config JSON from InstanceInfo
   */
  Json::Value buildWorkerConfigFromInstanceInfo(const InstanceInfo &info) const;

  /**
   * @brief Update local instance cache from worker response
   */
  void updateInstanceCache(const std::string &instanceId,
                           const worker::IPCMessage &response);

  /**
   * @brief Handle worker state changes
   */
  void onWorkerStateChange(const std::string &instanceId,
                           worker::WorkerState oldState,
                           worker::WorkerState newState);

  /**
   * @brief Handle worker errors
   */
  void onWorkerError(const std::string &instanceId, const std::string &error);

  /**
   * @brief Helper: Allocate GPU and spawn worker with GPU device ID
   * @param instanceId Instance ID
   * @param config Worker configuration
   * @return true if successful
   */
  bool allocateGPUAndSpawnWorker(const std::string &instanceId, const Json::Value &config);
};
