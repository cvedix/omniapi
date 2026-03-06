#include "instances/instance_state_manager.h"
#include <algorithm>
#include <sstream>

Json::Value InstanceStateManager::getState(const std::string &instanceId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = states_.find(instanceId);
  if (it != states_.end()) {
    return it->second;
  }
  return Json::Value(Json::objectValue);
}

bool InstanceStateManager::setState(const std::string &instanceId,
                                     const std::string &path,
                                     const Json::Value &value) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = states_.find(instanceId);
  if (it == states_.end()) {
    // Initialize with empty object if not exists
    states_[instanceId] = Json::Value(Json::objectValue);
    it = states_.find(instanceId);
  }

  return setNestedJsonValue(it->second, path, value);
}

void InstanceStateManager::clearState(const std::string &instanceId) {
  std::lock_guard<std::mutex> lock(mutex_);
  states_.erase(instanceId);
}

void InstanceStateManager::initializeState(const std::string &instanceId) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Initialize with empty object if not exists
  if (states_.find(instanceId) == states_.end()) {
    states_[instanceId] = Json::Value(Json::objectValue);
  }
}

bool InstanceStateManager::hasState(const std::string &instanceId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return states_.find(instanceId) != states_.end();
}

bool InstanceStateManager::setNestedJsonValue(Json::Value &root,
                                                const std::string &path,
                                                const Json::Value &value) const {
  if (path.empty()) {
    // If path is empty, replace entire root
    root = value;
    return true;
  }

  // Split path by "/"
  std::vector<std::string> parts;
  std::stringstream ss(path);
  std::string part;
  while (std::getline(ss, part, '/')) {
    if (!part.empty()) {
      parts.push_back(part);
    }
  }

  if (parts.empty()) {
    return false;
  }

  // Navigate/create path
  Json::Value *current = &root;
  for (size_t i = 0; i < parts.size() - 1; ++i) {
    const std::string &key = parts[i];
    if (!current->isMember(key) || !current->operator[](key).isObject()) {
      (*current)[key] = Json::Value(Json::objectValue);
    }
    current = &(*current)[key];
  }

  // Set value at final path
  (*current)[parts.back()] = value;
  return true;
}

