#include "api/securt_handler.h"
#include "core/analytics_entities_manager.h"
#include "core/exclusion_area_manager.h"
#include "core/logging_flags.h"
#include "core/logger.h"
#include "core/metrics_interceptor.h"
#include "core/securt_feature_config.h"
#include "core/securt_feature_manager.h"
#include "core/securt_instance.h"
#include "core/securt_instance_manager.h"
#include "instances/instance_info.h"
#include "instances/instance_manager.h"
#include <algorithm>
#include <chrono>
#include <drogon/HttpResponse.h>
#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

SecuRTInstanceManager *SecuRTHandler::instance_manager_ = nullptr;
AnalyticsEntitiesManager *SecuRTHandler::analytics_entities_manager_ = nullptr;
SecuRTFeatureManager *SecuRTHandler::feature_manager_ = nullptr;
ExclusionAreaManager *SecuRTHandler::exclusion_area_manager_ = nullptr;
IInstanceManager *SecuRTHandler::core_instance_manager_ = nullptr;

void SecuRTHandler::setInstanceManager(SecuRTInstanceManager *manager) {
  instance_manager_ = manager;
}

void SecuRTHandler::setAnalyticsEntitiesManager(
    AnalyticsEntitiesManager *manager) {
  analytics_entities_manager_ = manager;
}

void SecuRTHandler::setFeatureManager(SecuRTFeatureManager *manager) {
  feature_manager_ = manager;
}

void SecuRTHandler::setExclusionAreaManager(ExclusionAreaManager *manager) {
  exclusion_area_manager_ = manager;
}

void SecuRTHandler::setCoreInstanceManager(IInstanceManager *manager) {
  core_instance_manager_ = manager;
}

void SecuRTHandler::createInstance(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/instance - Create SecuRT instance";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
    PLOG_DEBUG << "[API] Request path: " << req->getPath();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/securt/instance - Error: Instance manager "
                      "not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] POST /v1/securt/instance - Error: Invalid JSON body";
        PLOG_DEBUG << "[API] Request body: " << req->getBody();
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    if (isApiLoggingEnabled()) {
      Json::StreamWriterBuilder builder;
      builder["indentation"] = "";
      std::string jsonStr = Json::writeString(builder, *json);
      PLOG_DEBUG << "[API] POST /v1/securt/instance - Request body: " << jsonStr;
    }

    // Parse request - instanceId is optional
    std::string instanceId;
    if (json->isMember("instanceId") && (*json)["instanceId"].isString()) {
      instanceId = (*json)["instanceId"].asString();
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[API] POST /v1/securt/instance - InstanceId from request: " << instanceId;
      }
    } else {
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[API] POST /v1/securt/instance - No instanceId provided, will generate";
      }
    }

    // Parse SecuRTInstanceWrite
    SecuRTInstanceWrite write = SecuRTInstanceWrite::fromJson(*json);
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] POST /v1/securt/instance - Parsed instance data - name: " << write.name
                 << ", detectorMode: " << write.detectorMode;
    }

    // Use default name if not provided
    if (write.name.empty()) {
      write.name = "SecuRT Instance " + (instanceId.empty() ? "" : instanceId.substr(0, 8));
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[API] POST /v1/securt/instance - No name provided, using default: " << write.name;
      }
    }

    // Create instance
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] POST /v1/securt/instance - Calling instance_manager_->createInstance()";
    }
    std::string createdId = instance_manager_->createInstance(instanceId, write);

    if (createdId.empty()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/securt/instance - Instance already exists "
                        "or creation failed - "
                     << duration.count() << "ms";
        if (!instanceId.empty()) {
          PLOG_DEBUG << "[API] POST /v1/securt/instance - Requested instanceId: " << instanceId;
        }
      }
      callback(createErrorResponse(409, "Conflict",
                                   "Instance already exists or creation failed"));
      return;
    }

    // Build response
    Json::Value response;
    response["instanceId"] = createdId;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/securt/instance - Success: Created instance "
                << createdId << " - " << duration.count() << "ms";
      Json::StreamWriterBuilder builder;
      builder["indentation"] = "";
      std::string responseStr = Json::writeString(builder, response);
      PLOG_DEBUG << "[API] POST /v1/securt/instance - Response: " << responseStr;
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/securt/instance - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/securt/instance - Unknown exception - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void SecuRTHandler::createInstanceWithId(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] PUT /v1/securt/instance/" << instanceId
              << " - Create SecuRT instance with ID";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
    PLOG_DEBUG << "[API] Request path: " << req->getPath();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] PUT /v1/securt/instance/" << instanceId
                   << " - Error: Instance manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/securt/instance/{instanceId} - Error: Instance ID is required";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/securt/instance/" << instanceId
                     << " - Error: Invalid JSON body";
        PLOG_DEBUG << "[API] Request body: " << req->getBody();
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    if (isApiLoggingEnabled()) {
      Json::StreamWriterBuilder builder;
      builder["indentation"] = "";
      std::string jsonStr = Json::writeString(builder, *json);
      PLOG_DEBUG << "[API] PUT /v1/securt/instance/" << instanceId
                 << " - Request body: " << jsonStr;
    }

    // Parse SecuRTInstanceWrite
    SecuRTInstanceWrite write = SecuRTInstanceWrite::fromJson(*json);
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] PUT /v1/securt/instance/" << instanceId
                 << " - Parsed instance data - name: " << write.name
                 << ", detectorMode: " << write.detectorMode;
    }

    // Use default name if not provided
    if (write.name.empty()) {
      write.name = "SecuRT Instance " + instanceId.substr(0, 8);
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[API] PUT /v1/securt/instance/" << instanceId
                   << " - No name provided, using default: " << write.name;
      }
    }

    // Create instance with ID
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] PUT /v1/securt/instance/" << instanceId
                 << " - Calling instance_manager_->createInstance()";
    }
    std::string createdId = instance_manager_->createInstance(instanceId, write);

    if (createdId.empty()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/securt/instance/" << instanceId
                     << " - Instance already exists - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(409, "Conflict",
                                   "Instance already exists"));
      return;
    }

    // Build response
    Json::Value response;
    response["instanceId"] = createdId;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] PUT /v1/securt/instance/" << instanceId
                << " - Success: Created instance " << createdId
                << " - " << duration.count() << "ms";
      Json::StreamWriterBuilder builder;
      builder["indentation"] = "";
      std::string responseStr = Json::writeString(builder, response);
      PLOG_DEBUG << "[API] PUT /v1/securt/instance/" << instanceId
                 << " - Response: " << responseStr;
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);

    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PUT /v1/securt/instance/" << instanceId
                 << " - Exception: " << e.what() << " - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void SecuRTHandler::updateInstance(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] PATCH /v1/securt/instance/" << instanceId
              << " - Update SecuRT instance";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
    PLOG_DEBUG << "[API] Request path: " << req->getPath();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] PATCH /v1/securt/instance/" << instanceId
                   << " - Error: Instance manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PATCH /v1/securt/instance/{instanceId} - Error: Instance ID is required";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Check if instance exists
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] PATCH /v1/securt/instance/" << instanceId
                 << " - Checking if instance exists";
    }
    if (!instance_manager_->hasInstance(instanceId)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PATCH /v1/securt/instance/" << instanceId
                     << " - Error: Instance does not exist";
      }
      callback(createErrorResponse(404, "Not Found",
                                   "Instance does not exist"));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PATCH /v1/securt/instance/" << instanceId
                     << " - Error: Invalid JSON body";
        PLOG_DEBUG << "[API] Request body: " << req->getBody();
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    if (isApiLoggingEnabled()) {
      Json::StreamWriterBuilder builder;
      builder["indentation"] = "";
      std::string jsonStr = Json::writeString(builder, *json);
      PLOG_DEBUG << "[API] PATCH /v1/securt/instance/" << instanceId
                 << " - Request body: " << jsonStr;
    }

    // Parse SecuRTInstanceWrite
    SecuRTInstanceWrite write = SecuRTInstanceWrite::fromJson(*json);
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] PATCH /v1/securt/instance/" << instanceId
                 << " - Parsed update data - name: " << write.name;
    }

    // Update instance
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] PATCH /v1/securt/instance/" << instanceId
                 << " - Calling instance_manager_->updateInstance()";
    }
    bool success = instance_manager_->updateInstance(instanceId, write);

    if (!success) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PATCH /v1/securt/instance/" << instanceId
                     << " - Error: Update failed - instance does not exist";
      }
      callback(createErrorResponse(404, "Not Found",
                                   "Instance does not exist"));
      return;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] PATCH /v1/securt/instance/" << instanceId
                << " - Success: Updated instance - " << duration.count() << "ms";
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);

    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PATCH, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PATCH /v1/securt/instance/" << instanceId
                 << " - Exception: " << e.what() << " - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PATCH /v1/securt/instance/" << instanceId
                 << " - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void SecuRTHandler::deleteInstance(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] DELETE /v1/securt/instance/" << instanceId
              << " - Delete SecuRT instance";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
    PLOG_DEBUG << "[API] Request path: " << req->getPath();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/securt/instance/" << instanceId
                   << " - Error: Instance manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/securt/instance/{instanceId} - Error: Instance ID is required";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Check if instance exists
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] DELETE /v1/securt/instance/" << instanceId
                 << " - Checking if instance exists";
    }
    if (!instance_manager_->hasInstance(instanceId)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/securt/instance/" << instanceId
                     << " - Error: Instance does not exist";
      }
      callback(createErrorResponse(404, "Not Found",
                                   "Instance does not exist"));
      return;
    }

    // Delete instance
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] DELETE /v1/securt/instance/" << instanceId
                 << " - Calling instance_manager_->deleteInstance()";
    }
    bool success = instance_manager_->deleteInstance(instanceId);

    if (!success) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/securt/instance/" << instanceId
                     << " - Error: Delete failed - instance does not exist";
      }
      callback(createErrorResponse(404, "Not Found",
                                   "Instance does not exist"));
      return;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] DELETE /v1/securt/instance/" << instanceId
                << " - Success: Deleted instance - " << duration.count() << "ms";
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);

    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "DELETE, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/securt/instance/" << instanceId
                 << " - Exception: " << e.what() << " - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/securt/instance/" << instanceId
                 << " - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void SecuRTHandler::getInstanceStats(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/securt/instance/" << instanceId
              << "/stats - Get instance statistics";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
    PLOG_DEBUG << "[API] Request path: " << req->getPath();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/securt/instance/" << instanceId
                   << "/stats - Error: Instance manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/securt/instance/{instanceId}/stats - Error: Instance ID is required";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Check if instance exists
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] GET /v1/securt/instance/" << instanceId
                 << "/stats - Checking if instance exists";
    }
    if (!instance_manager_->hasInstance(instanceId)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/securt/instance/" << instanceId
                     << "/stats - Error: Instance does not exist";
      }
      callback(createErrorResponse(404, "Not Found",
                                   "Instance does not exist"));
      return;
    }

    // Get statistics
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] GET /v1/securt/instance/" << instanceId
                 << "/stats - Calling instance_manager_->getStatistics()";
    }
    SecuRTInstanceStats stats = instance_manager_->getStatistics(instanceId);

    // Build response
    Json::Value response = stats.toJson();

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/securt/instance/" << instanceId
                << "/stats - Success - " << duration.count() << "ms";
      Json::StreamWriterBuilder builder;
      builder["indentation"] = "";
      std::string responseStr = Json::writeString(builder, response);
      PLOG_DEBUG << "[API] GET /v1/securt/instance/" << instanceId
                 << "/stats - Response: " << responseStr;
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/securt/instance/" << instanceId
                 << "/stats - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/securt/instance/" << instanceId
                 << "/stats - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void SecuRTHandler::getAnalyticsEntities(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/securt/instance/" << instanceId
              << "/analytics_entities - Get analytics entities";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
    PLOG_DEBUG << "[API] Request path: " << req->getPath();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/securt/instance/" << instanceId
                   << "/analytics_entities - Error: Instance manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (!analytics_entities_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/securt/instance/" << instanceId
                   << "/analytics_entities - Error: Analytics entities manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Analytics entities manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/securt/instance/{instanceId}/analytics_entities - Error: Instance ID is required";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    // Check if instance exists
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] GET /v1/securt/instance/" << instanceId
                 << "/analytics_entities - Checking if instance exists";
    }
    if (!instance_manager_->hasInstance(instanceId)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/securt/instance/" << instanceId
                     << "/analytics_entities - Error: Instance does not exist";
      }
      callback(createErrorResponse(404, "Not Found",
                                   "Instance does not exist"));
      return;
    }

    // Get analytics entities
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] GET /v1/securt/instance/" << instanceId
                 << "/analytics_entities - Calling analytics_entities_manager_->getAnalyticsEntities()";
    }
    Json::Value response = analytics_entities_manager_->getAnalyticsEntities(instanceId);

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/securt/instance/" << instanceId
                << "/analytics_entities - Success - " << duration.count() << "ms";
      Json::StreamWriterBuilder builder;
      builder["indentation"] = "";
      std::string responseStr = Json::writeString(builder, response);
      PLOG_DEBUG << "[API] GET /v1/securt/instance/" << instanceId
                 << "/analytics_entities - Response size: " << responseStr.length() << " chars";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/securt/instance/" << instanceId
                 << "/analytics_entities - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void SecuRTHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                   "GET, POST, PUT, PATCH, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
  resp->addHeader("Access-Control-Max-Age", "3600");

  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

std::string SecuRTHandler::extractInstanceId(const HttpRequestPtr &req) const {
  // Try getParameter first (standard way for path parameters)
  std::string instanceId = req->getParameter("instanceId");

  // Fallback: extract from path if getParameter doesn't work
  if (instanceId.empty()) {
    std::string path = req->getPath();
    // Try /securt/instance/ pattern
    size_t instancePos = path.find("/securt/instance/");
    if (instancePos != std::string::npos) {
      size_t start = instancePos + 17; // length of "/securt/instance/"
      size_t end = path.find("/", start);
      if (end == std::string::npos) {
        end = path.length();
      }
      instanceId = path.substr(start, end - start);
    }
  }

  return instanceId;
}

HttpResponsePtr SecuRTHandler::createErrorResponse(int statusCode,
                                                    const std::string &error,
                                                    const std::string &message) const {
  Json::Value json;
  json["error"] = error;
  if (!message.empty()) {
    json["message"] = message;
  }

  auto resp = HttpResponse::newHttpJsonResponse(json);
  resp->setStatusCode(static_cast<drogon::HttpStatusCode>(statusCode));

  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                   "GET, POST, PUT, PATCH, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

  return resp;
}

HttpResponsePtr SecuRTHandler::createSuccessResponse(const Json::Value &data,
                                                      int statusCode) const {
  auto resp = HttpResponse::newHttpJsonResponse(data);
  resp->setStatusCode(static_cast<drogon::HttpStatusCode>(statusCode));

  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                   "GET, POST, PUT, PATCH, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

  return resp;
}

// Advanced features implementations

void SecuRTHandler::setMotionArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/instance/" << instanceId
              << "/motion_area - Set motion area";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!feature_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Feature manager not initialized"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("coordinates") ||
        !(*json)["coordinates"].isArray()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request must contain 'coordinates' array"));
      return;
    }

    std::vector<Coordinate> coordinates;
    for (const auto &coord : (*json)["coordinates"]) {
      coordinates.push_back(Coordinate::fromJson(coord));
    }

    if (!feature_manager_->setMotionArea(instanceId, coordinates)) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Invalid coordinates"));
      return;
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::setFeatureExtraction(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/instance/" << instanceId
              << "/feature_extraction - Set feature extraction";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!feature_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Feature manager not initialized"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("types") || !(*json)["types"].isArray()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request must contain 'types' array"));
      return;
    }

    std::vector<std::string> types;
    for (const auto &type : (*json)["types"]) {
      if (type.isString()) {
        types.push_back(type.asString());
      }
    }

    if (!feature_manager_->setFeatureExtraction(instanceId, types)) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Invalid feature extraction types"));
      return;
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::getFeatureExtraction(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/securt/instance/" << instanceId
              << "/feature_extraction - Get feature extraction";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!feature_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Feature manager not initialized"));
      return;
    }

    auto types = feature_manager_->getFeatureExtraction(instanceId);
    Json::Value response;
    Json::Value typesArray(Json::arrayValue);
    if (types.has_value()) {
      for (const auto &type : types.value()) {
        typesArray.append(type);
      }
    }
    response["types"] = typesArray;

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::setAttributesExtraction(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/instance/" << instanceId
              << "/attributes_extraction - Set attributes extraction";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!feature_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Feature manager not initialized"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("mode") || !(*json)["mode"].isString()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request must contain 'mode' string"));
      return;
    }

    std::string mode = (*json)["mode"].asString();
    if (!feature_manager_->setAttributesExtraction(instanceId, mode)) {
      callback(createErrorResponse(400, "Invalid request", "Invalid mode"));
      return;
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::getAttributesExtraction(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/securt/instance/" << instanceId
              << "/attributes_extraction - Get attributes extraction";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!feature_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Feature manager not initialized"));
      return;
    }

    auto mode = feature_manager_->getAttributesExtraction(instanceId);
    Json::Value response;
    response["mode"] = mode.value_or("Off");

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::setPerformanceProfile(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/instance/" << instanceId
              << "/performance_profile - Set performance profile";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!feature_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Feature manager not initialized"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("profile") ||
        !(*json)["profile"].isString()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request must contain 'profile' string"));
      return;
    }

    std::string profile = (*json)["profile"].asString();
    if (!feature_manager_->setPerformanceProfile(instanceId, profile)) {
      callback(createErrorResponse(400, "Invalid request", "Invalid profile"));
      return;
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::getPerformanceProfile(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/securt/instance/" << instanceId
              << "/performance_profile - Get performance profile";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!feature_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Feature manager not initialized"));
      return;
    }

    auto profile = feature_manager_->getPerformanceProfile(instanceId);
    Json::Value response;
    response["profile"] = profile.value_or("Balanced");

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::setFaceDetection(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/instance/" << instanceId
              << "/face_detection - Set face detection";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!feature_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Feature manager not initialized"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("enable") || !(*json)["enable"].isBool()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request must contain 'enable' boolean"));
      return;
    }

    bool enable = (*json)["enable"].asBool();
    feature_manager_->setFaceDetection(instanceId, enable);

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::setLPR(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/instance/" << instanceId
              << "/lpr - Set LPR";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!feature_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Feature manager not initialized"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("enable") || !(*json)["enable"].isBool()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request must contain 'enable' boolean"));
      return;
    }

    bool enable = (*json)["enable"].asBool();
    feature_manager_->setLPR(instanceId, enable);

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::getLPR(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/securt/instance/" << instanceId << "/lpr - Get LPR";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!feature_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Feature manager not initialized"));
      return;
    }

    auto enabled = feature_manager_->getLPR(instanceId);
    Json::Value response;
    response["enabled"] = enabled.value_or(false);

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::setPIP(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/instance/" << instanceId
              << "/pip - Set PIP";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!feature_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Feature manager not initialized"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("enable") || !(*json)["enable"].isBool()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request must contain 'enable' boolean"));
      return;
    }

    bool enable = (*json)["enable"].asBool();
    feature_manager_->setPIP(instanceId, enable);

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::getPIP(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/securt/instance/" << instanceId << "/pip - Get PIP";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!feature_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Feature manager not initialized"));
      return;
    }

    auto enabled = feature_manager_->getPIP(instanceId);
    Json::Value response;
    response["enabled"] = enabled.value_or(false);

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::setSurrenderDetection(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/experimental/instance/" << instanceId
              << "/surrender_detection - Set surrender detection";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!feature_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Feature manager not initialized"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("enable") || !(*json)["enable"].isBool()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request must contain 'enable' boolean"));
      return;
    }

    bool enable = (*json)["enable"].asBool();
    feature_manager_->setSurrenderDetection(instanceId, enable);

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::getSurrenderDetection(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/securt/experimental/instance/" << instanceId
              << "/surrender_detection - Get surrender detection";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!feature_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Feature manager not initialized"));
      return;
    }

    auto enabled = feature_manager_->getSurrenderDetection(instanceId);
    Json::Value response;
    response["enabled"] = enabled.value_or(false);

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::setMaskingAreas(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/instance/" << instanceId
              << "/masking_areas - Set masking areas";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!feature_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Feature manager not initialized"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("areas") || !(*json)["areas"].isArray()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request must contain 'areas' array"));
      return;
    }

    std::vector<std::vector<Coordinate>> areas;
    for (const auto &area : (*json)["areas"]) {
      if (!area.isArray()) {
        callback(createErrorResponse(400, "Invalid request",
                                     "Each area must be an array"));
        return;
      }
      std::vector<Coordinate> coords;
      for (const auto &coord : area) {
        coords.push_back(Coordinate::fromJson(coord));
      }
      areas.push_back(coords);
    }

    if (!feature_manager_->setMaskingAreas(instanceId, areas)) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Invalid masking areas"));
      return;
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::addExclusionArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/instance/" << instanceId
              << "/exclusion_areas - Add exclusion area";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!exclusion_area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Exclusion area manager not initialized"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    ExclusionArea area = ExclusionArea::fromJson(*json);
    if (!ExclusionAreaManager::validateExclusionArea(area)) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Invalid exclusion area"));
      return;
    }

    if (!exclusion_area_manager_->addExclusionArea(instanceId, area)) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to add exclusion area"));
      return;
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::getExclusionAreas(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/securt/instance/" << instanceId
              << "/exclusion_areas - Get exclusion areas";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!exclusion_area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Exclusion area manager not initialized"));
      return;
    }

    auto areas = exclusion_area_manager_->getExclusionAreas(instanceId);
    Json::Value response(Json::arrayValue);
    for (const auto &area : areas) {
      response.append(area.toJson());
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::deleteExclusionAreas(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] DELETE /v1/securt/instance/" << instanceId
              << "/exclusion_areas - Delete exclusion areas";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!exclusion_area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Exclusion area manager not initialized"));
      return;
    }

    exclusion_area_manager_->deleteExclusionAreas(instanceId);

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::setInput(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/instance/" << instanceId
              << "/input - Set input source";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!core_instance_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Core instance manager not initialized"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Validate required fields
    if (!json->isMember("type") || !(*json)["type"].isString()) {
      callback(createErrorResponse(400, "Invalid request",
                                    "Field 'type' is required and must be a string"));
      return;
    }

    if (!json->isMember("uri") || !(*json)["uri"].isString()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Field 'uri' is required and must be a string"));
      return;
    }

    std::string type = (*json)["type"].asString();
    std::string uri = (*json)["uri"].asString();

    // Validate type
    if (type != "File" && type != "RTSP" && type != "RTMP" && type != "HLS") {
      callback(createErrorResponse(
          400, "Invalid request",
          "Field 'type' must be one of: File, RTSP, RTMP, HLS"));
      return;
    }

    // Validate uri is not empty
    if (uri.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Field 'uri' cannot be empty"));
      return;
    }

    // Check if instance exists in core manager
    auto optInfo = core_instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      callback(createErrorResponse(404, "Not Found",
                                   "Instance not found in core manager"));
      return;
    }

    // Build Input configuration JSON
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
    if (type == "File") {
      input["media_type"] = "File";
      input["uri"] = uri;
    } else if (type == "RTSP") {
      input["media_type"] = "IP Camera";
      
      // Get decoder name and transport from additionalParams if provided
      std::string decoderName = "avdec_h264";
      std::string rtspTransport = "";
      bool useUrisourcebin = false;

      if (json->isMember("additionalParams") &&
          (*json)["additionalParams"].isObject()) {
        const Json::Value &additionalParams = (*json)["additionalParams"];
        if (additionalParams.isMember("GST_DECODER_NAME") &&
            additionalParams["GST_DECODER_NAME"].isString()) {
          decoderName = additionalParams["GST_DECODER_NAME"].asString();
          if (decoderName == "decodebin") {
            useUrisourcebin = true;
          }
        }
        if (additionalParams.isMember("RTSP_TRANSPORT") &&
            additionalParams["RTSP_TRANSPORT"].isString()) {
          rtspTransport = additionalParams["RTSP_TRANSPORT"].asString();
        }
        if (additionalParams.isMember("USE_URISOURCEBIN") &&
            additionalParams["USE_URISOURCEBIN"].isString()) {
          useUrisourcebin = (additionalParams["USE_URISOURCEBIN"].asString() == "true" ||
                            additionalParams["USE_URISOURCEBIN"].asString() == "1");
        }
      }

      if (useUrisourcebin) {
        input["uri"] = "gstreamer:///urisourcebin uri=" + uri +
                       " ! decodebin ! videoconvert ! video/x-raw, format=NV12 "
                       "! appsink drop=true name=cvdsink";
      } else {
        std::string protocolsParam = "";
        if (!rtspTransport.empty()) {
          std::transform(rtspTransport.begin(), rtspTransport.end(),
                         rtspTransport.begin(), ::tolower);
          if (rtspTransport == "tcp" || rtspTransport == "udp") {
            protocolsParam = " protocols=" + rtspTransport;
          }
        }
        input["uri"] =
            "rtspsrc location=" + uri + protocolsParam +
            " ! application/x-rtp,media=video ! rtph264depay ! h264parse ! " +
            decoderName +
            " ! videoconvert ! video/x-raw,format=NV12 ! appsink drop=true "
            "name=cvdsink";
      }
    } else if (type == "RTMP") {
      input["media_type"] = "IP Camera";
      // For RTMP, use the URI directly (rtmp_src node will handle it)
      input["uri"] = uri;
    } else if (type == "HLS") {
      input["media_type"] = "IP Camera";
      // For HLS, use the URI directly (hls_src node will handle it)
      input["uri"] = uri;
    }

    inputConfig["Input"] = input;

    // Also set AdditionalParams for pipeline builder
    Json::Value additionalParams(Json::objectValue);
    if (type == "File") {
      additionalParams["FILE_PATH"] = uri;
    } else if (type == "RTSP") {
      additionalParams["RTSP_SRC_URL"] = uri;
    } else if (type == "RTMP") {
      additionalParams["RTMP_SRC_URL"] = uri;
    } else if (type == "HLS") {
      additionalParams["HLS_URL"] = uri;
    }

    // Copy additionalParams from request if provided
    if (json->isMember("additionalParams") &&
        (*json)["additionalParams"].isObject()) {
      const Json::Value &reqAdditionalParams = (*json)["additionalParams"];
      for (const auto &key : reqAdditionalParams.getMemberNames()) {
        additionalParams[key] = reqAdditionalParams[key];
      }
    }

    inputConfig["AdditionalParams"] = additionalParams;

    // Update instance using updateInstanceFromConfig
    if (core_instance_manager_->updateInstanceFromConfig(instanceId, inputConfig)) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] POST /v1/securt/instance/" << instanceId
                  << "/input - Success - " << duration.count() << "ms";
      }

      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k204NoContent);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
    } else {
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to update input settings"));
    }

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void SecuRTHandler::setOutput(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/instance/" << instanceId
              << "/output - Set output destination";
  }

  try {
    if (!instance_manager_ || !instance_manager_->hasInstance(instanceId)) {
      callback(createErrorResponse(404, "Not Found", "Instance does not exist"));
      return;
    }

    if (!core_instance_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Core instance manager not initialized"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Validate required fields
    if (!json->isMember("type") || !(*json)["type"].isString()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Field 'type' is required and must be a string"));
      return;
    }

    std::string type = (*json)["type"].asString();

    // Validate type
    if (type != "MQTT" && type != "RTMP" && type != "RTSP" && type != "HLS") {
      callback(createErrorResponse(
          400, "Invalid request",
          "Field 'type' must be one of: MQTT, RTMP, RTSP, HLS"));
      return;
    }

    // Check if instance exists in core manager
    auto optInfo = core_instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      callback(createErrorResponse(404, "Not Found",
                                   "Instance not found in core manager"));
      return;
    }

    // Build output configuration
    Json::Value outputConfig(Json::objectValue);
    Json::Value additionalParams(Json::objectValue);

    if (type == "MQTT") {
      // Validate MQTT required fields
      if (!json->isMember("broker") || !(*json)["broker"].isString()) {
        callback(createErrorResponse(400, "Invalid request",
                                     "Field 'broker' is required for MQTT output"));
        return;
      }

      std::string broker = (*json)["broker"].asString();
      std::string topic = json->isMember("topic") && (*json)["topic"].isString()
                             ? (*json)["topic"].asString()
                             : "securt/" + instanceId;
      std::string port = json->isMember("port") && (*json)["port"].isString()
                            ? (*json)["port"].asString()
                            : "1883";
      std::string username =
          json->isMember("username") && (*json)["username"].isString()
              ? (*json)["username"].asString()
              : "";
      std::string password =
          json->isMember("password") && (*json)["password"].isString()
              ? (*json)["password"].asString()
              : "";

      additionalParams["MQTT_BROKER_URL"] = broker;
      additionalParams["MQTT_PORT"] = port;
      additionalParams["MQTT_TOPIC"] = topic;
      if (!username.empty()) {
        additionalParams["MQTT_USERNAME"] = username;
      }
      if (!password.empty()) {
        additionalParams["MQTT_PASSWORD"] = password;
      }

    } else if (type == "RTMP" || type == "RTSP" || type == "HLS") {
      // Validate URI field
      if (!json->isMember("uri") || !(*json)["uri"].isString()) {
        callback(createErrorResponse(400, "Invalid request",
                                     "Field 'uri' is required for " + type +
                                         " output"));
        return;
      }

      std::string uri = (*json)["uri"].asString();

      // Validate URI format
      if (type == "RTMP" && uri.find("rtmp://") != 0) {
        callback(createErrorResponse(400, "Invalid request",
                                     "RTMP URI must start with rtmp://"));
        return;
      } else if (type == "RTSP" && uri.find("rtsp://") != 0) {
        callback(createErrorResponse(400, "Invalid request",
                                     "RTSP URI must start with rtsp://"));
        return;
      } else if (type == "HLS" && uri.find("hls://") != 0 &&
                 uri.find("http://") != 0 && uri.find("https://") != 0) {
        callback(createErrorResponse(
            400, "Invalid request",
            "HLS URI must start with hls://, http://, or https://"));
        return;
      }

      additionalParams["RTMP_URL"] = uri;
      if (type == "RTSP") {
        additionalParams["RTSP_URI"] = uri;
      } else if (type == "HLS") {
        additionalParams["HLS_URI"] = uri;
      }
    }

    outputConfig["AdditionalParams"] = additionalParams;

    // Update instance using updateInstanceFromConfig
    if (core_instance_manager_->updateInstanceFromConfig(instanceId,
                                                         outputConfig)) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] POST /v1/securt/instance/" << instanceId
                  << "/output - Success - " << duration.count() << "ms";
      }

      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k204NoContent);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
    } else {
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to update output settings"));
    }

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

