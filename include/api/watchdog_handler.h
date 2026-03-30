#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <functional>
#include <json/json.h>

// Forward declarations
class Watchdog;
class HealthMonitor;
class DeviceWatchdog;

using namespace drogon;

/**
 * @brief Watchdog status and device report config endpoint handler
 *
 * Endpoints:
 *   GET/PUT /v1/core/watchdog/config - Cấu hình device report (bật/tắt, server, chu kỳ, ...)
 *   GET /v1/core/watchdog - Trạng thái watchdog và device report
 *   GET /v1/core/watchdog/report-now - Gửi report thủ công
 */
class WatchdogHandler : public drogon::HttpController<WatchdogHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(WatchdogHandler::getWatchdogStatus, "/v1/core/watchdog", Get);
  ADD_METHOD_TO(WatchdogHandler::getDeviceReportConfig, "/v1/core/watchdog/config", Get);
  ADD_METHOD_TO(WatchdogHandler::putDeviceReportConfig, "/v1/core/watchdog/config", Put);
  ADD_METHOD_TO(WatchdogHandler::watchdogReportNow, "/v1/core/watchdog/report-now", Get);
  METHOD_LIST_END

  void getWatchdogStatus(const HttpRequestPtr &req,
                         std::function<void(const HttpResponsePtr &)> &&callback);
  void getDeviceReportConfig(const HttpRequestPtr &req,
                             std::function<void(const HttpResponsePtr &)> &&callback);
  void putDeviceReportConfig(const HttpRequestPtr &req,
                             std::function<void(const HttpResponsePtr &)> &&callback);
  void watchdogReportNow(const HttpRequestPtr &req,
                         std::function<void(const HttpResponsePtr &)> &&callback);

  static void setWatchdog(Watchdog *watchdog) { g_watchdog = watchdog; }
  static void setHealthMonitor(HealthMonitor *monitor) {
    g_health_monitor = monitor;
  }
  static void setDeviceWatchdog(DeviceWatchdog *device_watchdog) {
    g_device_watchdog = device_watchdog;
  }
  /** Gọi sau khi PUT config để áp dụng ngay (dừng/khởi động lại device report). */
  static void setDeviceReportReloadCallback(std::function<void()> cb) {
    g_device_report_reload_callback = std::move(cb);
  }

private:
  static Watchdog *g_watchdog;
  static HealthMonitor *g_health_monitor;
  static DeviceWatchdog *g_device_watchdog;
  static std::function<void()> g_device_report_reload_callback;
};
