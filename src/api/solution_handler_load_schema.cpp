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

