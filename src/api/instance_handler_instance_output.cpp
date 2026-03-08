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

void InstanceHandler::getInstanceOutput(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  try {
    std::cerr << "[InstanceHandler] getInstanceOutput called" << std::endl;

    // Check if registry is set
    if (!instance_manager_) {
      std::cerr << "[InstanceHandler] Error: Instance registry not initialized"
                << std::endl;
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    // Get instance ID from path parameter
    std::string instanceId = extractInstanceId(req);
    std::cerr << "[InstanceHandler] Extracted instance ID: " << instanceId
              << std::endl;

    if (instanceId.empty()) {
      std::cerr << "[InstanceHandler] Error: Instance ID is empty" << std::endl;
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Get instance info
    std::cerr << "[InstanceHandler] Getting instance info for: " << instanceId
              << std::endl;
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      std::cerr << "[InstanceHandler] Error: Instance not found: " << instanceId
                << std::endl;
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    std::cerr << "[InstanceHandler] Instance found, building response..."
              << std::endl;

    const InstanceInfo &info = optInfo.value();

    // Build output response
    Json::Value response;

    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    response["timestamp"] = ss.str();

    // Basic instance info
    response["instanceId"] = info.instanceId;
    response["displayName"] = info.displayName;
    response["solutionId"] = info.solutionId;
    response["solutionName"] = info.solutionName;
    response["running"] = info.running;
    response["loaded"] = info.loaded;

    // Processing metrics
    Json::Value metrics;
    metrics["fps"] = info.fps;
    metrics["frameRateLimit"] = info.frameRateLimit;
    response["metrics"] = metrics;

    // Input source
    Json::Value input;
    if (!info.filePath.empty()) {
      input["type"] = "FILE";
      input["path"] = info.filePath;
    } else if (info.additionalParams.find("RTSP_SRC_URL") !=
               info.additionalParams.end()) {
      input["type"] = "RTSP";
      input["url"] = info.additionalParams.at("RTSP_SRC_URL");
    } else if (info.additionalParams.find("RTSP_URL") !=
               info.additionalParams.end()) {
      input["type"] = "RTSP";
      input["url"] = info.additionalParams.at("RTSP_URL");
    } else if (info.additionalParams.find("FILE_PATH") !=
               info.additionalParams.end()) {
      input["type"] = "FILE";
      input["path"] = info.additionalParams.at("FILE_PATH");
    } else {
      input["type"] = "UNKNOWN";
    }
    response["input"] = input;

    // Output information
    Json::Value output;
    bool hasRTMP = instance_manager_->hasRTMPOutput(instanceId);

    if (hasRTMP) {
      output["type"] = "RTMP_STREAM";
      std::string rtmpUrlVal;
      if (!info.rtmpUrl.empty()) {
        rtmpUrlVal = info.rtmpUrl;
      } else if (info.additionalParams.find("RTMP_DES_URL") !=
                 info.additionalParams.end()) {
        rtmpUrlVal = info.additionalParams.at("RTMP_DES_URL");
      } else if (info.additionalParams.find("RTMP_URL") !=
                 info.additionalParams.end()) {
        rtmpUrlVal = info.additionalParams.at("RTMP_URL");
      }
      output["rtmpUrl"] = rtmpUrlVal;
      // RTMP node (SDK) automatically adds "_0" to stream key when publishing.
      // Playback URL for viewing must use the same key (e.g. .../live/stream_0).
      if (!rtmpUrlVal.empty()) {
        size_t lastSlash = rtmpUrlVal.find_last_of('/');
        if (lastSlash != std::string::npos && lastSlash + 1 < rtmpUrlVal.size()) {
          std::string streamKey = rtmpUrlVal.substr(lastSlash + 1);
          if (streamKey.size() >= 2 && streamKey.substr(streamKey.size() - 2) != "_0") {
            output["rtmpPlaybackUrl"] = rtmpUrlVal + "_0";
          } else {
            output["rtmpPlaybackUrl"] = rtmpUrlVal;
          }
        } else {
          output["rtmpPlaybackUrl"] = rtmpUrlVal;
        }
      }
      if (!info.rtspUrl.empty()) {
        output["rtspUrl"] = info.rtspUrl;
      } else if (info.additionalParams.find("RTSP_DES_URL") !=
                 info.additionalParams.end()) {
        output["rtspUrl"] = info.additionalParams.at("RTSP_DES_URL");
      } else if (info.additionalParams.find("RTSP_URL") !=
                 info.additionalParams.end()) {
        // RTSP_URL can be used for output if RTSP_DES_URL is not provided
        output["rtspUrl"] = info.additionalParams.at("RTSP_URL");
      }
    } else {
      output["type"] = "FILE";
      // Get file output information
      Json::Value fileInfo = getOutputFileInfo(instanceId);
      output["files"] = fileInfo;
    }
    response["output"] = output;

    // Detection settings
    Json::Value detection;
    detection["sensitivity"] = info.detectionSensitivity;
    detection["mode"] = info.detectorMode;
    detection["movementSensitivity"] = info.movementSensitivity;
    detection["sensorModality"] = info.sensorModality;
    response["detection"] = detection;

    // Processing modes
    Json::Value modes;
    modes["statisticsMode"] = info.statisticsMode;
    modes["metadataMode"] = info.metadataMode;
    modes["debugMode"] = info.debugMode;
    modes["diagnosticsMode"] = info.diagnosticsMode;
    response["modes"] = modes;

    // Status summary
    Json::Value status;
    status["running"] = info.running;
    status["processing"] = (info.running && info.fps > 0);
    if (info.running) {
      if (info.fps > 0) {
        status["message"] = "Instance is running and processing frames";
      } else {
        status["message"] = "Instance is running but not processing frames "
                            "(may be initializing)";
      }
    } else {
      status["message"] = "Instance is stopped";
    }
    response["status"] = status;

    std::cerr
        << "[InstanceHandler] Response built successfully, sending callback..."
        << std::endl;
    callback(createSuccessResponse(response));
    std::cerr << "[InstanceHandler] getInstanceOutput completed successfully"
              << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "[InstanceHandler] Exception in getInstanceOutput: "
              << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    std::cerr << "[InstanceHandler] Unknown exception in getInstanceOutput"
              << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

Json::Value
InstanceHandler::getOutputFileInfo(const std::string &instanceId) const {
  Json::Value fileInfo;

  // Check common output directories
  std::vector<std::string> outputDirs = {
      "./output/" + instanceId, "./build/output/" + instanceId,
      "output/" + instanceId, "build/output/" + instanceId};

  fs::path outputDir;
  bool found = false;

  for (const auto &dir : outputDirs) {
    fs::path testPath(dir);
    if (fs::exists(testPath) && fs::is_directory(testPath)) {
      outputDir = testPath;
      found = true;
      break;
    }
  }

  if (!found) {
    fileInfo["exists"] = false;
    fileInfo["message"] = "Output directory not found";
    fileInfo["expectedPaths"] = Json::arrayValue;
    for (const auto &dir : outputDirs) {
      fileInfo["expectedPaths"].append(dir);
    }
    return fileInfo;
  }

  fileInfo["exists"] = true;
  fileInfo["directory"] = outputDir.string();

  // Count files - OPTIMIZED: Single pass through directory
  int fileCount = 0;
  int totalSize = 0;
  std::string latestFile;
  std::time_t latestTime = 0;
  int recentFileCount = 0;
  auto now = std::chrono::system_clock::now();

  try {
    // Single iteration to collect all file information
    for (const auto &entry : fs::directory_iterator(outputDir)) {
      if (fs::is_regular_file(entry)) {
        fileCount++;

        try {
          // Get file size
          auto fileSize = fs::file_size(entry);
          totalSize += static_cast<int>(fileSize);

          // Get modification time
          auto fileTime = fs::last_write_time(entry);
          // Convert file_time_type to system_clock::time_point
          auto fileTimeDuration = fileTime.time_since_epoch();
          auto systemTimeDuration =
              std::chrono::duration_cast<std::chrono::system_clock::duration>(
                  fileTimeDuration);
          auto systemTimePoint =
              std::chrono::system_clock::time_point(systemTimeDuration);
          auto timeT = std::chrono::system_clock::to_time_t(systemTimePoint);

          // Check if this is the latest file
          if (timeT > latestTime) {
            latestTime = timeT;
            latestFile = entry.path().filename().string();
          }

          // Check if file was created recently (within last minute)
          auto age = std::chrono::duration_cast<std::chrono::seconds>(
                         now - systemTimePoint)
                         .count();
          if (age < 60) {
            recentFileCount++;
          }
        } catch (const std::exception &e) {
          // Skip this file if we can't read its metadata
          // Continue with next file
        }
      }
    }
  } catch (const std::exception &e) {
    fileInfo["error"] = std::string("Error reading directory: ") + e.what();
  }

  fileInfo["fileCount"] = fileCount;
  fileInfo["totalSizeBytes"] = totalSize;

  // Format total size
  std::string sizeStr;
  if (totalSize < 1024) {
    sizeStr = std::to_string(totalSize) + " B";
  } else if (totalSize < 1024 * 1024) {
    sizeStr = std::to_string(totalSize / 1024) + " KB";
  } else {
    sizeStr = std::to_string(totalSize / (1024 * 1024)) + " MB";
  }
  fileInfo["totalSize"] = sizeStr;

  if (!latestFile.empty()) {
    fileInfo["latestFile"] = latestFile;

    // Format latest file time
    std::stringstream ss;
    ss << std::put_time(std::localtime(&latestTime), "%Y-%m-%d %H:%M:%S");
    fileInfo["latestFileTime"] = ss.str();
  }

  fileInfo["recentFileCount"] = recentFileCount;
  fileInfo["isActive"] = (recentFileCount > 0);

  return fileInfo;
}

