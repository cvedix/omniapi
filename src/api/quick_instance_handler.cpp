#include "api/quick_instance_handler.h"
#include "config/system_config.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/metrics_interceptor.h"
#include "instances/instance_info.h"
#include "instances/instance_manager.h"
#include "models/create_instance_request.h"
#include "solutions/solution_registry.h"
#include <algorithm>
#include <chrono>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <sstream>

IInstanceManager *QuickInstanceHandler::instance_manager_ = nullptr;
SolutionRegistry *QuickInstanceHandler::solution_registry_ = nullptr;

void QuickInstanceHandler::setInstanceManager(IInstanceManager *manager) {
  instance_manager_ = manager;
}

void QuickInstanceHandler::setSolutionRegistry(SolutionRegistry *registry) {
  solution_registry_ = registry;
}

void QuickInstanceHandler::createQuickInstance(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/instance/quick - Create quick instance";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if manager is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/quick - Error: Instance "
                      "manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/quick - Error: Invalid "
                        "JSON body";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Parse request
    CreateInstanceRequest createReq;
    std::string parseError;
    if (!parseQuickRequest(*json, createReq, parseError)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/quick - Parse error: "
                     << parseError;
      }
      callback(createErrorResponse(400, "Invalid request", parseError));
      return;
    }

    // Validate request
    if (!createReq.validate()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/quick - Validation "
                        "failed: "
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
          PLOG_ERROR << "[API] POST /v1/core/instance/quick - Error: Solution "
                        "registry not initialized";
        }
        callback(createErrorResponse(500, "Internal server error",
                                     "Solution registry not initialized"));
        return;
      }

      if (!solution_registry_->hasSolution(createReq.solution)) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] POST /v1/core/instance/quick - Solution not "
                          "found: "
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
          PLOG_WARNING << "[API] POST /v1/core/instance/quick - Instance limit "
                          "reached: "
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
        PLOG_ERROR << "[API] POST /v1/core/instance/quick - Invalid argument: "
                   << e.what() << " - " << duration.count() << "ms";
      }
      callback(createErrorResponse(400, "Invalid request", e.what()));
      return;
    } catch (const std::runtime_error &e) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/quick - Runtime error: "
                   << e.what() << " - " << duration.count() << "ms";
      }
      callback(createErrorResponse(500, "Failed to create instance", e.what()));
      return;
    } catch (const std::exception &e) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/quick - Exception: "
                   << e.what() << " - " << duration.count() << "ms";
      }
      callback(createErrorResponse(500, "Failed to create instance",
                                   std::string("Error: ") + e.what()));
      return;
    } catch (...) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/quick - Unknown error - "
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
        PLOG_ERROR << "[API] POST /v1/core/instance/quick - Failed to create "
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
        PLOG_WARNING << "[API] POST /v1/core/instance/quick - Created but "
                        "could not retrieve info - "
                     << duration.count() << "ms";
      }
      callback(
          createErrorResponse(500, "Internal server error",
                              "Instance created but could not retrieve info"));
      return;
    }

    // Build response
    Json::Value response = instanceInfoToJson(optInfo.value());

    if (isApiLoggingEnabled()) {
      const auto &info = optInfo.value();
      PLOG_INFO << "[API] POST /v1/core/instance/quick - Success: Created "
                   "instance "
                << instanceId << " (" << info.displayName
                << ", solution: " << info.solutionId << ") - "
                << duration.count() << "ms";
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
      PLOG_ERROR << "[API] POST /v1/core/instance/quick - Exception: "
                 << e.what() << " - " << duration.count() << "ms";
    }
    std::cerr << "[QuickInstanceHandler] Exception: " << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/quick - Unknown exception - "
                 << duration.count() << "ms";
    }
    std::cerr << "[QuickInstanceHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void QuickInstanceHandler::handleOptions(
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

std::string
QuickInstanceHandler::mapSolutionTypeToId(const std::string &solutionType,
                                          const std::string &inputType,
                                          const std::string &outputType) const {
  // Convert to lowercase for comparison
  std::string type = solutionType;
  std::transform(type.begin(), type.end(), type.begin(), ::tolower);

  std::string input = inputType;
  std::transform(input.begin(), input.end(), input.begin(), ::tolower);

  std::string output = outputType;
  std::transform(output.begin(), output.end(), output.begin(), ::tolower);

  // Map solution types to solution IDs
  if (type == "face_detection" || type == "face") {
    if (input == "file" || input == "video") {
      return "face_detection_file_default";
    } else if (input == "rtsp" || input == "stream") {
      return "face_detection_rtsp_default";
    } else if (input == "rtmp") {
      return "face_detection_rtmp_default";
    } else {
      // Default to file input
      return "face_detection_file_default";
    }
  } else if (type == "ba_crossline" || type == "crossline" ||
             type == "behavior_analysis") {
    if (output == "mqtt") {
      return "ba_crossline_mqtt_default";
    } else {
      return "ba_crossline_default";
    }
  } else if (type == "object_detection" || type == "yolo") {
    return "object_detection_yolo_default";
  } else if (type == "mask_rcnn" || type == "segmentation") {
    if (output == "rtmp") {
      return "mask_rcnn_rtmp_default";
    } else {
      return "mask_rcnn_detection_default";
    }
  } else if (type == "securt") {
    return "securt";
  }

  // If not found, return empty string
  return "";
}

std::string
QuickInstanceHandler::convertPathToProduction(const std::string &path) const {
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

std::map<std::string, std::string>
QuickInstanceHandler::getDefaultParams(const std::string &solutionType,
                                       const std::string &inputType,
                                       const std::string &outputType) const {

  std::map<std::string, std::string> defaults;

  // Convert to lowercase for comparison
  std::string type = solutionType;
  std::transform(type.begin(), type.end(), type.begin(), ::tolower);

  std::string input = inputType;
  std::transform(input.begin(), input.end(), input.begin(), ::tolower);

  std::string output = outputType;
  std::transform(output.begin(), output.end(), output.begin(), ::tolower);

  // Face detection defaults (Production paths)
  if (type == "face_detection" || type == "face") {
    if (input == "file" || input == "video") {
      defaults["FILE_PATH"] = "/opt/edgeos-api/videos/face.mp4";
      defaults["MODEL_PATH"] =
          "/opt/edgeos-api/models/face/face_detection_yunet_2022mar.onnx";
    } else if (input == "rtsp" || input == "stream") {
      defaults["RTSP_URL"] = "rtsp://localhost:8554/stream";
      defaults["MODEL_PATH"] =
          "/opt/edgeos-api/models/face/face_detection_yunet_2022mar.onnx";
    }
    defaults["RESIZE_RATIO"] = "1.0";
  }
  // BA Crossline defaults (Production paths)
  // Updated to match actual file locations in /opt/edgeos-api/models/det_cls/
  else if (type == "ba_crossline" || type == "crossline" ||
           type == "behavior_analysis") {
    // Only add FILE_PATH if input type is file/video
    // Only add RTSP_URL if input type is rtsp/stream
    if (input == "file" || input == "video") {
      defaults["FILE_PATH"] = "/opt/edgeos-api/videos/face.mp4";
    } else if (input == "rtsp" || input == "stream") {
      defaults["RTSP_URL"] = "rtsp://localhost:8554/stream";
    }
    // Model paths are always needed regardless of input type
    defaults["WEIGHTS_PATH"] =
        "/opt/edgeos-api/models/det_cls/yolov3-tiny-2022-0721_best.weights";
    defaults["CONFIG_PATH"] =
        "/opt/edgeos-api/models/det_cls/yolov3-tiny-2022-0721.cfg";
    defaults["LABELS_PATH"] =
        "/opt/edgeos-api/models/det_cls/yolov3_tiny_5classes.txt";
    defaults["RESIZE_RATIO"] = "1.0";
    // NOTE: CROSSLINE_START_X, CROSSLINE_START_Y, CROSSLINE_END_X,
    // CROSSLINE_END_Y are NOT set as defaults here. If user doesn't provide
    // them, pipeline builder will use its own default line configuration
    // (Priority 3 fallback). This allows users to create instances without
    // crossline config if they want.
    defaults["ENABLE_SCREEN_DES"] = "false";
    if (output == "rtmp") {
      defaults["RTMP_URL"] = "rtmp://localhost:1935/live/stream";
    }
  }
  // Object detection defaults (Production paths)
  // Updated to match actual file locations in /opt/edgeos-api/models/det_cls/
  else if (type == "object_detection" || type == "yolo") {
    defaults["FILE_PATH"] = "/opt/edgeos-api/videos/face.mp4";
    defaults["WEIGHTS_PATH"] =
        "/opt/edgeos-api/models/det_cls/yolov3-tiny-2022-0721_best.weights";
    defaults["CONFIG_PATH"] =
        "/opt/edgeos-api/models/det_cls/yolov3-tiny-2022-0721.cfg";
    defaults["LABELS_PATH"] =
        "/opt/edgeos-api/models/det_cls/yolov3_tiny_5classes.txt";
    defaults["RESIZE_RATIO"] = "1.0";
  }
  // MaskRCNN defaults (Production paths)
  // Updated to match actual config file name
  else if (type == "mask_rcnn" || type == "segmentation") {
    defaults["FILE_PATH"] = "/opt/edgeos-api/videos/face.mp4";
    defaults["MODEL_PATH"] =
        "/opt/edgeos-api/models/mask_rcnn/frozen_inference_graph.pb";
    defaults["MODEL_CONFIG_PATH"] =
        "/opt/edgeos-api/models/mask_rcnn/"
        "mask_rcnn_inception_v2_coco_2018_01_28.pbtxt";
    defaults["LABELS_PATH"] = "/opt/edgeos-api/models/coco_80classes.txt";
    if (output == "rtmp") {
      defaults["RTMP_URL"] = "rtmp://localhost:1935/live/stream";
    }
  }

  return defaults;
}

bool QuickInstanceHandler::parseQuickRequest(const Json::Value &json,
                                             CreateInstanceRequest &req,
                                             std::string &error) {

  // Required field: name
  if (!json.isMember("name") || !json["name"].isString()) {
    error = "Missing required field: name";
    return false;
  }
  req.name = json["name"].asString();

  // Required field: solutionType or solution
  std::string solutionType;
  if (json.isMember("solutionType") && json["solutionType"].isString()) {
    solutionType = json["solutionType"].asString();
  } else if (json.isMember("solution") && json["solution"].isString()) {
    solutionType = json["solution"].asString();
  } else {
    error = "Missing required field: solutionType or solution";
    return false;
  }

  // Optional: input and output types
  std::string inputType = "file";
  std::string outputType = "file";

  if (json.isMember("input") && json["input"].isObject()) {
    if (json["input"].isMember("type") && json["input"]["type"].isString()) {
      inputType = json["input"]["type"].asString();
    }
  } else if (json.isMember("inputType") && json["inputType"].isString()) {
    inputType = json["inputType"].asString();
  }

  if (json.isMember("output") && json["output"].isObject()) {
    if (json["output"].isMember("type") && json["output"]["type"].isString()) {
      outputType = json["output"]["type"].asString();
    }
  } else if (json.isMember("outputType") && json["outputType"].isString()) {
    outputType = json["outputType"].asString();
  }

  // Map solution type to solution ID
  std::string solutionId =
      mapSolutionTypeToId(solutionType, inputType, outputType);
  if (solutionId.empty()) {
    error = "Unsupported solution type: " + solutionType;
    return false;
  }
  req.solution = solutionId;

  // Optional fields
  if (json.isMember("group") && json["group"].isString()) {
    req.group = json["group"].asString();
  }

  if (json.isMember("persistent") && json["persistent"].isBool()) {
    req.persistent = json["persistent"].asBool();
  }

  if (json.isMember("autoStart") && json["autoStart"].isBool()) {
    req.autoStart = json["autoStart"].asBool();
  }

  // FPS configuration (target frame processing rate)
  if (json.isMember("fps") && json["fps"].isNumeric()) {
    req.fps = json["fps"].asInt();
  }

  // Get default parameters
  std::map<std::string, std::string> defaultParams =
      getDefaultParams(solutionType, inputType, outputType);

  // Parse input parameters (support both top-level and additionalParams.input)
  Json::Value inputObj;
  if (json.isMember("input") && json["input"].isObject()) {
    inputObj = json["input"];
  } else if (json.isMember("additionalParams") && 
             json["additionalParams"].isObject() &&
             json["additionalParams"].isMember("input") &&
             json["additionalParams"]["input"].isObject()) {
    inputObj = json["additionalParams"]["input"];
  }
  
  if (!inputObj.isNull()) {
    for (const auto &key : inputObj.getMemberNames()) {
      if (inputObj[key].isString()) {
        std::string value = inputObj[key].asString();
        // Convert path to production if needed
        if (key == "FILE_PATH" || key == "RTSP_URL" || key == "RTSP_SRC_URL" || 
            key == "RTMP_SRC_URL" || key == "MODEL_PATH" ||
            key == "WEIGHTS_PATH" || key == "CONFIG_PATH" ||
            key == "LABELS_PATH") {
          value = convertPathToProduction(value);
        }
        req.additionalParams[key] = value;
      }
    }
  }

  // Parse output parameters (support both top-level and additionalParams.output)
  Json::Value outputObj;
  if (json.isMember("output") && json["output"].isObject()) {
    outputObj = json["output"];
  } else if (json.isMember("additionalParams") && 
             json["additionalParams"].isObject() &&
             json["additionalParams"].isMember("output") &&
             json["additionalParams"]["output"].isObject()) {
    outputObj = json["additionalParams"]["output"];
  }
  
  if (!outputObj.isNull()) {
    for (const auto &key : outputObj.getMemberNames()) {
      if (outputObj[key].isString()) {
        std::string value = outputObj[key].asString();
        // Convert path to production if needed
        if (key == "RTMP_URL" || key == "FILE_PATH") {
          value = convertPathToProduction(value);
        }
        req.additionalParams[key] = value;
        // Also handle RTMP_DES_URL
        if (key == "RTMP_URL") {
          req.additionalParams["RTMP_DES_URL"] = value;
        }
      }
    }
  }

  // Apply defaults for parameters not provided by user
  for (const auto &[key, value] : defaultParams) {
    if (req.additionalParams.find(key) == req.additionalParams.end()) {
      req.additionalParams[key] = value;
    }
  }

  // Parse lines parameter for BA Crossline (UI-friendly format)
  if (json.isMember("lines") && json["lines"].isArray()) {
    Json::Value linesArray = json["lines"];
    Json::Value crossingLinesArray(Json::arrayValue);

    for (const auto &line : linesArray) {
      if (!line.isObject()) {
        continue;
      }

      Json::Value lineObj;

      // Optional: id (will be auto-generated if not provided)
      if (line.isMember("id") && line["id"].isString()) {
        lineObj["id"] = line["id"].asString();
      }

      // Optional: name
      if (line.isMember("name") && line["name"].isString()) {
        lineObj["name"] = line["name"].asString();
      } else {
        lineObj["name"] =
            "Line " + std::to_string(crossingLinesArray.size() + 1);
      }

      // Required: coordinates (array of points)
      if (line.isMember("coordinates") && line["coordinates"].isArray()) {
        lineObj["coordinates"] = line["coordinates"];
      } else if (line.isMember("start") && line.isMember("end")) {
        // Support alternative format: start/end points
        Json::Value startPoint(Json::objectValue);
        startPoint["x"] = line["start"]["x"];
        startPoint["y"] = line["start"]["y"];
        Json::Value endPoint(Json::objectValue);
        endPoint["x"] = line["end"]["x"];
        endPoint["y"] = line["end"]["y"];
        Json::Value coords(Json::arrayValue);
        coords.append(startPoint);
        coords.append(endPoint);
        lineObj["coordinates"] = coords;
      } else {
        error = "Line must have 'coordinates' array or 'start'/'end' points";
        return false;
      }

      // Optional: direction (default: "Both")
      if (line.isMember("direction") && line["direction"].isString()) {
        std::string dir = line["direction"].asString();
        if (dir == "Up" || dir == "Down" || dir == "Both") {
          lineObj["direction"] = dir;
        } else {
          lineObj["direction"] = "Both";
        }
      } else {
        lineObj["direction"] = "Both";
      }

      // Required: classes (array of class names)
      if (line.isMember("classes") && line["classes"].isArray()) {
        lineObj["classes"] = line["classes"];
      } else {
        error =
            "Line must have 'classes' array (e.g., [\"Vehicle\", \"Person\"])";
        return false;
      }

      // Optional: color (default: [255, 0, 0, 255] - red)
      if (line.isMember("color") && line["color"].isArray()) {
        lineObj["color"] = line["color"];
      } else {
        Json::Value defaultColor(Json::arrayValue);
        defaultColor.append(255);
        defaultColor.append(0);
        defaultColor.append(0);
        defaultColor.append(255);
        lineObj["color"] = defaultColor;
      }

      crossingLinesArray.append(lineObj);
    }

    // Convert to JSON string format required by BA Crossline
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string crossingLinesStr =
        Json::writeString(builder, crossingLinesArray);
    req.additionalParams["CrossingLines"] = crossingLinesStr;
  }

  // Parse additional parameters (flat structure for backward compatibility)
  // Also parse top-level keys in additionalParams (like CrossingLines) even
  // when input/output sections exist
  if (json.isMember("additionalParams") &&
      json["additionalParams"].isObject()) {
    for (const auto &key : json["additionalParams"].getMemberNames()) {
      // Skip input/output sections (already parsed above)
      if (key == "input" || key == "output") {
        continue;
      }
      // Parse string values (including CrossingLines JSON string)
      if (json["additionalParams"][key].isString()) {
        std::string value = json["additionalParams"][key].asString();
        // Convert path to production if needed
        if (key.find("PATH") != std::string::npos ||
            key.find("FILE") != std::string::npos) {
          value = convertPathToProduction(value);
        }
        req.additionalParams[key] = value;
      }
    }
  }

  // Parse other optional fields
  if (json.isMember("frameRateLimit") && json["frameRateLimit"].isNumeric()) {
    req.frameRateLimit = json["frameRateLimit"].asInt();
  }

  if (json.isMember("detectionSensitivity") &&
      json["detectionSensitivity"].isString()) {
    req.detectionSensitivity = json["detectionSensitivity"].asString();
  } else if (json.isMember("detectionSensitivity") &&
             json["detectionSensitivity"].isNumeric()) {
    // Convert numeric to string
    double val = json["detectionSensitivity"].asDouble();
    req.detectionSensitivity = std::to_string(val);
  }

  return true;
}

Json::Value
QuickInstanceHandler::instanceInfoToJson(const InstanceInfo &info) const {
  Json::Value json;
  auto ensureStreamKeySuffixZero = [](const std::string &url) -> std::string {
    if (url.empty()) {
      return url;
    }
    size_t lastSlash = url.find_last_of('/');
    if (lastSlash == std::string::npos || lastSlash >= url.length() - 1) {
      return url;
    }
    std::string streamKey = url.substr(lastSlash + 1);
    if (streamKey.length() >= 2 && streamKey.substr(streamKey.length() - 2) == "_0") {
      return url;
    }
    return url + "_0";
  };
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

  // Add streaming URLs if available
  if (!info.rtmpUrl.empty()) {
    std::string normalizedRtmpUrl = ensureStreamKeySuffixZero(info.rtmpUrl);
    json["rtmpUrl"] = normalizedRtmpUrl;

    // Keep the actual stream key from URL path (including suffix such as "_0")
    // to match the real publish path used by RTMP output.
    std::string streamKey;
    size_t lastSlash = normalizedRtmpUrl.find_last_of('/');
    if (lastSlash != std::string::npos && lastSlash < normalizedRtmpUrl.length() - 1) {
      streamKey = normalizedRtmpUrl.substr(lastSlash + 1);
    }
    if (!streamKey.empty()) {
      json["prefix"] = streamKey;
    }
  }
  if (!info.rtspUrl.empty()) {
    json["rtspUrl"] = ensureStreamKeySuffixZero(info.rtspUrl);
  }

  return json;
}

HttpResponsePtr
QuickInstanceHandler::createErrorResponse(int statusCode,
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
