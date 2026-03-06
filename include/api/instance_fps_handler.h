#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

using namespace drogon;

// Forward declarations
class IInstanceManager;

/**
 * @brief Instance FPS Management Handler
 *
 * Handles FPS configuration and management for instances.
 *
 * Endpoints:
 * - GET /api/v1/instances/{instance_id}/fps - Get current FPS configuration
 * - POST /api/v1/instances/{instance_id}/fps - Set FPS configuration
 * - DELETE /api/v1/instances/{instance_id}/fps - Reset FPS to default (5 FPS)
 */
class InstanceFpsHandler : public drogon::HttpController<InstanceFpsHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(InstanceFpsHandler::getFps, "/api/v1/instances/{instance_id}/fps", Get);
  ADD_METHOD_TO(InstanceFpsHandler::setFps, "/api/v1/instances/{instance_id}/fps", Post);
  ADD_METHOD_TO(InstanceFpsHandler::resetFps, "/api/v1/instances/{instance_id}/fps", Delete);
  ADD_METHOD_TO(InstanceFpsHandler::handleOptions, "/api/v1/instances/{instance_id}/fps", Options);
  METHOD_LIST_END

  /**
   * @brief Handle GET /api/v1/instances/{instance_id}/fps
   * Retrieves the current FPS configuration for a specific instance
   */
  void getFps(const HttpRequestPtr &req,
              std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /api/v1/instances/{instance_id}/fps
   * Sets or updates the FPS configuration for a specific instance
   */
  void setFps(const HttpRequestPtr &req,
              std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle DELETE /api/v1/instances/{instance_id}/fps
   * Resets the FPS configuration to default (5 FPS)
   */
  void resetFps(const HttpRequestPtr &req,
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

private:
  static IInstanceManager *instance_manager_;

  /**
   * @brief Extract instance ID from request path
   * @param req HTTP request
   * @return Instance ID if found, empty string otherwise
   */
  std::string extractInstanceId(const HttpRequestPtr &req) const;

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
