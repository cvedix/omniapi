#include "core/device_report_collector.h"
#include <chrono>
#include <cstdint>
#include <fstream>
#include <future>
#include <sstream>
#include <stdlib.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifdef __linux__
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#endif

#include <drogon/HttpClient.h>

namespace {

constexpr int kPublicIpCacheSec = 300;  // 5 min

std::string trim(const std::string &s) {
  auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end == std::string::npos ? std::string::npos : end - start + 1);
}

}  // namespace

DeviceReportSnapshot DeviceReportCollector::collect() {
  DeviceReportSnapshot s;
  s.hostname = getHostname();
  s.localIp = getLocalIp();
  s.publicIp = getPublicIp();
  s.macAddress = getMacAddress();
  s.osVersion = getOsVersion();
  s.kernelVersion = getKernelVersion();
  s.cpuModel = getCpuModel();
  s.cpuCores = getCpuCores();
  s.cpuUsage = getCpuUsage();
  getMemInfo(s.memTotalMB, s.memAvailableMB, s.memUsage);
  getDiskInfo(s.diskTotalGB, s.diskUsedGB, s.diskAvailableGB, s.diskUsage);
  s.uptimeSeconds = getUptimeSeconds();
  s.loadAvg = getLoadAvg();
  s.activeStreams = active_streams_;
  return s;
}

std::string DeviceReportCollector::getHostname() {
  char buf[256];
  if (gethostname(buf, sizeof(buf)) == 0)
    return std::string(buf);
  return "";
}

std::string DeviceReportCollector::getLocalIp() {
#ifdef __linux__
  struct ifaddrs *ifa = nullptr;
  if (getifaddrs(&ifa) != 0) return "";
  std::string result;
  for (struct ifaddrs *p = ifa; p; p = p->ifa_next) {
    if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
    if (std::string(p->ifa_name).find("lo") == 0) continue;
    void *addr = &((struct sockaddr_in *)p->ifa_addr)->sin_addr;
    char buf[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, addr, buf, sizeof(buf))) {
      std::string ip(buf);
      if (ip != "127.0.0.1" && !ip.empty()) {
        result = ip;
        break;
      }
    }
  }
  freeifaddrs(ifa);
  return result;
#else
  return "";
#endif
}

std::string DeviceReportCollector::getPublicIp() {
  static std::string cached;
  static std::chrono::steady_clock::time_point cached_time;
  auto now = std::chrono::steady_clock::now();
  if (!cached.empty() &&
      std::chrono::duration_cast<std::chrono::seconds>(now - cached_time).count() < kPublicIpCacheSec)
    return cached;

  try {
    auto client = drogon::HttpClient::newHttpClient("https://api.ipify.org", 5.0);
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    std::promise<std::string> prom;
    auto fut = prom.get_future();
    client->sendRequest(req, [&prom](drogon::ReqResult r, const drogon::HttpResponsePtr &resp) {
      if (r == drogon::ReqResult::Ok && resp && resp->getStatusCode() == drogon::k200OK)
        prom.set_value(std::string(resp->getBody()));
      else
        prom.set_value("");
    });
    if (fut.wait_for(std::chrono::seconds(6)) == std::future_status::ready) {
      cached = trim(fut.get());
      if (!cached.empty()) cached_time = now;
      return cached;
    }
  } catch (...) {}
  return cached.empty() ? "" : cached;
}

std::string DeviceReportCollector::getMacAddress() {
#ifdef __linux__
  std::ifstream f("/sys/class/net/eth0/address");
  if (!f) {
    f.open("/sys/class/net/enp0s3/address");
  }
  if (f) {
    std::string line;
    if (std::getline(f, line)) return trim(line);
  }
#endif
  return "";
}

std::string DeviceReportCollector::getOsVersion() {
  std::ifstream f("/etc/os-release");
  if (!f) return "";
  std::string line;
  while (std::getline(f, line)) {
    if (line.find("PRETTY_NAME=") == 0) {
      std::string v = line.substr(12);
      if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
        v = v.substr(1, v.size() - 2);
      return trim(v);
    }
  }
  return "";
}

std::string DeviceReportCollector::getKernelVersion() {
  struct utsname u;
  if (uname(&u) == 0) return std::string(u.release);
  return "";
}

std::string DeviceReportCollector::getCpuModel() {
  std::ifstream f("/proc/cpuinfo");
  if (!f) return "";
  std::string line;
  while (std::getline(f, line)) {
    if (line.find("model name") != std::string::npos) {
      size_t colon = line.find(':');
      if (colon != std::string::npos)
        return trim(line.substr(colon + 1));
    }
  }
  return "";
}

int DeviceReportCollector::getCpuCores() {
  return static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
}

double DeviceReportCollector::getCpuUsage() {
  std::ifstream f("/proc/stat");
  if (!f) return 0.0;
  std::string line;
  if (!std::getline(f, line) || line.find("cpu ") != 0) return 0.0;
  std::istringstream iss(line.substr(5));
  uint64_t user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
  if (!(iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal >> guest >> guest_nice))
    return 0.0;
  uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal + guest + guest_nice;
  uint64_t idle_all = idle + iowait;
  if (total == 0) return 0.0;
  return 100.0 * (1.0 - static_cast<double>(idle_all) / static_cast<double>(total));
}

void DeviceReportCollector::getMemInfo(int &totalMB, int &availableMB, double &usagePercent) {
  totalMB = availableMB = 0;
  usagePercent = 0.0;
  std::ifstream f("/proc/meminfo");
  if (!f) return;
  uint64_t totalKb = 0, availableKb = 0;
  std::string line;
  while (std::getline(f, line)) {
    if (line.find("MemTotal:") == 0) {
      std::istringstream iss(line.substr(9));
      iss >> totalKb;
    } else if (line.find("MemAvailable:") == 0) {
      std::istringstream iss(line.substr(13));
      iss >> availableKb;
    }
  }
  totalMB = static_cast<int>(totalKb / 1024);
  availableMB = static_cast<int>(availableKb / 1024);
  if (totalMB > 0)
    usagePercent = 100.0 * (1.0 - static_cast<double>(availableMB) / static_cast<double>(totalMB));
}

void DeviceReportCollector::getDiskInfo(int &totalGB, int &usedGB, int &availableGB, double &usagePercent) {
  totalGB = usedGB = availableGB = 0;
  usagePercent = 0.0;
  struct statvfs s;
  if (statvfs("/", &s) != 0) return;
  uint64_t total = s.f_blocks * s.f_frsize;
  uint64_t avail = s.f_bavail * s.f_frsize;
  totalGB = static_cast<int>(total / (1024 * 1024 * 1024));
  availableGB = static_cast<int>(avail / (1024 * 1024 * 1024));
  usedGB = totalGB - availableGB;
  if (totalGB > 0)
    usagePercent = 100.0 * (1.0 - static_cast<double>(availableGB) / static_cast<double>(totalGB));
}

int64_t DeviceReportCollector::getUptimeSeconds() {
  std::ifstream f("/proc/uptime");
  if (!f) return 0;
  double up;
  if (f >> up) return static_cast<int64_t>(up);
  return 0;
}

double DeviceReportCollector::getLoadAvg() {
  double load[3];
  if (getloadavg(load, 3) >= 1) return load[0];
  return 0.0;
}
