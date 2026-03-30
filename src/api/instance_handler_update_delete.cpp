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

