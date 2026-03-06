#pragma once

#include "core/analytics_common_types.h"
#include <json/json.h>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Exclusion Area structure
 */
struct ExclusionArea {
  std::vector<Coordinate> coordinates;
  std::vector<std::string> classes;  // "Person", "Vehicle", etc.

  /**
   * @brief Convert to JSON
   */
  Json::Value toJson() const {
    Json::Value json;
    Json::Value coordsArray(Json::arrayValue);
    for (const auto &coord : coordinates) {
      coordsArray.append(coord.toJson());
    }
    json["coordinates"] = coordsArray;

    Json::Value classesArray(Json::arrayValue);
    for (const auto &cls : classes) {
      classesArray.append(cls);
    }
    json["classes"] = classesArray;
    return json;
  }

  /**
   * @brief Create from JSON
   */
  static ExclusionArea fromJson(const Json::Value &json) {
    ExclusionArea area;
    if (json.isMember("coordinates") && json["coordinates"].isArray()) {
      for (const auto &coord : json["coordinates"]) {
        area.coordinates.push_back(Coordinate::fromJson(coord));
      }
    }
    if (json.isMember("classes") && json["classes"].isArray()) {
      for (const auto &cls : json["classes"]) {
        if (cls.isString()) {
          area.classes.push_back(cls.asString());
        }
      }
    }
    return area;
  }
};

/**
 * @brief SecuRT Feature Configuration
 *
 * Stores all feature settings for a SecuRT instance.
 */
struct SecuRTFeatureConfig {
  std::vector<Coordinate> motionArea;
  std::vector<std::string> featureExtractionTypes;  // "Face", "Person", "Vehicle"
  std::string attributesExtractionMode;            // "Off", "Person", "Vehicle", "Both"
  std::string performanceProfile;                  // "Performance", "Balanced", "Accurate"
  bool faceDetectionEnabled = false;
  bool lprEnabled = false;
  bool pipEnabled = false;
  bool surrenderDetectionEnabled = false;
  std::vector<std::vector<Coordinate>> maskingAreas;
  std::vector<ExclusionArea> exclusionAreas;

  /**
   * @brief Convert to JSON (for debugging/storage)
   */
  Json::Value toJson() const {
    Json::Value json;

    // Motion area
    Json::Value motionAreaArray(Json::arrayValue);
    for (const auto &coord : motionArea) {
      motionAreaArray.append(coord.toJson());
    }
    json["motionArea"] = motionAreaArray;

    // Feature extraction types
    Json::Value typesArray(Json::arrayValue);
    for (const auto &type : featureExtractionTypes) {
      typesArray.append(type);
    }
    json["featureExtractionTypes"] = typesArray;

    json["attributesExtractionMode"] = attributesExtractionMode;
    json["performanceProfile"] = performanceProfile;
    json["faceDetectionEnabled"] = faceDetectionEnabled;
    json["lprEnabled"] = lprEnabled;
    json["pipEnabled"] = pipEnabled;
    json["surrenderDetectionEnabled"] = surrenderDetectionEnabled;

    // Masking areas
    Json::Value maskingArray(Json::arrayValue);
    for (const auto &area : maskingAreas) {
      Json::Value areaArray(Json::arrayValue);
      for (const auto &coord : area) {
        areaArray.append(coord.toJson());
      }
      maskingArray.append(areaArray);
    }
    json["maskingAreas"] = maskingArray;

    // Exclusion areas
    Json::Value exclusionArray(Json::arrayValue);
    for (const auto &area : exclusionAreas) {
      exclusionArray.append(area.toJson());
    }
    json["exclusionAreas"] = exclusionArray;

    return json;
  }
};

