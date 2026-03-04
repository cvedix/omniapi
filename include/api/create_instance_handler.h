#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

using namespace drogon;

// Forward declarations
class IInstanceManager;
class SolutionRegistry;
class CreateInstanceRequest;
class InstanceInfo;

/**
 * @brief Create Instance Handler
 *
 * Handles POST /v1/core/instance endpoint for creating new AI instances.
 *
 * Endpoints:
 * - POST /v1/core/instance - Create a new instance
 */
class CreateInstanceHandler
    : public drogon::HttpController<CreateInstanceHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(CreateInstanceHandler::createInstance, "/v1/core/instance",
                Post);
  ADD_METHOD_TO(CreateInstanceHandler::handleOptions, "/v1/core/instance",
                Options);
  METHOD_LIST_END

  /**
   * @brief Handle POST /v1/core/instance
   * Creates a new AI instance based on the request
   */
  void createInstance(const HttpRequestPtr &req,
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

private:
  static IInstanceManager *instance_manager_;
  static SolutionRegistry *solution_registry_;

  /**
   * @brief Parse JSON request body to CreateInstanceRequest
   */
  bool parseRequest(const Json::Value &json, CreateInstanceRequest &req,
                    std::string &error);

  /**
   * @brief Convert development paths to production paths
   * Converts paths like:
   * - /home/cvedix/project/edgeos-api/cvedix_data/... -> /opt/edgeos-api/...
   * - ./cvedix_data/... -> /opt/edgeos-api/...
   * - cvedix_data/... -> /opt/edgeos-api/...
   * @param path Original path
   * @return Converted path
   */
  std::string convertPathToProduction(const std::string &path) const;

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
