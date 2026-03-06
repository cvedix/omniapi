#include "api/system_handler.h"
#include "core/decoder_detector.h"
#include "core/metrics_interceptor.h"
#include "core/preferences_manager.h"
#include "core/system_config_manager.h"
#include <cstdlib>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <plog/Log.h>
#include <thread>
#include <chrono>

void SystemHandler::getSystemConfig(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    auto &configManager = SystemConfigManager::getInstance();

    // Ensure config is loaded
    if (!configManager.isLoaded()) {
      configManager.loadConfig();
    }

    Json::Value response = configManager.getSystemConfigJson();

    auto resp = createSuccessResponse(response);
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    PLOG_ERROR << "[SystemHandler] Error getting system config: " << e.what();
    auto resp = createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void SystemHandler::updateSystemConfig(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    auto json = req->getJsonObject();
    if (!json) {
      auto resp = createErrorResponse(400, "Bad Request", "Invalid JSON body");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    auto &configManager = SystemConfigManager::getInstance();

    // Ensure config is loaded
    if (!configManager.isLoaded()) {
      configManager.loadConfig();
    }

    // Update config
    if (!configManager.updateSystemConfigFromJson(*json)) {
      auto resp = createErrorResponse(406, "Not Acceptable",
                                     "Failed to update system config");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    Json::Value response;
    response["status"] = "success";
    response["message"] = "System config updated successfully";

    auto resp = createSuccessResponse(response);
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    PLOG_ERROR << "[SystemHandler] Error updating system config: " << e.what();
    auto resp = createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void SystemHandler::getPreferences(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    auto &prefsManager = PreferencesManager::getInstance();

    // Ensure preferences are loaded
    if (!prefsManager.isLoaded()) {
      prefsManager.loadPreferences();
    }

    Json::Value response = prefsManager.getPreferences();

    auto resp = createSuccessResponse(response);
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    PLOG_ERROR << "[SystemHandler] Error getting preferences: " << e.what();
    auto resp = createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void SystemHandler::getDecoders(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    auto &decoderDetector = DecoderDetector::getInstance();

    // Detect decoders if not already detected
    if (!decoderDetector.isDetected()) {
      decoderDetector.detectDecoders();
    }

    Json::Value response = decoderDetector.getDecodersJson();

    // If no decoders found, return empty object
    if (response.empty()) {
      response = Json::objectValue;
    }

    auto resp = createSuccessResponse(response);
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    PLOG_ERROR << "[SystemHandler] Error getting decoders: " << e.what();
    auto resp = createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void SystemHandler::getRegistry(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    auto key = req->getParameter("key");
    if (key.empty()) {
      auto resp = createErrorResponse(400, "Bad Request",
                                    "Missing required parameter: key");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // For now, return a simple registry structure
    // In production, this would query actual registry/database
    Json::Value response;
    Json::Value testObj;
    testObj["value_bool"] = true;
    testObj["value_float"] = 3.14;
    testObj["value_int"] = 42;
    testObj["value_string"] = "hello";
    response["test"] = testObj;

    Json::Value systemObj;
    systemObj["version"] = "1.0.0";
    response["system"] = systemObj;

    auto resp = createSuccessResponse(response);
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    PLOG_ERROR << "[SystemHandler] Error getting registry: " << e.what();
    auto resp = createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void SystemHandler::shutdown(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    Json::Value response;
    response["status"] = "success";
    response["message"] = "System shutdown initiated";

    auto resp = createSuccessResponse(response);

    // Send response first
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

    // Schedule shutdown after a short delay
    std::thread([]() {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      std::exit(0);
    }).detach();
  } catch (const std::exception &e) {
    PLOG_ERROR << "[SystemHandler] Error shutting down: " << e.what();
    auto resp = createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void SystemHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "GET, PUT, POST, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
  resp->addHeader("Access-Control-Max-Age", "3600");
  callback(resp);
}

HttpResponsePtr SystemHandler::createErrorResponse(int statusCode,
                                                  const std::string &error,
                                                  const std::string &message) const {
  Json::Value errorResponse;
  errorResponse["error"] = error;
  if (!message.empty()) {
    errorResponse["message"] = message;
  }

  auto resp = HttpResponse::newHttpJsonResponse(errorResponse);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));
  resp->addHeader("Access-Control-Allow-Origin", "*");
  return resp;
}

HttpResponsePtr SystemHandler::createSuccessResponse(const Json::Value &data,
                                                    int statusCode) const {
  auto resp = HttpResponse::newHttpJsonResponse(data);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));
  resp->addHeader("Access-Control-Allow-Origin", "*");
  return resp;
}

