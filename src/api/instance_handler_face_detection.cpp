#include "api/instance_handler.h"
#include "core/logging_flags.h"
#include "core/metrics_interceptor.h"
#include "instances/instance_manager.h"

void InstanceHandler::setFaceDetection(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  if (!instance_manager_) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Instance registry not initialized"));
    return;
  }

  const std::string instanceId = extractInstanceId(req);
  if (instanceId.empty()) {
    callback(createErrorResponse(400, "Bad request", "Instance ID is required"));
    return;
  }

  auto json = req->getJsonObject();
  if (!json || !json->isMember("enable") || !(*json)["enable"].isBool()) {
    callback(createErrorResponse(400, "Bad request",
                                 "Field 'enable' is required and must be a boolean"));
    return;
  }

  if (!instance_manager_->getInstance(instanceId).has_value()) {
    callback(createErrorResponse(404, "Instance not found",
                                 "Instance not found: " + instanceId));
    return;
  }

  const bool enable = (*json)["enable"].asBool();

  Json::Value updateConfig(Json::objectValue);
  Json::Value additionalParams(Json::objectValue);
  additionalParams["ENABLE_FACE_DETECTION"] = enable ? "true" : "false";
  additionalParams["SECURT_FACE_DETECTION_ENABLE"] = enable ? "true" : "false";
  updateConfig["AdditionalParams"] = additionalParams;

  if (!instance_manager_->updateInstanceFromConfig(instanceId, updateConfig)) {
    callback(createErrorResponse(
        500, "Internal server error",
        "Failed to apply face detection setting to instance"));
    return;
  }

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k204NoContent);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, PUT, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}
