#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

using namespace drogon;

// Forward declarations
class SecuRTInstanceManager;
class SecuRTLineManager;

/**
 * @brief SecuRT Line Handler
 *
 * Handles SecuRT line management endpoints.
 *
 * Endpoints:
 * - POST /v1/securt/instance/{instanceId}/line/counting - Create counting line
 * - PUT /v1/securt/instance/{instanceId}/line/counting/{lineId} - Create counting line with ID
 * - POST /v1/securt/instance/{instanceId}/line/crossing - Create crossing line
 * - PUT /v1/securt/instance/{instanceId}/line/crossing/{lineId} - Create crossing line with ID
 * - POST /v1/securt/instance/{instanceId}/line/tailgating - Create tailgating line
 * - PUT /v1/securt/instance/{instanceId}/line/tailgating/{lineId} - Create tailgating line with ID
 * - GET /v1/securt/instance/{instanceId}/lines - Get all lines
 * - DELETE /v1/securt/instance/{instanceId}/lines - Delete all lines
 * - DELETE /v1/securt/instance/{instanceId}/line/{lineId} - Delete line
 */
class SecuRTLineHandler : public drogon::HttpController<SecuRTLineHandler> {
public:
  METHOD_LIST_BEGIN
  // Counting lines
  ADD_METHOD_TO(SecuRTLineHandler::createCountingLine,
                "/v1/securt/instance/{instanceId}/line/counting", Post);
  ADD_METHOD_TO(SecuRTLineHandler::createCountingLineWithId,
                "/v1/securt/instance/{instanceId}/line/counting/{lineId}", Put);
  // Crossing lines
  ADD_METHOD_TO(SecuRTLineHandler::createCrossingLine,
                "/v1/securt/instance/{instanceId}/line/crossing", Post);
  ADD_METHOD_TO(SecuRTLineHandler::createCrossingLineWithId,
                "/v1/securt/instance/{instanceId}/line/crossing/{lineId}", Put);
  // Tailgating lines
  ADD_METHOD_TO(SecuRTLineHandler::createTailgatingLine,
                "/v1/securt/instance/{instanceId}/line/tailgating", Post);
  ADD_METHOD_TO(SecuRTLineHandler::createTailgatingLineWithId,
                "/v1/securt/instance/{instanceId}/line/tailgating/{lineId}", Put);
  // Common operations
  ADD_METHOD_TO(SecuRTLineHandler::getAllLines,
                "/v1/securt/instance/{instanceId}/lines", Get);
  ADD_METHOD_TO(SecuRTLineHandler::deleteAllLines,
                "/v1/securt/instance/{instanceId}/lines", Delete);
  ADD_METHOD_TO(SecuRTLineHandler::deleteLine,
                "/v1/securt/instance/{instanceId}/line/{lineId}", Delete);
  // CORS
  ADD_METHOD_TO(SecuRTLineHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/line/counting", Options);
  ADD_METHOD_TO(SecuRTLineHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/line/counting/{lineId}", Options);
  ADD_METHOD_TO(SecuRTLineHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/line/crossing", Options);
  ADD_METHOD_TO(SecuRTLineHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/line/crossing/{lineId}", Options);
  ADD_METHOD_TO(SecuRTLineHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/line/tailgating", Options);
  ADD_METHOD_TO(SecuRTLineHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/line/tailgating/{lineId}", Options);
  ADD_METHOD_TO(SecuRTLineHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/lines", Options);
  ADD_METHOD_TO(SecuRTLineHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/line/{lineId}", Options);
  METHOD_LIST_END

  /**
   * @brief Handle POST /v1/securt/instance/{instanceId}/line/counting
   * Creates a new counting line
   */
  void createCountingLine(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle PUT /v1/securt/instance/{instanceId}/line/counting/{lineId}
   * Creates a counting line with specific ID
   */
  void createCountingLineWithId(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/securt/instance/{instanceId}/line/crossing
   * Creates a new crossing line
   */
  void createCrossingLine(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle PUT /v1/securt/instance/{instanceId}/line/crossing/{lineId}
   * Creates a crossing line with specific ID
   */
  void createCrossingLineWithId(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/securt/instance/{instanceId}/line/tailgating
   * Creates a new tailgating line
   */
  void createTailgatingLine(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle PUT /v1/securt/instance/{instanceId}/line/tailgating/{lineId}
   * Creates a tailgating line with specific ID
   */
  void createTailgatingLineWithId(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/securt/instance/{instanceId}/lines
   * Gets all lines grouped by type
   */
  void getAllLines(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle DELETE /v1/securt/instance/{instanceId}/lines
   * Deletes all lines for an instance
   */
  void deleteAllLines(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle DELETE /v1/securt/instance/{instanceId}/line/{lineId}
   * Deletes a specific line
   */
  void deleteLine(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle OPTIONS request for CORS preflight
   */
  void handleOptions(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Set instance manager (dependency injection)
   */
  static void setInstanceManager(SecuRTInstanceManager *manager);

  /**
   * @brief Set line manager (dependency injection)
   */
  static void setLineManager(SecuRTLineManager *manager);

private:
  static SecuRTInstanceManager *instance_manager_;
  static SecuRTLineManager *line_manager_;

  /**
   * @brief Extract instance ID from request path
   */
  std::string extractInstanceId(const HttpRequestPtr &req) const;

  /**
   * @brief Extract line ID from request path
   */
  std::string extractLineId(const HttpRequestPtr &req) const;

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
   * @brief Restart instance to apply line changes (if instance is running)
   * @param instanceId Instance ID
   * @return true if restart initiated, false otherwise
   */
  bool restartInstanceForLineUpdate(const std::string &instanceId) const;

  /**
   * @brief Try to update lines at runtime without restart (in-process mode only)
   * @param instanceId Instance ID
   * @return true if update successful, false if fallback to restart needed
   */
  bool updateLinesRuntime(const std::string &instanceId) const;
};

