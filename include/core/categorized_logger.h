#pragma once

#include "core/env_config.h"
#include "core/log_manager.h"
#include "core/logging_flags.h"
#include <algorithm>
#include <memory>
#include <plog/Appenders/ConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>
#include <string>

namespace CategorizedLogger {

inline void init(const LogManagerInitParams &lm, plog::Severity log_level,
                 bool enable_console = true) {

  std::string log_level_str = EnvConfig::getString("LOG_LEVEL", "");
  if (!log_level_str.empty()) {
    std::string upper = log_level_str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    if (upper == "NONE")
      log_level = plog::none;
    else if (upper == "FATAL")
      log_level = plog::fatal;
    else if (upper == "ERROR")
      log_level = plog::error;
    else if (upper == "WARNING" || upper == "WARN")
      log_level = plog::warning;
    else if (upper == "INFO")
      log_level = plog::info;
    else if (upper == "DEBUG")
      log_level = plog::debug;
    else if (upper == "VERBOSE")
      log_level = plog::verbose;
  }

  LogManager::init(lm);

  static plog::ConsoleAppender<plog::TxtFormatter> consoleAppender;

  plog::Logger<0> *logger = nullptr;
  if (enable_console) {
    logger = &plog::init<0>(log_level, &consoleAppender);
  } else {
    logger = &plog::init<0>(log_level);
  }

  if (isApiLoggingEnabled()) {
    auto *a = LogManager::getAppender(LogManager::Category::API);
    if (a) {
      logger->addAppender(a);
    }
  }

  if (isSdkOutputLoggingEnabled()) {
    auto *s = LogManager::getAppender(LogManager::Category::SDK_OUTPUT);
    if (s) {
      logger->addAppender(s);
    }
  }

  auto *g = LogManager::getAppender(LogManager::Category::GENERAL);
  if (g) {
    logger->addAppender(g);
  }

  PLOG_INFO << "========================================";
  PLOG_INFO << "Categorized Logger initialized";
  PLOG_INFO << "Log root: " << lm.resolved_log_root;
  PLOG_INFO << "Max log file size: " << (lm.max_file_bytes / (1024 * 1024))
            << " MB";
  if (isApiLoggingEnabled()) {
    PLOG_INFO << "  API logs: "
              << LogManager::getCategoryDir(LogManager::Category::API);
  }
  if (isInstanceLoggingEnabled()) {
    PLOG_INFO << "  Instance logs: "
              << LogManager::getCategoryDir(LogManager::Category::INSTANCE)
              << "/<instance_id>/YYYY-MM-DD.log";
  }
  if (isSdkOutputLoggingEnabled()) {
    PLOG_INFO << "  SDK output: "
              << LogManager::getCategoryDir(LogManager::Category::SDK_OUTPUT);
  }
  PLOG_INFO << "  General: "
            << LogManager::getCategoryDir(LogManager::Category::GENERAL);
  PLOG_INFO << "Disk suspend logging >= " << lm.suspend_disk_percent
            << "% used, resume <= " << lm.resume_disk_percent << "%";
  PLOG_INFO << "========================================";
}

inline void shutdown() { LogManager::stopCleanupThread(); }

} // namespace CategorizedLogger
