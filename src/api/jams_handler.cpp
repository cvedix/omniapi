#include "api/jams_handler.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/metrics_interceptor.h"
#include "core/uuid_generator.h"
#include "instances/instance_manager.h"
#include "instances/inprocess_instance_manager.h"
#include "instances/subprocess_instance_manager.h"
#include <cvedix/nodes/ba/cvedix_ba_area_jam_node.h>
#include "solutions/solution_registry.h"
#include <algorithm>
#include <drogon/HttpResponse.h>
#include <json/reader.h>
#include <json/writer.h>

IInstanceManager *JamsHandler::instance_manager_ = nullptr;

void JamsHandler::setInstanceManager(IInstanceManager *manager) { 
  instance_manager_ = manager; 
}

std::string JamsHandler::extractInstanceId(const HttpRequestPtr &req) const {
  std::string instanceId = req->getParameter("instanceId");

  if (instanceId.empty()) {
    std::string path = req->getPath();
    size_t instancesPos = path.find("/instances/");
    if (instancesPos != std::string::npos) {
      size_t start = instancesPos + 11;
      size_t end = path.find("/", start);
      if (end == std::string::npos) end = path.length();
      instanceId = path.substr(start, end - start);
    } else {
      size_t instancePos = path.find("/instance/");
      if (instancePos != std::string::npos) {
        size_t start = instancePos + 10;
        size_t end = path.find("/", start);
        if (end == std::string::npos) end = path.length();
        instanceId = path.substr(start, end - start);
      }
    }
  }
  return instanceId;
}

std::string JamsHandler::extractJamId(const HttpRequestPtr &req) const {
  std::string jamId = req->getParameter("jamId");

  if (jamId.empty()) {
    std::string path = req->getPath();
    size_t jamsPos = path.find("/jams/");
    if (jamsPos != std::string::npos) {
      size_t start = jamsPos + 6;
      size_t end = path.find("/", start);
      if (end == std::string::npos) end = path.length();
      jamId = path.substr(start, end - start);
    }
  }
  return jamId;
}

HttpResponsePtr 
JamsHandler::createErrorResponse(int statusCode, const std::string &error, 
                                const std::string &message) const {
  Json::Value errorJson;
  errorJson["error"] = error;
  if (!message.empty()) {
    errorJson["message"] = message;
  }

  auto resp = HttpResponse::newHttpJsonResponse(errorJson);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                 "GET, POST, PUT, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                 "Content-Type, Authorization");
  return resp;
}

HttpResponsePtr JamsHandler::createSuccessResponse(const Json::Value &data, 
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

Json::Value
JamsHandler::loadJamsFromConfig(const std::string &instanceId) const {
  Json::Value jamsArray(Json::arrayValue);

  if (!instance_manager_) {
    return jamsArray;
  }

  auto optInfo = instance_manager_->getInstance(instanceId);
  if (!optInfo.has_value()) {
    return jamsArray;
  }

  const auto &info = optInfo.value();
  auto it = info.additionalParams.find("JamZones");
  if (it != info.additionalParams.end() && !it->second.empty()) {
    Json::Reader reader;
    Json::Value parsedJams;
    if (reader.parse(it->second, parsedJams) && parsedJams.isArray()) {
      for (Json::ArrayIndex i = 0; i < parsedJams.size(); ++i) {
        Json::Value &jam = parsedJams[i];
        if (!jam.isObject()) continue;
        if (!jam.isMember("id") || !jam["id"].isString() || jam["id"].asString().empty()) {
          jam["id"] = UUIDGenerator::generateUUID();
          if (isApiLoggingEnabled()) PLOG_DEBUG << "[API] loadJamsFromConfig: Generated UUID for jam at index " << i;
        }
      }
      return parsedJams;
    }
  }
  return jamsArray;
}

bool JamsHandler::saveJamsToConfig(const std::string &instanceId, 
                                  const Json::Value &jams) const {
  if (!instance_manager_) {
    return false;
  }

  Json::Value normalized = jams;
  if (normalized.isArray()) {
    for (Json::ArrayIndex i = 0; i < normalized.size(); ++i) {
      Json::Value &jam = normalized[i];
      if (!jam.isObject()) continue;
      if (!jam.isMember("id") || !jam["id"].isString() || jam["id"].asString().empty()) {
        jam["id"] = UUIDGenerator::generateUUID();
        if (isApiLoggingEnabled()) PLOG_DEBUG << "[API] saveJamsToConfig: Generated UUID for jam at index " << i << " before saving";
      }
    }
  }
  Json::StreamWriterBuilder builder; 
  builder["indentation"] = "";
  std::string jamsStr = Json::writeString(builder, normalized);

  Json::Value configUpdate(Json::objectValue);
  Json::Value additionalParams(Json::objectValue);
  additionalParams["JamZones"] = jamsStr;
  configUpdate["AdditionalParams"] = additionalParams;

  bool result = 
      instance_manager_->updateInstanceFromConfig(instanceId, configUpdate);
 
  if (!result && isApiLoggingEnabled()) {
    PLOG_WARNING << "[API] saveJamsToConfig: updateInstanceFromConfig failed for instance " << instanceId;
  }
  return result;
}

bool JamsHandler::validateROI(const Json::Value &roi, std::string &error) const {
  if (!roi.isArray()) { 
    error = "ROI must be an array"; 
    return false; 
  }

  if (roi.size() < 3) { 
    error = "ROI must contain at least 3 points"; 
    return false; 
  }
  for (const auto &pt : roi) {
    if (!pt.isObject()) { 
      error = "Each ROI point must be an object"; 
      return false; 
    }

    if (!pt.isMember("x") || !pt.isMember("y")) { 
      error = "Each ROI point must have 'x' and 'y'"; 
      return false; 
    }

    if (!pt["x"].isNumeric() || !pt["y"].isNumeric()) { 
      error = "ROI 'x' and 'y' must be numbers"; 
      return false; 
    }
  }
  return true;
}

bool JamsHandler::validateJamParameters(const Json::Value &jam, std::string &error) const {

  // Detection tuning parameters (optional)
  if (jam.isMember("checkIntervalFrames")) {
    if (!jam["checkIntervalFrames"].isInt() || jam["checkIntervalFrames"].asInt() < 1) {
      error = "checkIntervalFrames must be an integer >= 1"; return false;
    }
  }
  if (jam.isMember("checkMinHitFrames")) {
    if (!jam["checkMinHitFrames"].isInt() || jam["checkMinHitFrames"].asInt() < 1) {
      error = "checkMinHitFrames must be an integer >= 1"; return false;
    }
  }
  if (jam.isMember("checkMaxDistance")) {
    if (!jam["checkMaxDistance"].isInt() || jam["checkMaxDistance"].asInt() < 0) {
      error = "checkMaxDistance must be an integer >= 0"; return false;
    }
  }
  if (jam.isMember("checkMinStops")) {
    if (!jam["checkMinStops"].isInt() || jam["checkMinStops"].asInt() < 1) {
      error = "checkMinStops must be an integer >= 1"; return false;
    }
  }
  if (jam.isMember("checkNotifyInterval")) {
    if (!jam["checkNotifyInterval"].isInt() || jam["checkNotifyInterval"].asInt() < 0) {
      error = "checkNotifyInterval must be an integer >= 0"; return false;
    }
  }

  return true;
}

void JamsHandler::getAllJams(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
              << "/jams - Get all jams";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                   << "/jams - Error: Instance manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/{instanceId}/jams - "
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
                     << "/jams - Instance not found - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Load jams from config
    Json::Value jamsArray = loadJamsFromConfig(instanceId);

    // Build response
    Json::Value response;
    response["jamZones"] = jamsArray;
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
                << "/jams - Success: " << jamsArray.size() << " jams - "
                << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << "/jams - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << "/jams - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void JamsHandler::createJam(
    const HttpRequestPtr &req, 
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId << "/jams - Create jam";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId << "/jams - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error", "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/{instanceId}/jams - Error: Instance ID is empty";
      }
      callback(createErrorResponse(400, "Bad request", "Instance ID is required"));
      return;
    }

    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId << "/jams - Instance not found - " << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Not found", "Instance not found: " + instanceId));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId << "/jams - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Bad request", "Request body must be valid JSON"));
      return;
    }

    Json::Value jamsToAdd(Json::arrayValue);
    bool isArrayRequest = json->isArray();

    if (isArrayRequest) {
      for (Json::ArrayIndex i = 0; i < json->size(); ++i) {
        const Json::Value &jam = (*json)[i];
        if (!jam.isObject()) {
          callback(createErrorResponse(400, "Bad request", "Each jam must be an object"));
          return;
        }

        if (!jam.isMember("roi") || !validateROI(jam["roi"], *new std::string())) {
          callback(createErrorResponse(400, "Bad request", "Each jam must contain a valid 'roi' array with at least 3 points"));
          return;
        }

        // Validate optional numeric parameters
        {
          std::string err;
          if (!validateJamParameters(jam, err)) {
            callback(createErrorResponse(400, "Bad request", err));
          return;
        }
        }

        jamsToAdd.append(jam);
      }
    } else {
      const Json::Value &jam = *json;
      if (!jam.isObject()) {
        callback(createErrorResponse(400, "Bad request", "Jam must be an object"));
        return;
      }

      if (!jam.isMember("roi") || !validateROI(jam["roi"], *new std::string())) {
        callback(createErrorResponse(400, "Bad request", "Jam must contain a valid 'roi' array with at least 3 points"));
        return;
      }

      {
        std::string err;
        if (!validateJamParameters(jam, err)) {
          callback(createErrorResponse(400, "Bad request", err));
          return;
        }
      }

      jamsToAdd.append(jam);
    }

    // Load existing jams and append
    Json::Value existingJams = loadJamsFromConfig(instanceId);
    for (Json::ArrayIndex i = 0; i < jamsToAdd.size(); ++i) {
      existingJams.append(jamsToAdd[i]);
    }

    bool saveResult = saveJamsToConfig(instanceId, existingJams);
    if (!saveResult) {
      callback(createErrorResponse(500, "Internal server error", "Failed to save jam zones to instance config"));
      return;
    }

    // Reload jams to get generated IDs
    Json::Value savedJams = loadJamsFromConfig(instanceId);

    // Try updating runtime; fallback to restart
    if (!updateJamsRuntime(instanceId, savedJams)) {
      restartInstanceForJamUpdate(instanceId);
    }

    // If single object request, return the created object with generated id
    if (!isArrayRequest) {
      const Json::Value &created = savedJams[savedJams.size() - 1];
      callback(createSuccessResponse(created, 201));
      return;
    }

    // Array request - return metadata
    Json::Value result;
    result["message"] = "Jams created successfully";
    result["count"] = (int)jamsToAdd.size();
    result["zones"] = savedJams;
    callback(createSuccessResponse(result, 201));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId << "/jams - Exception: " << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId << "/jams - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", "Unknown error occurred"));
  }
}

void JamsHandler::deleteAllJams(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] DELETE /v1/core/instance/" << instanceId
              << "/jams - Delete all jams";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId
                   << "/jams - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/core/instance/{instanceId}/jams - "
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
        PLOG_WARNING << "[API] DELETE /v1/core/instance/" << instanceId
                     << "/jams - Instance not found - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Save empty array to config
    Json::Value emptyArray(Json::arrayValue);
    if (!saveJamsToConfig(instanceId, emptyArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId
                   << "/jams - Failed to save jams to config";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to save jams configuration"));
      return;
    }

    // Try runtime update first (without restart)
    if (updateJamsRuntime(instanceId, emptyArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] DELETE /v1/core/instance/" << instanceId
                  << "/jams - Jams updated runtime without restart";
      }
    } else {
      // Fallback to restart if runtime update failed
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] DELETE /v1/core/instance/" << instanceId
            << "/jams - Runtime update failed, falling back to restart";
      }
      restartInstanceForJamUpdate(instanceId);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] DELETE /v1/core/instance/" << instanceId
                << "/jams - Success - " << duration.count() << "ms";
    }

    Json::Value response;
    response["message"] = "All jams deleted successfully";
    response["jamZones"] = Json::Value(Json::arrayValue);
    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId
                 << "/jams - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId
                 << "/jams - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void JamsHandler::getJam(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);
  std::string jamId = extractJamId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId << "/jams/"
              << jamId << " - Get jam";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] GET /v1/core/instance/{instanceId}/jams/{jamId} - "
               "Error: Instance ID is empty";
      }
      callback(
          createErrorResponse(400, "Bad request", "Instance ID is required"));
      return;
    }

    if (jamId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId
                     << "/jams/{jamId} - Error: Jam ID is empty";
      }
      callback(createErrorResponse(400, "Bad request", "Jam ID is required"));
      return;
    }

    Json::Value jamsArray = loadJamsFromConfig(instanceId);

    for (const auto &jam : jamsArray) {
      if (jam.isObject() && jam.isMember("id") && jam["id"].isString()) {
        if (jam["id"].asString() == jamId) {
          auto end_time = std::chrono::steady_clock::now();
          auto duration =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  end_time - start_time);

          if (isApiLoggingEnabled()) {
            PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
                      << "/jams/" << jamId << " - Success - "
                      << duration.count() << "ms";
          }

          callback(createSuccessResponse(jam));
          return;
        }
      }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId << "/jams/"
                   << jamId << " - Jam not found - " << duration.count()
                   << "ms";
    }
    callback(
        createErrorResponse(404, "Not found", "Jam not found: " + jamId));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId << "/jams/"
                 << jamId << " - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId << "/jams/"
                 << jamId << " - Unknown exception - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void JamsHandler::updateJam(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);
  std::string jamId = extractJamId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] PUT /v1/core/instance/" << instanceId << "/jams/"
              << jamId << " - Update jam";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] PUT /v1/core/instance/" << instanceId << "/jams/"
                   << jamId << " - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] PUT /v1/core/instance/{instanceId}/jams/{jamId} - "
               "Error: Instance ID is empty";
      }
      callback(
          createErrorResponse(400, "Bad request", "Instance ID is required"));
      return;
    }

    if (jamId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/instance/" << instanceId
                     << "/jams/{jamId} - Error: Jam ID is empty";
      }
      callback(createErrorResponse(400, "Bad request", "Jam ID is required"));
      return;
    }

    // Check if instance exists
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/instance/" << instanceId
                     << "/jams/" << jamId << " - Instance not found - "
                     << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/instance/" << instanceId
                     << "/jams/" << jamId << " - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Validate required fields
    if (!json->isMember("roi") || !(*json)["roi"].isArray()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/instance/" << instanceId
                     << "/jams/" << jamId
                     << " - Error: Missing or invalid 'roi' field";
      }
      callback(createErrorResponse(
          400, "Bad request",
          "Field 'roi' is required and must be an array"));
      return;
    }

    // Validate roi
    std::string roiErr;
    if (!validateROI((*json)["roi"], roiErr)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/instance/" << instanceId
                     << "/jams/" << jamId
                     << " - Validation error: " << roiErr;
      }
      callback(createErrorResponse(400, "Bad request", roiErr));
      return;
    }

    // Load existing jams
    Json::Value jamsArray = loadJamsFromConfig(instanceId);

    // Find and update jam with matching ID
    bool found = false;
    for (Json::ArrayIndex i = 0; i < jamsArray.size(); ++i) {
      Json::Value &jam = jamsArray[i];
      if (jam.isObject() && jam.isMember("id") && jam["id"].isString()) {
        if (jam["id"].asString() == jamId) {
          found = true;

          // Update jam fields (preserve ID)
          if (json->isMember("name") && (*json)["name"].isString()) {
            jam["name"] = (*json)["name"];
          }

          jam["roi"] = (*json)["roi"];

          break;
        }
      }
    }

    if (!found) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/instance/" << instanceId
                     << "/jams/" << jamId << " - Jam not found - "
                     << duration.count() << "ms";
      }
      callback(
          createErrorResponse(404, "Not found", "Jam not found: " + jamId));
      return;
    }

    // Save updated jams to config
    if (!saveJamsToConfig(instanceId, jamsArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] PUT /v1/core/instance/" << instanceId << "/jams/"
                   << jamId << " - Failed to save jams to config";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to save jams configuration"));
      return;
    }

    // Try runtime update first (without restart)
    if (updateJamsRuntime(instanceId, jamsArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] PUT /v1/core/instance/" << instanceId << "/jams/"
                  << jamId << " - Jams updated runtime without restart";
      }
    } else {
      // Fallback to restart if runtime update failed
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/instance/" << instanceId
                     << "/jams/" << jamId
                     << " - Runtime update failed, falling back to restart";
      }
      restartInstanceForJamUpdate(instanceId);
    }

    // Find updated jam to return
    Json::Value updatedJam;
    for (const auto &jam : jamsArray) {
      if (jam.isObject() && jam.isMember("id") && jam["id"].isString()) {
        if (jam["id"].asString() == jamId) {
          updatedJam = jam;
          break;
        }
      }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] PUT /v1/core/instance/" << instanceId << "/jams/"
                << jamId << " - Success - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(updatedJam));
  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PUT /v1/core/instance/" << instanceId << "/jams/"
                 << jamId << " - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PUT /v1/core/instance/" << instanceId << "/jams/"
                 << jamId << " - Unknown exception - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void JamsHandler::deleteJam(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);
  std::string jamId = extractJamId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] DELETE /v1/core/instance/" << instanceId << "/jams/"
              << jamId << " - Delete jam";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId
                   << "/jams/" << jamId
                   << " - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] DELETE /v1/core/instance/{instanceId}/jams/{jamId} - "
               "Error: Instance ID is empty";
      }
      callback(
          createErrorResponse(400, "Bad request", "Instance ID is required"));
      return;
    }

    if (jamId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/core/instance/" << instanceId
                     << "/jams/{jamId} - Error: Jam ID is empty";
      }
      callback(createErrorResponse(400, "Bad request", "Jam ID is required"));
      return;
    }

    // Check if instance exists
    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/core/instance/" << instanceId
                     << "/jams/" << jamId << " - Instance not found - "
                     << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Load existing jams
    Json::Value jamsArray = loadJamsFromConfig(instanceId);
    // Find and remove jam with matching ID
    Json::Value newJamsArray(Json::arrayValue);
    bool found = false;

    for (const auto &jam : jamsArray) {
      if (jam.isObject() && jam.isMember("id") && jam["id"].isString()) {
        if (jam["id"].asString() == jamId) {
          found = true;
          // Skip this jam (don't add to new array)
          continue;
        }
      }
      newJamsArray.append(jam);
    }

    if (!found) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/core/instance/" << instanceId
                     << "/jams/" << jamId << " - Jam not found - "
                     << duration.count() << "ms";
      }
      callback(
          createErrorResponse(404, "Not found", "Jam not found: " + jamId));
      return;
    }

    // Save updated jams to config
    if (!saveJamsToConfig(instanceId, newJamsArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId
                   << "/jams/" << jamId
                   << " - Failed to save jams to config";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to save jams configuration"));
      return;
    }

    // Try runtime update first (without restart)
    if (updateJamsRuntime(instanceId, newJamsArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] DELETE /v1/core/instance/" << instanceId
                  << "/jams/" << jamId
                  << " - Jams updated runtime without restart";
      }
    } else {
      // Fallback to restart if runtime update failed
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/core/instance/" << instanceId
                     << "/jams/" << jamId
                     << " - Runtime update failed, falling back to restart";
      }
      restartInstanceForJamUpdate(instanceId);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] DELETE /v1/core/instance/" << instanceId << "/jams/"
                << jamId << " - Success - " << duration.count() << "ms";
    }

    Json::Value response;
    response["message"] = "Jam deleted successfully";
    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId << "/jams/"
                 << jamId << " - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId << "/jams/"
                 << jamId << " - Unknown exception - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void JamsHandler::batchUpdateJams(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();
  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId << "/jams/batch - Batch update jams";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId << "/jams/batch - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error", "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/{instanceId}/jams/batch - Error: Instance ID is empty";
      }
      callback(createErrorResponse(400, "Bad request", "Instance ID is required"));
      return;
    }

    auto optInfo = instance_manager_->getInstance(instanceId);
    if (!optInfo.has_value()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId << "/jams/batch - Instance not found - " << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Not found", "Instance not found: " + instanceId));
      return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isArray()) {
      callback(createErrorResponse(400, "Bad request", "Request body must be an array of jam objects"));
      return;
    }

    Json::Value newJams(Json::arrayValue);
    for (Json::ArrayIndex i = 0; i < json->size(); ++i) {
      const Json::Value &jam = (*json)[i];
      if (!jam.isObject()) {
        callback(createErrorResponse(400, "Bad request", "Each jam must be an object"));
        return;
      }

      std::string roiErr;
      if (!jam.isMember("roi") || !validateROI(jam["roi"], roiErr)) {
        callback(createErrorResponse(400, "Bad request", "Each jam must contain a valid 'roi' array with at least 3 points"));
        return;
      }

      {
        std::string paramErr;
        if (!validateJamParameters(jam, paramErr)) {
          callback(createErrorResponse(400, "Bad request", paramErr));
          return;
        }
      }

      newJams.append(jam);
    }

    bool saveResult = saveJamsToConfig(instanceId, newJams);
    if (!saveResult) {
      callback(createErrorResponse(500, "Internal server error", "Failed to save jams to instance config"));
      return;
    }

    if (!updateJamsRuntime(instanceId, newJams)) {
      restartInstanceForJamUpdate(instanceId);
    }

    Json::Value result;
    result["message"] = "Jams batch updated successfully";
    result["count"] = (int)newJams.size();
    result["jamZones"] = newJams;
    callback(createSuccessResponse(result));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId << "/jams/batch - Exception: " << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId << "/jams/batch - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", "Unknown error occurred"));
  }
}

void JamsHandler::handleOptions(
    const HttpRequestPtr &req, 
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, PUT, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");
  resp->addHeader("Access-Control-Max-Age", "3600");

  // Record metrics and call callback
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

bool JamsHandler::restartInstanceForJamUpdate(const std::string &instanceId) const {
  if (!instance_manager_) {
    return false;
  }

  // Check if instance is running
  auto optInfo = instance_manager_->getInstance(instanceId);
  if (!optInfo.has_value() || !optInfo.value().running) {
    // Instance not running, no need to restart
    return true;
  }

  // Restart instance in background thread to apply jam changes
  std::thread restartThread([this, instanceId]() {
    try {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] ========================================";
        PLOG_INFO << "[API] Restarting instance " << instanceId
                  << " to apply jam changes";
        PLOG_INFO << "[API] This will rebuild pipeline with new zones from additionalParams[\"JamZones\"]";
        PLOG_INFO << "[API] ========================================";
      }

      // Stop instance (this will stop the pipeline)
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] Step 1/3: Stopping instance " << instanceId;
      }
      instance_manager_->stopInstance(instanceId);

      // Wait for cleanup to ensure pipeline is fully stopped
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] Step 2/3: Waiting for pipeline cleanup (500ms)";
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      // Start instance again (this will rebuild pipeline with new zones)
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] Step 3/3: Starting instance " << instanceId
                  << " (will rebuild pipeline with new JamZones)";
      }
      bool startSuccess = instance_manager_->startInstance(instanceId, true);

      if (startSuccess) {
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[API] ========================================";
          PLOG_INFO << "[API] ✓ Instance " << instanceId
                    << " restarted successfully for jam update";
          PLOG_INFO << "[API] Pipeline rebuilt with new JamZones - zones should now be active";
          PLOG_INFO << "[API] ========================================";
        }
      } else {
        if (isApiLoggingEnabled()) {
          PLOG_ERROR << "[API] ✗ Failed to start instance " << instanceId
                     << " after restart";
        }
      }
    } catch (const std::exception &e) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] ✗ Exception restarting instance " << instanceId
                   << " for jam update: " << e.what();
      }
    } catch (...) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] ✗ Unknown error restarting instance " << instanceId
                   << " for jam update";
      }
    }
  });

  restartThread.detach();
  return true;
}

std::shared_ptr<cvedix_nodes::cvedix_ba_area_jam_node>
JamsHandler::findBAJamNode(const std::string &instanceId) const {
  // Note: In subprocess mode, nodes are not directly accessible. This will
  // return nullptr and let updateJamsRuntime() fallback to restart.
  if (!instance_manager_) {
    return nullptr;
  }

  if (instance_manager_->isSubprocessMode()) {
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] findBAJamNode: Subprocess mode - nodes not directly accessible";
    }
    return nullptr;
  }

  // In in-process mode, try to access nodes via InstanceRegistry
  try {
    // Cast to InProcessInstanceManager to access registry
    auto *inProcessManager = dynamic_cast<InProcessInstanceManager *>(instance_manager_);
    if (!inProcessManager) {
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[API] findBAJamNode: Cannot cast to InProcessInstanceManager";
      }
      return nullptr;
    }

    // Get nodes from registry
    auto &registry = inProcessManager->getRegistry();
    auto nodes = registry.getInstanceNodes(instanceId);
    
    if (nodes.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[API] findBAJamNode: No nodes found for instance " << instanceId;
      }
      return nullptr;
    }

    // Search for ba_jam_node in pipeline
    for (const auto &node : nodes) {
      if (!node) continue;
      
      auto jamNode = std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_area_jam_node>(node);
      if (jamNode) {
        if (isApiLoggingEnabled()) {
          PLOG_DEBUG << "[API] findBAJamNode: Found ba_jam_node for instance " << instanceId;
        }
        return jamNode;
      }
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] findBAJamNode: ba_jam_node not found in pipeline for instance " << instanceId;
    }
    return nullptr;
  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[API] findBAJamNode: Exception accessing nodes: " << e.what();
    }
    return nullptr;
  }
}

std::map<int, std::vector<cvedix_objects::cvedix_point>>
JamsHandler::parseJamsFromJson(const Json::Value &jamsArray) const {
  std::map<int, std::vector<cvedix_objects::cvedix_point>> jams;

  if (!jamsArray.isArray()) {
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[API] parseJamsFromJson: Input is not a JSON array";
    }
    return jams;
  }

  for (Json::ArrayIndex i = 0; i < jamsArray.size(); ++i) {
    const Json::Value &jamObj = jamsArray[i];
    if (!jamObj.isObject()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] parseJamsFromJson: Jam at index " << i << " is not an object, skipping";
      }
      continue;
    }

    if (!jamObj.isMember("roi") || !jamObj["roi"].isArray()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] parseJamsFromJson: Jam at index " << i << " missing or invalid 'roi', skipping";
      }
      continue;
    }

    std::vector<cvedix_objects::cvedix_point> roiPoints;
    bool ok = true;
    for (const auto &coord : jamObj["roi"]) {
      if (!coord.isObject() || !coord.isMember("x") || !coord.isMember("y") || !coord["x"].isNumeric() || !coord["y"].isNumeric()) {
        ok = false;
        break;
      }
      cvedix_objects::cvedix_point p;
      p.x = static_cast<int>(coord["x"].asDouble());
      p.y = static_cast<int>(coord["y"].asDouble());
      roiPoints.push_back(p);
    }

    if (!ok || roiPoints.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] parseJamsFromJson: Invalid ROI at index " << i << " - skipping";
      }
      continue;
    }

    // Use index as key - SDK typically maps by channel; we don't have channel info here
    jams[static_cast<int>(i)] = std::move(roiPoints);
  }

  return jams;
}

bool JamsHandler::updateJamsRuntime(const std::string &instanceId, const Json::Value &jamsArray) const {
  if (!instance_manager_) {
    if (isApiLoggingEnabled()) {
      PLOG_WARNING
          << "[API] updateJamsRuntime: Instance registry not initialized";
    }
    return false;
  }

  // Check if instance is running
  auto optInfo = instance_manager_->getInstance(instanceId);
  if (!optInfo.has_value()) {
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] updateJamsRuntime: Instance " << instanceId
                 << " not found";
    }
    return false;
  }

  if (!optInfo.value().running) {
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] updateJamsRuntime: Instance " << instanceId
                 << " is not running, no need to update runtime";
    }
    return true; // Not an error - instance not running, config will apply on
                 // next start
  }

  // Subprocess: send UPDATE_JAMS IPC to worker
  if (instance_manager_->isSubprocessMode()) {
    auto *sub = dynamic_cast<SubprocessInstanceManager *>(instance_manager_);
    if (sub && sub->updateJams(instanceId, jamsArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] updateJamsRuntime: Jams updated via IPC (subprocess) for instance " << instanceId;
      }
      return true;
    }
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[API] updateJamsRuntime: Subprocess updateJams failed, fallback to restart for instance " << instanceId;
    }
    return false; // Fallback to restart
  }

  // In-process: Find ba_jam_node in pipeline
  auto baJamNode = findBAJamNode(instanceId);
  if (!baJamNode) {
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[API] updateJamsRuntime: ba_jam_node not found "
                      "in pipeline for instance "
                   << instanceId << ", fallback to restart";
    }
    return false; // Fallback to restart
  }

  // Parse jams from JSON
  auto jams = parseJamsFromJson(jamsArray);
  if (jams.empty() && jamsArray.isArray() && jamsArray.size() > 0) {
    // Parse failed but array is not empty - error
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[API] updateJamsRuntime: Failed to parse jams from "
                      "JSON, fallback to restart";
    }
    return false; // Fallback to restart
  }

  // Try to update jams via SDK API
  // NOTE: CVEDIX SDK's ba_jam_node doesn't expose public methods to update
  // regions at runtime. Regions are set during node construction.
  // We need to restart the instance to apply changes, which will rebuild
  // the pipeline with new jam zones from additionalParams["JamZones"]
  try {
    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] updateJamsRuntime: Found ba_jam_node for instance " << instanceId;
      PLOG_INFO << "[API] updateJamsRuntime: Parsed " << jams.size() << " jam zone(s) from JSON";
      PLOG_INFO << "[API] updateJamsRuntime: SDK doesn't support direct runtime update of jam zones";
      PLOG_INFO << "[API] updateJamsRuntime: Configuration saved, will restart instance to apply changes";
    }

    // Verify jams were parsed correctly
    if (jams.empty() && jamsArray.isArray() && jamsArray.size() == 0) {
      // Empty array is valid (delete all jams)
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] updateJamsRuntime: Empty jams array - will clear all jam zones via restart";
      }
    } else if (jams.empty()) {
      // Parse failed
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] updateJamsRuntime: Failed to parse jams, fallback to restart";
      }
      return false;
    }

    // Since SDK doesn't expose runtime update API, we need to restart
    // But we've verified that jams are correctly parsed and saved to config
    // The restart will rebuild pipeline with new jams from
    // additionalParams["JamZones"]
    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] updateJamsRuntime: Jam zones configuration saved successfully";
      PLOG_INFO << "[API] updateJamsRuntime: Instance restart will be triggered to apply changes";
    }

    // Return false to trigger fallback to restart
    // This ensures jams are applied correctly through pipeline rebuild
    return false;

  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] updateJamsRuntime: Exception updating jams: "
                 << e.what() << ", fallback to restart";
    }
    return false; // Fallback to restart
  } catch (...) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] updateJamsRuntime: Unknown exception updating "
                    "jams, fallback to restart";
    }
    return false; // Fallback to restart
  }
}
