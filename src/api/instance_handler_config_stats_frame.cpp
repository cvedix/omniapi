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

void InstanceHandler::getConfig(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  // Get instance ID from path parameter
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
              << "/config - Get instance config";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                   << "/config - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/{id}/config - Error: "
                        "Instance ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Get instance config
    Json::Value config = instance_manager_->getInstanceConfig(instanceId);

    // Check if config is empty (instance not found)
    if (config.empty() || !config.isMember("InstanceId")) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId
                     << "/config - Not found - " << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
                << "/config - Success - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(config));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << "/config - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << "/config - Unknown exception - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void InstanceHandler::getStatistics(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  // Get instance ID from path parameter
  std::string instanceId = extractInstanceId(req);

  std::cout << "[InstanceHandler] ===== GET_STATISTICS API CALL START ====="
            << std::endl;
  std::cout << "[InstanceHandler] Instance ID: " << instanceId << std::endl;
  std::cout << "[InstanceHandler] Request from: "
            << req->getPeerAddr().toIpPort() << std::endl;

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
              << "/statistics - Get instance statistics";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                   << "/statistics - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/{instanceId}/statistics - "
                        "Error: Instance ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Get statistics with timeout protection
    // CRITICAL: getInstanceStatistics() can block for up to 5 seconds (IPC
    // timeout) We need async + timeout to prevent API from hanging
    std::cout << "[InstanceHandler] Calling "
                 "instance_manager_->getInstanceStatistics() async..."
              << std::endl;
    std::optional<InstanceStatistics> optStats;
    try {
      std::cout << "[InstanceHandler] Creating async future..." << std::endl;
      auto future = std::async(
          std::launch::async,
          [this, instanceId]() -> std::optional<InstanceStatistics> {
            std::cout << "[InstanceHandler] [ASYNC THREAD] ===== ASYNC THREAD "
                         "STARTED ====="
                      << std::endl;
            std::cout << "[InstanceHandler] [ASYNC THREAD] Thread ID: "
                      << std::this_thread::get_id() << std::endl;
            std::cout << "[InstanceHandler] [ASYNC THREAD] Starting "
                         "getInstanceStatistics() for "
                      << instanceId << std::endl;
            try {
              if (!instance_manager_) {
                std::cerr << "[InstanceHandler] [ASYNC THREAD] ERROR: "
                             "instance_manager_ is null!"
                          << std::endl;
                return std::nullopt;
              }
              std::cout << "[InstanceHandler] [ASYNC THREAD] instance_manager_ "
                           "is valid, calling getInstanceStatistics()..."
                        << std::endl;
              std::cout << "[InstanceHandler] [ASYNC THREAD] About to call: "
                           "instance_manager_->getInstanceStatistics("
                        << instanceId << ")" << std::endl;
              std::cout
                  << "[InstanceHandler] [ASYNC THREAD] instance_manager_ type: "
                  << typeid(*instance_manager_).name() << std::endl;

              // Flush output to ensure logs appear
              std::cout.flush();
              std::cerr.flush();

              std::cout << "[InstanceHandler] [ASYNC THREAD] Calling "
                           "getInstanceStatistics() NOW..."
                        << std::endl;
              std::cout.flush();

              // Wrap in try-catch to catch any exception during function call
              std::optional<InstanceStatistics> result;

              // Add a marker right before the call to see if we reach here
              std::cout << "[InstanceHandler] [ASYNC THREAD] ===== ABOUT TO "
                           "CALL getInstanceStatistics() ===== "
                        << std::endl;
              std::cout.flush();

              // Force a small delay to ensure log appears
              std::this_thread::sleep_for(std::chrono::milliseconds(10));

              try {
                std::cout << "[InstanceHandler] [ASYNC THREAD] Entering try "
                             "block, calling getInstanceStatistics()..."
                          << std::endl;
                std::cout.flush();

                auto call_start = std::chrono::steady_clock::now();
                std::cout << "[InstanceHandler] [ASYNC THREAD] Call start time "
                             "recorded, making virtual function call..."
                          << std::endl;
                std::cout.flush();

                // This is the actual call - if it blocks, we won't see logs
                // after this
                result = instance_manager_->getInstanceStatistics(instanceId);

                auto call_end = std::chrono::steady_clock::now();
                auto call_duration =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        call_end - call_start)
                        .count();
                std::cout << "[InstanceHandler] [ASYNC THREAD] "
                             "getInstanceStatistics() returned after "
                          << call_duration << "ms!" << std::endl;
                std::cout.flush();
              } catch (const std::exception &e) {
                std::cerr << "[InstanceHandler] [ASYNC THREAD] EXCEPTION "
                             "during getInstanceStatistics() call: "
                          << e.what() << std::endl;
                std::cerr << "[InstanceHandler] [ASYNC THREAD] Exception type: "
                          << typeid(e).name() << std::endl;
                std::cerr.flush();
                return std::nullopt;
              } catch (...) {
                std::cerr << "[InstanceHandler] [ASYNC THREAD] UNKNOWN "
                             "EXCEPTION during getInstanceStatistics() call"
                          << std::endl;
                std::cerr.flush();
                return std::nullopt;
              }

              std::cout << "[InstanceHandler] [ASYNC THREAD] Result has_value: "
                        << result.has_value() << std::endl;
              std::cout << "[InstanceHandler] [ASYNC THREAD] ===== ASYNC "
                           "THREAD COMPLETED ====="
                        << std::endl;
              return result;
            } catch (const std::exception &e) {
              std::cerr << "[InstanceHandler] [ASYNC THREAD] EXCEPTION in "
                           "getInstanceStatistics: "
                        << e.what() << std::endl;
              std::cerr << "[InstanceHandler] [ASYNC THREAD] Exception type: "
                        << typeid(e).name() << std::endl;
              return std::nullopt;
            } catch (...) {
              std::cerr << "[InstanceHandler] [ASYNC THREAD] UNKNOWN EXCEPTION "
                           "in getInstanceStatistics"
                        << std::endl;
              return std::nullopt;
            }
          });
      std::cout
          << "[InstanceHandler] Async future created, waiting for result..."
          << std::endl;

      // Wait with timeout: IPC_API_TIMEOUT_MS (default 5s) + 500ms buffer
      auto timeoutMs = TimeoutConstants::getIpcApiTimeoutMs() + 500;
      auto timeout = std::chrono::milliseconds(timeoutMs);
      std::cout << "[InstanceHandler] Waiting for async result with timeout: "
                << timeoutMs << "ms" << std::endl;
      auto status = future.wait_for(timeout);
      std::cout << "[InstanceHandler] Future status: "
                << (status == std::future_status::ready     ? "READY"
                    : status == std::future_status::timeout ? "TIMEOUT"
                                                            : "DEFERRED")
                << std::endl;
      if (status == std::future_status::timeout) {
        std::cerr << "[InstanceHandler] TIMEOUT waiting for "
                     "getInstanceStatistics() after "
                  << timeoutMs << "ms" << std::endl;
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId
                       << "/statistics - Timeout getting statistics ("
                       << timeoutMs << "ms)";
        }
        std::cerr
            << "[InstanceHandler] WARNING: getInstanceStatistics() timeout "
               "after "
            << timeoutMs << "ms - worker may be busy or hung" << std::endl;
        callback(createErrorResponse(504, "Gateway Timeout",
                                     "Statistics request timed out. Worker may "
                                     "be busy. Please try again later."));
        return;
      } else if (status == std::future_status::ready) {
        try {
          std::cout << "[InstanceHandler] Getting result from future..."
                    << std::endl;
          optStats = future.get();
          std::cout << "[InstanceHandler] Got result, has_value: "
                    << optStats.has_value() << std::endl;
        } catch (const std::exception &e) {
          std::cerr << "[InstanceHandler] Exception getting future result: "
                    << e.what() << std::endl;
          callback(createErrorResponse(500, "Internal server error",
                                       "Failed to get statistics"));
          return;
        } catch (...) {
          std::cerr
              << "[InstanceHandler] Unknown exception getting future result"
              << std::endl;
          callback(createErrorResponse(500, "Internal server error",
                                       "Failed to get statistics"));
          return;
        }
      }
    } catch (...) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to get statistics"));
      return;
    }

    if (!optStats.has_value()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId
                     << "/statistics - Not found or not running - "
                     << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found or not running: " +
                                       instanceId));
      return;
    }

    // Build JSON response
    Json::Value response = optStats.value().toJson();

    // Log response content for debugging
    std::cout << "[InstanceHandler] Statistics response JSON: "
              << response.toStyledString() << std::endl;
    std::cout << "[InstanceHandler] Statistics response - frames_processed: "
              << response["frames_processed"].asInt64()
              << ", frames_incoming: " << response["frames_incoming"].asInt64()
              << ", dropped_frames: "
              << response["dropped_frames_count"].asInt64()
              << ", current_fps: " << response["current_framerate"].asDouble()
              << ", source_fps: " << response["source_framerate"].asDouble()
              << ", queue_size: " << response["input_queue_size"].asInt64()
              << ", start_time: " << response["start_time"].asInt64()
              << std::endl;
    std::cout.flush();

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
                << "/statistics - Success - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << "/statistics - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    std::cerr << "[InstanceHandler] Exception in getStatistics: " << e.what()
              << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << "/statistics - Unknown exception - " << duration.count()
                 << "ms";
    }
    std::cerr << "[InstanceHandler] Unknown exception in getStatistics"
              << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void InstanceHandler::getLastFrame(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  // Get instance ID from path parameter
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
              << "/frame - Get last frame";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                   << "/frame - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/{instanceId}/frame - "
                        "Error: Instance ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
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
                     << "/frame - Instance not found - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance ID not found: " + instanceId));
      return;
    }

    const InstanceInfo &info = optInfo.value();

    // DEBUG: Log instance state before getting frame
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] GET /v1/core/instance/" << instanceId
                 << "/frame - Instance state: running=" << info.running
                 << ", loaded=" << info.loaded;
    }

    // Get last frame with timeout protection
    // CRITICAL: getLastFrame() can block for up to 5 seconds (IPC timeout)
    // We need async + timeout to prevent API from hanging in production
    // when there are concurrent requests
    std::string frameBase64;
    try {
      auto future = std::async(
          std::launch::async,
          [this, instanceId]() -> std::string {
            try {
              if (!instance_manager_) {
                std::cerr << "[InstanceHandler] [ASYNC THREAD] ERROR: "
                             "instance_manager_ is null!"
                          << std::endl;
                return "";
              }
              return instance_manager_->getLastFrame(instanceId);
            } catch (const std::exception &e) {
              std::cerr << "[InstanceHandler] [ASYNC THREAD] EXCEPTION in "
                           "getLastFrame: "
                        << e.what() << std::endl;
              return "";
            } catch (...) {
              std::cerr << "[InstanceHandler] [ASYNC THREAD] UNKNOWN EXCEPTION "
                           "in getLastFrame"
                        << std::endl;
              return "";
            }
          });

      // Wait with timeout: IPC_API_TIMEOUT_MS (default 5s) + 500ms buffer
      auto timeoutMs = TimeoutConstants::getIpcApiTimeoutMs() + 500;
      auto timeout = std::chrono::milliseconds(timeoutMs);
      auto status = future.wait_for(timeout);
      if (status == std::future_status::timeout) {
        std::cerr << "[InstanceHandler] TIMEOUT waiting for getLastFrame() after "
                  << timeoutMs << "ms" << std::endl;
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId
                       << "/frame - Timeout getting frame (" << timeoutMs << "ms)";
        }
        callback(createErrorResponse(504, "Gateway Timeout",
                                     "Frame request timed out. Worker may "
                                     "be busy. Please try again later."));
        return;
      } else if (status == std::future_status::ready) {
        try {
          frameBase64 = future.get();
        } catch (const std::exception &e) {
          std::cerr << "[InstanceHandler] Exception getting future result: "
                    << e.what() << std::endl;
          callback(createErrorResponse(500, "Internal server error",
                                       "Failed to get frame"));
          return;
        } catch (...) {
          std::cerr << "[InstanceHandler] Unknown exception getting future result"
                    << std::endl;
          callback(createErrorResponse(500, "Internal server error",
                                       "Failed to get frame"));
          return;
        }
      }
    } catch (...) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to get frame"));
      return;
    }

    // DEBUG: Log frame retrieval result
    if (isApiLoggingEnabled()) {
      if (frameBase64.empty()) {
        PLOG_DEBUG << "[API] GET /v1/core/instance/" << instanceId
                   << "/frame - No frame cached (empty result)";
      } else {
        PLOG_DEBUG << "[API] GET /v1/core/instance/" << instanceId
                   << "/frame - Frame retrieved: size=" << frameBase64.length()
                   << " chars (base64), estimated image size="
                   << (frameBase64.length() * 3 / 4) << " bytes";
      }
    }

    // Build JSON response
    Json::Value response;
    response["frame"] = frameBase64;
    response["running"] = info.running;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
                << "/frame - Success - " << duration.count()
                << "ms (frame size: " << frameBase64.length() << " chars)";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR
          << "[API] GET /v1/core/instance/{instanceId}/frame - Exception: "
          << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/{instanceId}/frame - Unknown "
                    "exception - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

