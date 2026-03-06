#include "core/securt_statistics_collector.h"
#include <json/json.h>
#include <shared_mutex>

SecuRTStatisticsCollector::SecuRTStatisticsCollector() = default;

void SecuRTStatisticsCollector::startTracking(const std::string &instanceId) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  auto &tracker = trackers_[instanceId];
  tracker.startTime = std::chrono::system_clock::now();
  tracker.framesProcessed.store(0);
  tracker.trackCount.store(0);
  tracker.frameRate.store(0.0);
  tracker.latency.store(0.0);
  tracker.isRunning.store(true);
}

void SecuRTStatisticsCollector::stopTracking(const std::string &instanceId) {
  setRunningStatus(instanceId, false);
}

SecuRTInstanceStats
SecuRTStatisticsCollector::getStatistics(const std::string &instanceId) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = trackers_.find(instanceId);
  if (it == trackers_.end()) {
    SecuRTInstanceStats stats;
    return stats;
  }

  const StatisticsTracker &tracker = it->second;
  SecuRTInstanceStats stats;
  
  // Convert start time to milliseconds since epoch
  auto startTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      tracker.startTime.time_since_epoch()).count();
  stats.startTime = startTimeMs;
  
  stats.frameRate = tracker.frameRate.load(std::memory_order_relaxed);
  stats.latency = tracker.latency.load(std::memory_order_relaxed);
  stats.framesProcessed = static_cast<int>(
      tracker.framesProcessed.load(std::memory_order_relaxed));
  stats.trackCount = tracker.trackCount.load(std::memory_order_relaxed);
  stats.isRunning = tracker.isRunning.load(std::memory_order_relaxed);

  return stats;
}

void SecuRTStatisticsCollector::recordFrameProcessed(
    const std::string &instanceId) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = trackers_.find(instanceId);
  if (it != trackers_.end()) {
    it->second.framesProcessed.fetch_add(1, std::memory_order_relaxed);
  }
}

void SecuRTStatisticsCollector::updateTrackCount(const std::string &instanceId,
                                                   int trackCount) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = trackers_.find(instanceId);
  if (it != trackers_.end()) {
    it->second.trackCount.store(trackCount, std::memory_order_relaxed);
  }
}

void SecuRTStatisticsCollector::updateFrameRate(const std::string &instanceId,
                                                  double frameRate) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = trackers_.find(instanceId);
  if (it != trackers_.end()) {
    it->second.frameRate.store(frameRate, std::memory_order_relaxed);
  }
}

void SecuRTStatisticsCollector::updateLatency(const std::string &instanceId,
                                                double latency) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = trackers_.find(instanceId);
  if (it != trackers_.end()) {
    it->second.latency.store(latency, std::memory_order_relaxed);
  }
}

void SecuRTStatisticsCollector::setRunningStatus(const std::string &instanceId,
                                                   bool isRunning) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = trackers_.find(instanceId);
  if (it != trackers_.end()) {
    it->second.isRunning.store(isRunning, std::memory_order_relaxed);
  }
}

void SecuRTStatisticsCollector::clearStatistics(
    const std::string &instanceId) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  trackers_.erase(instanceId);
}

