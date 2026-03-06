#include "core/securt_line_manager.h"

SecuRTLineManager::SecuRTLineManager() {}

std::string SecuRTLineManager::createCountingLine(const std::string &instanceId,
                                                   const Json::Value &json,
                                                   const std::string &lineId) {
  std::string error;
  if (!validateLine(json, LineType::Counting, error)) {
    return "";
  }

  std::string finalLineId = lineId.empty() ? UUIDGenerator::generateUUID() : lineId;
  auto line = std::make_unique<CountingLine>(CountingLine::fromJson(json, finalLineId));
  line->id = finalLineId;
  storage_.addCountingLine(instanceId, std::move(line));
  return finalLineId;
}

std::string SecuRTLineManager::createCrossingLine(const std::string &instanceId,
                                                    const Json::Value &json,
                                                    const std::string &lineId) {
  std::string error;
  if (!validateLine(json, LineType::Crossing, error)) {
    return "";
  }

  std::string finalLineId = lineId.empty() ? UUIDGenerator::generateUUID() : lineId;
  auto line = std::make_unique<CrossingLine>(CrossingLine::fromJson(json, finalLineId));
  line->id = finalLineId;
  storage_.addCrossingLine(instanceId, std::move(line));
  return finalLineId;
}

std::string SecuRTLineManager::createTailgatingLine(const std::string &instanceId,
                                                     const Json::Value &json,
                                                     const std::string &lineId) {
  std::string error;
  if (!validateLine(json, LineType::Tailgating, error)) {
    return "";
  }

  std::string finalLineId = lineId.empty() ? UUIDGenerator::generateUUID() : lineId;
  auto line = std::make_unique<TailgatingLine>(TailgatingLine::fromJson(json, finalLineId));
  line->id = finalLineId;
  storage_.addTailgatingLine(instanceId, std::move(line));
  return finalLineId;
}

bool SecuRTLineManager::updateLine(const std::string &instanceId,
                                   const std::string &lineId,
                                   const Json::Value &json, LineType type) {
  std::string error;
  if (!validateLine(json, type, error)) {
    return false;
  }

  return storage_.updateLine(instanceId, lineId, json, type);
}

bool SecuRTLineManager::deleteLine(const std::string &instanceId,
                                    const std::string &lineId) {
  return storage_.deleteLine(instanceId, lineId);
}

void SecuRTLineManager::deleteAllLines(const std::string &instanceId) {
  storage_.deleteAllLines(instanceId);
}

Json::Value SecuRTLineManager::getAllLines(const std::string &instanceId) const {
  return storage_.getAllLines(instanceId);
}

Json::Value SecuRTLineManager::getCountingLines(const std::string &instanceId) const {
  Json::Value result(Json::arrayValue);
  auto lines = storage_.getCountingLines(instanceId);
  for (const auto *linePtr : lines) {
    result.append(linePtr->toJson());
  }
  return result;
}

Json::Value SecuRTLineManager::getCrossingLines(const std::string &instanceId) const {
  Json::Value result(Json::arrayValue);
  auto lines = storage_.getCrossingLines(instanceId);
  for (const auto *linePtr : lines) {
    result.append(linePtr->toJson());
  }
  return result;
}

Json::Value SecuRTLineManager::getTailgatingLines(const std::string &instanceId) const {
  Json::Value result(Json::arrayValue);
  auto lines = storage_.getTailgatingLines(instanceId);
  for (const auto *linePtr : lines) {
    result.append(linePtr->toJson());
  }
  return result;
}

bool SecuRTLineManager::hasLine(const std::string &instanceId,
                                 const std::string &lineId) const {
  return storage_.hasLine(instanceId, lineId);
}

bool SecuRTLineManager::validateLine(const Json::Value &json, LineType type,
                                      std::string &error) const {
  // Validate coordinates (exactly 2 points)
  if (!json.isMember("coordinates") || !json["coordinates"].isArray()) {
    error = "Field 'coordinates' is required and must be an array";
    return false;
  }

  std::string coordError;
  if (!validateCoordinates(json["coordinates"], coordError)) {
    error = coordError;
    return false;
  }

  // Validate classes (at least one)
  if (!json.isMember("classes") || !json["classes"].isArray()) {
    error = "Field 'classes' is required and must be an array";
    return false;
  }

  std::string classesError;
  if (!validateClasses(json["classes"], classesError)) {
    error = classesError;
    return false;
  }

  if (json["classes"].size() == 0) {
    error = "At least one class must be specified";
    return false;
  }

  // Validate direction (optional, defaults to Both)
  if (json.isMember("direction")) {
    std::string dirError;
    if (!validateDirection(json["direction"], dirError)) {
      error = dirError;
      return false;
    }
  }

  // Validate color (optional)
  if (json.isMember("color")) {
    std::string colorError;
    if (!validateColor(json["color"], colorError)) {
      error = colorError;
      return false;
    }
  }

  // Validate line-specific fields
  if (type == LineType::Tailgating) {
    std::string tailgatingError;
    if (!validateTailgatingFields(json, tailgatingError)) {
      error = tailgatingError;
      return false;
    }
  }

  return true;
}

bool SecuRTLineManager::validateCoordinates(const Json::Value &coordinates,
                                             std::string &error) const {
  if (!coordinates.isArray()) {
    error = "Coordinates must be an array";
    return false;
  }

  if (coordinates.size() != 2) {
    error = "Line must have exactly 2 coordinates (start and end points)";
    return false;
  }

  for (Json::ArrayIndex i = 0; i < coordinates.size(); ++i) {
    const auto &coord = coordinates[i];
    if (!coord.isObject()) {
      error = "Coordinate at index " + std::to_string(i) + " must be an object";
      return false;
    }

    if (!coord.isMember("x") || !coord.isMember("y")) {
      error = "Coordinate at index " + std::to_string(i) +
              " must have 'x' and 'y' fields";
      return false;
    }

    if (!coord["x"].isNumeric() || !coord["y"].isNumeric()) {
      error = "Coordinate 'x' and 'y' at index " + std::to_string(i) +
              " must be numbers";
      return false;
    }
  }

  return true;
}

bool SecuRTLineManager::validateClasses(const Json::Value &classes,
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

bool SecuRTLineManager::validateDirection(const Json::Value &direction,
                                           std::string &error) const {
  if (!direction.isString()) {
    error = "Direction must be a string";
    return false;
  }

  std::string dirStr = direction.asString();
  if (dirStr != "Up" && dirStr != "Down" && dirStr != "Both") {
    error = "Invalid direction: " + dirStr +
            ". Allowed values: Up, Down, Both";
    return false;
  }

  return true;
}

bool SecuRTLineManager::validateColor(const Json::Value &color,
                                      std::string &error) const {
  if (!color.isArray()) {
    error = "Color must be an array";
    return false;
  }

  if (color.size() != 4) {
    error = "Color must contain exactly 4 values (RGBA)";
    return false;
  }

  for (Json::ArrayIndex i = 0; i < color.size(); ++i) {
    if (!color[i].isNumeric()) {
      error = "Color value at index " + std::to_string(i) + " must be a number";
      return false;
    }

    double val = color[i].asDouble();
    if (val < 0.0 || val > 1.0) {
      error = "Color value at index " + std::to_string(i) +
              " must be in range [0.0, 1.0]";
      return false;
    }
  }

  return true;
}

bool SecuRTLineManager::validateTailgatingFields(const Json::Value &json,
                                                  std::string &error) const {
  if (json.isMember("seconds")) {
    if (!json["seconds"].isInt()) {
      error = "Field 'seconds' must be an integer";
      return false;
    }

    int seconds = json["seconds"].asInt();
    if (seconds <= 0) {
      error = "Field 'seconds' must be greater than 0";
      return false;
    }
  }

  return true;
}

