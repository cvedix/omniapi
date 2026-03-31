#include "core/device_watchdog.h"
#include "core/device_report_collector.h"
#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <ctime>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

std::string urlEncode(const std::string &s) {
  std::ostringstream out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      out << c;
    else
      out << '%' << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
  }
  return out.str();
}

std::string buildOsmAndQuery(const DeviceReportSnapshot &s,
                             const SystemConfig::DeviceReportConfig &cfg,
                             const std::string &event) {
  std::ostringstream q;
  int64_t ts = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
  std::string id = cfg.deviceId.empty() ? s.hostname : cfg.deviceId;
  if (id.empty()) id = "device";

  q << "id=" << urlEncode(id)
    << "&timestamp=" << ts
    << "&lat=" << cfg.latitude << "&lon=" << cfg.longitude
    << "&altitude=0&speed=0"
    << "&hostname=" << urlEncode(s.hostname)
    << "&deviceType=" << urlEncode(cfg.deviceType)
    << "&serverVersion=OmniAPI"
    << "&macAddress=" << urlEncode(s.macAddress)
    << "&localIp=" << urlEncode(s.localIp)
    << "&publicIp=" << urlEncode(s.publicIp)
    << "&osVersion=" << urlEncode(s.osVersion)
    << "&kernelVersion=" << urlEncode(s.kernelVersion)
    << "&cpuModel=" << urlEncode(s.cpuModel)
    << "&cpuCores=" << s.cpuCores
    << "&cpuUsage=" << std::fixed << std::setprecision(1) << s.cpuUsage
    << "&memTotalMB=" << s.memTotalMB
    << "&memAvailableMB=" << s.memAvailableMB
    << "&memUsage=" << std::fixed << std::setprecision(1) << s.memUsage
    << "&diskTotalGB=" << s.diskTotalGB
    << "&diskUsedGB=" << s.diskUsedGB
    << "&diskAvailableGB=" << s.diskAvailableGB
    << "&diskUsage=" << std::fixed << std::setprecision(1) << s.diskUsage
    << "&uptimeSeconds=" << s.uptimeSeconds
    << "&loadAvg=" << std::fixed << std::setprecision(2) << s.loadAvg
    << "&activeStreams=" << s.activeStreams;
  if (!event.empty())
    q << "&event=" << urlEncode(event);
  return q.str();
}

std::string extractBaseUrl(std::string url) {
  if (url.find("://") == std::string::npos) url = "http://" + url;
  size_t path = url.find('/', url.find("://") + 3);
  if (path != std::string::npos) return url.substr(0, path);
  return url;
}

}  // namespace

DeviceWatchdog::DeviceWatchdog(const SystemConfig::DeviceReportConfig &config)
    : config_(config) {}

DeviceWatchdog::~DeviceWatchdog() { stop(); }

void DeviceWatchdog::start() {
  if (!config_.enabled || config_.serverUrl.empty()) {
    std::cout << "[DeviceWatchdog] Disabled or server URL empty" << std::endl;
    return;
  }
  if (running_.load()) {
    std::cerr << "[DeviceWatchdog] Already running" << std::endl;
    return;
  }
  running_.store(true);
  thread_ = std::make_unique<std::thread>(&DeviceWatchdog::loop, this);
  std::cout << "[DeviceWatchdog] Started (server=" << config_.serverUrl
            << ", interval=" << config_.intervalSec << "s)" << std::endl;
}

void DeviceWatchdog::stop() {
  if (!running_.load()) return;
  running_.store(false);
  if (thread_ && thread_->joinable()) {
    thread_->join();
  }
  std::cout << "[DeviceWatchdog] Stopped" << std::endl;
}

void DeviceWatchdog::loop() {
  const unsigned backoff_sec = 30;
  unsigned wait_sec = 5;

  while (running_.load()) {
    if (!checkReachability()) {
      {
        std::lock_guard<std::mutex> lock(status_mutex_);
        server_reachable_ = false;
        last_status_ = "network_error";
      }
      std::this_thread::sleep_for(std::chrono::seconds(std::min(wait_sec, 300u)));
      wait_sec = std::min(wait_sec + backoff_sec, 300u);
      continue;
    }
    wait_sec = 5;
    {
      std::lock_guard<std::mutex> lock(status_mutex_);
      server_reachable_ = true;
    }
    if (sendReport("startup")) {
      break;
    }
  }

  while (running_.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(config_.intervalSec));
    if (!running_.load()) break;
    if (!checkReachability()) {
      std::lock_guard<std::mutex> lock(status_mutex_);
      server_reachable_ = false;
      last_status_ = "network_error";
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(status_mutex_);
      server_reachable_ = true;
    }
    sendReport("");
  }
}

bool DeviceWatchdog::checkReachability() {
  try {
    std::string url = config_.serverUrl;
    if (url.find("://") == std::string::npos) url = "http://" + url;
    auto client = drogon::HttpClient::newHttpClient(url, static_cast<double>(config_.reachabilityTimeoutSec));
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath("/");
    std::promise<bool> prom;
    auto fut = prom.get_future();
    client->sendRequest(req, [&prom](drogon::ReqResult r, const drogon::HttpResponsePtr &resp) {
      bool ok = (r == drogon::ReqResult::Ok && resp);
      int code = ok ? resp->getStatusCode() : 0;
      prom.set_value(ok && (code == drogon::k200OK || code == drogon::k400BadRequest));
    });
    if (fut.wait_for(std::chrono::seconds(config_.reachabilityTimeoutSec + 2)) == std::future_status::ready)
      return fut.get();
  } catch (...) {}
  return false;
}

bool DeviceWatchdog::sendReport(const std::string &event) {
  DeviceReportCollector collector;
  DeviceReportSnapshot s = collector.collect();
  std::string query = buildOsmAndQuery(s, config_, event);
  std::string base = extractBaseUrl(config_.serverUrl);

  try {
    auto client = drogon::HttpClient::newHttpClient(base, static_cast<double>(config_.reportTimeoutSec));
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath("/?" + query);
    std::promise<std::pair<bool, std::string>> prom;
    auto fut = prom.get_future();
    client->sendRequest(req, [&prom](drogon::ReqResult r, const drogon::HttpResponsePtr &resp) {
      if (r == drogon::ReqResult::Ok && resp) {
        int code = resp->getStatusCode();
        if (code == drogon::k200OK)
          prom.set_value({true, "success"});
        else
          prom.set_value({false, (code == 400 ? "http_400" : "http_error")});
      } else {
        prom.set_value({false, "network_error"});
      }
    });
    if (fut.wait_for(std::chrono::seconds(config_.reportTimeoutSec + 2)) == std::future_status::ready) {
      auto [ok, status] = fut.get();
      std::lock_guard<std::mutex> lock(status_mutex_);
      last_status_ = status;
      if (ok) {
        report_count_++;
        last_report_time_ = std::chrono::system_clock::now();
        return true;
      }
    }
  } catch (const std::exception &e) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    last_status_ = "network_error";
    std::cerr << "[DeviceWatchdog] Send failed: " << e.what() << std::endl;
  }
  return false;
}

bool DeviceWatchdog::reportNow() {
  if (!config_.enabled || config_.serverUrl.empty()) return false;
  if (!checkReachability()) return false;
  return sendReport("manual");
}

DeviceWatchdog::Status DeviceWatchdog::getStatus() const {
  std::lock_guard<std::mutex> lock(status_mutex_);
  Status s;
  s.enabled = config_.enabled;
  s.serverUrl = config_.serverUrl;
  s.deviceId = config_.deviceId;
  s.lastStatus = last_status_;
  s.reportCount = report_count_;
  auto t = last_report_time_;
  if (t.time_since_epoch().count() > 0) {
    std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::ostringstream os;
    os << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
    s.lastReportTime = os.str();
  } else {
    s.lastReportTime.clear();
  }
  s.serverReachable = server_reachable_;
  return s;
}
