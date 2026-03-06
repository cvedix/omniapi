#include "api/license_handler.h"
#include "core/metrics_interceptor.h"
#include <drogon/HttpResponse.h>
#include <json/json.h>

#ifdef CVEDIX_WITH_LICENSE
#include <cvedix/utils/license/cvedix_license_manager.h>
#endif

void LicenseHandler::checkLicense(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    Json::Value response;

#ifdef CVEDIX_WITH_LICENSE
    try {
      auto &licenseManager = cvedix_utils::cvedix_license_manager::get_instance();
      bool isValid = licenseManager.check_license();
      
      response["valid"] = isValid;
      response["checked"] = true;
      response["license_path"] = licenseManager.get_license_path();
      
      if (!isValid) {
        response["message"] = "License is invalid or expired";
      } else {
        response["message"] = "License is valid";
      }
    } catch (const std::exception &e) {
      response["valid"] = false;
      response["checked"] = false;
      response["error"] = e.what();
      response["message"] = "Failed to check license";
    }
#else
    response["valid"] = false;
    response["checked"] = false;
    response["message"] = "License management is not compiled (CVEDIX_WITH_LICENSE not enabled)";
    response["error"] = "License management not available";
#endif

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    auto errorResp = createErrorResponse(k500InternalServerError,
                                         "Internal server error", e.what());
    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, errorResp, std::move(callback));
  }
}

void LicenseHandler::getLicenseInfo(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    Json::Value response;

#ifdef CVEDIX_WITH_LICENSE
    try {
      auto &licenseManager = cvedix_utils::cvedix_license_manager::get_instance();
      bool isValid = licenseManager.is_licensed();
      std::string licensePath = licenseManager.get_license_path();
      
      response["valid"] = isValid;
      response["license_path"] = licensePath;
      response["available"] = true;
      
      if (!licensePath.empty()) {
        response["message"] = "License information retrieved successfully";
      } else {
        response["message"] = "License path not found";
      }
    } catch (const std::exception &e) {
      response["valid"] = false;
      response["available"] = false;
      response["error"] = e.what();
      response["message"] = "Failed to get license information";
    }
#else
    response["valid"] = false;
    response["available"] = false;
    response["message"] = "License management is not compiled (CVEDIX_WITH_LICENSE not enabled)";
    response["error"] = "License management not available";
#endif

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    auto errorResp = createErrorResponse(k500InternalServerError,
                                         "Internal server error", e.what());
    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, errorResp, std::move(callback));
  }
}

void LicenseHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
  resp->addHeader("Access-Control-Max-Age", "86400");
  callback(resp);
}

HttpResponsePtr LicenseHandler::createErrorResponse(HttpStatusCode code,
                                                     const std::string &message,
                                                     const std::string &details) {
  Json::Value error;
  error["error"] = message;
  if (!details.empty()) {
    error["details"] = details;
  }

  auto resp = HttpResponse::newHttpJsonResponse(error);
  resp->setStatusCode(code);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  return resp;
}

