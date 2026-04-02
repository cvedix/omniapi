#pragma once

#include <json/json.h>
#include <map>
#include <string>

// Forward declarations
class IInstanceManager;
class SolutionRegistry;
class CreateInstanceRequest;
class InstanceInfo;

/**
 * @brief Quick Instance Parser
 *
 * Extracts JSON request body mapping logic originally used by the Quick Instance endpoint
 * to build CreateInstanceRequest instances.
 */
class QuickInstanceParser {
public:
  /**
   * @brief Set instance manager (dependency injection)
   * @param manager Pointer to IInstanceManager implementation
   */
  static void setInstanceManager(IInstanceManager *manager);

  /**
   * @brief Set solution registry (dependency injection)
   */
  static void setSolutionRegistry(SolutionRegistry *registry);

  /**
   * @brief Parse JSON request body và build CreateInstanceRequest (dùng chung)
   */
  bool parseQuickRequest(const Json::Value &json, CreateInstanceRequest &req,
                         std::string &error);

private:
  static IInstanceManager *instance_manager_;
  static SolutionRegistry *solution_registry_;

  /**
   * @brief Map solution type to solution ID
   */
  std::string mapSolutionTypeToId(const std::string &solutionType,
                                  const std::string &inputType,
                                  const std::string &outputType) const;

  /**
   * @brief Convert development paths to production paths
   */
  std::string convertPathToProduction(const std::string &path) const;

  /**
   * @brief Get default values for a solution type
   */
  std::map<std::string, std::string>
  getDefaultParams(const std::string &solutionType,
                   const std::string &inputType,
                   const std::string &outputType) const;
};
