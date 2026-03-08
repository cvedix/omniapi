#include "api/watchdog_handler.h"
#include "config/system_config.h"
#include "core/device_watchdog.h"
#include "core/health_monitor.h"
#include "core/metrics_interceptor.h"
#include "core/watchdog.h"
#include <drogon/HttpResponse.h>
#include <json/json.h>

// Static members initialization
Watchdog *WatchdogHandler::g_watchdog = nullptr;
HealthMonitor *WatchdogHandler::g_health_monitor = nullptr;
DeviceWatchdog *WatchdogHandler::g_device_watchdog = nullptr;
std::function<void()> WatchdogHandler::g_device_report_reload_callback;

namespace {

Json::Value deviceReportConfigToJson(const SystemConfig::DeviceReportConfig &c) {
  Json::Value j;
  j["enabled"] = c.enabled;
  j["server_url"] = c.serverUrl;
  j["device_id"] = c.deviceId;
  j["device_type"] = c.deviceType;
  j["interval_sec"] = static_cast<Json::Int>(c.intervalSec);
  j["latitude"] = c.latitude;
  j["longitude"] = c.longitude;
  j["reachability_timeout_sec"] = static_cast<Json::Int>(c.reachabilityTimeoutSec);
  j["report_timeout_sec"] = static_cast<Json::Int>(c.reportTimeoutSec);
  return j;
}

void jsonToDeviceReportConfig(const Json::Value &j,
                              SystemConfig::DeviceReportConfig &c) {
  if (j.isMember("enabled") && j["enabled"].isBool())
    c.enabled = j["enabled"].asBool();
  if (j.isMember("server_url") && j["server_url"].isString())
    c.serverUrl = j["server_url"].asString();
  if (j.isMember("device_id") && j["device_id"].isString())
    c.deviceId = j["device_id"].asString();
  if (j.isMember("device_type") && j["device_type"].isString())
    c.deviceType = j["device_type"].asString();
  if (j.isMember("interval_sec") && j["interval_sec"].isInt())
    c.intervalSec = static_cast<uint32_t>(j["interval_sec"].asInt());
  if (j.isMember("latitude") && j["latitude"].isDouble())
    c.latitude = j["latitude"].asDouble();
  if (j.isMember("longitude") && j["longitude"].isDouble())
    c.longitude = j["longitude"].asDouble();
  if (j.isMember("reachability_timeout_sec") && j["reachability_timeout_sec"].isInt())
    c.reachabilityTimeoutSec = static_cast<uint32_t>(j["reachability_timeout_sec"].asInt());
  if (j.isMember("report_timeout_sec") && j["report_timeout_sec"].isInt())
    c.reportTimeoutSec = static_cast<uint32_t>(j["report_timeout_sec"].asInt());
}

}  // namespace

void WatchdogHandler::getWatchdogStatus(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);
  try {
    Json::Value response;

    // Watchdog statistics
    if (g_watchdog) {
      auto stats = g_watchdog->getStats();
      Json::Value watchdog_info;
      watchdog_info["running"] = g_watchdog->isRunning();
      watchdog_info["total_heartbeats"] =
          static_cast<Json::Int64>(stats.total_heartbeats);
      watchdog_info["missed_heartbeats"] =
          static_cast<Json::Int64>(stats.missed_heartbeats);
      watchdog_info["recovery_actions"] =
          static_cast<Json::Int64>(stats.recovery_actions);
      watchdog_info["is_healthy"] = stats.is_healthy;

      // Last heartbeat time
      auto last_hb = stats.last_heartbeat;
      auto now = std::chrono::steady_clock::now();
      auto elapsed =
          std::chrono::duration_cast<std::chrono::seconds>(now - last_hb)
              .count();
      watchdog_info["seconds_since_last_heartbeat"] =
          static_cast<Json::Int64>(elapsed);

      response["watchdog"] = watchdog_info;
    } else {
      response["watchdog"] = Json::Value(Json::objectValue);
      response["watchdog"]["error"] = "Watchdog not initialized";
    }

    // Health monitor statistics
    if (g_health_monitor) {
      auto metrics = g_health_monitor->getMetrics();
      Json::Value monitor_info;
      monitor_info["running"] = g_health_monitor->isRunning();
      monitor_info["cpu_usage_percent"] = metrics.cpu_usage_percent;
      monitor_info["memory_usage_mb"] =
          static_cast<Json::Int64>(metrics.memory_usage_mb);
      monitor_info["request_count"] =
          static_cast<Json::Int64>(metrics.request_count);
      monitor_info["error_count"] =
          static_cast<Json::Int64>(metrics.error_count);

      response["health_monitor"] = monitor_info;
    } else {
      response["health_monitor"] = Json::Value(Json::objectValue);
      response["health_monitor"]["error"] = "Health monitor not initialized";
    }

    if (g_device_watchdog) {
      auto ds = g_device_watchdog->getStatus();
      Json::Value dr;
      dr["enabled"] = ds.enabled;
      dr["server"] = ds.serverUrl;
      dr["device_id"] = ds.deviceId;
      dr["report_count"] = ds.reportCount;
      dr["last_report"] = ds.lastReportTime;
      dr["last_status"] = ds.lastStatus;
      dr["server_reachable"] = ds.serverReachable;
      response["device_report"] = dr;
    } else {
      response["device_report"] = Json::Value(Json::objectValue);
      response["device_report"]["enabled"] = false;
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    Json::Value errorResponse;
    errorResponse["error"] = "Internal server error";
    errorResponse["message"] = e.what();

    auto resp = HttpResponse::newHttpJsonResponse(errorResponse);
    resp->setStatusCode(k500InternalServerError);

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void WatchdogHandler::getDeviceReportConfig(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);
  try {
    auto &config = SystemConfig::getInstance();
    auto cfg = config.getDeviceReportConfig();
    Json::Value body = deviceReportConfigToJson(cfg);
    Json::Value meta(Json::objectValue);
    meta["enabled"] = "Bật/tắt gửi report lên server (true/false).";
    meta["server_url"] = "URL server Traccar OsmAnd (vd. http://traccar:5055).";
    meta["device_id"] = "Mã thiết bị trên Traccar; để trống sẽ dùng hostname.";
    meta["device_type"] = "Loại thiết bị (vd. aibox, omnimedia).";
    meta["interval_sec"] = "Chu kỳ gửi report (giây). Mặc định 300 (5 phút).";
    meta["latitude"] = "Vĩ độ (thiết bị cố định).";
    meta["longitude"] = "Kinh độ (thiết bị cố định).";
    meta["reachability_timeout_sec"] = "Timeout kiểm tra server reachable (giây).";
    meta["report_timeout_sec"] = "Timeout gửi report (giây).";
    body["_description"] = meta;

    Json::Value response(Json::objectValue);
    response["config"] = body;
    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    Json::Value err;
    err["error"] = "Internal server error";
    err["message"] = e.what();
    auto resp = HttpResponse::newHttpJsonResponse(err);
    resp->setStatusCode(k500InternalServerError);
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void WatchdogHandler::putDeviceReportConfig(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);
  try {
    auto *body = req->getJsonObject();
    if (!body || !body->isObject()) {
      Json::Value err;
      err["error"] = "Invalid request";
      err["message"] = "Request body must be a JSON object.";
      auto resp = HttpResponse::newHttpJsonResponse(err);
      resp->setStatusCode(k400BadRequest);
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    auto &config = SystemConfig::getInstance();
    Json::Value current = config.getConfigSection("system.monitoring.device_report");
    if (!current.isObject()) {
      auto cfg = config.getDeviceReportConfig();
      current = deviceReportConfigToJson(cfg);
    }
    for (const auto &key : body->getMemberNames()) {
      if (key == "_description") continue;
      current[key] = (*body)[key];
    }
    if (!config.updateConfigSection("system.monitoring.device_report", current)) {
      Json::Value err;
      err["error"] = "Failed to update config";
      auto resp = HttpResponse::newHttpJsonResponse(err);
      resp->setStatusCode(k500InternalServerError);
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }
    config.saveConfig();

    if (g_device_report_reload_callback) {
      g_device_report_reload_callback();
    }

    Json::Value response(Json::objectValue);
    response["message"] = "Device report config updated and applied.";
    response["config"] = deviceReportConfigToJson(config.getDeviceReportConfig());
    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    Json::Value err;
    err["error"] = "Internal server error";
    err["message"] = e.what();
    auto resp = HttpResponse::newHttpJsonResponse(err);
    resp->setStatusCode(k500InternalServerError);
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void WatchdogHandler::watchdogReportNow(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);
  try {
    Json::Value response;
    if (!g_device_watchdog) {
      response["sent"] = false;
      response["error"] = "Device report not enabled";
      auto resp = HttpResponse::newHttpJsonResponse(response);
      resp->setStatusCode(k200OK);
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }
    bool sent = g_device_watchdog->reportNow();
    response["sent"] = sent;
    response["event"] = "manual";
    if (sent) {
      auto st = g_device_watchdog->getStatus();
      response["report_count"] = st.reportCount;
      response["last_report"] = st.lastReportTime;
    } else {
      response["last_status"] = g_device_watchdog->getStatus().lastStatus;
    }
    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    Json::Value errorResponse;
    errorResponse["error"] = "Internal server error";
    errorResponse["message"] = e.what();
    auto resp = HttpResponse::newHttpJsonResponse(errorResponse);
    resp->setStatusCode(k500InternalServerError);
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}
