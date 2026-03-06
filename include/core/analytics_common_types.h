#pragma once

#include <json/json.h>
#include <string>

/**
 * @brief Common types for analytics entities (Lines and Areas)
 * 
 * This file contains shared types used by both SecuRT Lines and Areas features.
 */

/**
 * @brief Coordinate point
 */
struct Coordinate {
  double x;
  double y;

  /**
   * @brief Create from JSON
   */
  static Coordinate fromJson(const Json::Value &json) {
    Coordinate coord;
    if (json.isMember("x") && json["x"].isNumeric()) {
      coord.x = json["x"].asDouble();
    }
    if (json.isMember("y") && json["y"].isNumeric()) {
      coord.y = json["y"].asDouble();
    }
    return coord;
  }

  /**
   * @brief Convert to JSON
   */
  Json::Value toJson() const {
    Json::Value json;
    json["x"] = x;
    json["y"] = y;
    return json;
  }
};

/**
 * @brief Color RGBA (0.0-1.0 range)
 */
struct ColorRGBA {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  double a = 1.0;

  /**
   * @brief Create from JSON array [r, g, b, a]
   * Supports both 0-255 integer format and 0.0-1.0 float format
   * Automatically converts 0-255 to 0.0-1.0 range
   */
  static ColorRGBA fromJson(const Json::Value &json) {
    ColorRGBA color;
    if (json.isArray() && json.size() >= 4) {
      if (json[0].isNumeric()) {
        double r = json[0].asDouble();
        // If value > 1.0, assume it's 0-255 format and convert to 0.0-1.0
        color.r = (r > 1.0) ? (r / 255.0) : r;
      }
      if (json[1].isNumeric()) {
        double g = json[1].asDouble();
        color.g = (g > 1.0) ? (g / 255.0) : g;
      }
      if (json[2].isNumeric()) {
        double b = json[2].asDouble();
        color.b = (b > 1.0) ? (b / 255.0) : b;
      }
      if (json[3].isNumeric()) {
        double a = json[3].asDouble();
        color.a = (a > 1.0) ? (a / 255.0) : a;
      }
    }
    return color;
  }

  /**
   * @brief Convert to JSON array
   */
  Json::Value toJson() const {
    Json::Value json(Json::arrayValue);
    json.append(r);
    json.append(g);
    json.append(b);
    json.append(a);
    return json;
  }
};

/**
 * @brief Class enum for object types
 */
enum class ObjectClass {
  Person,
  Animal,
  Vehicle,
  Face,
  Unknown
};

/**
 * @brief Convert class string to enum
 */
inline ObjectClass classFromString(const std::string &cls) {
  if (cls == "Person") return ObjectClass::Person;
  if (cls == "Animal") return ObjectClass::Animal;
  if (cls == "Vehicle") return ObjectClass::Vehicle;
  if (cls == "Face") return ObjectClass::Face;
  return ObjectClass::Unknown;
}

/**
 * @brief Convert string to ObjectClass (alias for classFromString)
 */
inline ObjectClass stringToClass(const std::string &str) {
  return classFromString(str);
}

/**
 * @brief Convert class enum to string
 */
inline std::string classToString(ObjectClass cls) {
  switch (cls) {
    case ObjectClass::Person:
      return "Person";
    case ObjectClass::Animal:
      return "Animal";
    case ObjectClass::Vehicle:
      return "Vehicle";
    case ObjectClass::Face:
      return "Face";
    case ObjectClass::Unknown:
      return "Unknown";
    default:
      return "Unknown";
  }
}

