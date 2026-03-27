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

SolutionRegistry *SolutionHandler::solution_registry_ = nullptr;
SolutionStorage *SolutionHandler::solution_storage_ = nullptr;

void SolutionHandler::setSolutionRegistry(SolutionRegistry *registry) {
  solution_registry_ = registry;
}

void SolutionHandler::setSolutionStorage(SolutionStorage *storage) {
  solution_storage_ = storage;
}

std::string
SolutionHandler::extractSolutionId(const HttpRequestPtr &req) const {
  // Try getParameter first (standard way - Drogon auto-extracts from route
  // pattern)
  std::string solutionId = req->getParameter("solutionId");

  // Fallback: extract from path if getParameter doesn't work (e.g., in unit
  // tests)
  if (solutionId.empty()) {
    std::string path = req->getPath();

    // Pattern: /v1/core/solution/{solutionId} or
    // /v1/core/solution/{solutionId}/... Try "/solution/" (singular) first -
    // this is the current API pattern
    size_t solutionPos = path.find("/solution/");
    if (solutionPos != std::string::npos) {
      size_t start = solutionPos + 10; // length of "/solution/"
      size_t end = path.find("/", start);
      if (end == std::string::npos) {
        end = path.length();
      }
      solutionId = path.substr(start, end - start);

      // Special handling for /v1/core/solution/defaults/{solutionId}
      // If we got "defaults", skip it and get the next segment
      if (solutionId == "defaults") {
        if (end < path.length()) {
          start = end + 1;
          end = path.find("/", start);
          if (end == std::string::npos) {
            end = path.length();
          }
          solutionId = path.substr(start, end - start);
        } else {
          // Path ends with "/defaults", no solutionId
          solutionId = "";
        }
      }
    } else {
      // Try "/solution" at end of path (no trailing slash)
      size_t solutionEndPos = path.rfind("/solution");
      if (solutionEndPos != std::string::npos &&
          solutionEndPos + 9 == path.length()) {
        // This is just "/solution" without ID, return empty
        solutionId = "";
      } else {
        // Fallback to plural form for backward compatibility
        size_t solutionsPos = path.find("/solutions/");
        if (solutionsPos != std::string::npos) {
          size_t start = solutionsPos + 11; // length of "/solutions/"
          size_t end = path.find("/", start);
          if (end == std::string::npos) {
            end = path.length();
          }
          solutionId = path.substr(start, end - start);
        }
      }
    }
  }

  return solutionId;
}

bool SolutionHandler::validateSolutionId(const std::string &solutionId,
                                         std::string &error) const {
  if (solutionId.empty()) {
    error = "Solution ID cannot be empty";
    return false;
  }

  // Validate format: alphanumeric, underscore, hyphen only
  std::regex pattern("^[A-Za-z0-9_-]+$");
  if (!std::regex_match(solutionId, pattern)) {
    error = "Solution ID must contain only alphanumeric characters, "
            "underscores, and hyphens";
    return false;
  }

  return true;
}

Json::Value
SolutionHandler::solutionConfigToJson(const SolutionConfig &config) const {
  Json::Value json(Json::objectValue);

  json["solutionId"] = config.solutionId;
  json["solutionName"] = config.solutionName;
  json["solutionType"] = config.solutionType;
  json["isDefault"] = config.isDefault;

  // Convert pipeline
  Json::Value pipeline(Json::arrayValue);
  for (const auto &node : config.pipeline) {
    Json::Value nodeJson(Json::objectValue);
    nodeJson["nodeType"] = node.nodeType;
    nodeJson["nodeName"] = node.nodeName;

    Json::Value params(Json::objectValue);
    for (const auto &param : node.parameters) {
      params[param.first] = param.second;
    }
    nodeJson["parameters"] = params;

    pipeline.append(nodeJson);
  }
  json["pipeline"] = pipeline;

  // Convert defaults
  Json::Value defaults(Json::objectValue);
  for (const auto &def : config.defaults) {
    defaults[def.first] = def.second;
  }
  json["defaults"] = defaults;

  return json;
}

std::optional<SolutionConfig>
SolutionHandler::parseSolutionConfig(const Json::Value &json,
                                     std::string &error) const {
  try {
    SolutionConfig config;

    // Required: solutionId
    if (!json.isMember("solutionId") || !json["solutionId"].isString()) {
      error = "Missing required field: solutionId";
      return std::nullopt;
    }
    config.solutionId = json["solutionId"].asString();

    // Validate solutionId format
    std::string validationError;
    if (!validateSolutionId(config.solutionId, validationError)) {
      error = validationError;
      return std::nullopt;
    }

    // Required: solutionName
    if (!json.isMember("solutionName") || !json["solutionName"].isString()) {
      error = "Missing required field: solutionName";
      return std::nullopt;
    }
    config.solutionName = json["solutionName"].asString();

    if (config.solutionName.empty()) {
      error = "solutionName cannot be empty";
      return std::nullopt;
    }

    // Required: solutionType
    if (!json.isMember("solutionType") || !json["solutionType"].isString()) {
      error = "Missing required field: solutionType";
      return std::nullopt;
    }
    config.solutionType = json["solutionType"].asString();

    // SECURITY: Ignore isDefault from user input - users cannot create default
    // solutions Default solutions are hardcoded in the application and cannot
    // be created via API We explicitly ignore this field if provided by the
    // user
    config.isDefault = false;

    // Required: pipeline
    if (!json.isMember("pipeline") || !json["pipeline"].isArray()) {
      error = "Missing required field: pipeline (must be an array)";
      return std::nullopt;
    }

    if (json["pipeline"].size() == 0) {
      error = "pipeline cannot be empty";
      return std::nullopt;
    }

    // Parse pipeline nodes
    for (const auto &nodeJson : json["pipeline"]) {
      if (!nodeJson.isObject()) {
        error = "Pipeline nodes must be objects";
        return std::nullopt;
      }

      SolutionConfig::NodeConfig node;

      // Required: nodeType
      if (!nodeJson.isMember("nodeType") || !nodeJson["nodeType"].isString()) {
        error = "Pipeline node missing required field: nodeType";
        return std::nullopt;
      }
      node.nodeType = nodeJson["nodeType"].asString();

      // Required: nodeName
      if (!nodeJson.isMember("nodeName") || !nodeJson["nodeName"].isString()) {
        error = "Pipeline node missing required field: nodeName";
        return std::nullopt;
      }
      node.nodeName = nodeJson["nodeName"].asString();

      // Optional: parameters
      if (nodeJson.isMember("parameters") &&
          nodeJson["parameters"].isObject()) {
        for (const auto &key : nodeJson["parameters"].getMemberNames()) {
          if (nodeJson["parameters"][key].isString()) {
            node.parameters[key] = nodeJson["parameters"][key].asString();
          } else {
            error = "Pipeline node parameters must be strings";
            return std::nullopt;
          }
        }
      }

      config.pipeline.push_back(node);
    }

    // Optional: defaults
    if (json.isMember("defaults") && json["defaults"].isObject()) {
      for (const auto &key : json["defaults"].getMemberNames()) {
        if (json["defaults"][key].isString()) {
          config.defaults[key] = json["defaults"][key].asString();
        }
      }
    }

    return config;
  } catch (const std::exception &e) {
    error = std::string("Error parsing solution config: ") + e.what();
    return std::nullopt;
  }
}

HttpResponsePtr
SolutionHandler::createErrorResponse(int statusCode, const std::string &error,
                                     const std::string &message) const {
  Json::Value response(Json::objectValue);
  response["error"] = error;
  if (!message.empty()) {
    response["message"] = message;
  }

  auto resp = HttpResponse::newHttpJsonResponse(response);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, PUT, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");

  return resp;
}

HttpResponsePtr SolutionHandler::createSuccessResponse(const Json::Value &data,
                                                       int statusCode) const {
  auto resp = HttpResponse::newHttpJsonResponse(data);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, PUT, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");
  return resp;
}

void SolutionHandler::listSolutions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/solution - List solutions";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!solution_registry_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/solution - Error: Solution registry "
                      "not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Solution registry not initialized"));
      return;
    }

    // Get all solutions from registry
    auto allSolutions = solution_registry_->getAllSolutions();

    // Build response
    Json::Value response;
    Json::Value solutions(Json::arrayValue);

    int totalCount = 0;
    int defaultCount = 0;
    int customCount = 0;
    int availableDefaultCount = 0;

    // Add solutions from registry
    std::set<std::string> registeredSolutionIds;
    for (const auto &[solutionId, config] : allSolutions) {
      Json::Value solution;
      solution["id"] = config.solutionId; // Use "id" to match index.json format
      solution["solutionId"] =
          config.solutionId; // Keep for backward compatibility
      solution["name"] =
          config.solutionName; // Use "name" to match index.json format
      solution["solutionName"] =
          config.solutionName; // Keep for backward compatibility
      solution["solutionType"] = config.solutionType;
      solution["isDefault"] = config.isDefault;
      solution["pipelineNodeCount"] = static_cast<int>(config.pipeline.size());
      solution["loaded"] = true; // Already loaded in registry

      // Infer category and generate metadata
      std::string category = config.solutionType;
      if (category.empty() || category == "unknown") {
        // Try to infer from solutionId
        std::string lowerId = solutionId;
        std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(),
                       ::tolower);
        if (lowerId.find("face") != std::string::npos) {
          category = "face_detection";
        } else if (lowerId.find("object") != std::string::npos ||
                   lowerId.find("yolo") != std::string::npos) {
          category = "object_detection";
        } else if (lowerId.find("mask") != std::string::npos ||
                   lowerId.find("rcnn") != std::string::npos) {
          category = "segmentation";
        } else if (lowerId.find("ba") != std::string::npos ||
                   lowerId.find("crossline") != std::string::npos) {
          category = "behavior_analysis";
        } else {
          category = config.solutionType;
        }
      }
      solution["category"] = category;
      solution["description"] = config.solutionName;

      // Generate useCase in Vietnamese based on solutionId
      std::string useCase = generateUseCase(solutionId, category);
      solution["useCase"] = useCase;

      // Determine difficulty
      std::string difficulty = "intermediate";
      std::string lowerId = solutionId;
      std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(),
                     ::tolower);
      if (lowerId.find("minimal") != std::string::npos) {
        difficulty = "beginner";
      } else if (lowerId.find("mask") != std::string::npos ||
                 lowerId.find("ba") != std::string::npos ||
                 lowerId.find("crossline") != std::string::npos) {
        difficulty = "advanced";
      }
      solution["difficulty"] = difficulty;

      solutions.append(solution);
      registeredSolutionIds.insert(config.solutionId);
      totalCount++;
      if (config.isDefault) {
        defaultCount++;
      } else {
        customCount++;
      }
    }

    // Add default solutions from filesystem that are not yet loaded
    try {
      std::string dir = getDefaultSolutionsDir();
      std::vector<std::string> defaultFiles = listDefaultSolutionFiles();

      // List from files and load metadata from individual files
      for (const auto &filename : defaultFiles) {
        std::string solutionId = filename;
        // Remove .json extension
        if (solutionId.size() > 5 &&
            solutionId.substr(solutionId.size() - 5) == ".json") {
          solutionId = solutionId.substr(0, solutionId.size() - 5);
        }

        if (registeredSolutionIds.find(solutionId) ==
            registeredSolutionIds.end()) {
          // Not yet loaded, try to load metadata from file
          Json::Value solution;
          solution["id"] = solutionId;
          solution["solutionId"] = solutionId;
          solution["isDefault"] = false;
          solution["loaded"] = false;
          solution["available"] = true;
          solution["file"] = filename;

          // Try to load solution file to get metadata
          std::string filepath = dir + "/" + filename;
          std::string solutionName = solutionId;
          std::string solutionType = "";
          std::string description = solutionId;

          if (std::filesystem::exists(filepath) &&
              std::filesystem::is_regular_file(filepath)) {
            try {
              std::ifstream file(filepath);
              if (file.is_open()) {
                Json::CharReaderBuilder builder;
                std::string parseErrors;
                Json::Value fileData;
                if (Json::parseFromStream(builder, file, &fileData,
                                          &parseErrors)) {
                  if (fileData.isMember("solutionName")) {
                    solutionName = fileData["solutionName"].asString();
                  }
                  if (fileData.isMember("solutionType")) {
                    solutionType = fileData["solutionType"].asString();
                  }
                  if (fileData.isMember("description")) {
                    description = fileData["description"].asString();
                  }
                }
              }
            } catch (...) {
              // If file parsing fails, use defaults
            }
          }

          solution["name"] = solutionName;
          solution["solutionName"] = solutionName;
          solution["solutionType"] = solutionType;
          solution["description"] = description;

          // Infer category and generate metadata
          std::string category = solutionType;
          std::string lowerId = solutionId;
          std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(),
                         ::tolower);

          if (category.empty() || category == "unknown") {
            if (lowerId.find("face") != std::string::npos) {
              category = "face_detection";
            } else if (lowerId.find("object") != std::string::npos ||
                       lowerId.find("yolo") != std::string::npos) {
              category = "object_detection";
            } else if (lowerId.find("mask") != std::string::npos ||
                       lowerId.find("rcnn") != std::string::npos) {
              category = "segmentation";
            } else if (lowerId.find("ba") != std::string::npos ||
                       lowerId.find("crossline") != std::string::npos) {
              category = "behavior_analysis";
            } else {
              category = "general";
            }
          }
          solution["category"] = category;

          // Generate useCase in Vietnamese
          std::string useCase = generateUseCase(solutionId, category);
          solution["useCase"] = useCase;

          // Determine difficulty
          std::string difficulty = "intermediate";
          if (lowerId.find("minimal") != std::string::npos) {
            difficulty = "beginner";
          } else if (lowerId.find("mask") != std::string::npos ||
                     lowerId.find("ba") != std::string::npos ||
                     lowerId.find("crossline") != std::string::npos) {
            difficulty = "advanced";
          }
          solution["difficulty"] = difficulty;

          solutions.append(solution);
          totalCount++;
        }
      }
    } catch (const std::exception &e) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] Error loading default solutions from filesystem: "
            << e.what();
      }
      // Continue without default solutions
    }

    response["solutions"] = solutions;
    response["total"] = totalCount;
    response["default"] = defaultCount;
    response["custom"] = customCount;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/solution - Success: " << totalCount
                << " solutions (default: " << defaultCount
                << ", custom: " << customCount << ") - " << duration.count()
                << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/solution - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }
    std::cerr << "[SolutionHandler] Exception: " << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/solution - Unknown exception - "
                 << duration.count() << "ms";
    }
    std::cerr << "[SolutionHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void SolutionHandler::getSolution(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  std::string solutionId = extractSolutionId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/solution/" << solutionId
              << " - Get solution";
  }

  try {
    if (!solution_registry_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/solution/" << solutionId
                   << " - Error: Solution registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Solution registry not initialized"));
      return;
    }

    if (solutionId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/solution/{id} - Error: Solution "
                        "ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Solution ID is required"));
      return;
    }

    auto optConfig = solution_registry_->getSolution(solutionId);
    if (!optConfig.has_value()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/solution/" << solutionId
                     << " - Not found - " << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Solution not found: " + solutionId));
      return;
    }

    Json::Value response = solutionConfigToJson(optConfig.value());

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/solution/" << solutionId
                << " - Success - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/solution/" << solutionId
                 << " - Exception: " << e.what() << " - " << duration.count()
                 << "ms";
    }
    std::cerr << "[SolutionHandler] Exception: " << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/solution/" << solutionId
                 << " - Unknown exception - " << duration.count() << "ms";
    }
    std::cerr << "[SolutionHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

