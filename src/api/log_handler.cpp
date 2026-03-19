#include "api/log_handler.h"
#include "core/log_manager.h"
#include "core/metrics_interceptor.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

void LogHandler::listLogFiles(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    Json::Value response;
    Json::Value categories(Json::objectValue);

    // List files for each category
    std::vector<std::pair<LogManager::Category, std::string>> categoryList = {
        {LogManager::Category::API, "api"},
        {LogManager::Category::INSTANCE, "instance"},
        {LogManager::Category::SDK_OUTPUT, "sdk_output"},
        {LogManager::Category::GENERAL, "general"}};

    for (const auto &[category, name] : categoryList) {
      if (category == LogManager::Category::INSTANCE) {
        auto instFiles = LogManager::listInstanceLogTree();
        Json::Value categoryFiles(Json::arrayValue);
        for (const auto &e : instFiles) {
          Json::Value fileInfo;
          fileInfo["instance_id"] = e.instance_id;
          fileInfo["date"] = e.date;
          fileInfo["size"] = static_cast<Json::Int64>(e.size);
          fileInfo["path"] = e.path;
          categoryFiles.append(fileInfo);
        }
        categories[name] = categoryFiles;
        continue;
      }
      auto files = LogManager::listLogFiles(category);
      Json::Value categoryFiles(Json::arrayValue);

      for (const auto &[date, size] : files) {
        Json::Value fileInfo;
        fileInfo["date"] = date;
        fileInfo["size"] = static_cast<Json::Int64>(size);
        fileInfo["path"] = LogManager::getLogFilePath(category, date);
        categoryFiles.append(fileInfo);
      }

      categories[name] = categoryFiles;
    }

    response["categories"] = categories;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
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

std::string LogHandler::extractCategory(const HttpRequestPtr &req) const {
  // Try getParameter first (standard way)
  std::string category = req->getParameter("category");

  // Fallback: extract from path if getParameter doesn't work
  if (category.empty()) {
    std::string path = req->getPath();
    // Look for "/log/" (singular) in path like "/v1/core/log/{category}"
    size_t logPos = path.find("/log/");
    if (logPos != std::string::npos) {
      size_t start = logPos + 5; // length of "/log/"
      size_t end = path.find("/", start);
      if (end == std::string::npos) {
        end = path.length();
      }
      category = path.substr(start, end - start);
    }
  }

  return category;
}

std::string LogHandler::extractDate(const HttpRequestPtr &req) const {
  // Try getParameter first (standard way)
  std::string date = req->getParameter("date");

  // Fallback: extract from path if getParameter doesn't work
  if (date.empty()) {
    std::string path = req->getPath();
    // Look for pattern: /log/{category}/{date}
    size_t logPos = path.find("/log/");
    if (logPos != std::string::npos) {
      size_t categoryStart = logPos + 5; // length of "/log/"
      size_t categoryEnd = path.find("/", categoryStart);
      if (categoryEnd != std::string::npos) {
        size_t dateStart = categoryEnd + 1;
        size_t dateEnd = path.find("/", dateStart);
        if (dateEnd == std::string::npos) {
          dateEnd = path.length();
        }
        date = path.substr(dateStart, dateEnd - dateStart);
      }
    }
  }

  return date;
}

void LogHandler::getLogsByCategory(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    // Get category from path parameter
    std::string category_str = extractCategory(req);
    if (category_str.empty()) {
      callback(createErrorResponse(k400BadRequest, "Bad request",
                                   "Category parameter is required"));
      return;
    }

    LogManager::Category category;
    if (!parseCategory(category_str, category)) {
      callback(createErrorResponse(k400BadRequest, "Bad request",
                                   "Invalid category: " + category_str));
      return;
    }

    std::string instance_id_param = req->getParameter("instance_id");

    std::string level_filter = req->getParameter("level");
    std::string from_timestamp = req->getParameter("from");
    std::string to_timestamp = req->getParameter("to");
    std::string tail_str = req->getParameter("tail");
    int tail_count = 0;
    if (!tail_str.empty()) {
      try {
        tail_count = std::stoi(tail_str);
        if (tail_count < 0)
          tail_count = 0;
      } catch (...) {
        tail_count = 0;
      }
    }

    if (category == LogManager::Category::INSTANCE && instance_id_param.empty()) {
      callback(createErrorResponse(
          k400BadRequest, "Bad request",
          "instance_id query parameter is required for category instance"));
      return;
    }

    std::string category_dir = LogManager::getCategoryDir(category);
    if (category == LogManager::Category::INSTANCE) {
      category_dir += "/" + instance_id_param;
    }

    std::vector<std::pair<std::string, uint64_t>> files;
    if (category == LogManager::Category::INSTANCE) {
      if (fs::exists(category_dir) && fs::is_directory(category_dir)) {
        for (const auto &entry : fs::directory_iterator(category_dir)) {
          if (!fs::is_regular_file(entry)) {
            continue;
          }
          std::string fn = entry.path().filename().string();
          if (fn.size() < 5 || fn.substr(fn.size() - 4) != ".log") {
            continue;
          }
          std::string base = fn.substr(0, fn.size() - 4);
          size_t dotp = base.find('.');
          if (dotp != std::string::npos) {
            base = base.substr(0, dotp);
          }
          if (base.size() == 10 && base[4] == '-' && base[7] == '-') {
            bool dup = false;
            for (const auto &pr : files) {
              if (pr.first == base) {
                dup = true;
                break;
              }
            }
            if (!dup) {
              try {
                files.push_back({base, fs::file_size(entry.path())});
              } catch (...) {
              }
            }
          }
        }
        std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) {
          return a.first > b.first;
        });
      }
    } else {
      files = LogManager::listLogFiles(category);
    }

    // Build response (declare once)
    Json::Value response;
    response["category"] = category_str;
    response["category_dir"] = category_dir;
    response["files_count"] = static_cast<int>(files.size());

    if (files.empty()) {
      response["total_lines"] = 0;
      response["filtered_lines"] = 0;
      response["logs"] = Json::Value(Json::arrayValue);

      auto resp = HttpResponse::newHttpJsonResponse(response);
      resp->setStatusCode(k200OK);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Read logs from all files (or just latest if tail is specified)
    std::vector<Json::Value> allLogs;
    int totalLines = 0;

    // If tail is specified, only read from the latest file
    if (tail_count > 0 && !files.empty()) {
      std::string latest_file;
      if (category == LogManager::Category::INSTANCE) {
        latest_file = category_dir + "/" + files[0].first + ".log";
      } else {
        latest_file = LogManager::getLogFilePath(category, files[0].first);
      }
      auto logs = readLogFile(latest_file, tail_count);
      allLogs.insert(allLogs.end(), logs.begin(), logs.end());
      totalLines = static_cast<int>(logs.size());
    } else {
      for (const auto &[date, size] : files) {
        std::vector<std::string> paths;
        if (category == LogManager::Category::INSTANCE) {
          std::string p0 = category_dir + "/" + date + ".log";
          paths.push_back(p0);
          for (int p = 1; p < 20; ++p) {
            std::string pp =
                category_dir + "/" + date + "." + std::to_string(p) + ".log";
            if (fs::exists(pp)) {
              paths.push_back(pp);
            }
          }
        } else {
          paths.push_back(LogManager::getLogFilePath(category, date));
        }
        for (const auto &fp : paths) {
          auto logs = readLogFile(fp, 0);
          allLogs.insert(allLogs.end(), logs.begin(), logs.end());
          totalLines += static_cast<int>(logs.size());
        }
      }
    }

    // Apply filters
    auto filteredLogs =
        filterLogs(allLogs, level_filter, from_timestamp, to_timestamp);

    // Update response with log data
    response["total_lines"] = totalLines;
    response["filtered_lines"] = static_cast<int>(filteredLogs.size());
    response["logs"] = Json::Value(Json::arrayValue);
    for (const auto &log : filteredLogs) {
      response["logs"].append(log);
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
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

void LogHandler::getLogsByCategoryAndDate(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    // Get category and date from path parameters
    std::string category_str = extractCategory(req);
    std::string date_str = extractDate(req);

    if (category_str.empty() || date_str.empty()) {
      callback(
          createErrorResponse(k400BadRequest, "Bad request",
                              "Category and date parameters are required"));
      return;
    }

    // Validate date format (YYYY-MM-DD)
    if (date_str.length() != 10 || date_str[4] != '-' || date_str[7] != '-') {
      callback(createErrorResponse(k400BadRequest, "Bad request",
                                   "Invalid date format. Expected YYYY-MM-DD"));
      return;
    }

    LogManager::Category category;
    if (!parseCategory(category_str, category)) {
      callback(createErrorResponse(k400BadRequest, "Bad request",
                                   "Invalid category: " + category_str));
      return;
    }

    std::string inst_id2 = req->getParameter("instance_id");
    if (category == LogManager::Category::INSTANCE && inst_id2.empty()) {
      callback(createErrorResponse(
          k400BadRequest, "Bad request",
          "instance_id query parameter is required for category instance"));
      return;
    }

    std::string level_filter = req->getParameter("level");
    std::string from_timestamp = req->getParameter("from");
    std::string to_timestamp = req->getParameter("to");
    std::string tail_str = req->getParameter("tail");
    int tail_count = 0;
    if (!tail_str.empty()) {
      try {
        tail_count = std::stoi(tail_str);
        if (tail_count < 0)
          tail_count = 0;
      } catch (...) {
        tail_count = 0;
      }
    }

    std::vector<Json::Value> logs;
    if (category == LogManager::Category::INSTANCE) {
      std::string idir = LogManager::getCategoryDir(
                             LogManager::Category::INSTANCE) +
                         "/" + inst_id2;
      std::vector<std::string> paths;
      std::string p0 = idir + "/" + date_str + ".log";
      if (fs::exists(p0)) {
        paths.push_back(p0);
      }
      for (int p = 1; p < 50; ++p) {
        std::string pp =
            idir + "/" + date_str + "." + std::to_string(p) + ".log";
        if (fs::exists(pp)) {
          paths.push_back(pp);
        }
      }
      if (paths.empty()) {
        callback(createErrorResponse(k404NotFound, "Not found",
                                     "Log file not found for date: " + date_str));
        return;
      }
      for (const auto &fp : paths) {
        auto part = readLogFile(fp, 0);
        logs.insert(logs.end(), part.begin(), part.end());
      }
    } else {
      std::string file_path = LogManager::getLogFilePath(category, date_str);
      if (!fs::exists(file_path)) {
        callback(createErrorResponse(k404NotFound, "Not found",
                                     "Log file not found for date: " + date_str));
        return;
      }
      logs = readLogFile(file_path, tail_count);
    }

    int totalLines = static_cast<int>(logs.size());

    // Apply filters
    auto filteredLogs =
        filterLogs(logs, level_filter, from_timestamp, to_timestamp);

    // Build response
    Json::Value response;
    response["category"] = category_str;
    response["date"] = date_str;
    response["total_lines"] = totalLines;
    response["filtered_lines"] = static_cast<int>(filteredLogs.size());
    response["logs"] = Json::Value(Json::arrayValue);
    for (const auto &log : filteredLogs) {
      response["logs"].append(log);
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
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

void LogHandler::handleOptions(
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

Json::Value LogHandler::parseLogLine(const std::string &line) const {
  Json::Value logEntry;

  if (line.empty()) {
    return logEntry; // Empty
  }

  // Plog TxtFormatter format: YYYY-MM-DD HH:MM:SS.mmm LEVEL  [thread]
  // [function@line] message Example: 2025-12-08 19:16:04.659 INFO  [1699886]
  // [CategorizedLogger::init@93] message Or simpler: 2025-12-08 19:16:04.659
  // INFO  message

  // Try to match the full format first
  std::regex logRegexFull(
      R"((\d{4}-\d{2}-\d{2})\s+(\d{2}:\d{2}:\d{2})\.(\d{3})\s+(\w+)\s+\[.*?\]\s+\[.*?\]\s+(.*))");
  std::smatch matches;

  if (std::regex_match(line, matches, logRegexFull)) {
    if (matches.size() >= 5) {
      std::string date = matches[1].str();
      std::string time = matches[2].str();
      std::string milliseconds = matches[3].str();
      std::string level = matches[4].str();
      std::string message = matches.size() > 5 ? matches[5].str() : "";

      // Build ISO 8601 timestamp
      std::string timestamp = date + "T" + time + "." + milliseconds + "Z";

      logEntry["timestamp"] = timestamp;
      logEntry["level"] = level;
      logEntry["message"] = message;
      return logEntry;
    }
  }

  // Try simpler format without thread and function info
  std::regex logRegexSimple(
      R"((\d{4}-\d{2}-\d{2})\s+(\d{2}:\d{2}:\d{2})\.(\d{3})\s+(\w+)\s+(.*))");
  if (std::regex_match(line, matches, logRegexSimple)) {
    if (matches.size() >= 5) {
      std::string date = matches[1].str();
      std::string time = matches[2].str();
      std::string milliseconds = matches[3].str();
      std::string level = matches[4].str();
      std::string message = matches.size() > 5 ? matches[5].str() : "";

      // Build ISO 8601 timestamp
      std::string timestamp = date + "T" + time + "." + milliseconds + "Z";

      logEntry["timestamp"] = timestamp;
      logEntry["level"] = level;
      logEntry["message"] = message;
      return logEntry;
    }
  }

  return logEntry;
}

std::vector<Json::Value> LogHandler::filterLogs(
    const std::vector<Json::Value> &logs, const std::string &level_filter,
    const std::string &from_timestamp, const std::string &to_timestamp) const {
  std::vector<Json::Value> filtered;

  // Convert level filter to uppercase for case-insensitive comparison
  std::string level_upper = level_filter;
  std::transform(level_upper.begin(), level_upper.end(), level_upper.begin(),
                 ::toupper);

  // Parse time range if provided
  time_t from_time = 0;
  time_t to_time = 0;
  if (!from_timestamp.empty()) {
    from_time = parseTimestamp(from_timestamp);
  }
  if (!to_timestamp.empty()) {
    to_time = parseTimestamp(to_timestamp);
  }

  for (const auto &log : logs) {
    if (log.empty())
      continue;

    // Filter by level
    if (!level_filter.empty()) {
      std::string log_level = log["level"].asString();
      std::string log_level_upper = log_level;
      std::transform(log_level_upper.begin(), log_level_upper.end(),
                     log_level_upper.begin(), ::toupper);
      if (log_level_upper != level_upper) {
        continue;
      }
    }

    // Filter by time range
    if (from_time > 0 || to_time > 0) {
      std::string log_timestamp = log["timestamp"].asString();
      time_t log_time = parseTimestamp(log_timestamp);
      if (log_time == 0)
        continue;

      if (from_time > 0 && log_time < from_time) {
        continue;
      }
      if (to_time > 0 && log_time > to_time) {
        continue;
      }
    }

    filtered.push_back(log);
  }

  return filtered;
}

std::vector<std::string> LogHandler::getTailLines(const std::string &file_path,
                                                  int tail_count) const {
  std::vector<std::string> lines;

  if (tail_count <= 0) {
    return lines;
  }

  try {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
      return lines;
    }

    // Seek to end
    file.seekg(0, std::ios::end);
    std::streampos file_size = file.tellg();

    if (file_size == 0) {
      return lines;
    }

    // Read file in reverse to get last N lines
    std::string buffer;
    buffer.resize(static_cast<size_t>(file_size));
    file.seekg(0, std::ios::beg);
    file.read(&buffer[0], file_size);
    file.close();

    // Split into lines
    std::istringstream iss(buffer);
    std::string line;
    std::vector<std::string> all_lines;
    while (std::getline(iss, line)) {
      if (!line.empty()) {
        all_lines.push_back(line);
      }
    }

    // Get last N lines
    int start_idx =
        std::max(0, static_cast<int>(all_lines.size()) - tail_count);
    for (int i = start_idx; i < static_cast<int>(all_lines.size()); ++i) {
      lines.push_back(all_lines[i]);
    }

  } catch (const std::exception &e) {
    // Return empty on error
  }

  return lines;
}

std::vector<Json::Value> LogHandler::readLogFile(const std::string &file_path,
                                                 int tail_count) const {
  std::vector<Json::Value> logs;

  try {
    std::vector<std::string> lines;

    if (tail_count > 0) {
      lines = getTailLines(file_path, tail_count);
    } else {
      // Read all lines
      std::ifstream file(file_path);
      if (!file.is_open()) {
        return logs;
      }

      std::string line;
      while (std::getline(file, line)) {
        if (!line.empty()) {
          lines.push_back(line);
        }
      }
      file.close();
    }

    // Parse each line
    for (const auto &line : lines) {
      Json::Value logEntry = parseLogLine(line);
      if (!logEntry.empty()) {
        logs.push_back(logEntry);
      }
    }

  } catch (const std::exception &e) {
    // Return empty on error
  }

  return logs;
}

bool LogHandler::parseCategory(const std::string &category_str,
                               LogManager::Category &category) const {
  std::string lower = category_str;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  if (lower == "api") {
    category = LogManager::Category::API;
    return true;
  } else if (lower == "instance") {
    category = LogManager::Category::INSTANCE;
    return true;
  } else if (lower == "sdk_output" || lower == "sdkoutput") {
    category = LogManager::Category::SDK_OUTPUT;
    return true;
  } else if (lower == "general") {
    category = LogManager::Category::GENERAL;
    return true;
  }

  return false;
}

std::string LogHandler::categoryToString(LogManager::Category category) const {
  switch (category) {
  case LogManager::Category::API:
    return "api";
  case LogManager::Category::INSTANCE:
    return "instance";
  case LogManager::Category::SDK_OUTPUT:
    return "sdk_output";
  case LogManager::Category::GENERAL:
  default:
    return "general";
  }
}

time_t LogHandler::parseTimestamp(const std::string &timestamp) const {
  try {
    // Parse ISO 8601 format: YYYY-MM-DDTHH:MM:SS.mmmZ or YYYY-MM-DDTHH:MM:SSZ
    std::tm tm = {};
    std::istringstream ss(timestamp);

    // Remove 'Z' if present
    std::string ts = timestamp;
    if (!ts.empty() && ts.back() == 'Z') {
      ts.pop_back();
    }

    // Try to parse with milliseconds
    int milliseconds = 0; // Parse but ignore milliseconds
    if (sscanf(ts.c_str(), "%d-%d-%dT%d:%d:%d.%d", &tm.tm_year, &tm.tm_mon,
               &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec,
               &milliseconds) >= 6) {
      tm.tm_year -= 1900;
      tm.tm_mon -= 1;
      tm.tm_isdst = -1;
      return std::mktime(&tm);
    }

    // Try without milliseconds
    if (sscanf(ts.c_str(), "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon,
               &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) >= 6) {
      tm.tm_year -= 1900;
      tm.tm_mon -= 1;
      tm.tm_isdst = -1;
      return std::mktime(&tm);
    }

  } catch (...) {
    // Return 0 on error
  }

  return 0;
}

HttpResponsePtr
LogHandler::createErrorResponse(int statusCode, const std::string &error,
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
