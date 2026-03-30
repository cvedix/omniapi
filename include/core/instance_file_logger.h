#pragma once

#include <plog/Log.h>
#include <sstream>
#include <string>

class InstanceFileLogger {
public:
  static void init(const std::string &instance_logs_root, size_t max_file_bytes);
  static void setInstanceFileLogging(const std::string &instance_id, bool enabled);
  static bool isFileLoggingEnabled(const std::string &instance_id);
  static void removeInstance(const std::string &instance_id);
  static void log(const std::string &instance_id, plog::Severity severity,
                  const std::string &message);

private:
  static std::string sanitizeId(const std::string &id);
};
