#include "core/log_manager.h"
#include "core/daily_prefix_appender.h"
#include "core/env_config.h"
#include "core/instance_file_logger.h"
#include "core/log_disk_guard.h"
#include <algorithm>
#include <ctime>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/statvfs.h>
#include <unistd.h>

namespace fs = std::filesystem;

std::string LogManager::base_dir_ = "./logs";
int LogManager::max_disk_usage_percent_ = 85;
int LogManager::cleanup_interval_hours_ = 24;
int LogManager::retention_days_ = 30;

std::unique_ptr<DailyPrefixAppender> LogManager::api_appender_;
std::unique_ptr<DailyPrefixAppender> LogManager::sdk_output_appender_;
std::unique_ptr<DailyPrefixAppender> LogManager::general_appender_;

std::unique_ptr<std::thread> LogManager::cleanup_thread_;
std::atomic<bool> LogManager::cleanup_running_{false};
std::mutex LogManager::cleanup_mutex_;

void LogManager::init(const LogManagerInitParams &params) {
  base_dir_ = params.resolved_log_root;
  if (base_dir_.empty()) {
    base_dir_ = EnvConfig::resolveDataDir("LOG_DIR", "logs");
  }

  max_disk_usage_percent_ =
      std::clamp(params.max_disk_usage_percent, 50, 95);
  cleanup_interval_hours_ =
      std::clamp(params.cleanup_interval_hours, 1, 168);
  retention_days_ = std::clamp(params.retention_days, 1, 365);

  LogDiskGuard::configure(base_dir_, params.suspend_disk_percent,
                          params.resume_disk_percent);

  try {
    base_dir_ = fs::weakly_canonical(fs::path(base_dir_)).string();
  } catch (...) {
  }

  createDirectories();

  const size_t max_sz = std::max(size_t(1024 * 1024), params.max_file_bytes);

  api_appender_.reset();
  sdk_output_appender_.reset();
  general_appender_.reset();

  if (isApiLoggingEnabled()) {
    api_appender_ = std::make_unique<DailyPrefixAppender>(
        getCategoryDir(Category::API), DailyPrefixAppender::Mode::ApiLines,
        max_sz);
  }
  if (isSdkOutputLoggingEnabled()) {
    sdk_output_appender_ = std::make_unique<DailyPrefixAppender>(
        getCategoryDir(Category::SDK_OUTPUT),
        DailyPrefixAppender::Mode::SdkLines, max_sz);
  }
  general_appender_ = std::make_unique<DailyPrefixAppender>(
      getCategoryDir(Category::GENERAL),
      DailyPrefixAppender::Mode::GeneralLines, max_sz);

  InstanceFileLogger::init(getCategoryDir(Category::INSTANCE), max_sz);

  {
    std::lock_guard<std::mutex> lock(cleanup_mutex_);
    startCleanupThread();
  }
}

plog::IAppender *LogManager::getAppender(Category category) {
  switch (category) {
  case Category::API:
    return api_appender_.get();
  case Category::INSTANCE:
    return nullptr;
  case Category::SDK_OUTPUT:
    return sdk_output_appender_.get();
  case Category::GENERAL:
  default:
    return general_appender_.get();
  }
}

std::string LogManager::getCategoryDir(Category category) {
  std::string category_dir = base_dir_;
  if (!base_dir_.empty() && base_dir_.back() != '/') {
    category_dir += "/";
  }
  switch (category) {
  case Category::API:
    category_dir += "api";
    break;
  case Category::INSTANCE:
    category_dir += "instance";
    break;
  case Category::SDK_OUTPUT:
    category_dir += "sdk_output";
    break;
  case Category::GENERAL:
  default:
    category_dir += "general";
    break;
  }
  return category_dir;
}

std::string LogManager::getCurrentLogFile(Category category) {
  if (category == Category::INSTANCE) {
    return getCategoryDir(category) + "/<instance_id>/" + getDateString() +
           ".log";
  }
  return getLogFilePath(category, getDateString());
}

void LogManager::startCleanupThread() {
  if (cleanup_running_.load()) {
    return;
  }
  cleanup_running_ = true;
  cleanup_thread_ = std::make_unique<std::thread>(cleanupThreadFunc);
}

void LogManager::stopCleanupThread() {
  cleanup_running_ = false;
  if (cleanup_thread_ && cleanup_thread_->joinable()) {
    auto thread_ptr = cleanup_thread_.get();
    auto future =
        std::async(std::launch::async, [thread_ptr]() { thread_ptr->join(); });
    if (future.wait_for(std::chrono::seconds(1)) ==
        std::future_status::timeout) {
      std::cerr << "[LogManager] Warning: Cleanup thread join timeout\n";
      cleanup_thread_->detach();
    }
  }
  cleanup_thread_.reset();
}

void LogManager::performCleanup() {
  std::lock_guard<std::mutex> lock(cleanup_mutex_);
  LogDiskGuard::checkNow();
  cleanupOldLogs();
  cleanupOnLowDiskSpace();
}

double LogManager::getDiskUsagePercent(const std::string &path) {
  struct statvfs stat {};
  if (statvfs(path.c_str(), &stat) != 0) {
    return -1.0;
  }
  uint64_t total = stat.f_blocks * stat.f_frsize;
  uint64_t available = stat.f_bavail * stat.f_frsize;
  if (total == 0) {
    return -1.0;
  }
  return (1.0 - static_cast<double>(available) / total) * 100.0;
}

uint64_t LogManager::getDirectorySize(const std::string &dir_path) {
  uint64_t total_size = 0;
  try {
    if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
      return 0;
    }
    for (const auto &entry : fs::recursive_directory_iterator(dir_path)) {
      if (fs::is_regular_file(entry)) {
        total_size += fs::file_size(entry);
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "[LogManager] Error directory size: " << e.what() << std::endl;
  }
  return total_size;
}

void LogManager::createDirectories() {
  try {
    base_dir_ = EnvConfig::resolveDirectory(base_dir_, "logs");
    fs::create_directories(base_dir_);
    for (auto c :
         {Category::API, Category::INSTANCE, Category::SDK_OUTPUT,
          Category::GENERAL}) {
      std::string d = getCategoryDir(c);
      d = EnvConfig::resolveDirectory(
          d, std::string("logs/") +
                 (c == Category::API          ? "api"
                  : c == Category::INSTANCE   ? "instance"
                  : c == Category::SDK_OUTPUT ? "sdk_output"
                                              : "general"));
      fs::create_directories(d);
    }
  } catch (const std::exception &e) {
    std::cerr << "[LogManager] Warning: mkdir: " << e.what() << std::endl;
  }
}

std::string LogManager::getDateString() {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm {};
  localtime_r(&time_t, &tm);
  std::ostringstream oss;
  oss << std::setfill('0') << std::setw(4) << (tm.tm_year + 1900) << "-"
      << std::setw(2) << (tm.tm_mon + 1) << "-" << std::setw(2) << tm.tm_mday;
  return oss.str();
}

std::string LogManager::getLogFilePath(Category category,
                                       const std::string &date_str) {
  return getCategoryDir(category) + "/" + date_str + ".log";
}

void LogManager::cleanupOldLogs() {
  int days =
      EnvConfig::getInt("LOG_RETENTION_DAYS", retention_days_, 1, 365);
  std::vector<std::string> flat = {
      getCategoryDir(Category::API), getCategoryDir(Category::SDK_OUTPUT),
      getCategoryDir(Category::GENERAL)};
  for (const auto &d : flat) {
    try {
      deleteOldFiles(d, days);
    } catch (const std::exception &e) {
      std::cerr << "[LogManager] cleanup " << d << ": " << e.what() << std::endl;
    }
  }
  try {
    deleteOldFilesRecursiveInstance(getCategoryDir(Category::INSTANCE), days);
  } catch (const std::exception &e) {
    std::cerr << "[LogManager] instance cleanup: " << e.what() << std::endl;
  }
}

void LogManager::deleteOldFilesRecursiveInstance(
    const std::string &instance_root, int days_old) {
  if (!fs::exists(instance_root) || !fs::is_directory(instance_root)) {
    return;
  }
  for (const auto &entry : fs::directory_iterator(instance_root)) {
    if (!fs::is_directory(entry)) {
      continue;
    }
    try {
      for (const auto &f : fs::directory_iterator(entry.path())) {
        if (!fs::is_regular_file(f)) {
          continue;
        }
        std::string fn = f.path().filename().string();
        bool ok = (fn.size() >= 4 && fn.substr(fn.size() - 4) == ".log");
        if (ok) {
          int age = getFileAgeDays(f.path());
          if (age > days_old) {
            fs::remove(f.path());
          }
        }
      }
    } catch (...) {
    }
  }
}

void LogManager::cleanupOnLowDiskSpace() {
  double disk_usage = getDiskUsagePercent(base_dir_);
  if (disk_usage < 0) {
    return;
  }
  if (disk_usage >= max_disk_usage_percent_) {
    std::cerr << "[LogManager] Disk " << disk_usage << "% — aggressive cleanup\n";
    int days = 7;
    deleteOldFiles(getCategoryDir(Category::API), days);
    deleteOldFiles(getCategoryDir(Category::SDK_OUTPUT), days);
    deleteOldFiles(getCategoryDir(Category::GENERAL), days);
    deleteOldFilesRecursiveInstance(getCategoryDir(Category::INSTANCE), days);
  }
}

void LogManager::cleanupThreadFunc() {
  int disk_check_ticks = 0;
  while (cleanup_running_.load()) {
    int total_seconds = cleanup_interval_hours_ * 3600;
    for (int i = 0; i < total_seconds && cleanup_running_.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      if (++disk_check_ticks >= 60) {
        disk_check_ticks = 0;
        LogDiskGuard::checkNow();
      }
    }
    if (!cleanup_running_.load()) {
      break;
    }
    performCleanup();
  }
}

void LogManager::deleteOldFiles(const std::string &dir_path, int days_old) {
  if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
    return;
  }
  try {
    for (const auto &entry : fs::directory_iterator(dir_path)) {
      if (fs::is_regular_file(entry)) {
        if (getFileAgeDays(entry.path()) > days_old) {
          fs::remove(entry.path());
        }
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "[LogManager] deleteOldFiles: " << e.what() << std::endl;
  }
}

int LogManager::getFileAgeDays(const fs::path &file_path) {
  try {
    auto file_time = fs::last_write_time(file_path);
    auto now = std::chrono::system_clock::now();
    auto file_time_tp =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            file_time - fs::file_time_type::clock::now() + now);
    auto duration = now - file_time_tp;
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::hours>(duration).count() / 24);
  } catch (...) {
    return -1;
  }
}

std::vector<std::pair<std::string, uint64_t>>
LogManager::listLogFiles(Category category) {
  std::vector<std::pair<std::string, uint64_t>> files;
  if (category == Category::INSTANCE) {
    auto tree = listInstanceLogTree();
    for (const auto &e : tree) {
      files.push_back({e.instance_id + "/" + e.date, e.size});
    }
    std::sort(files.begin(), files.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });
    return files;
  }
  try {
    std::string category_dir = getCategoryDir(category);
    if (!fs::exists(category_dir) || !fs::is_directory(category_dir)) {
      return files;
    }
    for (const auto &entry : fs::directory_iterator(category_dir)) {
      if (!fs::is_regular_file(entry)) {
        continue;
      }
      std::string filename = entry.path().filename().string();
      if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".log") {
        std::string base = filename.substr(0, filename.size() - 4);
        size_t dot = base.find('.');
        if (dot != std::string::npos) {
          base = base.substr(0, dot);
        }
        if (base.size() == 10 && base[4] == '-' && base[7] == '-') {
          try {
            files.push_back({base, fs::file_size(entry.path())});
          } catch (...) {
          }
        }
      }
    }
    std::sort(files.begin(), files.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });
  } catch (const std::exception &e) {
    std::cerr << "[LogManager] listLogFiles: " << e.what() << std::endl;
  }
  return files;
}

std::vector<LogManager::InstanceLogFileEntry>
LogManager::listInstanceLogTree() {
  std::vector<InstanceLogFileEntry> out;
  std::string root = getCategoryDir(Category::INSTANCE);
  try {
    if (!fs::exists(root) || !fs::is_directory(root)) {
      return out;
    }
    for (const auto &inst : fs::directory_iterator(root)) {
      if (!fs::is_directory(inst)) {
        continue;
      }
      std::string iid = inst.path().filename().string();
      for (const auto &f : fs::directory_iterator(inst.path())) {
        if (!fs::is_regular_file(f)) {
          continue;
        }
        std::string fn = f.path().filename().string();
        if (fn.size() < 5 || fn.substr(fn.size() - 4) != ".log") {
          continue;
        }
        std::string base = fn.substr(0, fn.size() - 4);
        size_t dot = base.find('.');
        if (dot != std::string::npos) {
          base = base.substr(0, dot);
        }
        if (base.size() != 10 || base[4] != '-' || base[7] != '-') {
          continue;
        }
        InstanceLogFileEntry e;
        e.instance_id = iid;
        e.date = base;
        try {
          e.size = fs::file_size(f.path());
        } catch (...) {
          e.size = 0;
        }
        e.path = f.path().string();
        out.push_back(std::move(e));
      }
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
      if (a.instance_id != b.instance_id) {
        return a.instance_id < b.instance_id;
      }
      return a.date > b.date;
    });
  } catch (...) {
  }
  return out;
}
