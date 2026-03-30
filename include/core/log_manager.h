#pragma once

#include "core/env_config.h"
#include "core/logging_flags.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <plog/Appenders/IAppender.h>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

class DailyPrefixAppender;

struct LogManagerInitParams {
  std::string resolved_log_root;
  size_t max_file_bytes = 104857600;
  int max_disk_usage_percent = 85;
  int cleanup_interval_hours = 24;
  int suspend_disk_percent = 95;
  int resume_disk_percent = 88;
  int retention_days = 30;
};

class LogManager {
public:
  enum class Category {
    API,
    INSTANCE,
    SDK_OUTPUT,
    GENERAL
  };

  static void init(const LogManagerInitParams &params);

  static plog::IAppender *getAppender(Category category);

  /**
   * @brief Get the base log directory actually in use at runtime (resolved at init).
   * Use this to know where log files are being written; may differ from config if init ran before config load.
   */
  static std::string getBaseDir();

  /**
   * @brief Get log directory for a category
   */
  static std::string getCategoryDir(Category category);

  static std::string getCurrentLogFile(Category category);

  static void startCleanupThread();

  static void stopCleanupThread();

  static void performCleanup();

  static double getDiskUsagePercent(const std::string &path);

  static uint64_t getDirectorySize(const std::string &dir_path);

  static std::vector<std::pair<std::string, uint64_t>>
  listLogFiles(Category category);

  struct InstanceLogFileEntry {
    std::string instance_id;
    std::string date;
    uint64_t size = 0;
    std::string path;
  };
  static std::vector<InstanceLogFileEntry> listInstanceLogTree();

  static std::string getLogFilePath(Category category,
                                    const std::string &date_str);

  /**
   * @brief Get log directory for a specific instance (logs/instance/<instance_id>/).
   */
  static std::string getInstanceLogDir(const std::string &instance_id);

  /**
   * @brief Get log file path for an instance and date.
   */
  static std::string getInstanceLogPath(const std::string &instance_id,
                                        const std::string &date_str);

  /**
   * @brief Write one log line to instance-specific log file. Creates directory if needed.
   */
  static void writeInstanceLog(const std::string &instance_id,
                               const std::string &level,
                               const std::string &message);

private:
  static std::string base_dir_;
  static int max_disk_usage_percent_;
  static int cleanup_interval_hours_;
  static int retention_days_;

  static std::unique_ptr<DailyPrefixAppender> api_appender_;
  static std::unique_ptr<DailyPrefixAppender> sdk_output_appender_;
  static std::unique_ptr<DailyPrefixAppender> general_appender_;

  static std::unique_ptr<std::thread> cleanup_thread_;
  static std::atomic<bool> cleanup_running_;
  static std::mutex cleanup_mutex_;

  static void createDirectories();

  static std::string getDateString();

  static void cleanupOldLogs();

  static void cleanupOnLowDiskSpace();

  static void cleanupThreadFunc();

  static void deleteOldFiles(const std::string &dir_path, int days_old);

  static void deleteOldFilesRecursiveInstance(const std::string &instance_root,
                                              int days_old);

  static int getFileAgeDays(const fs::path &file_path);
};
