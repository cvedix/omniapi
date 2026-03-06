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

void SolutionHandler::createSolution(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/solution - Create solution";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!solution_registry_ || !solution_storage_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/solution - Error: Solution "
                      "registry or storage not initialized";
      }
      callback(
          createErrorResponse(500, "Internal server error",
                              "Solution registry or storage not initialized"));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] POST /v1/core/solution - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Parse solution config
    std::string parseError;
    auto optConfig = parseSolutionConfig(*json, parseError);
    if (!optConfig.has_value()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/solution - Parse error: "
                     << parseError;
      }
      callback(createErrorResponse(400, "Invalid request", parseError));
      return;
    }

    SolutionConfig config = optConfig.value();

    // Ensure isDefault is false for custom solutions
    config.isDefault = false;

    // Check if solution already exists
    if (solution_registry_->hasSolution(config.solutionId)) {
      // Check if it's a default solution - cannot override default solutions
      if (solution_registry_->isDefaultSolution(config.solutionId)) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] POST /v1/core/solution - Error: Cannot "
                          "override default solution: "
                       << config.solutionId;
        }
        callback(createErrorResponse(
            403, "Forbidden",
            "Cannot create solution with ID '" + config.solutionId +
                "': This ID is reserved for a default system solution. Please "
                "use a different solution ID."));
        return;
      }

      // Solution exists and is not default - return conflict
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/solution - Error: Solution "
                        "already exists: "
                     << config.solutionId;
      }
      callback(createErrorResponse(
          409, "Conflict",
          "Solution with ID '" + config.solutionId +
              "' already exists. Use PUT /v1/core/solution/" +
              config.solutionId + " to update it."));
      return;
    }

    // Validate node types in solution - ensure all node types are supported
    auto &nodePool = NodePoolManager::getInstance();
    auto allTemplates = nodePool.getAllTemplates();
    std::set<std::string> supportedNodeTypes;
    for (const auto &template_ : allTemplates) {
      supportedNodeTypes.insert(template_.nodeType);
    }

    std::vector<std::string> unsupportedNodeTypes;
    for (const auto &nodeConfig : config.pipeline) {
      if (supportedNodeTypes.find(nodeConfig.nodeType) ==
          supportedNodeTypes.end()) {
        unsupportedNodeTypes.push_back(nodeConfig.nodeType);
      }
    }

    if (!unsupportedNodeTypes.empty()) {
      std::string errorMsg = "Solution contains unsupported node types: ";
      for (size_t i = 0; i < unsupportedNodeTypes.size(); ++i) {
        errorMsg += unsupportedNodeTypes[i];
        if (i < unsupportedNodeTypes.size() - 1) {
          errorMsg += ", ";
        }
      }
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/solution - Error: " << errorMsg;
      }
      callback(createErrorResponse(400, "Invalid request", errorMsg));
      return;
    }

    // Register solution
    solution_registry_->registerSolution(config);

    // Create nodes for node types in this custom solution (if they don't exist)
    // Note: These are user-created nodes, not default nodes
    size_t nodesCreated = nodePool.createNodesFromSolution(config);
    if (nodesCreated > 0) {
      std::cerr << "[SolutionHandler] Created " << nodesCreated
                << " nodes for custom solution: " << config.solutionId
                << std::endl;
    }

    // Save to storage
    std::cerr << "[SolutionHandler] Attempting to save solution to storage: "
              << config.solutionId << std::endl;
    if (!solution_storage_->saveSolution(config)) {
      std::cerr
          << "[SolutionHandler] Warning: Failed to save solution to storage: "
          << config.solutionId << std::endl;
    } else {
      std::cerr
          << "[SolutionHandler] ✓ Solution saved successfully to storage: "
          << config.solutionId << std::endl;
    }

    Json::Value response = solutionConfigToJson(config);
    response["message"] = "Solution created successfully";

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/core/solution - Success: Created solution "
                << config.solutionId << " (" << config.solutionName << ") - "
                << duration.count() << "ms";
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers",
                    "Content-Type, Authorization");

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/solution - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }
    std::cerr << "[SolutionHandler] Exception: " << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/solution - Unknown exception - "
                 << duration.count() << "ms";
    }
    std::cerr << "[SolutionHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void SolutionHandler::updateSolution(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  std::string solutionId = extractSolutionId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] PUT /v1/core/solution/" << solutionId
              << " - Update solution";
  }

  try {
    if (!solution_registry_ || !solution_storage_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] PUT /v1/core/solution/" << solutionId
                   << " - Error: Solution registry or storage not initialized";
      }
      callback(
          createErrorResponse(500, "Internal server error",
                              "Solution registry or storage not initialized"));
      return;
    }

    if (solutionId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/solution/{id} - Error: Solution "
                        "ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Solution ID is required"));
      return;
    }

    // Check if solution exists
    if (!solution_registry_->hasSolution(solutionId)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/solution/" << solutionId
                     << " - Not found";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Solution not found: " + solutionId));
      return;
    }

    // Check if it's a default solution
    if (solution_registry_->isDefaultSolution(solutionId)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/solution/" << solutionId
                     << " - Cannot update default solution";
      }
      callback(createErrorResponse(
          403, "Forbidden", "Cannot update default solution: " + solutionId));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/solution/" << solutionId
                     << " - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Parse solution config
    std::string parseError;
    auto optConfig = parseSolutionConfig(*json, parseError);
    if (!optConfig.has_value()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/solution/" << solutionId
                     << " - Parse error: " << parseError;
      }
      callback(createErrorResponse(400, "Invalid request", parseError));
      return;
    }

    SolutionConfig config = optConfig.value();

    // Ensure solutionId matches
    if (config.solutionId != solutionId) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/solution/" << solutionId
                     << " - Solution ID mismatch";
      }
      callback(
          createErrorResponse(400, "Invalid request",
                              "Solution ID in body must match URL parameter"));
      return;
    }

    // Ensure isDefault is false for custom solutions
    config.isDefault = false;

    // Validate node types in solution - ensure all node types are supported
    auto &nodePool = NodePoolManager::getInstance();
    auto allTemplates = nodePool.getAllTemplates();
    std::set<std::string> supportedNodeTypes;
    for (const auto &template_ : allTemplates) {
      supportedNodeTypes.insert(template_.nodeType);
    }

    std::vector<std::string> unsupportedNodeTypes;
    for (const auto &nodeConfig : config.pipeline) {
      if (supportedNodeTypes.find(nodeConfig.nodeType) ==
          supportedNodeTypes.end()) {
        unsupportedNodeTypes.push_back(nodeConfig.nodeType);
      }
    }

    if (!unsupportedNodeTypes.empty()) {
      std::string errorMsg = "Solution contains unsupported node types: ";
      for (size_t i = 0; i < unsupportedNodeTypes.size(); ++i) {
        errorMsg += unsupportedNodeTypes[i];
        if (i < unsupportedNodeTypes.size() - 1) {
          errorMsg += ", ";
        }
      }
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/solution/" << solutionId
                     << " - Error: " << errorMsg;
      }
      callback(createErrorResponse(400, "Invalid request", errorMsg));
      return;
    }

    // Update solution
    if (!solution_registry_->updateSolution(config)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] PUT /v1/core/solution/" << solutionId
                   << " - Failed to update";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to update solution"));
      return;
    }

    // Create nodes for node types in this custom solution (if they don't exist)
    // Note: These are user-created nodes, not default nodes
    size_t nodesCreated = nodePool.createNodesFromSolution(config);
    if (nodesCreated > 0) {
      std::cerr << "[SolutionHandler] Created " << nodesCreated
                << " nodes for custom solution: " << config.solutionId
                << std::endl;
    }

    // Save to storage
    std::cerr
        << "[SolutionHandler] Attempting to save updated solution to storage: "
        << config.solutionId << std::endl;
    if (!solution_storage_->saveSolution(config)) {
      std::cerr
          << "[SolutionHandler] Warning: Failed to save solution to storage: "
          << config.solutionId << std::endl;
    } else {
      std::cerr << "[SolutionHandler] ✓ Solution updated and saved "
                   "successfully to storage: "
                << config.solutionId << std::endl;
    }

    Json::Value response = solutionConfigToJson(config);
    response["message"] = "Solution updated successfully";

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] PUT /v1/core/solution/" << solutionId
                << " - Success - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PUT /v1/core/solution/" << solutionId
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
      PLOG_ERROR << "[API] PUT /v1/core/solution/" << solutionId
                 << " - Unknown exception - " << duration.count() << "ms";
    }
    std::cerr << "[SolutionHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void SolutionHandler::deleteSolution(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  std::string solutionId = extractSolutionId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] DELETE /v1/core/solution/" << solutionId
              << " - Delete solution";
  }

  try {
    if (!solution_registry_ || !solution_storage_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/core/solution/" << solutionId
                   << " - Error: Solution registry or storage not initialized";
      }
      callback(
          createErrorResponse(500, "Internal server error",
                              "Solution registry or storage not initialized"));
      return;
    }

    if (solutionId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/core/solution/{id} - Error: "
                        "Solution ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Solution ID is required"));
      return;
    }

    // Check if solution exists
    if (!solution_registry_->hasSolution(solutionId)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/core/solution/" << solutionId
                     << " - Not found";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Solution not found: " + solutionId));
      return;
    }

    // Check if it's a default solution
    if (solution_registry_->isDefaultSolution(solutionId)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/core/solution/" << solutionId
                     << " - Cannot delete default solution";
      }
      callback(createErrorResponse(
          403, "Forbidden", "Cannot delete default solution: " + solutionId));
      return;
    }

    // Delete from registry
    if (!solution_registry_->deleteSolution(solutionId)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/core/solution/" << solutionId
                   << " - Failed to delete from registry";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to delete solution"));
      return;
    }

    // Delete from storage
    if (!solution_storage_->deleteSolution(solutionId)) {
      std::cerr
          << "[SolutionHandler] Warning: Failed to delete solution from storage"
          << std::endl;
    }

    Json::Value response(Json::objectValue);
    response["message"] = "Solution deleted successfully";
    response["solutionId"] = solutionId;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] DELETE /v1/core/solution/" << solutionId
                << " - Success - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/core/solution/" << solutionId
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
      PLOG_ERROR << "[API] DELETE /v1/core/solution/" << solutionId
                 << " - Unknown exception - " << duration.count() << "ms";
    }
    std::cerr << "[SolutionHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void SolutionHandler::getSolutionParameters(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  std::string solutionId = extractSolutionId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/solution/" << solutionId
              << "/parameters - Get solution parameters";
  }

  try {
    if (!solution_registry_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/solution/" << solutionId
                   << "/parameters - Error: Solution registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Solution registry not initialized"));
      return;
    }

    if (solutionId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/solution/{id}/parameters - Error: "
                        "Solution ID is empty";
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
                     << "/parameters - Not found - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Solution not found: " + solutionId));
      return;
    }

    const SolutionConfig &config = optConfig.value();

    // Extract parameters from solution
    Json::Value response(Json::objectValue);
    response["solutionId"] = config.solutionId;
    response["solutionName"] = config.solutionName;
    response["solutionType"] = config.solutionType;

    // Get node templates for detailed parameter information
    auto &nodePool = NodePoolManager::getInstance();
    auto allTemplates = nodePool.getAllTemplates();
    std::map<std::string, NodePoolManager::NodeTemplate> templatesByType;
    for (const auto &template_ : allTemplates) {
      templatesByType[template_.nodeType] = template_;
    }

    // Extract variables from pipeline nodes and defaults
    std::set<std::string> allParams;      // All parameters found
    std::set<std::string> requiredParams; // Parameters that are required
    std::map<std::string, std::string> paramDefaults;
    std::map<std::string, std::string> paramDescriptions;
    std::map<std::string, std::string> paramTypes; // Parameter types
    std::map<std::string, std::vector<std::string>>
        paramUsedInNodes; // Which nodes use this param

    // Extract from pipeline node parameters
    std::regex varPattern("\\$\\{([A-Za-z0-9_]+)\\}");
    Json::Value pipelineNodes(Json::arrayValue);

    for (const auto &node : config.pipeline) {
      Json::Value nodeInfo(Json::objectValue);
      nodeInfo["nodeType"] = node.nodeType;
      nodeInfo["nodeName"] = node.nodeName;

      // Get template info for this node type
      auto templateIt = templatesByType.find(node.nodeType);
      if (templateIt != templatesByType.end()) {
        const auto &nodeTemplate = templateIt->second;
        nodeInfo["displayName"] = nodeTemplate.displayName;
        nodeInfo["description"] = nodeTemplate.description;
        nodeInfo["category"] = nodeTemplate.category;

        // Add required and optional parameters from template
        Json::Value requiredParamsArray(Json::arrayValue);
        for (const auto &param : nodeTemplate.requiredParameters) {
          requiredParamsArray.append(param);
        }
        nodeInfo["requiredParameters"] = requiredParamsArray;

        Json::Value optionalParamsArray(Json::arrayValue);
        for (const auto &param : nodeTemplate.optionalParameters) {
          optionalParamsArray.append(param);
        }
        nodeInfo["optionalParameters"] = optionalParamsArray;

        // Add default parameters
        Json::Value defaultParams(Json::objectValue);
        for (const auto &[key, value] : nodeTemplate.defaultParameters) {
          defaultParams[key] = value;
        }
        nodeInfo["defaultParameters"] = defaultParams;
      }

      // Extract variables from node parameters
      Json::Value nodeParams(Json::objectValue);
      for (const auto &param : node.parameters) {
        nodeParams[param.first] = param.second;

        std::string value = param.second;
        std::sregex_iterator iter(value.begin(), value.end(), varPattern);
        std::sregex_iterator end;

        for (; iter != end; ++iter) {
          std::string varName = (*iter)[1].str();
          allParams.insert(varName);
          // Initially mark as required (will be overridden if has default)
          requiredParams.insert(varName);

          // Track which nodes use this parameter
          paramUsedInNodes[varName].push_back(node.nodeType);

          // Try to infer description from parameter name and node template
          if (paramDescriptions.find(varName) == paramDescriptions.end()) {
            std::string desc = "Parameter for " + node.nodeType + " node";
            if (templateIt != templatesByType.end()) {
              const auto &nodeTemplate = templateIt->second;
              // Check if this parameter is in required/optional list
              bool isRequired =
                  std::find(nodeTemplate.requiredParameters.begin(),
                            nodeTemplate.requiredParameters.end(),
                            param.first) !=
                  nodeTemplate.requiredParameters.end();
              if (isRequired) {
                desc = "Required parameter for " + node.nodeType + " (" +
                       param.first + ")";
              } else {
                desc = "Optional parameter for " + node.nodeType + " (" +
                       param.first + ")";
              }
            } else if (param.first == "url" ||
                       varName.find("URL") != std::string::npos) {
              desc = "URL for " + node.nodeType;
            } else if (varName.find("MODEL") != std::string::npos ||
                       varName.find("PATH") != std::string::npos) {
              desc = "File path for " + node.nodeType;
            }
            paramDescriptions[varName] = desc;
          }
        }
      }
      nodeInfo["parameters"] = nodeParams;
      pipelineNodes.append(nodeInfo);
    }

    response["pipeline"] = pipelineNodes;

    // Extract from defaults
    // If a parameter has a default value (literal, not containing variables),
    // it's optional If default value contains variables, those variables are
    // still required
    for (const auto &def : config.defaults) {
      std::string paramName = def.first;
      std::string defaultValue = def.second;
      allParams.insert(paramName);
      paramDefaults[paramName] = defaultValue;

      // Check if default value contains variables
      std::sregex_iterator iter(defaultValue.begin(), defaultValue.end(),
                                varPattern);
      std::sregex_iterator end;
      bool hasVariables = false;
      for (; iter != end; ++iter) {
        std::string varName = (*iter)[1].str();
        allParams.insert(varName);
        requiredParams.insert(varName); // Variables in defaults are required
        hasVariables = true;
      }

      // If default value is literal (no variables), the parameter is optional
      if (!hasVariables) {
        requiredParams.erase(paramName);
      }
    }

    // Build additionalParams schema
    Json::Value additionalParams(Json::objectValue);
    Json::Value required(Json::arrayValue);

    for (const auto &param : allParams) {
      Json::Value paramInfo(Json::objectValue);
      paramInfo["name"] = param;
      paramInfo["type"] = "string";

      // Check if has default
      auto defIt = paramDefaults.find(param);
      bool isRequired = (requiredParams.find(param) != requiredParams.end());

      if (defIt != paramDefaults.end()) {
        std::string defaultValue = defIt->second;
        // Only set default if it's a literal value (doesn't contain variables)
        std::sregex_iterator iter(defaultValue.begin(), defaultValue.end(),
                                  varPattern);
        std::sregex_iterator end;
        if (iter == end) {
          // No variables in default, it's a literal value
          paramInfo["default"] = defaultValue;
          paramInfo["required"] = false;
        } else {
          // Default contains variables, parameter is still required
          paramInfo["required"] = true;
          required.append(param);
        }
      } else {
        paramInfo["required"] = isRequired;
        if (isRequired) {
          required.append(param);
        }
      }

      // Add description
      auto descIt = paramDescriptions.find(param);
      if (descIt != paramDescriptions.end()) {
        paramInfo["description"] = descIt->second;
      } else {
        // Generate default description
        if (param.find("URL") != std::string::npos) {
          paramInfo["description"] = "URL parameter";
        } else if (param.find("PATH") != std::string::npos ||
                   param.find("MODEL") != std::string::npos) {
          paramInfo["description"] = "File path parameter";
        } else {
          paramInfo["description"] = "Solution parameter";
        }
      }

      // Add information about which nodes use this parameter
      auto nodesIt = paramUsedInNodes.find(param);
      if (nodesIt != paramUsedInNodes.end()) {
        Json::Value usedInNodes(Json::arrayValue);
        for (const auto &nodeType : nodesIt->second) {
          usedInNodes.append(nodeType);
        }
        paramInfo["usedInNodes"] = usedInNodes;
      }

      additionalParams[param] = paramInfo;
    }

    response["additionalParams"] = additionalParams;
    response["requiredAdditionalParams"] = required;

    // Add standard instance creation fields
    Json::Value standardFields(Json::objectValue);

    // Required fields
    Json::Value requiredFields(Json::arrayValue);
    requiredFields.append("name");
    standardFields["name"] = Json::Value(Json::objectValue);
    standardFields["name"]["type"] = "string";
    standardFields["name"]["required"] = true;
    standardFields["name"]["description"] =
        "Instance name (pattern: ^[A-Za-z0-9 -_]+$)";
    standardFields["name"]["pattern"] = "^[A-Za-z0-9 -_]+$";

    // Optional fields
    standardFields["group"] = Json::Value(Json::objectValue);
    standardFields["group"]["type"] = "string";
    standardFields["group"]["required"] = false;
    standardFields["group"]["description"] =
        "Group name (pattern: ^[A-Za-z0-9 -_]+$)";
    standardFields["group"]["pattern"] = "^[A-Za-z0-9 -_]+$";

    standardFields["solution"] = Json::Value(Json::objectValue);
    standardFields["solution"]["type"] = "string";
    standardFields["solution"]["required"] = false;
    standardFields["solution"]["description"] =
        "Solution ID (must match existing solution)";
    standardFields["solution"]["default"] = solutionId;

    standardFields["persistent"] = Json::Value(Json::objectValue);
    standardFields["persistent"]["type"] = "boolean";
    standardFields["persistent"]["required"] = false;
    standardFields["persistent"]["description"] = "Save instance to JSON file";
    standardFields["persistent"]["default"] = false;

    standardFields["autoStart"] = Json::Value(Json::objectValue);
    standardFields["autoStart"]["type"] = "boolean";
    standardFields["autoStart"]["required"] = false;
    standardFields["autoStart"]["description"] =
        "Automatically start instance when created";
    standardFields["autoStart"]["default"] = false;

    standardFields["frameRateLimit"] = Json::Value(Json::objectValue);
    standardFields["frameRateLimit"]["type"] = "integer";
    standardFields["frameRateLimit"]["required"] = false;
    standardFields["frameRateLimit"]["description"] = "Frame rate limit (FPS)";
    standardFields["frameRateLimit"]["default"] = 0;
    standardFields["frameRateLimit"]["minimum"] = 0;

    standardFields["detectionSensitivity"] = Json::Value(Json::objectValue);
    standardFields["detectionSensitivity"]["type"] = "string";
    standardFields["detectionSensitivity"]["required"] = false;
    standardFields["detectionSensitivity"]["description"] =
        "Detection sensitivity level";
    standardFields["detectionSensitivity"]["enum"] =
        Json::Value(Json::arrayValue);
    standardFields["detectionSensitivity"]["enum"].append("Low");
    standardFields["detectionSensitivity"]["enum"].append("Medium");
    standardFields["detectionSensitivity"]["enum"].append("High");
    standardFields["detectionSensitivity"]["enum"].append("Normal");
    standardFields["detectionSensitivity"]["enum"].append("Slow");
    standardFields["detectionSensitivity"]["default"] = "Low";

    standardFields["detectorMode"] = Json::Value(Json::objectValue);
    standardFields["detectorMode"]["type"] = "string";
    standardFields["detectorMode"]["required"] = false;
    standardFields["detectorMode"]["description"] = "Detector mode";
    standardFields["detectorMode"]["enum"] = Json::Value(Json::arrayValue);
    standardFields["detectorMode"]["enum"].append("SmartDetection");
    standardFields["detectorMode"]["enum"].append("FullRegionInference");
    standardFields["detectorMode"]["enum"].append("MosaicInference");
    standardFields["detectorMode"]["default"] = "SmartDetection";

    standardFields["metadataMode"] = Json::Value(Json::objectValue);
    standardFields["metadataMode"]["type"] = "boolean";
    standardFields["metadataMode"]["required"] = false;
    standardFields["metadataMode"]["description"] = "Enable metadata mode";
    standardFields["metadataMode"]["default"] = false;

    standardFields["statisticsMode"] = Json::Value(Json::objectValue);
    standardFields["statisticsMode"]["type"] = "boolean";
    standardFields["statisticsMode"]["required"] = false;
    standardFields["statisticsMode"]["description"] = "Enable statistics mode";
    standardFields["statisticsMode"]["default"] = false;

    standardFields["diagnosticsMode"] = Json::Value(Json::objectValue);
    standardFields["diagnosticsMode"]["type"] = "boolean";
    standardFields["diagnosticsMode"]["required"] = false;
    standardFields["diagnosticsMode"]["description"] =
        "Enable diagnostics mode";
    standardFields["diagnosticsMode"]["default"] = false;

    standardFields["debugMode"] = Json::Value(Json::objectValue);
    standardFields["debugMode"]["type"] = "boolean";
    standardFields["debugMode"]["required"] = false;
    standardFields["debugMode"]["description"] = "Enable debug mode";
    standardFields["debugMode"]["default"] = false;

    response["standardFields"] = standardFields;
    response["requiredStandardFields"] = requiredFields;

    // Add flexible input/output parameters info
    // These are auto-detected by pipeline builder and can be added to any
    // instance
    Json::Value flexibleIO(Json::objectValue);

    // Input options
    Json::Value inputOptions(Json::objectValue);
    inputOptions["description"] =
        "Choose ONE input source. Pipeline builder auto-detects input type.";
    Json::Value inputParams(Json::objectValue);

    inputParams["FILE_PATH"] = Json::Value(Json::objectValue);
    inputParams["FILE_PATH"]["type"] = "string";
    inputParams["FILE_PATH"]["description"] =
        "Local video file path or URL (rtsp://, rtmp://, http://)";
    inputParams["FILE_PATH"]["example"] =
        "./cvedix_data/test_video/example.mp4";

    inputParams["RTSP_SRC_URL"] = Json::Value(Json::objectValue);
    inputParams["RTSP_SRC_URL"]["type"] = "string";
    inputParams["RTSP_SRC_URL"]["description"] =
        "RTSP stream URL (overrides FILE_PATH)";
    inputParams["RTSP_SRC_URL"]["example"] = "rtsp://camera-ip:8554/stream";

    inputParams["RTMP_SRC_URL"] = Json::Value(Json::objectValue);
    inputParams["RTMP_SRC_URL"]["type"] = "string";
    inputParams["RTMP_SRC_URL"]["description"] = "RTMP input stream URL";
    inputParams["RTMP_SRC_URL"]["example"] =
        "rtmp://input-server:1935/live/input";

    inputParams["HLS_URL"] = Json::Value(Json::objectValue);
    inputParams["HLS_URL"]["type"] = "string";
    inputParams["HLS_URL"]["description"] = "HLS stream URL (.m3u8)";
    inputParams["HLS_URL"]["example"] = "http://example.com/stream.m3u8";

    inputOptions["parameters"] = inputParams;
    flexibleIO["input"] = inputOptions;

    // Output options (can combine multiple)
    Json::Value outputOptions(Json::objectValue);
    outputOptions["description"] =
        "Add any combination of outputs. Pipeline builder auto-adds nodes.";
    Json::Value outputParams(Json::objectValue);

    // MQTT
    outputParams["MQTT_BROKER_URL"] = Json::Value(Json::objectValue);
    outputParams["MQTT_BROKER_URL"]["type"] = "string";
    outputParams["MQTT_BROKER_URL"]["description"] =
        "MQTT broker address (enables MQTT output)";
    outputParams["MQTT_BROKER_URL"]["example"] = "localhost";

    outputParams["MQTT_PORT"] = Json::Value(Json::objectValue);
    outputParams["MQTT_PORT"]["type"] = "string";
    outputParams["MQTT_PORT"]["description"] = "MQTT broker port";
    outputParams["MQTT_PORT"]["default"] = "1883";

    outputParams["MQTT_TOPIC"] = Json::Value(Json::objectValue);
    outputParams["MQTT_TOPIC"]["type"] = "string";
    outputParams["MQTT_TOPIC"]["description"] = "MQTT topic for events";
    outputParams["MQTT_TOPIC"]["default"] = "events";

    // RTMP output
    outputParams["RTMP_URL"] = Json::Value(Json::objectValue);
    outputParams["RTMP_URL"]["type"] = "string";
    outputParams["RTMP_URL"]["description"] =
        "RTMP destination URL (enables RTMP streaming output)";
    outputParams["RTMP_URL"]["example"] = "rtmp://server:1935/live/stream";

    // Screen display
    outputParams["ENABLE_SCREEN_DES"] = Json::Value(Json::objectValue);
    outputParams["ENABLE_SCREEN_DES"]["type"] = "string";
    outputParams["ENABLE_SCREEN_DES"]["description"] =
        "Enable screen display (true/false)";
    outputParams["ENABLE_SCREEN_DES"]["default"] = "false";

    // Recording
    outputParams["RECORD_PATH"] = Json::Value(Json::objectValue);
    outputParams["RECORD_PATH"]["type"] = "string";
    outputParams["RECORD_PATH"]["description"] =
        "Path for video recording output";
    outputParams["RECORD_PATH"]["example"] = "./output/recordings";

    outputOptions["parameters"] = outputParams;
    flexibleIO["output"] = outputOptions;

    // Zone info for BA solutions
    Json::Value zoneParams(Json::objectValue);
    zoneParams["ZONE_ID"] = Json::Value(Json::objectValue);
    zoneParams["ZONE_ID"]["type"] = "string";
    zoneParams["ZONE_ID"]["description"] = "Zone identifier for BA events";
    zoneParams["ZONE_ID"]["default"] = "zone_1";

    zoneParams["ZONE_NAME"] = Json::Value(Json::objectValue);
    zoneParams["ZONE_NAME"]["type"] = "string";
    zoneParams["ZONE_NAME"]["description"] = "Zone display name for BA events";
    zoneParams["ZONE_NAME"]["default"] = "Default Zone";

    flexibleIO["zoneInfo"] = zoneParams;

    response["flexibleInputOutput"] = flexibleIO;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/solution/" << solutionId
                << "/parameters - Success - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/solution/" << solutionId
                 << "/parameters - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    std::cerr << "[SolutionHandler] Exception: " << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/solution/" << solutionId
                 << "/parameters - Unknown exception - " << duration.count()
                 << "ms";
    }
    std::cerr << "[SolutionHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void SolutionHandler::getSolutionInstanceBody(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  std::string solutionId = extractSolutionId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/solution/" << solutionId
              << "/instance-body - Get instance body";
  }

  try {
    if (!solution_registry_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR
            << "[API] GET /v1/core/solution/" << solutionId
            << "/instance-body - Error: Solution registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Solution registry not initialized"));
      return;
    }

    if (solutionId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/solution/{id}/instance-body - "
                        "Error: Solution ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Solution ID is required"));
      return;
    }

    auto optConfig = solution_registry_->getSolution(solutionId);

    // If solution not found in registry, try to load from default_solutions
    // directory
    if (!optConfig.has_value()) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO
            << "[API] Solution " << solutionId
            << " not found in registry, trying to load from default_solutions";
      }

      if (!solution_registry_ || !solution_storage_) {
        callback(createErrorResponse(
            500, "Internal server error",
            "Solution registry or storage not initialized"));
        return;
      }

      // Check if this is a system default solution ID (even if not loaded yet)
      // System default solutions should not be loaded from default_solutions
      // directory
      if (solution_registry_->isDefaultSolution(solutionId)) {
        // This is a system default solution, but not found in registry
        // This shouldn't happen normally, but handle gracefully
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] System default solution " << solutionId
                       << " not found in registry";
        }
        callback(createErrorResponse(500, "Internal server error",
                                     "System default solution not available: " +
                                         solutionId));
        return;
      }

      // Try to load from default_solutions directory
      std::string error;
      auto loadedConfig = loadDefaultSolutionFromFile(solutionId, error);
      if (loadedConfig.has_value()) {
        SolutionConfig config = loadedConfig.value();

        // Double-check: ensure solutionId matches and check for conflicts
        if (config.solutionId != solutionId) {
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] Solution ID mismatch: requested "
                         << solutionId << ", got " << config.solutionId;
          }
          callback(createErrorResponse(
              400, "Bad request",
              "Solution ID mismatch in file: expected " + solutionId +
                  ", got " + config.solutionId));
          return;
        }

        // Check if solution already exists (race condition check)
        // Also check if it's a system default solution
        if (solution_registry_->hasSolution(config.solutionId)) {
          // Solution exists, get it
          optConfig = solution_registry_->getSolution(solutionId);
        } else if (solution_registry_->isDefaultSolution(config.solutionId)) {
          // This shouldn't happen, but handle it
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] Attempted to load system default solution "
                         << config.solutionId << " from file";
          }
          callback(createErrorResponse(
              403, "Forbidden",
              "Cannot load system default solution from file: " +
                  config.solutionId));
          return;
        } else {
          // Validate node types before registering
          auto &nodePool = NodePoolManager::getInstance();
          auto allTemplates = nodePool.getAllTemplates();
          std::set<std::string> supportedNodeTypes;
          for (const auto &template_ : allTemplates) {
            supportedNodeTypes.insert(template_.nodeType);
          }

          bool hasUnsupportedNodes = false;
          for (const auto &nodeConfig : config.pipeline) {
            if (supportedNodeTypes.find(nodeConfig.nodeType) ==
                supportedNodeTypes.end()) {
              hasUnsupportedNodes = true;
              break;
            }
          }

          if (!hasUnsupportedNodes) {
            // Final check before registering (double-check for race condition)
            if (solution_registry_->hasSolution(config.solutionId)) {
              // Another thread already registered it, get the existing one
              optConfig = solution_registry_->getSolution(solutionId);
            } else {
              // Register solution
              solution_registry_->registerSolution(config);

              // Verify registration succeeded
              auto registeredConfig =
                  solution_registry_->getSolution(solutionId);
              if (!registeredConfig.has_value()) {
                if (isApiLoggingEnabled()) {
                  PLOG_ERROR << "[API] Failed to register solution: "
                             << solutionId;
                }
                callback(createErrorResponse(500, "Internal server error",
                                             "Failed to register solution: " +
                                                 solutionId));
                return;
              }

              // Save to storage (non-blocking, failure is acceptable)
              if (!solution_storage_->saveSolution(config)) {
                if (isApiLoggingEnabled()) {
                  PLOG_WARNING << "[API] Failed to save solution to storage: "
                               << solutionId;
                }
                // Continue anyway - solution is registered in memory
              }

              // Create nodes for node types in this solution
              nodePool.createNodesFromSolution(config);

              if (isApiLoggingEnabled()) {
                PLOG_INFO << "[API] Auto-loaded default solution: "
                          << solutionId;
              }

              optConfig = registeredConfig;
            }
          } else {
            if (isApiLoggingEnabled()) {
              PLOG_WARNING << "[API] Cannot load default solution "
                           << solutionId << ": contains unsupported node types";
            }
            callback(createErrorResponse(
                400, "Bad request",
                "Default solution contains unsupported node types"));
            return;
          }
        }
      } else {
        // Not found in default_solutions either
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] GET /v1/core/solution/" << solutionId
                       << "/instance-body - Not found - " << duration.count()
                       << "ms";
        }
        callback(createErrorResponse(404, "Not found",
                                     "Solution not found: " + solutionId +
                                         ". Available default solutions can be "
                                         "loaded via GET /v1/core/solution"));
        return;
      }
    }

    if (!optConfig.has_value()) {
      callback(createErrorResponse(404, "Not found",
                                   "Solution not found: " + solutionId));
      return;
    }

    const SolutionConfig &config = optConfig.value();

    // Check if minimal mode is requested
    bool minimalMode = false;
    std::string minimalParam = req->getParameter("minimal");
    if (!minimalParam.empty() &&
        (minimalParam == "true" || minimalParam == "1")) {
      minimalMode = true;
    }

    // Build example request body
    Json::Value body(Json::objectValue);

    // Standard fields - always include essential ones
    body["name"] = "example_instance";
    body["solution"] = solutionId;

    // Include optional but commonly used fields
    if (!minimalMode) {
      body["group"] = "default";
      body["persistent"] = false;
      body["autoStart"] = false;
      body["frameRateLimit"] = 0;
      body["detectionSensitivity"] = "Low";
      body["detectorMode"] = "SmartDetection";
      body["metadataMode"] = false;
      body["statisticsMode"] = false;
      body["diagnosticsMode"] = false;
      body["debugMode"] = false;
    } else {
      // Minimal mode: only include commonly used optional fields
      body["group"] = "default";
      body["autoStart"] = false;
    }

    // Get node templates for additional parameter information
    auto &nodePool = NodePoolManager::getInstance();
    auto allTemplates = nodePool.getAllTemplates();
    std::map<std::string, NodePoolManager::NodeTemplate> templatesByType;
    for (const auto &template_ : allTemplates) {
      templatesByType[template_.nodeType] = template_;
    }

    // Extract variables from pipeline and defaults
    std::set<std::string> allParams;
    std::map<std::string, std::string> paramDefaults;
    std::regex varPattern("\\$\\{([A-Za-z0-9_]+)\\}");

    // Extract from pipeline nodes
    for (const auto &node : config.pipeline) {
      // Extract variables from node parameters
      for (const auto &param : node.parameters) {
        std::string value = param.second;
        std::sregex_iterator iter(value.begin(), value.end(), varPattern);
        std::sregex_iterator end;
        for (; iter != end; ++iter) {
          std::string varName = (*iter)[1].str();
          allParams.insert(varName);
        }
      }

      // Extract variables from nodeName template (e.g., {instanceId})
      std::string nodeName = node.nodeName;
      std::regex instanceIdPattern("\\{([A-Za-z0-9_]+)\\}");
      std::sregex_iterator iter(nodeName.begin(), nodeName.end(),
                                instanceIdPattern);
      std::sregex_iterator end;
      for (; iter != end; ++iter) {
        std::string varName = (*iter)[1].str();
        // Note: {instanceId} is handled automatically, but we track it for
        // completeness
        if (varName != "instanceId") {
          // Convert {VAR} to ${VAR} format for consistency
          allParams.insert(varName);
        }
      }

      // Add required parameters from node template if not already in pipeline
      // parameters
      auto templateIt = templatesByType.find(node.nodeType);
      if (templateIt != templatesByType.end()) {
        const auto &nodeTemplate = templateIt->second;

        // Check required parameters from template
        for (const auto &requiredParam : nodeTemplate.requiredParameters) {
          // Check if this required parameter is used in node.parameters
          bool foundInParams = false;
          for (const auto &param : node.parameters) {
            if (param.first == requiredParam) {
              foundInParams = true;
              // Check if value contains variable placeholder
              std::string value = param.second;
              std::sregex_iterator iter(value.begin(), value.end(), varPattern);
              std::sregex_iterator end;
              if (iter != end) {
                // Value contains variable, extract it
                for (; iter != end; ++iter) {
                  std::string varName = (*iter)[1].str();
                  allParams.insert(varName);
                }
              }
              break;
            }
          }

          // If required parameter not found in node.parameters,
          // check if it should be provided via additionalParams
          if (!foundInParams) {
            // Convert parameter name to uppercase with underscores (common
            // pattern)
            std::string upperParam = requiredParam;
            std::transform(upperParam.begin(), upperParam.end(),
                           upperParam.begin(), ::toupper);
            std::replace(upperParam.begin(), upperParam.end(), '-', '_');
            allParams.insert(upperParam);
          }
        }
      }
    }

    // Extract from defaults
    for (const auto &def : config.defaults) {
      std::string paramName = def.first;
      std::string defaultValue = def.second;
      allParams.insert(paramName);
      paramDefaults[paramName] = defaultValue;

      // Also extract variables from default values
      std::sregex_iterator iter(defaultValue.begin(), defaultValue.end(),
                                varPattern);
      std::sregex_iterator end;
      for (; iter != end; ++iter) {
        std::string varName = (*iter)[1].str();
        allParams.insert(varName);
      }
    }

    // Detect output nodes in pipeline and add corresponding output parameters
    // This handles cases where output nodes exist but don't have explicit
    // parameters
    bool hasMQTTBroker = false;
    bool hasRTMPDes = false;
    bool hasScreenDes = false;

    for (const auto &node : config.pipeline) {
      std::string nodeType = node.nodeType;
      if (nodeType == "json_mqtt_broker" ||
          nodeType == "json_crossline_mqtt_broker" ||
          nodeType == "json_enhanced_mqtt_broker") {
        hasMQTTBroker = true;
      } else if (nodeType == "rtmp_des") {
        hasRTMPDes = true;
      } else if (nodeType == "screen_des") {
        hasScreenDes = true;
      }
    }

    // Add MQTT output parameters if MQTT broker node exists
    if (hasMQTTBroker) {
      if (allParams.find("MQTT_BROKER_URL") == allParams.end()) {
        allParams.insert("MQTT_BROKER_URL");
      }
      if (allParams.find("MQTT_PORT") == allParams.end()) {
        allParams.insert("MQTT_PORT");
        paramDefaults["MQTT_PORT"] = "1883";
      }
      if (allParams.find("MQTT_TOPIC") == allParams.end()) {
        allParams.insert("MQTT_TOPIC");
        paramDefaults["MQTT_TOPIC"] = "events";
      }
      if (allParams.find("MQTT_USERNAME") == allParams.end()) {
        allParams.insert("MQTT_USERNAME");
        paramDefaults["MQTT_USERNAME"] = "";
      }
      if (allParams.find("MQTT_PASSWORD") == allParams.end()) {
        allParams.insert("MQTT_PASSWORD");
        paramDefaults["MQTT_PASSWORD"] = "";
      }
    }

    // Add RTMP output parameter if RTMP destination node exists
    if (hasRTMPDes) {
      if (allParams.find("RTMP_URL") == allParams.end() &&
          allParams.find("RTMP_DES_URL") == allParams.end()) {
        allParams.insert("RTMP_URL");
      }
    }

    // Add screen output parameter if screen destination node exists
    if (hasScreenDes) {
      if (allParams.find("ENABLE_SCREEN_DES") == allParams.end()) {
        allParams.insert("ENABLE_SCREEN_DES");
        paramDefaults["ENABLE_SCREEN_DES"] = "false";
      }
    }

    // Helper function to generate example value based on parameter name
    auto generateExampleValue = [](const std::string &param) -> std::string {
      std::string upperParam = param;
      std::transform(upperParam.begin(), upperParam.end(), upperParam.begin(),
                     ::toupper);

      // Flexible Output - MQTT parameters (check before generic URL pattern)
      if (upperParam == "MQTT_BROKER_URL") {
        return "localhost";
      } else if (upperParam == "MQTT_PORT") {
        return "1883";
      } else if (upperParam == "MQTT_TOPIC") {
        return "events";
      } else if (upperParam == "MQTT_USERNAME") {
        return "";
      } else if (upperParam == "MQTT_PASSWORD") {
        return "";
      }

      // Flexible Input parameters (auto-detected by pipeline builder)
      if (upperParam == "RTSP_SRC_URL") {
        return "rtsp://camera-ip:8554/stream";
      } else if (upperParam == "RTMP_SRC_URL") {
        return "rtmp://input-server:1935/live/input";
      } else if (upperParam == "HLS_URL") {
        return "http://example.com/stream.m3u8";
      } else if (upperParam == "HTTP_URL") {
        return "http://example.com/video.mp4";
      }

      // URL parameters (legacy)
      if (upperParam.find("RTSP_URL") != std::string::npos ||
          upperParam == "RTSP_URL") {
        return "rtsp://example.com/stream";
      } else if (upperParam.find("RTMP_URL") != std::string::npos ||
                 upperParam == "RTMP_URL") {
        return "rtmp://example.com/live/stream";
      } else if (upperParam.find("URL") != std::string::npos) {
        return "http://example.com";
      }

      // Flexible Output - RTMP destination
      if (upperParam == "RTMP_URL") {
        return "rtmp://server:1935/live/stream";
      }

      // Flexible Output - Screen display
      if (upperParam == "ENABLE_SCREEN_DES") {
        return "false";
      }

      // Crossline specific parameters
      if (upperParam == "ZONE_ID") {
        return "zone_1";
      } else if (upperParam == "ZONE_NAME") {
        return "Default Zone";
      } else if (upperParam.find("CROSSLINE_START_X") != std::string::npos) {
        return "0";
      } else if (upperParam.find("CROSSLINE_START_Y") != std::string::npos) {
        return "250";
      } else if (upperParam.find("CROSSLINE_END_X") != std::string::npos) {
        return "700";
      } else if (upperParam.find("CROSSLINE_END_Y") != std::string::npos) {
        return "220";
      }

      // Model and file paths
      if (upperParam.find("MODEL_PATH") != std::string::npos ||
          upperParam == "MODEL_PATH") {
        return "./cvedix_data/models/example.model";
      } else if (upperParam.find("MODEL_CONFIG_PATH") != std::string::npos ||
                 upperParam == "MODEL_CONFIG_PATH") {
        return "./cvedix_data/models/example.config";
      } else if (upperParam.find("WEIGHTS_PATH") != std::string::npos ||
                 upperParam == "WEIGHTS_PATH") {
        return "./cvedix_data/models/example.weights";
      } else if (upperParam.find("CONFIG_PATH") != std::string::npos ||
                 upperParam == "CONFIG_PATH") {
        return "./cvedix_data/models/example.cfg";
      } else if (upperParam.find("LABELS_PATH") != std::string::npos ||
                 upperParam == "LABELS_PATH") {
        return "./cvedix_data/models/example.labels";
      } else if (upperParam.find("FILE_PATH") != std::string::npos ||
                 upperParam == "FILE_PATH") {
        return "./cvedix_data/test_video/example.mp4";
      } else if (upperParam.find("PATH") != std::string::npos ||
                 upperParam.find("MODEL") != std::string::npos) {
        return "./cvedix_data/models/example.model";
      }

      // Directory parameters
      if (upperParam.find("DIR") != std::string::npos ||
          upperParam.find("OUTPUT") != std::string::npos) {
        return "./output";
      }

      // Numeric parameters
      if (upperParam.find("WIDTH") != std::string::npos) {
        return "416";
      } else if (upperParam.find("HEIGHT") != std::string::npos) {
        return "416";
      } else if (upperParam.find("THRESHOLD") != std::string::npos ||
                 upperParam.find("SCORE") != std::string::npos) {
        return "0.5";
      } else if (upperParam.find("RATIO") != std::string::npos) {
        return "1.0";
      } else if (upperParam.find("CHANNEL") != std::string::npos) {
        return "0";
      }

      // Boolean-like parameters
      if (upperParam.find("ENABLE") != std::string::npos) {
        return "true";
      } else if (upperParam.find("DISABLE") != std::string::npos) {
        return "false";
      }

      // Default
      return "example_value";
    };

    // List of standard fields that should NOT be in additionalParams
    // These are already handled at root level
    std::set<std::string> standardFields = {"detectionSensitivity",
                                            "DETECTION_SENSITIVITY",
                                            "detectorMode",
                                            "DETECTOR_MODE",
                                            "sensorModality",
                                            "SENSOR_MODALITY",
                                            "movementSensitivity",
                                            "MOVEMENT_SENSITIVITY",
                                            "frameRateLimit",
                                            "FRAME_RATE_LIMIT",
                                            "metadataMode",
                                            "METADATA_MODE",
                                            "statisticsMode",
                                            "STATISTICS_MODE",
                                            "diagnosticsMode",
                                            "DIAGNOSTICS_MODE",
                                            "debugMode",
                                            "DEBUG_MODE",
                                            "autoStart",
                                            "AUTO_START",
                                            "persistent",
                                            "PERSISTENT",
                                            "name",
                                            "NAME",
                                            "group",
                                            "GROUP",
                                            "solution",
                                            "SOLUTION"};

    // Define output-related parameters
    std::set<std::string> outputParams = {
        "MQTT_BROKER_URL", "MQTT_PORT",         "MQTT_TOPIC",
        "MQTT_USERNAME",   "MQTT_PASSWORD",     "RTMP_URL",
        "RTMP_DES_URL",    "ENABLE_SCREEN_DES", "RECORD_PATH"};

    // Build additionalParams with input/output structure
    Json::Value additionalParams(Json::objectValue);
    Json::Value inputParams(Json::objectValue);
    Json::Value outputParamsObj(Json::objectValue);

    for (const auto &param : allParams) {
      // Skip standard fields that are already at root level
      std::string upperParam = param;
      std::transform(upperParam.begin(), upperParam.end(), upperParam.begin(),
                     ::toupper);
      if (standardFields.find(param) != standardFields.end() ||
          standardFields.find(upperParam) != standardFields.end()) {
        continue; // Skip this parameter
      }

      // In minimal mode, only include required parameters (no default value or
      // default contains variables) Exception: Include output parameters if
      // solution has corresponding output nodes (they work together)
      if (minimalMode) {
        std::string upperParamCheck = param;
        std::transform(upperParamCheck.begin(), upperParamCheck.end(),
                       upperParamCheck.begin(), ::toupper);

        // Always include all MQTT parameters if solution has MQTT broker node
        if (hasMQTTBroker && (upperParamCheck == "MQTT_BROKER_URL" ||
                              upperParamCheck == "MQTT_PORT" ||
                              upperParamCheck == "MQTT_TOPIC" ||
                              upperParamCheck == "MQTT_USERNAME" ||
                              upperParamCheck == "MQTT_PASSWORD")) {
          // Include it - don't skip
        } else if (hasRTMPDes && (upperParamCheck == "RTMP_URL" ||
                                  upperParamCheck == "RTMP_DES_URL")) {
          // Always include RTMP_URL if solution has RTMP destination node
          // Include it - don't skip
        } else if (hasScreenDes && upperParamCheck == "ENABLE_SCREEN_DES") {
          // Always include ENABLE_SCREEN_DES if solution has screen destination
          // node Include it - don't skip
        } else {
          auto defIt = paramDefaults.find(param);
          if (defIt != paramDefaults.end()) {
            std::string defaultValue = defIt->second;
            // Check if default contains variables (if yes, it's required)
            std::sregex_iterator iter(defaultValue.begin(), defaultValue.end(),
                                      varPattern);
            std::sregex_iterator end;
            if (iter == end) {
              // Has literal default value, skip in minimal mode (it's optional)
              continue;
            }
            // Default contains variables, it's required - include it
          }
          // No default value, it's required - include it
        }
      }

      // Determine example value
      std::string exampleValue;
      auto defIt = paramDefaults.find(param);
      if (defIt != paramDefaults.end()) {
        std::string defaultValue = defIt->second;
        // Check if default contains variables
        std::sregex_iterator iter(defaultValue.begin(), defaultValue.end(),
                                  varPattern);
        std::sregex_iterator end;
        if (iter == end) {
          // Literal default value - use it
          exampleValue = defaultValue;
        } else {
          // Default contains variables, use generated example value
          exampleValue = generateExampleValue(param);
        }
      } else {
        // No default, use generated example value based on parameter name
        exampleValue = generateExampleValue(param);
      }

      // Put in output or input based on parameter type
      if (outputParams.find(upperParam) != outputParams.end()) {
        outputParamsObj[param] = exampleValue;
      } else {
        inputParams[param] = exampleValue;
      }
    }

    // In minimal mode, filter out optional output parameters
    if (minimalMode) {
      // Remove empty optional output parameters
      // But keep all MQTT parameters if solution has MQTT broker node (they
      // work together)
      std::vector<std::string> keysToRemove;
      for (const auto &key : outputParamsObj.getMemberNames()) {
        std::string upperKey = key;
        std::transform(upperKey.begin(), upperKey.end(), upperKey.begin(),
                       ::toupper);
        std::string value = outputParamsObj[key].asString();

        // Keep all MQTT parameters if solution has MQTT broker node
        if (hasMQTTBroker &&
            (upperKey == "MQTT_BROKER_URL" || upperKey == "MQTT_PORT" ||
             upperKey == "MQTT_TOPIC" || upperKey == "MQTT_USERNAME" ||
             upperKey == "MQTT_PASSWORD")) {
          continue; // Keep this parameter
        }

        // Keep ENABLE_SCREEN_DES if solution has screen destination node
        if (hasScreenDes && upperKey == "ENABLE_SCREEN_DES") {
          continue; // Keep this parameter
        }

        // Keep RTMP_URL if solution has RTMP destination node
        if (hasRTMPDes &&
            (upperKey == "RTMP_URL" || upperKey == "RTMP_DES_URL")) {
          continue; // Keep this parameter
        }

        if (value.empty() || value == "false" || value == "1883" ||
            value == "events") {
          keysToRemove.push_back(key);
        }
      }
      for (const auto &key : keysToRemove) {
        outputParamsObj.removeMember(key);
      }
    } else {
      // Add default output parameters if not already present (full mode)
      if (hasScreenDes && !outputParamsObj.isMember("ENABLE_SCREEN_DES")) {
        outputParamsObj["ENABLE_SCREEN_DES"] = "false";
      }
      // Only add MQTT parameters if solution has MQTT broker node
      if (hasMQTTBroker) {
        if (!outputParamsObj.isMember("MQTT_BROKER_URL")) {
          outputParamsObj["MQTT_BROKER_URL"] = "";
        }
        if (!outputParamsObj.isMember("MQTT_PORT")) {
          outputParamsObj["MQTT_PORT"] = "1883";
        }
        if (!outputParamsObj.isMember("MQTT_TOPIC")) {
          outputParamsObj["MQTT_TOPIC"] = "events";
        }
        if (!outputParamsObj.isMember("MQTT_USERNAME")) {
          outputParamsObj["MQTT_USERNAME"] = "";
        }
        if (!outputParamsObj.isMember("MQTT_PASSWORD")) {
          outputParamsObj["MQTT_PASSWORD"] = "";
        }
      }
      // Only add RTMP parameter if solution has RTMP destination node
      if (hasRTMPDes && !outputParamsObj.isMember("RTMP_URL") &&
          !outputParamsObj.isMember("RTMP_DES_URL")) {
        outputParamsObj["RTMP_URL"] = "";
      }
    }

    additionalParams["input"] = inputParams;
    if (!outputParamsObj.empty()) {
      additionalParams["output"] = outputParamsObj;
    }

    body["additionalParams"] = additionalParams;

    // Add detailed schema metadata for UI
    Json::Value schema(Json::objectValue);

    // Standard fields schema
    Json::Value standardFieldsSchema(Json::objectValue);
    addStandardFieldSchema(standardFieldsSchema, "name", "string", true,
                           "Instance name (pattern: ^[A-Za-z0-9 -_]+$)",
                           "^[A-Za-z0-9 -_]+$", "example_instance");
    addStandardFieldSchema(standardFieldsSchema, "group", "string", false,
                           "Group name (pattern: ^[A-Za-z0-9 -_]+$)",
                           "^[A-Za-z0-9 -_]+$", "default");
    addStandardFieldSchema(standardFieldsSchema, "solution", "string", false,
                           "Solution ID (must match existing solution)", "",
                           solutionId);
    addStandardFieldSchema(standardFieldsSchema, "persistent", "boolean", false,
                           "Save instance to JSON file", "", false);
    addStandardFieldSchema(standardFieldsSchema, "autoStart", "boolean", false,
                           "Automatically start instance when created", "",
                           false);
    addStandardFieldSchema(standardFieldsSchema, "frameRateLimit", "integer",
                           false, "Frame rate limit (FPS, 0 = unlimited)", "",
                           0, 0);
    addStandardFieldSchema(standardFieldsSchema, "detectionSensitivity",
                           "string", false, "Detection sensitivity level", "",
                           "Low", -1, -1,
                           {"Low", "Medium", "High", "Normal", "Slow"});
    addStandardFieldSchema(
        standardFieldsSchema, "detectorMode", "string", false, "Detector mode",
        "", "SmartDetection", -1, -1,
        {"SmartDetection", "FullRegionInference", "MosaicInference"});
    addStandardFieldSchema(
        standardFieldsSchema, "metadataMode", "boolean", false,
        "Enable metadata mode (output detection results as JSON)", "", false);
    addStandardFieldSchema(standardFieldsSchema, "statisticsMode", "boolean",
                           false, "Enable statistics mode", "", false);
    addStandardFieldSchema(standardFieldsSchema, "diagnosticsMode", "boolean",
                           false, "Enable diagnostics mode", "", false);
    addStandardFieldSchema(standardFieldsSchema, "debugMode", "boolean", false,
                           "Enable debug mode", "", false);

    // In minimal mode, only include essential standard fields in schema
    if (minimalMode) {
      Json::Value minimalStandardFields(Json::objectValue);
      minimalStandardFields["name"] = standardFieldsSchema["name"];
      minimalStandardFields["group"] = standardFieldsSchema["group"];
      minimalStandardFields["solution"] = standardFieldsSchema["solution"];
      minimalStandardFields["autoStart"] = standardFieldsSchema["autoStart"];
      schema["standardFields"] = minimalStandardFields;
    } else {
      schema["standardFields"] = standardFieldsSchema;
    }

    // Additional parameters schema (input/output)
    Json::Value additionalParamsSchema(Json::objectValue);
    Json::Value inputParamsSchema(Json::objectValue);
    Json::Value outputParamsSchema(Json::objectValue);

    // Process input parameters
    for (const auto &paramName : inputParams.getMemberNames()) {
      Json::Value paramSchema = buildParameterSchema(
          paramName, inputParams[paramName].asString(), allParams,
          paramDefaults, templatesByType, config);

      // In minimal mode, only include required parameters in schema
      if (!minimalMode || paramSchema["required"].asBool()) {
        inputParamsSchema[paramName] = paramSchema;
      }
    }

    // Process output parameters
    for (const auto &paramName : outputParamsObj.getMemberNames()) {
      Json::Value paramSchema = buildParameterSchema(
          paramName, outputParamsObj[paramName].asString(), allParams,
          paramDefaults, templatesByType, config);

      // In minimal mode, include required parameters OR MQTT parameters if
      // solution has MQTT broker node
      if (minimalMode) {
        std::string upperParamName = paramName;
        std::transform(upperParamName.begin(), upperParamName.end(),
                       upperParamName.begin(), ::toupper);

        // Include if required OR if it's an MQTT parameter and solution has
        // MQTT broker node
        bool isMQTTParam =
            (upperParamName == "MQTT_BROKER_URL" ||
             upperParamName == "MQTT_PORT" || upperParamName == "MQTT_TOPIC" ||
             upperParamName == "MQTT_USERNAME" ||
             upperParamName == "MQTT_PASSWORD");

        // Include if required OR (is MQTT param and has MQTT broker) OR (is
        // ENABLE_SCREEN_DES and has screen des) OR (is RTMP_URL and has RTMP
        // des)
        bool isRTMPParam =
            (upperParamName == "RTMP_URL" || upperParamName == "RTMP_DES_URL");

        if (paramSchema["required"].asBool() ||
            (isMQTTParam && hasMQTTBroker) ||
            (upperParamName == "ENABLE_SCREEN_DES" && hasScreenDes) ||
            (isRTMPParam && hasRTMPDes)) {
          outputParamsSchema[paramName] = paramSchema;
        }
      } else {
        // Full mode: include all
        outputParamsSchema[paramName] = paramSchema;
      }
    }

    additionalParamsSchema["input"] = inputParamsSchema;
    if (!outputParamsSchema.empty()) {
      additionalParamsSchema["output"] = outputParamsSchema;
    }
    schema["additionalParams"] = additionalParamsSchema;

    // Add flexible input/output schema only in full mode
    if (!minimalMode) {
      Json::Value flexibleIOSchema(Json::objectValue);
      flexibleIOSchema["description"] =
          "Flexible input/output options that can be added to any instance";
      flexibleIOSchema["input"] = buildFlexibleInputSchema();
      flexibleIOSchema["output"] = buildFlexibleOutputSchema();
      schema["flexibleInputOutput"] = flexibleIOSchema;
    }

    body["schema"] = schema;

    // Return body directly (no wrapper) - can be used directly to create
    // instance
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/solution/" << solutionId
                << "/instance-body - Success - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(body));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/solution/" << solutionId
                 << "/instance-body - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    std::cerr << "[SolutionHandler] Exception: " << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/solution/" << solutionId
                 << "/instance-body - Unknown exception - " << duration.count()
                 << "ms";
    }
    std::cerr << "[SolutionHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

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
      "/opt/edgeos-api/examples/default_solutions",
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

void SolutionHandler::loadDefaultSolution(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  std::string solutionId = extractSolutionId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/solution/defaults/" << solutionId
              << " - Load default solution";
  }

  try {
    if (!solution_registry_ || !solution_storage_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/solution/defaults/" << solutionId
                   << " - Error: Solution registry or storage not initialized";
      }
      callback(
          createErrorResponse(500, "Internal server error",
                              "Solution registry or storage not initialized"));
      return;
    }

    if (solutionId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/solution/defaults/{id} - Error: "
                        "Solution ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Solution ID is required"));
      return;
    }

    // Load solution from file
    std::string error;
    auto optConfig = loadDefaultSolutionFromFile(solutionId, error);
    if (!optConfig.has_value()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/solution/defaults/" << solutionId
                     << " - Error: " << error;
      }
      callback(createErrorResponse(404, "Not found", error));
      return;
    }

    SolutionConfig config = optConfig.value();

    // Check if solution already exists
    if (solution_registry_->hasSolution(config.solutionId)) {
      // Check if it's a default solution - cannot override default solutions
      if (solution_registry_->isDefaultSolution(config.solutionId)) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] POST /v1/core/solution/defaults/" << solutionId
                       << " - Error: Cannot override default solution: "
                       << config.solutionId;
        }
        callback(createErrorResponse(
            403, "Forbidden",
            "Cannot create solution with ID '" + config.solutionId +
                "': This ID is reserved for a default system solution."));
        return;
      }

      // Solution exists and is not default - return conflict
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/solution/defaults/" << solutionId
                     << " - Error: Solution already exists: "
                     << config.solutionId;
      }
      callback(createErrorResponse(
          409, "Conflict",
          "Solution with ID '" + config.solutionId +
              "' already exists. Use PUT /v1/core/solution/" +
              config.solutionId + " to update it."));
      return;
    }

    // Validate node types
    auto &nodePool = NodePoolManager::getInstance();
    auto allTemplates = nodePool.getAllTemplates();
    std::set<std::string> supportedNodeTypes;
    for (const auto &template_ : allTemplates) {
      supportedNodeTypes.insert(template_.nodeType);
    }

    std::vector<std::string> unsupportedNodeTypes;
    for (const auto &nodeConfig : config.pipeline) {
      if (supportedNodeTypes.find(nodeConfig.nodeType) ==
          supportedNodeTypes.end()) {
        unsupportedNodeTypes.push_back(nodeConfig.nodeType);
      }
    }

    if (!unsupportedNodeTypes.empty()) {
      std::string errorMsg = "Unsupported node types: ";
      for (size_t i = 0; i < unsupportedNodeTypes.size(); i++) {
        if (i > 0)
          errorMsg += ", ";
        errorMsg += unsupportedNodeTypes[i];
      }
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/solution/defaults/" << solutionId
                     << " - Error: " << errorMsg;
      }
      callback(createErrorResponse(400, "Invalid request", errorMsg));
      return;
    }

    // Register solution
    solution_registry_->registerSolution(config);

    // Save to storage
    if (!solution_storage_->saveSolution(config)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/solution/defaults/" << solutionId
                     << " - Warning: Failed to save solution to storage";
      }
      // Continue anyway - solution is registered in memory
    }

    // Create nodes for node types in this solution
    size_t nodesCreated = nodePool.createNodesFromSolution(config);
    if (nodesCreated > 0 && isApiLoggingEnabled()) {
      PLOG_INFO << "[API] Created " << nodesCreated
                << " nodes for solution: " << config.solutionId;
    }

    Json::Value response = solutionConfigToJson(config);
    response["message"] = "Default solution loaded and created successfully";

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/core/solution/defaults/" << solutionId
                << " - Success: Created solution " << config.solutionId << " - "
                << duration.count() << "ms";
    }

    callback(createSuccessResponse(response, 201));
  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/solution/defaults/" << solutionId
                 << " - Exception: " << e.what() << " - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Exception: " + std::string(e.what())));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/solution/defaults/" << solutionId
                 << " - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

// Helper functions for building parameter schema metadata
void SolutionHandler::addStandardFieldSchema(
    Json::Value &schema, const std::string &fieldName, const std::string &type,
    bool required, const std::string &description, const std::string &pattern,
    const Json::Value &defaultValue, int min, int max,
    const std::vector<std::string> &enumValues) const {
  Json::Value fieldSchema(Json::objectValue);
  fieldSchema["name"] = fieldName;
  fieldSchema["type"] = type;
  fieldSchema["required"] = required;
  fieldSchema["description"] = description;

  if (!defaultValue.isNull()) {
    fieldSchema["default"] = defaultValue;
  }

  if (!pattern.empty()) {
    fieldSchema["pattern"] = pattern;
    if (pattern == "^[A-Za-z0-9 -_]+$") {
      fieldSchema["patternDescription"] =
          "Alphanumeric characters, spaces, hyphens, and underscores only";
    }
  }

  if (min >= 0) {
    fieldSchema["minimum"] = min;
  }
  if (max >= 0) {
    fieldSchema["maximum"] = max;
  }

  if (!enumValues.empty()) {
    Json::Value enumArray(Json::arrayValue);
    for (const auto &val : enumValues) {
      enumArray.append(val);
    }
    fieldSchema["enum"] = enumArray;
  }

  // Add UI hints
  Json::Value uiHints(Json::objectValue);
  if (type == "boolean") {
    uiHints["inputType"] = "checkbox";
    uiHints["widget"] = "switch";
  } else if (type == "integer") {
    uiHints["inputType"] = "number";
    uiHints["widget"] = "input";
  } else if (!enumValues.empty()) {
    uiHints["inputType"] = "select";
    uiHints["widget"] = "select";
  } else {
    uiHints["inputType"] = "text";
    uiHints["widget"] = "input";
  }
  fieldSchema["uiHints"] = uiHints;

  // Add examples
  Json::Value examples(Json::arrayValue);
  if (fieldName == "name") {
    examples.append("my_instance");
    examples.append("camera_01");
  } else if (fieldName == "group") {
    examples.append("production");
    examples.append("testing");
  } else if (fieldName == "frameRateLimit") {
    examples.append("0");
    examples.append("10");
    examples.append("30");
  }
  if (examples.size() > 0) {
    fieldSchema["examples"] = examples;
  }

  schema[fieldName] = fieldSchema;
}

Json::Value SolutionHandler::buildParameterSchema(
    const std::string &paramName, const std::string &exampleValue,
    const std::set<std::string> &allParams,
    const std::map<std::string, std::string> &paramDefaults,
    const std::map<std::string, class NodePoolManager::NodeTemplate>
        &templatesByType,
    const class SolutionConfig &config) const {
  Json::Value paramSchema(Json::objectValue);

  paramSchema["name"] = paramName;
  std::string paramType = inferParameterType(paramName);
  paramSchema["type"] = paramType;

  // Check if required
  bool isRequired = false;
  // Check if parameter is used in pipeline with ${VAR} placeholder
  std::regex varPattern("\\$\\{([A-Za-z0-9_]+)\\}");
  for (const auto &node : config.pipeline) {
    for (const auto &param : node.parameters) {
      std::string value = param.second;
      std::sregex_iterator iter(value.begin(), value.end(), varPattern);
      std::sregex_iterator end;
      for (; iter != end; ++iter) {
        std::string varName = (*iter)[1].str();
        if (varName == paramName) {
          isRequired = true;
          break;
        }
      }
      if (isRequired)
        break;
    }
    if (isRequired)
      break;
  }

  paramSchema["required"] = isRequired;
  paramSchema["example"] = exampleValue;

  // Get default value
  auto defIt = paramDefaults.find(paramName);
  if (defIt != paramDefaults.end()) {
    std::string defaultValue = defIt->second;
    // Check if default contains variables
    std::sregex_iterator iter(defaultValue.begin(), defaultValue.end(),
                              varPattern);
    std::sregex_iterator end;
    if (iter == end) {
      // Literal default
      paramSchema["default"] = defaultValue;
    }
  }

  // Add UI hints
  Json::Value uiHints(Json::objectValue);
  uiHints["inputType"] = getInputType(paramName, paramType);
  uiHints["widget"] = getWidgetType(paramName, paramType);
  std::string placeholder = getPlaceholder(paramName);
  if (!placeholder.empty()) {
    uiHints["placeholder"] = placeholder;
  }
  paramSchema["uiHints"] = uiHints;

  // Add validation rules
  Json::Value validation(Json::objectValue);
  addValidationRules(validation, paramName, paramType);
  if (validation.size() > 0) {
    paramSchema["validation"] = validation;
  }

  // Add description
  paramSchema["description"] = getParameterDescription(paramName);

  // Add examples
  Json::Value examples(Json::arrayValue);
  auto exampleValues = getParameterExamples(paramName);
  for (const auto &ex : exampleValues) {
    examples.append(ex);
  }
  if (examples.size() > 0) {
    paramSchema["examples"] = examples;
  }

  // Add category
  paramSchema["category"] = getParameterCategory(paramName);

  // Find which nodes use this parameter
  Json::Value usedInNodes(Json::arrayValue);
  for (const auto &node : config.pipeline) {
    for (const auto &param : node.parameters) {
      std::string value = param.second;
      std::sregex_iterator iter(value.begin(), value.end(), varPattern);
      std::sregex_iterator end;
      for (; iter != end; ++iter) {
        std::string varName = (*iter)[1].str();
        if (varName == paramName) {
          usedInNodes.append(node.nodeType);
          break;
        }
      }
    }
  }
  if (usedInNodes.size() > 0) {
    paramSchema["usedInNodes"] = usedInNodes;
  }

  return paramSchema;
}

Json::Value SolutionHandler::buildFlexibleInputSchema() const {
  Json::Value inputSchema(Json::objectValue);
  inputSchema["description"] =
      "Choose ONE input source. Pipeline builder auto-detects input type.";
  inputSchema["mutuallyExclusive"] = true;

  Json::Value params(Json::objectValue);

  // Helper to build simple parameter schema for flexible params
  auto buildFlexibleParam = [this](const std::string &name,
                                   const std::string &example,
                                   const std::string &desc) -> Json::Value {
    Json::Value param(Json::objectValue);
    param["name"] = name;
    param["type"] = inferParameterType(name);
    param["required"] = false;
    param["example"] = example;
    param["description"] = desc;

    Json::Value uiHints(Json::objectValue);
    uiHints["inputType"] = getInputType(name, param["type"].asString());
    uiHints["widget"] = getWidgetType(name, param["type"].asString());
    std::string placeholder = getPlaceholder(name);
    if (!placeholder.empty()) {
      uiHints["placeholder"] = placeholder;
    }
    param["uiHints"] = uiHints;

    Json::Value validation(Json::objectValue);
    addValidationRules(validation, name, param["type"].asString());
    if (validation.size() > 0) {
      param["validation"] = validation;
    }

    auto examples = getParameterExamples(name);
    if (!examples.empty()) {
      Json::Value examplesArray(Json::arrayValue);
      for (const auto &ex : examples) {
        examplesArray.append(ex);
      }
      param["examples"] = examplesArray;
    }

    param["category"] = getParameterCategory(name);
    return param;
  };

  // FILE_PATH
  params["FILE_PATH"] = buildFlexibleParam(
      "FILE_PATH", "./cvedix_data/test_video/example.mp4",
      "Local video file path or URL (supports file://, rtsp://, rtmp://, "
      "http://, https://). Pipeline builder auto-detects input type.");

  // RTSP_SRC_URL
  params["RTSP_SRC_URL"] = buildFlexibleParam(
      "RTSP_SRC_URL", "rtsp://camera-ip:8554/stream",
      "RTSP stream URL (overrides FILE_PATH if both provided)");

  // RTMP_SRC_URL
  params["RTMP_SRC_URL"] =
      buildFlexibleParam("RTMP_SRC_URL", "rtmp://input-server:1935/live/input",
                         "RTMP input stream URL");

  // HLS_URL
  params["HLS_URL"] = buildFlexibleParam(
      "HLS_URL", "http://example.com/stream.m3u8", "HLS stream URL (.m3u8)");

  inputSchema["parameters"] = params;
  return inputSchema;
}

Json::Value SolutionHandler::buildFlexibleOutputSchema() const {
  Json::Value outputSchema(Json::objectValue);
  outputSchema["description"] =
      "Add any combination of outputs. Pipeline builder auto-adds nodes.";
  outputSchema["mutuallyExclusive"] = false;

  Json::Value params(Json::objectValue);

  // Helper to build simple parameter schema for flexible params
  auto buildFlexibleParam = [this](const std::string &name,
                                   const std::string &example,
                                   const std::string &desc) -> Json::Value {
    Json::Value param(Json::objectValue);
    param["name"] = name;
    param["type"] = inferParameterType(name);
    param["required"] = false;
    param["example"] = example;
    param["description"] = desc;

    Json::Value uiHints(Json::objectValue);
    uiHints["inputType"] = getInputType(name, param["type"].asString());
    uiHints["widget"] = getWidgetType(name, param["type"].asString());
    std::string placeholder = getPlaceholder(name);
    if (!placeholder.empty()) {
      uiHints["placeholder"] = placeholder;
    }
    param["uiHints"] = uiHints;

    Json::Value validation(Json::objectValue);
    addValidationRules(validation, name, param["type"].asString());
    if (validation.size() > 0) {
      param["validation"] = validation;
    }

    auto examples = getParameterExamples(name);
    if (!examples.empty()) {
      Json::Value examplesArray(Json::arrayValue);
      for (const auto &ex : examples) {
        examplesArray.append(ex);
      }
      param["examples"] = examplesArray;
    }

    param["category"] = getParameterCategory(name);
    return param;
  };

  // MQTT
  params["MQTT_BROKER_URL"] = buildFlexibleParam(
      "MQTT_BROKER_URL", "localhost",
      "MQTT broker address (enables MQTT output). Leave empty to disable.");
  params["MQTT_PORT"] =
      buildFlexibleParam("MQTT_PORT", "1883", "MQTT broker port");
  params["MQTT_TOPIC"] = buildFlexibleParam("MQTT_TOPIC", "events",
                                            "MQTT topic for publishing events");

  // RTMP
  params["RTMP_URL"] = buildFlexibleParam(
      "RTMP_URL", "rtmp://server:1935/live/stream",
      "RTMP destination URL (enables RTMP streaming output)");

  // Screen
  params["ENABLE_SCREEN_DES"] = buildFlexibleParam(
      "ENABLE_SCREEN_DES", "false", "Enable screen display (true/false)");

  // Recording
  params["RECORD_PATH"] = buildFlexibleParam(
      "RECORD_PATH", "./output/recordings", "Path for video recording output");

  outputSchema["parameters"] = params;
  return outputSchema;
}

// Helper functions for parameter metadata (similar to NodeHandler)
std::string
SolutionHandler::inferParameterType(const std::string &paramName) const {
  std::string upperParam = paramName;
  std::transform(upperParam.begin(), upperParam.end(), upperParam.begin(),
                 ::toupper);

  if (upperParam.find("THRESHOLD") != std::string::npos ||
      upperParam.find("RATIO") != std::string::npos ||
      upperParam.find("SCORE") != std::string::npos) {
    return "number";
  }
  if (upperParam.find("PORT") != std::string::npos ||
      upperParam.find("CHANNEL") != std::string::npos ||
      upperParam.find("WIDTH") != std::string::npos ||
      upperParam.find("HEIGHT") != std::string::npos ||
      upperParam.find("TOP_K") != std::string::npos) {
    return "integer";
  }
  if (upperParam.find("ENABLE") != std::string::npos || upperParam == "OSD" ||
      upperParam.find("DISABLE") != std::string::npos) {
    return "boolean";
  }
  return "string";
}

std::string SolutionHandler::getInputType(const std::string &paramName,
                                          const std::string &paramType) const {
  if (paramType == "number" || paramType == "integer") {
    return "number";
  }
  if (paramType == "boolean") {
    return "checkbox";
  }
  std::string upperParam = paramName;
  std::transform(upperParam.begin(), upperParam.end(), upperParam.begin(),
                 ::toupper);
  if (upperParam.find("URL") != std::string::npos) {
    return "url";
  }
  if (upperParam.find("PATH") != std::string::npos ||
      upperParam.find("DIR") != std::string::npos) {
    return "file";
  }
  return "text";
}

std::string SolutionHandler::getWidgetType(const std::string &paramName,
                                           const std::string &paramType) const {
  if (paramType == "boolean") {
    return "switch";
  }
  if (paramName.find("threshold") != std::string::npos ||
      paramName.find("ratio") != std::string::npos) {
    return "slider";
  }
  std::string upperParam = paramName;
  std::transform(upperParam.begin(), upperParam.end(), upperParam.begin(),
                 ::toupper);
  if (upperParam.find("URL") != std::string::npos) {
    return "url-input";
  }
  if (upperParam.find("PATH") != std::string::npos) {
    return "file-picker";
  }
  return "input";
}

std::string
SolutionHandler::getPlaceholder(const std::string &paramName) const {
  std::string upperParam = paramName;
  std::transform(upperParam.begin(), upperParam.end(), upperParam.begin(),
                 ::toupper);

  if (upperParam == "RTSP_SRC_URL" || upperParam == "RTSP_URL") {
    return "rtsp://camera-ip:8554/stream";
  }
  if (upperParam == "RTMP_SRC_URL" || upperParam == "RTMP_URL") {
    return "rtmp://localhost:1935/live/stream";
  }
  if (upperParam == "FILE_PATH") {
    return "/path/to/video.mp4";
  }
  if (upperParam.find("MODEL_PATH") != std::string::npos) {
    return "/opt/edgeos-api/models/example.onnx";
  }
  if (upperParam == "MQTT_BROKER_URL") {
    return "localhost";
  }
  if (upperParam == "MQTT_PORT") {
    return "1883";
  }
  if (upperParam == "MQTT_TOPIC") {
    return "events";
  }
  return "";
}

void SolutionHandler::addValidationRules(Json::Value &validation,
                                         const std::string &paramName,
                                         const std::string &paramType) const {
  std::string upperParam = paramName;
  std::transform(upperParam.begin(), upperParam.end(), upperParam.begin(),
                 ::toupper);

  if (paramType == "number" || paramType == "integer") {
    if (upperParam.find("THRESHOLD") != std::string::npos ||
        upperParam.find("SCORE") != std::string::npos) {
      validation["min"] = 0.0;
      validation["max"] = 1.0;
      validation["step"] = 0.01;
    }
    if (upperParam.find("RATIO") != std::string::npos) {
      validation["min"] = 0.0;
      validation["max"] = 1.0;
      validation["step"] = 0.1;
    }
    if (upperParam.find("PORT") != std::string::npos) {
      validation["min"] = 1;
      validation["max"] = 65535;
    }
    if (upperParam.find("CHANNEL") != std::string::npos) {
      validation["min"] = 0;
      validation["max"] = 15;
    }
  }

  if (paramType == "string") {
    if (upperParam.find("URL") != std::string::npos) {
      validation["pattern"] = "^(rtsp|rtmp|http|https|file|udp)://.+";
      validation["patternDescription"] =
          "Must be a valid URL (rtsp://, rtmp://, http://, https://, file://, "
          "or udp://)";
    }
    if (upperParam.find("PATH") != std::string::npos) {
      validation["pattern"] = "^[^\\0]+$";
      validation["patternDescription"] = "Must be a valid file path";
    }
  }
}

std::string
SolutionHandler::getParameterDescription(const std::string &paramName) const {
  std::string upperParam = paramName;
  std::transform(upperParam.begin(), upperParam.end(), upperParam.begin(),
                 ::toupper);

  if (upperParam == "FILE_PATH") {
    return "Path to video file or URL (supports file://, rtsp://, rtmp://, "
           "http://, https://). Pipeline builder auto-detects input type.";
  }
  if (upperParam == "RTSP_SRC_URL" || upperParam == "RTSP_URL") {
    return "RTSP stream URL (e.g., rtsp://camera-ip:8554/stream)";
  }
  if (upperParam == "RTMP_SRC_URL" || upperParam == "RTMP_URL") {
    return "RTMP stream URL (e.g., rtmp://localhost:1935/live/stream)";
  }
  if (upperParam.find("MODEL_PATH") != std::string::npos) {
    return "Path to model file (.onnx, .trt, .rknn, etc.)";
  }
  if (upperParam == "MQTT_BROKER_URL") {
    return "MQTT broker address (hostname or IP). Leave empty to disable MQTT "
           "output.";
  }
  if (upperParam == "MQTT_PORT") {
    return "MQTT broker port (default: 1883)";
  }
  if (upperParam == "MQTT_TOPIC") {
    return "MQTT topic to publish messages to";
  }
  if (upperParam == "ENABLE_SCREEN_DES") {
    return "Enable screen display (true/false)";
  }
  if (upperParam.find("THRESHOLD") != std::string::npos) {
    return "Confidence threshold (0.0-1.0). Higher values = fewer detections "
           "but more accurate";
  }
  if (upperParam.find("RATIO") != std::string::npos) {
    return "Resize ratio (0.0-1.0). 1.0 = no resize, smaller values = "
           "downscale";
  }
  return "Parameter: " + paramName;
}

std::vector<std::string>
SolutionHandler::getParameterExamples(const std::string &paramName) const {
  std::vector<std::string> examples;
  std::string upperParam = paramName;
  std::transform(upperParam.begin(), upperParam.end(), upperParam.begin(),
                 ::toupper);

  if (upperParam == "FILE_PATH") {
    examples.push_back("./cvedix_data/test_video/example.mp4");
    examples.push_back("file:///path/to/video.mp4");
    examples.push_back("rtsp://camera-ip:8554/stream");
  }
  if (upperParam == "RTSP_SRC_URL" || upperParam == "RTSP_URL") {
    examples.push_back("rtsp://192.168.1.100:8554/stream1");
    examples.push_back("rtsp://admin:password@camera-ip:554/stream");
  }
  if (upperParam == "RTMP_SRC_URL" || upperParam == "RTMP_URL") {
    examples.push_back("rtmp://localhost:1935/live/stream");
    examples.push_back("rtmp://youtube.com/live2/stream-key");
  }
  if (upperParam.find("MODEL_PATH") != std::string::npos) {
    examples.push_back("/opt/edgeos-api/models/face/yunet.onnx");
    examples.push_back("/opt/edgeos-api/models/trt/yolov8.engine");
  }
  if (upperParam == "MQTT_BROKER_URL") {
    examples.push_back("localhost");
    examples.push_back("192.168.1.100");
    examples.push_back("mqtt.example.com");
  }
  if (upperParam == "MQTT_TOPIC") {
    examples.push_back("detections");
    examples.push_back("events");
    examples.push_back("camera/stream1/events");
  }

  return examples;
}

std::string
SolutionHandler::generateUseCase(const std::string &solutionId,
                                 const std::string &category) const {
  std::string lowerId = solutionId;
  std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(), ::tolower);

  // Generate useCase in Vietnamese based on solutionId patterns
  if (lowerId.find("minimal") != std::string::npos) {
    return "Test nhanh hoặc demo đơn giản";
  }

  if (lowerId.find("file") != std::string::npos &&
      lowerId.find("rtmp") == std::string::npos &&
      lowerId.find("rtsp") == std::string::npos) {
    return "Xử lý video file offline, phân tích batch";
  }

  if (lowerId.find("rtsp") != std::string::npos) {
    return "Giám sát real-time từ camera IP";
  }

  if (lowerId.find("rtmp") != std::string::npos) {
    return "Streaming kết quả detection lên server";
  }

  if (lowerId.find("mqtt") != std::string::npos) {
    return "Tích hợp với hệ thống IoT, gửi events qua MQTT";
  }

  if (lowerId.find("crossline") != std::string::npos ||
      (lowerId.find("ba") != std::string::npos &&
       lowerId.find("crossline") != std::string::npos)) {
    return "Đếm người/đối tượng vượt qua đường, phân tích hành vi";
  }

  if (lowerId.find("mask") != std::string::npos ||
      lowerId.find("rcnn") != std::string::npos) {
    return "Phân đoạn instance và tạo mask cho từng đối tượng";
  }

  if (category == "face_detection") {
    return "Phát hiện khuôn mặt trong video hoặc stream";
  }

  if (category == "object_detection") {
    return "Phát hiện đối tượng tổng quát (người, xe, đồ vật)";
  }

  if (category == "behavior_analysis") {
    return "Phân tích hành vi và đếm đối tượng";
  }

  if (category == "segmentation") {
    return "Phân đoạn và tạo mask cho đối tượng";
  }

  if (category == "face_recognition") {
    return "Nhận diện và so khớp khuôn mặt";
  }

  if (category == "face_processing") {
    return "Xử lý và biến đổi khuôn mặt";
  }

  if (category == "multimodal_analysis") {
    return "Phân tích đa phương tiện với MLLM";
  }

  return "Giải pháp tổng quát cho xử lý video và AI";
}

std::string
SolutionHandler::getParameterCategory(const std::string &paramName) const {
  std::string upperParam = paramName;
  std::transform(upperParam.begin(), upperParam.end(), upperParam.begin(),
                 ::toupper);

  if (upperParam.find("URL") != std::string::npos ||
      upperParam.find("PATH") != std::string::npos ||
      upperParam.find("PORT") != std::string::npos) {
    return "connection";
  }
  if (upperParam.find("THRESHOLD") != std::string::npos ||
      upperParam.find("RATIO") != std::string::npos) {
    return "performance";
  }
  if (upperParam.find("MODEL") != std::string::npos ||
      upperParam.find("WEIGHTS") != std::string::npos ||
      upperParam.find("CONFIG") != std::string::npos ||
      upperParam.find("LABELS") != std::string::npos) {
    return "model";
  }
  if (upperParam.find("MQTT") != std::string::npos ||
      upperParam.find("RTMP") != std::string::npos ||
      upperParam == "ENABLE_SCREEN_DES" || upperParam == "RECORD_PATH") {
    return "output";
  }
  return "general";
}
