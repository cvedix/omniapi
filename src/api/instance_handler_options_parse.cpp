#include "api/instance_handler.h"
#include "core/env_config.h"
#include "core/event_queue.h"
#include "core/frame_input_queue.h"
#include "core/codec_manager.h"
#include "core/frame_processor.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/metrics_interceptor.h"
#include "core/timeout_constants.h"
#include "instances/instance_info.h"
#include "instances/instance_manager.h"
#include "instances/subprocess_instance_manager.h"
#include "models/update_instance_request.h"
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <json/reader.h>
#include <json/writer.h>
#include <optional>
#include <sstream>
#include <thread>
#include <typeinfo>
#include <unordered_map>
#include <vector>
namespace fs = std::filesystem;

namespace {
std::string base64Encode(const unsigned char *data, size_t length) {
  static const char base64_chars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;
  encoded.reserve(((length + 2) / 3) * 4);
  size_t i = 0;
  while (i < length) {
    unsigned char byte1 = data[i++];
    unsigned char byte2 = (i < length) ? data[i++] : 0;
    unsigned char byte3 = (i < length) ? data[i++] : 0;
    unsigned int combined = (byte1 << 16) | (byte2 << 8) | byte3;
    encoded += base64_chars[(combined >> 18) & 0x3F];
    encoded += base64_chars[(combined >> 12) & 0x3F];
    encoded += (i - 2 < length) ? base64_chars[(combined >> 6) & 0x3F] : '=';
    encoded += (i - 1 < length) ? base64_chars[combined & 0x3F] : '=';
  }
  return encoded;
}
} // namespace

void InstanceHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    // Set handler start time for accurate metrics
    if (req) {
      MetricsInterceptor::setHandlerStartTime(req);
    }

    auto resp = HttpResponse::newHttpResponse();
    if (!resp) {
      std::cerr << "[InstanceHandler] Failed to create response in handleOptions" << std::endl;
      return;
    }

    resp->setStatusCode(k200OK);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods",
                    "GET, POST, PUT, DELETE, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers",
                    "Content-Type, Authorization");
    resp->addHeader("Access-Control-Max-Age", "3600");

    // Record metrics and call callback
    if (req) {
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
    } else {
      callback(resp);
    }
  } catch (const std::exception &e) {
    std::cerr << "[InstanceHandler] Exception in handleOptions: " << e.what() << std::endl;
    try {
      auto errorResp = HttpResponse::newHttpResponse();
      if (errorResp) {
        errorResp->setStatusCode(k500InternalServerError);
        callback(errorResp);
      }
    } catch (...) {
      std::cerr << "[InstanceHandler] Failed to create error response in handleOptions" << std::endl;
    }
  } catch (...) {
    std::cerr << "[InstanceHandler] Unknown exception in handleOptions" << std::endl;
    try {
      auto errorResp = HttpResponse::newHttpResponse();
      if (errorResp) {
        errorResp->setStatusCode(k500InternalServerError);
        callback(errorResp);
      }
    } catch (...) {
      std::cerr << "[InstanceHandler] Failed to create error response in handleOptions" << std::endl;
    }
  }
}

bool InstanceHandler::parseUpdateRequest(const Json::Value &json,
                                         UpdateInstanceRequest &req,
                                         std::string &error) {

  // Support both camelCase and PascalCase field names
  // Basic fields - camelCase
  if (json.isMember("name") && json["name"].isString()) {
    req.name = json["name"].asString();
  }
  // PascalCase support
  if (json.isMember("DisplayName") && json["DisplayName"].isString()) {
    req.name = json["DisplayName"].asString();
  }

  if (json.isMember("group") && json["group"].isString()) {
    req.group = json["group"].asString();
  }
  if (json.isMember("Group") && json["Group"].isString()) {
    req.group = json["Group"].asString();
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
  if (json.isMember("AutoStart") && json["AutoStart"].isBool()) {
    req.autoStart = json["AutoStart"].asBool();
  }

  if (json.isMember("autoRestart") && json["autoRestart"].isBool()) {
    req.autoRestart = json["autoRestart"].asBool();
  }

  // Parse Detector nested object
  if (json.isMember("Detector") && json["Detector"].isObject()) {
    const Json::Value &detector = json["Detector"];
    if (detector.isMember("current_preset") &&
        detector["current_preset"].isString()) {
      req.detectorMode = detector["current_preset"].asString();
    }
    if (detector.isMember("current_sensitivity_preset") &&
        detector["current_sensitivity_preset"].isString()) {
      req.detectionSensitivity =
          detector["current_sensitivity_preset"].asString();
    }
    if (detector.isMember("model_file") && detector["model_file"].isString()) {
      req.additionalParams["DETECTOR_MODEL_FILE"] =
          detector["model_file"].asString();
    }
    if (detector.isMember("animal_confidence_threshold") &&
        detector["animal_confidence_threshold"].isNumeric()) {
      req.additionalParams["ANIMAL_CONFIDENCE_THRESHOLD"] =
          std::to_string(detector["animal_confidence_threshold"].asDouble());
    }
    if (detector.isMember("person_confidence_threshold") &&
        detector["person_confidence_threshold"].isNumeric()) {
      req.additionalParams["PERSON_CONFIDENCE_THRESHOLD"] =
          std::to_string(detector["person_confidence_threshold"].asDouble());
    }
    if (detector.isMember("vehicle_confidence_threshold") &&
        detector["vehicle_confidence_threshold"].isNumeric()) {
      req.additionalParams["VEHICLE_CONFIDENCE_THRESHOLD"] =
          std::to_string(detector["vehicle_confidence_threshold"].asDouble());
    }
    if (detector.isMember("face_confidence_threshold") &&
        detector["face_confidence_threshold"].isNumeric()) {
      req.additionalParams["FACE_CONFIDENCE_THRESHOLD"] =
          std::to_string(detector["face_confidence_threshold"].asDouble());
    }
    if (detector.isMember("license_plate_confidence_threshold") &&
        detector["license_plate_confidence_threshold"].isNumeric()) {
      req.additionalParams["LICENSE_PLATE_CONFIDENCE_THRESHOLD"] =
          std::to_string(
              detector["license_plate_confidence_threshold"].asDouble());
    }
    if (detector.isMember("conf_threshold") &&
        detector["conf_threshold"].isNumeric()) {
      req.additionalParams["CONF_THRESHOLD"] =
          std::to_string(detector["conf_threshold"].asDouble());
    }
  }

  // Parse Input nested object
  if (json.isMember("Input") && json["Input"].isObject()) {
    const Json::Value &input = json["Input"];
    if (input.isMember("uri") && input["uri"].isString()) {
      std::string uri = input["uri"].asString();
      // Extract RTSP URL from GStreamer URI - support both old format (uri=)
      // and new format (location=)
      size_t rtspPos = uri.find("location=");
      if (rtspPos != std::string::npos) {
        // New format: rtspsrc location=...
        size_t start = rtspPos + 9;
        size_t end = uri.find(" ", start);
        if (end == std::string::npos) {
          end = uri.find(" !", start);
        }
        if (end == std::string::npos) {
          end = uri.length();
        }
        req.additionalParams["RTSP_URL"] = uri.substr(start, end - start);
      } else {
        // Old format: gstreamer:///urisourcebin uri=...
        rtspPos = uri.find("uri=");
        if (rtspPos != std::string::npos) {
          size_t start = rtspPos + 4;
          size_t end = uri.find(" !", start);
          if (end == std::string::npos) {
            end = uri.length();
          }
          req.additionalParams["RTSP_URL"] = uri.substr(start, end - start);
        } else if (uri.find("://") == std::string::npos) {
          // Direct file path
          req.additionalParams["FILE_PATH"] = uri;
        }
      }
    }
    if (input.isMember("media_type") && input["media_type"].isString()) {
      req.additionalParams["INPUT_MEDIA_TYPE"] = input["media_type"].asString();
    }
  }

  // Parse Output nested object
  if (json.isMember("Output") && json["Output"].isObject()) {
    const Json::Value &output = json["Output"];
    if (output.isMember("JSONExport") && output["JSONExport"].isObject()) {
      if (output["JSONExport"].isMember("enabled") &&
          output["JSONExport"]["enabled"].isBool()) {
        req.metadataMode = output["JSONExport"]["enabled"].asBool();
      }
    }
    if (output.isMember("handlers") && output["handlers"].isObject()) {
      // Parse RTSP handler if available
      for (const auto &handlerKey : output["handlers"].getMemberNames()) {
        const Json::Value &handler = output["handlers"][handlerKey];
        if (handler.isMember("uri") && handler["uri"].isString()) {
          req.additionalParams["OUTPUT_RTSP_URL"] = handler["uri"].asString();
        }
        if (handler.isMember("config") && handler["config"].isObject()) {
          if (handler["config"].isMember("fps") &&
              handler["config"]["fps"].isNumeric()) {
            req.frameRateLimit = handler["config"]["fps"].asInt();
          }
        }
      }
    }
  }

  // Parse SolutionManager nested object
  if (json.isMember("SolutionManager") && json["SolutionManager"].isObject()) {
    const Json::Value &sm = json["SolutionManager"];
    if (sm.isMember("frame_rate_limit") && sm["frame_rate_limit"].isNumeric()) {
      req.frameRateLimit = sm["frame_rate_limit"].asInt();
    }
    if (sm.isMember("send_metadata") && sm["send_metadata"].isBool()) {
      req.metadataMode = sm["send_metadata"].asBool();
    }
    if (sm.isMember("run_statistics") && sm["run_statistics"].isBool()) {
      req.statisticsMode = sm["run_statistics"].asBool();
    }
    if (sm.isMember("send_diagnostics") && sm["send_diagnostics"].isBool()) {
      req.diagnosticsMode = sm["send_diagnostics"].asBool();
    }
    if (sm.isMember("enable_debug") && sm["enable_debug"].isBool()) {
      req.debugMode = sm["enable_debug"].asBool();
    }
    if (sm.isMember("input_pixel_limit") &&
        sm["input_pixel_limit"].isNumeric()) {
      req.inputPixelLimit = sm["input_pixel_limit"].asInt();
    }
  }

  // Parse PerformanceMode nested object
  if (json.isMember("PerformanceMode") && json["PerformanceMode"].isObject()) {
    const Json::Value &pm = json["PerformanceMode"];
    if (pm.isMember("current_preset") && pm["current_preset"].isString()) {
      req.additionalParams["PERFORMANCE_MODE"] =
          pm["current_preset"].asString();
    }
  }

  // Parse DetectorThermal nested object
  if (json.isMember("DetectorThermal") && json["DetectorThermal"].isObject()) {
    const Json::Value &dt = json["DetectorThermal"];
    if (dt.isMember("model_file") && dt["model_file"].isString()) {
      req.additionalParams["DETECTOR_THERMAL_MODEL_FILE"] =
          dt["model_file"].asString();
    }
  }

  // Parse Zone nested object (store as JSON string for later processing)
  if (json.isMember("Zone") && json["Zone"].isObject()) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string zoneJson = Json::writeString(builder, json["Zone"]);
    req.additionalParams["ZONE_CONFIG"] = zoneJson;
  }

  // Parse Tripwire nested object
  if (json.isMember("Tripwire") && json["Tripwire"].isObject()) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string tripwireJson = Json::writeString(builder, json["Tripwire"]);
    req.additionalParams["TRIPWIRE_CONFIG"] = tripwireJson;
  }

  // Parse DetectorRegions nested object
  if (json.isMember("DetectorRegions") && json["DetectorRegions"].isObject()) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string regionsJson =
        Json::writeString(builder, json["DetectorRegions"]);
    req.additionalParams["DETECTOR_REGIONS_CONFIG"] = regionsJson;
  }

  if (json.isMember("inputOrientation") &&
      json["inputOrientation"].isNumeric()) {
    req.inputOrientation = json["inputOrientation"].asInt();
  }

  if (json.isMember("inputPixelLimit") && json["inputPixelLimit"].isNumeric()) {
    req.inputPixelLimit = json["inputPixelLimit"].asInt();
  }

  // Additional parameters (e.g., RTSP_URL, MODEL_PATH, FILE_PATH, RTMP_URL)
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
    req.additionalParams["MODEL_PATH"] = json["MODEL_PATH"].asString();
  }

  // Also check for FILE_PATH at top level (for file source)
  if (json.isMember("FILE_PATH") && json["FILE_PATH"].isString()) {
    req.additionalParams["FILE_PATH"] = json["FILE_PATH"].asString();
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
        json["SFACE_MODEL_PATH"].asString();
  }

  // Also check for SFACE_MODEL_NAME at top level (for SFace encoder by name)
  if (json.isMember("SFACE_MODEL_NAME") &&
      json["SFACE_MODEL_NAME"].isString()) {
    req.additionalParams["SFACE_MODEL_NAME"] =
        json["SFACE_MODEL_NAME"].asString();
  }

  return true;
}

Json::Value
InstanceHandler::instanceInfoToJson(const InstanceInfo &info) const {
  Json::Value json;

  // Format matching task/instance_detail.txt
  // Basic fields
  json["InstanceId"] = info.instanceId;
  json["DisplayName"] = info.displayName;
  json["AutoStart"] = info.autoStart;
  json["Solution"] = info.solutionId;

  // Async pipeline build status (match POST create response; required for polling GET before start)
  json["instanceId"] = info.instanceId;
  json["building"] = info.building;
  if (info.building) {
    json["status"] = "building";
    json["message"] = "Pipeline is being built in background";
  } else if (!info.buildError.empty()) {
    json["status"] = "error";
    json["buildError"] = info.buildError;
  } else {
    json["status"] = "ready";
  }

  // OriginatorInfo
  Json::Value originator(Json::objectValue);
  originator["address"] =
      info.originator.address.empty() ? "127.0.0.1" : info.originator.address;
  json["OriginatorInfo"] = originator;

  // Input configuration
  Json::Value input(Json::objectValue);
  if (!info.rtspUrl.empty()) {
    input["media_type"] = "IP Camera";

    // Check if user wants to use urisourcebin format (for
    // compatibility/auto-detect decoder)
    bool useUrisourcebin = false;
    auto urisourcebinIt = info.additionalParams.find("USE_URISOURCEBIN");
    if (urisourcebinIt != info.additionalParams.end()) {
      useUrisourcebin =
          (urisourcebinIt->second == "true" || urisourcebinIt->second == "1");
    }

    // Get decoder name from additionalParams
    std::string decoderName = "avdec_h264"; // Default decoder
    auto decoderIt = info.additionalParams.find("GST_DECODER_NAME");
    if (decoderIt != info.additionalParams.end() &&
        !decoderIt->second.empty()) {
      decoderName = decoderIt->second;
      // If decodebin is specified, use urisourcebin format
      if (decoderName == "decodebin") {
        useUrisourcebin = true;
      }
    }

    if (useUrisourcebin) {
      // Format: gstreamer:///urisourcebin uri=... ! decodebin ! videoconvert !
      // video/x-raw, format=NV12 ! appsink drop=true name=cvdsink This format
      // uses decodebin for auto-detection (may help with decoder compatibility
      // issues)
      input["uri"] = "gstreamer:///urisourcebin uri=" + info.rtspUrl +
                     " ! decodebin ! videoconvert ! video/x-raw, format=NV12 ! "
                     "appsink drop=true name=cvdsink";
    } else {
      // Format: rtspsrc location=... [protocols=...] !
      // application/x-rtp,media=video ! rtph264depay ! h264parse ! decoder !
      // videoconvert ! video/x-raw,format=NV12 ! appsink drop=true name=cvdsink
      // This format matches SDK template structure (with h264parse before
      // decoder) Get transport protocol from additionalParams if specified
      std::string protocolsParam = "";
      auto rtspTransportIt = info.additionalParams.find("RTSP_TRANSPORT");
      if (rtspTransportIt != info.additionalParams.end() &&
          !rtspTransportIt->second.empty()) {
        std::string transport = rtspTransportIt->second;
        std::transform(transport.begin(), transport.end(), transport.begin(),
                       ::tolower);
        if (transport == "tcp" || transport == "udp") {
          protocolsParam = " protocols=" + transport;
        }
      }
      // If no transport specified, don't add protocols parameter - let
      // GStreamer use default
      input["uri"] =
          "rtspsrc location=" + info.rtspUrl + protocolsParam +
          " ! application/x-rtp,media=video ! rtph264depay ! h264parse ! " +
          decoderName +
          " ! videoconvert ! video/x-raw,format=NV12 ! appsink drop=true "
          "name=cvdsink";
    }
  } else if (!info.filePath.empty()) {
    input["media_type"] = "File";
    input["uri"] = info.filePath;
  } else {
    input["media_type"] = "IP Camera";
    input["uri"] = "";
  }

  // Input media_format
  Json::Value mediaFormat(Json::objectValue);
  mediaFormat["color_format"] = 0;
  mediaFormat["default_format"] = true;
  mediaFormat["height"] = 0;
  mediaFormat["is_software"] = false;
  mediaFormat["name"] = "Same as Source";
  input["media_format"] = mediaFormat;
  json["Input"] = input;

  // Output configuration
  Json::Value output(Json::objectValue);
  output["JSONExport"]["enabled"] = info.metadataMode;
  output["NXWitness"]["enabled"] = false;

  // Output handlers (RTSP output if available)
  Json::Value handlers(Json::objectValue);
  if (!info.rtspUrl.empty() || !info.rtmpUrl.empty()) {
    // Create RTSP handler for output stream
    Json::Value rtspHandler(Json::objectValue);
    Json::Value handlerConfig(Json::objectValue);
    handlerConfig["debug"] = info.debugMode ? "4" : "0";
    // Use configuredFps (from API /api/v1/instances/{id}/fps) for output FPS
    // This ensures output stream matches processing FPS
    handlerConfig["fps"] = info.configuredFps > 0 ? info.configuredFps : 5;
    handlerConfig["pipeline"] =
        "( appsrc name=cvedia-rt ! videoconvert ! videoscale ! x264enc ! "
        "video/x-h264,profile=high ! rtph264pay name=pay0 pt=96 )";
    rtspHandler["config"] = handlerConfig;
    rtspHandler["enabled"] = info.running;
    rtspHandler["sink"] = "output-image";

    // Use RTSP URL if available, otherwise construct from RTMP
    std::string outputUrl = info.rtspUrl;
    if (outputUrl.empty() && !info.rtmpUrl.empty()) {
      // Extract port and stream name from RTMP URL if possible
      outputUrl = "rtsp://0.0.0.0:8554/stream1";
    }
    if (outputUrl.empty()) {
      outputUrl = "rtsp://0.0.0.0:8554/stream1";
    }
    rtspHandler["uri"] = outputUrl;
    handlers["rtsp:--0.0.0.0:8554-stream1"] = rtspHandler;
  }
  output["handlers"] = handlers;
  output["render_preset"] = "Default";
  json["Output"] = output;

  // Detector configuration
  Json::Value detector(Json::objectValue);
  detector["animal_confidence_threshold"] = info.animalConfidenceThreshold > 0.0
                                                ? info.animalConfidenceThreshold
                                                : 0.3;
  detector["conf_threshold"] =
      info.confThreshold > 0.0 ? info.confThreshold : 0.2;
  detector["current_preset"] =
      info.detectorMode.empty() ? "FullRegionInference" : info.detectorMode;
  detector["current_sensitivity_preset"] =
      info.detectionSensitivity.empty() ? "High" : info.detectionSensitivity;
  detector["face_confidence_threshold"] =
      info.faceConfidenceThreshold > 0.0 ? info.faceConfidenceThreshold : 0.1;
  detector["license_plate_confidence_threshold"] =
      info.licensePlateConfidenceThreshold > 0.0
          ? info.licensePlateConfidenceThreshold
          : 0.1;
  detector["model_file"] = info.detectorModelFile.empty()
                               ? "pva_det_full_frame_512"
                               : info.detectorModelFile;
  detector["person_confidence_threshold"] = info.personConfidenceThreshold > 0.0
                                                ? info.personConfidenceThreshold
                                                : 0.3;
  detector["vehicle_confidence_threshold"] =
      info.vehicleConfidenceThreshold > 0.0 ? info.vehicleConfidenceThreshold
                                            : 0.3;

  // Preset values
  Json::Value presetValues(Json::objectValue);
  Json::Value mosaicInference(Json::objectValue);
  mosaicInference["Detector/model_file"] = "pva_det_mosaic_320";
  presetValues["MosaicInference"] = mosaicInference;
  detector["preset_values"] = presetValues;

  json["Detector"] = detector;

  // DetectorRegions (empty by default)
  json["DetectorRegions"] = Json::Value(Json::objectValue);

  // DetectorThermal (always include)
  Json::Value detectorThermal(Json::objectValue);
  detectorThermal["model_file"] = info.detectorThermalModelFile.empty()
                                      ? "pva_det_mosaic_320"
                                      : info.detectorThermalModelFile;
  json["DetectorThermal"] = detectorThermal;

  // PerformanceMode
  Json::Value performanceMode(Json::objectValue);
  performanceMode["current_preset"] =
      info.performanceMode.empty() ? "Balanced" : info.performanceMode;
  json["PerformanceMode"] = performanceMode;

  // SolutionManager
  Json::Value solutionManager(Json::objectValue);
  solutionManager["enable_debug"] = info.debugMode;
  // Use configuredFps (from API /api/v1/instances/{id}/fps) for frame_rate_limit
  // This ensures SDK processing matches configured FPS
  // Fallback to frameRateLimit if configuredFps not set (backward compatibility)
  solutionManager["frame_rate_limit"] =
      info.configuredFps > 0 ? info.configuredFps : 
      (info.frameRateLimit > 0 ? info.frameRateLimit : 5);
  solutionManager["input_pixel_limit"] =
      info.inputPixelLimit > 0 ? info.inputPixelLimit : 2000000;
  solutionManager["recommended_frame_rate"] =
      info.recommendedFrameRate > 0 ? info.recommendedFrameRate : 5;
  solutionManager["run_statistics"] = info.statisticsMode;
  solutionManager["send_diagnostics"] = info.diagnosticsMode;
  solutionManager["send_metadata"] = info.metadataMode;
  json["SolutionManager"] = solutionManager;

  // Tripwire (empty by default)
  Json::Value tripwire(Json::objectValue);
  tripwire["Tripwires"] = Json::Value(Json::objectValue);
  json["Tripwire"] = tripwire;

  // Zone (empty by default, can be populated from additionalParams if needed)
  Json::Value zone(Json::objectValue);
  zone["Zones"] = Json::Value(Json::objectValue);
  json["Zone"] = zone;

  return json;
}

HttpResponsePtr
InstanceHandler::createErrorResponse(int statusCode, const std::string &error,
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
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, PUT, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");

  return resp;
}

