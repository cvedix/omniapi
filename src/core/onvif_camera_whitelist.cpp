#include "core/onvif_camera_whitelist.h"
#include <algorithm>
#include <cctype>
#include <map>

ONVIFCameraWhitelist::ONVIFCameraWhitelist() : allowAll_(true) {
  // Initially allow all cameras
  // Add specific manufacturers/models as they are tested and verified
}

ONVIFCameraWhitelist &ONVIFCameraWhitelist::getInstance() {
  static ONVIFCameraWhitelist instance;
  return instance;
}

bool ONVIFCameraWhitelist::isSupported(const std::string &manufacturer,
                                        const std::string &model) const {
  // If whitelist is empty, allow all
  if (allowAll_ && supportedManufacturers_.empty()) {
    return true;
  }

  // Normalize manufacturer name (lowercase)
  std::string normManufacturer = manufacturer;
  std::transform(normManufacturer.begin(), normManufacturer.end(),
                 normManufacturer.begin(), ::tolower);

  // Check if manufacturer is whitelisted
  if (supportedManufacturers_.find(normManufacturer) ==
      supportedManufacturers_.end()) {
    return false;
  }

  // Check if specific model is whitelisted
  auto it = supportedModels_.find(normManufacturer);
  if (it != supportedModels_.end()) {
    // If model set is empty, all models from this manufacturer are supported
    if (it->second.empty()) {
      return true;
    }

    // Check if specific model is in the set
    std::string normModel = model;
    std::transform(normModel.begin(), normModel.end(), normModel.begin(),
                   ::tolower);
    return it->second.find(normModel) != it->second.end();
  }

  // Manufacturer is supported, no specific model restrictions
  return true;
}

bool ONVIFCameraWhitelist::isManufacturerSupported(
    const std::string &manufacturer) const {
  if (allowAll_ && supportedManufacturers_.empty()) {
    return true;
  }

  std::string normManufacturer = manufacturer;
  std::transform(normManufacturer.begin(), normManufacturer.end(),
                 normManufacturer.begin(), ::tolower);
  return supportedManufacturers_.find(normManufacturer) !=
         supportedManufacturers_.end();
}

void ONVIFCameraWhitelist::addManufacturer(const std::string &manufacturer) {
  std::string normManufacturer = manufacturer;
  std::transform(normManufacturer.begin(), normManufacturer.end(),
                 normManufacturer.begin(), ::tolower);
  supportedManufacturers_.insert(normManufacturer);
  allowAll_ = false;
}

void ONVIFCameraWhitelist::addCameraModel(const std::string &manufacturer,
                                           const std::string &model) {
  std::string normManufacturer = manufacturer;
  std::transform(normManufacturer.begin(), normManufacturer.end(),
                 normManufacturer.begin(), ::tolower);

  std::string normModel = model;
  std::transform(normModel.begin(), normModel.end(), normModel.begin(),
                 ::tolower);

  supportedManufacturers_.insert(normManufacturer);
  supportedModels_[normManufacturer].insert(normModel);
  allowAll_ = false;
}

void ONVIFCameraWhitelist::removeManufacturer(const std::string &manufacturer) {
  std::string normManufacturer = manufacturer;
  std::transform(normManufacturer.begin(), normManufacturer.end(),
                 normManufacturer.begin(), ::tolower);
  supportedManufacturers_.erase(normManufacturer);
  supportedModels_.erase(normManufacturer);
}

std::set<std::string>
ONVIFCameraWhitelist::getSupportedManufacturers() const {
  return supportedManufacturers_;
}

bool ONVIFCameraWhitelist::isEmpty() const {
  return allowAll_ && supportedManufacturers_.empty();
}

void ONVIFCameraWhitelist::clear() {
  supportedManufacturers_.clear();
  supportedModels_.clear();
  allowAll_ = true;
}

