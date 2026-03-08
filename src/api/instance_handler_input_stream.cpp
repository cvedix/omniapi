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

void InstanceHandler::setInstanceInput(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO
        << "[API] POST /v1/core/instance/{instanceId}/input - Set input source";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/input - "
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
        PLOG_WARNING << "[API] POST /v1/core/instance/{instanceId}/input - "
                        "Error: Instance ID is empty";
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
                     << "/input - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Validate required fields
    if (!json->isMember("type") || !(*json)["type"].isString()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/input - Error: Missing or invalid 'type' field";
      }
      callback(createErrorResponse(
          400, "Bad request", "Field 'type' is required and must be a string"));
      return;
    }

    if (!json->isMember("uri") || !(*json)["uri"].isString()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/input - Error: Missing or invalid 'uri' field";
      }
      callback(createErrorResponse(
          400, "Bad request", "Field 'uri' is required and must be a string"));
      return;
    }

    std::string type = (*json)["type"].asString();
    std::string uri = (*json)["uri"].asString();

    // Validate type
    if (type != "RTSP" && type != "HLS" && type != "Manual") {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/input - Error: Invalid type: " << type;
      }
      callback(createErrorResponse(
          400, "Bad request",
          "Field 'type' must be one of: RTSP, HLS, Manual"));
      return;
    }

    // Validate uri is not empty
    if (uri.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/input - Error: URI cannot be empty";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Field 'uri' cannot be empty"));
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
                     << "/input - Instance not found - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(404, "Instance not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Build Input configuration JSON (PascalCase format)
    Json::Value inputConfig(Json::objectValue);
    Json::Value input(Json::objectValue);

    // Set media_format (required field)
    Json::Value mediaFormat(Json::objectValue);
    mediaFormat["color_format"] = 0;
    mediaFormat["default_format"] = true;
    mediaFormat["height"] = 0;
    mediaFormat["is_software"] = false;
    mediaFormat["name"] = "Same as Source";
    input["media_format"] = mediaFormat;

    // Set media_type and uri based on type
    if (type == "RTSP") {
      input["media_type"] = "IP Camera";

      // Check if user wants to use urisourcebin format (for
      // compatibility/auto-detect decoder)
      bool useUrisourcebin = false;
      if (json->isMember("additionalParams") &&
          (*json)["additionalParams"].isObject()) {
        const Json::Value &additionalParams = (*json)["additionalParams"];
        if (additionalParams.isMember("USE_URISOURCEBIN") &&
            additionalParams["USE_URISOURCEBIN"].isString()) {
          useUrisourcebin =
              (additionalParams["USE_URISOURCEBIN"].asString() == "true" ||
               additionalParams["USE_URISOURCEBIN"].asString() == "1");
        }
      }

      // Get decoder name from instance additionalParams or request JSON
      std::string decoderName = "avdec_h264"; // Default decoder

      // First try to get from request JSON additionalParams
      if (json->isMember("additionalParams") &&
          (*json)["additionalParams"].isObject()) {
        const Json::Value &additionalParams = (*json)["additionalParams"];
        if (additionalParams.isMember("GST_DECODER_NAME") &&
            additionalParams["GST_DECODER_NAME"].isString()) {
          decoderName = additionalParams["GST_DECODER_NAME"].asString();
          // If decodebin is specified, use urisourcebin format
          if (decoderName == "decodebin") {
            useUrisourcebin = true;
          }
        }
      }

      // If not found in request, try to get from existing instance info
      if (decoderName == "avdec_h264" && optInfo.has_value()) {
        const InstanceInfo &info = optInfo.value();
        auto decoderIt = info.additionalParams.find("GST_DECODER_NAME");
        if (decoderIt != info.additionalParams.end() &&
            !decoderIt->second.empty()) {
          decoderName = decoderIt->second;
          if (decoderName == "decodebin") {
            useUrisourcebin = true;
          }
        }
        // Check USE_URISOURCEBIN from instance
        auto urisourcebinIt = info.additionalParams.find("USE_URISOURCEBIN");
        if (urisourcebinIt != info.additionalParams.end()) {
          useUrisourcebin = (urisourcebinIt->second == "true" ||
                             urisourcebinIt->second == "1");
        }
      }

      if (useUrisourcebin) {
        // Format: gstreamer:///urisourcebin uri=... ! decodebin ! videoconvert
        // ! video/x-raw, format=NV12 ! appsink drop=true name=cvdsink This
        // format uses decodebin for auto-detection (may help with decoder
        // compatibility issues)
        input["uri"] = "gstreamer:///urisourcebin uri=" + uri +
                       " ! decodebin ! videoconvert ! video/x-raw, format=NV12 "
                       "! appsink drop=true name=cvdsink";
      } else {
        // Format: rtspsrc location=... [protocols=...] !
        // application/x-rtp,media=video ! rtph264depay ! h264parse ! decoder !
        // videoconvert ! video/x-raw,format=NV12 ! appsink drop=true
        // name=cvdsink This format matches SDK template structure (with
        // h264parse before decoder) Get transport protocol from request JSON or
        // instance info
        std::string protocolsParam = "";
        std::string rtspTransport = "";

        // First try request JSON additionalParams
        if (json->isMember("additionalParams") &&
            (*json)["additionalParams"].isObject()) {
          const Json::Value &additionalParams = (*json)["additionalParams"];
          if (additionalParams.isMember("RTSP_TRANSPORT") &&
              additionalParams["RTSP_TRANSPORT"].isString()) {
            rtspTransport = additionalParams["RTSP_TRANSPORT"].asString();
          }
        }

        // If not found, try instance info
        if (rtspTransport.empty() && optInfo.has_value()) {
          const InstanceInfo &info = optInfo.value();
          auto rtspTransportIt = info.additionalParams.find("RTSP_TRANSPORT");
          if (rtspTransportIt != info.additionalParams.end() &&
              !rtspTransportIt->second.empty()) {
            rtspTransport = rtspTransportIt->second;
          }
        }

        // Validate and add protocols parameter
        if (!rtspTransport.empty()) {
          std::transform(rtspTransport.begin(), rtspTransport.end(),
                         rtspTransport.begin(), ::tolower);
          if (rtspTransport == "tcp" || rtspTransport == "udp") {
            protocolsParam = " protocols=" + rtspTransport;
          }
        }
        // If no transport specified, don't add protocols parameter - let
        // GStreamer use default

        input["uri"] =
            "rtspsrc location=" + uri + protocolsParam +
            " ! application/x-rtp,media=video ! rtph264depay ! h264parse ! " +
            decoderName +
            " ! videoconvert ! video/x-raw,format=NV12 ! appsink drop=true "
            "name=cvdsink";
      }
    } else if (type == "HLS") {
      input["media_type"] = "IP Camera";
      // For HLS, use the URI directly (hls_src node will handle it)
      input["uri"] = uri;
    } else if (type == "Manual") {
      input["media_type"] = "IP Camera";
      // For Manual (v4l2_src), use the URI directly (device path)
      input["uri"] = uri;
    }

    inputConfig["Input"] = input;

    // Update instance using updateInstanceFromConfig
    if (instance_manager_->updateInstanceFromConfig(instanceId, inputConfig)) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                  << "/input - Success - " << duration.count() << "ms";
      }

      // Return 204 No Content as specified in task
      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k204NoContent);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods",
                      "GET, POST, PUT, DELETE, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers",
                      "Content-Type, Authorization");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
    } else {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/input - Failed to update - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to update input settings"));
    }

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR
          << "[API] POST /v1/core/instance/{instanceId}/input - Exception: "
          << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/input - Unknown "
                    "exception - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void InstanceHandler::getStreamOutput(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  // Get instance ID from path parameter
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
              << "/output/stream - Get stream output configuration";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR
            << "[API] GET /v1/core/instance/" << instanceId
            << "/output/stream - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] GET /v1/core/instance/{instanceId}/output/stream - "
               "Error: Instance ID is required";
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
                     << "/output/stream - Instance not found - "
                     << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Instance not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    const InstanceInfo &info = optInfo.value();

    // Build response with stream output configuration
    Json::Value response;

    // Get RTMP URL from instance (check both rtmpUrl field and
    // additionalParams)
    std::string streamUri;
    if (!info.rtmpUrl.empty()) {
      streamUri = info.rtmpUrl;
    } else if (info.additionalParams.find("RTMP_DES_URL") !=
               info.additionalParams.end()) {
      streamUri = info.additionalParams.at("RTMP_DES_URL");
    } else if (info.additionalParams.find("RTMP_URL") !=
               info.additionalParams.end()) {
      streamUri = info.additionalParams.at("RTMP_URL");
    }

    // Get RECORD_PATH if exists (for record output mode)
    std::string recordPath;
    if (info.additionalParams.find("RECORD_PATH") !=
        info.additionalParams.end()) {
      recordPath = info.additionalParams.at("RECORD_PATH");
    }

    // Determine if stream output is enabled (has URI or RECORD_PATH)
    bool enabled = !streamUri.empty() || !recordPath.empty();
    response["enabled"] = enabled;

    // Set URI if enabled (for stream output mode)
    if (enabled) {
      if (!streamUri.empty()) {
        response["uri"] = streamUri;
      } else {
        response["uri"] = ""; // Empty URI when using record mode
      }

      // Set path if exists (for record output mode)
      if (!recordPath.empty()) {
        response["path"] = recordPath;
      }
    } else {
      response["uri"] = "";  // Empty string when disabled
      response["path"] = ""; // Empty string when disabled
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
                << "/output/stream - Success - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/{instanceId}/output/stream - "
                    "Exception: "
                 << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/{instanceId}/output/stream - "
                    "Unknown exception - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

