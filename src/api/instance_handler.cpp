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
  auto sanitizeInstanceId = [](std::string id) -> std::string {
    auto trim = [](std::string s) -> std::string {
      size_t f = s.find_first_not_of(" \t\n\r\f\v");
      if (f == std::string::npos) {
        return "";
      }
      size_t l = s.find_last_not_of(" \t\n\r\f\v");
      return s.substr(f, l - f + 1);
    };
    id = trim(std::move(id));
    // Copy-paste from JSON / malformed clients sometimes leave a stray quote.
    while (!id.empty() && (id.front() == '"' || id.front() == '\'')) {
      id.erase(0, 1);
    }
    while (!id.empty() && (id.back() == '"' || id.back() == '\'')) {
      id.pop_back();
    }
    return trim(std::move(id));
  };

  // Try getParameter first (standard way)
  std::string instanceId = sanitizeInstanceId(req->getParameter("instanceId"));

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
      instanceId = sanitizeInstanceId(path.substr(start, end - start));
    } else {
      // Try /instance/ pattern (singular)
      size_t instancePos = path.find("/instance/");
      if (instancePos != std::string::npos) {
        size_t start = instancePos + 10; // length of "/instance/"
        size_t end = path.find("/", start);
        if (end == std::string::npos) {
          end = path.length();
        }
        instanceId = sanitizeInstanceId(path.substr(start, end - start));
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

