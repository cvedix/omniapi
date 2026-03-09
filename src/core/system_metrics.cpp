#include "core/system_metrics.h"
#include <chrono>
#include <fstream>
#include <sstream>

namespace SystemMetrics {

namespace {
static std::chrono::steady_clock::time_point s_last_cpu_check =
    std::chrono::steady_clock::now();
static uint64_t s_last_idle = 0;
static uint64_t s_last_total = 0;
}

double getSystemCpuUsagePercent() {
  try {
    std::ifstream f("/proc/stat");
    if (!f.is_open()) return 0.0;
    std::string line;
    if (!std::getline(f, line)) return 0.0;
    std::istringstream iss(line);
    std::string cpu;
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
    iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal >> guest >> guest_nice;
    if (cpu != "cpu") return 0.0;
    uint64_t idle_time = idle + iowait;
    uint64_t non_idle = user + nice + system + irq + softirq + steal;
    uint64_t total = idle_time + non_idle;
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_last_cpu_check).count();
    if (elapsed_ms < 100) {
      if (s_last_total > 0) {
        uint64_t total_diff = total - s_last_total;
        uint64_t idle_diff = idle_time - s_last_idle;
        if (total_diff > 0)
          return (1.0 - static_cast<double>(idle_diff) / static_cast<double>(total_diff)) * 100.0;
      }
      return 0.0;
    }
    if (s_last_total > 0 && s_last_idle > 0) {
      uint64_t total_diff = total - s_last_total;
      uint64_t idle_diff = idle_time - s_last_idle;
      if (total_diff > 0) {
        double pct = (1.0 - static_cast<double>(idle_diff) / static_cast<double>(total_diff)) * 100.0;
        s_last_cpu_check = now;
        s_last_idle = idle_time;
        s_last_total = total;
        if (pct < 0.0) pct = 0.0;
        if (pct > 100.0) pct = 100.0;
        return pct;
      }
    }
    s_last_cpu_check = now;
    s_last_idle = idle_time;
    s_last_total = total;
    return 0.0;
  } catch (...) {
    return 0.0;
  }
}

double getSystemRamUsagePercent() {
  try {
    std::ifstream f("/proc/meminfo");
    if (!f.is_open()) return 0.0;
    uint64_t total_kb = 0;
    uint64_t available_kb = 0;
    std::string line;
    while (std::getline(f, line)) {
      if (line.find("MemTotal:") == 0) {
        std::istringstream iss(line);
        std::string key, unit;
        iss >> key >> total_kb >> unit;
      } else if (line.find("MemAvailable:") == 0) {
        std::istringstream iss(line);
        std::string key, unit;
        iss >> key >> available_kb >> unit;
        break;
      }
    }
    if (total_kb == 0) return 0.0;
    uint64_t used_kb = total_kb > available_kb ? (total_kb - available_kb) : 0;
    return (static_cast<double>(used_kb) / static_cast<double>(total_kb)) * 100.0;
  } catch (...) {
    return 0.0;
  }
}

}
