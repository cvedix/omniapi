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

