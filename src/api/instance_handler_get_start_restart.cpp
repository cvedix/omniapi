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

void InstanceHandler::getInstance(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  // Get instance ID from path parameter
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
              << " - Get instance details";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                   << " - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/{id} - Error: Instance "
                        "ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Get instance info with timeout protection
    std::optional<InstanceInfo> optInfo;
    try {
      auto future =
          std::async(std::launch::async,
                     [this, instanceId]() -> std::optional<InstanceInfo> {
                       try {
                         if (instance_manager_) {
                           return instance_manager_->getInstance(instanceId);
                         }
                         return std::nullopt;
                       } catch (...) {
                         return std::nullopt;
                       }
                     });

      // Wait with timeout (configurable, default: registry timeout + 500ms
      // buffer) This ensures API wrapper timeout >= registry timeout to allow
      // registry to complete or timeout first
      auto timeout = TimeoutConstants::getApiWrapperTimeout();
      auto status = future.wait_for(timeout);
      if (status == std::future_status::timeout) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId
                       << " - Timeout getting instance (" << timeout.count()
                       << "ms)";
        }
        callback(createErrorResponse(
            503, "Service Unavailable",
            "Instance registry is busy. Please try again later."));
        return;
      } else if (status == std::future_status::ready) {
        try {
          optInfo = future.get();
        } catch (...) {
          callback(createErrorResponse(500, "Internal server error",
                                       "Failed to get instance"));
          return;
        }
      }
    } catch (...) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to get instance"));
      return;
    }

    if (!optInfo.has_value()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId
                     << " - Not found - " << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Build response
    Json::Value response = instanceInfoToJson(optInfo.value());
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      const auto &info = optInfo.value();
      PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
                << " - Success: " << info.displayName
                << " (running: " << (info.running ? "true" : "false")
                << ", fps: " << std::fixed << std::setprecision(2) << info.fps
                << ") - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << " - Exception: " << e.what() << " - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << " - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void InstanceHandler::startInstance(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  // Get instance ID from path parameter
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
              << "/start - Start instance";
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                   << "/start - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/{id}/start - Error: "
                        "Instance ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Check if instance is already running (per OpenAPI spec: "If the instance
    // is already running, the request will succeed without error")
    std::optional<InstanceInfo> optInfoCheck;
    try {
      optInfoCheck = instance_manager_->getInstance(instanceId);
      if (optInfoCheck.has_value() && optInfoCheck.value().running) {
        // Instance is already running, return success immediately
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                    << "/start - Instance already running, returning success";
        }
        Json::Value response = instanceInfoToJson(optInfoCheck.value());
        response["message"] = "Instance is already running";
        response["status"] = "running";
        callback(createSuccessResponse(response));
        return;
      }
    } catch (...) {
      // If we can't check status, continue with normal start flow
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/start - Could not check instance status, proceeding "
                        "with start";
      }
    }

    // Return 202 Accepted immediately; start runs in background. Client polls GET /v1/core/instance/{id}
    Json::Value response202;
    response202["instanceId"] = instanceId;
    response202["status"] = "starting";
    response202["message"] = "Instance start accepted. Poll GET /v1/core/instance/" + instanceId + " for status.";
    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                << "/start - Returning 202 Accepted (start in background)";
    }
    callback(createSuccessResponse(response202, 202));

    // Run start in background (detached thread)
    IInstanceManager *mgr = instance_manager_;
    std::thread([mgr, instanceId]() {
      try {
        if (!mgr) return;
        bool ok = mgr->startInstance(instanceId);
        if (ok) {
          auto &frameProcessor = FrameProcessor::getInstance();
          frameProcessor.startProcessing(instanceId, mgr);
          if (isApiLoggingEnabled()) {
            PLOG_INFO << "[API] [Background] Instance " << instanceId << " started successfully";
          }
        } else {
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] [Background] Instance " << instanceId << " start failed";
          }
        }
      } catch (const std::exception &e) {
        std::cerr << "[InstanceHandler] Background start exception " << instanceId << ": " << e.what() << std::endl;
      } catch (...) {
        std::cerr << "[InstanceHandler] Unknown exception starting instance " << instanceId << std::endl;
      }
    }).detach();
    return;

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                 << "/start - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    std::cerr << "[InstanceHandler] Exception: " << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                 << "/start - Unknown exception - " << duration.count() << "ms";
    }
    std::cerr << "[InstanceHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void InstanceHandler::stopInstance(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  // Get instance ID from path parameter
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
              << "/stop - Stop instance";
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                   << "/stop - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/{id}/stop - Error: "
                        "Instance ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Stop frame processor for this instance
    auto& frameProcessor = FrameProcessor::getInstance();
    frameProcessor.stopProcessing(instanceId);
    
    // OPTIMIZED: Run stopInstance() in detached thread to avoid blocking API
    // thread and other instances This allows multiple instances to stop
    // concurrently without blocking each other The instance will stop in
    // background, and status can be checked via GET /v1/core/instance/{id}
    std::thread stopThread([this, instanceId]() {
      try {
        instance_manager_->stopInstance(instanceId);
      } catch (const std::exception &e) {
        std::cerr << "[InstanceHandler] Exception stopping instance "
                  << instanceId << ": " << e.what() << std::endl;
      } catch (...) {
        std::cerr << "[InstanceHandler] Unknown exception stopping instance "
                  << instanceId << std::endl;
      }
    });
    stopThread.detach(); // Detach thread - instance stops in background

    // Return immediately - instance is stopping in background
    // Client can check status using GET /v1/core/instance/{id}
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (optInfo.has_value()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                  << "/stop - Accepted (async) - " << duration.count() << "ms";
      }
      Json::Value response = instanceInfoToJson(optInfo.value());
      response["message"] =
          "Instance stop request accepted. Instance is stopping in background. "
          "Check status using GET /v1/core/instance/" +
          instanceId;
      response["status"] = "stopping"; // Indicate that instance is stopping
      callback(createSuccessResponse(
          response, 202)); // 202 Accepted - request accepted but not completed
    } else {
      callback(createErrorResponse(404, "Not found", "Instance not found"));
    }
    return;

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                 << "/stop - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    std::cerr << "[InstanceHandler] Exception: " << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                 << "/stop - Unknown exception - " << duration.count() << "ms";
    }
    std::cerr << "[InstanceHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void InstanceHandler::restartInstance(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  try {
    // Check if registry is set
    if (!instance_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    // Get instance ID from path parameter
    std::string instanceId = extractInstanceId(req);

    if (instanceId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // OPTIMIZED: Run restartInstance() in detached thread to avoid blocking API
    // thread and other instances This allows multiple instances to restart
    // concurrently without blocking each other The instance will restart in
    // background, and status can be checked via GET /v1/core/instance/{id}
    std::thread restartThread([this, instanceId]() {
      try {
        // First, stop the instance if it's running
        auto optInfo = instance_manager_->getInstance(instanceId);
        if (optInfo.has_value() && optInfo.value().running) {
          instance_manager_->stopInstance(instanceId);
          // Give it a moment to fully stop and cleanup
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        // Now start the instance (skip auto-stop since we already stopped it)
        instance_manager_->startInstance(instanceId,
                                         true); // true = skipAutoStop
      } catch (const std::exception &e) {
        std::cerr << "[InstanceHandler] Exception restarting instance "
                  << instanceId << ": " << e.what() << std::endl;
      } catch (...) {
        std::cerr << "[InstanceHandler] Unknown exception restarting instance "
                  << instanceId << std::endl;
      }
    });
    restartThread.detach(); // Detach thread - instance restarts in background

    // Return immediately - instance is restarting in background
    // Client can check status using GET /v1/core/instance/{id}
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (optInfo.has_value()) {
      Json::Value response = instanceInfoToJson(optInfo.value());
      response["message"] = "Instance restart request accepted. Instance is "
                            "restarting in background. "
                            "Check status using GET /v1/core/instance/" +
                            instanceId;
      response["status"] = "restarting"; // Indicate that instance is restarting
      callback(createSuccessResponse(
          response, 202)); // 202 Accepted - request accepted but not completed
    } else {
      callback(createErrorResponse(404, "Not found", "Instance not found"));
    }
    return;

  } catch (const std::exception &e) {
    std::cerr << "[InstanceHandler] Exception: " << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    std::cerr << "[InstanceHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

