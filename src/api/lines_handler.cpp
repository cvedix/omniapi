#include "api/lines_handler.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/metrics_interceptor.h"
#include "core/uuid_generator.h"
#include "instances/instance_manager.h"
#include "instances/inprocess_instance_manager.h"
#include "instances/subprocess_instance_manager.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cvedix/nodes/ba/cvedix_ba_line_crossline_node.h>
#include <cvedix/objects/shapes/cvedix_line.h>
#include <cvedix/objects/shapes/cvedix_point.h>
#include <drogon/HttpResponse.h>
#include <json/reader.h>
#include <json/writer.h>
#include <opencv2/core.hpp>
#include <sstream>
#include <thread>

IInstanceManager *LinesHandler::instance_manager_ = nullptr;

void LinesHandler::setInstanceManager(IInstanceManager *manager) {
  instance_manager_ = manager;
}

std::string LinesHandler::extractInstanceId(const HttpRequestPtr &req) const {
  std::string instanceId = req->getParameter("instanceId");

  if (instanceId.empty()) {
    std::string path = req->getPath();
    // Try /instances/ pattern first (plural, standard)
    size_t instancesPos = path.find("/instances/");
    if (instancesPos != std::string::npos) {
      size_t start = instancesPos + 11; // length of "/instances/"
      size_t end = path.find("/", start);
      if (end == std::string::npos) {
        end = path.length();
      }
      instanceId = path.substr(start, end - start);
    } else {
      // Try /instance/ pattern (singular, backward compatibility)
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

std::string LinesHandler::extractLineId(const HttpRequestPtr &req) const {
  std::string lineId = req->getParameter("lineId");

  if (lineId.empty()) {
    std::string path = req->getPath();
    size_t linesPos = path.find("/lines/");
    if (linesPos != std::string::npos) {
      size_t start = linesPos + 7; // length of "/lines/"
      size_t end = path.find("/", start);
      if (end == std::string::npos) {
        end = path.length();
      }
      lineId = path.substr(start, end - start);
    }
  }

  return lineId;
}

HttpResponsePtr
LinesHandler::createErrorResponse(int statusCode, const std::string &error,
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

HttpResponsePtr LinesHandler::createSuccessResponse(const Json::Value &data,
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
LinesHandler::loadLinesFromConfig(const std::string &instanceId) const {
  Json::Value linesArray(Json::arrayValue);

  if (!instance_manager_) {
    return linesArray;
  }

  auto optInfo = instance_manager_->getInstance(instanceId);
  if (!optInfo.has_value()) {
    return linesArray;
  }

  const auto &info = optInfo.value();

  // 1) Prefer CrossingLines (JSON array string) when present.
  // Line ids: if user provided id it is kept for the whole instance lifecycle;
  // if not provided, PATCH/save paths normalize and store a generated UUID once.
  // Here we only generate for display when stored data has no id (e.g. legacy).
  auto it = info.additionalParams.find("CrossingLines");
  if (it != info.additionalParams.end() && !it->second.empty()) {
    Json::Reader reader;
    Json::Value parsedLines;
    if (reader.parse(it->second, parsedLines) && parsedLines.isArray()) {
      for (Json::ArrayIndex i = 0; i < parsedLines.size(); ++i) {
        Json::Value &line = parsedLines[i];
        if (!line.isObject()) continue;
        if (!line.isMember("id") || !line["id"].isString() ||
            line["id"].asString().empty()) {
          line["id"] = UUIDGenerator::generateUUID();
          if (isApiLoggingEnabled()) {
            PLOG_DEBUG << "[API] loadLinesFromConfig: Generated UUID for line at index " << i;
          }
        }
      }
      return parsedLines;
    }
  }

  // 2) When CrossingLines is absent, build one line from CROSSLINE_START_* / CROSSLINE_END_*
  //    (same format used in additionalParams.input for ba_crossline)
  //    Use a stable id so GET /lines returns the same id every time until user updates with CrossingLines.
  auto sx = info.additionalParams.find("CROSSLINE_START_X");
  auto sy = info.additionalParams.find("CROSSLINE_START_Y");
  auto ex = info.additionalParams.find("CROSSLINE_END_X");
  auto ey = info.additionalParams.find("CROSSLINE_END_Y");
  if (sx != info.additionalParams.end() && !sx->second.empty() &&
      sy != info.additionalParams.end() && !sy->second.empty() &&
      ex != info.additionalParams.end() && !ex->second.empty() &&
      ey != info.additionalParams.end() && !ey->second.empty()) {
    int startX = 0, startY = 0, endX = 0, endY = 0;
    try {
      startX = std::stoi(sx->second);
      startY = std::stoi(sy->second);
      endX = std::stoi(ex->second);
      endY = std::stoi(ey->second);
    } catch (...) {
      return linesArray;
    }
    Json::Value line(Json::objectValue);
    line["id"] = "line_1";  // Stable id for single line from CROSSLINE_* (no random UUID per GET)
    line["name"] = "Line 1";
    Json::Value coords(Json::arrayValue);
    Json::Value p1(Json::objectValue);
    p1["x"] = startX;
    p1["y"] = startY;
    Json::Value p2(Json::objectValue);
    p2["x"] = endX;
    p2["y"] = endY;
    coords.append(p1);
    coords.append(p2);
    line["coordinates"] = coords;
    line["direction"] = "Both";
    line["classes"] = Json::arrayValue;
    line["classes"].append("Vehicle");
    line["color"] = Json::arrayValue;
    line["color"].append(255);
    line["color"].append(0);
    line["color"].append(0);
    line["color"].append(255);
    linesArray.append(line);
    return linesArray;
  }

  return linesArray;
}

bool LinesHandler::saveLinesToConfig(const std::string &instanceId,
                                     const Json::Value &lines) const {
  if (!instance_manager_) {
    return false;
  }

  // Ensure all lines have an id - generate UUID if missing
  Json::Value normalizedLines = lines;
  if (normalizedLines.isArray()) {
    for (Json::ArrayIndex i = 0; i < normalizedLines.size(); ++i) {
      Json::Value &line = normalizedLines[i];
      if (!line.isObject()) {
        continue;
      }

      // Check if id is missing or empty
      if (!line.isMember("id") || !line["id"].isString() ||
          line["id"].asString().empty()) {
        // Generate UUID for line without id
        line["id"] = UUIDGenerator::generateUUID();
        if (isApiLoggingEnabled()) {
          PLOG_DEBUG << "[API] saveLinesToConfig: Generated UUID for line at "
                        "index "
                     << i << " before saving";
        }
      }
    }
  }

  // Convert lines array to JSON string
  Json::StreamWriterBuilder builder;
  builder["indentation"] = ""; // Compact format
  std::string linesJsonStr = Json::writeString(builder, normalizedLines);

  // Create config update JSON
  Json::Value configUpdate(Json::objectValue);
  Json::Value additionalParams(Json::objectValue);
  additionalParams["CrossingLines"] = linesJsonStr;
  configUpdate["AdditionalParams"] = additionalParams;

  // Update instance config
  // Note: updateInstanceFromConfig will merge AdditionalParams correctly,
  // preserving existing keys like "input" and adding/updating "CrossingLines"
  bool result =
      instance_manager_->updateInstanceFromConfig(instanceId, configUpdate);

  if (!result && isApiLoggingEnabled()) {
    PLOG_WARNING << "[API] saveLinesToConfig: updateInstanceFromConfig failed "
                    "for instance "
                 << instanceId;
  }

  return result;
}

bool LinesHandler::validateCoordinates(const Json::Value &coordinates,
                                       std::string &error) const {
  if (!coordinates.isArray()) {
    error = "Coordinates must be an array";
    return false;
  }

  if (coordinates.size() < 2) {
    error = "Coordinates must contain at least 2 points";
    return false;
  }

  for (const auto &coord : coordinates) {
    if (!coord.isObject()) {
      error = "Each coordinate must be an object";
      return false;
    }

    if (!coord.isMember("x") || !coord.isMember("y")) {
      error = "Each coordinate must have 'x' and 'y' fields";
      return false;
    }

    if (!coord["x"].isNumeric() || !coord["y"].isNumeric()) {
      error = "Coordinate 'x' and 'y' must be numbers";
      return false;
    }
  }

  return true;
}

bool LinesHandler::validateDirection(const std::string &direction,
                                     std::string &error) const {
  std::string directionLower = direction;
  std::transform(directionLower.begin(), directionLower.end(),
                 directionLower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (directionLower != "up" && directionLower != "down" &&
      directionLower != "both") {
    error = "Direction must be one of: Up, Down, Both";
    return false;
  }

  return true;
}

bool LinesHandler::validateClasses(const Json::Value &classes,
                                   std::string &error) const {
  if (!classes.isArray()) {
    error = "Classes must be an array";
    return false;
  }

  static const std::vector<std::string> allowedClasses = {
      "Person", "Animal", "Vehicle", "Face", "Unknown"};

  for (const auto &cls : classes) {
    if (!cls.isString()) {
      error = "Each class must be a string";
      return false;
    }

    std::string classStr = cls.asString();
    bool found = false;
    for (const auto &allowed : allowedClasses) {
      if (classStr == allowed) {
        found = true;
        break;
      }
    }

    if (!found) {
      error = "Invalid class: " + classStr +
              ". Allowed values: Person, Animal, Vehicle, Face, Unknown";
      return false;
    }
  }

  return true;
}

bool LinesHandler::validateColor(const Json::Value &color,
                                 std::string &error) const {
  if (!color.isArray()) {
    error = "Color must be an array";
    return false;
  }

  if (color.size() != 4) {
    error = "Color must contain exactly 4 values (RGBA)";
    return false;
  }

  for (const auto &val : color) {
    if (!val.isNumeric()) {
      error = "Each color value must be a number";
      return false;
    }
  }

  return true;
}

void LinesHandler::getAllLines(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
              << "/lines - Get all lines";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                   << "/lines - Error: Instance manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/{instanceId}/lines - "
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
                     << "/lines - Instance not found - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Load lines from config
    Json::Value linesArray = loadLinesFromConfig(instanceId);

    // Build response
    Json::Value response;
    response["crossingLines"] = linesArray;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
                << "/lines - Success: " << linesArray.size() << " lines - "
                << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << "/lines - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId
                 << "/lines - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void LinesHandler::createLine(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
              << "/lines - Create line";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                   << "/lines - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/{instanceId}/lines - "
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
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/lines - Instance not found - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Parse JSON body - support both single object and array
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/lines - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Request body must be valid JSON"));
      return;
    }

    Json::Value linesToAdd(Json::arrayValue);
    bool isArrayRequest = json->isArray();

    if (isArrayRequest) {
      // Handle array request (multiple lines)
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[API] POST /v1/core/instance/" << instanceId
                   << "/lines - Processing array request with " << json->size()
                   << " line(s)";
      }

      // Validate each line in the array
      for (Json::ArrayIndex i = 0; i < json->size(); ++i) {
        const Json::Value &line = (*json)[i];
        if (!line.isObject()) {
          callback(createErrorResponse(400, "Bad request",
                                       "Line at index " + std::to_string(i) +
                                           " must be an object"));
          return;
        }

        // Validate coordinates
        if (!line.isMember("coordinates") || !line["coordinates"].isArray()) {
          callback(createErrorResponse(
              400, "Bad request",
              "Line at index " + std::to_string(i) +
                  ": Missing or invalid 'coordinates' field"));
          return;
        }

        std::string coordError;
        if (!validateCoordinates(line["coordinates"], coordError)) {
          callback(createErrorResponse(400, "Bad request",
                                       "Line at index " + std::to_string(i) +
                                           ": " + coordError));
          return;
        }

        // Validate optional fields
        if (line.isMember("direction") && line["direction"].isString()) {
          std::string dirError;
          if (!validateDirection(line["direction"].asString(), dirError)) {
            callback(createErrorResponse(400, "Bad request",
                                         "Line at index " + std::to_string(i) +
                                             ": " + dirError));
            return;
          }
        }

        if (line.isMember("classes") && line["classes"].isArray()) {
          std::string classesError;
          if (!validateClasses(line["classes"], classesError)) {
            callback(createErrorResponse(400, "Bad request",
                                         "Line at index " + std::to_string(i) +
                                             ": " + classesError));
            return;
          }
        }

        if (line.isMember("color") && line["color"].isArray()) {
          std::string colorError;
          if (!validateColor(line["color"], colorError)) {
            callback(createErrorResponse(400, "Bad request",
                                         "Line at index " + std::to_string(i) +
                                             ": " + colorError));
            return;
          }
        }

        // Create line object with defaults
        Json::Value newLine(Json::objectValue);
        newLine["id"] = UUIDGenerator::generateUUID();

        if (line.isMember("name") && line["name"].isString()) {
          newLine["name"] = line["name"];
        }

        newLine["coordinates"] = line["coordinates"];

        if (line.isMember("classes") && line["classes"].isArray()) {
          newLine["classes"] = line["classes"];
        } else {
          newLine["classes"] = Json::Value(Json::arrayValue);
        }

        if (line.isMember("direction") && line["direction"].isString()) {
          newLine["direction"] = line["direction"];
        } else {
          newLine["direction"] = "Both";
        }

        if (line.isMember("color") && line["color"].isArray()) {
          newLine["color"] = line["color"];
        } else {
          Json::Value defaultColor(Json::arrayValue);
          defaultColor.append(255); // R
          defaultColor.append(0);   // G
          defaultColor.append(0);   // B
          defaultColor.append(255); // A
          newLine["color"] = defaultColor;
        }

        linesToAdd.append(newLine);
      }
    } else {
      // Handle single object request (one line) - original behavior
      // Validate required fields
      if (!json->isMember("coordinates") || !(*json)["coordinates"].isArray()) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING
              << "[API] POST /v1/core/instance/" << instanceId
              << "/lines - Error: Missing or invalid 'coordinates' field";
        }
        callback(createErrorResponse(
            400, "Bad request",
            "Field 'coordinates' is required and must be an array"));
        return;
      }

      // Validate coordinates
      std::string coordError;
      if (!validateCoordinates((*json)["coordinates"], coordError)) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                       << "/lines - Validation error: " << coordError;
        }
        callback(createErrorResponse(400, "Bad request", coordError));
        return;
      }

      // Validate optional fields
      if (json->isMember("direction") && (*json)["direction"].isString()) {
        std::string dirError;
        if (!validateDirection((*json)["direction"].asString(), dirError)) {
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                         << "/lines - Validation error: " << dirError;
          }
          callback(createErrorResponse(400, "Bad request", dirError));
          return;
        }
      }

      if (json->isMember("classes") && (*json)["classes"].isArray()) {
        std::string classesError;
        if (!validateClasses((*json)["classes"], classesError)) {
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                         << "/lines - Validation error: " << classesError;
          }
          callback(createErrorResponse(400, "Bad request", classesError));
          return;
        }
      }

      if (json->isMember("color") && (*json)["color"].isArray()) {
        std::string colorError;
        if (!validateColor((*json)["color"], colorError)) {
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                         << "/lines - Validation error: " << colorError;
          }
          callback(createErrorResponse(400, "Bad request", colorError));
          return;
        }
      }

      // Create new line object
      Json::Value newLine(Json::objectValue);
      newLine["id"] = UUIDGenerator::generateUUID();

      if (json->isMember("name") && (*json)["name"].isString()) {
        newLine["name"] = (*json)["name"];
      }

      newLine["coordinates"] = (*json)["coordinates"];

      if (json->isMember("classes") && (*json)["classes"].isArray()) {
        newLine["classes"] = (*json)["classes"];
      } else {
        newLine["classes"] = Json::Value(Json::arrayValue);
      }

      if (json->isMember("direction") && (*json)["direction"].isString()) {
        newLine["direction"] = (*json)["direction"];
      } else {
        newLine["direction"] = "Both";
      }

      if (json->isMember("color") && (*json)["color"].isArray()) {
        newLine["color"] = (*json)["color"];
      } else {
        Json::Value defaultColor(Json::arrayValue);
        defaultColor.append(255); // R
        defaultColor.append(0);   // G
        defaultColor.append(0);   // B
        defaultColor.append(255); // A
        newLine["color"] = defaultColor;
      }

      linesToAdd.append(newLine);
    }

    // Load existing lines
    Json::Value linesArray = loadLinesFromConfig(instanceId);

    // Append all new lines to existing lines
    for (Json::ArrayIndex i = 0; i < linesToAdd.size(); ++i) {
      linesArray.append(linesToAdd[i]);
    }

    // Save to config
    if (!saveLinesToConfig(instanceId, linesArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                   << "/lines - Failed to save lines to config";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to save lines configuration"));
      return;
    }

    // Try runtime update first (without restart)
    if (updateLinesRuntime(instanceId, linesArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                  << "/lines - Lines updated runtime without restart";
      }
    } else {
      // Fallback to restart if runtime update failed
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] POST /v1/core/instance/" << instanceId
            << "/lines - Runtime update failed, falling back to restart";
      }
      restartInstanceForLineUpdate(instanceId);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    // Prepare response based on request type
    if (isArrayRequest) {
      // Multiple lines: return array with metadata
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                  << "/lines - Success: Created " << linesToAdd.size()
                  << " line(s) - " << duration.count() << "ms";
      }
      Json::Value response;
      response["message"] = "Lines created successfully";
      response["count"] = static_cast<int>(linesToAdd.size());
      response["lines"] = linesToAdd;
      callback(createSuccessResponse(response, 201));
    } else {
      // Single line: return single line object (backward compatible)
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                  << "/lines - Success: Created line "
                  << linesToAdd[0]["id"].asString() << " - " << duration.count()
                  << "ms";
      }
      callback(createSuccessResponse(linesToAdd[0], 201));
    }

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                 << "/lines - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                 << "/lines - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void LinesHandler::deleteAllLines(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] DELETE /v1/core/instance/" << instanceId
              << "/lines - Delete all lines";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId
                   << "/lines - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/core/instance/{instanceId}/lines - "
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
                     << "/lines - Instance not found - " << duration.count()
                     << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Save empty array to config
    Json::Value emptyArray(Json::arrayValue);
    if (!saveLinesToConfig(instanceId, emptyArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId
                   << "/lines - Failed to save lines to config";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to save lines configuration"));
      return;
    }

    // Try runtime update first (without restart)
    if (updateLinesRuntime(instanceId, emptyArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] DELETE /v1/core/instance/" << instanceId
                  << "/lines - Lines updated runtime without restart";
      }
    } else {
      // Fallback to restart if runtime update failed
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] DELETE /v1/core/instance/" << instanceId
            << "/lines - Runtime update failed, falling back to restart";
      }
      restartInstanceForLineUpdate(instanceId);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] DELETE /v1/core/instance/" << instanceId
                << "/lines - Success - " << duration.count() << "ms";
    }

    Json::Value response;
    response["message"] = "All lines deleted successfully";
    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId
                 << "/lines - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId
                 << "/lines - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void LinesHandler::getLine(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);
  std::string lineId = extractLineId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId << "/lines/"
              << lineId << " - Get line";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId << "/lines/"
                   << lineId << " - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] GET /v1/core/instance/{instanceId}/lines/{lineId} - "
               "Error: Instance ID is empty";
      }
      callback(
          createErrorResponse(400, "Bad request", "Instance ID is required"));
      return;
    }

    if (lineId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId
                     << "/lines/{lineId} - Error: Line ID is empty";
      }
      callback(createErrorResponse(400, "Bad request", "Line ID is required"));
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
                     << "/lines/" << lineId << " - Instance not found - "
                     << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Load lines from config
    Json::Value linesArray = loadLinesFromConfig(instanceId);

    // Find line with matching ID
    for (const auto &line : linesArray) {
      if (line.isObject() && line.isMember("id") && line["id"].isString()) {
        if (line["id"].asString() == lineId) {
          auto end_time = std::chrono::steady_clock::now();
          auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
              end_time - start_time);

          if (isApiLoggingEnabled()) {
            PLOG_INFO << "[API] GET /v1/core/instance/" << instanceId
                      << "/lines/" << lineId << " - Success - "
                      << duration.count() << "ms";
          }

          callback(createSuccessResponse(line));
          return;
        }
      }
    }

    // Line not found
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[API] GET /v1/core/instance/" << instanceId << "/lines/"
                   << lineId << " - Line not found - " << duration.count()
                   << "ms";
    }
    callback(
        createErrorResponse(404, "Not found", "Line not found: " + lineId));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId << "/lines/"
                 << lineId << " - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/instance/" << instanceId << "/lines/"
                 << lineId << " - Unknown exception - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void LinesHandler::updateLine(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);
  std::string lineId = extractLineId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] PUT /v1/core/instance/" << instanceId << "/lines/"
              << lineId << " - Update line";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] PUT /v1/core/instance/" << instanceId << "/lines/"
                   << lineId << " - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] PUT /v1/core/instance/{instanceId}/lines/{lineId} - "
               "Error: Instance ID is empty";
      }
      callback(
          createErrorResponse(400, "Bad request", "Instance ID is required"));
      return;
    }

    if (lineId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/instance/" << instanceId
                     << "/lines/{lineId} - Error: Line ID is empty";
      }
      callback(createErrorResponse(400, "Bad request", "Line ID is required"));
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
                     << "/lines/" << lineId << " - Instance not found - "
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
                     << "/lines/" << lineId << " - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Validate required fields
    if (!json->isMember("coordinates") || !(*json)["coordinates"].isArray()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/instance/" << instanceId
                     << "/lines/" << lineId
                     << " - Error: Missing or invalid 'coordinates' field";
      }
      callback(createErrorResponse(
          400, "Bad request",
          "Field 'coordinates' is required and must be an array"));
      return;
    }

    // Validate coordinates
    std::string coordError;
    if (!validateCoordinates((*json)["coordinates"], coordError)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/instance/" << instanceId
                     << "/lines/" << lineId
                     << " - Validation error: " << coordError;
      }
      callback(createErrorResponse(400, "Bad request", coordError));
      return;
    }

    // Validate optional fields
    if (json->isMember("direction") && (*json)["direction"].isString()) {
      std::string dirError;
      if (!validateDirection((*json)["direction"].asString(), dirError)) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] PUT /v1/core/instance/" << instanceId
                       << "/lines/" << lineId
                       << " - Validation error: " << dirError;
        }
        callback(createErrorResponse(400, "Bad request", dirError));
        return;
      }
    }

    if (json->isMember("classes") && (*json)["classes"].isArray()) {
      std::string classesError;
      if (!validateClasses((*json)["classes"], classesError)) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] PUT /v1/core/instance/" << instanceId
                       << "/lines/" << lineId
                       << " - Validation error: " << classesError;
        }
        callback(createErrorResponse(400, "Bad request", classesError));
        return;
      }
    }

    if (json->isMember("color") && (*json)["color"].isArray()) {
      std::string colorError;
      if (!validateColor((*json)["color"], colorError)) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] PUT /v1/core/instance/" << instanceId
                       << "/lines/" << lineId
                       << " - Validation error: " << colorError;
        }
        callback(createErrorResponse(400, "Bad request", colorError));
        return;
      }
    }

    // Load existing lines
    Json::Value linesArray = loadLinesFromConfig(instanceId);

    // Find and update line with matching ID
    bool found = false;
    for (Json::ArrayIndex i = 0; i < linesArray.size(); ++i) {
      Json::Value &line = linesArray[i];
      if (line.isObject() && line.isMember("id") && line["id"].isString()) {
        if (line["id"].asString() == lineId) {
          found = true;

          // Update line fields (preserve ID)
          if (json->isMember("name") && (*json)["name"].isString()) {
            line["name"] = (*json)["name"];
          }

          line["coordinates"] = (*json)["coordinates"];

          if (json->isMember("classes") && (*json)["classes"].isArray()) {
            line["classes"] = (*json)["classes"];
          } else {
            line["classes"] = Json::Value(Json::arrayValue);
          }

          if (json->isMember("direction") && (*json)["direction"].isString()) {
            line["direction"] = (*json)["direction"];
          } else {
            line["direction"] = "Both";
          }

          if (json->isMember("color") && (*json)["color"].isArray()) {
            line["color"] = (*json)["color"];
          } else {
            Json::Value defaultColor(Json::arrayValue);
            defaultColor.append(255); // R
            defaultColor.append(0);   // G
            defaultColor.append(0);   // B
            defaultColor.append(255); // A
            line["color"] = defaultColor;
          }

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
                     << "/lines/" << lineId << " - Line not found - "
                     << duration.count() << "ms";
      }
      callback(
          createErrorResponse(404, "Not found", "Line not found: " + lineId));
      return;
    }

    // Save updated lines to config
    if (!saveLinesToConfig(instanceId, linesArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] PUT /v1/core/instance/" << instanceId << "/lines/"
                   << lineId << " - Failed to save lines to config";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to save lines configuration"));
      return;
    }

    // Try runtime update first (without restart)
    if (updateLinesRuntime(instanceId, linesArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] PUT /v1/core/instance/" << instanceId << "/lines/"
                  << lineId << " - Lines updated runtime without restart";
      }
    } else {
      // Fallback to restart if runtime update failed
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/instance/" << instanceId
                     << "/lines/" << lineId
                     << " - Runtime update failed, falling back to restart";
      }
      restartInstanceForLineUpdate(instanceId);
    }

    // Find updated line to return
    Json::Value updatedLine;
    for (const auto &line : linesArray) {
      if (line.isObject() && line.isMember("id") && line["id"].isString()) {
        if (line["id"].asString() == lineId) {
          updatedLine = line;
          break;
        }
      }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] PUT /v1/core/instance/" << instanceId << "/lines/"
                << lineId << " - Success - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(updatedLine));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PUT /v1/core/instance/" << instanceId << "/lines/"
                 << lineId << " - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PUT /v1/core/instance/" << instanceId << "/lines/"
                 << lineId << " - Unknown exception - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void LinesHandler::deleteLine(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);
  std::string lineId = extractLineId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] DELETE /v1/core/instance/" << instanceId << "/lines/"
              << lineId << " - Delete line";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId
                   << "/lines/" << lineId
                   << " - Error: Instance registry not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] DELETE /v1/core/instance/{instanceId}/lines/{lineId} - "
               "Error: Instance ID is empty";
      }
      callback(
          createErrorResponse(400, "Bad request", "Instance ID is required"));
      return;
    }

    if (lineId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/core/instance/" << instanceId
                     << "/lines/{lineId} - Error: Line ID is empty";
      }
      callback(createErrorResponse(400, "Bad request", "Line ID is required"));
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
                     << "/lines/" << lineId << " - Instance not found - "
                     << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Instance not found: " + instanceId));
      return;
    }

    // Load existing lines
    Json::Value linesArray = loadLinesFromConfig(instanceId);

    // Find and remove line with matching ID
    Json::Value newLinesArray(Json::arrayValue);
    bool found = false;

    for (const auto &line : linesArray) {
      if (line.isObject() && line.isMember("id") && line["id"].isString()) {
        if (line["id"].asString() == lineId) {
          found = true;
          // Skip this line (don't add to new array)
          continue;
        }
      }
      newLinesArray.append(line);
    }

    if (!found) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/core/instance/" << instanceId
                     << "/lines/" << lineId << " - Line not found - "
                     << duration.count() << "ms";
      }
      callback(
          createErrorResponse(404, "Not found", "Line not found: " + lineId));
      return;
    }

    // Save updated lines to config
    if (!saveLinesToConfig(instanceId, newLinesArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId
                   << "/lines/" << lineId
                   << " - Failed to save lines to config";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to save lines configuration"));
      return;
    }

    // Try runtime update first (without restart)
    if (updateLinesRuntime(instanceId, newLinesArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] DELETE /v1/core/instance/" << instanceId
                  << "/lines/" << lineId
                  << " - Lines updated runtime without restart";
      }
    } else {
      // Fallback to restart if runtime update failed
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/core/instance/" << instanceId
                     << "/lines/" << lineId
                     << " - Runtime update failed, falling back to restart";
      }
      restartInstanceForLineUpdate(instanceId);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] DELETE /v1/core/instance/" << instanceId << "/lines/"
                << lineId << " - Success - " << duration.count() << "ms";
    }

    Json::Value response;
    response["message"] = "Line deleted successfully";
    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId << "/lines/"
                 << lineId << " - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/core/instance/" << instanceId << "/lines/"
                 << lineId << " - Unknown exception - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void LinesHandler::batchUpdateLines(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  std::string instanceId = extractInstanceId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
              << "/lines/batch - Batch update lines";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!instance_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                   << "/lines/batch - Error: Instance manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Instance manager not initialized"));
      return;
    }

    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] POST /v1/core/instance/{instanceId}/lines/batch - "
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
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/lines/batch - Instance not found - "
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
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/lines/batch - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Validate lines array
    if (!json->isMember("lines") || !(*json)["lines"].isArray()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] POST /v1/core/instance/" << instanceId
            << "/lines/batch - Error: Missing or invalid 'lines' array";
      }
      callback(createErrorResponse(400, "Bad request",
                                   "Field 'lines' must be an array"));
      return;
    }

    Json::Value linesArray = (*json)["lines"];

    // Validate each line in the array
    for (Json::ArrayIndex i = 0; i < linesArray.size(); ++i) {
      const Json::Value &line = linesArray[i];
      if (!line.isObject()) {
        callback(createErrorResponse(400, "Bad request",
                                     "Line at index " + std::to_string(i) +
                                         " must be an object"));
        return;
      }

      // Validate coordinates
      if (line.isMember("coordinates")) {
        std::string coordError;
        if (!validateCoordinates(line["coordinates"], coordError)) {
          callback(createErrorResponse(400, "Bad request",
                                       "Line at index " + std::to_string(i) +
                                           ": " + coordError));
          return;
        }
      } else if (line.isMember("start") && line.isMember("end")) {
        // Alternative format: start/end
        Json::Value coords(Json::arrayValue);
        coords.append(line["start"]);
        coords.append(line["end"]);
        std::string coordError;
        if (!validateCoordinates(coords, coordError)) {
          callback(createErrorResponse(400, "Bad request",
                                       "Line at index " + std::to_string(i) +
                                           ": " + coordError));
          return;
        }
      } else {
        callback(createErrorResponse(400, "Bad request",
                                     "Line at index " + std::to_string(i) +
                                         ": Missing coordinates or start/end"));
        return;
      }

      // Validate classes (required for ba_crossline)
      if (line.isMember("classes")) {
        std::string classesError;
        if (!validateClasses(line["classes"], classesError)) {
          callback(createErrorResponse(400, "Bad request",
                                       "Line at index " + std::to_string(i) +
                                           ": " + classesError));
          return;
        }
      }

      // Validate direction if provided
      if (line.isMember("direction")) {
        std::string dirError;
        if (!validateDirection(line["direction"].asString(), dirError)) {
          callback(createErrorResponse(400, "Bad request",
                                       "Line at index " + std::to_string(i) +
                                           ": " + dirError));
          return;
        }
      }

      // Validate color if provided
      if (line.isMember("color")) {
        std::string colorError;
        if (!validateColor(line["color"], colorError)) {
          callback(createErrorResponse(400, "Bad request",
                                       "Line at index " + std::to_string(i) +
                                           ": " + colorError));
          return;
        }
      }

      // Ensure all lines have an ID - generate if missing
      if (!line.isMember("id") || !line["id"].isString() ||
          line["id"].asString().empty()) {
        linesArray[i]["id"] = UUIDGenerator::generateUUID();
        if (isApiLoggingEnabled()) {
          PLOG_DEBUG << "[API] POST /v1/core/instance/" << instanceId
                     << "/lines/batch - Generated UUID for line at index " << i;
        }
      }
    }

    // Save updated lines to config
    if (!saveLinesToConfig(instanceId, linesArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                   << "/lines/batch - Failed to save lines to config";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to save lines configuration"));
      return;
    }

    // Try runtime update first (without restart)
    if (updateLinesRuntime(instanceId, linesArray)) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                  << "/lines/batch - Lines updated runtime without restart";
      }
    } else {
      // Fallback to restart if runtime update failed
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/instance/" << instanceId
                     << "/lines/batch - Runtime update failed, falling back to "
                        "restart";
      }
      restartInstanceForLineUpdate(instanceId);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/core/instance/" << instanceId
                << "/lines/batch - Success - " << duration.count() << "ms ("
                << linesArray.size() << " lines)";
    }

    Json::Value response;
    response["message"] = "Lines updated successfully";
    response["count"] = static_cast<int>(linesArray.size());
    response["lines"] = linesArray;
    callback(createSuccessResponse(response, 200));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                 << "/lines/batch - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/instance/" << instanceId
                 << "/lines/batch - Unknown exception - " << duration.count()
                 << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void LinesHandler::handleOptions(
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

bool LinesHandler::restartInstanceForLineUpdate(
    const std::string &instanceId) const {
  if (!instance_manager_) {
    return false;
  }

  // Check if instance is running
  auto optInfo = instance_manager_->getInstance(instanceId);
  if (!optInfo.has_value() || !optInfo.value().running) {
    // Instance not running, no need to restart
    return true;
  }

  // Restart instance in background thread to apply line changes
  std::thread restartThread([this, instanceId]() {
    try {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] ========================================";
        PLOG_INFO << "[API] Restarting instance " << instanceId
                  << " to apply line changes";
        PLOG_INFO << "[API] This will rebuild pipeline with new lines from "
                     "additionalParams[\"CrossingLines\"]";
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

      // Start instance again (this will rebuild pipeline with new lines)
      // startInstance() calls rebuildPipelineFromInstanceInfo() which rebuilds
      // pipeline with lines from additionalParams["CrossingLines"]
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] Step 3/3: Starting instance " << instanceId
                  << " (will rebuild pipeline with new lines)";
      }
      bool startSuccess = instance_manager_->startInstance(instanceId, true);

      if (startSuccess) {
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[API] ========================================";
          PLOG_INFO << "[API] ✓ Instance " << instanceId
                    << " restarted successfully for line update";
          PLOG_INFO << "[API] Pipeline rebuilt with new lines - lines should "
                       "now be visible on stream";
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
                   << " for line update: " << e.what();
      }
    } catch (...) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] ✗ Unknown error restarting instance " << instanceId
                   << " for line update";
      }
    }
  });
  restartThread.detach();

  return true;
}

std::shared_ptr<cvedix_nodes::cvedix_ba_line_crossline_node>
LinesHandler::findBACrosslineNode(const std::string &instanceId) const {
  // Note: In subprocess mode, we cannot access nodes directly
  // as they run in separate worker processes.
  // This method will return nullptr in subprocess mode,
  // and updateLinesRuntime() will fallback to restart.

  if (!instance_manager_) {
    return nullptr;
  }

  // Check if we're in subprocess mode
  if (instance_manager_->isSubprocessMode()) {
    // In subprocess mode, nodes are in worker process, not accessible directly
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] findBACrosslineNode: Subprocess mode - nodes not "
                    "directly accessible";
    }
    return nullptr;
  }

  // In in-process mode, try to access nodes via InstanceRegistry
  try {
    // Cast to InProcessInstanceManager to access registry
    auto *inProcessManager =
        dynamic_cast<InProcessInstanceManager *>(instance_manager_);
    if (!inProcessManager) {
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[API] findBACrosslineNode: Cannot cast to "
                      "InProcessInstanceManager";
      }
      return nullptr;
    }

    // Get nodes from registry
    auto &registry = inProcessManager->getRegistry();
    auto nodes = registry.getInstanceNodes(instanceId);

    if (nodes.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[API] findBACrosslineNode: No nodes found for instance "
                   << instanceId;
      }
      return nullptr;
    }

    // Search for ba_crossline_node in pipeline
    for (const auto &node : nodes) {
      if (!node)
        continue;

      auto crosslineNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_line_crossline_node>(
              node);
      if (crosslineNode) {
        if (isApiLoggingEnabled()) {
          PLOG_DEBUG << "[API] findBACrosslineNode: Found ba_crossline_node for "
                        "instance "
                     << instanceId;
        }
        return crosslineNode;
      }
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] findBACrosslineNode: ba_crossline_node not found in "
                    "pipeline for instance "
                 << instanceId;
    }
    return nullptr;
  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[API] findBACrosslineNode: Exception accessing nodes: "
                   << e.what();
    }
    return nullptr;
  }
}

std::map<int, cvedix_objects::cvedix_line>
LinesHandler::parseLinesFromJson(const Json::Value &linesArray) const {
  std::map<int, cvedix_objects::cvedix_line> lines;

  if (!linesArray.isArray()) {
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[API] parseLinesFromJson: Input is not a JSON array";
    }
    return lines;
  }

  // Iterate through lines array
  for (Json::ArrayIndex i = 0; i < linesArray.size(); ++i) {
    const Json::Value &lineObj = linesArray[i];

    // Check if line has coordinates
    if (!lineObj.isMember("coordinates") || !lineObj["coordinates"].isArray()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] parseLinesFromJson: Line at index " << i
                     << " missing or invalid 'coordinates' field, skipping";
      }
      continue;
    }

    const Json::Value &coordinates = lineObj["coordinates"];
    if (coordinates.size() < 2) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] parseLinesFromJson: Line at index " << i
                     << " has less than 2 coordinates, skipping";
      }
      continue;
    }

    // Get first and last coordinates
    const Json::Value &startCoord = coordinates[0];
    const Json::Value &endCoord = coordinates[coordinates.size() - 1];

    if (!startCoord.isMember("x") || !startCoord.isMember("y") ||
        !endCoord.isMember("x") || !endCoord.isMember("y")) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] parseLinesFromJson: Line at index " << i
                     << " has invalid coordinate format, skipping";
      }
      continue;
    }

    if (!startCoord["x"].isNumeric() || !startCoord["y"].isNumeric() ||
        !endCoord["x"].isNumeric() || !endCoord["y"].isNumeric()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] parseLinesFromJson: Line at index " << i
                     << " has non-numeric coordinates, skipping";
      }
      continue;
    }

    // Convert to cvedix_line
    int start_x = startCoord["x"].asInt();
    int start_y = startCoord["y"].asInt();
    int end_x = endCoord["x"].asInt();
    int end_y = endCoord["y"].asInt();

    cvedix_objects::cvedix_point start(start_x, start_y);
    cvedix_objects::cvedix_point end(end_x, end_y);

    // Use array index as channel (0, 1, 2, ...)
    int channel = static_cast<int>(i);
    lines[channel] = cvedix_objects::cvedix_line(start, end);
  }

  if (isApiLoggingEnabled() && !lines.empty()) {
    PLOG_DEBUG << "[API] parseLinesFromJson: Parsed " << lines.size()
               << " line(s) from JSON";
  }

  return lines;
}

std::map<int, std::vector<cvedix_nodes::crossline_config>>
LinesHandler::parseLinesFromJsonWithConfigs(const Json::Value &linesArray) const {
  std::map<int, std::vector<cvedix_nodes::crossline_config>> configsByChannel;

  if (!linesArray.isArray()) {
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[API] parseLinesFromJsonWithConfigs: Input is not a JSON array";
    }
    return configsByChannel;
  }

  // Iterate through lines array
  for (Json::ArrayIndex i = 0; i < linesArray.size(); ++i) {
    const Json::Value &lineObj = linesArray[i];

    // Check if line has coordinates
    if (!lineObj.isMember("coordinates") || !lineObj["coordinates"].isArray()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] parseLinesFromJsonWithConfigs: Line at index " << i
                     << " missing or invalid 'coordinates' field, skipping";
      }
      continue;
    }

    const Json::Value &coordinates = lineObj["coordinates"];
    if (coordinates.size() < 2) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] parseLinesFromJsonWithConfigs: Line at index " << i
                     << " has less than 2 coordinates, skipping";
      }
      continue;
    }

    // Get first and last coordinates
    const Json::Value &startCoord = coordinates[0];
    const Json::Value &endCoord = coordinates[coordinates.size() - 1];

    if (!startCoord.isMember("x") || !startCoord.isMember("y") ||
        !endCoord.isMember("x") || !endCoord.isMember("y")) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] parseLinesFromJsonWithConfigs: Line at index " << i
                     << " has invalid coordinate format, skipping";
      }
      continue;
    }

    if (!startCoord["x"].isNumeric() || !startCoord["y"].isNumeric() ||
        !endCoord["x"].isNumeric() || !endCoord["y"].isNumeric()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] parseLinesFromJsonWithConfigs: Line at index " << i
                     << " has non-numeric coordinates, skipping";
      }
      continue;
    }

    // Convert to cvedix_line
    int start_x = startCoord["x"].asInt();
    int start_y = startCoord["y"].asInt();
    int end_x = endCoord["x"].asInt();
    int end_y = endCoord["y"].asInt();

    cvedix_objects::cvedix_point start(start_x, start_y);
    cvedix_objects::cvedix_point end(end_x, end_y);
    cvedix_objects::cvedix_line line(start, end);

    // Parse line name
    std::string line_name = "";
    if (lineObj.isMember("name") && lineObj["name"].isString() &&
        !lineObj["name"].asString().empty()) {
      line_name = lineObj["name"].asString();
    } else {
      line_name = "Line " + std::to_string(i + 1);
    }

    // Parse color (RGBA format: [R, G, B, A])
    cv::Scalar line_color = cv::Scalar(0, 255, 0); // Default green
    if (lineObj.isMember("color") && lineObj["color"].isArray() &&
        lineObj["color"].size() >= 3) {
      int r = lineObj["color"][0].asInt();
      int g = lineObj["color"][1].asInt();
      int b = lineObj["color"][2].asInt();
      // OpenCV uses BGR format
      line_color = cv::Scalar(b, g, r);
    }

    // Parse direction
    cvedix_objects::cvedix_ba_direct_type direction =
        cvedix_objects::cvedix_ba_direct_type::BOTH;
    if (lineObj.isMember("direction") && lineObj["direction"].isString()) {
      std::string dir = lineObj["direction"].asString();
      if (dir == "Up" || dir == "IN") {
        direction = cvedix_objects::cvedix_ba_direct_type::IN;
      } else if (dir == "Down" || dir == "OUT") {
        direction = cvedix_objects::cvedix_ba_direct_type::OUT;
      } else {
        direction = cvedix_objects::cvedix_ba_direct_type::BOTH;
      }
    }

    // All lines go to channel 0 by default (as per original design in pipeline_builder)
    int channel = 0;
    if (lineObj.isMember("channel") && lineObj["channel"].isNumeric()) {
      channel = lineObj["channel"].asInt();
    }

    // Create crossline_config with name, color, and direction
    cvedix_nodes::crossline_config config(line, line_color, line_name, direction);
    configsByChannel[channel].push_back(config);
  }

  if (isApiLoggingEnabled() && !configsByChannel.empty()) {
    int totalLines = 0;
    for (const auto &pair : configsByChannel) {
      totalLines += static_cast<int>(pair.second.size());
    }
    PLOG_DEBUG << "[API] parseLinesFromJsonWithConfigs: Parsed " << totalLines
               << " line(s) with configs from JSON";
  }

  return configsByChannel;
}

bool LinesHandler::updateLinesRuntime(const std::string &instanceId,
                                      const Json::Value &linesArray) const {
  if (!instance_manager_) {
    if (isApiLoggingEnabled()) {
      PLOG_WARNING
          << "[API] updateLinesRuntime: Instance registry not initialized";
    }
    return false;
  }

  // Check if instance is running
  auto optInfo = instance_manager_->getInstance(instanceId);
  if (!optInfo.has_value()) {
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] updateLinesRuntime: Instance " << instanceId
                 << " not found";
    }
    return false;
  }

  if (!optInfo.value().running) {
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] updateLinesRuntime: Instance " << instanceId
                 << " is not running, no need to update runtime";
    }
    return true; // Not an error - instance not running, config will apply on
                 // next start
  }

  // Check if we're in subprocess mode
  if (instance_manager_->isSubprocessMode()) {
    // In subprocess mode, send IPC message to worker process
    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] updateLinesRuntime: Subprocess mode detected, "
                   "sending UPDATE_LINES IPC message to worker";
    }

    try {
      // Cast to SubprocessInstanceManager to call updateLines()
      auto *subprocessManager =
          dynamic_cast<SubprocessInstanceManager *>(instance_manager_);
      if (!subprocessManager) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] updateLinesRuntime: Cannot cast to "
                         "SubprocessInstanceManager";
        }
        return false;
      }

      // Send UPDATE_LINES IPC message to worker
      bool success = subprocessManager->updateLines(instanceId, linesArray);
      if (success) {
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[API] updateLinesRuntime: ✓ Successfully updated lines "
                       "via IPC (no restart needed)";
        }
        return true;
      } else {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] updateLinesRuntime: IPC update failed, "
                          "fallback to restart";
        }
        return false; // Fallback to restart
      }
    } catch (const std::exception &e) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] updateLinesRuntime: Exception in subprocess mode: "
                   << e.what();
      }
      return false;
    }
  }

  // In-process mode: find ba_crossline_node in pipeline
  auto baCrosslineNode = findBACrosslineNode(instanceId);
  if (!baCrosslineNode) {
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[API] updateLinesRuntime: ba_crossline_node not found "
                      "in pipeline for instance "
                   << instanceId << ", fallback to restart";
    }
    return false; // Fallback to restart
  }

  // Parse lines from JSON with full configs (name, color, direction)
  auto configsByChannel = parseLinesFromJsonWithConfigs(linesArray);
  
  // Check if we have valid lines or empty array
  bool isEmptyArray = linesArray.isArray() && linesArray.size() == 0;
  bool hasValidLines = !configsByChannel.empty();
  
  if (!isEmptyArray && !hasValidLines) {
    // Parse failed but array is not empty - error
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[API] updateLinesRuntime: Failed to parse lines from "
                      "JSON, fallback to restart";
    }
    return false; // Fallback to restart
  }

  // Try to update lines via SDK API
  // Strategy: Use set_lines() with all lines (simpler and more reliable)
  // This ensures consistency - we replace all lines at once
  try {
    if (isApiLoggingEnabled()) {
      if (isEmptyArray) {
        PLOG_INFO << "[API] updateLinesRuntime: Found ba_crossline_node, "
                     "attempting to clear all lines";
      } else {
        int totalLines = 0;
        for (const auto &pair : configsByChannel) {
          totalLines += static_cast<int>(pair.second.size());
        }
        PLOG_INFO << "[API] updateLinesRuntime: Found ba_crossline_node, "
                     "attempting to update "
                  << totalLines << " line(s) via SDK set_lines() API (hot reload)";
      }
    }

    if (isEmptyArray) {
      // Empty array - clear all lines
      std::map<int, cvedix_objects::cvedix_line> emptyLines;
      bool clearSuccess = baCrosslineNode->set_lines(emptyLines);
      if (clearSuccess) {
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[API] updateLinesRuntime: ✓ Successfully cleared all "
                       "lines via hot reload";
        }
        return true;
      } else {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] updateLinesRuntime: Failed to clear lines, "
                          "fallback to restart";
        }
        return false;
      }
    }

    // For non-empty array: Use set_lines() to replace all lines
    // This is the most reliable approach - it ensures config and runtime are always in sync
    // Note: set_lines() doesn't preserve names/colors/directions, but it's the only reliable way
    // to update lines at runtime without knowing the current state
    
    // Parse lines in format for set_lines() (basic line info only)
    auto lines = parseLinesFromJson(linesArray);
    if (!lines.empty()) {
      // Wrap set_lines() call in try-catch to handle potential crashes
      // when node is under heavy load (queue full)
      try {
        bool success = baCrosslineNode->set_lines(lines);
        if (success) {
          if (isApiLoggingEnabled()) {
            PLOG_INFO << "[API] updateLinesRuntime: ✓ Successfully updated "
                      << lines.size() << " line(s) via set_lines() API (no restart needed)";
            PLOG_WARNING << "[API] updateLinesRuntime: NOTE: set_lines() API updates line coordinates only. "
                            "Line names, colors, and directions are preserved in config but may not be "
                            "reflected in runtime until instance restart. For full feature support, "
                            "consider restarting instance.";
          }
          return true;
        } else {
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] updateLinesRuntime: set_lines() returned false, fallback to restart";
          }
          return false;
        }
      } catch (const std::exception &e) {
        if (isApiLoggingEnabled()) {
          PLOG_ERROR << "[API] updateLinesRuntime: Exception calling set_lines(): " << e.what()
                     << " - Node may be under heavy load (queue full). Fallback to restart.";
        }
        return false; // Fallback to restart for safety
      } catch (...) {
        if (isApiLoggingEnabled()) {
          PLOG_ERROR << "[API] updateLinesRuntime: Unknown exception calling set_lines() - "
                        "Node may be under heavy load. Fallback to restart.";
        }
        return false; // Fallback to restart for safety
      }
    } else {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] updateLinesRuntime: Failed to parse lines, fallback to restart";
      }
      return false;
    }

  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] updateLinesRuntime: Exception updating lines: "
                 << e.what() << ", fallback to restart";
    }
    return false; // Fallback to restart
  } catch (...) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] updateLinesRuntime: Unknown exception updating "
                    "lines, fallback to restart";
    }
    return false; // Fallback to restart
  }
}
