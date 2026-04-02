#pragma once

#include <chrono>
#include <ctime>
#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <iomanip>
#include <json/json.h>
#include <sstream>

using namespace drogon;

/**
 * @brief Health check endpoint handler
 *
 * Endpoint: GET /v1/securt/health
 * Returns: JSON with status, timestamp, and uptime
 */
class HealthHandler : public drogon::HttpController<HealthHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(HealthHandler::getHealth, "/v1/securt/health", Get);
  METHOD_LIST_END

  /**
   * @brief Handle GET /v1/securt/health
   *
   * @param req HTTP request
   * @param callback Response callback
   */
  void getHealth(const HttpRequestPtr &req,
                 std::function<void(const HttpResponsePtr &)> &&callback);

private:
  /**
   * @brief Get current timestamp in ISO 8601 format
   */
  std::string getCurrentTimestamp() const;

  /**
   * @brief Get system uptime in seconds
   */
  int64_t getUptime() const;
};
