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

