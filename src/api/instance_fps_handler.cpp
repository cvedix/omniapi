#include "api/instance_fps_handler.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/metrics_interceptor.h"
#include "instances/instance_info.h"
#include "instances/instance_manager.h"
#include <chrono>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <sstream>

IInstanceManager *InstanceFpsHandler::instance_manager_ = nullptr;

void InstanceFpsHandler::setInstanceManager(IInstanceManager *manager) {
  instance_manager_ = manager;
}

std::string
InstanceFpsHandler::extractInstanceId(const HttpRequestPtr &req) const {
  // Try getParameter first (standard way)
  std::string instanceId = req->getParameter("instance_id");

  // Fallback: extract from path if getParameter doesn't work
  if (instanceId.empty()) {
    std::string path = req->getPath();
    // Try /instances/ pattern
    size_t instancesPos = path.find("/instances/");
    if (instancesPos != std::string::npos) {
      size_t start = instancesPos + 11; // length of "/instances/"
      size_t end = path.find("/", start);
      if (end == std::string::npos) {
        end = path.length();
      }
      instanceId = path.substr(start, end - start);
    }
  }

  return instanceId;
}

HttpResponsePtr InstanceFpsHandler::createErrorResponse(int statusCode,
                                                        const std::string &error,
                                                        const std::string &message) const {
  Json::Value response;
  response["error"] = error;
  if (!message.empty()) {
    response["message"] = message;
  }

  auto resp = HttpResponse::newHttpJsonResponse(response);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");
  return resp;
}

HttpResponsePtr InstanceFpsHandler::createSuccessResponse(const Json::Value &data,
                                                          int statusCode) const {
  auto resp = HttpResponse::newHttpJsonResponse(data);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");
  return resp;
}

void InstanceFpsHandler::getFps(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  MetricsInterceptor::setHandlerStartTime(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /api/v1/instances/{instance_id}/fps - Get FPS configuration";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if manager is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /api/v1/instances/{instance_id}/fps - Error: Instance manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    // Extract instance ID
    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /api/v1/instances/{instance_id}/fps - Error: Missing instance_id";
      }
      callback(createErrorResponse(400, "Bad Request", "Missing instance_id parameter"));
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] GET /api/v1/instances/" << instanceId << "/fps";
    }

    // Get instance info
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /api/v1/instances/" << instanceId
                     << "/fps - Instance not found";
      }
      callback(createErrorResponse(404, "Not Found",
                                   "Instance ID does not exist"));
      return;
    }

    const InstanceInfo &info = optInfo.value();

    // Build response
    Json::Value response;
    response["instance_id"] = instanceId;
    response["fps"] = info.configuredFps;

    auto resp = createSuccessResponse(response, 200);
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /api/v1/instances/{instance_id}/fps - Exception: " << e.what();
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void InstanceFpsHandler::setFps(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  MetricsInterceptor::setHandlerStartTime(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /api/v1/instances/{instance_id}/fps - Set FPS configuration";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if manager is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /api/v1/instances/{instance_id}/fps - Error: Instance manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    // Extract instance ID
    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /api/v1/instances/{instance_id}/fps - Error: Missing instance_id";
      }
      callback(createErrorResponse(400, "Bad Request", "Missing instance_id parameter"));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /api/v1/instances/" << instanceId
                     << "/fps - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Bad Request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Validate fps field
    if (!json->isMember("fps") || !(*json)["fps"].isNumeric()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /api/v1/instances/" << instanceId
                     << "/fps - Error: Missing or invalid fps field";
      }
      callback(createErrorResponse(400, "Bad Request",
                                   "Missing required field: fps (must be a positive integer)"));
      return;
    }

    int fps = (*json)["fps"].asInt();

    // Validate fps value (must be positive)
    if (fps <= 0) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /api/v1/instances/" << instanceId
                     << "/fps - Error: Invalid fps value: " << fps;
      }
      callback(createErrorResponse(400, "Bad Request",
                                   "FPS value must be a positive integer (greater than 0)"));
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] POST /api/v1/instances/" << instanceId
                 << "/fps - Setting FPS to: " << fps;
    }

    // Check if instance exists
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /api/v1/instances/" << instanceId
                     << "/fps - Instance not found";
      }
      callback(createErrorResponse(404, "Not Found",
                                   "Instance ID does not exist"));
      return;
    }

    // Update instance FPS configuration
    Json::Value configJson;
    configJson["configuredFps"] = fps;

    if (!instance_manager_->updateInstance(instanceId, configJson)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /api/v1/instances/" << instanceId
                   << "/fps - Failed to update instance";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to update FPS configuration"));
      return;
    }

    // Build response
    Json::Value response;
    response["message"] = "FPS configuration updated successfully.";
    response["instance_id"] = instanceId;
    response["fps"] = fps;

    auto resp = createSuccessResponse(response, 200);
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /api/v1/instances/{instance_id}/fps - Exception: " << e.what();
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void InstanceFpsHandler::resetFps(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  MetricsInterceptor::setHandlerStartTime(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] DELETE /api/v1/instances/{instance_id}/fps - Reset FPS to default";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Check if manager is set
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /api/v1/instances/{instance_id}/fps - Error: Instance manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    // Extract instance ID
    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /api/v1/instances/{instance_id}/fps - Error: Missing instance_id";
      }
      callback(createErrorResponse(400, "Bad Request", "Missing instance_id parameter"));
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] DELETE /api/v1/instances/" << instanceId << "/fps";
    }

    // Check if instance exists
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /api/v1/instances/" << instanceId
                     << "/fps - Instance not found";
      }
      callback(createErrorResponse(404, "Not Found",
                                   "Instance ID does not exist"));
      return;
    }

    // Reset FPS to default (5 FPS)
    const int defaultFps = 5;
    Json::Value configJson;
    configJson["configuredFps"] = defaultFps;

    if (!instance_manager_->updateInstance(instanceId, configJson)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /api/v1/instances/" << instanceId
                   << "/fps - Failed to update instance";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to reset FPS configuration"));
      return;
    }

    // Build response
    Json::Value response;
    response["message"] = "FPS configuration reset to default.";
    response["instance_id"] = instanceId;
    response["fps"] = defaultFps;

    auto resp = createSuccessResponse(response, 200);
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /api/v1/instances/{instance_id}/fps - Exception: " << e.what();
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void InstanceFpsHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
  resp->addHeader("Access-Control-Max-Age", "3600");
  callback(resp);
}
