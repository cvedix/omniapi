#pragma once

#include "core/pipeline_builder.h"
#include "instances/inprocess_instance_manager.h"
#include "instances/instance_manager.h"
#include "instances/instance_registry.h"
#include "instances/instance_storage.h"
#include "instances/subprocess_instance_manager.h"
#include "solutions/solution_registry.h"
#include <memory>
#include <string>

/**
 * @brief Factory for creating Instance Managers
 *
 * Creates the appropriate instance manager based on configuration.
 * Supports both legacy in-process mode and new subprocess isolation mode.
 */
class InstanceManagerFactory {
public:
  /**
   * @brief Create instance manager based on execution mode
   *
   * @param mode Execution mode (IN_PROCESS or SUBPROCESS)
   * @param solutionRegistry Solution registry reference
   * @param pipelineBuilder Pipeline builder reference (only used for
   * IN_PROCESS)
   * @param instanceStorage Instance storage reference
   * @param workerExecutable Worker executable path (only used for SUBPROCESS)
   * @return Unique pointer to instance manager
   */
  static std::unique_ptr<IInstanceManager>
  create(InstanceExecutionMode mode, SolutionRegistry &solutionRegistry,
         PipelineBuilder &pipelineBuilder, InstanceStorage &instanceStorage,
         const std::string &workerExecutable = "edgeos-worker");

  /**
   * @brief Create in-process manager (legacy mode)
   *
   * Uses InstanceRegistry directly - pipelines run in main process.
   */
  static std::unique_ptr<IInstanceManager>
  createInProcess(SolutionRegistry &solutionRegistry,
                  PipelineBuilder &pipelineBuilder,
                  InstanceStorage &instanceStorage);

  /**
   * @brief Create subprocess manager (isolated mode)
   *
   * Uses WorkerSupervisor - each instance runs in separate process.
   */
  static std::unique_ptr<IInstanceManager>
  createSubprocess(SolutionRegistry &solutionRegistry,
                   InstanceStorage &instanceStorage,
                   const std::string &workerExecutable = "edgeos-worker");

  /**
   * @brief Get execution mode from environment variable or config
   *
   * Checks EDGE_AI_EXECUTION_MODE env var:
   * - "subprocess" or "isolated" -> SUBPROCESS mode
   * - "inprocess" or "legacy" or unset -> IN_PROCESS mode
   */
  static InstanceExecutionMode getExecutionModeFromEnv();

  /**
   * @brief Get execution mode name as string
   */
  static std::string getModeName(InstanceExecutionMode mode);

private:
  // Internal registry for in-process mode (owned by factory)
  // This is needed because InProcessInstanceManager wraps InstanceRegistry
  static std::unique_ptr<InstanceRegistry> s_registry_;
};
