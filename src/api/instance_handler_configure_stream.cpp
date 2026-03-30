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

void InstanceHandler::configureStreamOutput(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  // Get instance ID from path parameter
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
              << "/output/stream - Configure stream output";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR
            << "[API] POST /v1/core/instance/" << instanceId
            << "/output/stream - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] POST /v1/core/instance/{instanceId}/output/stream - "
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
                     << "/output/stream - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Validate required fields
    if (!json->isMember("enabled") || !(*json)["enabled"].isBool()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] POST /v1/core/instance/" << instanceId
            << "/output/stream - Error: Missing or invalid 'enabled' field";
      }
      callback(createErrorResponse(
          400, "Bad request",
          "Field 'enabled' is required and must be a boolean"));
      return;
    }

    bool enabled = (*json)["enabled"].asBool();

    // Check if instance exists
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/output/stream - Instance not found - "
                     << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Instance not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // If enabled, validate and set path for record output
    if (enabled) {
      // Check if path is provided (for record output) or uri (for stream
      // output)
      bool hasPath = json->isMember("path") && (*json)["path"].isString();
      bool hasUri = json->isMember("uri") && (*json)["uri"].isString();

      Json::Value streamConfig(Json::objectValue);

      if (hasPath) {
        // Record output mode: save video to local path
        std::string path = (*json)["path"].asString();

        // Validate path is not empty
        if (path.empty()) {
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                         << "/output/stream - Error: Path cannot be empty";
          }
          callback(createErrorResponse(400, "Bad request",
                                       "Field 'path' cannot be empty"));
          return;
        }

        // Validate and create path
        try {
          fs::path dirPath(path);

          // Create directory if it doesn't exist (with fallback if needed)
          if (!fs::exists(dirPath)) {
            if (isApiLoggingEnabled()) {
              PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                        << "/output/stream - Creating directory: " << path;
            }
            // Extract subdir from path for fallback
            std::string subdir = dirPath.filename().string();
            if (subdir.empty()) {
              subdir = "output"; // Default fallback subdir
            }
            std::string resolvedPath =
                EnvConfig::resolveDirectory(path, subdir);
            if (resolvedPath != path) {
              // Fallback was used, update path
              path = resolvedPath;
              dirPath = fs::path(path);
            }
            fs::create_directories(dirPath);
          }

          // Check if path is a directory
          if (!fs::is_directory(dirPath)) {
            if (isApiLoggingEnabled()) {
              PLOG_WARNING
                  << "[API] POST /v1/core/instance/" << instanceId
                  << "/output/stream - Error: Path is not a directory: "
                  << path;
            }
            callback(createErrorResponse(400, "Bad request",
                                         "Path must be a directory"));
            return;
          }

          // Check write permissions by attempting to create a test file
          fs::path testFile = dirPath / ".write_test";
          std::ofstream testStream(testFile);
          if (!testStream.is_open()) {
            if (isApiLoggingEnabled()) {
              PLOG_WARNING
                  << "[API] POST /v1/core/instance/" << instanceId
                  << "/output/stream - Error: No write permission for path: "
                  << path;
            }
            callback(createErrorResponse(
                400, "Bad request", "Path does not have write permissions"));
            return;
          }
          testStream.close();
          fs::remove(testFile); // Clean up test file

        } catch (const fs::filesystem_error &e) {
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                         << "/output/stream - Error validating path: "
                         << e.what();
          }
          callback(createErrorResponse(
              400, "Bad request", "Invalid path: " + std::string(e.what())));
          return;
        } catch (const std::exception &e) {
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                         << "/output/stream - Error validating path: "
                         << e.what();
          }
          callback(createErrorResponse(400, "Bad request",
                                       "Error validating path: " +
                                           std::string(e.what())));
          return;
        }

        // Store RECORD_PATH in AdditionalParams
        // Note: We do NOT overwrite RTMP_URL here to preserve the existing
        // stream configuration The file_des node only needs RECORD_PATH, not
        // RTMP_URL
        if (!streamConfig.isMember("AdditionalParams")) {
          streamConfig["AdditionalParams"] = Json::Value(Json::objectValue);
        }
        streamConfig["AdditionalParams"]["RECORD_PATH"] = path;

        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                    << "/output/stream - Configuring record output to path: "
                    << path;
          PLOG_WARNING
              << "[API] NOTE: Recorded MP4 files may use H.264 High profile "
                 "with yuv444p pixel format, which may not be compatible with "
                 "all video players. Use ./scripts/convert_mp4_compatible.sh "
                 "to convert files for maximum compatibility.";
        }

      } else if (hasUri) {
        // Stream output mode: stream to URI (RTMP/RTSP/HLS)
        std::string uri = (*json)["uri"].asString();

        // Validate URI is not empty
        if (uri.empty()) {
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                         << "/output/stream - Error: URI cannot be empty";
          }
          callback(createErrorResponse(400, "Bad request",
                                       "Field 'uri' cannot be empty"));
          return;
        }

        // Validate URI format (rtmp://, rtsp://, or hls://)
        if (uri.find("rtmp://") != 0 && uri.find("rtsp://") != 0 &&
            uri.find("hls://") != 0) {
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                         << "/output/stream - Error: Invalid URI format. Must "
                            "start with rtmp://, rtsp://, or hls://";
          }
          callback(createErrorResponse(
              400, "Bad request",
              "URI must start with rtmp://, rtsp://, or hls://"));
          return;
        }

        // Determine stream type from URI
        std::string streamType;
        if (uri.find("rtmp://") == 0) {
          streamType = "RTMP";
        } else if (uri.find("rtsp://") == 0) {
          streamType = "RTSP";
        } else if (uri.find("hls://") == 0) {
          streamType = "HLS";
        }

        // Set RTMP_URL in additionalParams
        if (!streamConfig.isMember("AdditionalParams")) {
          streamConfig["AdditionalParams"] = Json::Value(Json::objectValue);
        }
        streamConfig["AdditionalParams"]["RTMP_URL"] = uri;

        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                    << "/output/stream - Configuring " << streamType
                    << " stream: " << uri;
        }
      } else {
        // Neither path nor uri provided
        if (isApiLoggingEnabled()) {
          PLOG_WARNING
              << "[API] POST /v1/core/instance/" << instanceId
              << "/output/stream - Error: Missing 'path' or 'uri' field";
        }
        callback(createErrorResponse(
            400, "Bad request",
            "Field 'path' (for record output) or 'uri' (for stream output) is "
            "required when enabled is true"));
        return;
      }

      // Update instance using updateInstanceFromConfig
      // This will merge streamConfig with existing config and update
      // AdditionalParams
      if (instance_manager_->updateInstanceFromConfig(instanceId,
                                                      streamConfig)) {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        if (isApiLoggingEnabled()) {
          PLOG_INFO
              << "[API] POST /v1/core/instance/" << instanceId
              << "/output/stream - Success: Record/stream output configured - "
              << duration.count() << "ms";
        }

        // Get updated instance info to return current configuration
        auto optInfo = instance_manager_->getInstance(instanceId);
        if (!optInfo.has_value()) {
          callback(createErrorResponse(500, "Internal server error",
                                       "Failed to retrieve instance info"));
          return;
        }

        const InstanceInfo &info = optInfo.value();

        // Build response with current stream output configuration
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
          } else {
            response["path"] = ""; // Empty path when using stream mode
          }
        } else {
          response["uri"] = "";  // Empty string when disabled
          response["path"] = ""; // Empty string when disabled
        }

        // Return 200 OK with current configuration
        callback(createSuccessResponse(response));
      } else {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                       << "/output/stream - Failed to update - "
                       << duration.count() << "ms";
        }
        callback(createErrorResponse(500, "Internal server error",
                                     "Failed to configure stream output"));
      }
    } else {
      // Disable stream/record output - clear RTMP_URL and RECORD_PATH
      Json::Value streamConfig(Json::objectValue);
      if (!streamConfig.isMember("AdditionalParams")) {
        streamConfig["AdditionalParams"] = Json::Value(Json::objectValue);
      }
      streamConfig["AdditionalParams"]["RTMP_URL"] =
          ""; // Empty string to clear
      streamConfig["AdditionalParams"]["RECORD_PATH"] =
          ""; // Empty string to clear

      if (instance_manager_->updateInstanceFromConfig(instanceId,
                                                      streamConfig)) {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                    << "/output/stream - Success: Stream output disabled - "
                    << duration.count() << "ms";
        }

        // Get updated instance info to return current configuration
        auto optInfo = instance_manager_->getInstance(instanceId);
        if (!optInfo.has_value()) {
          callback(createErrorResponse(500, "Internal server error",
                                       "Failed to retrieve instance info"));
          return;
        }

        const InstanceInfo &info = optInfo.value();

        // Build response with current stream output configuration
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
          } else {
            response["path"] = ""; // Empty path when using stream mode
          }
        } else {
          response["uri"] = "";  // Empty string when disabled
          response["path"] = ""; // Empty string when disabled
        }

        // Return 200 OK with current configuration
        callback(createSuccessResponse(response));
      } else {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                       << "/output/stream - Failed to disable - "
                       << duration.count() << "ms";
        }
        callback(createErrorResponse(500, "Internal server error",
                                     "Failed to disable stream output"));
      }
    }

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/output/stream - "
                    "Exception: "
                 << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/output/stream - "
                    "Unknown exception - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

