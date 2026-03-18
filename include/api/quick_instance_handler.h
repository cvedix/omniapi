#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <map>
#include <string>

using namespace drogon;

// Forward declarations
class IInstanceManager;
class SolutionRegistry;
class CreateInstanceRequest;
class InstanceInfo;

/**
 * @brief Quick Instance Handler
 *
 * Handles POST /v1/core/instance/quick endpoint for creating instances
 * with simplified parameters. Automatically maps solution types to
 * appropriate solution IDs and provides default values.
 *
 * Endpoints:
 * - POST /v1/core/instance/quick - Create a new instance quickly
 */
class QuickInstanceHandler
    : public drogon::HttpController<QuickInstanceHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(QuickInstanceHandler::createQuickInstance,
                "/v1/core/instance/quick", Post);
  ADD_METHOD_TO(QuickInstanceHandler::handleOptions, "/v1/core/instance/quick",
                Options);
  METHOD_LIST_END

  /**
   * @brief Handle POST /v1/core/instance/quick
   * Creates a new AI instance with simplified parameters
   */
  void
  createQuickInstance(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle OPTIONS request for CORS preflight
   */
  void handleOptions(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

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
   *
   * Được dùng bởi:
   * - QuickInstanceHandler (POST /v1/core/instance/quick)
   * - SecuRTHandler (POST /v1/securt/instance với full-config body)
   */
  bool parseQuickRequest(const Json::Value &json, CreateInstanceRequest &req,
                         std::string &error);

private:
  static IInstanceManager *instance_manager_;
  static SolutionRegistry *solution_registry_;

  /**
   * @brief Map solution type to solution ID
   * @param solutionType Solution type (e.g., "face_detection", "ba_crossline")
   * @param inputType Input type ("file", "rtsp", etc.)
   * @param outputType Output type ("file", "rtmp", etc.)
   * @return Solution ID or empty string if not found
   */
  std::string mapSolutionTypeToId(const std::string &solutionType,
                                  const std::string &inputType,
                                  const std::string &outputType) const;

  /**
   * @brief Convert development paths to production paths
   * @param path Original path
   * @return Converted path
   */
  std::string convertPathToProduction(const std::string &path) const;

  /**
   * @brief Get default values for a solution type
   * @param solutionType Solution type
   * @param inputType Input type
   * @param outputType Output type
   * @return Map of default parameter values
   */
  std::map<std::string, std::string>
  getDefaultParams(const std::string &solutionType,
                   const std::string &inputType,
                   const std::string &outputType) const;

  /**
   * @brief Convert InstanceInfo to JSON response
   */
  Json::Value instanceInfoToJson(const InstanceInfo &info) const;

  /**
   * @brief Create error response
   */
  HttpResponsePtr createErrorResponse(int statusCode, const std::string &error,
                                      const std::string &message = "") const;
};
