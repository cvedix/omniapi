#include "api/quick_instance_parser.h"
#include "config/system_config.h"
#include "core/logger.h"
#include "core/system_metrics.h"
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

IInstanceManager *QuickInstanceParser::instance_manager_ = nullptr;
SolutionRegistry *QuickInstanceParser::solution_registry_ = nullptr;

void QuickInstanceParser::setInstanceManager(IInstanceManager *manager) {
  instance_manager_ = manager;
}

void QuickInstanceParser::setSolutionRegistry(SolutionRegistry *registry) {
  solution_registry_ = registry;
}

std::string
QuickInstanceParser::mapSolutionTypeToId(const std::string &solutionType,
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
    if (output == "rtmp") {
      if (input == "rtsp" || input == "stream") {
        return "face_detection_rtsp_rtmp_default";
      } else if (input == "rtmp") {
        return "face_detection_rtmp_rtmp_default";
      }
      return "face_detection_rtmp_default";
    }

    if (input == "file" || input == "video") {
      return "face_detection_file_default";
    } else if (input == "rtsp" || input == "stream") {
      return "face_detection_rtsp_default";
    } else if (input == "rtmp") {
      return "face_detection_rtmp_rtmp_default";
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
  } else if (type == "ba_stop" || type == "stop") {
    if (output == "mqtt") {
      return "ba_stop_mqtt_default";
    }
    return "ba_stop";
  } else if (type == "ba_jam" || type == "jam" || type == "traffic_jam") {
    if (output == "mqtt") {
      return "ba_jam_mqtt_default";
    }
    return "ba_jam";
  } else if (type == "ba_loitering" || type == "loitering") {
    return "ba_loitering";
  } else if (type == "ba_area_enter_exit" || type == "intrusion" ||
             type == "area_enter_exit") {
    return "ba_area_enter_exit";
  } else if (type == "ba_line_counting" || type == "line_counting" ||
             type == "counting") {
    return "ba_line_counting";
  } else if (type == "ba_crowding" || type == "crowding") {
    return "ba_crowding";
  } else if (type == "fire_smoke" || type == "fire_smoke_detection" ||
             type == "fire" || type == "firefighting") {
    return "fire_smoke_detection";
  } else if (type == "wrong_way" || type == "wrong_way_detection") {
    return "wrong_way_detection";
  } else if (type == "obstacle" || type == "obstacle_detection") {
    return "obstacle_detection";
  }

  // If not found, return empty string
  return "";
}

std::string
QuickInstanceParser::convertPathToProduction(const std::string &path) const {
  if (path.empty()) {
    return path;
  }

  std::string result = path;

  // Convert absolute development paths to production paths
  // Pattern: /home/cvedix/project/omniapi/cvedix_data/... -> /opt/omniapi/...
  const std::string devPrefix = "/home/cvedix/project/omniapi/cvedix_data/";
  if (result.find(devPrefix) == 0) {
    // Extract path after cvedix_data/
    std::string relativePath = result.substr(devPrefix.length());
    
    // Map test_video/ to videos/
    if (relativePath.find("test_video/") == 0) {
      relativePath = "videos/" + relativePath.substr(11);
    }
    
    result = "/opt/omniapi/" + relativePath;
    return result;
  }

  // Also handle other common development paths
  const std::string devPrefix2 = "/home/cvedix/project/omniapi/";
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
    
    result = "/opt/omniapi/" + relativePath;
    return result;
  }

  // Convert ./cvedix_data/ paths to /opt/omniapi/
  if (result.find("./cvedix_data/") == 0) {
    result = result.substr(15); // Remove "./cvedix_data/"
    // Map test_video/ to videos/
    if (result.find("test_video/") == 0) {
      result = "videos/" + result.substr(11);
    }
    result = "/opt/omniapi/" + result;
    return result;
  } else if (result.find("cvedix_data/") == 0) {
    result = result.substr(12); // Remove "cvedix_data/"
    // Map test_video/ to videos/
    if (result.find("test_video/") == 0) {
      result = "videos/" + result.substr(11);
    }
    result = "/opt/omniapi/" + result;
    return result;
  }

  // Specific mappings
  // Models: ./cvedix_data/models/ -> /opt/omniapi/models/
  if (result.find("./models/") == 0) {
    result = "/opt/omniapi" + result.substr(1);
    return result;
  }

  // Videos: ./cvedix_data/test_video/ -> /opt/omniapi/videos/
  if (result.find("./test_video/") == 0) {
    result = "/opt/omniapi/videos/" + result.substr(12);
    return result;
  }

  return result;
}

std::map<std::string, std::string>
QuickInstanceParser::getDefaultParams(const std::string &solutionType,
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
      defaults["FILE_PATH"] = "/opt/omniapi/videos/face.mp4";
      defaults["MODEL_PATH"] =
          "/opt/omniapi/models/face/face_detection_yunet_2023mar.onnx";
    } else if (input == "rtsp" || input == "stream") {
      defaults["RTSP_URL"] = "rtsp://localhost:8554/stream";
      defaults["MODEL_PATH"] =
          "/opt/omniapi/models/face/face_detection_yunet_2023mar.onnx";
    } else if (input == "rtmp") {
      defaults["RTMP_SRC_URL"] = "rtmp://localhost:1935/live/stream";
      defaults["MODEL_PATH"] =
          "/opt/omniapi/models/face/face_detection_yunet_2023mar.onnx";
    }
    defaults["RESIZE_RATIO"] = "1.0";
  }
  // BA Crossline defaults (Production paths)
  // Updated to match actual file locations in /opt/omniapi/models/det_cls/
  else if (type == "ba_crossline" || type == "crossline" ||
           type == "behavior_analysis") {
    // Only add FILE_PATH if input type is file/video
    // Only add RTSP_URL if input type is rtsp/stream
    if (input == "file" || input == "video") {
      defaults["FILE_PATH"] = "/opt/omniapi/videos/face.mp4";
    } else if (input == "rtsp" || input == "stream") {
      defaults["RTSP_URL"] = "rtsp://localhost:8554/stream";
    }
    // Model paths are always needed regardless of input type
    defaults["WEIGHTS_PATH"] =
        "/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721_best.weights";
    defaults["CONFIG_PATH"] =
        "/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721.cfg";
    defaults["LABELS_PATH"] =
        "/opt/omniapi/models/det_cls/yolov3_tiny_5classes.txt";
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
  // Updated to match actual file locations in /opt/omniapi/models/det_cls/
  else if (type == "object_detection" || type == "yolo") {
    defaults["FILE_PATH"] = "/opt/omniapi/videos/face.mp4";
    defaults["WEIGHTS_PATH"] =
        "/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721_best.weights";
    defaults["CONFIG_PATH"] =
        "/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721.cfg";
    defaults["LABELS_PATH"] =
        "/opt/omniapi/models/det_cls/yolov3_tiny_5classes.txt";
    defaults["RESIZE_RATIO"] = "1.0";
  }
  // MaskRCNN defaults (Production paths)
  // Updated to match actual config file name
  else if (type == "mask_rcnn" || type == "segmentation") {
    defaults["FILE_PATH"] = "/opt/omniapi/videos/face.mp4";
    defaults["MODEL_PATH"] =
        "/opt/omniapi/models/mask_rcnn/frozen_inference_graph.pb";
    defaults["MODEL_CONFIG_PATH"] =
        "/opt/omniapi/models/mask_rcnn/"
        "mask_rcnn_inception_v2_coco_2018_01_28.pbtxt";
    defaults["LABELS_PATH"] = "/opt/omniapi/models/coco_80classes.txt";
    if (output == "rtmp") {
      defaults["RTMP_URL"] = "rtmp://localhost:1935/live/stream";
    }
  }

  return defaults;
}

bool QuickInstanceParser::parseQuickRequest(const Json::Value &json,
                                             CreateInstanceRequest &req,
                                             std::string &error) {

  // Required field: name
  if (!json.isMember("name") || !json["name"].isString()) {
    error = "Missing required field: name";
    return false;
  }
  req.name = json["name"].asString();

  // ── Tiered Category API ─────────────────────────────────────────────
  // Priority: category field > solutionType/solution field
  // If "category" is provided, resolve solution via SolutionRegistry
  std::string solutionId;
  std::string solutionType;

  // Optional: input and output types (needed for both paths)
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

  // If input type was not explicitly provided, infer it from FILE_PATH when possible.
  auto inferInputTypeFromPath = [](const std::string &path) -> std::string {
    if (path.empty()) {
      return "file";
    }
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.rfind("rtsp://", 0) == 0) {
      return "rtsp";
    }
    if (lower.rfind("rtmp://", 0) == 0) {
      return "rtmp";
    }
    if (lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0 ||
        lower.find(".m3u8") != std::string::npos) {
      return "hls";
    }
    return "file";
  };

  if (inputType == "file") {
    const auto inferFromValue = [&](const std::string &value) {
      std::string inferred = inferInputTypeFromPath(value);
      if (inferred != "file") {
        inputType = inferred;
      }
    };

    // Check simplified "url" field first (new format)
    if (json.isMember("input") && json["input"].isObject() &&
        json["input"].isMember("url") && json["input"]["url"].isString()) {
      inferFromValue(json["input"]["url"].asString());
    }
    // Then check legacy internal param names
    else if (json.isMember("input") && json["input"].isObject()) {
      if (json["input"].isMember("FILE_PATH") && json["input"]["FILE_PATH"].isString()) {
        inferFromValue(json["input"]["FILE_PATH"].asString());
      } else if (json["input"].isMember("RTSP_SRC_URL") && json["input"]["RTSP_SRC_URL"].isString()) {
        inferFromValue(json["input"]["RTSP_SRC_URL"].asString());
      } else if (json["input"].isMember("RTMP_SRC_URL") && json["input"]["RTMP_SRC_URL"].isString()) {
        inferFromValue(json["input"]["RTMP_SRC_URL"].asString());
      }
    } else if (json.isMember("additionalParams") && json["additionalParams"].isObject()) {
      if (json["additionalParams"].isMember("input") &&
          json["additionalParams"]["input"].isObject() &&
          json["additionalParams"]["input"].isMember("FILE_PATH") &&
          json["additionalParams"]["input"]["FILE_PATH"].isString()) {
        inferFromValue(json["additionalParams"]["input"]["FILE_PATH"].asString());
      } else if (json["additionalParams"].isMember("FILE_PATH") &&
                 json["additionalParams"]["FILE_PATH"].isString()) {
        inferFromValue(json["additionalParams"]["FILE_PATH"].asString());
      }
    } else if (json.isMember("FILE_PATH") && json["FILE_PATH"].isString()) {
      inferFromValue(json["FILE_PATH"].asString());
    }
  }

  // ── Resolution: category path or legacy solutionType path ──────────
  if (json.isMember("category") && json["category"].isString()) {
    std::string category = json["category"].asString();
    std::string feature;
    if (json.isMember("feature") && json["feature"].isString()) {
      feature = json["feature"].asString();
    }

    if (category == "custom" || category == "Custom") {
      // Custom category: require explicit solution + model paths
      if (json.isMember("solution") && json["solution"].isString()) {
        solutionType = json["solution"].asString();
      } else if (json.isMember("solutionType") && json["solutionType"].isString()) {
        solutionType = json["solutionType"].asString();
      } else {
        error = "Category 'custom' requires explicit 'solution' or 'solutionType' field";
        return false;
      }
      // Map the solution type to ID for custom category
      solutionId = mapSolutionTypeToId(solutionType, inputType, outputType);
      if (solutionId.empty()) {
        // Try using solutionType as-is (direct solution ID)
        if (solution_registry_ && solution_registry_->hasSolution(solutionType)) {
          solutionId = solutionType;
        } else {
          error = "Unsupported solution type for custom category: " + solutionType;
          return false;
        }
      }
    } else {
      // Auto-resolve solution from category + feature via SolutionRegistry
      if (solution_registry_) {
        solutionId = solution_registry_->resolveSolutionByCategory(category, feature);
      }
      if (solutionId.empty()) {
        error = "Unknown category/feature combination: " + category +
                (feature.empty() ? "" : "/" + feature);
        return false;
      }
      // Set solutionType from category for default params lookup
      solutionType = solutionId;
    }
  } else {
    // Legacy path: solutionType or solution field
    if (json.isMember("solutionType") && json["solutionType"].isString()) {
      solutionType = json["solutionType"].asString();
    } else if (json.isMember("solution") && json["solution"].isString()) {
      solutionType = json["solution"].asString();
    } else {
      error = "Missing required field: category, solutionType, or solution";
      return false;
    }

    // Map solution type to solution ID
    solutionId = mapSolutionTypeToId(solutionType, inputType, outputType);
    if (solutionId.empty()) {
      // Final fallback: try using solutionType as-is (direct solution ID)
      if (solution_registry_ && solution_registry_->hasSolution(solutionType)) {
        solutionId = solutionType;
      } else {
        error = "Unsupported solution type: " + solutionType;
        return false;
      }
    }
  }
  req.solution = solutionId;

  // Face detection sample/pipeline uses YuNet detector only (no SFace encoder).
  // Ignore SFACE_MODEL_PATH for face_detection* solutions to keep API payload aligned.
  const bool isFaceDetectionSolution =
      solutionId.rfind("face_detection", 0) == 0;

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
    // ── Simplified "url"/"type" format mapping ───────────────────────
    // Maps user-friendly { "url": "...", "type": "rtsp" } to internal
    // parameter names like RTSP_SRC_URL, RTMP_SRC_URL, FILE_PATH
    if (inputObj.isMember("url") && inputObj["url"].isString()) {
      std::string url = inputObj["url"].asString();
      url = convertPathToProduction(url);

      // Determine internal param name based on inputType
      std::string lowerInput = inputType;
      std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(), ::tolower);

      if (lowerInput == "rtsp" || lowerInput == "stream") {
        req.additionalParams["RTSP_SRC_URL"] = url;
        req.additionalParams["RTSP_URL"] = url;  // backward compat alias
      } else if (lowerInput == "rtmp") {
        req.additionalParams["RTMP_SRC_URL"] = url;
      } else if (lowerInput == "hls") {
        req.additionalParams["FILE_PATH"] = url;  // HLS uses file_src node
      } else {
        // Default: file input
        req.additionalParams["FILE_PATH"] = url;
      }
    }

    // Also copy all other keys from input object (legacy + extra params)
    for (const auto &key : inputObj.getMemberNames()) {
      // Skip "url" and "type" — they are already handled above
      if (key == "url" || key == "type") {
        continue;
      }
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
    // ── Simplified "url"/"type" format for output ─────────────────────
    if (outputObj.isMember("url") && outputObj["url"].isString()) {
      std::string url = outputObj["url"].asString();
      url = convertPathToProduction(url);

      std::string lowerOutput = outputType;
      std::transform(lowerOutput.begin(), lowerOutput.end(), lowerOutput.begin(), ::tolower);

      if (lowerOutput == "rtmp") {
        req.additionalParams["RTMP_URL"] = url;
        req.additionalParams["RTMP_DES_URL"] = url;
      } else {
        // File output
        req.additionalParams["SAVE_DIR"] = url;
      }
    }

    // Also copy all other keys from output object
    for (const auto &key : outputObj.getMemberNames()) {
      if (key == "url" || key == "type") {
        continue;
      }
      if (outputObj[key].isString()) {
        std::string value = outputObj[key].asString();
        // Convert path to production if needed
        if (key == "RTMP_URL" || key == "FILE_PATH") {
          value = convertPathToProduction(value);
        }
        req.additionalParams[key] = value;
        // Also handle RTMP_DES_URL alias
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

  if (isFaceDetectionSolution) {
    req.additionalParams.erase("SFACE_MODEL_PATH");
    req.additionalParams.erase("SFACE_MODEL_NAME");
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
