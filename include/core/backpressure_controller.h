/**
 * @file backpressure_controller.h
 * @brief Backpressure Control và Frame Rate Limiting
 *
 * Phase 3 Optimization: Kiểm soát backpressure và adaptive frame rate
 * để tránh queue overflow và giảm I/O blocking
 */

#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace BackpressureController {

/**
 * @brief Frame dropping policy
 */
enum class DropPolicy {
  DROP_OLDEST, // Drop frame cũ nhất khi queue đầy
  DROP_NEWEST, // Drop frame mới nhất khi queue đầy (default - keep latest)
  ADAPTIVE_FPS // Giảm FPS adaptively khi detect backpressure
};

/**
 * @brief Backpressure statistics per instance
 */
struct BackpressureStats {
  std::atomic<uint64_t> frames_dropped{0};
  std::atomic<uint64_t> frames_processed{0};
  std::atomic<uint64_t> queue_full_count{0};
  std::atomic<double> current_fps{0.0};
  std::atomic<double> target_fps{30.0};
  std::atomic<bool> backpressure_detected{false};
  std::atomic<size_t> current_queue_size{
      0}; // Current queue size for queue-based dropping
  std::chrono::steady_clock::time_point last_drop_time;
  std::chrono::steady_clock::time_point last_processed_time;
};

/**
 * @brief Backpressure Controller
 *
 * Quản lý backpressure và frame dropping để tránh queue overflow
 */
class BackpressureController {
public:
  static BackpressureController &getInstance() {
    static BackpressureController instance;
    return instance;
  }

  /**
   * @brief Configure backpressure control for an instance
   */
  void configure(const std::string &instanceId,
                 DropPolicy policy = DropPolicy::DROP_NEWEST,
                 double max_fps = 30.0, size_t max_queue_size = 10);

  /**
   * @brief Check if frame should be dropped
   * @return true if frame should be dropped, false otherwise
   */
  bool shouldDropFrame(const std::string &instanceId);

  /**
   * @brief Record frame processed
   */
  void recordFrameProcessed(const std::string &instanceId);

  /**
   * @brief Record frame dropped
   */
  void recordFrameDropped(const std::string &instanceId);

  /**
   * @brief Record queue full event
   */
  void recordQueueFull(const std::string &instanceId);

  /**
   * @brief Update current queue size for an instance
   * @param instanceId Instance ID
   * @param queue_size Current queue size
   */
  void updateQueueSize(const std::string &instanceId, size_t queue_size);

  /**
   * @brief Get current FPS for instance
   */
  double getCurrentFPS(const std::string &instanceId) const;

  /**
   * @brief Get target FPS for instance (may be reduced due to backpressure)
   */
  double getTargetFPS(const std::string &instanceId) const;

  /**
   * @brief Check if backpressure is detected
   */
  bool isBackpressureDetected(const std::string &instanceId) const;

  /**
   * @brief Get statistics snapshot (copy values from atomic)
   */
  struct BackpressureStatsSnapshot {
    uint64_t frames_dropped;
    uint64_t frames_processed;
    uint64_t queue_full_count;
    double current_fps;
    double target_fps;
    bool backpressure_detected;
    size_t current_queue_size;
    std::chrono::steady_clock::time_point last_drop_time;
    std::chrono::steady_clock::time_point last_processed_time;
  };

  BackpressureStatsSnapshot getStats(const std::string &instanceId) const;

  /**
   * @brief Reset statistics for instance
   */
  void resetStats(const std::string &instanceId);

  /**
   * @brief Update adaptive FPS based on backpressure
   */
  void updateAdaptiveFPS(const std::string &instanceId);

  /**
   * @brief Update queue size for an instance (for dynamic queue sizing)
   * @param instanceId Instance ID
   * @param new_queue_size New queue size
   */
  void updateQueueSizeConfig(const std::string &instanceId,
                             size_t new_queue_size);

  /**
   * @brief Get current max queue size for instance
   * @param instanceId Instance ID
   * @return Current max queue size
   */
  size_t getMaxQueueSize(const std::string &instanceId) const;

private:
  BackpressureController() = default;
  ~BackpressureController() = default;
  BackpressureController(const BackpressureController &) = delete;
  BackpressureController &operator=(const BackpressureController &) = delete;

  struct InstanceConfig {
    DropPolicy policy;
    // Use atomic for values accessed in hot path (shouldDropFrame)
    std::atomic<double> max_fps{30.0};
    std::atomic<int64_t> min_frame_interval_ms{33}; // 1000/30 FPS default
    // Store time as nanoseconds since epoch for atomic access
    std::atomic<int64_t> last_frame_time_ns{0};
    size_t max_queue_size;

    // Helper to get last_frame_time atomically
    std::chrono::steady_clock::time_point getLastFrameTime() const {
      int64_t ns = last_frame_time_ns.load(std::memory_order_relaxed);
      if (ns == 0) {
        return std::chrono::steady_clock::time_point{}; // Epoch
      }
      return std::chrono::steady_clock::time_point(
          std::chrono::steady_clock::duration(std::chrono::nanoseconds(ns)));
    }

    // Helper to set last_frame_time atomically
    void setLastFrameTime(const std::chrono::steady_clock::time_point &time) {
      auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    time.time_since_epoch())
                    .count();
      last_frame_time_ns.store(ns, std::memory_order_relaxed);
    }
  };

  mutable std::shared_mutex stats_mutex_;  // For concurrent stats reads
  mutable std::shared_mutex config_mutex_; // For concurrent config reads
  std::unordered_map<std::string, InstanceConfig> configs_;
  std::unordered_map<std::string, BackpressureStats> stats_;

  // Adaptive FPS parameters
  // Note: MIN_FPS should be > 0 to avoid division by zero when calculating
  // interval_ms Set to 5.0 to support task 12 requirement (default FPS = 5)
  static constexpr double MIN_FPS =
      5.0; // Minimum FPS to support task 12 (default FPS = 5)
  static constexpr double MAX_FPS =
      120.0; // Increased from 60.0 to support high FPS processing for multiple
             // instances
  static constexpr double FPS_REDUCTION_FACTOR =
      0.9; // Reduce by 10% when backpressure
  static constexpr double FPS_INCREASE_FACTOR =
      1.05; // Increase by 5% when stable
  static constexpr std::chrono::milliseconds ADAPTIVE_UPDATE_INTERVAL{
      1000}; // Update every 1 second
};

} // namespace BackpressureController
