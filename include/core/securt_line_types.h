#pragma once

#include "core/analytics_common_types.h"
#include <json/json.h>
#include <string>
#include <vector>

/**
 * @brief SecuRT Line Types
 *
 * Defines base classes and specific line types for SecuRT analytics.
 */

/**
 * @brief Direction enum for line crossing detection
 */
enum class LineDirection {
  Up,
  Down,
  Both
};

/**
 * @brief Convert direction string to enum
 */
inline LineDirection directionFromString(const std::string &dir) {
  if (dir == "Up") return LineDirection::Up;
  if (dir == "Down") return LineDirection::Down;
  return LineDirection::Both;
}

/**
 * @brief Convert direction enum to string
 */
inline std::string directionToString(LineDirection dir) {
  switch (dir) {
    case LineDirection::Up:
      return "Up";
    case LineDirection::Down:
      return "Down";
    case LineDirection::Both:
      return "Both";
    default:
      return "Both";
  }
}

/**
 * @brief Base line structure
 */
struct LineBase {
  std::string id;
  std::string name;
  std::vector<Coordinate> coordinates;  // Exactly 2 points for line
  std::vector<ObjectClass> classes;
  LineDirection direction;
  ColorRGBA color;

  /**
   * @brief Convert to JSON
   */
  Json::Value toJson() const {
    Json::Value json;
    json["id"] = id;
    json["name"] = name;
    Json::Value coords(Json::arrayValue);
    for (const auto &coord : coordinates) {
      coords.append(coord.toJson());
    }
    json["coordinates"] = coords;
    Json::Value classesArray(Json::arrayValue);
    for (const auto &cls : classes) {
      classesArray.append(classToString(cls));
    }
    json["classes"] = classesArray;
    json["direction"] = directionToString(direction);
    json["color"] = color.toJson();
    return json;
  }
};

/**
 * @brief Counting Line
 * Counts objects crossing line
 */
struct CountingLine : LineBase {
  /**
   * @brief Create from JSON (LineWrite schema)
   */
  static CountingLine fromJson(const Json::Value &json, const std::string &lineId = "") {
    CountingLine line;
    if (!lineId.empty()) {
      line.id = lineId;
    }
    if (json.isMember("name") && json["name"].isString()) {
      line.name = json["name"].asString();
    }
    if (json.isMember("coordinates") && json["coordinates"].isArray()) {
      for (const auto &coord : json["coordinates"]) {
        line.coordinates.push_back(Coordinate::fromJson(coord));
      }
    }
    if (json.isMember("classes") && json["classes"].isArray()) {
      for (const auto &cls : json["classes"]) {
        if (cls.isString()) {
          line.classes.push_back(classFromString(cls.asString()));
        }
      }
    }
    if (json.isMember("direction") && json["direction"].isString()) {
      line.direction = directionFromString(json["direction"].asString());
    } else {
      line.direction = LineDirection::Both;
    }
    if (json.isMember("color") && json["color"].isArray()) {
      line.color = ColorRGBA::fromJson(json["color"]);
    } else {
      line.color = ColorRGBA{0.0, 0.0, 0.0, 1.0};
    }
    return line;
  }
};

/**
 * @brief Crossing Line
 * Detects objects crossing line by direction
 */
struct CrossingLine : LineBase {
  /**
   * @brief Create from JSON (LineWrite schema)
   */
  static CrossingLine fromJson(const Json::Value &json, const std::string &lineId = "") {
    CrossingLine line;
    if (!lineId.empty()) {
      line.id = lineId;
    }
    if (json.isMember("name") && json["name"].isString()) {
      line.name = json["name"].asString();
    }
    if (json.isMember("coordinates") && json["coordinates"].isArray()) {
      for (const auto &coord : json["coordinates"]) {
        line.coordinates.push_back(Coordinate::fromJson(coord));
      }
    }
    if (json.isMember("classes") && json["classes"].isArray()) {
      for (const auto &cls : json["classes"]) {
        if (cls.isString()) {
          line.classes.push_back(classFromString(cls.asString()));
        }
      }
    }
    if (json.isMember("direction") && json["direction"].isString()) {
      line.direction = directionFromString(json["direction"].asString());
    } else {
      line.direction = LineDirection::Both;
    }
    if (json.isMember("color") && json["color"].isArray()) {
      line.color = ColorRGBA::fromJson(json["color"]);
    } else {
      line.color = ColorRGBA{0.0, 0.0, 0.0, 1.0};
    }
    return line;
  }
};

/**
 * @brief Tailgating Line
 * Detects multiple objects crossing simultaneously within time window
 */
struct TailgatingLine : LineBase {
  int seconds;  // Time window for tailgating detection

  /**
   * @brief Create from JSON (LineWrite schema)
   */
  static TailgatingLine fromJson(const Json::Value &json, const std::string &lineId = "") {
    TailgatingLine line;
    if (!lineId.empty()) {
      line.id = lineId;
    }
    if (json.isMember("name") && json["name"].isString()) {
      line.name = json["name"].asString();
    }
    if (json.isMember("coordinates") && json["coordinates"].isArray()) {
      for (const auto &coord : json["coordinates"]) {
        line.coordinates.push_back(Coordinate::fromJson(coord));
      }
    }
    if (json.isMember("classes") && json["classes"].isArray()) {
      for (const auto &cls : json["classes"]) {
        if (cls.isString()) {
          line.classes.push_back(classFromString(cls.asString()));
        }
      }
    }
    if (json.isMember("direction") && json["direction"].isString()) {
      line.direction = directionFromString(json["direction"].asString());
    } else {
      line.direction = LineDirection::Both;
    }
    if (json.isMember("color") && json["color"].isArray()) {
      line.color = ColorRGBA::fromJson(json["color"]);
    } else {
      line.color = ColorRGBA{0.0, 0.0, 0.0, 1.0};
    }
    if (json.isMember("seconds") && json["seconds"].isInt()) {
      line.seconds = json["seconds"].asInt();
    } else {
      line.seconds = 1;  // Default 1 second
    }
    return line;
  }

  /**
   * @brief Convert to JSON
   */
  Json::Value toJson() const {
    Json::Value json = LineBase::toJson();
    json["seconds"] = seconds;
    return json;
  }
};

/**
 * @brief Line type enum
 */
enum class LineType {
  Counting,
  Crossing,
  Tailgating
};

/**
 * @brief Convert line type string to enum
 */
inline LineType lineTypeFromString(const std::string &type) {
  if (type == "counting") return LineType::Counting;
  if (type == "crossing") return LineType::Crossing;
  if (type == "tailgating") return LineType::Tailgating;
  return LineType::Counting;
}

/**
 * @brief Convert line type enum to string
 */
inline std::string lineTypeToString(LineType type) {
  switch (type) {
    case LineType::Counting:
      return "counting";
    case LineType::Crossing:
      return "crossing";
    case LineType::Tailgating:
      return "tailgating";
    default:
      return "counting";
  }
}

