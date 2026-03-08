#include "core/runtime_update_log.h"
#include "core/env_config.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

static std::mutex g_runtime_update_log_mutex;

void logRuntimeUpdate(const std::string &instance_id,
                      const std::string &message) {
  std::string logDir =
      EnvConfig::getString("EDGE_AI_RUNTIME_UPDATE_LOG_DIR", "");
  if (logDir.empty()) {
    logDir = EnvConfig::getString("LOG_DIR", "");
  }
  if (logDir.empty()) {
    logDir = "/tmp";
  }
  std::lock_guard<std::mutex> lock(g_runtime_update_log_mutex);
  try {
    if (logDir != "/tmp" && !std::filesystem::exists(logDir)) {
      std::filesystem::create_directories(logDir);
    }
    std::string path = logDir + "/runtime_update.log";
    std::ofstream f(path, std::ios::app);
    if (f.is_open()) {
      auto now = std::chrono::system_clock::now();
      auto t = std::chrono::system_clock::to_time_t(now);
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()) % 1000;
      std::tm *tm = std::localtime(&t);
      f << "[" << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << "."
        << std::setfill('0') << std::setw(3) << ms.count() << "]"
        << "[" << instance_id << "] " << message << std::endl;
      f.flush();
    }
  } catch (...) {
    // ignore
  }
}
