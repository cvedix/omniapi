#include "api/create_instance_handler.h"
#include "config/system_config.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/metrics_interceptor.h"
#include "core/pipeline_builder_destination_nodes.h"
#include "instances/instance_info.h"
#include "instances/instance_manager.h"
#include "models/create_instance_request.h"
#include "solutions/solution_registry.h"
#include <chrono>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <sstream>

IInstanceManager *CreateInstanceHandler::instance_manager_ = nullptr;
SolutionRegistry *CreateInstanceHandler::solution_registry_ = nullptr;

void CreateInstanceHandler::setInstanceManager(IInstanceManager *manager) {
  instance_manager_ = manager;
}

void CreateInstanceHandler::setSolutionRegistry(SolutionRegistry *registry) {
  solution_registry_ = registry;
}

void CreateInstanceHandler::createInstance(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/instance - Create instance";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if manager is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance - Error: Instance manager "
                      "not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] POST /v1/core/instance - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Parse request
    CreateInstanceRequest createReq;
    std::string parseError;
    if (!parseRequest(*json, createReq, parseError)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance - Parse error: "
                     << parseError;
      }
      callback(createErrorResponse(400, "Invalid request", parseError));
      return;
    }

    // Validate request
    if (!createReq.validate()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance - Validation failed: "
                     << createReq.getValidationError();
      }
      callback(createErrorResponse(400, "Validation failed",
                                   createReq.getValidationError()));
      return;
    }

    // Validate solution if provided
    if (!createReq.solution.empty()) {
      if (!solution_registry_) {
        if (isApiLoggingEnabled()) {
          PLOG_ERROR << "[API] POST /v1/core/instance - Error: Solution "
                        "registry not initialized";
        }
        callback(createErrorResponse(500, "Internal server error",
                                     "Solution registry not initialized"));
        return;
      }

      if (!solution_registry_->hasSolution(createReq.solution)) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] POST /v1/core/instance - Solution not found: "
                       << createReq.solution;
        }
        callback(
            createErrorResponse(400, "Invalid solution",
                                "Solution not found: " + createReq.solution +
                                    ". Please check available solutions using "
                                    "GET /v1/core/solution"));
        return;
      }
    }

    // Check max running instances limit
    auto &systemConfig = SystemConfig::getInstance();
    int maxInstances = systemConfig.getMaxRunningInstances();
    if (maxInstances > 0) {
      int currentCount = instance_manager_->getInstanceCount();
      if (currentCount >= maxInstances) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING
              << "[API] POST /v1/core/instance - Instance limit reached: "
              << currentCount << "/" << maxInstances;
        }
        callback(createErrorResponse(
            429, "Too Many Requests",
            "Maximum instance limit reached: " + std::to_string(maxInstances) +
                ". Current instances: " + std::to_string(currentCount)));
        return;
      }
    }

    // Create instance
    std::string instanceId;
    try {
      instanceId = instance_manager_->createInstance(createReq);
    } catch (const std::invalid_argument &e) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance - Invalid argument: "
                   << e.what() << " - " << duration.count() << "ms";
      }
      callback(createErrorResponse(400, "Invalid request", e.what()));
      return;
    } catch (const std::runtime_error &e) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance - Runtime error: "
                   << e.what() << " - " << duration.count() << "ms";
      }
      callback(createErrorResponse(500, "Failed to create instance", e.what()));
      return;
    } catch (const std::exception &e) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance - Exception: " << e.what()
                   << " - " << duration.count() << "ms";
      }
      callback(createErrorResponse(500, "Failed to create instance",
                                   std::string("Error: ") + e.what()));
      return;
    } catch (...) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance - Unknown error - "
                   << duration.count() << "ms";
      }
      callback(createErrorResponse(
          500, "Failed to create instance",
          "Unknown error occurred while creating instance"));
      return;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance - Failed to create "
                      "instance (empty ID) - "
                   << duration.count() << "ms";
      }
      callback(createErrorResponse(
          500, "Failed to create instance",
          "Instance creation returned empty ID. This should not happen."));
      return;
    }

    // Get instance info
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance - Created but could not "
                        "retrieve info - "
                     << duration.count() << "ms";
      }
      callback(
          createErrorResponse(500, "Internal server error",
                              "Instance created but could not retrieve info"));
      return;
    }

    // Build response
    Json::Value response = instanceInfoToJson(optInfo.value());

    // Add async build status information
    const auto &info = optInfo.value();
    if (info.building) {
      response["building"] = true;
      response["status"] = "building";
      response["message"] = "Pipeline is being built in background";
    } else if (!info.buildError.empty()) {
      response["building"] = false;
      response["status"] = "error";
      response["buildError"] = info.buildError;
    } else {
      response["building"] = false;
      response["status"] = "ready";
    }

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/core/instance - Success: Created instance "
                << instanceId << " (" << info.displayName
                << ", solution: " << info.solutionId
                << ", building: " << (info.building ? "true" : "false")
                << ") - " << duration.count() << "ms";
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }
    std::cerr << "[CreateInstanceHandler] Exception: " << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance - Unknown exception - "
                 << duration.count() << "ms";
    }
    std::cerr << "[CreateInstanceHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void CreateInstanceHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
  resp->addHeader("Access-Control-Max-Age", "3600");

  // Record metrics and call callback
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

bool CreateInstanceHandler::parseRequest(const Json::Value &json,
                                         CreateInstanceRequest &req,
                                         std::string &error) {

  // Required field: name
  if (!json.isMember("name") || !json["name"].isString()) {
    error = "Missing required field: name";
    return false;
  }
  req.name = json["name"].asString();

  // Optional fields
  if (json.isMember("group") && json["group"].isString()) {
    req.group = json["group"].asString();
  }

  if (json.isMember("solution") && json["solution"].isString()) {
    req.solution = json["solution"].asString();
  }

  if (json.isMember("persistent") && json["persistent"].isBool()) {
    req.persistent = json["persistent"].asBool();
  }

  if (json.isMember("frameRateLimit") && json["frameRateLimit"].isNumeric()) {
    req.frameRateLimit = json["frameRateLimit"].asInt();
  }

  if (json.isMember("metadataMode") && json["metadataMode"].isBool()) {
    req.metadataMode = json["metadataMode"].asBool();
  }

  if (json.isMember("statisticsMode") && json["statisticsMode"].isBool()) {
    req.statisticsMode = json["statisticsMode"].asBool();
  }

  if (json.isMember("diagnosticsMode") && json["diagnosticsMode"].isBool()) {
    req.diagnosticsMode = json["diagnosticsMode"].asBool();
  }

  if (json.isMember("debugMode") && json["debugMode"].isBool()) {
    req.debugMode = json["debugMode"].asBool();
  }

  if (json.isMember("detectorMode") && json["detectorMode"].isString()) {
    req.detectorMode = json["detectorMode"].asString();
  }

  if (json.isMember("detectionSensitivity") &&
      json["detectionSensitivity"].isString()) {
    req.detectionSensitivity = json["detectionSensitivity"].asString();
  }

  if (json.isMember("movementSensitivity") &&
      json["movementSensitivity"].isString()) {
    req.movementSensitivity = json["movementSensitivity"].asString();
  }

  if (json.isMember("sensorModality") && json["sensorModality"].isString()) {
    req.sensorModality = json["sensorModality"].asString();
  }

  if (json.isMember("autoStart") && json["autoStart"].isBool()) {
    req.autoStart = json["autoStart"].asBool();
  }

  if (json.isMember("autoRestart") && json["autoRestart"].isBool()) {
    req.autoRestart = json["autoRestart"].asBool();
  }

  if (json.isMember("blockingReadaheadQueue") &&
      json["blockingReadaheadQueue"].isBool()) {
    req.blockingReadaheadQueue = json["blockingReadaheadQueue"].asBool();
  }

  if (json.isMember("inputOrientation") &&
      json["inputOrientation"].isNumeric()) {
    req.inputOrientation = json["inputOrientation"].asInt();
  }

  if (json.isMember("inputPixelLimit") && json["inputPixelLimit"].isNumeric()) {
    req.inputPixelLimit = json["inputPixelLimit"].asInt();
  }

  // Detector configuration (detailed)
  if (json.isMember("detectorModelFile") &&
      json["detectorModelFile"].isString()) {
    req.detectorModelFile = json["detectorModelFile"].asString();
  }
  if (json.isMember("animalConfidenceThreshold") &&
      json["animalConfidenceThreshold"].isNumeric()) {
    req.animalConfidenceThreshold =
        json["animalConfidenceThreshold"].asDouble();
  }
  if (json.isMember("personConfidenceThreshold") &&
      json["personConfidenceThreshold"].isNumeric()) {
    req.personConfidenceThreshold =
        json["personConfidenceThreshold"].asDouble();
  }
  if (json.isMember("vehicleConfidenceThreshold") &&
      json["vehicleConfidenceThreshold"].isNumeric()) {
    req.vehicleConfidenceThreshold =
        json["vehicleConfidenceThreshold"].asDouble();
  }
  if (json.isMember("faceConfidenceThreshold") &&
      json["faceConfidenceThreshold"].isNumeric()) {
    req.faceConfidenceThreshold = json["faceConfidenceThreshold"].asDouble();
  }
  if (json.isMember("licensePlateConfidenceThreshold") &&
      json["licensePlateConfidenceThreshold"].isNumeric()) {
    req.licensePlateConfidenceThreshold =
        json["licensePlateConfidenceThreshold"].asDouble();
  }
  if (json.isMember("confThreshold") && json["confThreshold"].isNumeric()) {
    req.confThreshold = json["confThreshold"].asDouble();
  }

  // DetectorThermal configuration
  if (json.isMember("detectorThermalModelFile") &&
      json["detectorThermalModelFile"].isString()) {
    req.detectorThermalModelFile = json["detectorThermalModelFile"].asString();
  }

  // Performance mode
  if (json.isMember("performanceMode") && json["performanceMode"].isString()) {
    req.performanceMode = json["performanceMode"].asString();
  }

  // SolutionManager settings
  if (json.isMember("recommendedFrameRate") &&
      json["recommendedFrameRate"].isNumeric()) {
    req.recommendedFrameRate = json["recommendedFrameRate"].asInt();
  }

  // FPS configuration (target frame processing rate)
  if (json.isMember("fps") && json["fps"].isNumeric()) {
    req.fps = json["fps"].asInt();
  }

  // Additional parameters (e.g., RTSP_URL)
  // Helper function to trim whitespace (especially important for RTMP URLs)
  auto trim = [](const std::string &str) -> std::string {
    if (str.empty())
      return str;
    size_t first = str.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string::npos)
      return "";
    size_t last = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(first, (last - first + 1));
  };

  // Parse additionalParams - support both new structure (input/output) and old
  // structure (flat)
  if (json.isMember("additionalParams") &&
      json["additionalParams"].isObject()) {
    // Check if using new structure (input/output)
    if (json["additionalParams"].isMember("input") &&
        json["additionalParams"]["input"].isObject()) {
      // New structure: parse input section
      for (const auto &key :
           json["additionalParams"]["input"].getMemberNames()) {
        if (json["additionalParams"]["input"][key].isString()) {
          std::string value = json["additionalParams"]["input"][key].asString();
          // Convert path to production if needed
          if (key == "FILE_PATH" || key == "RTSP_URL" || key == "MODEL_PATH" ||
              key == "SFACE_MODEL_PATH" || key == "WEIGHTS_PATH" ||
              key == "CONFIG_PATH" || key == "LABELS_PATH") {
            value = convertPathToProduction(value);
          }
          req.additionalParams[key] = value;
        }
      }
    }

    if (json["additionalParams"].isMember("output") &&
        json["additionalParams"]["output"].isObject()) {
      // New structure: parse output section
      for (const auto &key :
           json["additionalParams"]["output"].getMemberNames()) {
        if (json["additionalParams"]["output"][key].isString()) {
          std::string value =
              json["additionalParams"]["output"][key].asString();
          // Trim RTMP URLs to prevent GStreamer pipeline errors
          if (key == "RTMP_URL" || key == "RTMP_DES_URL") {
            value = trim(value);
          }
          // Convert path to production if needed
          if (key == "FILE_PATH" || key == "RTSP_URL" || key == "MODEL_PATH" ||
              key == "SFACE_MODEL_PATH" || key == "WEIGHTS_PATH" ||
              key == "CONFIG_PATH" || key == "LABELS_PATH" ||
              key == "RECORD_PATH" || key == "OUTPUT_PATH" ||
              key == "SAVE_PATH" || key == "DEST_PATH") {
            value = convertPathToProduction(value);
          }
          req.additionalParams[key] = value;
        }
      }
    }

    // Backward compatibility: if no input/output sections, parse as flat
    // structure
    // Also parse top-level keys (like CrossingLines) even when input/output
    // sections exist
    if (!json["additionalParams"].isMember("input") &&
        !json["additionalParams"].isMember("output")) {
      for (const auto &key : json["additionalParams"].getMemberNames()) {
        if (json["additionalParams"][key].isString()) {
          std::string value = json["additionalParams"][key].asString();
          // Trim RTMP URLs to prevent GStreamer pipeline errors
          if (key == "RTMP_URL" || key == "RTMP_DES_URL") {
            value = trim(value);
          }
          // Convert path to production if needed
          if (key == "FILE_PATH" || key == "RTSP_URL" || key == "MODEL_PATH" ||
              key == "SFACE_MODEL_PATH" || key == "WEIGHTS_PATH" ||
              key == "CONFIG_PATH" || key == "LABELS_PATH") {
            value = convertPathToProduction(value);
          }
          req.additionalParams[key] = value;
        }
      }
    } else {
      // Parse top-level keys in additionalParams (like CrossingLines) even when
      // input/output exist
      for (const auto &key : json["additionalParams"].getMemberNames()) {
        // Skip input/output sections (already parsed above)
        if (key == "input" || key == "output") {
          continue;
        }
        if (json["additionalParams"][key].isString()) {
          std::string value = json["additionalParams"][key].asString();
          // Trim RTMP URLs to prevent GStreamer pipeline errors
          if (key == "RTMP_URL" || key == "RTMP_DES_URL") {
            value = trim(value);
          }
          // Convert path to production if needed
          if (key == "FILE_PATH" || key == "RTSP_URL" || key == "MODEL_PATH" ||
              key == "SFACE_MODEL_PATH" || key == "WEIGHTS_PATH" ||
              key == "CONFIG_PATH" || key == "LABELS_PATH") {
            value = convertPathToProduction(value);
          }
          req.additionalParams[key] = value;
        }
      }
    }
  }

  // Also check for RTSP_URL at top level
  if (json.isMember("RTSP_URL") && json["RTSP_URL"].isString()) {
    req.additionalParams["RTSP_URL"] = json["RTSP_URL"].asString();
  }

  // Also check for MODEL_NAME at top level
  if (json.isMember("MODEL_NAME") && json["MODEL_NAME"].isString()) {
    req.additionalParams["MODEL_NAME"] = json["MODEL_NAME"].asString();
  }

  // Also check for MODEL_PATH at top level
  if (json.isMember("MODEL_PATH") && json["MODEL_PATH"].isString()) {
    req.additionalParams["MODEL_PATH"] =
        convertPathToProduction(json["MODEL_PATH"].asString());
  }

  // Also check for FILE_PATH at top level (for file source)
  if (json.isMember("FILE_PATH") && json["FILE_PATH"].isString()) {
    req.additionalParams["FILE_PATH"] =
        convertPathToProduction(json["FILE_PATH"].asString());
  }

  // Also check for RTMP_DES_URL or RTMP_URL at top level (for RTMP destination)
  if (json.isMember("RTMP_DES_URL") && json["RTMP_DES_URL"].isString()) {
    req.additionalParams["RTMP_DES_URL"] =
        trim(json["RTMP_DES_URL"].asString());
  } else if (json.isMember("RTMP_URL") && json["RTMP_URL"].isString()) {
    req.additionalParams["RTMP_URL"] = trim(json["RTMP_URL"].asString());
  }

  // Also check for SFACE_MODEL_PATH at top level (for SFace encoder)
  if (json.isMember("SFACE_MODEL_PATH") &&
      json["SFACE_MODEL_PATH"].isString()) {
    req.additionalParams["SFACE_MODEL_PATH"] =
        convertPathToProduction(json["SFACE_MODEL_PATH"].asString());
  }

  // Also check for SFACE_MODEL_NAME at top level (for SFace encoder by name)
  if (json.isMember("SFACE_MODEL_NAME") &&
      json["SFACE_MODEL_NAME"].isString()) {
    req.additionalParams["SFACE_MODEL_NAME"] =
        json["SFACE_MODEL_NAME"].asString();
  }

  // Detection tuning parameters must be specified per-zone (JamZones/StopZones) and not as instance-level additionalParams
  const std::vector<std::string> forbiddenKeys = {
      "check_interval_frames", "check_min_hit_frames", "check_max_distance",
      "check_min_stops", "check_notify_interval", "min_stop_seconds"};
  for (const auto &k : forbiddenKeys) {
    auto it = req.additionalParams.find(k);
    if (it != req.additionalParams.end() && !it->second.empty()) {
      error = "Invalid additionalParam: " + k + " must not be provided at instance level; specify per zone in JamZones or StopZones";
      return false;
    }
  }

  return true;
}

std::string CreateInstanceHandler::convertPathToProduction(
    const std::string &path) const {
  if (path.empty()) {
    return path;
  }

  std::string result = path;

  // Convert absolute development paths to production paths
  // Pattern: /home/cvedix/project/edgeos-api/cvedix_data/... -> /opt/edgeos-api/...
  const std::string devPrefix = "/home/cvedix/project/edgeos-api/cvedix_data/";
  if (result.find(devPrefix) == 0) {
    // Extract path after cvedix_data/
    std::string relativePath = result.substr(devPrefix.length());
    
    // Map test_video/ to videos/
    if (relativePath.find("test_video/") == 0) {
      relativePath = "videos/" + relativePath.substr(11);
    }
    
    result = "/opt/edgeos-api/" + relativePath;
    return result;
  }

  // Also handle other common development paths
  const std::string devPrefix2 = "/home/cvedix/project/edgeos-api/";
  if (result.find(devPrefix2) == 0) {
    std::string relativePath = result.substr(devPrefix2.length());
    
    // Map cvedix_data/test_video/ to videos/
    if (relativePath.find("cvedix_data/test_video/") == 0) {
      relativePath = "videos/" + relativePath.substr(23);
    }
    // Map cvedix_data/models/ to models/
    else if (relativePath.find("cvedix_data/models/") == 0) {
      relativePath = "models/" + relativePath.substr(19);
    }
    // Map cvedix_data/ to root
    else if (relativePath.find("cvedix_data/") == 0) {
      relativePath = relativePath.substr(12);
    }
    
    result = "/opt/edgeos-api/" + relativePath;
    return result;
  }

  // Convert ./cvedix_data/ paths to /opt/edgeos-api/
  if (result.find("./cvedix_data/") == 0) {
    result = result.substr(15); // Remove "./cvedix_data/"
    // Map test_video/ to videos/
    if (result.find("test_video/") == 0) {
      result = "videos/" + result.substr(11);
    }
    result = "/opt/edgeos-api/" + result;
    return result;
  } else if (result.find("cvedix_data/") == 0) {
    result = result.substr(12); // Remove "cvedix_data/"
    // Map test_video/ to videos/
    if (result.find("test_video/") == 0) {
      result = "videos/" + result.substr(11);
    }
    result = "/opt/edgeos-api/" + result;
    return result;
  }

  // Specific mappings
  // Models: ./cvedix_data/models/ -> /opt/edgeos-api/models/
  if (result.find("./models/") == 0) {
    result = "/opt/edgeos-api" + result.substr(1);
    return result;
  }

  // Videos: ./cvedix_data/test_video/ -> /opt/edgeos-api/videos/
  if (result.find("./test_video/") == 0) {
    result = "/opt/edgeos-api/videos/" + result.substr(12);
    return result;
  }

  return result;
}

Json::Value
CreateInstanceHandler::instanceInfoToJson(const InstanceInfo &info) const {
  Json::Value json;
  json["instanceId"] = info.instanceId;
  json["displayName"] = info.displayName;
  json["group"] = info.group;
  json["solutionId"] = info.solutionId;
  json["solutionName"] = info.solutionName;
  json["persistent"] = info.persistent;
  json["loaded"] = info.loaded;
  json["running"] = info.running;
  json["fps"] = info.fps;
  json["version"] = info.version;
  json["frameRateLimit"] = info.frameRateLimit;
  json["metadataMode"] = info.metadataMode;
  json["statisticsMode"] = info.statisticsMode;
  json["diagnosticsMode"] = info.diagnosticsMode;
  json["debugMode"] = info.debugMode;
  json["readOnly"] = info.readOnly;
  json["autoStart"] = info.autoStart;
  json["autoRestart"] = info.autoRestart;
  json["systemInstance"] = info.systemInstance;
  json["inputPixelLimit"] = info.inputPixelLimit;
  json["inputOrientation"] = info.inputOrientation;
  json["detectorMode"] = info.detectorMode;
  json["detectionSensitivity"] = info.detectionSensitivity;
  json["movementSensitivity"] = info.movementSensitivity;
  json["sensorModality"] = info.sensorModality;
  json["originator"]["address"] = info.originator.address;

  // Async pipeline build status
  json["building"] = info.building;
  if (!info.buildError.empty()) {
    json["buildError"] = info.buildError;
  }
  // Add status field for convenience
  if (info.building) {
    json["status"] = "building";
  } else if (!info.buildError.empty()) {
    json["status"] = "error";
  } else {
    json["status"] = "ready";
  }

  // Add streaming URLs if available
  if (!info.rtmpUrl.empty()) {
    json["rtmpUrl"] = info.rtmpUrl;
    
    // Extract RTMP prefix (stream key without _0 suffix)
    // RTMP node automatically adds _0 suffix, so we extract the original stream key
    std::string streamKey = PipelineBuilderDestinationNodes::extractRTMPStreamKey(info.rtmpUrl);
    if (!streamKey.empty()) {
      json["prefix"] = streamKey;
    }
  }
  if (!info.rtspUrl.empty()) {
    json["rtspUrl"] = info.rtspUrl;
  }

  return json;
}

HttpResponsePtr
CreateInstanceHandler::createErrorResponse(int statusCode,
                                           const std::string &error,
                                           const std::string &message) const {

  Json::Value errorJson;
  errorJson["error"] = error;
  if (!message.empty()) {
    errorJson["message"] = message;
  }

  auto resp = HttpResponse::newHttpJsonResponse(errorJson);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));

  // Add CORS headers to error responses
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");

  return resp;
}
