#include "core/area_storage.h"
#include "core/uuid_generator.h"
#include <json/json.h>
#include <iostream>

std::string AreaStorage::createCrossingArea(const std::string &instanceId,
                                             const std::string &areaId,
                                             const CrossingAreaWrite &write) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string finalAreaId =
      areaId.empty() ? UUIDGenerator::generateUUID() : areaId;

  // Check if area ID already exists
  // Use findAreaIndex directly since we already hold the lock
  auto [type, index] = findAreaIndex(instanceId, finalAreaId);
  if (index != SIZE_MAX) {
    return ""; // Area already exists
  }

  // Create area
  CrossingArea area;
  area.id = finalAreaId;
  area.name = write.name;
  area.coordinates = write.coordinates;
  area.classes = write.classes;
  area.color = write.color;
  area.ignoreStationaryObjects = write.ignoreStationaryObjects;
  area.areaEvent = write.areaEvent;

  // Store area
  storage_[instanceId][AreaType::Crossing].push_back(
      std::make_shared<CrossingArea>(area));

  std::cerr << "[AreaStorage::createCrossingArea] DEBUG: Stored area ID: " << finalAreaId 
            << ", Name: " << area.name 
            << ", Type: Crossing (enum: " << static_cast<int>(AreaType::Crossing) << ")" << std::endl;

  return finalAreaId;
}

std::string AreaStorage::createIntrusionArea(const std::string &instanceId,
                                               const std::string &areaId,
                                               const IntrusionAreaWrite &write) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string finalAreaId =
      areaId.empty() ? UUIDGenerator::generateUUID() : areaId;

  auto [type, index] = findAreaIndex(instanceId, finalAreaId);
  if (index != SIZE_MAX) {
    return "";
  }

  IntrusionArea area;
  area.id = finalAreaId;
  area.name = write.name;
  area.coordinates = write.coordinates;
  area.classes = write.classes;
  area.color = write.color;

  storage_[instanceId][AreaType::Intrusion].push_back(
      std::make_shared<IntrusionArea>(area));

  std::cerr << "[AreaStorage::createIntrusionArea] DEBUG: Stored area ID: " << finalAreaId 
            << ", Name: " << area.name 
            << ", Type: Intrusion (enum: " << static_cast<int>(AreaType::Intrusion) << ")" << std::endl;

  return finalAreaId;
}

std::string AreaStorage::createLoiteringArea(const std::string &instanceId,
                                              const std::string &areaId,
                                              const LoiteringAreaWrite &write) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string finalAreaId =
      areaId.empty() ? UUIDGenerator::generateUUID() : areaId;

  auto [type, index] = findAreaIndex(instanceId, finalAreaId);
  if (index != SIZE_MAX) {
    return "";
  }

  LoiteringArea area;
  area.id = finalAreaId;
  area.name = write.name;
  area.coordinates = write.coordinates;
  area.classes = write.classes;
  area.color = write.color;
  area.seconds = write.seconds;

  storage_[instanceId][AreaType::Loitering].push_back(
      std::make_shared<LoiteringArea>(area));

  std::cerr << "[AreaStorage::createLoiteringArea] DEBUG: Stored area ID: " << finalAreaId 
            << ", Name: " << area.name 
            << ", Type: Loitering (enum: " << static_cast<int>(AreaType::Loitering) << ")" << std::endl;

  return finalAreaId;
}

std::string AreaStorage::createCrowdingArea(const std::string &instanceId,
                                             const std::string &areaId,
                                             const CrowdingAreaWrite &write) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string finalAreaId =
      areaId.empty() ? UUIDGenerator::generateUUID() : areaId;

  auto [type, index] = findAreaIndex(instanceId, finalAreaId);
  if (index != SIZE_MAX) {
    return "";
  }

  CrowdingArea area;
  area.id = finalAreaId;
  area.name = write.name;
  area.coordinates = write.coordinates;
  area.classes = write.classes;
  area.color = write.color;
  area.objectCount = write.objectCount;
  area.seconds = write.seconds;

  storage_[instanceId][AreaType::Crowding].push_back(
      std::make_shared<CrowdingArea>(area));

  std::cerr << "[AreaStorage::createCrowdingArea] DEBUG: Stored area ID: " << finalAreaId 
            << ", Name: " << area.name 
            << ", Type: Crowding (enum: " << static_cast<int>(AreaType::Crowding) << ")" << std::endl;

  return finalAreaId;
}

std::string AreaStorage::createOccupancyArea(const std::string &instanceId,
                                              const std::string &areaId,
                                              const OccupancyAreaWrite &write) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string finalAreaId =
      areaId.empty() ? UUIDGenerator::generateUUID() : areaId;

  auto [type, index] = findAreaIndex(instanceId, finalAreaId);
  if (index != SIZE_MAX) {
    return "";
  }

  OccupancyArea area;
  area.id = finalAreaId;
  area.name = write.name;
  area.coordinates = write.coordinates;
  area.classes = write.classes;
  area.color = write.color;

  storage_[instanceId][AreaType::Occupancy].push_back(
      std::make_shared<OccupancyArea>(area));

  std::cerr << "[AreaStorage::createOccupancyArea] DEBUG: Stored area ID: " << finalAreaId 
            << ", Name: " << area.name 
            << ", Type: Occupancy (enum: " << static_cast<int>(AreaType::Occupancy) << ")" << std::endl;

  return finalAreaId;
}

std::string AreaStorage::createCrowdEstimationArea(
    const std::string &instanceId, const std::string &areaId,
    const CrowdEstimationAreaWrite &write) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string finalAreaId =
      areaId.empty() ? UUIDGenerator::generateUUID() : areaId;

  auto [type, index] = findAreaIndex(instanceId, finalAreaId);
  if (index != SIZE_MAX) {
    return "";
  }

  CrowdEstimationArea area;
  area.id = finalAreaId;
  area.name = write.name;
  area.coordinates = write.coordinates;
  area.classes = write.classes;
  area.color = write.color;

  storage_[instanceId][AreaType::CrowdEstimation].push_back(
      std::make_shared<CrowdEstimationArea>(area));

  std::cerr << "[AreaStorage::createCrowdEstimationArea] DEBUG: Stored area ID: " << finalAreaId 
            << ", Name: " << area.name 
            << ", Type: CrowdEstimation (enum: " << static_cast<int>(AreaType::CrowdEstimation) << ")" << std::endl;

  return finalAreaId;
}

std::string AreaStorage::createDwellingArea(const std::string &instanceId,
                                              const std::string &areaId,
                                              const DwellingAreaWrite &write) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string finalAreaId =
      areaId.empty() ? UUIDGenerator::generateUUID() : areaId;

  auto [type, index] = findAreaIndex(instanceId, finalAreaId);
  if (index != SIZE_MAX) {
    return "";
  }

  DwellingArea area;
  area.id = finalAreaId;
  area.name = write.name;
  area.coordinates = write.coordinates;
  area.classes = write.classes;
  area.color = write.color;
  area.seconds = write.seconds;

  storage_[instanceId][AreaType::Dwelling].push_back(
      std::make_shared<DwellingArea>(area));

  std::cerr << "[AreaStorage::createDwellingArea] DEBUG: Stored area ID: " << finalAreaId 
            << ", Name: " << area.name 
            << ", Type: Dwelling (enum: " << static_cast<int>(AreaType::Dwelling) << ")" << std::endl;

  return finalAreaId;
}

std::string AreaStorage::createArmedPersonArea(
    const std::string &instanceId, const std::string &areaId,
    const ArmedPersonAreaWrite &write) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string finalAreaId =
      areaId.empty() ? UUIDGenerator::generateUUID() : areaId;

  auto [type, index] = findAreaIndex(instanceId, finalAreaId);
  if (index != SIZE_MAX) {
    return "";
  }

  ArmedPersonArea area;
  area.id = finalAreaId;
  area.name = write.name;
  area.coordinates = write.coordinates;
  area.classes = write.classes;
  area.color = write.color;

  storage_[instanceId][AreaType::ArmedPerson].push_back(
      std::make_shared<ArmedPersonArea>(area));

  std::cerr << "[AreaStorage::createArmedPersonArea] DEBUG: Stored area ID: " << finalAreaId 
            << ", Name: " << area.name 
            << ", Type: ArmedPerson (enum: " << static_cast<int>(AreaType::ArmedPerson) << ")" << std::endl;

  return finalAreaId;
}

std::string AreaStorage::createObjectLeftArea(const std::string &instanceId,
                                               const std::string &areaId,
                                               const ObjectLeftAreaWrite &write) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string finalAreaId =
      areaId.empty() ? UUIDGenerator::generateUUID() : areaId;

  auto [type, index] = findAreaIndex(instanceId, finalAreaId);
  if (index != SIZE_MAX) {
    return "";
  }

  ObjectLeftArea area;
  area.id = finalAreaId;
  area.name = write.name;
  area.coordinates = write.coordinates;
  area.classes = write.classes;
  area.color = write.color;
  area.seconds = write.seconds;

  storage_[instanceId][AreaType::ObjectLeft].push_back(
      std::make_shared<ObjectLeftArea>(area));

  std::cerr << "[AreaStorage::createObjectLeftArea] DEBUG: Stored area ID: " << finalAreaId 
            << ", Name: " << area.name 
            << ", Type: ObjectLeft (enum: " << static_cast<int>(AreaType::ObjectLeft) << ")" << std::endl;

  return finalAreaId;
}

std::string AreaStorage::createObjectRemovedArea(
    const std::string &instanceId, const std::string &areaId,
    const ObjectRemovedAreaWrite &write) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string finalAreaId =
      areaId.empty() ? UUIDGenerator::generateUUID() : areaId;

  auto [type, index] = findAreaIndex(instanceId, finalAreaId);
  if (index != SIZE_MAX) {
    return "";
  }

  ObjectRemovedArea area;
  area.id = finalAreaId;
  area.name = write.name;
  area.coordinates = write.coordinates;
  area.classes = write.classes;
  area.color = write.color;
  area.seconds = write.seconds;

  storage_[instanceId][AreaType::ObjectRemoved].push_back(
      std::make_shared<ObjectRemovedArea>(area));

  std::cerr << "[AreaStorage::createObjectRemovedArea] DEBUG: Stored area ID: " << finalAreaId 
            << ", Name: " << area.name 
            << ", Type: ObjectRemoved (enum: " << static_cast<int>(AreaType::ObjectRemoved) << ")" << std::endl;

  return finalAreaId;
}

std::string AreaStorage::createFallenPersonArea(
    const std::string &instanceId, const std::string &areaId,
    const FallenPersonAreaWrite &write) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string finalAreaId =
      areaId.empty() ? UUIDGenerator::generateUUID() : areaId;

  auto [type, index] = findAreaIndex(instanceId, finalAreaId);
  if (index != SIZE_MAX) {
    return "";
  }

  FallenPersonArea area;
  area.id = finalAreaId;
  area.name = write.name;
  area.coordinates = write.coordinates;
  area.classes = write.classes;
  area.color = write.color;

  storage_[instanceId][AreaType::FallenPerson].push_back(
      std::make_shared<FallenPersonArea>(area));

  std::cerr << "[AreaStorage::createFallenPersonArea] DEBUG: Stored area ID: " << finalAreaId 
            << ", Name: " << area.name 
            << ", Type: FallenPerson (enum: " << static_cast<int>(AreaType::FallenPerson) << ")" << std::endl;

  return finalAreaId;
}

std::string AreaStorage::createVehicleGuardArea(
    const std::string &instanceId, const std::string &areaId,
    const VehicleGuardAreaWrite &write) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string finalAreaId =
      areaId.empty() ? UUIDGenerator::generateUUID() : areaId;

  auto [type, index] = findAreaIndex(instanceId, finalAreaId);
  if (index != SIZE_MAX) {
    return "";
  }

  VehicleGuardArea area;
  area.id = finalAreaId;
  area.name = write.name;
  area.coordinates = write.coordinates;
  area.classes = write.classes;
  area.color = write.color;

  storage_[instanceId][AreaType::VehicleGuard].push_back(
      std::make_shared<VehicleGuardArea>(area));

  std::cerr << "[AreaStorage::createVehicleGuardArea] DEBUG: Stored area ID: " << finalAreaId 
            << ", Name: " << area.name 
            << ", Type: VehicleGuard (enum: " << static_cast<int>(AreaType::VehicleGuard) << ")" << std::endl;

  return finalAreaId;
}

std::string AreaStorage::createFaceCoveredArea(
    const std::string &instanceId, const std::string &areaId,
    const FaceCoveredAreaWrite &write) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string finalAreaId =
      areaId.empty() ? UUIDGenerator::generateUUID() : areaId;

  auto [type, index] = findAreaIndex(instanceId, finalAreaId);
  if (index != SIZE_MAX) {
    return "";
  }

  FaceCoveredArea area;
  area.id = finalAreaId;
  area.name = write.name;
  area.coordinates = write.coordinates;
  area.classes = write.classes;
  area.color = write.color;

  storage_[instanceId][AreaType::FaceCovered].push_back(
      std::make_shared<FaceCoveredArea>(area));

  std::cerr << "[AreaStorage::createFaceCoveredArea] DEBUG: Stored area ID: " << finalAreaId 
            << ", Name: " << area.name 
            << ", Type: FaceCovered (enum: " << static_cast<int>(AreaType::FaceCovered) << ")" << std::endl;

  return finalAreaId;
}

std::string AreaStorage::createObjectEnterExitArea(
    const std::string &instanceId, const std::string &areaId,
    const ObjectEnterExitAreaWrite &write) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string finalAreaId =
      areaId.empty() ? UUIDGenerator::generateUUID() : areaId;

  auto [type, index] = findAreaIndex(instanceId, finalAreaId);
  if (index != SIZE_MAX) {
    return "";
  }

  ObjectEnterExitArea area;
  area.id = finalAreaId;
  area.name = write.name;
  area.coordinates = write.coordinates;
  area.classes = write.classes;
  area.color = write.color;
  area.alertOnEnter = write.alertOnEnter;
  area.alertOnExit = write.alertOnExit;

  storage_[instanceId][AreaType::ObjectEnterExit].push_back(
      std::make_shared<ObjectEnterExitArea>(area));

  std::cerr << "[AreaStorage::createObjectEnterExitArea] DEBUG: Stored area ID: " << finalAreaId 
            << ", Name: " << area.name 
            << ", Type: ObjectEnterExit (enum: " << static_cast<int>(AreaType::ObjectEnterExit) << ")" << std::endl;

  return finalAreaId;
}

std::unordered_map<std::string, std::vector<Json::Value>>
AreaStorage::getAllAreas(const std::string &instanceId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  std::unordered_map<std::string, std::vector<Json::Value>> result;

  auto instanceIt = storage_.find(instanceId);
  if (instanceIt == storage_.end()) {
    std::cerr << "[AreaStorage::getAllAreas] DEBUG: Instance " << instanceId << " not found in storage" << std::endl;
    return result; // Empty result
  }

  std::cerr << "[AreaStorage::getAllAreas] DEBUG: Instance " << instanceId << " found. Total area types in storage: " << instanceIt->second.size() << std::endl;

  // Iterate through all area types
  for (const auto &[type, areas] : instanceIt->second) {
    std::cerr << "[AreaStorage::getAllAreas] DEBUG: Found area type in storage - enum: " << static_cast<int>(type) << ", areas count: " << areas.size() << std::endl;
    std::string typeStr = areaTypeToString(type);
    std::cerr << "[AreaStorage::getAllAreas] DEBUG: Converted enum " << static_cast<int>(type) << " to string: \"" << typeStr << "\"" << std::endl;
    
    std::vector<Json::Value> areasJson;

    for (const auto &areaPtr : areas) {
      Json::Value areaJson = areaToJson(type, areaPtr);
      if (areaJson.isMember("id") && areaJson.isMember("name")) {
        std::cerr << "[AreaStorage::getAllAreas] DEBUG:   - Area ID: " << areaJson["id"].asString() 
                  << ", Name: " << areaJson["name"].asString() << std::endl;
      }
      areasJson.push_back(areaJson);
    }

    result[typeStr] = areasJson;
    std::cerr << "[AreaStorage::getAllAreas] DEBUG: Added " << areasJson.size() << " areas to result with key: " << typeStr << std::endl;
  }

  std::cerr << "[AreaStorage::getAllAreas] DEBUG: Final result map has " << result.size() << " keys: ";
  for (const auto &[key, value] : result) {
    std::cerr << key << "(" << value.size() << " areas) ";
  }
  std::cerr << std::endl;

  return result;
}

std::vector<Json::Value> AreaStorage::getAreasByType(const std::string &instanceId,
                                                       AreaType type) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  std::vector<Json::Value> result;

  auto instanceIt = storage_.find(instanceId);
  if (instanceIt == storage_.end()) {
    return result;
  }

  auto typeIt = instanceIt->second.find(type);
  if (typeIt == instanceIt->second.end()) {
    return result;
  }

  for (const auto &areaPtr : typeIt->second) {
    result.push_back(areaToJson(type, areaPtr));
  }

  return result;
}

Json::Value AreaStorage::getArea(const std::string &instanceId,
                                 const std::string &areaId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  auto [type, index] = findAreaIndex(instanceId, areaId);
  if (index == SIZE_MAX) {
    return Json::Value(); // Not found
  }

  auto instanceIt = storage_.find(instanceId);
  if (instanceIt == storage_.end()) {
    return Json::Value();
  }

  auto typeIt = instanceIt->second.find(type);
  if (typeIt == instanceIt->second.end() || index >= typeIt->second.size()) {
    return Json::Value();
  }

  return areaToJson(type, typeIt->second[index]);
}

bool AreaStorage::deleteArea(const std::string &instanceId,
                              const std::string &areaId) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  auto [type, index] = findAreaIndex(instanceId, areaId);
  if (index == SIZE_MAX) {
    return false; // Not found
  }

  auto instanceIt = storage_.find(instanceId);
  if (instanceIt == storage_.end()) {
    return false;
  }

  auto typeIt = instanceIt->second.find(type);
  if (typeIt == instanceIt->second.end() || index >= typeIt->second.size()) {
    return false;
  }

  typeIt->second.erase(typeIt->second.begin() + index);
  return true;
}

bool AreaStorage::deleteAllAreas(const std::string &instanceId) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  auto instanceIt = storage_.find(instanceId);
  if (instanceIt == storage_.end()) {
    return false;
  }

  instanceIt->second.clear();
  return true;
}

bool AreaStorage::hasArea(const std::string &instanceId,
                           const std::string &areaId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto [type, index] = findAreaIndex(instanceId, areaId);
  return index != SIZE_MAX;
}

size_t AreaStorage::getAreaCount(const std::string &instanceId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  auto instanceIt = storage_.find(instanceId);
  if (instanceIt == storage_.end()) {
    return 0;
  }

  size_t count = 0;
  for (const auto &[type, areas] : instanceIt->second) {
    count += areas.size();
  }

  return count;
}

std::pair<AreaType, size_t>
AreaStorage::findAreaIndex(const std::string &instanceId,
                            const std::string &areaId) const {
  auto instanceIt = storage_.find(instanceId);
  if (instanceIt == storage_.end()) {
    return {AreaType::Crossing, SIZE_MAX};
  }

  // Search through all area types
  for (const auto &[type, areas] : instanceIt->second) {
    for (size_t i = 0; i < areas.size(); ++i) {
      Json::Value areaJson = areaToJson(type, areas[i]);
      if (areaJson.isMember("id") && areaJson["id"].asString() == areaId) {
        return {type, i};
      }
    }
  }

  return {AreaType::Crossing, SIZE_MAX};
}

Json::Value AreaStorage::areaToJson(AreaType type,
                                     const AreaPtr &area) const {
  switch (type) {
  case AreaType::Crossing: {
    auto *crossingArea = static_cast<CrossingArea *>(area.get());
    return crossingArea->toJson();
  }
  case AreaType::Intrusion: {
    auto *intrusionArea = static_cast<IntrusionArea *>(area.get());
    return intrusionArea->toJson();
  }
  case AreaType::Loitering: {
    auto *loiteringArea = static_cast<LoiteringArea *>(area.get());
    return loiteringArea->toJson();
  }
  case AreaType::Crowding: {
    auto *crowdingArea = static_cast<CrowdingArea *>(area.get());
    return crowdingArea->toJson();
  }
  case AreaType::Occupancy: {
    auto *occupancyArea = static_cast<OccupancyArea *>(area.get());
    return occupancyArea->toJson();
  }
  case AreaType::CrowdEstimation: {
    auto *crowdEstArea = static_cast<CrowdEstimationArea *>(area.get());
    return crowdEstArea->toJson();
  }
  case AreaType::Dwelling: {
    auto *dwellingArea = static_cast<DwellingArea *>(area.get());
    return dwellingArea->toJson();
  }
  case AreaType::ArmedPerson: {
    auto *armedArea = static_cast<ArmedPersonArea *>(area.get());
    return armedArea->toJson();
  }
  case AreaType::ObjectLeft: {
    auto *objectLeftArea = static_cast<ObjectLeftArea *>(area.get());
    return objectLeftArea->toJson();
  }
  case AreaType::ObjectRemoved: {
    auto *objectRemovedArea = static_cast<ObjectRemovedArea *>(area.get());
    return objectRemovedArea->toJson();
  }
  case AreaType::FallenPerson: {
    auto *fallenArea = static_cast<FallenPersonArea *>(area.get());
    return fallenArea->toJson();
  }
  case AreaType::VehicleGuard: {
    auto *vehicleGuardArea = static_cast<VehicleGuardArea *>(area.get());
    return vehicleGuardArea->toJson();
  }
  case AreaType::FaceCovered: {
    auto *faceCoveredArea = static_cast<FaceCoveredArea *>(area.get());
    return faceCoveredArea->toJson();
  }
  case AreaType::ObjectEnterExit: {
    auto *objectEnterExitArea = static_cast<ObjectEnterExitArea *>(area.get());
    return objectEnterExitArea->toJson();
  }
  default:
    return Json::Value();
  }
}

