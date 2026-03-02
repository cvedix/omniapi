#include "api/health_handler.h"
#include "core/metrics_interceptor.h"
#include <chrono>
#include <ctime>
#include <drogon/HttpResponse.h>
#include <drogon/utils/Utilities.h>
#include <iomanip>
#include <json/json.h>
#include <sstream>

// Static start time for uptime calculation
static std::chrono::steady_clock::time_point g_start_time =
    std::chrono::steady_clock::now();

void HealthHandler::getHealth(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    Json::Value response;

    // Determine health status based on system checks
    std::string status = "healthy";
    Json::Value checks;

    // Check 1: Uptime check (basic availability)
    int64_t uptime = getUptime();
    checks["uptime"] = uptime > 0;

    // Check 2: Memory check (simple heuristic)
    // In production, you might want to check actual memory usage
    // For now, we just check if uptime is reasonable
    if (uptime < 0) {
      status = "unhealthy";
    }

    // Check 3: Service availability
    // Add more checks here as needed (database, external services, etc.)
    checks["service"] = true;

    // Set response
    response["status"] = status;
    response["timestamp"] = getCurrentTimestamp();
    response["uptime"] = static_cast<Json::Int64>(uptime);
    response["service"] = "edgeos-api";
    response["version"] = "2026.0.1.1";
    response["checks"] = checks;

    auto resp = HttpResponse::newHttpJsonResponse(response);

    // Set status code based on health
    if (status == "healthy") {
      resp->setStatusCode(k200OK);
    } else if (status == "degraded") {
      resp->setStatusCode(k200OK); // Still 200, but status indicates degraded
    } else {
      resp->setStatusCode(k503ServiceUnavailable);
    }

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    // Error handling
    Json::Value errorResponse;
    errorResponse["error"] = "Internal server error";
    errorResponse["message"] = e.what();

    auto resp = HttpResponse::newHttpJsonResponse(errorResponse);
    resp->setStatusCode(k500InternalServerError);

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

std::string HealthHandler::getCurrentTimestamp() const {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;

  std::stringstream ss;
  ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
  ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
  ss << "Z";

  return ss.str();
}

int64_t HealthHandler::getUptime() const {
  auto now = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::seconds>(now - g_start_time);
  return duration.count();
}
