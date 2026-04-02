#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

using namespace drogon;

/**
 * @brief Version endpoint handler
 *
 * Endpoint: GET /v1/securt/version
 * Returns: JSON with version information
 */
class VersionHandler : public drogon::HttpController<VersionHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(VersionHandler::getVersion, "/v1/securt/version", Get);
  METHOD_LIST_END

  /**
   * @brief Handle GET /v1/securt/version
   *
   * @param req HTTP request
   * @param callback Response callback
   */
  void getVersion(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);
};
