#include "core/exclusion_area_manager.h"
#include <algorithm>
#include <set>

bool ExclusionAreaManager::addExclusionArea(const std::string &instanceId,
                                             const ExclusionArea &area) {
  if (!validateExclusionArea(area)) {
    return false;
  }

  std::unique_lock<std::shared_mutex> lock(mutex_);
  exclusion_areas_[instanceId].push_back(area);
  return true;
}

std::vector<ExclusionArea>
ExclusionAreaManager::getExclusionAreas(const std::string &instanceId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = exclusion_areas_.find(instanceId);
  if (it != exclusion_areas_.end()) {
    return it->second;
  }
  return std::vector<ExclusionArea>();
}

bool ExclusionAreaManager::deleteExclusionAreas(
    const std::string &instanceId) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  return exclusion_areas_.erase(instanceId) > 0;
}

bool ExclusionAreaManager::validateExclusionArea(const ExclusionArea &area) {
  // Validate coordinates (need at least 3 points for polygon)
  if (area.coordinates.size() < 3) {
    return false;
  }

  // Validate coordinate values (normalized 0.0-1.0)
  for (const auto &coord : area.coordinates) {
    if (coord.x < 0.0 || coord.x > 1.0 || coord.y < 0.0 || coord.y > 1.0) {
      return false;
    }
  }

  // Validate classes (at least one class required)
  if (area.classes.empty()) {
    return false;
  }

  // Validate class values
  std::set<std::string> validClasses = {"Person", "Vehicle"};
  for (const auto &cls : area.classes) {
    if (validClasses.find(cls) == validClasses.end()) {
      return false;
    }
  }

  return true;
}

