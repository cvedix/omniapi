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

IInstanceManager *InstanceHandler::instance_manager_ = nullptr;

void InstanceHandler::setInstanceManager(IInstanceManager *manager) {
  instance_manager_ = manager;
}

std::string
InstanceHandler::extractInstanceId(const HttpRequestPtr &req) const {
  // Try getParameter first (standard way)
  std::string instanceId = req->getParameter("instanceId");

  // Fallback: extract from path if getParameter doesn't work
  if (instanceId.empty()) {
    std::string path = req->getPath();
    // Try /instances/ pattern first
    size_t instancesPos = path.find("/instances/");
    if (instancesPos != std::string::npos) {
      size_t start = instancesPos + 11; // length of "/instances/"
      size_t end = path.find("/", start);
      if (end == std::string::npos) {
        end = path.length();
      }
      instanceId = path.substr(start, end - start);
    } else {
      // Try /instance/ pattern (singular)
      size_t instancePos = path.find("/instance/");
      if (instancePos != std::string::npos) {
        size_t start = instancePos + 10; // length of "/instance/"
        size_t end = path.find("/", start);
        if (end == std::string::npos) {
          end = path.length();
        }
        instanceId = path.substr(start, end - start);
      }
    }
  }

  return instanceId;
}

HttpResponsePtr InstanceHandler::createSuccessResponse(const Json::Value &data,
                                                       int statusCode) const {
  auto resp = HttpResponse::newHttpJsonResponse(data);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, PUT, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");
  return resp;
}

void InstanceHandler::getStatusSummary(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled() && req) {
    PLOG_INFO << "[API] GET /v1/core/instance/status/summary - Get instance "
                 "status summary";
    try {
      PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
    } catch (...) {
      // Ignore errors getting peer address
    }
  }

  try {
    // Check if registry is set and capture pointer locally to avoid race conditions
    IInstanceManager *manager = instance_manager_;
    if (!manager) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/instance/status/summary - Error: "
                      "Instance registry not initialized";
      }
      try {
        auto errorResp = createErrorResponse(500, "Internal server error",
                                            "Instance registry not initialized");
        if (errorResp) {
          callback(errorResp);
        }
      } catch (const std::exception &e) {
        std::cerr << "[InstanceHandler] Exception creating error response: " << e.what() << std::endl;
        // Try to create a minimal error response
        try {
          Json::Value errorJson;
          errorJson["error"] = "Internal server error";
          auto resp = HttpResponse::newHttpJsonResponse(errorJson);
          resp->setStatusCode(k500InternalServerError);
          callback(resp);
        } catch (...) {
          std::cerr << "[InstanceHandler] Failed to create fallback error response" << std::endl;
        }
      } catch (...) {
        std::cerr << "[InstanceHandler] Unknown exception creating error response" << std::endl;
      }
      return;
    }

    // Get all instances in one lock acquisition (optimized)
    // CRITICAL: Use async with timeout to prevent blocking if mutex is held
    // Capture manager pointer locally to avoid race conditions
    std::vector<InstanceInfo> allInstances;
    try {
      auto future =
          std::async(std::launch::async, [manager]() -> std::vector<InstanceInfo> {
            try {
              if (manager) {
                return manager->getAllInstances();
              }
              return {};
            } catch (...) {
              return {};
            }
          });

      // Wait with timeout (2 seconds) to prevent hanging
      auto status = future.wait_for(std::chrono::seconds(2));
      if (status == std::future_status::timeout) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] GET /v1/core/instance/status/summary - "
                          "Timeout getting instances (2s)";
        }
        callback(createErrorResponse(
            503, "Service Unavailable",
            "Instance registry is busy. Please try again later."));
        return;
      } else if (status == std::future_status::ready) {
        try {
          allInstances = future.get();
        } catch (...) {
          callback(createErrorResponse(500, "Internal server error",
                                       "Failed to get instances"));
          return;
        }
      }
    } catch (...) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to get instances"));
      return;
    }

    // Count instances by status
    int totalCount = 0;
    int runningCount = 0;
    int stoppedCount = 0;

    for (const auto &info : allInstances) {
      totalCount++;
      if (info.running) {
        runningCount++;
      } else {
        stoppedCount++;
      }
    }

    // Build response
    Json::Value response;
    response["total"] = totalCount;
    response["configured"] = totalCount; // Same as total for clarity
    response["running"] = runningCount;
    response["stopped"] = stoppedCount;

    // Add timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << "Z";
    response["timestamp"] = ss.str();

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/instance/status/summary - Success: "
                << totalCount << " total (running: " << runningCount
                << ", stopped: " << stoppedCount << ") - " << duration.count()
                << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/status/summary - Exception: "
                 << e.what() << " - " << duration.count() << "ms";
    }
    std::cerr << "[InstanceHandler] Exception in getStatusSummary: " << e.what()
              << std::endl;
    try {
      auto errorResp = createErrorResponse(500, "Internal server error", e.what());
      if (errorResp) {
        callback(errorResp);
      }
    } catch (...) {
      std::cerr << "[InstanceHandler] Failed to create error response in exception handler" << std::endl;
    }
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR
          << "[API] GET /v1/core/instance/status/summary - Unknown exception - "
          << duration.count() << "ms";
    }
    std::cerr << "[InstanceHandler] Unknown exception in getStatusSummary"
              << std::endl;
    try {
      auto errorResp = createErrorResponse(500, "Internal server error",
                                          "Unknown error occurred");
      if (errorResp) {
        callback(errorResp);
      }
    } catch (...) {
      std::cerr << "[InstanceHandler] Failed to create error response in unknown exception handler" << std::endl;
    }
  }
}

void InstanceHandler::listInstances(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/instance - List instances";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/instance - Error: Instance registry "
                      "not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    // Get all instances in one lock acquisition (optimized)
    // CRITICAL: Use async with shorter timeout to prevent blocking if mutex is
    // held Reduced timeout from 2s to 500ms to fail fast when registry is busy
    std::vector<InstanceInfo> allInstances;
    try {
      auto future =
          std::async(std::launch::async, [this]() -> std::vector<InstanceInfo> {
            try {
              if (instance_manager_) {
                return instance_manager_->getAllInstances();
              }
              return {};
            } catch (const std::exception &e) {
              // Log error but return empty vector
              std::cerr << "[InstanceHandler] Error in getAllInstances: "
                        << e.what() << std::endl;
              return {};
            } catch (...) {
              std::cerr << "[InstanceHandler] Unknown error in getAllInstances"
                        << std::endl;
              return {};
            }
          });

      // Wait with timeout (2.5 seconds) - slightly longer than
      // getAllInstances() timeout (2s) This gives enough time for
      // getAllInstances() to complete even if checkAndHandleRetryLimits() is
      // running
      auto status = future.wait_for(std::chrono::milliseconds(2500));
      if (status == std::future_status::timeout) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING
              << "[API] GET /v1/core/instance - Timeout getting instances "
                 "(2.5s) - mutex may be locked or operation is slow";
        }
        std::cerr << "[InstanceHandler] WARNING: getAllInstances() timeout "
                     "after 2.5s - registry may be busy "
                     "(checkAndHandleRetryLimits() may be running)"
                  << std::endl;
        callback(createErrorResponse(
            503, "Service Unavailable",
            "Instance registry is busy. Please try again later."));
        return;
      } else if (status == std::future_status::ready) {
        try {
          allInstances = future.get();
        } catch (const std::exception &e) {
          if (isApiLoggingEnabled()) {
            PLOG_ERROR << "[API] GET /v1/core/instance - Exception getting "
                          "instances: "
                       << e.what();
          }
          std::cerr << "[InstanceHandler] Exception getting instances: "
                    << e.what() << std::endl;
          callback(createErrorResponse(500, "Internal server error",
                                       "Failed to get instances: " +
                                           std::string(e.what())));
          return;
        } catch (...) {
          if (isApiLoggingEnabled()) {
            PLOG_ERROR << "[API] GET /v1/core/instance - Unknown exception "
                          "getting instances";
          }
          std::cerr << "[InstanceHandler] Unknown exception getting instances"
                    << std::endl;
          callback(createErrorResponse(500, "Internal server error",
                                       "Failed to get instances"));
          return;
        }
      } else {
        // Should not happen, but handle it
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] GET /v1/core/instance - Future status is not "
                          "ready or timeout";
        }
        callback(createErrorResponse(500, "Internal server error",
                                     "Failed to get instances"));
        return;
      }
    } catch (const std::exception &e) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR
            << "[API] GET /v1/core/instance - Exception creating async task: "
            << e.what();
      }
      std::cerr << "[InstanceHandler] Exception creating async task: "
                << e.what() << std::endl;
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to get instances: " +
                                       std::string(e.what())));
      return;
    } catch (...) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/instance - Unknown exception "
                      "creating async task";
      }
      std::cerr << "[InstanceHandler] Unknown exception creating async task"
                << std::endl;
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to get instances"));
      return;
    }

    // Build response with summary information
    Json::Value response;
    Json::Value instances(Json::arrayValue);

    int totalCount = 0;
    int runningCount = 0;
    int stoppedCount = 0;

    // Process all instances without additional lock acquisitions
    for (const auto &info : allInstances) {
      Json::Value instance;
      instance["instanceId"] = info.instanceId;
      instance["displayName"] = info.displayName;
      instance["group"] = info.group;
      instance["solutionId"] = info.solutionId;
      instance["solutionName"] = info.solutionName;
      instance["running"] = info.running;
      instance["loaded"] = info.loaded;
      instance["persistent"] = info.persistent;
      instance["fps"] = info.fps;

      instances.append(instance); // Use append instead of operator[] to avoid
                                  // ambiguous overload
      totalCount++;
      if (info.running) {
        runningCount++;
      } else {
        stoppedCount++;
      }
    }

    response["instances"] = instances;
    response["total"] = totalCount;
    response["running"] = runningCount;
    response["stopped"] = stoppedCount;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/instance - Success: " << totalCount
                << " instances (running: " << runningCount
                << ", stopped: " << stoppedCount << ") - " << duration.count()
                << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }
    std::cerr << "[InstanceHandler] Exception: " << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance - Unknown exception - "
                 << duration.count() << "ms";
    }
    std::cerr << "[InstanceHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

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

void InstanceHandler::updateInstance(
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

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Check if this is a direct config update (PascalCase format matching
    // instance_detail.txt) If JSON has top-level fields like "InstanceId",
    // "DisplayName", "Detector", etc., it's a direct config update
    bool isDirectConfigUpdate =
        json->isMember("InstanceId") || json->isMember("DisplayName") ||
        json->isMember("Detector") || json->isMember("Input") ||
        json->isMember("Output") || json->isMember("Zone");

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] Checking update format - isDirectConfigUpdate: "
                 << (isDirectConfigUpdate ? "true" : "false");
      PLOG_DEBUG << "[API] Request has InstanceId: "
                 << json->isMember("InstanceId");
      PLOG_DEBUG << "[API] Request has DisplayName: "
                 << json->isMember("DisplayName");
      PLOG_DEBUG << "[API] Request has Detector: "
                 << json->isMember("Detector");
      PLOG_DEBUG << "[API] Request has Input: " << json->isMember("Input");
      PLOG_DEBUG << "[API] Request has Output: " << json->isMember("Output");
      PLOG_DEBUG << "[API] Request has Zone: " << json->isMember("Zone");
    }

    if (isDirectConfigUpdate) {
      // Direct config update - merge JSON directly into storage
      if (instance_manager_->updateInstanceFromConfig(instanceId, *json)) {
        // Get updated instance info
        auto optInfo = instance_manager_->getInstance(instanceId);
        if (optInfo.has_value()) {
          Json::Value response = instanceInfoToJson(optInfo.value());
          response["message"] = "Instance updated successfully";
          callback(createSuccessResponse(response));
        } else {
          callback(createErrorResponse(500, "Internal server error",
                                       "Failed to retrieve updated instance"));
        }
      } else {
        callback(createErrorResponse(500, "Internal server error",
                                     "Failed to update instance"));
      }
      return;
    }

    // Traditional update request (camelCase fields)
    UpdateInstanceRequest updateReq;
    std::string parseError;
    if (!parseUpdateRequest(*json, updateReq, parseError)) {
      callback(createErrorResponse(400, "Invalid request", parseError));
      return;
    }

    // Validate request
    if (!updateReq.validate()) {
      callback(createErrorResponse(400, "Validation failed",
                                   updateReq.getValidationError()));
      return;
    }

    // Check if request has any updates
    if (!updateReq.hasUpdates()) {
      callback(
          createErrorResponse(400, "Invalid request", "No fields to update"));
      return;
    }

    // Convert UpdateInstanceRequest to JSON for IInstanceManager interface
    Json::Value updateJson;
    if (!updateReq.name.empty()) {
      updateJson["name"] = updateReq.name;
    }
    if (!updateReq.group.empty()) {
      updateJson["group"] = updateReq.group;
    }
    if (updateReq.persistent.has_value()) {
      updateJson["persistent"] = updateReq.persistent.value();
    }
    if (updateReq.frameRateLimit >= 0) {
      updateJson["frameRateLimit"] = updateReq.frameRateLimit;
    }
    if (updateReq.metadataMode.has_value()) {
      updateJson["metadataMode"] = updateReq.metadataMode.value();
    }
    if (updateReq.statisticsMode.has_value()) {
      updateJson["statisticsMode"] = updateReq.statisticsMode.value();
    }
    if (updateReq.diagnosticsMode.has_value()) {
      updateJson["diagnosticsMode"] = updateReq.diagnosticsMode.value();
    }
    if (updateReq.debugMode.has_value()) {
      updateJson["debugMode"] = updateReq.debugMode.value();
    }
    if (!updateReq.detectorMode.empty()) {
      updateJson["detectorMode"] = updateReq.detectorMode;
    }
    if (!updateReq.detectionSensitivity.empty()) {
      updateJson["detectionSensitivity"] = updateReq.detectionSensitivity;
    }
    if (!updateReq.movementSensitivity.empty()) {
      updateJson["movementSensitivity"] = updateReq.movementSensitivity;
    }
    if (!updateReq.sensorModality.empty()) {
      updateJson["sensorModality"] = updateReq.sensorModality;
    }
    if (updateReq.autoStart.has_value()) {
      updateJson["autoStart"] = updateReq.autoStart.value();
    }
    if (updateReq.autoRestart.has_value()) {
      updateJson["autoRestart"] = updateReq.autoRestart.value();
    }
    if (updateReq.inputOrientation >= 0) {
      updateJson["inputOrientation"] = updateReq.inputOrientation;
    }
    if (updateReq.inputPixelLimit >= 0) {
      updateJson["inputPixelLimit"] = updateReq.inputPixelLimit;
    }
    for (const auto &[key, value] : updateReq.additionalParams) {
      updateJson["additionalParams"][key] = value;
    }

    // Update instance
    if (instance_manager_->updateInstance(instanceId, updateJson)) {
      // Get updated instance info
      auto optInfo = instance_manager_->getInstance(instanceId);
      if (optInfo.has_value()) {
        Json::Value response = instanceInfoToJson(optInfo.value());
        response["message"] = "Instance updated successfully";
        callback(createSuccessResponse(response));
      } else {
        callback(createErrorResponse(
            500, "Internal server error",
            "Instance updated but could not retrieve info"));
      }
    } else {
      callback(createErrorResponse(400, "Failed to update",
                                   "Could not update instance. Check if "
                                   "instance exists and is not read-only."));
    }

  } catch (const std::exception &e) {
    std::cerr << "[InstanceHandler] Exception: " << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    std::cerr << "[InstanceHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void InstanceHandler::deleteInstance(
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

    // OPTIMIZED: Run deleteInstance() in detached thread to avoid blocking API
    // thread and other instances deleteInstance() calls stopPipeline() which
    // can take time (cleanup, DNN state clearing) This allows multiple
    // instances to be deleted concurrently without blocking each other The
    // instance will be deleted in background, and status can be checked via GET
    // /v1/core/instance/{id}

    // Check if instance exists first (before async deletion)
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    std::thread deleteThread([this, instanceId]() {
      try {
        instance_manager_->deleteInstance(instanceId);
      } catch (const std::exception &e) {
        std::cerr << "[InstanceHandler] Exception deleting instance "
                  << instanceId << ": " << e.what() << std::endl;
      } catch (...) {
        std::cerr << "[InstanceHandler] Unknown exception deleting instance "
                  << instanceId << std::endl;
      }
    });
    deleteThread
        .detach(); // Detach thread - instance deletion happens in background

    // Return immediately - instance is being deleted in background
    Json::Value response;
    response["success"] = true;
    response["message"] = "Instance deletion request accepted. Instance is "
                          "being deleted in background. "
                          "Check status using GET /v1/core/instance/" +
                          instanceId;
    response["instanceId"] = instanceId;
    response["status"] = "deleting"; // Indicate that instance is being deleted
    callback(createSuccessResponse(
        response, 202)); // 202 Accepted - request accepted but not completed
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

void InstanceHandler::deleteAllInstances(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] DELETE /v1/core/instance - Delete all instances";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/core/instance - Error: Instance "
                      "registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    // Get all instances
    std::vector<InstanceInfo> allInstances;
    try {
      auto future =
          std::async(std::launch::async, [this]() -> std::vector<InstanceInfo> {
            try {
              if (instance_manager_) {
                return instance_manager_->getAllInstances();
              }
              return {};
            } catch (...) {
              return {};
            }
          });

      // Wait with timeout (2.5 seconds)
      auto status = future.wait_for(std::chrono::milliseconds(2500));
      if (status == std::future_status::timeout) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] DELETE /v1/core/instance - Timeout getting "
                          "instances (2.5s)";
        }
        callback(createErrorResponse(
            503, "Service Unavailable",
            "Instance registry is busy. Please try again later."));
        return;
      } else if (status == std::future_status::ready) {
        try {
          allInstances = future.get();
        } catch (const std::exception &e) {
          if (isApiLoggingEnabled()) {
            PLOG_ERROR << "[API] DELETE /v1/core/instance - Exception getting "
                          "instances: "
                       << e.what();
          }
          callback(createErrorResponse(500, "Internal server error",
                                       "Failed to get instances: " +
                                           std::string(e.what())));
          return;
        } catch (...) {
          if (isApiLoggingEnabled()) {
            PLOG_ERROR << "[API] DELETE /v1/core/instance - Unknown exception "
                          "getting instances";
          }
          callback(createErrorResponse(500, "Internal server error",
                                       "Failed to get instances"));
          return;
        }
      }
    } catch (const std::exception &e) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/core/instance - Exception creating "
                      "async task: "
                   << e.what();
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to get instances: " +
                                       std::string(e.what())));
      return;
    } catch (...) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/core/instance - Unknown exception "
                      "creating async task";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to get instances"));
      return;
    }

    // If no instances, return success
    if (allInstances.empty()) {
      Json::Value response;
      response["success"] = true;
      response["message"] = "No instances to delete";
      response["total"] = 0;
      response["deleted"] = 0;
      response["failed"] = 0;
      response["results"] = Json::Value(Json::arrayValue);

      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);

      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] DELETE /v1/core/instance - Success: No instances "
                     "to delete - "
                  << duration.count() << "ms";
      }

      callback(createSuccessResponse(response));
      return;
    }

    // Extract instance IDs
    std::vector<std::string> instanceIds;
    for (const auto &info : allInstances) {
      instanceIds.push_back(info.instanceId);
    }

    // Execute delete operations concurrently using async
    std::vector<std::future<std::pair<std::string, bool>>> futures;
    for (const auto &instanceId : instanceIds) {
      futures.push_back(std::async(
          std::launch::async,
          [this, instanceId]() -> std::pair<std::string, bool> {
            try {
              bool success = instance_manager_->deleteInstance(instanceId);
              return {instanceId, success};
            } catch (...) {
              return {instanceId, false};
            }
          }));
    }

    // Collect results
    Json::Value response;
    Json::Value results(Json::arrayValue);
    int successCount = 0;
    int failureCount = 0;

    for (auto &future : futures) {
      auto [instanceId, success] = future.get();

      Json::Value result;
      result["instanceId"] = instanceId;
      result["success"] = success;

      if (success) {
        result["status"] = "deleted";
        successCount++;
      } else {
        result["status"] = "failed";
        result["error"] = "Could not delete instance. Instance may not exist.";
        failureCount++;
      }

      results.append(result);
    }

    response["results"] = results;
    response["total"] = static_cast<int>(instanceIds.size());
    response["deleted"] = successCount;
    response["failed"] = failureCount;
    response["message"] = "Delete all instances operation completed";
    response["success"] = (failureCount == 0);

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] DELETE /v1/core/instance - Success: " << successCount
                << " deleted, " << failureCount << " failed out of "
                << instanceIds.size() << " total - " << duration.count()
                << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/core/instance - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }
    std::cerr << "[InstanceHandler] Exception in deleteAllInstances: "
              << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/core/instance - Unknown exception - "
                 << duration.count() << "ms";
    }
    std::cerr << "[InstanceHandler] Unknown exception in deleteAllInstances"
              << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

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

void InstanceHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    // Set handler start time for accurate metrics
    if (req) {
      MetricsInterceptor::setHandlerStartTime(req);
    }

    auto resp = HttpResponse::newHttpResponse();
    if (!resp) {
      std::cerr << "[InstanceHandler] Failed to create response in handleOptions" << std::endl;
      return;
    }

    resp->setStatusCode(k200OK);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods",
                    "GET, POST, PUT, DELETE, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers",
                    "Content-Type, Authorization");
    resp->addHeader("Access-Control-Max-Age", "3600");

    // Record metrics and call callback
    if (req) {
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
    } else {
      callback(resp);
    }
  } catch (const std::exception &e) {
    std::cerr << "[InstanceHandler] Exception in handleOptions: " << e.what() << std::endl;
    try {
      auto errorResp = HttpResponse::newHttpResponse();
      if (errorResp) {
        errorResp->setStatusCode(k500InternalServerError);
        callback(errorResp);
      }
    } catch (...) {
      std::cerr << "[InstanceHandler] Failed to create error response in handleOptions" << std::endl;
    }
  } catch (...) {
    std::cerr << "[InstanceHandler] Unknown exception in handleOptions" << std::endl;
    try {
      auto errorResp = HttpResponse::newHttpResponse();
      if (errorResp) {
        errorResp->setStatusCode(k500InternalServerError);
        callback(errorResp);
      }
    } catch (...) {
      std::cerr << "[InstanceHandler] Failed to create error response in handleOptions" << std::endl;
    }
  }
}

bool InstanceHandler::parseUpdateRequest(const Json::Value &json,
                                         UpdateInstanceRequest &req,
                                         std::string &error) {

  // Support both camelCase and PascalCase field names
  // Basic fields - camelCase
  if (json.isMember("name") && json["name"].isString()) {
    req.name = json["name"].asString();
  }
  // PascalCase support
  if (json.isMember("DisplayName") && json["DisplayName"].isString()) {
    req.name = json["DisplayName"].asString();
  }

  if (json.isMember("group") && json["group"].isString()) {
    req.group = json["group"].asString();
  }
  if (json.isMember("Group") && json["Group"].isString()) {
    req.group = json["Group"].asString();
  }

  if (json.isMember("persistent") && json["persistent"].isBool()) {
    req.persistent = json["persistent"].asBool();
  }

  if (json.isMember("frameRateLimit") && json["frameRateLimit"].isNumeric()) {
    req.frameRateLimit = json["frameRateLimit"].asInt();
  }

  if (json.isMember("metadataMode") && json["metadataMode"].isBool()) {
    req.metadataMode = json["metadataMode"].asBool();
  }

  if (json.isMember("statisticsMode") && json["statisticsMode"].isBool()) {
    req.statisticsMode = json["statisticsMode"].asBool();
  }

  if (json.isMember("diagnosticsMode") && json["diagnosticsMode"].isBool()) {
    req.diagnosticsMode = json["diagnosticsMode"].asBool();
  }

  if (json.isMember("debugMode") && json["debugMode"].isBool()) {
    req.debugMode = json["debugMode"].asBool();
  }

  if (json.isMember("detectorMode") && json["detectorMode"].isString()) {
    req.detectorMode = json["detectorMode"].asString();
  }

  if (json.isMember("detectionSensitivity") &&
      json["detectionSensitivity"].isString()) {
    req.detectionSensitivity = json["detectionSensitivity"].asString();
  }

  if (json.isMember("movementSensitivity") &&
      json["movementSensitivity"].isString()) {
    req.movementSensitivity = json["movementSensitivity"].asString();
  }

  if (json.isMember("sensorModality") && json["sensorModality"].isString()) {
    req.sensorModality = json["sensorModality"].asString();
  }

  if (json.isMember("autoStart") && json["autoStart"].isBool()) {
    req.autoStart = json["autoStart"].asBool();
  }
  if (json.isMember("AutoStart") && json["AutoStart"].isBool()) {
    req.autoStart = json["AutoStart"].asBool();
  }

  if (json.isMember("autoRestart") && json["autoRestart"].isBool()) {
    req.autoRestart = json["autoRestart"].asBool();
  }

  // Parse Detector nested object
  if (json.isMember("Detector") && json["Detector"].isObject()) {
    const Json::Value &detector = json["Detector"];
    if (detector.isMember("current_preset") &&
        detector["current_preset"].isString()) {
      req.detectorMode = detector["current_preset"].asString();
    }
    if (detector.isMember("current_sensitivity_preset") &&
        detector["current_sensitivity_preset"].isString()) {
      req.detectionSensitivity =
          detector["current_sensitivity_preset"].asString();
    }
    if (detector.isMember("model_file") && detector["model_file"].isString()) {
      req.additionalParams["DETECTOR_MODEL_FILE"] =
          detector["model_file"].asString();
    }
    if (detector.isMember("animal_confidence_threshold") &&
        detector["animal_confidence_threshold"].isNumeric()) {
      req.additionalParams["ANIMAL_CONFIDENCE_THRESHOLD"] =
          std::to_string(detector["animal_confidence_threshold"].asDouble());
    }
    if (detector.isMember("person_confidence_threshold") &&
        detector["person_confidence_threshold"].isNumeric()) {
      req.additionalParams["PERSON_CONFIDENCE_THRESHOLD"] =
          std::to_string(detector["person_confidence_threshold"].asDouble());
    }
    if (detector.isMember("vehicle_confidence_threshold") &&
        detector["vehicle_confidence_threshold"].isNumeric()) {
      req.additionalParams["VEHICLE_CONFIDENCE_THRESHOLD"] =
          std::to_string(detector["vehicle_confidence_threshold"].asDouble());
    }
    if (detector.isMember("face_confidence_threshold") &&
        detector["face_confidence_threshold"].isNumeric()) {
      req.additionalParams["FACE_CONFIDENCE_THRESHOLD"] =
          std::to_string(detector["face_confidence_threshold"].asDouble());
    }
    if (detector.isMember("license_plate_confidence_threshold") &&
        detector["license_plate_confidence_threshold"].isNumeric()) {
      req.additionalParams["LICENSE_PLATE_CONFIDENCE_THRESHOLD"] =
          std::to_string(
              detector["license_plate_confidence_threshold"].asDouble());
    }
    if (detector.isMember("conf_threshold") &&
        detector["conf_threshold"].isNumeric()) {
      req.additionalParams["CONF_THRESHOLD"] =
          std::to_string(detector["conf_threshold"].asDouble());
    }
  }

  // Parse Input nested object
  if (json.isMember("Input") && json["Input"].isObject()) {
    const Json::Value &input = json["Input"];
    if (input.isMember("uri") && input["uri"].isString()) {
      std::string uri = input["uri"].asString();
      // Extract RTSP URL from GStreamer URI - support both old format (uri=)
      // and new format (location=)
      size_t rtspPos = uri.find("location=");
      if (rtspPos != std::string::npos) {
        // New format: rtspsrc location=...
        size_t start = rtspPos + 9;
        size_t end = uri.find(" ", start);
        if (end == std::string::npos) {
          end = uri.find(" !", start);
        }
        if (end == std::string::npos) {
          end = uri.length();
        }
        req.additionalParams["RTSP_URL"] = uri.substr(start, end - start);
      } else {
        // Old format: gstreamer:///urisourcebin uri=...
        rtspPos = uri.find("uri=");
        if (rtspPos != std::string::npos) {
          size_t start = rtspPos + 4;
          size_t end = uri.find(" !", start);
          if (end == std::string::npos) {
            end = uri.length();
          }
          req.additionalParams["RTSP_URL"] = uri.substr(start, end - start);
        } else if (uri.find("://") == std::string::npos) {
          // Direct file path
          req.additionalParams["FILE_PATH"] = uri;
        }
      }
    }
    if (input.isMember("media_type") && input["media_type"].isString()) {
      req.additionalParams["INPUT_MEDIA_TYPE"] = input["media_type"].asString();
    }
  }

  // Parse Output nested object
  if (json.isMember("Output") && json["Output"].isObject()) {
    const Json::Value &output = json["Output"];
    if (output.isMember("JSONExport") && output["JSONExport"].isObject()) {
      if (output["JSONExport"].isMember("enabled") &&
          output["JSONExport"]["enabled"].isBool()) {
        req.metadataMode = output["JSONExport"]["enabled"].asBool();
      }
    }
    if (output.isMember("handlers") && output["handlers"].isObject()) {
      // Parse RTSP handler if available
      for (const auto &handlerKey : output["handlers"].getMemberNames()) {
        const Json::Value &handler = output["handlers"][handlerKey];
        if (handler.isMember("uri") && handler["uri"].isString()) {
          req.additionalParams["OUTPUT_RTSP_URL"] = handler["uri"].asString();
        }
        if (handler.isMember("config") && handler["config"].isObject()) {
          if (handler["config"].isMember("fps") &&
              handler["config"]["fps"].isNumeric()) {
            req.frameRateLimit = handler["config"]["fps"].asInt();
          }
        }
      }
    }
  }

  // Parse SolutionManager nested object
  if (json.isMember("SolutionManager") && json["SolutionManager"].isObject()) {
    const Json::Value &sm = json["SolutionManager"];
    if (sm.isMember("frame_rate_limit") && sm["frame_rate_limit"].isNumeric()) {
      req.frameRateLimit = sm["frame_rate_limit"].asInt();
    }
    if (sm.isMember("send_metadata") && sm["send_metadata"].isBool()) {
      req.metadataMode = sm["send_metadata"].asBool();
    }
    if (sm.isMember("run_statistics") && sm["run_statistics"].isBool()) {
      req.statisticsMode = sm["run_statistics"].asBool();
    }
    if (sm.isMember("send_diagnostics") && sm["send_diagnostics"].isBool()) {
      req.diagnosticsMode = sm["send_diagnostics"].asBool();
    }
    if (sm.isMember("enable_debug") && sm["enable_debug"].isBool()) {
      req.debugMode = sm["enable_debug"].asBool();
    }
    if (sm.isMember("input_pixel_limit") &&
        sm["input_pixel_limit"].isNumeric()) {
      req.inputPixelLimit = sm["input_pixel_limit"].asInt();
    }
  }

  // Parse PerformanceMode nested object
  if (json.isMember("PerformanceMode") && json["PerformanceMode"].isObject()) {
    const Json::Value &pm = json["PerformanceMode"];
    if (pm.isMember("current_preset") && pm["current_preset"].isString()) {
      req.additionalParams["PERFORMANCE_MODE"] =
          pm["current_preset"].asString();
    }
  }

  // Parse DetectorThermal nested object
  if (json.isMember("DetectorThermal") && json["DetectorThermal"].isObject()) {
    const Json::Value &dt = json["DetectorThermal"];
    if (dt.isMember("model_file") && dt["model_file"].isString()) {
      req.additionalParams["DETECTOR_THERMAL_MODEL_FILE"] =
          dt["model_file"].asString();
    }
  }

  // Parse Zone nested object (store as JSON string for later processing)
  if (json.isMember("Zone") && json["Zone"].isObject()) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string zoneJson = Json::writeString(builder, json["Zone"]);
    req.additionalParams["ZONE_CONFIG"] = zoneJson;
  }

  // Parse Tripwire nested object
  if (json.isMember("Tripwire") && json["Tripwire"].isObject()) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string tripwireJson = Json::writeString(builder, json["Tripwire"]);
    req.additionalParams["TRIPWIRE_CONFIG"] = tripwireJson;
  }

  // Parse DetectorRegions nested object
  if (json.isMember("DetectorRegions") && json["DetectorRegions"].isObject()) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string regionsJson =
        Json::writeString(builder, json["DetectorRegions"]);
    req.additionalParams["DETECTOR_REGIONS_CONFIG"] = regionsJson;
  }

  if (json.isMember("inputOrientation") &&
      json["inputOrientation"].isNumeric()) {
    req.inputOrientation = json["inputOrientation"].asInt();
  }

  if (json.isMember("inputPixelLimit") && json["inputPixelLimit"].isNumeric()) {
    req.inputPixelLimit = json["inputPixelLimit"].asInt();
  }

  // Additional parameters (e.g., RTSP_URL, MODEL_PATH, FILE_PATH, RTMP_URL)
  // Helper function to trim whitespace (especially important for RTMP URLs)
  auto trim = [](const std::string &str) -> std::string {
    if (str.empty())
      return str;
    size_t first = str.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string::npos)
      return "";
    size_t last = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(first, (last - first + 1));
  };

  // Parse additionalParams - support both new structure (input/output) and old
  // structure (flat)
  if (json.isMember("additionalParams") &&
      json["additionalParams"].isObject()) {
    // Check if using new structure (input/output)
    if (json["additionalParams"].isMember("input") &&
        json["additionalParams"]["input"].isObject()) {
      // New structure: parse input section
      for (const auto &key :
           json["additionalParams"]["input"].getMemberNames()) {
        if (json["additionalParams"]["input"][key].isString()) {
          std::string value = json["additionalParams"]["input"][key].asString();
          req.additionalParams[key] = value;
        }
      }
    }

    if (json["additionalParams"].isMember("output") &&
        json["additionalParams"]["output"].isObject()) {
      // New structure: parse output section
      for (const auto &key :
           json["additionalParams"]["output"].getMemberNames()) {
        if (json["additionalParams"]["output"][key].isString()) {
          std::string value =
              json["additionalParams"]["output"][key].asString();
          // Trim RTMP URLs to prevent GStreamer pipeline errors
          if (key == "RTMP_URL" || key == "RTMP_DES_URL") {
            value = trim(value);
          }
          req.additionalParams[key] = value;
        }
      }
    }

    // Backward compatibility: if no input/output sections, parse as flat
    // structure
    // Also parse top-level keys (like CrossingLines) even when input/output
    // sections exist
    if (!json["additionalParams"].isMember("input") &&
        !json["additionalParams"].isMember("output")) {
      for (const auto &key : json["additionalParams"].getMemberNames()) {
        if (json["additionalParams"][key].isString()) {
          std::string value = json["additionalParams"][key].asString();
          // Trim RTMP URLs to prevent GStreamer pipeline errors
          if (key == "RTMP_URL" || key == "RTMP_DES_URL") {
            value = trim(value);
          }
          req.additionalParams[key] = value;
        }
      }
    } else {
      // Parse top-level keys in additionalParams (like CrossingLines) even when
      // input/output exist
      for (const auto &key : json["additionalParams"].getMemberNames()) {
        // Skip input/output sections (already parsed above)
        if (key == "input" || key == "output") {
          continue;
        }
        if (json["additionalParams"][key].isString()) {
          std::string value = json["additionalParams"][key].asString();
          // Trim RTMP URLs to prevent GStreamer pipeline errors
          if (key == "RTMP_URL" || key == "RTMP_DES_URL") {
            value = trim(value);
          }
          req.additionalParams[key] = value;
        }
      }
    }
  }

  // Also check for RTSP_URL at top level
  if (json.isMember("RTSP_URL") && json["RTSP_URL"].isString()) {
    req.additionalParams["RTSP_URL"] = json["RTSP_URL"].asString();
  }

  // Also check for MODEL_NAME at top level
  if (json.isMember("MODEL_NAME") && json["MODEL_NAME"].isString()) {
    req.additionalParams["MODEL_NAME"] = json["MODEL_NAME"].asString();
  }

  // Also check for MODEL_PATH at top level
  if (json.isMember("MODEL_PATH") && json["MODEL_PATH"].isString()) {
    req.additionalParams["MODEL_PATH"] = json["MODEL_PATH"].asString();
  }

  // Also check for FILE_PATH at top level (for file source)
  if (json.isMember("FILE_PATH") && json["FILE_PATH"].isString()) {
    req.additionalParams["FILE_PATH"] = json["FILE_PATH"].asString();
  }

  // Also check for RTMP_DES_URL or RTMP_URL at top level (for RTMP destination)
  if (json.isMember("RTMP_DES_URL") && json["RTMP_DES_URL"].isString()) {
    req.additionalParams["RTMP_DES_URL"] =
        trim(json["RTMP_DES_URL"].asString());
  } else if (json.isMember("RTMP_URL") && json["RTMP_URL"].isString()) {
    req.additionalParams["RTMP_URL"] = trim(json["RTMP_URL"].asString());
  }

  // Also check for SFACE_MODEL_PATH at top level (for SFace encoder)
  if (json.isMember("SFACE_MODEL_PATH") &&
      json["SFACE_MODEL_PATH"].isString()) {
    req.additionalParams["SFACE_MODEL_PATH"] =
        json["SFACE_MODEL_PATH"].asString();
  }

  // Also check for SFACE_MODEL_NAME at top level (for SFace encoder by name)
  if (json.isMember("SFACE_MODEL_NAME") &&
      json["SFACE_MODEL_NAME"].isString()) {
    req.additionalParams["SFACE_MODEL_NAME"] =
        json["SFACE_MODEL_NAME"].asString();
  }

  return true;
}

Json::Value
InstanceHandler::instanceInfoToJson(const InstanceInfo &info) const {
  Json::Value json;

  // Format matching task/instance_detail.txt
  // Basic fields
  json["InstanceId"] = info.instanceId;
  json["DisplayName"] = info.displayName;
  json["AutoStart"] = info.autoStart;
  json["Solution"] = info.solutionId;

  // Async pipeline build status (match POST create response; required for polling GET before start)
  json["instanceId"] = info.instanceId;
  json["building"] = info.building;
  if (info.building) {
    json["status"] = "building";
    json["message"] = "Pipeline is being built in background";
  } else if (!info.buildError.empty()) {
    json["status"] = "error";
    json["buildError"] = info.buildError;
  } else {
    json["status"] = "ready";
  }

  // OriginatorInfo
  Json::Value originator(Json::objectValue);
  originator["address"] =
      info.originator.address.empty() ? "127.0.0.1" : info.originator.address;
  json["OriginatorInfo"] = originator;

  // Input configuration
  Json::Value input(Json::objectValue);
  if (!info.rtspUrl.empty()) {
    input["media_type"] = "IP Camera";

    // Check if user wants to use urisourcebin format (for
    // compatibility/auto-detect decoder)
    bool useUrisourcebin = false;
    auto urisourcebinIt = info.additionalParams.find("USE_URISOURCEBIN");
    if (urisourcebinIt != info.additionalParams.end()) {
      useUrisourcebin =
          (urisourcebinIt->second == "true" || urisourcebinIt->second == "1");
    }

    // Get decoder name from additionalParams
    std::string decoderName = "avdec_h264"; // Default decoder
    auto decoderIt = info.additionalParams.find("GST_DECODER_NAME");
    if (decoderIt != info.additionalParams.end() &&
        !decoderIt->second.empty()) {
      decoderName = decoderIt->second;
      // If decodebin is specified, use urisourcebin format
      if (decoderName == "decodebin") {
        useUrisourcebin = true;
      }
    }

    if (useUrisourcebin) {
      // Format: gstreamer:///urisourcebin uri=... ! decodebin ! videoconvert !
      // video/x-raw, format=NV12 ! appsink drop=true name=cvdsink This format
      // uses decodebin for auto-detection (may help with decoder compatibility
      // issues)
      input["uri"] = "gstreamer:///urisourcebin uri=" + info.rtspUrl +
                     " ! decodebin ! videoconvert ! video/x-raw, format=NV12 ! "
                     "appsink drop=true name=cvdsink";
    } else {
      // Format: rtspsrc location=... [protocols=...] !
      // application/x-rtp,media=video ! rtph264depay ! h264parse ! decoder !
      // videoconvert ! video/x-raw,format=NV12 ! appsink drop=true name=cvdsink
      // This format matches SDK template structure (with h264parse before
      // decoder) Get transport protocol from additionalParams if specified
      std::string protocolsParam = "";
      auto rtspTransportIt = info.additionalParams.find("RTSP_TRANSPORT");
      if (rtspTransportIt != info.additionalParams.end() &&
          !rtspTransportIt->second.empty()) {
        std::string transport = rtspTransportIt->second;
        std::transform(transport.begin(), transport.end(), transport.begin(),
                       ::tolower);
        if (transport == "tcp" || transport == "udp") {
          protocolsParam = " protocols=" + transport;
        }
      }
      // If no transport specified, don't add protocols parameter - let
      // GStreamer use default
      input["uri"] =
          "rtspsrc location=" + info.rtspUrl + protocolsParam +
          " ! application/x-rtp,media=video ! rtph264depay ! h264parse ! " +
          decoderName +
          " ! videoconvert ! video/x-raw,format=NV12 ! appsink drop=true "
          "name=cvdsink";
    }
  } else if (!info.filePath.empty()) {
    input["media_type"] = "File";
    input["uri"] = info.filePath;
  } else {
    input["media_type"] = "IP Camera";
    input["uri"] = "";
  }

  // Input media_format
  Json::Value mediaFormat(Json::objectValue);
  mediaFormat["color_format"] = 0;
  mediaFormat["default_format"] = true;
  mediaFormat["height"] = 0;
  mediaFormat["is_software"] = false;
  mediaFormat["name"] = "Same as Source";
  input["media_format"] = mediaFormat;
  json["Input"] = input;

  // Output configuration
  Json::Value output(Json::objectValue);
  output["JSONExport"]["enabled"] = info.metadataMode;
  output["NXWitness"]["enabled"] = false;

  // Output handlers (RTSP output if available)
  Json::Value handlers(Json::objectValue);
  if (!info.rtspUrl.empty() || !info.rtmpUrl.empty()) {
    // Create RTSP handler for output stream
    Json::Value rtspHandler(Json::objectValue);
    Json::Value handlerConfig(Json::objectValue);
    handlerConfig["debug"] = info.debugMode ? "4" : "0";
    // Use configuredFps (from API /api/v1/instances/{id}/fps) for output FPS
    // This ensures output stream matches processing FPS
    handlerConfig["fps"] = info.configuredFps > 0 ? info.configuredFps : 5;
    handlerConfig["pipeline"] =
        "( appsrc name=cvedia-rt ! videoconvert ! videoscale ! x264enc ! "
        "video/x-h264,profile=high ! rtph264pay name=pay0 pt=96 )";
    rtspHandler["config"] = handlerConfig;
    rtspHandler["enabled"] = info.running;
    rtspHandler["sink"] = "output-image";

    // Use RTSP URL if available, otherwise construct from RTMP
    std::string outputUrl = info.rtspUrl;
    if (outputUrl.empty() && !info.rtmpUrl.empty()) {
      // Extract port and stream name from RTMP URL if possible
      outputUrl = "rtsp://0.0.0.0:8554/stream1";
    }
    if (outputUrl.empty()) {
      outputUrl = "rtsp://0.0.0.0:8554/stream1";
    }
    rtspHandler["uri"] = outputUrl;
    handlers["rtsp:--0.0.0.0:8554-stream1"] = rtspHandler;
  }
  output["handlers"] = handlers;
  output["render_preset"] = "Default";
  json["Output"] = output;

  // Detector configuration
  Json::Value detector(Json::objectValue);
  detector["animal_confidence_threshold"] = info.animalConfidenceThreshold > 0.0
                                                ? info.animalConfidenceThreshold
                                                : 0.3;
  detector["conf_threshold"] =
      info.confThreshold > 0.0 ? info.confThreshold : 0.2;
  detector["current_preset"] =
      info.detectorMode.empty() ? "FullRegionInference" : info.detectorMode;
  detector["current_sensitivity_preset"] =
      info.detectionSensitivity.empty() ? "High" : info.detectionSensitivity;
  detector["face_confidence_threshold"] =
      info.faceConfidenceThreshold > 0.0 ? info.faceConfidenceThreshold : 0.1;
  detector["license_plate_confidence_threshold"] =
      info.licensePlateConfidenceThreshold > 0.0
          ? info.licensePlateConfidenceThreshold
          : 0.1;
  detector["model_file"] = info.detectorModelFile.empty()
                               ? "pva_det_full_frame_512"
                               : info.detectorModelFile;
  detector["person_confidence_threshold"] = info.personConfidenceThreshold > 0.0
                                                ? info.personConfidenceThreshold
                                                : 0.3;
  detector["vehicle_confidence_threshold"] =
      info.vehicleConfidenceThreshold > 0.0 ? info.vehicleConfidenceThreshold
                                            : 0.3;

  // Preset values
  Json::Value presetValues(Json::objectValue);
  Json::Value mosaicInference(Json::objectValue);
  mosaicInference["Detector/model_file"] = "pva_det_mosaic_320";
  presetValues["MosaicInference"] = mosaicInference;
  detector["preset_values"] = presetValues;

  json["Detector"] = detector;

  // DetectorRegions (empty by default)
  json["DetectorRegions"] = Json::Value(Json::objectValue);

  // DetectorThermal (always include)
  Json::Value detectorThermal(Json::objectValue);
  detectorThermal["model_file"] = info.detectorThermalModelFile.empty()
                                      ? "pva_det_mosaic_320"
                                      : info.detectorThermalModelFile;
  json["DetectorThermal"] = detectorThermal;

  // PerformanceMode
  Json::Value performanceMode(Json::objectValue);
  performanceMode["current_preset"] =
      info.performanceMode.empty() ? "Balanced" : info.performanceMode;
  json["PerformanceMode"] = performanceMode;

  // SolutionManager
  Json::Value solutionManager(Json::objectValue);
  solutionManager["enable_debug"] = info.debugMode;
  // Use configuredFps (from API /api/v1/instances/{id}/fps) for frame_rate_limit
  // This ensures SDK processing matches configured FPS
  // Fallback to frameRateLimit if configuredFps not set (backward compatibility)
  solutionManager["frame_rate_limit"] =
      info.configuredFps > 0 ? info.configuredFps : 
      (info.frameRateLimit > 0 ? info.frameRateLimit : 5);
  solutionManager["input_pixel_limit"] =
      info.inputPixelLimit > 0 ? info.inputPixelLimit : 2000000;
  solutionManager["recommended_frame_rate"] =
      info.recommendedFrameRate > 0 ? info.recommendedFrameRate : 5;
  solutionManager["run_statistics"] = info.statisticsMode;
  solutionManager["send_diagnostics"] = info.diagnosticsMode;
  solutionManager["send_metadata"] = info.metadataMode;
  json["SolutionManager"] = solutionManager;

  // Tripwire (empty by default)
  Json::Value tripwire(Json::objectValue);
  tripwire["Tripwires"] = Json::Value(Json::objectValue);
  json["Tripwire"] = tripwire;

  // Zone (empty by default, can be populated from additionalParams if needed)
  Json::Value zone(Json::objectValue);
  zone["Zones"] = Json::Value(Json::objectValue);
  json["Zone"] = zone;

  return json;
}

HttpResponsePtr
InstanceHandler::createErrorResponse(int statusCode, const std::string &error,
                                     const std::string &message) const {

  Json::Value errorJson;
  errorJson["error"] = error;
  if (!message.empty()) {
    errorJson["message"] = message;
  }

  auto resp = HttpResponse::newHttpJsonResponse(errorJson);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));

  // Add CORS headers to error responses
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, PUT, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");

  return resp;
}

void InstanceHandler::batchStartInstances(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  try {
    // Check if registry is set
    if (!instance_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Extract instance IDs from request
    if (!json->isMember("instanceIds") || !(*json)["instanceIds"].isArray()) {
      callback(
          createErrorResponse(400, "Invalid request",
                              "Request body must contain 'instanceIds' array"));
      return;
    }

    std::vector<std::string> instanceIds;
    const Json::Value &idsArray = (*json)["instanceIds"];
    for (const auto &id : idsArray) {
      if (id.isString()) {
        instanceIds.push_back(id.asString());
      }
    }

    if (instanceIds.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "At least one instance ID is required"));
      return;
    }

    // Execute start operations concurrently using async
    std::vector<std::future<std::pair<std::string, bool>>> futures;
    for (const auto &instanceId : instanceIds) {
      futures.push_back(std::async(
          std::launch::async,
          [this, instanceId]() -> std::pair<std::string, bool> {
            try {
              bool success = instance_manager_->startInstance(instanceId);
              return {instanceId, success};
            } catch (...) {
              return {instanceId, false};
            }
          }));
    }

    // Collect results
    Json::Value response;
    Json::Value results(Json::arrayValue);
    int successCount = 0;
    int failureCount = 0;

    for (auto &future : futures) {
      auto [instanceId, success] = future.get();

      Json::Value result;
      result["instanceId"] = instanceId;
      result["success"] = success;

      if (success) {
        auto optInfo = instance_manager_->getInstance(instanceId);
        if (optInfo.has_value()) {
          result["status"] = "started";
          result["running"] = optInfo.value().running;
        }
        successCount++;
      } else {
        result["status"] = "failed";
        result["error"] = "Could not start instance. Check if instance exists "
                          "and has a pipeline.";
        failureCount++;
      }

      results.append(result);
    }

    response["results"] = results;
    response["total"] = static_cast<int>(instanceIds.size());
    response["success"] = successCount;
    response["failed"] = failureCount;
    response["message"] = "Batch start operation completed";

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    std::cerr << "[InstanceHandler] Exception in batchStartInstances: "
              << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    std::cerr << "[InstanceHandler] Unknown exception in batchStartInstances"
              << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void InstanceHandler::batchStopInstances(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  try {
    // Check if registry is set
    if (!instance_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Extract instance IDs from request
    if (!json->isMember("instanceIds") || !(*json)["instanceIds"].isArray()) {
      callback(
          createErrorResponse(400, "Invalid request",
                              "Request body must contain 'instanceIds' array"));
      return;
    }

    std::vector<std::string> instanceIds;
    const Json::Value &idsArray = (*json)["instanceIds"];
    for (const auto &id : idsArray) {
      if (id.isString()) {
        instanceIds.push_back(id.asString());
      }
    }

    if (instanceIds.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "At least one instance ID is required"));
      return;
    }

    // Execute stop operations concurrently using async
    std::vector<std::future<std::pair<std::string, bool>>> futures;
    for (const auto &instanceId : instanceIds) {
      futures.push_back(std::async(
          std::launch::async,
          [this, instanceId]() -> std::pair<std::string, bool> {
            try {
              bool success = instance_manager_->stopInstance(instanceId);
              return {instanceId, success};
            } catch (...) {
              return {instanceId, false};
            }
          }));
    }

    // Collect results
    Json::Value response;
    Json::Value results(Json::arrayValue);
    int successCount = 0;
    int failureCount = 0;

    for (auto &future : futures) {
      auto [instanceId, success] = future.get();

      Json::Value result;
      result["instanceId"] = instanceId;
      result["success"] = success;

      if (success) {
        auto optInfo = instance_manager_->getInstance(instanceId);
        if (optInfo.has_value()) {
          result["status"] = "stopped";
          result["running"] = optInfo.value().running;
        }
        successCount++;
      } else {
        result["status"] = "failed";
        result["error"] = "Could not stop instance. Check if instance exists.";
        failureCount++;
      }

      results.append(result);
    }

    response["results"] = results;
    response["total"] = static_cast<int>(instanceIds.size());
    response["success"] = successCount;
    response["failed"] = failureCount;
    response["message"] = "Batch stop operation completed";

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    std::cerr << "[InstanceHandler] Exception in batchStopInstances: "
              << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    std::cerr << "[InstanceHandler] Unknown exception in batchStopInstances"
              << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void InstanceHandler::batchRestartInstances(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  try {
    // Check if registry is set
    if (!instance_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Extract instance IDs from request
    if (!json->isMember("instanceIds") || !(*json)["instanceIds"].isArray()) {
      callback(
          createErrorResponse(400, "Invalid request",
                              "Request body must contain 'instanceIds' array"));
      return;
    }

    std::vector<std::string> instanceIds;
    const Json::Value &idsArray = (*json)["instanceIds"];
    for (const auto &id : idsArray) {
      if (id.isString()) {
        instanceIds.push_back(id.asString());
      }
    }

    if (instanceIds.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "At least one instance ID is required"));
      return;
    }

    // Execute restart operations concurrently using async
    // Restart = stop then start
    std::vector<std::future<std::pair<std::string, bool>>> futures;
    for (const auto &instanceId : instanceIds) {
      futures.push_back(std::async(
          std::launch::async,
          [this, instanceId]() -> std::pair<std::string, bool> {
            try {
              // First stop the instance
              bool stopSuccess = instance_manager_->stopInstance(instanceId);
              if (!stopSuccess) {
                return {instanceId, false};
              }

              // Wait a moment for cleanup
              std::this_thread::sleep_for(std::chrono::milliseconds(500));

              // Then start the instance
              bool startSuccess = instance_manager_->startInstance(
                  instanceId, true); // skipAutoStop=true
              return {instanceId, startSuccess};
            } catch (...) {
              return {instanceId, false};
            }
          }));
    }

    // Collect results
    Json::Value response;
    Json::Value results(Json::arrayValue);
    int successCount = 0;
    int failureCount = 0;

    for (auto &future : futures) {
      auto [instanceId, success] = future.get();

      Json::Value result;
      result["instanceId"] = instanceId;
      result["success"] = success;

      if (success) {
        auto optInfo = instance_manager_->getInstance(instanceId);
        if (optInfo.has_value()) {
          result["status"] = "restarted";
          result["running"] = optInfo.value().running;
        }
        successCount++;
      } else {
        result["status"] = "failed";
        result["error"] = "Could not restart instance. Check if instance "
                          "exists and has a pipeline.";
        failureCount++;
      }

      results.append(result);
    }

    response["results"] = results;
    response["total"] = static_cast<int>(instanceIds.size());
    response["success"] = successCount;
    response["failed"] = failureCount;
    response["message"] = "Batch restart operation completed";

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    std::cerr << "[InstanceHandler] Exception in batchRestartInstances: "
              << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    std::cerr << "[InstanceHandler] Unknown exception in batchRestartInstances"
              << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

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

bool InstanceHandler::setNestedJsonValue(Json::Value &root,
                                         const std::string &path,
                                         const Json::Value &value) const {
  if (path.empty()) {
    return false;
  }

  // Split path by "/"
  std::vector<std::string> parts;
  std::istringstream iss(path);
  std::string part;
  while (std::getline(iss, part, '/')) {
    if (!part.empty()) {
      parts.push_back(part);
    }
  }

  if (parts.empty()) {
    return false;
  }

  // Navigate/create nested structure
  Json::Value *current = &root;
  for (size_t i = 0; i < parts.size() - 1; ++i) {
    const std::string &key = parts[i];
    if (!current->isMember(key) || !(*current)[key].isObject()) {
      (*current)[key] = Json::Value(Json::objectValue);
    }
    current = &((*current)[key]);
  }

  // Set the final value
  (*current)[parts.back()] = value;
  return true;
}

void InstanceHandler::setConfig(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO
        << "[API] POST /v1/core/instance/{instanceId}/config - Set config";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/config - "
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
        PLOG_WARNING << "[API] POST /v1/core/instance/{instanceId}/config - "
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
                     << "/config - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Validate required fields
    if (!json->isMember("path") || !(*json)["path"].isString()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/config - Error: Missing or invalid 'path' field";
      }
      callback(createErrorResponse(
          400, "Bad request", "Field 'path' is required and must be a string"));
      return;
    }

    if (!json->isMember("jsonValue") || !(*json)["jsonValue"].isString()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/config - Error: Missing or invalid 'jsonValue' field";
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
                     << "/config - Error: Path cannot be empty";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Field 'path' cannot be empty"));
      return;
    }

    // Validate jsonValue is not empty
    if (jsonValueStr.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/config - Error: jsonValue cannot be empty";
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
                     << "/config - Error: Invalid JSON in jsonValue: "
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
                     << "/config - Instance not found - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(404, "Instance not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Build partial config JSON with only the path to update
    // This will be merged with existing config by updateInstanceFromConfig
    Json::Value partialConfig(Json::objectValue);
    if (!setNestedJsonValue(partialConfig, path, parsedValue)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/config - Error: Failed to set value at path: "
                     << path;
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to set value at path: " + path));
      return;
    }

    // Update instance using updateInstanceFromConfig
    // This will merge partialConfig with existing config
    if (instance_manager_->updateInstanceFromConfig(instanceId,
                                                    partialConfig)) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                  << "/config - Success: Set " << path << " - "
                  << duration.count() << "ms";
      }

      // Return 204 No Content (similar to setInstanceInput)
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
                     << "/config - Failed to update - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to update instance configuration"));
    }

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR
          << "[API] POST /v1/core/instance/{instanceId}/config - Exception: "
          << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/config - "
                    "Unknown exception - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void InstanceHandler::loadInstance(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  // Get instance ID from path parameter
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
              << "/load - Load instance";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                   << "/load - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/{id}/load - Error: "
                        "Instance ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Check if instance exists
    if (!instance_manager_->hasInstance(instanceId)) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/load - Not found - " << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Load instance
    if (!instance_manager_->loadInstance(instanceId)) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      // Always log error even if API logging is disabled
      std::cerr << "[API] POST /v1/core/instance/" << instanceId
                << "/load - Cannot load instance (check server logs for details) - "
                << duration.count() << "ms" << std::endl;
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/load - Cannot load - " << duration.count() << "ms";
      }
      callback(createErrorResponse(406, "Not Acceptable",
                                   "Cannot load instance: " + instanceId + 
                                   " (instance may already be loaded or worker failed to spawn - check server logs)"));
      return;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                << "/load - Success - " << duration.count() << "ms";
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
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                 << "/load - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                 << "/load - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void InstanceHandler::unloadInstance(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  // Get instance ID from path parameter
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
              << "/unload - Unload instance";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                   << "/unload - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/{id}/unload - Error: "
                        "Instance ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Check if instance exists
    if (!instance_manager_->hasInstance(instanceId)) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/unload - Not found - " << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Unload instance
    if (!instance_manager_->unloadInstance(instanceId)) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/unload - Instance not loaded - " << duration.count()
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
                << "/unload - Success - " << duration.count() << "ms";
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
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                 << "/unload - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                 << "/unload - Unknown exception - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void InstanceHandler::getInstanceState(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  // Get instance ID from path parameter
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
              << "/state - Get instance state";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                   << "/state - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/{id}/state - Error: "
                        "Instance ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Check if instance exists
    if (!instance_manager_->hasInstance(instanceId)) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId
                     << "/state - Not found - " << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Check if instance is loaded
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId
                     << "/state - Instance not found - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Check if instance is loaded or running
    // If instance is running, it's definitely loaded
    // If instance is not running, check loaded flag
    InstanceInfo info = optInfo.value();
    if (!info.loaded && !info.running) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId
                     << "/state - Instance not loaded - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(406, "Not Acceptable",
                                   "Instance not loaded: " + instanceId));
      return;
    }

    // Get state
    Json::Value state = instance_manager_->getInstanceState(instanceId);

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
                << "/state - Success - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(state));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << "/state - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << "/state - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

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

void InstanceHandler::configureRtspOutput(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/core/instance/{instanceId}/output/rtsp - "
                   "Configure RTSP output";
    }

    // Check if instance manager is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/output/rtsp - "
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
        PLOG_WARNING << "[API] POST /v1/core/instance/{instanceId}/output/rtsp - "
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
                     << "/output/rtsp - Instance not found";
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
                     << "/output/rtsp - Invalid JSON";
      }
      callback(createErrorResponse(400, "Bad request", "Invalid JSON body"));
      return;
    }

    // Check enabled field
    if (!json->isMember("enabled") || !(*json)["enabled"].isBool()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/output/rtsp - Missing 'enabled' field";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Field 'enabled' is required"));
      return;
    }

    bool enabled = (*json)["enabled"].asBool();

    if (enabled) {
      // Get URI from request (optional)
      std::string rtspUri;
      if (json->isMember("uri") && (*json)["uri"].isString()) {
        rtspUri = (*json)["uri"].asString();
      } else {
        // Generate default RTSP URI
        std::string host = EnvConfig::getString("API_HOST", "localhost");
        rtspUri = "rtsp://" + host + ":8554/stream";
      }

      // Validate URI format
      if (rtspUri.find("rtsp://") != 0) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                       << "/output/rtsp - Invalid URI format";
        }
        callback(createErrorResponse(400, "Bad request",
                                     "URI must start with rtsp://"));
        return;
      }

      // Configure RTSP output
      // Use same structure as /output/stream endpoint
      Json::Value streamConfig(Json::objectValue);
      streamConfig["enabled"] = true;

      // Set URI in AdditionalParams (same as /output/stream does)
      if (!streamConfig.isMember("AdditionalParams")) {
        streamConfig["AdditionalParams"] = Json::Value(Json::objectValue);
      }
      streamConfig["AdditionalParams"]["RTMP_URL"] = rtspUri;
      streamConfig["AdditionalParams"]["RTSP_URI"] = rtspUri;

      // Update instance config
      if (!instance_manager_->updateInstanceFromConfig(instanceId,
                                                        streamConfig)) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                       << "/output/rtsp - Could not set RTSP output";
        }
        callback(createErrorResponse(500, "Internal Server Error",
                                     "Failed to configure RTSP output"));
        return;
      }

      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k204NoContent);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                  << "/output/rtsp - Success: " << rtspUri;
      }
    } else {
      // Disable RTSP output
      Json::Value streamConfig(Json::objectValue);
      streamConfig["enabled"] = false;

      instance_manager_->updateInstanceFromConfig(instanceId, streamConfig);

      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k204NoContent);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                  << "/output/rtsp - Disabled";
      }
    }

  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/output/rtsp - "
                    "Exception: "
                 << e.what();
    }
    auto resp = createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (...) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/output/rtsp - "
                    "Unknown exception";
    }
    auto resp = createErrorResponse(500, "Internal server error",
                                   "Unknown error occurred");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void InstanceHandler::pushEncodedFrame(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);
  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/instance/{instanceId}/push/encoded/{codecId} - Push encoded frame";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if registry is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/push/encoded/{codecId} - "
                      "Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance registry not initialized"));
      return;
    }

    // Get instance ID and codec ID from path parameters
    std::string instanceId = extractInstanceId(req);
    std::string codecId = req->getParameter("codecId");

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/{instanceId}/push/encoded/{codecId} - "
                        "Error: Instance ID is empty";
      }
      callback(createErrorResponse(400, "Bad request", "Instance ID is required"));
      return;
    }

    if (codecId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/push/encoded/{codecId} - Error: Codec ID is empty";
      }
      callback(createErrorResponse(400, "Bad request", "Codec ID is required"));
      return;
    }

    // Validate codec
    auto& codecManager = CodecManager::getInstance();
    std::string normalizedCodec = codecManager.normalizeCodecId(codecId);
    if (!codecManager.isCodecSupported(normalizedCodec)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/push/encoded/" << codecId << " - Unsupported codec";
      }
      callback(createErrorResponse(400, "Unsupported Codec",
                                   "Codec '" + codecId + "' is not supported. Supported codecs: h264, h265"));
      return;
    }

    // Check if instance exists
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/push/encoded/" << codecId << " - Instance not found";
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
                     << "/push/encoded/" << codecId << " - Instance is not running";
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
                     << "/push/encoded/" << codecId << " - Invalid content type";
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

    // Subprocess: push encoded (H.264/H.265) not supported in worker
    if (instance_manager_->isSubprocessMode()) {
      callback(createErrorResponse(
          501, "Not Implemented",
          "Push encoded frame (H.264/H.265) in subprocess mode is not supported. "
          "Use in-process mode or push compressed (JPEG/PNG) frames."));
      return;
    }

    // Use current timestamp if not provided
    if (timestamp == 0) {
      timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Lightweight validation only. Decode happens once in FrameProcessor
    // (same as compressed path) to avoid double decode and lag.
    if (frameData.empty()) {
      callback(createErrorResponse(400, "Bad Request", "Encoded frame data is empty"));
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

    FrameData frameDataObj(FrameType::ENCODED, normalizedCodec, frameData, timestamp);
    if (!queue.push(frameDataObj)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/push/encoded/" << codecId << " - Queue is full";
      }
      callback(createErrorResponse(500, "Internal Server Error",
                                   "Frame queue is full"));
      return;
    }

    // FrameProcessor will decode once and push to app_src node.

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                << "/push/encoded/" << codecId << " - Success - " 
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
      PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/push/encoded/{codecId} - "
                    "Exception: " << e.what();
    }
    auto resp = createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (...) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/{instanceId}/push/encoded/{codecId} - "
                    "Unknown exception";
    }
    auto resp = createErrorResponse(500, "Internal server error",
                                   "Unknown error occurred");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

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
