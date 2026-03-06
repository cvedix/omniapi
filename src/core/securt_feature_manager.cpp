#include "core/securt_feature_manager.h"
#include <algorithm>
#include <set>

bool SecuRTFeatureManager::setMotionArea(
    const std::string &instanceId,
    const std::vector<Coordinate> &coordinates) {
  if (!validateCoordinates(coordinates)) {
    return false;
  }

  std::unique_lock<std::shared_mutex> lock(mutex_);
  SecuRTFeatureConfig &config = getOrCreateConfig(instanceId);
  config.motionArea = coordinates;
  return true;
}

std::optional<std::vector<Coordinate>>
SecuRTFeatureManager::getMotionArea(const std::string &instanceId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = configs_.find(instanceId);
  if (it != configs_.end() && !it->second.motionArea.empty()) {
    return it->second.motionArea;
  }
  return std::nullopt;
}

bool SecuRTFeatureManager::setFeatureExtraction(
    const std::string &instanceId,
    const std::vector<std::string> &types) {
  if (!validateFeatureExtractionTypes(types)) {
    return false;
  }

  std::unique_lock<std::shared_mutex> lock(mutex_);
  SecuRTFeatureConfig &config = getOrCreateConfig(instanceId);
  config.featureExtractionTypes = types;
  return true;
}

std::optional<std::vector<std::string>>
SecuRTFeatureManager::getFeatureExtraction(
    const std::string &instanceId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = configs_.find(instanceId);
  if (it != configs_.end() && !it->second.featureExtractionTypes.empty()) {
    return it->second.featureExtractionTypes;
  }
  return std::nullopt;
}

bool SecuRTFeatureManager::setAttributesExtraction(
    const std::string &instanceId, const std::string &mode) {
  if (!validateAttributesExtractionMode(mode)) {
    return false;
  }

  std::unique_lock<std::shared_mutex> lock(mutex_);
  SecuRTFeatureConfig &config = getOrCreateConfig(instanceId);
  config.attributesExtractionMode = mode;
  return true;
}

std::optional<std::string>
SecuRTFeatureManager::getAttributesExtraction(
    const std::string &instanceId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = configs_.find(instanceId);
  if (it != configs_.end() && !it->second.attributesExtractionMode.empty()) {
    return it->second.attributesExtractionMode;
  }
  return std::nullopt;
}

bool SecuRTFeatureManager::setPerformanceProfile(
    const std::string &instanceId, const std::string &profile) {
  if (!validatePerformanceProfile(profile)) {
    return false;
  }

  std::unique_lock<std::shared_mutex> lock(mutex_);
  SecuRTFeatureConfig &config = getOrCreateConfig(instanceId);
  config.performanceProfile = profile;
  return true;
}

std::optional<std::string>
SecuRTFeatureManager::getPerformanceProfile(
    const std::string &instanceId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = configs_.find(instanceId);
  if (it != configs_.end() && !it->second.performanceProfile.empty()) {
    return it->second.performanceProfile;
  }
  return std::nullopt;
}

bool SecuRTFeatureManager::setFaceDetection(const std::string &instanceId,
                                             bool enable) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  SecuRTFeatureConfig &config = getOrCreateConfig(instanceId);
  config.faceDetectionEnabled = enable;
  return true;
}

std::optional<bool>
SecuRTFeatureManager::getFaceDetection(const std::string &instanceId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = configs_.find(instanceId);
  if (it != configs_.end()) {
    return it->second.faceDetectionEnabled;
  }
  return std::nullopt;
}

bool SecuRTFeatureManager::setLPR(const std::string &instanceId, bool enable) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  SecuRTFeatureConfig &config = getOrCreateConfig(instanceId);
  config.lprEnabled = enable;
  return true;
}

std::optional<bool> SecuRTFeatureManager::getLPR(
    const std::string &instanceId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = configs_.find(instanceId);
  if (it != configs_.end()) {
    return it->second.lprEnabled;
  }
  return std::nullopt;
}

bool SecuRTFeatureManager::setPIP(const std::string &instanceId, bool enable) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  SecuRTFeatureConfig &config = getOrCreateConfig(instanceId);
  config.pipEnabled = enable;
  return true;
}

std::optional<bool> SecuRTFeatureManager::getPIP(
    const std::string &instanceId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = configs_.find(instanceId);
  if (it != configs_.end()) {
    return it->second.pipEnabled;
  }
  return std::nullopt;
}

bool SecuRTFeatureManager::setSurrenderDetection(const std::string &instanceId,
                                                   bool enable) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  SecuRTFeatureConfig &config = getOrCreateConfig(instanceId);
  config.surrenderDetectionEnabled = enable;
  return true;
}

std::optional<bool> SecuRTFeatureManager::getSurrenderDetection(
    const std::string &instanceId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = configs_.find(instanceId);
  if (it != configs_.end()) {
    return it->second.surrenderDetectionEnabled;
  }
  return std::nullopt;
}

bool SecuRTFeatureManager::setMaskingAreas(
    const std::string &instanceId,
    const std::vector<std::vector<Coordinate>> &areas) {
  // Validate all areas
  for (const auto &area : areas) {
    if (!validateCoordinates(area)) {
      return false;
    }
  }

  std::unique_lock<std::shared_mutex> lock(mutex_);
  SecuRTFeatureConfig &config = getOrCreateConfig(instanceId);
  config.maskingAreas = areas;
  return true;
}

std::optional<std::vector<std::vector<Coordinate>>>
SecuRTFeatureManager::getMaskingAreas(const std::string &instanceId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = configs_.find(instanceId);
  if (it != configs_.end() && !it->second.maskingAreas.empty()) {
    return it->second.maskingAreas;
  }
  return std::nullopt;
}

std::optional<SecuRTFeatureConfig>
SecuRTFeatureManager::getFeatureConfig(const std::string &instanceId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = configs_.find(instanceId);
  if (it != configs_.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool SecuRTFeatureManager::deleteFeatureConfig(
    const std::string &instanceId) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  return configs_.erase(instanceId) > 0;
}

bool SecuRTFeatureManager::validateFeatureExtractionTypes(
    const std::vector<std::string> &types) {
  if (types.empty()) {
    return false;
  }

  std::set<std::string> validTypes = {"Face", "Person", "Vehicle"};
  for (const auto &type : types) {
    if (validTypes.find(type) == validTypes.end()) {
      return false;
    }
  }
  return true;
}

bool SecuRTFeatureManager::validateAttributesExtractionMode(
    const std::string &mode) {
  return mode == "Off" || mode == "Person" || mode == "Vehicle" ||
         mode == "Both";
}

bool SecuRTFeatureManager::validatePerformanceProfile(
    const std::string &profile) {
  return profile == "Performance" || profile == "Balanced" ||
         profile == "Accurate";
}

bool SecuRTFeatureManager::validateCoordinates(
    const std::vector<Coordinate> &coordinates) {
  // Need at least 3 points for a polygon
  if (coordinates.size() < 3) {
    return false;
  }

  // Validate each coordinate has valid values
  for (const auto &coord : coordinates) {
    if (coord.x < 0.0 || coord.x > 1.0 || coord.y < 0.0 || coord.y > 1.0) {
      return false;
    }
  }

  return true;
}

SecuRTFeatureConfig &
SecuRTFeatureManager::getOrCreateConfig(const std::string &instanceId) {
  auto it = configs_.find(instanceId);
  if (it == configs_.end()) {
    configs_[instanceId] = SecuRTFeatureConfig();
  }
  return configs_[instanceId];
}

