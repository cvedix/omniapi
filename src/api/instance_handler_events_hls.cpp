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

void InstanceHandler::consumeEvents(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/instance/{instanceId}/consume_events - "
                   "Consume events";
    }

    // Check if instance manager is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/instance/{instanceId}/consume_events - "
                      "Error: Instance manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    // Get instance ID from path parameter
    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/{instanceId}/consume_events - "
                        "Error: Instance ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Check if instance exists
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId
                     << "/consume_events - Instance not found";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Consume events from queue
    auto &eventQueue = EventQueue::getInstance();
    auto events = eventQueue.consumeEvents(instanceId, 0); // 0 = all events

    // Build response
    Json::Value response(Json::arrayValue);
    for (const auto &event : events) {
      Json::Value eventJson;
      eventJson["dataType"] = event.dataType;
      eventJson["jsonObject"] = event.jsonObject;
      response.append(eventJson);
    }

    // Return 204 if no events, 200 if events available
    auto resp = HttpResponse::newHttpJsonResponse(response);
    if (events.empty()) {
      resp->setStatusCode(k204NoContent);
    } else {
      resp->setStatusCode(k200OK);
    }
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
                << "/consume_events - Success: " << events.size() << " events";
    }

  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/{instanceId}/consume_events - "
                    "Exception: "
                 << e.what();
    }
    auto resp = createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (...) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/{instanceId}/consume_events - "
                    "Unknown exception";
    }
    auto resp = createErrorResponse(500, "Internal server error",
                                   "Unknown error occurred");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void InstanceHandler::configureHlsOutput(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/core/instance/{instanceId}/output/hls - "
                   "Configure HLS output";
    }

    // Check if instance manager is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/output/hls - "
                      "Error: Instance manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    // Get instance ID from path parameter
    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/{instanceId}/output/hls - "
                        "Error: Instance ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Check if instance exists
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/output/hls - Instance not found";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Parse request body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/output/hls - Invalid JSON";
      }
      callback(createErrorResponse(400, "Bad request", "Invalid JSON body"));
      return;
    }

    // Check enabled field
    if (!json->isMember("enabled") || !(*json)["enabled"].isBool()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/output/hls - Missing 'enabled' field";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Field 'enabled' is required"));
      return;
    }

    bool enabled = (*json)["enabled"].asBool();

    if (enabled) {
      // Configure HLS output
      // Get host and port from environment or use defaults
      std::string host = EnvConfig::getString("API_HOST", "localhost");
      std::string port = EnvConfig::getString("API_PORT", "8080");

      // Generate HLS URI
      std::string hlsUri = "http://" + host + ":" + port + "/hls/" + instanceId +
                          "/stream.m3u8";

      // Configure stream output using existing method
      // Use same structure as /output/stream endpoint
      Json::Value streamConfig(Json::objectValue);
      streamConfig["enabled"] = true;
      
      // Set URI in AdditionalParams (same as /output/stream does)
      if (!streamConfig.isMember("AdditionalParams")) {
        streamConfig["AdditionalParams"] = Json::Value(Json::objectValue);
      }
      std::string hlsStreamUri = "hls://" + host + ":" + port + "/hls/" + instanceId + "/stream";
      streamConfig["AdditionalParams"]["RTMP_URL"] = hlsStreamUri;
      streamConfig["AdditionalParams"]["HLS_URI"] = hlsUri;

      // Update instance config
      if (!instance_manager_->updateInstanceFromConfig(instanceId,
                                                        streamConfig)) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                       << "/output/hls - Could not set HLS output";
        }
        callback(createErrorResponse(406, "Could not set HLS output",
                                     "Failed to configure HLS output"));
        return;
      }

      // Return URI
      Json::Value response;
      response["uri"] = hlsUri;

      auto resp = HttpResponse::newHttpJsonResponse(response);
      resp->setStatusCode(k200OK);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                  << "/output/hls - Success: " << hlsUri;
      }
    } else {
      // Disable HLS output
      Json::Value streamConfig(Json::objectValue);
      streamConfig["enabled"] = false;

      instance_manager_->updateInstanceFromConfig(instanceId, streamConfig);

      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k204NoContent);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                  << "/output/hls - Disabled";
      }
    }

  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/output/hls - "
                    "Exception: "
                 << e.what();
    }
    auto resp = createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (...) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/output/hls - "
                    "Unknown exception";
    }
    auto resp = createErrorResponse(500, "Internal server error",
                                   "Unknown error occurred");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

