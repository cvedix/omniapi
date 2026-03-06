#pragma once

#include <json/json.h>
#include <string>

/**
 * @brief SecuRT Instance Model
 *
 * Represents a SecuRT instance with all its configuration.
 */
struct SecuRTInstance {
  std::string instanceId;
  std::string name;
  std::string detectorMode = "SmartDetection";  // "SmartDetection", "Detection"
  std::string detectionSensitivity = "Low";     // "Low", "Medium", "High"
  std::string movementSensitivity = "Low";       // "Low", "Medium", "High"
  std::string sensorModality = "RGB";            // "RGB", "Thermal"
  double frameRateLimit = 0.0;
  bool metadataMode = false;
  bool statisticsMode = false;
  bool diagnosticsMode = false;
  bool debugMode = false;

  /**
   * @brief Convert to JSON
   */
  Json::Value toJson() const {
    Json::Value json;
    json["instanceId"] = instanceId;
    json["name"] = name;
    json["detectorMode"] = detectorMode;
    json["detectionSensitivity"] = detectionSensitivity;
    json["movementSensitivity"] = movementSensitivity;
    json["sensorModality"] = sensorModality;
    json["frameRateLimit"] = frameRateLimit;
    json["metadataMode"] = metadataMode;
    json["statisticsMode"] = statisticsMode;
    json["diagnosticsMode"] = diagnosticsMode;
    json["debugMode"] = debugMode;
    return json;
  }

  /**
   * @brief Create from JSON
   */
  static SecuRTInstance fromJson(const Json::Value &json) {
    SecuRTInstance instance;
    if (json.isMember("instanceId") && json["instanceId"].isString()) {
      instance.instanceId = json["instanceId"].asString();
    }
    if (json.isMember("name") && json["name"].isString()) {
      instance.name = json["name"].asString();
    }
    if (json.isMember("detectorMode") && json["detectorMode"].isString()) {
      instance.detectorMode = json["detectorMode"].asString();
    }
    if (json.isMember("detectionSensitivity") &&
        json["detectionSensitivity"].isString()) {
      instance.detectionSensitivity = json["detectionSensitivity"].asString();
    }
    if (json.isMember("movementSensitivity") &&
        json["movementSensitivity"].isString()) {
      instance.movementSensitivity = json["movementSensitivity"].asString();
    }
    if (json.isMember("sensorModality") &&
        json["sensorModality"].isString()) {
      instance.sensorModality = json["sensorModality"].asString();
    }
    if (json.isMember("frameRateLimit") &&
        json["frameRateLimit"].isNumeric()) {
      instance.frameRateLimit = json["frameRateLimit"].asDouble();
    }
    if (json.isMember("metadataMode") && json["metadataMode"].isBool()) {
      instance.metadataMode = json["metadataMode"].asBool();
    }
    if (json.isMember("statisticsMode") && json["statisticsMode"].isBool()) {
      instance.statisticsMode = json["statisticsMode"].asBool();
    }
    if (json.isMember("diagnosticsMode") && json["diagnosticsMode"].isBool()) {
      instance.diagnosticsMode = json["diagnosticsMode"].asBool();
    }
    if (json.isMember("debugMode") && json["debugMode"].isBool()) {
      instance.debugMode = json["debugMode"].asBool();
    }
    return instance;
  }
};

/**
 * @brief SecuRT Instance Write Schema
 *
 * Used for creating/updating SecuRT instances.
 * Tracks which fields are set for partial updates.
 */
struct SecuRTInstanceWrite {
  std::string name;
  std::string detectorMode = "SmartDetection";
  std::string detectionSensitivity = "Low";
  std::string movementSensitivity = "Low";
  std::string sensorModality = "RGB";
  double frameRateLimit = 0.0;
  bool metadataMode = false;
  bool statisticsMode = false;
  bool diagnosticsMode = false;
  bool debugMode = false;

  // Track which fields are set (for partial updates)
  bool nameSet = false;
  bool detectorModeSet = false;
  bool detectionSensitivitySet = false;
  bool movementSensitivitySet = false;
  bool sensorModalitySet = false;
  bool frameRateLimitSet = false;
  bool metadataModeSet = false;
  bool statisticsModeSet = false;
  bool diagnosticsModeSet = false;
  bool debugModeSet = false;

  /**
   * @brief Create from JSON
   */
  static SecuRTInstanceWrite fromJson(const Json::Value &json) {
    SecuRTInstanceWrite write;
    if (json.isMember("name") && json["name"].isString()) {
      write.name = json["name"].asString();
      write.nameSet = true;
    }
    if (json.isMember("detectorMode") && json["detectorMode"].isString()) {
      write.detectorMode = json["detectorMode"].asString();
      write.detectorModeSet = true;
    }
    if (json.isMember("detectionSensitivity") &&
        json["detectionSensitivity"].isString()) {
      write.detectionSensitivity = json["detectionSensitivity"].asString();
      write.detectionSensitivitySet = true;
    }
    if (json.isMember("movementSensitivity") &&
        json["movementSensitivity"].isString()) {
      write.movementSensitivity = json["movementSensitivity"].asString();
      write.movementSensitivitySet = true;
    }
    if (json.isMember("sensorModality") &&
        json["sensorModality"].isString()) {
      write.sensorModality = json["sensorModality"].asString();
      write.sensorModalitySet = true;
    }
    if (json.isMember("frameRateLimit") &&
        json["frameRateLimit"].isNumeric()) {
      write.frameRateLimit = json["frameRateLimit"].asDouble();
      write.frameRateLimitSet = true;
    }
    if (json.isMember("metadataMode") && json["metadataMode"].isBool()) {
      write.metadataMode = json["metadataMode"].asBool();
      write.metadataModeSet = true;
    }
    if (json.isMember("statisticsMode") && json["statisticsMode"].isBool()) {
      write.statisticsMode = json["statisticsMode"].asBool();
      write.statisticsModeSet = true;
    }
    if (json.isMember("diagnosticsMode") && json["diagnosticsMode"].isBool()) {
      write.diagnosticsMode = json["diagnosticsMode"].asBool();
      write.diagnosticsModeSet = true;
    }
    if (json.isMember("debugMode") && json["debugMode"].isBool()) {
      write.debugMode = json["debugMode"].asBool();
      write.debugModeSet = true;
    }
    return write;
  }
};

