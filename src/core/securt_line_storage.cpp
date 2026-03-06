#include "core/securt_line_storage.h"
#include <algorithm>

void SecuRTLineStorage::addCountingLine(const std::string &instanceId,
                                         std::unique_ptr<CountingLine> line) {
  std::lock_guard<std::mutex> lock(mutex_);
  CountingLine *rawLine = line.release();
  auto linePtr = std::make_unique<LinePtr>(rawLine);
  storage_[instanceId][linePtr->getId()] = std::move(linePtr);
}

void SecuRTLineStorage::addCrossingLine(const std::string &instanceId,
                                         std::unique_ptr<CrossingLine> line) {
  std::lock_guard<std::mutex> lock(mutex_);
  CrossingLine *rawLine = line.release();
  auto linePtr = std::make_unique<LinePtr>(rawLine);
  storage_[instanceId][linePtr->getId()] = std::move(linePtr);
}

void SecuRTLineStorage::addTailgatingLine(const std::string &instanceId,
                                          std::unique_ptr<TailgatingLine> line) {
  std::lock_guard<std::mutex> lock(mutex_);
  TailgatingLine *rawLine = line.release();
  auto linePtr = std::make_unique<LinePtr>(rawLine);
  storage_[instanceId][linePtr->getId()] = std::move(linePtr);
}

SecuRTLineStorage::LinePtr *
SecuRTLineStorage::getLine(const std::string &instanceId,
                            const std::string &lineId) {
  std::lock_guard<std::mutex> lock(mutex_);
  return findLineUnlocked(instanceId, lineId);
}

bool SecuRTLineStorage::deleteLine(const std::string &instanceId,
                                    const std::string &lineId) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto instanceIt = storage_.find(instanceId);
  if (instanceIt == storage_.end()) {
    return false;
  }
  auto lineIt = instanceIt->second.find(lineId);
  if (lineIt == instanceIt->second.end()) {
    return false;
  }
  instanceIt->second.erase(lineIt);
  return true;
}

void SecuRTLineStorage::deleteAllLines(const std::string &instanceId) {
  std::lock_guard<std::mutex> lock(mutex_);
  storage_.erase(instanceId);
}

Json::Value SecuRTLineStorage::getAllLines(const std::string &instanceId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  Json::Value result;
  result["countingLines"] = Json::Value(Json::arrayValue);
  result["crossingLines"] = Json::Value(Json::arrayValue);
  result["tailgatingLines"] = Json::Value(Json::arrayValue);

  auto instanceIt = storage_.find(instanceId);
  if (instanceIt == storage_.end()) {
    return result;
  }

  for (const auto &[lineId, linePtr] : instanceIt->second) {
    Json::Value lineJson = linePtr->toJson();
    switch (linePtr->type) {
      case LineType::Counting:
        result["countingLines"].append(lineJson);
        break;
      case LineType::Crossing:
        result["crossingLines"].append(lineJson);
        break;
      case LineType::Tailgating:
        result["tailgatingLines"].append(lineJson);
        break;
    }
  }

  return result;
}

std::vector<SecuRTLineStorage::LinePtr *>
SecuRTLineStorage::getCountingLines(const std::string &instanceId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<LinePtr *> result;
  auto instanceIt = storage_.find(instanceId);
  if (instanceIt == storage_.end()) {
    return result;
  }
  for (const auto &[lineId, linePtr] : instanceIt->second) {
    if (linePtr->type == LineType::Counting) {
      result.push_back(linePtr.get());
    }
  }
  return result;
}

std::vector<SecuRTLineStorage::LinePtr *>
SecuRTLineStorage::getCrossingLines(const std::string &instanceId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<LinePtr *> result;
  auto instanceIt = storage_.find(instanceId);
  if (instanceIt == storage_.end()) {
    return result;
  }
  for (const auto &[lineId, linePtr] : instanceIt->second) {
    if (linePtr->type == LineType::Crossing) {
      result.push_back(linePtr.get());
    }
  }
  return result;
}

std::vector<SecuRTLineStorage::LinePtr *>
SecuRTLineStorage::getTailgatingLines(const std::string &instanceId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<LinePtr *> result;
  auto instanceIt = storage_.find(instanceId);
  if (instanceIt == storage_.end()) {
    return result;
  }
  for (const auto &[lineId, linePtr] : instanceIt->second) {
    if (linePtr->type == LineType::Tailgating) {
      result.push_back(linePtr.get());
    }
  }
  return result;
}

bool SecuRTLineStorage::hasLine(const std::string &instanceId,
                                 const std::string &lineId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return findLineUnlocked(instanceId, lineId) != nullptr;
}

bool SecuRTLineStorage::updateLine(const std::string &instanceId,
                                   const std::string &lineId,
                                   const Json::Value &json, LineType type) {
  std::lock_guard<std::mutex> lock(mutex_);
  LinePtr *linePtr = findLineUnlocked(instanceId, lineId);
  if (!linePtr) {
    return false;
  }

  // Update based on type
  switch (type) {
    case LineType::Counting: {
      if (linePtr->type != LineType::Counting) {
        return false;
      }
      auto updated = CountingLine::fromJson(json, lineId);
      linePtr->countingLine = std::make_unique<CountingLine>(updated);
      break;
    }
    case LineType::Crossing: {
      if (linePtr->type != LineType::Crossing) {
        return false;
      }
      auto updated = CrossingLine::fromJson(json, lineId);
      linePtr->crossingLine = std::make_unique<CrossingLine>(updated);
      break;
    }
    case LineType::Tailgating: {
      if (linePtr->type != LineType::Tailgating) {
        return false;
      }
      auto updated = TailgatingLine::fromJson(json, lineId);
      linePtr->tailgatingLine = std::make_unique<TailgatingLine>(updated);
      break;
    }
  }

  return true;
}

SecuRTLineStorage::LinePtr *
SecuRTLineStorage::findLineUnlocked(const std::string &instanceId,
                                     const std::string &lineId) const {
  auto instanceIt = storage_.find(instanceId);
  if (instanceIt == storage_.end()) {
    return nullptr;
  }
  auto lineIt = instanceIt->second.find(lineId);
  if (lineIt == instanceIt->second.end()) {
    return nullptr;
  }
  return lineIt->second.get();
}

