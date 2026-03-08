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

void InstanceHandler::pushCompressedFrame(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);
  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/instance/{instanceId}/push/compressed - Push compressed frame";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/push/compressed - "
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
        PLOG_WARNING << "[API] POST /v1/core/instance/{instanceId}/push/compressed - "
                        "Error: Instance ID is empty";
      }
      callback(createErrorResponse(400, "Bad request", "Instance ID is required"));
      return;
    }

    // Check if instance exists
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/push/compressed - Instance not found";
      }
      callback(createErrorResponse(404, "Instance not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    const InstanceInfo& info = optInfo.value();

    // Check if instance is running
    if (!info.running) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/push/compressed - Instance is not running";
      }
      callback(createErrorResponse(409, "Instance is not currently running",
                                   "Instance must be running to push frames"));
      return;
    }

    // Parse multipart/form-data
    std::string contentType = req->getHeader("Content-Type");
    if (contentType.find("multipart/form-data") == std::string::npos) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/push/compressed - Invalid content type";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Content-Type must be multipart/form-data"));
      return;
    }

    // Extract boundary
    std::string boundary;
    size_t boundaryPos = contentType.find("boundary=");
    if (boundaryPos != std::string::npos) {
      boundaryPos += 9;
      size_t endPos = contentType.find_first_of("; \r\n", boundaryPos);
      if (endPos != std::string::npos) {
        boundary = contentType.substr(boundaryPos, endPos - boundaryPos);
      } else {
        boundary = contentType.substr(boundaryPos);
      }
      if (!boundary.empty() && boundary.front() == '"' && boundary.back() == '"') {
        boundary = boundary.substr(1, boundary.length() - 2);
      }
    }

    if (boundary.empty()) {
      callback(createErrorResponse(400, "Bad request",
                                   "Could not find boundary in Content-Type"));
      return;
    }

    // Parse multipart body
    auto body = req->getBody();
    if (body.empty()) {
      callback(createErrorResponse(400, "Bad request", "Request body is empty"));
      return;
    }

    std::string bodyStr(reinterpret_cast<const char*>(body.data()), body.size());
    std::string boundaryMarker = "--" + boundary;

    // Extract frame data and timestamp
    std::vector<uint8_t> frameData;
    int64_t timestamp = 0;

    // Find frame field
    size_t partStart = bodyStr.find(boundaryMarker);
    while (partStart != std::string::npos) {
      size_t contentDispositionPos = bodyStr.find("Content-Disposition:", partStart);
      if (contentDispositionPos == std::string::npos || contentDispositionPos > partStart + 1024) {
        partStart = bodyStr.find(boundaryMarker, partStart + 1);
        continue;
      }

      // Check if this is the "frame" field
      size_t frameFieldPos = bodyStr.find("name=\"frame\"", contentDispositionPos);
      if (frameFieldPos == std::string::npos) {
        frameFieldPos = bodyStr.find("name='frame'", contentDispositionPos);
      }
      if (frameFieldPos == std::string::npos) {
        frameFieldPos = bodyStr.find("name=frame", contentDispositionPos);
      }

      if (frameFieldPos != std::string::npos && frameFieldPos < contentDispositionPos + 512) {
        // Found frame field, extract data
        size_t contentStart = bodyStr.find("\r\n\r\n", contentDispositionPos);
        if (contentStart == std::string::npos) {
          contentStart = bodyStr.find("\n\n", contentDispositionPos);
        }
        if (contentStart != std::string::npos) {
          contentStart += 2;
          if (contentStart < bodyStr.length() && (bodyStr[contentStart] == '\r' || bodyStr[contentStart] == '\n')) {
            contentStart++;
          }
          while (contentStart < bodyStr.length() && 
                 (bodyStr[contentStart] == '\r' || bodyStr[contentStart] == '\n' || 
                  bodyStr[contentStart] == ' ' || bodyStr[contentStart] == '\t')) {
            contentStart++;
          }
          size_t nextBoundary = bodyStr.find(boundaryMarker, contentStart);
          size_t contentEnd = (nextBoundary != std::string::npos) ? nextBoundary : bodyStr.length();
          while (contentEnd > contentStart && 
                 (bodyStr[contentEnd - 1] == '\r' || bodyStr[contentEnd - 1] == '\n')) {
            contentEnd--;
          }
          if (contentEnd > contentStart) {
            frameData.assign(body.begin() + contentStart, body.begin() + contentEnd);
          }
        }
        break;
      }

      // Check if this is the "timestamp" field
      size_t timestampFieldPos = bodyStr.find("name=\"timestamp\"", contentDispositionPos);
      if (timestampFieldPos == std::string::npos) {
        timestampFieldPos = bodyStr.find("name='timestamp'", contentDispositionPos);
      }
      if (timestampFieldPos == std::string::npos) {
        timestampFieldPos = bodyStr.find("name=timestamp", contentDispositionPos);
      }

      if (timestampFieldPos != std::string::npos && timestampFieldPos < contentDispositionPos + 512) {
        // Found timestamp field, extract value
        size_t contentStart = bodyStr.find("\r\n\r\n", contentDispositionPos);
        if (contentStart == std::string::npos) {
          contentStart = bodyStr.find("\n\n", contentDispositionPos);
        }
        if (contentStart != std::string::npos) {
          contentStart += 2;
          while (contentStart < bodyStr.length() && 
                 (bodyStr[contentStart] == '\r' || bodyStr[contentStart] == '\n' || 
                  bodyStr[contentStart] == ' ' || bodyStr[contentStart] == '\t')) {
            contentStart++;
          }
          size_t nextBoundary = bodyStr.find(boundaryMarker, contentStart);
          size_t contentEnd = (nextBoundary != std::string::npos) ? nextBoundary : bodyStr.length();
          while (contentEnd > contentStart && 
                 (bodyStr[contentEnd - 1] == '\r' || bodyStr[contentEnd - 1] == '\n')) {
            contentEnd--;
          }
          if (contentEnd > contentStart) {
            std::string timestampStr = bodyStr.substr(contentStart, contentEnd - contentStart);
            try {
              timestamp = std::stoll(timestampStr);
            } catch (...) {
              // Use current timestamp if parsing fails
              timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
            }
          }
        }
      }

      partStart = bodyStr.find(boundaryMarker, partStart + 1);
    }

    if (frameData.empty()) {
      callback(createErrorResponse(400, "Bad request",
                                   "Frame data is required in multipart field 'frame'"));
      return;
    }

    // Use current timestamp if not provided
    if (timestamp == 0) {
      timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Subprocess: send frame via IPC PUSH_FRAME (worker decodes and pushes to app_src)
    if (instance_manager_->isSubprocessMode()) {
      auto *sub = dynamic_cast<SubprocessInstanceManager *>(instance_manager_);
      if (sub) {
        std::string frameBase64 = base64Encode(frameData.data(), frameData.size());
        std::string codec = "jpeg";
        if (frameData.size() >= 8 && frameData[0] == 0x89 && frameData[1] == 0x50 &&
            frameData[2] == 0x4E && frameData[3] == 0x47)
          codec = "png";
        else if (frameData.size() >= 3 && frameData[0] == 0xFF && frameData[1] == 0xD8)
          codec = "jpeg";
        if (sub->pushFrame(instanceId, frameBase64, codec)) {
          auto resp = HttpResponse::newHttpResponse();
          resp->setStatusCode(k204NoContent);
          resp->addHeader("Access-Control-Allow-Origin", "*");
          resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
          resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
          MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
          return;
        }
      }
      callback(createErrorResponse(500, "Internal Server Error",
                                   "Failed to push frame to worker"));
      return;
    }

    // Lightweight validation only (magic bytes). Decode happens once in
    // FrameProcessor to avoid double decode and reduce lag (samples decode
    // once; API was decoding here then again in FrameProcessor).
    auto validMagic = [](const std::vector<uint8_t>& d) {
      if (d.size() < 8u) return false;
      // JPEG: FF D8 FF
      if (d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return true;
      // PNG: 89 50 4E 47 0D 0A 1A 0A
      if (d[0] == 0x89 && d[1] == 0x50 && d[2] == 0x4E && d[3] == 0x47 &&
          d[4] == 0x0D && d[5] == 0x0A && d[6] == 0x1A && d[7] == 0x0A) return true;
      return false;
    };
    if (!validMagic(frameData)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                   << "/push/compressed - Invalid image magic (expected JPEG/PNG)";
      }
      callback(createErrorResponse(400, "Bad Request",
                                   "Invalid compressed image (expected JPEG or PNG)"));
      return;
    }

    // Backpressure: reject when queue is near full so client can reduce rate
    auto& queueManager = FrameInputQueueManager::getInstance();
    auto& queue = queueManager.getQueue(instanceId);
    size_t maxSz = queue.getMaxSize();
    if (maxSz > 0 && queue.size() >= static_cast<size_t>(maxSz * 4 / 5)) {
      auto resp = createErrorResponse(503, "Service Unavailable",
          "Frame queue near full (backpressure). Reduce push rate or retry later.");
      resp->addHeader("Retry-After", "1");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    FrameData frameDataObj(FrameType::COMPRESSED, "", frameData, timestamp);
    if (!queue.push(frameDataObj)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/push/compressed - Queue is full";
      }
      callback(createErrorResponse(500, "Internal Server Error",
                                   "Frame queue is full"));
      return;
    }

    // FrameProcessor thread will pop, decode once, and push to app_src node.

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                << "/push/compressed - Success - " 
                << duration.count() << "ms";
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception& e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/push/compressed - "
                    "Exception: " << e.what();
    }
    auto resp = createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (...) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/push/compressed - "
                    "Unknown exception";
    }
    auto resp = createErrorResponse(500, "Internal server error",
                                   "Unknown error occurred");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}
