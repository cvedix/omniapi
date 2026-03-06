#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

using namespace drogon;

/**
 * @brief System Configuration Handler
 *
 * Handles system configuration, preferences, and decoder information.
 *
 * Endpoints:
 * - GET /v1/core/system/config - Get system configuration
 * - PUT /v1/core/system/config - Update system configuration
 * - GET /v1/core/system/preferences - Get system preferences
 * - GET /v1/core/system/decoders - Get available decoders
 * - GET /v1/core/system/registry - Get registry key value (optional)
 * - POST /v1/core/system/shutdown - Shutdown system (optional)
 */
class SystemHandler : public drogon::HttpController<SystemHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(SystemHandler::getSystemConfig, "/v1/core/system/config", Get);
  ADD_METHOD_TO(SystemHandler::updateSystemConfig, "/v1/core/system/config", Put);
  ADD_METHOD_TO(SystemHandler::getPreferences, "/v1/core/system/preferences", Get);
  ADD_METHOD_TO(SystemHandler::getDecoders, "/v1/core/system/decoders", Get);
  ADD_METHOD_TO(SystemHandler::getRegistry, "/v1/core/system/registry", Get);
  ADD_METHOD_TO(SystemHandler::shutdown, "/v1/core/system/shutdown", Post);
  ADD_METHOD_TO(SystemHandler::handleOptions, "/v1/core/system/config", Options);
  ADD_METHOD_TO(SystemHandler::handleOptions, "/v1/core/system/preferences", Options);
  ADD_METHOD_TO(SystemHandler::handleOptions, "/v1/core/system/decoders", Options);
  ADD_METHOD_TO(SystemHandler::handleOptions, "/v1/core/system/registry", Options);
  ADD_METHOD_TO(SystemHandler::handleOptions, "/v1/core/system/shutdown", Options);
  METHOD_LIST_END

  /**
   * @brief Handle GET /v1/core/system/config
   * Gets system configuration entities
   */
  void getSystemConfig(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle PUT /v1/core/system/config
   * Updates system configuration
   */
  void updateSystemConfig(const HttpRequestPtr &req,
                         std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/system/preferences
   * Gets system preferences from rtconfig.json
   */
  void getPreferences(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/system/decoders
   * Gets available decoders information
   */
  void getDecoders(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/system/registry
   * Gets registry key value (optional)
   */
  void getRegistry(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/system/shutdown
   * Shutdowns the system (optional)
   */
  void shutdown(const HttpRequestPtr &req,
               std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle OPTIONS request for CORS preflight
   */
  void handleOptions(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);

private:
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
};

