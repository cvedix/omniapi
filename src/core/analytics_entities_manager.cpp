#include "core/analytics_entities_manager.h"
#include "core/area_manager.h"
#include "core/area_storage.h"
#include "core/securt_line_manager.h"
#include "instances/instance_manager.h"
#include <iostream>

AreaManager *AnalyticsEntitiesManager::area_manager_ = nullptr;

void AnalyticsEntitiesManager::setAreaManager(AreaManager *manager) {
  area_manager_ = manager;
}
Json::Value AnalyticsEntitiesManager::getAnalyticsEntities(
    const std::string &instanceId) const {
  Json::Value result;
  
  // Areas
  result["crossingAreas"] = getCrossingAreas(instanceId);
  result["intrusionAreas"] = getIntrusionAreas(instanceId);
  result["loiteringAreas"] = getLoiteringAreas(instanceId);
  result["crowdingAreas"] = getCrowdingAreas(instanceId);
  result["occupancyAreas"] = getOccupancyAreas(instanceId);
  result["crowdEstimationAreas"] = getCrowdEstimationAreas(instanceId);
  result["dwellingAreas"] = getDwellingAreas(instanceId);
  result["armedPersonAreas"] = getArmedPersonAreas(instanceId);
  result["objectLeftAreas"] = getObjectLeftAreas(instanceId);
  result["objectRemovedAreas"] = getObjectRemovedAreas(instanceId);
  result["fallenPersonAreas"] = getFallenPersonAreas(instanceId);
  
  // Lines
  result["crossingLines"] = getCrossingLines(instanceId);
  result["countingLines"] = getCountingLines(instanceId);
  result["tailgatingLines"] = getTailgatingLines(instanceId);
  
  return result;
}

Json::Value AnalyticsEntitiesManager::getCrossingAreas(
    const std::string &instanceId) const {
  if (!area_manager_) {
    Json::Value areas(Json::arrayValue);
    return areas;
  }
  auto areasMap = area_manager_->getAllAreas(instanceId);
  auto it = areasMap.find("crossing");
  if (it != areasMap.end()) {
    Json::Value areasArray(Json::arrayValue);
    for (const auto &area : it->second) {
      areasArray.append(area);
    }
    return areasArray;
  }
  return Json::Value(Json::arrayValue);
}

Json::Value AnalyticsEntitiesManager::getIntrusionAreas(
    const std::string &instanceId) const {
  if (!area_manager_) {
    return Json::Value(Json::arrayValue);
  }
  auto areasMap = area_manager_->getAllAreas(instanceId);
  auto it = areasMap.find("intrusion");
  if (it != areasMap.end()) {
    Json::Value areasArray(Json::arrayValue);
    for (const auto &area : it->second) {
      areasArray.append(area);
    }
    return areasArray;
  }
  return Json::Value(Json::arrayValue);
}

Json::Value AnalyticsEntitiesManager::getLoiteringAreas(
    const std::string &instanceId) const {
  if (!area_manager_) {
    return Json::Value(Json::arrayValue);
  }
  auto areasMap = area_manager_->getAllAreas(instanceId);
  auto it = areasMap.find("loitering");
  if (it != areasMap.end()) {
    Json::Value areasArray(Json::arrayValue);
    for (const auto &area : it->second) {
      areasArray.append(area);
    }
    return areasArray;
  }
  return Json::Value(Json::arrayValue);
}

Json::Value AnalyticsEntitiesManager::getCrowdingAreas(
    const std::string &instanceId) const {
  if (!area_manager_) {
    return Json::Value(Json::arrayValue);
  }
  auto areasMap = area_manager_->getAllAreas(instanceId);
  auto it = areasMap.find("crowding");
  if (it != areasMap.end()) {
    Json::Value areasArray(Json::arrayValue);
    for (const auto &area : it->second) {
      areasArray.append(area);
    }
    return areasArray;
  }
  return Json::Value(Json::arrayValue);
}

Json::Value AnalyticsEntitiesManager::getOccupancyAreas(
    const std::string &instanceId) const {
  if (!area_manager_) {
    return Json::Value(Json::arrayValue);
  }
  auto areasMap = area_manager_->getAllAreas(instanceId);
  auto it = areasMap.find("occupancy");
  if (it != areasMap.end()) {
    Json::Value areasArray(Json::arrayValue);
    for (const auto &area : it->second) {
      areasArray.append(area);
    }
    return areasArray;
  }
  return Json::Value(Json::arrayValue);
}

Json::Value AnalyticsEntitiesManager::getCrowdEstimationAreas(
    const std::string &instanceId) const {
  if (!area_manager_) {
    return Json::Value(Json::arrayValue);
  }
  auto areasMap = area_manager_->getAllAreas(instanceId);
  auto it = areasMap.find("crowdEstimation");
  if (it != areasMap.end()) {
    Json::Value areasArray(Json::arrayValue);
    for (const auto &area : it->second) {
      areasArray.append(area);
    }
    return areasArray;
  }
  return Json::Value(Json::arrayValue);
}

Json::Value AnalyticsEntitiesManager::getDwellingAreas(
    const std::string &instanceId) const {
  if (!area_manager_) {
    return Json::Value(Json::arrayValue);
  }
  auto areasMap = area_manager_->getAllAreas(instanceId);
  auto it = areasMap.find("dwelling");
  if (it != areasMap.end()) {
    Json::Value areasArray(Json::arrayValue);
    for (const auto &area : it->second) {
      areasArray.append(area);
    }
    return areasArray;
  }
  return Json::Value(Json::arrayValue);
}

Json::Value AnalyticsEntitiesManager::getArmedPersonAreas(
    const std::string &instanceId) const {
  if (!area_manager_) {
    return Json::Value(Json::arrayValue);
  }
  auto areasMap = area_manager_->getAllAreas(instanceId);
  auto it = areasMap.find("armedPerson");
  if (it != areasMap.end()) {
    Json::Value areasArray(Json::arrayValue);
    for (const auto &area : it->second) {
      areasArray.append(area);
    }
    return areasArray;
  }
  return Json::Value(Json::arrayValue);
}

Json::Value AnalyticsEntitiesManager::getObjectLeftAreas(
    const std::string &instanceId) const {
  if (!area_manager_) {
    return Json::Value(Json::arrayValue);
  }
  auto areasMap = area_manager_->getAllAreas(instanceId);
  auto it = areasMap.find("objectLeft");
  if (it != areasMap.end()) {
    Json::Value areasArray(Json::arrayValue);
    for (const auto &area : it->second) {
      areasArray.append(area);
    }
    return areasArray;
  }
  return Json::Value(Json::arrayValue);
}

Json::Value AnalyticsEntitiesManager::getObjectRemovedAreas(
    const std::string &instanceId) const {
  if (!area_manager_) {
    return Json::Value(Json::arrayValue);
  }
  auto areasMap = area_manager_->getAllAreas(instanceId);
  auto it = areasMap.find("objectRemoved");
  if (it != areasMap.end()) {
    Json::Value areasArray(Json::arrayValue);
    for (const auto &area : it->second) {
      areasArray.append(area);
    }
    return areasArray;
  }
  return Json::Value(Json::arrayValue);
}

Json::Value AnalyticsEntitiesManager::getFallenPersonAreas(
    const std::string &instanceId) const {
  if (!area_manager_) {
    return Json::Value(Json::arrayValue);
  }
  auto areasMap = area_manager_->getAllAreas(instanceId);
  auto it = areasMap.find("fallenPerson");
  if (it != areasMap.end()) {
    Json::Value areasArray(Json::arrayValue);
    for (const auto &area : it->second) {
      areasArray.append(area);
    }
    return areasArray;
  }
  return Json::Value(Json::arrayValue);
}

void AnalyticsEntitiesManager::setLineManager(SecuRTLineManager *manager) {
  line_manager_ = manager;
}

Json::Value AnalyticsEntitiesManager::getCrossingLines(
    const std::string &instanceId) const {
  if (!line_manager_) {
    Json::Value lines(Json::arrayValue);
    return lines;
  }
  return line_manager_->getCrossingLines(instanceId);
}

Json::Value AnalyticsEntitiesManager::getCountingLines(
    const std::string &instanceId) const {
  if (!line_manager_) {
    Json::Value lines(Json::arrayValue);
    return lines;
  }
  return line_manager_->getCountingLines(instanceId);
}

Json::Value AnalyticsEntitiesManager::getTailgatingLines(
    const std::string &instanceId) const {
  if (!line_manager_) {
    Json::Value lines(Json::arrayValue);
    return lines;
  }
  return line_manager_->getTailgatingLines(instanceId);
}

