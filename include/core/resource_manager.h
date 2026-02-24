#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Resource manager for GPU/Accelerator allocation
 *
 * Manages GPU and accelerator resources, providing allocation,
 * deallocation, and load balancing across multiple devices.
 */
class ResourceManager {
public:
  struct GPUInfo {
    int device_id;
    std::string name;
    size_t total_memory_mb;
    size_t free_memory_mb;
    size_t used_memory_mb;
    double utilization_percent;
    std::atomic<bool> in_use{false};
    std::chrono::steady_clock::time_point last_used;
  };

  struct Allocation {
    int device_id;
    size_t memory_mb;
    std::string resource_id;
    std::chrono::steady_clock::time_point allocated_at;
  };

  static ResourceManager &getInstance() {
    static ResourceManager instance;
    return instance;
  }

  /**
   * @brief Initialize resource manager
   * @param max_concurrent Maximum concurrent allocations per device
   */
  void initialize(size_t max_concurrent = 4);

  /**
   * @brief Allocate GPU resource
   * @param memory_mb Required memory in MB
   * @param preferred_device Preferred device ID (-1 for any)
   * @return Allocation info, or nullptr if failed
   */
  std::shared_ptr<Allocation> allocateGPU(size_t memory_mb,
                                          int preferred_device = -1);

  /**
   * @brief Try to allocate GPU with timeout (non-blocking for API responsiveness)
   * @param memory_mb Required memory in MB
   * @param preferred_device Preferred device ID (-1 for any)
   * @param timeout_ms Max wait for internal lock in ms (0 = no wait)
   * @return Allocation info, or nullptr if failed or timeout
   */
  std::shared_ptr<Allocation> tryAllocateGPU(size_t memory_mb,
                                            int preferred_device = -1,
                                            int timeout_ms = 5000);

  /**
   * @brief Release GPU resource
   * @param allocation Allocation to release
   */
  void releaseGPU(std::shared_ptr<Allocation> allocation);

  /**
   * @brief Get GPU information
   * @param device_id Device ID
   * @return GPU info or nullptr if not found
   */
  std::shared_ptr<GPUInfo> getGPUInfo(int device_id) const;

  /**
   * @brief Get all available GPUs
   */
  std::vector<std::shared_ptr<GPUInfo>> getAllGPUs() const;

  /**
   * @brief Get best available GPU (least utilized)
   */
  std::shared_ptr<GPUInfo> getBestGPU() const;

  /**
   * @brief Get resource statistics
   */
  struct Stats {
    size_t total_gpus;
    size_t available_gpus;
    size_t total_allocations;
    size_t total_memory_mb;
    size_t used_memory_mb;
  };

  Stats getStats() const;

private:
  ResourceManager() = default;
  ~ResourceManager() = default;
  ResourceManager(const ResourceManager &) = delete;
  ResourceManager &operator=(const ResourceManager &) = delete;

  void detectGPUs();
  void updateGPUStats(int device_id);
  void updateGPUMemoryUsage();

  /** Called with mutex_ held. */
  std::shared_ptr<Allocation> allocateGPUImpl(size_t memory_mb,
                                              int preferred_device);

  std::vector<std::shared_ptr<GPUInfo>> gpus_;
  std::unordered_map<std::string, std::shared_ptr<Allocation>> allocations_;
  mutable std::timed_mutex mutex_;
  size_t max_concurrent_per_device_;
  std::atomic<uint64_t> allocation_counter_{0};
};
