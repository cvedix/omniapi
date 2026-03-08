#pragma once

#include "config/system_config.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

/**
 * Device Watchdog: send system report to Traccar (OsmAnd) only when server is
 * reachable. Retry connection on failure; no report when unreachable.
 */
class DeviceWatchdog {
public:
  explicit DeviceWatchdog(const SystemConfig::DeviceReportConfig &config);
  ~DeviceWatchdog();

  void start();
  void stop();
  bool isRunning() const { return running_.load(); }

  struct Status {
    bool enabled;
    std::string serverUrl;
    std::string deviceId;
    std::string lastStatus;   // "success" | "network_error" | "http_400" | "disabled" | ""
    int reportCount;
    std::string lastReportTime;
    bool serverReachable;
  };
  Status getStatus() const;

  /** Trigger one report now (event=manual). Returns true if sent. */
  bool reportNow();

private:
  void loop();
  bool checkReachability();
  bool sendReport(const std::string &event);

  SystemConfig::DeviceReportConfig config_;
  std::atomic<bool> running_{false};
  std::unique_ptr<std::thread> thread_;

  mutable std::mutex status_mutex_;
  int report_count_ = 0;
  std::string last_status_;
  std::chrono::system_clock::time_point last_report_time_;
  bool server_reachable_ = false;
};
