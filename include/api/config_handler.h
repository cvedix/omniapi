#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

using namespace drogon;

/**
 * @brief Configuration Management Handler
 *
 * Handles system configuration CRUD operations.
 *
 * Endpoints:
 * - GET /v1/securt/config - Get full configuration
 * - GET /v1/securt/config/{path} - Get configuration section
 * - POST /v1/securt/config - Create/update configuration (merge)
 * - PUT /v1/securt/config - Replace entire configuration
 * - PATCH /v1/securt/config/{path} - Update configuration section
 * - DELETE /v1/securt/config/{path} - Delete configuration section
 * - POST /v1/securt/config/reset - Reset configuration to defaults
 */
class ConfigHandler : public drogon::HttpController<ConfigHandler> {
public:
  METHOD_LIST_BEGIN
  // Route with path parameter (more specific, must come first)
  ADD_METHOD_TO(ConfigHandler::getConfigSection, "/v1/securt/config/{path}", Get);
  ADD_METHOD_TO(ConfigHandler::updateConfigSection, "/v1/securt/config/{path}",
                Patch);
  ADD_METHOD_TO(ConfigHandler::deleteConfigSection, "/v1/securt/config/{path}",
                Delete);
  ADD_METHOD_TO(ConfigHandler::handleOptions, "/v1/securt/config/{path}",
                Options);
  // Route without path parameter (less specific, comes after)
  ADD_METHOD_TO(ConfigHandler::getConfig, "/v1/securt/config", Get);
  ADD_METHOD_TO(ConfigHandler::createOrUpdateConfig, "/v1/securt/config", Post);
  ADD_METHOD_TO(ConfigHandler::replaceConfig, "/v1/securt/config", Put);
  ADD_METHOD_TO(ConfigHandler::updateConfigSection, "/v1/securt/config", Patch);
  ADD_METHOD_TO(ConfigHandler::deleteConfigSection, "/v1/securt/config", Delete);
  ADD_METHOD_TO(ConfigHandler::resetConfig, "/v1/securt/config/reset", Post);
  ADD_METHOD_TO(ConfigHandler::handleOptions, "/v1/securt/config", Options);
  ADD_METHOD_TO(ConfigHandler::handleOptions, "/v1/securt/config/reset", Options);
  METHOD_LIST_END

  /**
   * @brief Handle GET /v1/securt/config
   * Gets full system configuration
   */
  void getConfig(const HttpRequestPtr &req,
                 std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/securt/config/{path}
   * Gets configuration section at specified path
   */
  void
  getConfigSection(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/securt/config
   * Creates or updates configuration (merge)
   */
  void
  createOrUpdateConfig(const HttpRequestPtr &req,
                       std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle PUT /v1/securt/config
   * Replaces entire configuration
   */
  void replaceConfig(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle PATCH /v1/securt/config/{path}
   * Updates configuration section at specified path
   */
  void
  updateConfigSection(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle DELETE /v1/securt/config/{path}
   * Deletes configuration section at specified path
   */
  void
  deleteConfigSection(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/securt/config/reset
   * Resets configuration to default values
   */
  void resetConfig(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle OPTIONS request for CORS preflight
   */
  void handleOptions(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

private:
  /**
   * @brief Extract path parameter from request
   */
  std::string extractPath(const HttpRequestPtr &req) const;

  /**
   * @brief Create error response
   */
  HttpResponsePtr createErrorResponse(int statusCode, const std::string &error,
                                      const std::string &message = "") const;

  /**
   * @brief Create success JSON response with CORS headers
   */
  HttpResponsePtr createSuccessResponse(const Json::Value &data,
                                        int statusCode = 200) const;

  /**
   * @brief Validate JSON configuration
   */
  bool validateConfigJson(const Json::Value &json, std::string &error) const;

  /**
   * @brief Get auto_restart option from request (query param or JSON body)
   * @param req HTTP request
   * @param jsonBody JSON body (if available)
   * @return true if auto_restart is requested, false otherwise
   */
  bool getAutoRestartOption(const HttpRequestPtr &req,
                            const Json::Value *jsonBody = nullptr) const;

  /**
   * @brief Schedule system restart if needed
   * @param shouldRestart Whether restart should be scheduled
   * @param reason Reason for restart (for logging)
   * @param delaySeconds Delay before restart (default: 3 seconds)
   */
  void scheduleRestartIfNeeded(bool shouldRestart,
                               const std::string &reason = "",
                               double delaySeconds = 3.0) const;
};
