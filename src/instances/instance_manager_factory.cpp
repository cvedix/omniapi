#include "instances/instance_manager_factory.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>

// Static member initialization
std::unique_ptr<InstanceRegistry> InstanceManagerFactory::s_registry_ = nullptr;

std::unique_ptr<IInstanceManager> InstanceManagerFactory::create(
    InstanceExecutionMode mode, SolutionRegistry &solutionRegistry,
    PipelineBuilder &pipelineBuilder, InstanceStorage &instanceStorage,
    const std::string &workerExecutable) {

  std::cout << "[InstanceManagerFactory] Creating manager in "
            << getModeName(mode) << " mode" << std::endl;

  switch (mode) {
  case InstanceExecutionMode::IN_PROCESS:
    return createInProcess(solutionRegistry, pipelineBuilder, instanceStorage);

  case InstanceExecutionMode::SUBPROCESS:
  default:
    return createSubprocess(solutionRegistry, instanceStorage, workerExecutable);
  }
}

std::unique_ptr<IInstanceManager>
InstanceManagerFactory::createInProcess(SolutionRegistry &solutionRegistry,
                                        PipelineBuilder &pipelineBuilder,
                                        InstanceStorage &instanceStorage) {

  // Create InstanceRegistry if not exists
  if (!s_registry_) {
    s_registry_ = std::make_unique<InstanceRegistry>(
        solutionRegistry, pipelineBuilder, instanceStorage);
  }

  return std::make_unique<InProcessInstanceManager>(*s_registry_);
}

std::unique_ptr<IInstanceManager>
InstanceManagerFactory::createSubprocess(SolutionRegistry &solutionRegistry,
                                         InstanceStorage &instanceStorage,
                                         const std::string &workerExecutable) {

  return std::make_unique<SubprocessInstanceManager>(
      solutionRegistry, instanceStorage, workerExecutable);
}

InstanceExecutionMode InstanceManagerFactory::getExecutionModeFromEnv() {
  const char *mode_env = std::getenv("EDGE_AI_EXECUTION_MODE");

  if (mode_env == nullptr || mode_env[0] == '\0') {
    return InstanceExecutionMode::SUBPROCESS;
  }

  std::string mode_str(mode_env);
  std::transform(mode_str.begin(), mode_str.end(), mode_str.begin(), ::tolower);

  // In-process only when explicitly requested (legacy / debugging)
  if (mode_str == "inprocess" || mode_str == "in-process" ||
      mode_str == "in_process" || mode_str == "legacy" ||
      mode_str == "main") {
    return InstanceExecutionMode::IN_PROCESS;
  }

  // subprocess, isolated, worker, or any other value → isolated workers
  return InstanceExecutionMode::SUBPROCESS;
}

std::string InstanceManagerFactory::getModeName(InstanceExecutionMode mode) {
  switch (mode) {
  case InstanceExecutionMode::SUBPROCESS:
    return "subprocess (isolated)";
  case InstanceExecutionMode::IN_PROCESS:
  default:
    return "in-process (legacy)";
  }
}
