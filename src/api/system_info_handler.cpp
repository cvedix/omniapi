#include "api/system_info_handler.h"
#include "config/system_config.h"
#include "core/metrics_interceptor.h"
#include "core/system_metrics.h"
#include "instances/instance_manager.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <dirent.h>
#include <drogon/HttpResponse.h>
#include <fstream>
#include <hwinfo/hwinfo.h>
#include <json/json.h>
#include <sstream>
#include <thread>
#include <unistd.h>

IInstanceManager *SystemInfoHandler::instance_manager_ = nullptr;

void SystemInfoHandler::setInstanceManager(IInstanceManager *manager) {
  instance_manager_ = manager;
}

// Static variables for CPU usage calculation
static std::chrono::steady_clock::time_point g_last_cpu_check =
    std::chrono::steady_clock::now();
static uint64_t g_last_idle_time = 0;
static uint64_t g_last_total_time = 0;

void SystemInfoHandler::getSystemInfo(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    Json::Value response;

    // Get hardware information from hwinfo
    response["cpu"] = getCPUInfo();
    response["ram"] = getRAMInfo();
    response["gpu"] = getGPUInfo();
    response["disk"] = getDiskInfo();
    response["mainboard"] = getMainboardInfo();
    response["os"] = getOSInfo();
    response["battery"] = getBatteryInfo();

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

void SystemInfoHandler::getSystemStatus(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    Json::Value response;

    // CPU Status
    Json::Value cpuStatus;
    cpuStatus["usage_percent"] = getCPUUsage();

    // Frequency accessor in hwinfo differs by version; keep response stable.
    cpuStatus["current_frequency_mhz"] = -1;

    double temp = getCPUTemperature();
    if (temp > 0) {
      cpuStatus["temperature_celsius"] = temp;
    }
    response["cpu"] = cpuStatus;

    // RAM Status
    Json::Value ramStatus = getMemoryStatus();
    response["ram"] = ramStatus;

    // System Load
    response["load_average"] = getLoadAverage();
    response["uptime_seconds"] = static_cast<Json::Int64>(getSystemUptime());

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

void SystemInfoHandler::getResourceStatus(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);
  try {
    Json::Value response;
    auto &sysConfig = SystemConfig::getInstance();

    Json::Value limits;
    limits["max_running_instances"] = sysConfig.getMaxRunningInstances();
    auto mon = sysConfig.getMonitoringConfig();
    limits["max_cpu_percent"] = mon.maxCpuPercent;
    limits["max_ram_percent"] = mon.maxRamPercent;
    auto perf = sysConfig.getPerformanceConfig();
    limits["thread_num"] = perf.threadNum;        // 0 = auto
    limits["min_threads"] = static_cast<Json::Int>(perf.minThreads);
    limits["max_threads"] = static_cast<Json::Int>(perf.maxThreads);
    response["limits"] = limits;

    Json::Value current;
    int instanceCount = 0;
    if (instance_manager_) {
      instanceCount = instance_manager_->getInstanceCount();
    }
    current["instance_count"] = instanceCount;
    current["cpu_usage_percent"] = SystemMetrics::getSystemCpuUsagePercent();
    current["ram_usage_percent"] = SystemMetrics::getSystemRamUsagePercent();
    response["current"] = current;

    int maxInst = sysConfig.getMaxRunningInstances();
    double cpuPct = current["cpu_usage_percent"].asDouble();
    double ramPct = current["ram_usage_percent"].asDouble();
    Json::Value over;
    over["at_instance_limit"] =
        (maxInst > 0 && instanceCount >= maxInst);
    over["over_cpu_limit"] =
        (mon.maxCpuPercent > 0 && cpuPct >= static_cast<double>(mon.maxCpuPercent));
    over["over_ram_limit"] =
        (mon.maxRamPercent > 0 && ramPct >= static_cast<double>(mon.maxRamPercent));
    response["over_limits"] = over;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    auto errorResp = createErrorResponse(k500InternalServerError,
                                         "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, errorResp, std::move(callback));
  }
}

void SystemInfoHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

  // Record metrics and call callback
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

Json::Value SystemInfoHandler::getCPUInfo() const {
  Json::Value cpuInfo;

  try {
    auto cpus = hwinfo::getAllCPUs();
    if (cpus.empty()) {
      return cpuInfo; // Return empty object
    }

    // Use first CPU (typically there's one CPU with multiple cores)
    const auto &cpu = cpus[0];

    cpuInfo["vendor"] = cpu.vendor();
    cpuInfo["model"] = cpu.modelName();
    cpuInfo["physical_cores"] = cpu.numPhysicalCores();
    cpuInfo["logical_cores"] = cpu.numLogicalCores();
    // Keep compatibility across hwinfo versions that expose different APIs.
    cpuInfo["max_frequency"] = -1;
    cpuInfo["regular_frequency"] = -1;
    cpuInfo["current_frequency"] = -1;
    cpuInfo["min_frequency"] = -1;
    cpuInfo["cache_size"] = static_cast<Json::Int64>(-1);

  } catch (const std::exception &e) {
    // Return empty or partial info on error
    cpuInfo["error"] = e.what();
  }

  return cpuInfo;
}

Json::Value SystemInfoHandler::getRAMInfo() const {
  Json::Value ramInfo;

  try {
    ramInfo["vendor"] = "Unknown";
    ramInfo["model"] = "Unknown";
    ramInfo["name"] = "Physical Memory";
    ramInfo["serial_number"] = "N/A";

    std::ifstream meminfoFile("/proc/meminfo");
    int64_t totalBytes = 0;
    int64_t freeBytes = 0;
    int64_t availableBytes = 0;
    std::string line;
    while (std::getline(meminfoFile, line)) {
      std::istringstream iss(line);
      std::string key, unit;
      int64_t value = 0;
      iss >> key >> value >> unit;
      if (key == "MemTotal:") {
        totalBytes = value * 1024;
      } else if (key == "MemFree:") {
        freeBytes = value * 1024;
      } else if (key == "MemAvailable:") {
        availableBytes = value * 1024;
      }
    }
    ramInfo["size_mib"] = static_cast<Json::Int64>(totalBytes / (1024 * 1024));
    ramInfo["free_mib"] = static_cast<Json::Int64>(freeBytes / (1024 * 1024));
    ramInfo["available_mib"] = static_cast<Json::Int64>(availableBytes / (1024 * 1024));

  } catch (const std::exception &e) {
    ramInfo["error"] = e.what();
  }

  return ramInfo;
}

Json::Value SystemInfoHandler::getGPUInfo() const {
  Json::Value gpuArray(Json::arrayValue);

  try {
    auto gpus = hwinfo::getAllGPUs();

    for (const auto &gpu : gpus) {
      Json::Value gpuInfo;
      gpuInfo["vendor"] = gpu.vendor();
      gpuInfo["model"] = gpu.name();
      gpuInfo["driver_version"] = gpu.driverVersion();

      // Keep compatibility with newer hwinfo naming.
      int64_t memoryBytes = static_cast<int64_t>(gpu.shared_memory_Bytes());
      gpuInfo["memory_mib"] =
          static_cast<Json::Int64>(memoryBytes / (1024 * 1024));

      int64_t freq = static_cast<int64_t>(gpu.frequency_hz() / 1000000ULL);
      gpuInfo["current_frequency"] = static_cast<Json::Int64>(freq);
      gpuInfo["min_frequency"] = -1; // Not available
      gpuInfo["max_frequency"] = -1; // Not available

      gpuArray.append(gpuInfo);
    }

    // If no GPUs found, return empty array
    if (gpus.empty()) {
      // Return empty array, not null
    }

  } catch (const std::exception &e) {
    // Return empty array on error
  }

  return gpuArray;
}

Json::Value SystemInfoHandler::getDiskInfo() const {
  Json::Value diskArray(Json::arrayValue);

  try {
    auto disks = hwinfo::getAllDisks();

    for (const auto &disk : disks) {
      Json::Value diskInfo;
      diskInfo["vendor"] = disk.vendor();
      diskInfo["model"] = disk.model();
      diskInfo["serial_number"] = disk.serial_number();
      diskInfo["size_bytes"] = static_cast<Json::Int64>(-1);
      diskInfo["free_size_bytes"] = static_cast<Json::Int64>(-1);
      diskInfo["volumes"] = Json::Value(Json::arrayValue);

      diskArray.append(diskInfo);
    }

  } catch (const std::exception &e) {
    // Return empty array on error
  }

  return diskArray;
}

Json::Value SystemInfoHandler::getMainboardInfo() const {
  Json::Value mainboardInfo;

  try {
    hwinfo::MainBoard mainboard;

    mainboardInfo["vendor"] = mainboard.vendor();
    mainboardInfo["name"] = mainboard.name();
    mainboardInfo["version"] = mainboard.version();
    mainboardInfo["serial_number"] = mainboard.serialNumber();

  } catch (const std::exception &e) {
    mainboardInfo["error"] = e.what();
  }

  return mainboardInfo;
}

Json::Value SystemInfoHandler::getOSInfo() const {
  Json::Value osInfo;

  try {
    hwinfo::OS os;

    osInfo["name"] = os.name();
    osInfo["version"] = os.version();
    osInfo["kernel"] = os.kernel();

    // Determine architecture
    if (os.is64bit()) {
      osInfo["architecture"] = "64 bit";
    } else if (os.is32bit()) {
      osInfo["architecture"] = "32 bit";
    } else {
      osInfo["architecture"] = "unknown";
    }

    // Determine endianess
    if (os.isLittleEndian()) {
      osInfo["endianess"] = "little endian";
    } else if (os.isBigEndian()) {
      osInfo["endianess"] = "big endian";
    } else {
      osInfo["endianess"] = "unknown";
    }

    // Short name (extract from full name)
    std::string name = os.name();
    if (name.find("Windows") != std::string::npos) {
      osInfo["short_name"] = "Windows";
    } else if (name.find("Linux") != std::string::npos ||
               name.find("Ubuntu") != std::string::npos) {
      osInfo["short_name"] = "Linux";
    } else if (name.find("macOS") != std::string::npos ||
               name.find("Darwin") != std::string::npos) {
      osInfo["short_name"] = "macOS";
    } else {
      osInfo["short_name"] = "Unknown";
    }

  } catch (const std::exception &e) {
    osInfo["error"] = e.what();
  }

  return osInfo;
}

Json::Value SystemInfoHandler::getBatteryInfo() const {
  Json::Value batteryArray(Json::arrayValue);

  try {
    auto batteries = hwinfo::getAllBatteries();

    for (const auto &battery : batteries) {
      Json::Value batteryInfo;
      batteryInfo["vendor"] = battery.vendor();
      batteryInfo["model"] = battery.model();
      batteryInfo["serial_number"] = battery.serialNumber();
      batteryInfo["technology"] = battery.technology();
      batteryInfo["capacity_mwh"] =
          static_cast<Json::Int64>(battery.energyFull());
      batteryInfo["charging"] = battery.charging();

      batteryArray.append(batteryInfo);
    }

  } catch (const std::exception &e) {
    // Return empty array on error (no batteries is normal for servers/desktops)
  }

  return batteryArray;
}

double SystemInfoHandler::getCPUUsage() const {
  try {
    std::ifstream statFile("/proc/stat");
    if (!statFile.is_open()) {
      return 0.0;
    }

    std::string line;
    if (!std::getline(statFile, line)) {
      return 0.0;
    }

    // Parse cpu line: cpu user nice system idle iowait irq softirq steal guest
    // guest_nice
    std::istringstream iss(line);
    std::string cpuLabel;
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal, guest,
        guest_nice;

    iss >> cpuLabel >> user >> nice >> system >> idle >> iowait >> irq >>
        softirq >> steal >> guest >> guest_nice;

    if (cpuLabel != "cpu") {
      return 0.0;
    }

    // Calculate total time and idle time
    uint64_t idleTime = idle + iowait;
    uint64_t nonIdleTime = user + nice + system + irq + softirq + steal;
    uint64_t totalTime = idleTime + nonIdleTime;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - g_last_cpu_check)
                       .count();

    // Need at least 100ms between samples for accurate calculation
    if (elapsed < 100) {
      // Use cached value or return 0
      if (g_last_total_time > 0) {
        uint64_t totalDiff = totalTime - g_last_total_time;
        uint64_t idleDiff = idleTime - g_last_idle_time;

        if (totalDiff > 0) {
          return (1.0 - static_cast<double>(idleDiff) /
                            static_cast<double>(totalDiff)) *
                 100.0;
        }
      }
      return 0.0;
    }

    // Calculate usage based on difference
    if (g_last_total_time > 0 && g_last_idle_time > 0) {
      uint64_t totalDiff = totalTime - g_last_total_time;
      uint64_t idleDiff = idleTime - g_last_idle_time;

      if (totalDiff > 0) {
        double usage = (1.0 - static_cast<double>(idleDiff) /
                                  static_cast<double>(totalDiff)) *
                       100.0;

        // Update cache
        g_last_cpu_check = now;
        g_last_idle_time = idleTime;
        g_last_total_time = totalTime;

        return std::max(0.0, std::min(100.0, usage));
      }
    }

    // First time or invalid data, update cache and return 0
    g_last_cpu_check = now;
    g_last_idle_time = idleTime;
    g_last_total_time = totalTime;

    return 0.0;

  } catch (const std::exception &) {
    return 0.0;
  }
}

Json::Value SystemInfoHandler::getLoadAverage() const {
  Json::Value loadAvg;

  try {
    std::ifstream loadFile("/proc/loadavg");
    if (!loadFile.is_open()) {
      loadAvg["1min"] = 0.0;
      loadAvg["5min"] = 0.0;
      loadAvg["15min"] = 0.0;
      return loadAvg;
    }

    double load1, load5, load15;
    std::string dummy;
    loadFile >> load1 >> load5 >> load15 >> dummy >> dummy;

    loadAvg["1min"] = load1;
    loadAvg["5min"] = load5;
    loadAvg["15min"] = load15;

  } catch (const std::exception &) {
    loadAvg["1min"] = 0.0;
    loadAvg["5min"] = 0.0;
    loadAvg["15min"] = 0.0;
  }

  return loadAvg;
}

int64_t SystemInfoHandler::getSystemUptime() const {
  try {
    std::ifstream uptimeFile("/proc/uptime");
    if (!uptimeFile.is_open()) {
      return 0;
    }

    double uptime;
    uptimeFile >> uptime;

    return static_cast<int64_t>(uptime);

  } catch (const std::exception &) {
    return 0;
  }
}

double SystemInfoHandler::getCPUTemperature() const {
  try {
    // Try to find thermal zone
    DIR *dir = opendir("/sys/class/thermal");
    if (!dir) {
      return -1.0;
    }

    double maxTemp = -1.0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != nullptr) {
      std::string name = entry->d_name;
      if (name.find("thermal_zone") == 0) {
        std::string tempPath = "/sys/class/thermal/" + name + "/temp";
        std::ifstream tempFile(tempPath);
        if (tempFile.is_open()) {
          int temp;
          tempFile >> temp;
          // Temperature is in millidegrees, convert to Celsius
          double tempC = temp / 1000.0;
          if (tempC > maxTemp) {
            maxTemp = tempC;
          }
        }
      }
    }

    closedir(dir);
    return maxTemp;

  } catch (const std::exception &) {
    return -1.0;
  }
}

Json::Value SystemInfoHandler::getMemoryStatus() const {
  Json::Value memStatus;

  try {
    int64_t totalBytes = 0;
    int64_t freeBytes = 0;
    int64_t availableBytes = 0;
    std::ifstream meminfoFile("/proc/meminfo");
    std::string line;
    while (std::getline(meminfoFile, line)) {
      std::istringstream iss(line);
      std::string key, unit;
      int64_t value = 0;
      iss >> key >> value >> unit;
      if (key == "MemTotal:") {
        totalBytes = value * 1024;
      } else if (key == "MemFree:") {
        freeBytes = value * 1024;
      } else if (key == "MemAvailable:") {
        availableBytes = value * 1024;
      }
    }
    int64_t usedBytes = totalBytes - freeBytes;

    memStatus["total_mib"] =
        static_cast<Json::Int64>(totalBytes / (1024 * 1024));
    memStatus["used_mib"] = static_cast<Json::Int64>(usedBytes / (1024 * 1024));
    memStatus["free_mib"] = static_cast<Json::Int64>(freeBytes / (1024 * 1024));
    memStatus["available_mib"] =
        static_cast<Json::Int64>(availableBytes / (1024 * 1024));

    if (totalBytes > 0) {
      memStatus["usage_percent"] =
          (static_cast<double>(usedBytes) / static_cast<double>(totalBytes)) *
          100.0;
    } else {
      memStatus["usage_percent"] = 0.0;
    }

    // Try to get cached and buffers from /proc/meminfo
    try {
      std::ifstream meminfoFile("/proc/meminfo");
      if (meminfoFile.is_open()) {
        std::string line;
        int64_t cached = 0;
        int64_t buffers = 0;

        while (std::getline(meminfoFile, line)) {
          if (line.find("Cached:") == 0) {
            std::istringstream iss(line);
            std::string label, unit;
            int64_t value;
            iss >> label >> value >> unit;
            cached = value * 1024; // Convert from KB to bytes
          } else if (line.find("Buffers:") == 0) {
            std::istringstream iss(line);
            std::string label, unit;
            int64_t value;
            iss >> label >> value >> unit;
            buffers = value * 1024; // Convert from KB to bytes
          }
        }

        if (cached > 0) {
          memStatus["cached_mib"] =
              static_cast<Json::Int64>(cached / (1024 * 1024));
        }
        if (buffers > 0) {
          memStatus["buffers_mib"] =
              static_cast<Json::Int64>(buffers / (1024 * 1024));
        }
      }
    } catch (...) {
      // Ignore errors reading /proc/meminfo
    }

  } catch (const std::exception &e) {
    memStatus["error"] = e.what();
  }

  return memStatus;
}

HttpResponsePtr
SystemInfoHandler::createErrorResponse(int statusCode, const std::string &error,
                                       const std::string &message) const {
  Json::Value errorResponse;
  errorResponse["error"] = error;
  if (!message.empty()) {
    errorResponse["message"] = message;
  }

  auto resp = HttpResponse::newHttpJsonResponse(errorResponse);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

  return resp;
}
