#pragma once

#include "core/env_config.h"
#include "core/logging_flags.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <plog/Appenders/RollingFileAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Log.h>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

/**
 * @brief Log Manager for categorized logging with disk space management
 *
 * Features:
 * - Categorized logs: api/, instance/, sdk_output/
 * - Daily log rotation: YYYY-MM-DD format
 * - Monthly cleanup: auto-delete logs older than 1 month
 * - Disk space monitoring: auto-cleanup when disk is nearly full
 */
class LogManager {
public:
  /**
   * @brief Log categories
   */
  enum class Category {
    API,        // API request/response logs
    INSTANCE,   // Instance execution logs
    SDK_OUTPUT, // SDK output logs
    GENERAL     // General application logs
  };

  /**
   * @brief Initialize log manager
   *
   * @param base_dir Base directory for logs (default: ./logs)
   * @param max_disk_usage_percent Maximum disk usage percentage before cleanup
   * (default: 85%)
   * @param cleanup_interval_hours Cleanup check interval in hours (default: 24)
   */
  static void init(const std::string &base_dir = "",
                   int max_disk_usage_percent = 85,
                   int cleanup_interval_hours = 24);

  /**
   * @brief Get appender for a specific category
   *
   * @param category Log category
   * @return Pointer to RollingFileAppender
   */
  static plog::RollingFileAppender<plog::TxtFormatter> *
  getAppender(Category category);

  /**
   * @brief Get the base log directory actually in use at runtime (resolved at init).
   * Use this to know where log files are being written; may differ from config if init ran before config load.
   */
  static std::string getBaseDir();

  /**
   * @brief Get log directory for a category
   */
  static std::string getCategoryDir(Category category);

  /**
   * @brief Get current log file path for a category
   */
  static std::string getCurrentLogFile(Category category);

  /**
   * @brief Start cleanup thread
   */
  static void startCleanupThread();

  /**
   * @brief Stop cleanup thread
   */
  static void stopCleanupThread();

  /**
   * @brief Perform cleanup (can be called manually)
   */
  static void performCleanup();

  /**
   * @brief Get disk usage percentage
   */
  static double getDiskUsagePercent(const std::string &path);

  /**
   * @brief Get directory size in bytes
   */
  static uint64_t getDirectorySize(const std::string &dir_path);

  /**
   * @brief List all log files in a category
   *
   * @param category Log category
   * @return Vector of log file info (date, size, path)
   */
  static std::vector<std::pair<std::string, uint64_t>>
  listLogFiles(Category category);

  /**
   * @brief Get log file path for category and date
   *
   * @param category Log category
   * @param date_str Date string in YYYY-MM-DD format
   * @return Full path to log file
   */
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

  // Appenders for each category
  static std::unique_ptr<plog::RollingFileAppender<plog::TxtFormatter>>
      api_appender_;
  static std::unique_ptr<plog::RollingFileAppender<plog::TxtFormatter>>
      instance_appender_;
  static std::unique_ptr<plog::RollingFileAppender<plog::TxtFormatter>>
      sdk_output_appender_;
  static std::unique_ptr<plog::RollingFileAppender<plog::TxtFormatter>>
      general_appender_;

  // Cleanup thread
  static std::unique_ptr<std::thread> cleanup_thread_;
  static std::atomic<bool> cleanup_running_;
  static std::mutex cleanup_mutex_;

  /**
   * @brief Create directory structure
   */
  static void createDirectories();

  /**
   * @brief Get date string for today (YYYY-MM-DD)
   */
  static std::string getDateString();

  /**
   * @brief Cleanup old log files (older than 1 month)
   */
  static void cleanupOldLogs();

  /**
   * @brief Cleanup when disk is nearly full
   */
  static void cleanupOnLowDiskSpace();

  /**
   * @brief Cleanup thread function
   */
  static void cleanupThreadFunc();

  /**
   * @brief Delete files older than specified days
   */
  static void deleteOldFiles(const std::string &dir_path, int days_old);

  /**
   * @brief Get file age in days
   */
  static int getFileAgeDays(const fs::path &file_path);
};
