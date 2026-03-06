#pragma once

#include "core/analytics_common_types.h"
#include <json/json.h>
#include <string>
#include <vector>

/**
 * @brief Base area structure
 * Contains common fields for all area types
 */
struct AreaBase {
  std::string id;
  std::string name;
  std::vector<Coordinate> coordinates;
  std::vector<ObjectClass> classes;
  ColorRGBA color;

  /**
   * @brief Convert to JSON
   */
  Json::Value toJson() const {
    Json::Value json;
    json["id"] = id;
    json["name"] = name;

    // Convert coordinates
    Json::Value coords(Json::arrayValue);
    for (const auto &coord : coordinates) {
      coords.append(coord.toJson());
    }
    json["coordinates"] = coords;

    // Convert classes
    Json::Value classesJson(Json::arrayValue);
    for (const auto &cls : classes) {
      classesJson.append(classToString(cls));
    }
    json["classes"] = classesJson;

    // Convert color
    json["color"] = color.toJson();

    return json;
  }

  /**
   * @brief Create from JSON (base fields only)
   */
  static AreaBase fromJson(const Json::Value &json) {
    AreaBase area;

    if (json.isMember("id") && json["id"].isString()) {
      area.id = json["id"].asString();
    }

    if (json.isMember("name") && json["name"].isString()) {
      area.name = json["name"].asString();
    }

    // Parse coordinates
    if (json.isMember("coordinates") && json["coordinates"].isArray()) {
      for (const auto &coordJson : json["coordinates"]) {
        area.coordinates.push_back(Coordinate::fromJson(coordJson));
      }
    }

    // Parse classes
    if (json.isMember("classes") && json["classes"].isArray()) {
      for (const auto &classJson : json["classes"]) {
        if (classJson.isString()) {
          area.classes.push_back(stringToClass(classJson.asString()));
        }
      }
    }

    // Parse color
    if (json.isMember("color")) {
      area.color = ColorRGBA::fromJson(json["color"]);
    }

    return area;
  }
};

/**
 * @brief Base area write structure
 * Used for creating/updating areas (without ID)
 */
struct AreaBaseWrite {
  std::string name;
  std::vector<Coordinate> coordinates;
  std::vector<ObjectClass> classes;
  ColorRGBA color;

  /**
   * @brief Create from JSON
   */
  static AreaBaseWrite fromJson(const Json::Value &json) {
    AreaBaseWrite write;

    if (json.isMember("name") && json["name"].isString()) {
      write.name = json["name"].asString();
    }

    // Parse coordinates
    if (json.isMember("coordinates") && json["coordinates"].isArray()) {
      for (const auto &coordJson : json["coordinates"]) {
        write.coordinates.push_back(Coordinate::fromJson(coordJson));
      }
    }

    // Parse classes
    if (json.isMember("classes") && json["classes"].isArray()) {
      for (const auto &classJson : json["classes"]) {
        if (classJson.isString()) {
          write.classes.push_back(stringToClass(classJson.asString()));
        }
      }
    }

    // Parse color
    if (json.isMember("color")) {
      write.color = ColorRGBA::fromJson(json["color"]);
    }

    return write;
  }
};

