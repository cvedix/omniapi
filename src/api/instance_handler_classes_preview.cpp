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

void InstanceHandler::getInstanceClasses(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
              << "/classes - Get available classes";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                   << "/classes - Error: Instance manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/{instanceId}/classes - "
                        "Error: Instance ID is empty";
      }
      callback(
          createErrorResponse(400, "Bad request", "Instance ID is required"));
      return;
    }

    // Check if instance exists
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId
                     << "/classes - Instance not found - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Get instance config to find LABELS_PATH
    Json::Value config = instance_manager_->getInstanceConfig(instanceId);
    if (config.empty() || !config.isMember("InstanceId")) {
      callback(createErrorResponse(404, "Not found",
                                   "Instance config not found: " + instanceId));
      return;
    }

    // Find LABELS_PATH in AdditionalParams
    std::string labelsPath;
    if (config.isMember("AdditionalParams")) {
      const Json::Value &additionalParams = config["AdditionalParams"];
      if (additionalParams.isMember("input") &&
          additionalParams["input"].isObject()) {
        if (additionalParams["input"].isMember("LABELS_PATH") &&
            additionalParams["input"]["LABELS_PATH"].isString()) {
          labelsPath = additionalParams["input"]["LABELS_PATH"].asString();
        }
      }
      if (labelsPath.empty() && additionalParams.isMember("LABELS_PATH") &&
          additionalParams["LABELS_PATH"].isString()) {
        labelsPath = additionalParams["LABELS_PATH"].asString();
      }
    }

    // Build response
    Json::Value response;
    response["labelsPath"] = labelsPath;

    if (labelsPath.empty()) {
      // No labels file found, return empty classes array
      response["classes"] = Json::Value(Json::arrayValue);
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG
            << "[API] GET /v1/core/instance/" << instanceId
            << "/classes - No LABELS_PATH found, returning empty classes";
      }
    } else {
      // Read classes from file
      std::vector<std::string> classes = readClassesFromFile(labelsPath);
      Json::Value classesArray(Json::arrayValue);
      for (const auto &cls : classes) {
        classesArray.append(cls);
      }
      response["classes"] = classesArray;
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[API] GET /v1/core/instance/" << instanceId
                   << "/classes - Found " << classes.size() << " classes from "
                   << labelsPath;
      }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
                << "/classes - Success - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << "/classes - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << "/classes - Unknown exception - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void InstanceHandler::getInstancePreview(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
              << "/preview - Get preview frame";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                   << "/preview - Error: Instance manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/{instanceId}/preview - "
                        "Error: Instance ID is empty";
      }
      callback(
          createErrorResponse(400, "Bad request", "Instance ID is required"));
      return;
    }

    // Check if instance exists
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId
                     << "/preview - Instance not found - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    const InstanceInfo &info = optInfo.value();

    // Get last frame (empty string if no frame cached)
    std::string frameBase64 = instance_manager_->getLastFrame(instanceId);

    // Build JSON response
    Json::Value response;
    if (frameBase64.empty()) {
      response["imageUrl"] = "";
      response["available"] = false;
      response["message"] = "No frame available. Instance may not be running "
                            "or no frames processed yet.";
    } else {
      // Return as data URL format
      response["imageUrl"] = "data:image/jpeg;base64," + frameBase64;
      response["available"] = true;
    }
    response["running"] = info.running;
    response["timestamp"] =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
                << "/preview - Success - " << duration.count()
                << "ms (frame available: " << !frameBase64.empty() << ")";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << "/preview - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << "/preview - Unknown exception - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

std::vector<std::string>
InstanceHandler::readClassesFromFile(const std::string &labelsPath) const {
  std::vector<std::string> classes;

  try {
    if (labelsPath.empty()) {
      return classes;
    }

    // Check if file exists
    if (!fs::exists(labelsPath)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[InstanceHandler] Labels file not found: "
                     << labelsPath;
      }
      return classes;
    }

    // Read file line by line
    std::ifstream file(labelsPath);
    if (!file.is_open()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[InstanceHandler] Failed to open labels file: "
                     << labelsPath;
      }
      return classes;
    }

    std::string line;
    while (std::getline(file, line)) {
      // Trim whitespace
      line.erase(0, line.find_first_not_of(" \t\r\n"));
      line.erase(line.find_last_not_of(" \t\r\n") + 1);

      // Skip empty lines and comments
      if (line.empty() || line[0] == '#') {
        continue;
      }

      classes.push_back(line);
    }

    file.close();

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[InstanceHandler] Read " << classes.size()
                 << " classes from " << labelsPath;
    }

  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[InstanceHandler] Exception reading labels file "
                 << labelsPath << ": " << e.what();
    }
  } catch (...) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[InstanceHandler] Unknown exception reading labels file: "
                 << labelsPath;
    }
  }

  return classes;
}

