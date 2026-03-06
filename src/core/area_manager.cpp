#include "core/area_manager.h"
#include "core/logger.h"
#include <algorithm>
#include <cmath>

AreaManager::AreaManager(AreaStorage *storage,
                         SecuRTInstanceManager *instanceManager)
    : storage_(storage), instance_manager_(instanceManager) {}

// ========================================================================
// Create Area Methods (POST)
// ========================================================================

std::string AreaManager::createCrossingArea(const std::string &instanceId,
                                             const CrossingAreaWrite &write) {
  // Validate instance
  if (!validateInstance(instanceId)) {
    return "";
  }

  // Validate area data
  std::string error = validateCrossingArea(write);
  if (!error.empty()) {
    return "";
  }

  return storage_->createCrossingArea(instanceId, "", write);
}

std::string AreaManager::createIntrusionArea(const std::string &instanceId,
                                              const IntrusionAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createIntrusionArea(instanceId, "", write);
}

std::string AreaManager::createLoiteringArea(const std::string &instanceId,
                                               const LoiteringAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateLoiteringArea(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createLoiteringArea(instanceId, "", write);
}

std::string AreaManager::createCrowdingArea(const std::string &instanceId,
                                             const CrowdingAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateCrowdingArea(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createCrowdingArea(instanceId, "", write);
}

std::string AreaManager::createOccupancyArea(const std::string &instanceId,
                                              const OccupancyAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createOccupancyArea(instanceId, "", write);
}

std::string AreaManager::createCrowdEstimationArea(
    const std::string &instanceId, const CrowdEstimationAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createCrowdEstimationArea(instanceId, "", write);
}

std::string AreaManager::createDwellingArea(const std::string &instanceId,
                                             const DwellingAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateDwellingArea(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createDwellingArea(instanceId, "", write);
}

std::string AreaManager::createArmedPersonArea(
    const std::string &instanceId, const ArmedPersonAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createArmedPersonArea(instanceId, "", write);
}

std::string AreaManager::createObjectLeftArea(const std::string &instanceId,
                                               const ObjectLeftAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateObjectLeftArea(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createObjectLeftArea(instanceId, "", write);
}

std::string AreaManager::createObjectRemovedArea(
    const std::string &instanceId, const ObjectRemovedAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateObjectRemovedArea(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createObjectRemovedArea(instanceId, "", write);
}

std::string AreaManager::createFallenPersonArea(
    const std::string &instanceId, const FallenPersonAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createFallenPersonArea(instanceId, "", write);
}

std::string AreaManager::createVehicleGuardArea(
    const std::string &instanceId, const VehicleGuardAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createVehicleGuardArea(instanceId, "", write);
}

std::string AreaManager::createFaceCoveredArea(
    const std::string &instanceId, const FaceCoveredAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createFaceCoveredArea(instanceId, "", write);
}

std::string AreaManager::createObjectEnterExitArea(
    const std::string &instanceId, const ObjectEnterExitAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    PLOG_WARNING << "[AreaManager] createObjectEnterExitArea - Instance not found: " << instanceId;
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    PLOG_WARNING << "[AreaManager] createObjectEnterExitArea - Validation error: " << error;
    return "";
  }
  return storage_->createObjectEnterExitArea(instanceId, "", write);
}

// ========================================================================
// Create Area with ID Methods (PUT)
// ========================================================================

std::string AreaManager::createCrossingAreaWithId(
    const std::string &instanceId, const std::string &areaId,
    const CrossingAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateCrossingArea(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createCrossingArea(instanceId, areaId, write);
}

std::string AreaManager::createIntrusionAreaWithId(
    const std::string &instanceId, const std::string &areaId,
    const IntrusionAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createIntrusionArea(instanceId, areaId, write);
}

std::string AreaManager::createLoiteringAreaWithId(
    const std::string &instanceId, const std::string &areaId,
    const LoiteringAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateLoiteringArea(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createLoiteringArea(instanceId, areaId, write);
}

std::string AreaManager::createCrowdingAreaWithId(
    const std::string &instanceId, const std::string &areaId,
    const CrowdingAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateCrowdingArea(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createCrowdingArea(instanceId, areaId, write);
}

std::string AreaManager::createOccupancyAreaWithId(
    const std::string &instanceId, const std::string &areaId,
    const OccupancyAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createOccupancyArea(instanceId, areaId, write);
}

std::string AreaManager::createCrowdEstimationAreaWithId(
    const std::string &instanceId, const std::string &areaId,
    const CrowdEstimationAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createCrowdEstimationArea(instanceId, areaId, write);
}

std::string AreaManager::createDwellingAreaWithId(
    const std::string &instanceId, const std::string &areaId,
    const DwellingAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateDwellingArea(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createDwellingArea(instanceId, areaId, write);
}

std::string AreaManager::createArmedPersonAreaWithId(
    const std::string &instanceId, const std::string &areaId,
    const ArmedPersonAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createArmedPersonArea(instanceId, areaId, write);
}

std::string AreaManager::createObjectLeftAreaWithId(
    const std::string &instanceId, const std::string &areaId,
    const ObjectLeftAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateObjectLeftArea(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createObjectLeftArea(instanceId, areaId, write);
}

std::string AreaManager::createObjectRemovedAreaWithId(
    const std::string &instanceId, const std::string &areaId,
    const ObjectRemovedAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateObjectRemovedArea(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createObjectRemovedArea(instanceId, areaId, write);
}

std::string AreaManager::createFallenPersonAreaWithId(
    const std::string &instanceId, const std::string &areaId,
    const FallenPersonAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createFallenPersonArea(instanceId, areaId, write);
}

std::string AreaManager::createVehicleGuardAreaWithId(
    const std::string &instanceId, const std::string &areaId,
    const VehicleGuardAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createVehicleGuardArea(instanceId, areaId, write);
}

std::string AreaManager::createFaceCoveredAreaWithId(
    const std::string &instanceId, const std::string &areaId,
    const FaceCoveredAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createFaceCoveredArea(instanceId, areaId, write);
}

std::string AreaManager::createObjectEnterExitAreaWithId(
    const std::string &instanceId, const std::string &areaId,
    const ObjectEnterExitAreaWrite &write) {
  if (!validateInstance(instanceId)) {
    return "";
  }
  std::string error = validateAreaBase(write);
  if (!error.empty()) {
    return "";
  }
  return storage_->createObjectEnterExitArea(instanceId, areaId, write);
}

// ========================================================================
// Get Methods
// ========================================================================

std::unordered_map<std::string, std::vector<Json::Value>>
AreaManager::getAllAreas(const std::string &instanceId) const {
  if (!validateInstance(instanceId)) {
    return {};
  }
  return storage_->getAllAreas(instanceId);
}

Json::Value AreaManager::getArea(const std::string &instanceId,
                                  const std::string &areaId) const {
  if (!validateInstance(instanceId)) {
    return Json::Value();
  }
  return storage_->getArea(instanceId, areaId);
}

// ========================================================================
// Delete Methods
// ========================================================================

bool AreaManager::deleteArea(const std::string &instanceId,
                              const std::string &areaId) {
  if (!validateInstance(instanceId)) {
    return false;
  }
  return storage_->deleteArea(instanceId, areaId);
}

bool AreaManager::deleteAllAreas(const std::string &instanceId) {
  if (!validateInstance(instanceId)) {
    return false;
  }
  return storage_->deleteAllAreas(instanceId);
}

// ========================================================================
// Validation
// ========================================================================

std::string AreaManager::validateAreaBase(const AreaBaseWrite &write) const {
  // Validate name
  if (write.name.empty()) {
    PLOG_DEBUG << "[AreaManager::validateAreaBase] Name is empty";
    return "Field 'name' is required";
  }

  // Validate coordinates
  std::string coordError = validateCoordinates(write.coordinates);
  if (!coordError.empty()) {
    PLOG_DEBUG << "[AreaManager::validateAreaBase] Coordinate validation failed: " << coordError;
    return coordError;
  }

  // Validate classes
  std::string classError = validateClasses(write.classes);
  if (!classError.empty()) {
    PLOG_DEBUG << "[AreaManager::validateAreaBase] Class validation failed: " << classError;
    return classError;
  }

  // Validate color
  std::string colorError = validateColor(write.color);
  if (!colorError.empty()) {
    PLOG_DEBUG << "[AreaManager::validateAreaBase] Color validation failed: " << colorError
               << " (r=" << write.color.r << ", g=" << write.color.g 
               << ", b=" << write.color.b << ", a=" << write.color.a << ")";
    return colorError;
  }

  return "";
}

std::string AreaManager::validateCrossingArea(const CrossingAreaWrite &write) const {
  std::string baseError = validateAreaBase(write);
  if (!baseError.empty()) {
    return baseError;
  }
  // No additional validation needed for crossing area
  return "";
}

std::string AreaManager::validateLoiteringArea(const LoiteringAreaWrite &write) const {
  std::string baseError = validateAreaBase(write);
  if (!baseError.empty()) {
    return baseError;
  }
  if (write.seconds <= 0) {
    return "Field 'seconds' must be greater than 0";
  }
  return "";
}

std::string AreaManager::validateCrowdingArea(const CrowdingAreaWrite &write) const {
  std::string baseError = validateAreaBase(write);
  if (!baseError.empty()) {
    return baseError;
  }
  if (write.objectCount <= 0) {
    return "Field 'objectCount' must be greater than 0";
  }
  if (write.seconds <= 0) {
    return "Field 'seconds' must be greater than 0";
  }
  return "";
}

std::string AreaManager::validateDwellingArea(const DwellingAreaWrite &write) const {
  std::string baseError = validateAreaBase(write);
  if (!baseError.empty()) {
    return baseError;
  }
  if (write.seconds <= 0) {
    return "Field 'seconds' must be greater than 0";
  }
  return "";
}

std::string AreaManager::validateObjectLeftArea(const ObjectLeftAreaWrite &write) const {
  std::string baseError = validateAreaBase(write);
  if (!baseError.empty()) {
    return baseError;
  }
  if (write.seconds <= 0) {
    return "Field 'seconds' must be greater than 0";
  }
  return "";
}

std::string AreaManager::validateObjectRemovedArea(const ObjectRemovedAreaWrite &write) const {
  std::string baseError = validateAreaBase(write);
  if (!baseError.empty()) {
    return baseError;
  }
  if (write.seconds <= 0) {
    return "Field 'seconds' must be greater than 0";
  }
  return "";
}

std::string AreaManager::validateCoordinates(const std::vector<Coordinate> &coordinates) const {
  if (coordinates.size() < 3) {
    return "Coordinates must have at least 3 points";
  }

  // Basic validation: check for valid numeric values
  for (const auto &coord : coordinates) {
    if (std::isnan(coord.x) || std::isnan(coord.y) ||
        std::isinf(coord.x) || std::isinf(coord.y)) {
      return "Coordinates contain invalid values";
    }
  }

  // TODO: Add polygon validation (check if points form valid polygon)
  // For now, just check minimum points

  return "";
}

std::string AreaManager::validateClasses(const std::vector<ObjectClass> &classes) const {
  if (classes.empty()) {
    return "At least one class must be specified";
  }
  return "";
}

std::string AreaManager::validateColor(const ColorRGBA &color) const {
  if (color.r < 0.0 || color.r > 1.0 || color.g < 0.0 || color.g > 1.0 ||
      color.b < 0.0 || color.b > 1.0 || color.a < 0.0 || color.a > 1.0) {
    return "Color values must be in range 0.0-1.0";
  }
  return "";
}

bool AreaManager::validateInstance(const std::string &instanceId) const {
  if (!instance_manager_) {
    return false;
  }
  return instance_manager_->hasInstance(instanceId);
}

