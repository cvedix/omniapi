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

