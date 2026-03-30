#include "core/instance_file_logger.h"
#include "core/log_disk_guard.h"
#include "core/logging_flags.h"
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {
struct InstLogState {
  std::string day;
  int part = 0;
  size_t bytes = 0;
};

std::mutex g_mu;
std::string g_root;
size_t g_max_bytes = 100 * 1024 * 1024;
std::unordered_map<std::string, bool> g_enabled;
std::unordered_map<std::string, InstLogState> g_state;

std::string todayStr() {
  auto tt = std::time(nullptr);
  std::tm tm {};
  localtime_r(&tt, &tm);
  std::ostringstream o;
  o << std::setfill('0') << std::setw(4) << (tm.tm_year + 1900) << "-"
    << std::setw(2) << (tm.tm_mon + 1) << "-" << std::setw(2) << tm.tm_mday;
  return o.str();
}

std::string sevStr(plog::Severity s) { return std::string(plog::severityToString(s)); }
} // namespace

std::string InstanceFileLogger::sanitizeId(const std::string &id) {
  std::string o;
  for (char c : id) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_') {
      o += c;
    }
  }
  return o.empty() ? "unknown" : o;
}

void InstanceFileLogger::init(const std::string &instance_logs_root,
                              size_t max_file_bytes) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_root = instance_logs_root;
  while (!g_root.empty() && g_root.back() == '/') {
    g_root.pop_back();
  }
  g_max_bytes = std::max(size_t(1024 * 1024), max_file_bytes);
}

void InstanceFileLogger::setInstanceFileLogging(const std::string &instance_id,
                                                bool enabled) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_enabled[instance_id] = enabled;
}

bool InstanceFileLogger::isFileLoggingEnabled(const std::string &instance_id) {
  if (!isInstanceLoggingEnabled()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_enabled.find(instance_id);
  if (it == g_enabled.end()) {
    return true;
  }
  return it->second;
}

void InstanceFileLogger::removeInstance(const std::string &instance_id) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_enabled.erase(instance_id);
  g_state.erase(instance_id);
}

void InstanceFileLogger::log(const std::string &instance_id,
                             plog::Severity severity,
                             const std::string &message) {
  if (!isInstanceLoggingEnabled()) {
    return;
  }
  LogDiskGuard::checkNow();
  if (LogDiskGuard::isSuspended()) {
    return;
  }

  const std::string sid = sanitizeId(instance_id);
  const std::string day = todayStr();

  auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm {};
  localtime_r(&tt, &tm);
  std::ostringstream ts;
  ts << std::setfill('0') << std::setw(4) << (tm.tm_year + 1900) << "-"
     << std::setw(2) << (tm.tm_mon + 1) << "-" << std::setw(2) << tm.tm_mday << " "
     << std::setw(2) << tm.tm_hour << ":" << std::setw(2) << tm.tm_min << ":"
     << std::setw(2) << tm.tm_sec;
  const std::string line =
      ts.str() + " " + sevStr(severity) + " [Instance] " + message + "\n";

  std::lock_guard<std::mutex> lock(g_mu);
  auto itEn = g_enabled.find(instance_id);
  if (itEn != g_enabled.end() && !itEn->second) {
    return;
  }
  if (g_root.empty()) {
    return;
  }

  std::string dir = g_root + "/" + sid;
  std::error_code ec;
  fs::create_directories(dir, ec);

  InstLogState &st = g_state[instance_id];
  if (st.day != day) {
    st.day = day;
    st.part = 0;
    st.bytes = 0;
  }

  for (int guard = 0; guard < 5000; ++guard) {
    std::string path =
        dir + "/" + day +
        (st.part == 0 ? ".log" : "." + std::to_string(st.part) + ".log");
    if (st.bytes == 0) {
      if (fs::exists(path, ec)) {
        try {
          st.bytes = static_cast<size_t>(fs::file_size(path));
        } catch (...) {
          st.bytes = 0;
        }
      }
    }
    if (st.bytes >= g_max_bytes && st.bytes > 0) {
      st.part++;
      st.bytes = 0;
      continue;
    }
    if (st.bytes + line.size() > g_max_bytes && st.bytes > 0) {
      st.part++;
      st.bytes = 0;
      continue;
    }
    std::ofstream out(path, std::ios::app | std::ios::out);
    if (!out.is_open()) {
      return;
    }
    out << line;
    out.flush();
    st.bytes += line.size();
    return;
  }
}
