#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

using namespace drogon;

/**
 * @brief License Management endpoint handler
 *
 * Endpoints:
 * - GET /v1/core/license/check - Check license validity
 * - GET /v1/core/license/info - Get license information
 */
class LicenseHandler : public drogon::HttpController<LicenseHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(LicenseHandler::checkLicense, "/v1/core/license/check", Get);
  ADD_METHOD_TO(LicenseHandler::getLicenseInfo, "/v1/core/license/info", Get);
  ADD_METHOD_TO(LicenseHandler::handleOptions, "/v1/core/license/check", Options);
  ADD_METHOD_TO(LicenseHandler::handleOptions, "/v1/core/license/info", Options);
  METHOD_LIST_END

  /**
   * @brief Handle GET /v1/core/license/check
   * Returns license validity status
   *
   * @param req HTTP request
   * @param callback Response callback
   */
  void checkLicense(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/license/info
   * Returns license information
   *
   * @param req HTTP request
   * @param callback Response callback
   */
  void getLicenseInfo(const HttpRequestPtr &req,
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
  HttpResponsePtr createErrorResponse(HttpStatusCode code,
                                      const std::string &message,
                                      const std::string &details = "");
};

