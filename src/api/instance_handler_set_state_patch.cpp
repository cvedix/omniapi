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

void InstanceHandler::setInstanceState(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO
        << "[API] POST /v1/core/instance/{instanceId}/state - Set instance state";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/state - "
                      "Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    // Get instance ID from path parameter
    std::string instanceId = extractInstanceId(req);

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/{instanceId}/state - "
                        "Error: Instance ID is required";
      }
      callback(
          createErrorResponse(400, "Bad request", "Instance ID is required"));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/state - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Validate required fields
    if (!json->isMember("path") || !(*json)["path"].isString()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/state - Error: Missing or invalid 'path' field";
      }
      callback(createErrorResponse(
          400, "Bad request", "Field 'path' is required and must be a string"));
      return;
    }

    if (!json->isMember("jsonValue") || !(*json)["jsonValue"].isString()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/state - Error: Missing or invalid 'jsonValue' field";
      }
      callback(createErrorResponse(
          400, "Bad request",
          "Field 'jsonValue' is required and must be a string"));
      return;
    }

    std::string path = (*json)["path"].asString();
    std::string jsonValueStr = (*json)["jsonValue"].asString();

    // Validate path is not empty
    if (path.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/state - Error: Path cannot be empty";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Field 'path' cannot be empty"));
      return;
    }

    // Validate jsonValue is not empty
    if (jsonValueStr.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/state - Error: jsonValue cannot be empty";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Field 'jsonValue' cannot be empty"));
      return;
    }

    // Parse jsonValue string to JSON value
    Json::Value parsedValue;
    Json::CharReaderBuilder readerBuilder;
    std::string parseErrors;
    std::istringstream jsonStream(jsonValueStr);

    if (!Json::parseFromStream(readerBuilder, jsonStream, &parsedValue,
                               &parseErrors)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/state - Error: Invalid JSON in jsonValue: "
                     << parseErrors;
      }
      callback(createErrorResponse(
          400, "Bad request",
          "Field 'jsonValue' must contain valid JSON: " + parseErrors));
      return;
    }

    // Check if instance exists
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/state - Instance not found - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(404, "Instance not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Set state
    if (!instance_manager_->setInstanceState(instanceId, path, parsedValue)) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/state - Instance not loaded - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(406, "Not Acceptable",
                                   "Instance not loaded: " + instanceId));
      return;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                << "/state - Success - " << duration.count() << "ms";
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    callback(resp);

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/state - Exception: "
                 << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/state - Unknown "
                    "exception - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void InstanceHandler::patchInstance(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] PATCH /v1/core/instance/{instanceId} - Patch instance";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] PATCH /v1/core/instance/{instanceId} - "
                      "Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    // Get instance ID from path parameter
    std::string instanceId = extractInstanceId(req);

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PATCH /v1/core/instance/{instanceId} - "
                        "Error: Instance ID is required";
      }
      callback(
          createErrorResponse(400, "Bad request", "Instance ID is required"));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PATCH /v1/core/instance/" << instanceId
                     << " - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Check if instance exists
    if (!instance_manager_->hasInstance(instanceId)) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PATCH /v1/core/instance/" << instanceId
                     << " - Instance not found - " << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Instance not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Update instance with partial data (same as updateInstance but allows
    // partial updates)
    if (!instance_manager_->updateInstance(instanceId, *json)) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PATCH /v1/core/instance/" << instanceId
                     << " - Update failed - " << duration.count() << "ms";
      }
      callback(createErrorResponse(500, "Internal Server Error",
                                   "Failed to update instance"));
      return;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] PATCH /v1/core/instance/" << instanceId
                << " - Success - " << duration.count() << "ms";
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    callback(resp);

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PATCH /v1/core/instance/{instanceId} - Exception: "
                 << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PATCH /v1/core/instance/{instanceId} - Unknown "
                    "exception - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

