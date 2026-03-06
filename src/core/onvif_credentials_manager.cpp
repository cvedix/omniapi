#include "core/onvif_credentials_manager.h"

ONVIFCredentialsManager &ONVIFCredentialsManager::getInstance() {
  static ONVIFCredentialsManager instance;
  return instance;
}

void ONVIFCredentialsManager::setCredentials(
    const std::string &cameraId, const ONVIFCredentials &credentials) {
  std::lock_guard<std::mutex> lock(mutex_);
  credentials_[cameraId] = credentials;
}

bool ONVIFCredentialsManager::getCredentials(
    const std::string &cameraId, ONVIFCredentials &credentials) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = credentials_.find(cameraId);
  if (it != credentials_.end()) {
    credentials = it->second;
    return true;
  }
  return false;
}

void ONVIFCredentialsManager::removeCredentials(const std::string &cameraId) {
  std::lock_guard<std::mutex> lock(mutex_);
  credentials_.erase(cameraId);
}

void ONVIFCredentialsManager::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  credentials_.clear();
}

bool ONVIFCredentialsManager::hasCredentials(
    const std::string &cameraId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return credentials_.find(cameraId) != credentials_.end();
}

