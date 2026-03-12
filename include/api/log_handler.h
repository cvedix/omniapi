#pragma once

#include "config/system_config.h"
#include "core/log_manager.h"
#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <string>
#include <vector>

using namespace drogon;

/**
 * @brief Log endpoint handler
 *
 * Endpoints:
 * - GET /v1/core/log - List all log files by category
 * - GET /v1/core/log/{category} - Get logs of a category with filtering
 * - GET /v1/core/log/{category}/{date} - Get logs of category and specific
 * date with filtering
 */
class LogHandler : public drogon::HttpController<LogHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(LogHandler::listLogFiles, "/v1/core/log", Get);
  ADD_METHOD_TO(LogHandler::getLogConfig, "/v1/core/log/config", Get);
  ADD_METHOD_TO(LogHandler::putLogConfig, "/v1/core/log/config", Put);
  ADD_METHOD_TO(LogHandler::getLogsByCategory, "/v1/core/log/{category}", Get);
  ADD_METHOD_TO(LogHandler::getLogsByCategoryAndDate,
                "/v1/core/log/{category}/{date}", Get);
  ADD_METHOD_TO(LogHandler::handleOptions, "/v1/core/log", Options);
  ADD_METHOD_TO(LogHandler::handleOptions, "/v1/core/log/config", Options);
  ADD_METHOD_TO(LogHandler::handleOptions, "/v1/core/log/{category}", Options);
  ADD_METHOD_TO(LogHandler::handleOptions, "/v1/core/log/{category}/{date}",
                Options);
  METHOD_LIST_END

  /**
   * @brief Handle GET /v1/core/log
   * List all log files organized by category
   */
  void listLogFiles(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/log/config
   * Get current logging configuration (enabled, log_level, per-category flags).
   */
  void getLogConfig(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle PUT /v1/core/log/config
   * Update logging configuration; category flags apply immediately.
   */
  void putLogConfig(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/log/{category}
   * Get logs of a category with optional filtering
   * Query params: level, from, to, tail
   *
   * @param req HTTP request
   * @param callback Response callback
   */
  void
  getLogsByCategory(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/log/{category}/{date}
   * Get logs of category and specific date with optional filtering
   * Query params: level, from, to, tail
   *
   * @param req HTTP request
   * @param callback Response callback
   */
  void getLogsByCategoryAndDate(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle OPTIONS request for CORS preflight
   */
  void handleOptions(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

private:
  /**
   * @brief Parse a log line from plog format
   * Format: YYYY-MM-DD HH:MM:SS.mmm [LEVEL] message
   *
   * @param line Log line to parse
   * @return Json::Value with timestamp, level, message, or empty if invalid
   */
  Json::Value parseLogLine(const std::string &line) const;

  /**
   * @brief Filter logs by level and time range
   *
   * @param logs Vector of log entries
   * @param level_filter Optional level filter (case-insensitive)
   * @param from_timestamp Optional from timestamp (ISO 8601)
   * @param to_timestamp Optional to timestamp (ISO 8601)
   * @return Filtered logs
   */
  std::vector<Json::Value> filterLogs(const std::vector<Json::Value> &logs,
                                      const std::string &level_filter,
                                      const std::string &from_timestamp,
                                      const std::string &to_timestamp) const;

  /**
   * @brief Get tail lines from a file (last N lines)
   *
   * @param file_path Path to log file
   * @param tail_count Number of lines to get from end
   * @return Vector of log lines
   */
  std::vector<std::string> getTailLines(const std::string &file_path,
                                        int tail_count) const;

  /**
   * @brief Read and parse log file
   *
   * @param file_path Path to log file
   * @param tail_count Optional tail count (0 = read all)
   * @return Vector of parsed log entries
   */
  std::vector<Json::Value> readLogFile(const std::string &file_path,
                                       int tail_count = 0) const;

  /**
   * @brief Convert category string to LogManager::Category enum
   *
   * @param category_str Category string (api, instance, sdk_output, general)
   * @param category Output category enum
   * @return true if valid, false otherwise
   */
  bool parseCategory(const std::string &category_str,
                     LogManager::Category &category) const;

  /**
   * @brief Convert LogManager::Category to string
   *
   * @param category Category enum
   * @return Category string
   */
  std::string categoryToString(LogManager::Category category) const;

  /**
   * @brief Parse ISO 8601 timestamp to time_t
   *
   * @param timestamp ISO 8601 timestamp string
   * @return time_t value, or 0 if invalid
   */
  time_t parseTimestamp(const std::string &timestamp) const;

  /**
   * @brief Extract category from request path
   *
   * @param req HTTP request
   * @return Category string extracted from path
   */
  std::string extractCategory(const HttpRequestPtr &req) const;

  /**
   * @brief Extract date from request path
   *
   * @param req HTTP request
   * @return Date string extracted from path
   */
  std::string extractDate(const HttpRequestPtr &req) const;

  /**
   * @brief Create error response
   *
   * @param statusCode HTTP status code
   * @param error Error type
   * @param message Error message
   * @return HttpResponsePtr
   */
  HttpResponsePtr createErrorResponse(int statusCode, const std::string &error,
                                      const std::string &message = "") const;
};
