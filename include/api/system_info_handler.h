#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

using namespace drogon;

/**
 * @brief System Information endpoint handler
 *
 * Endpoints:
 * - GET /v1/securt/system/info - Hardware information (CPU, GPU, RAM, Disk,
 * Mainboard, OS, Battery)
 * - GET /v1/securt/system/status - System status (CPU usage, RAM usage, load
 * average, etc.)
 */
class SystemInfoHandler : public drogon::HttpController<SystemInfoHandler> {
public:
  static void setInstanceManager(class IInstanceManager *manager);

  METHOD_LIST_BEGIN
  ADD_METHOD_TO(SystemInfoHandler::getSystemInfo, "/v1/securt/system/info", Get);
  ADD_METHOD_TO(SystemInfoHandler::getSystemStatus, "/v1/securt/system/status",
                Get);
  ADD_METHOD_TO(SystemInfoHandler::getResourceStatus,
                "/v1/securt/system/resource-status", Get);
  ADD_METHOD_TO(SystemInfoHandler::handleOptions, "/v1/securt/system/info",
                Options);
  ADD_METHOD_TO(SystemInfoHandler::handleOptions, "/v1/securt/system/status",
                Options);
  ADD_METHOD_TO(SystemInfoHandler::handleOptions,
                "/v1/securt/system/resource-status", Options);
  METHOD_LIST_END

  /**
   * @brief Handle GET /v1/securt/system/info
   * Returns detailed hardware information
   *
   * @param req HTTP request
   * @param callback Response callback
   */
  void getSystemInfo(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/securt/system/status
   * Returns current system status (CPU usage, RAM usage, etc.)
   *
   * @param req HTTP request
   * @param callback Response callback
   */
  void getSystemStatus(const HttpRequestPtr &req,
                       std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/securt/system/resource-status
   * Returns configured limits and current usage (max_running_instances,
   * max_cpu_percent, max_ram_percent) and over-limit flags.
   */
  void getResourceStatus(const HttpRequestPtr &req,
                         std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle OPTIONS request for CORS preflight
   */
  void handleOptions(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

private:
  static class IInstanceManager *instance_manager_;

  /**
   * @brief Get CPU information and convert to JSON
   */
  Json::Value getCPUInfo() const;

  /**
   * @brief Get RAM information and convert to JSON
   */
  Json::Value getRAMInfo() const;

  /**
   * @brief Get GPU information and convert to JSON
   */
  Json::Value getGPUInfo() const;

  /**
   * @brief Get Disk information and convert to JSON
   */
  Json::Value getDiskInfo() const;

  /**
   * @brief Get Mainboard information and convert to JSON
   */
  Json::Value getMainboardInfo() const;

  /**
   * @brief Get OS information and convert to JSON
   */
  Json::Value getOSInfo() const;

  /**
   * @brief Get Battery information and convert to JSON
   */
  Json::Value getBatteryInfo() const;

  /**
   * @brief Get CPU usage percentage
   * Reads from /proc/stat and calculates usage
   */
  double getCPUUsage() const;

  /**
   * @brief Get system load average
   * Reads from /proc/loadavg
   */
  Json::Value getLoadAverage() const;

  /**
   * @brief Get system uptime
   * Reads from /proc/uptime
   */
  int64_t getSystemUptime() const;

  /**
   * @brief Get CPU temperature (if available)
   * Reads from /sys/class/thermal/
   */
  double getCPUTemperature() const;

  /**
   * @brief Get memory info from /proc/meminfo
   */
  Json::Value getMemoryStatus() const;

  /**
   * @brief Create error response
   */
  HttpResponsePtr createErrorResponse(int statusCode, const std::string &error,
                                      const std::string &message = "") const;
};
