#include "api/instance_handler.h"
#include "core/instance_logging_config.h"
#include "core/logging_flags.h"
#include "core/metrics_interceptor.h"
#include "instances/instance_manager.h"
#include <drogon/HttpResponse.h>
#include <json/json.h>

void InstanceHandler::getInstanceLogConfig(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);
  std::string instanceId = extractInstanceId(req);
  if (instanceId.empty()) {
    callback(createErrorResponse(400, "Bad request", "Instance ID is required"));
    return;
  }
  if (!instance_manager_) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Instance manager not initialized"));
    return;
  }
  Json::Value config = instance_manager_->getInstanceConfig(instanceId);
  if (config.empty() || !config.isObject()) {
    callback(createErrorResponse(404, "Not found", "Instance not found: " + instanceId));
    return;
  }
  Json::Value logging = Json::objectValue;
  if (config.isMember("logging") && config["logging"].isObject())
    logging = config["logging"];
  if (!logging.isMember("enabled"))
    logging["enabled"] = false;
  Json::Value out;
  out["config"] = logging;
  out["_description"] = Json::objectValue;
  out["_description"]["enabled"] =
      "When true, instance logs are written to logs/instance/<instance_id>/ (per-instance folder).";
  auto resp = HttpResponse::newHttpJsonResponse(out);
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "GET, PUT, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

void InstanceHandler::putInstanceLogConfig(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);
  std::string instanceId = extractInstanceId(req);
  if (instanceId.empty()) {
    callback(createErrorResponse(400, "Bad request", "Instance ID is required"));
    return;
  }
  if (!instance_manager_) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Instance manager not initialized"));
    return;
  }
  auto json = req->getJsonObject();
  if (!json) {
    callback(createErrorResponse(400, "Bad request", "Request body must be valid JSON"));
    return;
  }
  bool enabled = false;
  if (json->isMember("enabled") && (*json)["enabled"].isBool())
    enabled = (*json)["enabled"].asBool();
  Json::Value config = instance_manager_->getInstanceConfig(instanceId);
  if (config.empty() || !config.isObject()) {
    callback(createErrorResponse(404, "Not found", "Instance not found: " + instanceId));
    return;
  }
  config["logging"] = Json::objectValue;
  config["logging"]["enabled"] = enabled;
  if (!instance_manager_->updateInstanceFromConfig(instanceId, config)) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Failed to update instance config"));
    return;
  }
  InstanceLoggingConfig::setEnabled(instanceId, enabled);
  Json::Value response;
  response["message"] = "Instance logging config updated. Logs will be written to logs/instance/"
      + instanceId + "/ when enabled.";
  response["config"] = Json::objectValue;
  response["config"]["enabled"] = enabled;
  auto resp = HttpResponse::newHttpJsonResponse(response);
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "GET, PUT, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}
