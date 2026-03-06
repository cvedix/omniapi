#include "core/securt_pipeline_integration.h"
#include "core/area_storage.h"
#include <json/json.h>
#include <json/writer.h>
#include <sstream>

std::string SecuRTPipelineIntegration::convertLinesToCrossingLinesFormat(
    const SecuRTLineManager *lineManager, const std::string &instanceId) {
  if (!lineManager) {
    return "";
  }

  // Get all lines
  Json::Value allLines = lineManager->getAllLines(instanceId);
  if (allLines.isNull() || allLines.empty()) {
    return "";
  }

  Json::Value crossingLinesArray(Json::arrayValue);

  // Process counting lines
  if (allLines.isMember("countingLines") && allLines["countingLines"].isArray()) {
    for (const auto &line : allLines["countingLines"]) {
      Json::Value converted = convertLineToCrossingLineFormat(line);
      if (!converted.isNull()) {
        crossingLinesArray.append(converted);
      }
    }
  }

  // Process crossing lines
  if (allLines.isMember("crossingLines") && allLines["crossingLines"].isArray()) {
    for (const auto &line : allLines["crossingLines"]) {
      Json::Value converted = convertLineToCrossingLineFormat(line);
      if (!converted.isNull()) {
        crossingLinesArray.append(converted);
      }
    }
  }

  // Process tailgating lines (also use crossing line format)
  if (allLines.isMember("tailgatingLines") &&
      allLines["tailgatingLines"].isArray()) {
    for (const auto &line : allLines["tailgatingLines"]) {
      Json::Value converted = convertLineToCrossingLineFormat(line);
      if (!converted.isNull()) {
        crossingLinesArray.append(converted);
      }
    }
  }

  if (crossingLinesArray.empty()) {
    return "";
  }

  // Convert to JSON string
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString(builder, crossingLinesArray);
}

Json::Value SecuRTPipelineIntegration::convertLineToCrossingLineFormat(
    const Json::Value &line) {
  if (!line.isObject() || !line.isMember("coordinates") ||
      !line["coordinates"].isArray() || line["coordinates"].size() < 2) {
    return Json::Value();
  }

  Json::Value result;

  // Copy id if exists
  if (line.isMember("id") && line["id"].isString()) {
    result["id"] = line["id"];
  }

  // Copy name if exists
  if (line.isMember("name") && line["name"].isString()) {
    result["name"] = line["name"];
  }

  // Copy coordinates (first and last point for line)
  Json::Value coords(Json::arrayValue);
  const auto &lineCoords = line["coordinates"];
  if (lineCoords.size() >= 2) {
    coords.append(lineCoords[0]); // Start point
    coords.append(lineCoords[lineCoords.size() - 1]); // End point
  }
  result["coordinates"] = coords;

  // Copy classes
  if (line.isMember("classes") && line["classes"].isArray()) {
    result["classes"] = line["classes"];
  }

  // Copy direction
  if (line.isMember("direction") && line["direction"].isString()) {
    result["direction"] = line["direction"];
  } else {
    result["direction"] = "Both"; // Default
  }

  // Copy color if exists
  if (line.isMember("color") && line["color"].isArray()) {
    result["color"] = line["color"];
  }

  return result;
}

std::string SecuRTPipelineIntegration::convertAreasToJsonFormat(
    const AreaManager *areaManager, const std::string &instanceId) {
  if (!areaManager) {
    return "";
  }

  Json::Value areasJson = getAllAreasAsJson(areaManager, instanceId);
  if (areasJson.isNull() || areasJson.empty()) {
    return "";
  }

  // Convert to JSON string
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  std::ostringstream oss;
  std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
  writer->write(areasJson, &oss);
  return oss.str();
}

Json::Value SecuRTPipelineIntegration::getAllLinesAsJson(
    const SecuRTLineManager *lineManager, const std::string &instanceId) {
  if (!lineManager) {
    return Json::Value(Json::objectValue);
  }
  return lineManager->getAllLines(instanceId);
}

Json::Value SecuRTPipelineIntegration::getAllAreasAsJson(
    const AreaManager *areaManager, const std::string &instanceId) {
  if (!areaManager) {
    return Json::Value(Json::objectValue);
  }

  // Get all areas grouped by type
  auto areasMap = areaManager->getAllAreas(instanceId);
  Json::Value result(Json::objectValue);

  // Convert each area type to JSON array
  for (const auto &[areaType, areas] : areasMap) {
    Json::Value areasArray(Json::arrayValue);
    for (const auto &area : areas) {
      areasArray.append(area);
    }
    result[areaType] = areasArray;
  }

  return result;
}

std::vector<std::pair<int, int>>
SecuRTPipelineIntegration::convertNormalizedToPixel(const Json::Value &coords,
                                                    int width, int height) {
  std::vector<std::pair<int, int>> pixelCoords;
  if (!coords.isArray()) {
    return pixelCoords;
  }

  for (const auto &coord : coords) {
    if (coord.isObject() && coord.isMember("x") && coord.isMember("y")) {
      double x = coord["x"].asDouble();
      double y = coord["y"].asDouble();

      // Check if already in pixel coordinates (values > 1.0)
      if (x > 1.0 || y > 1.0) {
        pixelCoords.push_back({static_cast<int>(x), static_cast<int>(y)});
      } else {
        // Convert from normalized to pixel
        pixelCoords.push_back(
            {static_cast<int>(x * width), static_cast<int>(y * height)});
      }
    }
  }

  return pixelCoords;
}

std::string SecuRTPipelineIntegration::convertAreasToStopZonesFormat(
    const AreaManager *areaManager, const std::string &instanceId) {
  if (!areaManager) {
    return "";
  }

  // Get all areas grouped by type
  auto areasMap = areaManager->getAllAreas(instanceId);
  if (areasMap.empty()) {
    return "";
  }

  Json::Value stopZonesArray(Json::arrayValue);

  // Convert all areas to StopZones format (ba_stop node format)
  // Each area becomes a zone with roi (polygon coordinates)
  for (const auto &[areaType, areas] : areasMap) {
    for (const auto &area : areas) {
      if (!area.isObject() || !area.isMember("coordinates") ||
          !area["coordinates"].isArray() || area["coordinates"].size() < 3) {
        continue; // Skip invalid areas
      }

      Json::Value zone(Json::objectValue);

      // Copy id if exists
      if (area.isMember("id") && area["id"].isString()) {
        zone["id"] = area["id"];
      }

      // Copy name if exists
      if (area.isMember("name") && area["name"].isString()) {
        zone["name"] = area["name"];
      } else {
        zone["name"] = areaType + " Area";
      }

      // Convert coordinates to roi format (ba_stop uses "roi" field)
      Json::Value roi(Json::arrayValue);
      const auto &coords = area["coordinates"];
      for (const auto &coord : coords) {
        if (coord.isObject() && coord.isMember("x") && coord.isMember("y")) {
          Json::Value point(Json::objectValue);
          // Convert to pixel coordinates if normalized
          double x = coord["x"].asDouble();
          double y = coord["y"].asDouble();
          
          // If normalized (0.0-1.0), convert to pixel (assume 1920x1080 default)
          // Otherwise use as-is (already in pixels)
          if (x <= 1.0 && y <= 1.0 && x >= 0.0 && y >= 0.0) {
            // Normalized coordinates - convert to pixels
            // Note: Actual frame size will be determined at runtime
            // For now, use default 1920x1080
            point["x"] = static_cast<int>(x * 1920);
            point["y"] = static_cast<int>(y * 1080);
          } else {
            // Already in pixels
            point["x"] = static_cast<int>(x);
            point["y"] = static_cast<int>(y);
          }
          roi.append(point);
        }
      }

      if (roi.size() >= 3) {
        zone["roi"] = roi;
        stopZonesArray.append(zone);
      }
    }
  }

  if (stopZonesArray.empty()) {
    return "";
  }

  // Convert to JSON string
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString(builder, stopZonesArray);
}

std::string SecuRTPipelineIntegration::convertAreasToLoiteringFormat(
    const AreaManager *areaManager, const std::string &instanceId) {
  if (!areaManager) {
    return "";
  }

  // Get all areas grouped by type
  auto areasMap = areaManager->getAllAreas(instanceId);
  if (areasMap.empty()) {
    return "";
  }

  Json::Value loiteringAreasArray(Json::arrayValue);

  // Find loitering areas and convert to LoiteringAreas format
  auto loiteringIt = areasMap.find("loitering");
  if (loiteringIt != areasMap.end()) {
    for (const auto &area : loiteringIt->second) {
      if (!area.isObject() || !area.isMember("coordinates") ||
          !area["coordinates"].isArray() || area["coordinates"].size() < 3) {
        continue; // Skip invalid areas
      }

      Json::Value loiteringArea(Json::objectValue);

      // Copy id if exists
      if (area.isMember("id") && area["id"].isString()) {
        loiteringArea["id"] = area["id"];
      }

      // Copy name if exists
      if (area.isMember("name") && area["name"].isString()) {
        loiteringArea["name"] = area["name"];
      } else {
        loiteringArea["name"] = "Loitering Area";
      }

      // Copy coordinates
      loiteringArea["coordinates"] = area["coordinates"];

      // Copy seconds (alarm threshold)
      if (area.isMember("seconds") && area["seconds"].isNumeric()) {
        loiteringArea["seconds"] = area["seconds"];
      } else {
        loiteringArea["seconds"] = 5; // Default
      }

      // Copy channel if exists
      if (area.isMember("channel") && area["channel"].isNumeric()) {
        loiteringArea["channel"] = area["channel"];
      }

      loiteringAreasArray.append(loiteringArea);
    }
  }

  if (loiteringAreasArray.empty()) {
    return "";
  }

  // Convert to JSON string
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString(builder, loiteringAreasArray);
}

