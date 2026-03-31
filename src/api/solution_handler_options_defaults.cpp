#include "api/solution_handler.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/metrics_interceptor.h"
#include "core/node_pool_manager.h"
#include "core/node_template_registry.h"
#include "models/solution_config.h"
#include "solutions/solution_registry.h"
#include "solutions/solution_storage.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <unistd.h>

void SolutionHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, PUT, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");
  resp->addHeader("Access-Control-Max-Age", "3600");

  // Record metrics and call callback
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

std::string SolutionHandler::getDefaultSolutionsDir() const {
  // Try multiple paths to find examples/default_solutions directory
  std::vector<std::string> possiblePaths = {
      "./examples/default_solutions",
      "../examples/default_solutions",
      "../../examples/default_solutions",
      "/opt/omniapi/examples/default_solutions",
  };

  // Also try relative to executable path
  try {
    char exePath[1024];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len != -1) {
      exePath[len] = '\0';
      std::filesystem::path exe(exePath);
      std::filesystem::path exeDir = exe.parent_path();

      // Try going up from bin/ or build/bin/
      for (int i = 0; i < 5; i++) {
        std::filesystem::path testPath = exeDir;
        for (int j = 0; j < i; j++) {
          testPath = testPath.parent_path();
        }
        std::filesystem::path examplesPath =
            testPath / "examples" / "default_solutions";
        if (std::filesystem::exists(examplesPath) &&
            std::filesystem::is_directory(examplesPath)) {
          return examplesPath.string();
        }
      }
    }
  } catch (...) {
    // Ignore errors, continue with other paths
  }

  // Try current directory and common paths
  for (const auto &path : possiblePaths) {
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
      return path;
    }
  }

  // Default fallback
  return "./examples/default_solutions";
}

std::vector<std::string> SolutionHandler::listDefaultSolutionFiles() const {
  std::vector<std::string> files;
  std::string dir = getDefaultSolutionsDir();

  try {
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
      return files;
    }

    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".json") {
        std::string filename = entry.path().filename().string();
        // Skip index.json
        if (filename != "index.json") {
          files.push_back(filename);
        }
      }
    }
  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[API] Error listing default solution files: "
                   << e.what();
    }
  }

  return files;
}

std::optional<SolutionConfig>
SolutionHandler::loadDefaultSolutionFromFile(const std::string &solutionId,
                                             std::string &error) const {
  std::string dir = getDefaultSolutionsDir();
  std::string filepath = dir + "/" + solutionId + ".json";

  // Also try without .json extension if solutionId already has it
  if (solutionId.find(".json") != std::string::npos) {
    filepath = dir + "/" + solutionId;
  }

  try {
    if (!std::filesystem::exists(filepath)) {
      error = "Default solution file not found: " + filepath;
      return std::nullopt;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
      error = "Failed to open file: " + filepath;
      return std::nullopt;
    }

    Json::CharReaderBuilder builder;
    std::string parseErrors;
    Json::Value json;
    if (!Json::parseFromStream(builder, file, &json, &parseErrors)) {
      error = "Failed to parse JSON: " + parseErrors;
      return std::nullopt;
    }

    // Parse solution config using existing parser
    std::string parseError;
    auto optConfig = parseSolutionConfig(json, parseError);
    if (!optConfig.has_value()) {
      error = "Failed to parse solution config: " + parseError;
      return std::nullopt;
    }

    SolutionConfig config = optConfig.value();
    // Ensure isDefault is false (these are templates, not system defaults)
    config.isDefault = false;

    return config;
  } catch (const std::exception &e) {
    error = "Exception loading default solution: " + std::string(e.what());
    return std::nullopt;
  }
}

void SolutionHandler::listDefaultSolutions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO
        << "[API] GET /v1/core/solution/defaults - List default solutions";
  }

  try {
    std::string dir = getDefaultSolutionsDir();
    std::vector<std::string> files = listDefaultSolutionFiles();

    Json::Value response;
    Json::Value solutions(Json::arrayValue);

    // Try to load index.json for metadata
    std::string indexPath = dir + "/index.json";
    Json::Value indexData(Json::objectValue);
    bool hasIndex = false;

    if (std::filesystem::exists(indexPath)) {
      try {
        std::ifstream indexFile(indexPath);
        if (indexFile.is_open()) {
          Json::CharReaderBuilder builder;
          std::string parseErrors;
          if (Json::parseFromStream(builder, indexFile, &indexData,
                                    &parseErrors)) {
            hasIndex = true;
          }
        }
      } catch (...) {
        // Ignore index parsing errors
      }
    }

    // Build response from index.json if available, otherwise from files
    if (hasIndex && indexData.isMember("solutions")) {
      // Use metadata from index.json
      for (const auto &solJson : indexData["solutions"]) {
        Json::Value solution;
        solution["solutionId"] = solJson["id"];
        solution["file"] = solJson["file"];
        solution["name"] = solJson["name"];
        solution["category"] = solJson["category"];
        solution["description"] = solJson["description"];
        solution["useCase"] = solJson["useCase"];
        solution["difficulty"] = solJson["difficulty"];

        // Check if file exists
        std::string filepath = dir + "/" + solJson["file"].asString();
        solution["available"] = std::filesystem::exists(filepath) &&
                                std::filesystem::is_regular_file(filepath);

        solutions.append(solution);
      }

      // Add categories if available
      if (indexData.isMember("categories")) {
        response["categories"] = indexData["categories"];
      }
    } else {
      // Fallback: list from files
      for (const auto &filename : files) {
        Json::Value solution;
        std::string solutionId = filename;
        // Remove .json extension
        if (solutionId.size() > 5 &&
            solutionId.substr(solutionId.size() - 5) == ".json") {
          solutionId = solutionId.substr(0, solutionId.size() - 5);
        }
        solution["solutionId"] = solutionId;
        solution["file"] = filename;
        solution["available"] = true;
        solutions.append(solution);
      }
    }

    response["solutions"] = solutions;
    response["total"] = static_cast<int>(solutions.size());
    response["directory"] = dir;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/solution/defaults - Success: "
                << solutions.size() << " solutions - " << duration.count()
                << "ms";
    }

    callback(createSuccessResponse(response));
  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/solution/defaults - Exception: "
                 << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Exception: " + std::string(e.what())));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR
          << "[API] GET /v1/core/solution/defaults - Unknown exception - "
          << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

