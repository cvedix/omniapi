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
