#include "core/onvif_camera_registry.h"
#include <algorithm>
#include <vector>

ONVIFCameraRegistry &ONVIFCameraRegistry::getInstance() {
  static ONVIFCameraRegistry instance;
  return instance;
}

void ONVIFCameraRegistry::addCamera(const std::string &cameraId,
                                     const ONVIFCamera &camera) {
  std::lock_guard<std::mutex> lock(mutex_);
  cameras_[cameraId] = camera;
  // Also index by UUID if different from cameraId
  if (!camera.uuid.empty() && camera.uuid != cameraId) {
    cameras_[camera.uuid] = camera;
  }
  // Also index by IP if different
  if (!camera.ip.empty() && camera.ip != cameraId && camera.ip != camera.uuid) {
    cameras_[camera.ip] = camera;
  }
}

bool ONVIFCameraRegistry::getCamera(const std::string &cameraId,
                                     ONVIFCamera &camera) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = cameras_.find(cameraId);
  if (it != cameras_.end()) {
    camera = it->second;
    return true;
  }
  return false;
}

std::vector<ONVIFCamera> ONVIFCameraRegistry::getAllCameras() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ONVIFCamera> result;
  std::map<std::string, ONVIFCamera> uniqueCameras;

  // Collect unique cameras by UUID (preferred) or IP
  for (const auto &[key, camera] : cameras_) {
    if (!camera.uuid.empty()) {
      uniqueCameras[camera.uuid] = camera;
    } else if (!camera.ip.empty()) {
      uniqueCameras[camera.ip] = camera;
    }
  }

  for (const auto &[key, camera] : uniqueCameras) {
    result.push_back(camera);
  }

  return result;
}

void ONVIFCameraRegistry::removeCamera(const std::string &cameraId) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Find the camera to get its UUID and IP
  auto it = cameras_.find(cameraId);
  if (it != cameras_.end()) {
    const ONVIFCamera &camera = it->second;
    // Remove all entries for this camera
    cameras_.erase(cameraId);
    if (!camera.uuid.empty()) {
      cameras_.erase(camera.uuid);
    }
    if (!camera.ip.empty()) {
      cameras_.erase(camera.ip);
    }
  }
}

void ONVIFCameraRegistry::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  cameras_.clear();
}

bool ONVIFCameraRegistry::hasCamera(const std::string &cameraId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cameras_.find(cameraId) != cameras_.end();
}

