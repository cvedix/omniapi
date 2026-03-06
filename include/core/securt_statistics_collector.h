#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <json/json.h>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

/**
 * @brief SecuRT Instance Statistics
 *
 * Statistics for a SecuRT instance.
 */
struct SecuRTInstanceStats {
  int64_t startTime = 0;        // Unix timestamp in milliseconds
  double frameRate = 0.0;       // Current frame rate
  double latency = 0.0;         // Average latency in milliseconds
  int framesProcessed = 0;      // Total frames processed
  int trackCount = 0;           // Current track count
  bool isRunning = false;       // Whether instance is running

  /**
   * @brief Convert to JSON
   */
  Json::Value toJson() const {
    Json::Value json;
    json["startTime"] = static_cast<Json::Int64>(startTime);
    json["frameRate"] = frameRate;
    json["latency"] = latency;
    json["framesProcessed"] = framesProcessed;
    json["trackCount"] = trackCount;
    json["isRunning"] = isRunning;
    return json;
  }
};

/**
 * @brief SecuRT Statistics Collector
 *
 * Thread-safe statistics collector for SecuRT instances.
 */
class SecuRTStatisticsCollector {
public:
  /**
   * @brief Constructor
   */
  SecuRTStatisticsCollector();

  /**
   * @brief Start tracking statistics for an instance
   * @param instanceId Instance ID
   */
  void startTracking(const std::string &instanceId);

  /**
   * @brief Stop tracking statistics for an instance
   * @param instanceId Instance ID
   */
  void stopTracking(const std::string &instanceId);

  /**
   * @brief Get statistics for an instance
   * @param instanceId Instance ID
   * @return Statistics if found, default stats otherwise
   */
  SecuRTInstanceStats getStatistics(const std::string &instanceId) const;

  /**
   * @brief Update frame processed count
   * @param instanceId Instance ID
   */
  void recordFrameProcessed(const std::string &instanceId);

  /**
   * @brief Update track count
   * @param instanceId Instance ID
   * @param trackCount Current track count
   */
  void updateTrackCount(const std::string &instanceId, int trackCount);

  /**
   * @brief Update frame rate
   * @param instanceId Instance ID
   * @param frameRate Current frame rate
   */
  void updateFrameRate(const std::string &instanceId, double frameRate);

  /**
   * @brief Update latency
   * @param instanceId Instance ID
   * @param latency Average latency in milliseconds
   */
  void updateLatency(const std::string &instanceId, double latency);

  /**
   * @brief Set running status
   * @param instanceId Instance ID
   * @param isRunning Running status
   */
  void setRunningStatus(const std::string &instanceId, bool isRunning);

  /**
   * @brief Clear statistics for an instance
   * @param instanceId Instance ID
   */
  void clearStatistics(const std::string &instanceId);

private:
  struct StatisticsTracker {
    std::chrono::system_clock::time_point startTime;
    std::atomic<int64_t> framesProcessed{0};
    std::atomic<int> trackCount{0};
    std::atomic<double> frameRate{0.0};
    std::atomic<double> latency{0.0};
    std::atomic<bool> isRunning{false};
  };

  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, StatisticsTracker> trackers_;
};

