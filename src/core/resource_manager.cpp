#include "core/resource_manager.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <fstream>

void ResourceManager::initialize(size_t max_concurrent) {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  max_concurrent_per_device_ = max_concurrent;
  detectGPUs();
}

void ResourceManager::detectGPUs() {
  gpus_.clear(); // Clear any existing GPUs
  
  // Detect NVIDIA GPUs using nvidia-smi
  // Optimized: Query all needed info in a single call to reduce initialization time
  FILE *pipe = popen(
      "nvidia-smi --query-gpu=index,name,memory.used,memory.total --format=csv,noheader,nounits 2>/dev/null",
      "r");
  
  if (pipe) {
    char buffer[512];
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      std::string line(buffer);
      // Remove trailing newline
      if (!line.empty() && line.back() == '\n') {
        line.pop_back();
      }
      
      // Parse: index, name, memory.used, memory.total (MB)
      // Example: "0, NVIDIA GeForce RTX 3060 Ti, 1024, 8192"
      std::istringstream iss(line);
      std::string token;
      std::vector<std::string> tokens;
      
      while (std::getline(iss, token, ',')) {
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        tokens.push_back(token);
      }
      
      if (tokens.size() >= 4) {
        try {
          int parsed_id = std::stoi(tokens[0]);
          std::string name = tokens[1];
          size_t used_memory_mb = std::stoul(tokens[2]);
          size_t total_memory_mb = std::stoul(tokens[3]);
          size_t free_memory_mb = (total_memory_mb > used_memory_mb) ? (total_memory_mb - used_memory_mb) : 0;
          
          auto gpu = std::make_shared<GPUInfo>();
          gpu->device_id = parsed_id;
          gpu->name = name;
          gpu->total_memory_mb = total_memory_mb;
          gpu->used_memory_mb = used_memory_mb;
          gpu->free_memory_mb = free_memory_mb;
          gpu->utilization_percent = (total_memory_mb > 0) 
            ? (static_cast<double>(used_memory_mb) / total_memory_mb) * 100.0 
            : 0.0;
          gpu->in_use = false;
          
          gpus_.push_back(gpu);
          
          std::cout << "[ResourceManager] Detected GPU " << parsed_id << ": " 
                    << name << " (" << total_memory_mb << " MB)" << std::endl;
        } catch (const std::exception &e) {
          std::cerr << "[ResourceManager] Failed to parse GPU info: " << line 
                    << " - " << e.what() << std::endl;
        }
      }
    }
    
    pclose(pipe);
    // No need to call updateGPUMemoryUsage() here - we already got all the data in one call
  } else {
    // No nvidia-smi available or no GPUs found
    std::cout << "[ResourceManager] No NVIDIA GPUs detected (nvidia-smi not available or no GPUs)" << std::endl;
  }
  
  if (gpus_.empty()) {
    std::cout << "[ResourceManager] No GPUs available - instances will use CPU" << std::endl;
  } else {
    std::cout << "[ResourceManager] Found " << gpus_.size() << " GPU(s)" << std::endl;
  }
}

void ResourceManager::updateGPUMemoryUsage() {
  // Query current GPU memory usage from nvidia-smi
  FILE *pipe = popen(
      "nvidia-smi --query-gpu=index,memory.used,memory.total --format=csv,noheader,nounits 2>/dev/null",
      "r");
  
  if (pipe) {
    char buffer[512];
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      std::string line(buffer);
      if (!line.empty() && line.back() == '\n') {
        line.pop_back();
      }
      
      std::istringstream iss(line);
      std::string token;
      std::vector<std::string> tokens;
      
      while (std::getline(iss, token, ',')) {
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        tokens.push_back(token);
      }
      
      if (tokens.size() >= 3) {
        try {
          int device_id = std::stoi(tokens[0]);
          size_t used_mb = std::stoul(tokens[1]);
          size_t total_mb = std::stoul(tokens[2]);
          
          // Find and update GPU info
          for (auto &gpu : gpus_) {
            if (gpu->device_id == device_id) {
              gpu->used_memory_mb = used_mb;
              gpu->free_memory_mb = (total_mb > used_mb) ? (total_mb - used_mb) : 0;
              updateGPUStats(device_id);
              break;
            }
          }
        } catch (const std::exception &e) {
          // Ignore parse errors
        }
      }
    }
    
    pclose(pipe);
  }
}

std::shared_ptr<ResourceManager::Allocation>
ResourceManager::allocateGPUImpl(size_t memory_mb, int preferred_device) {
  // Caller must hold mutex_ (do not call getBestGPU() here - it would deadlock)
  std::shared_ptr<GPUInfo> selected_gpu;

  if (preferred_device >= 0 &&
      preferred_device < static_cast<int>(gpus_.size())) {
    selected_gpu = gpus_[preferred_device];
  } else if (!gpus_.empty()) {
    // Inline getBestGPU logic to avoid re-locking
    std::shared_ptr<GPUInfo> best = nullptr;
    double best_score = std::numeric_limits<double>::max();
    for (const auto &gpu : gpus_) {
      if (gpu->free_memory_mb > 0) {
        double score =
            gpu->utilization_percent -
            (static_cast<double>(gpu->free_memory_mb) / gpu->total_memory_mb) *
                100.0;
        if (score < best_score) {
          best_score = score;
          best = gpu;
        }
      }
    }
    selected_gpu = best ? best : gpus_[0];
  }

  if (!selected_gpu || selected_gpu->free_memory_mb < memory_mb) {
    return nullptr;
  }

  size_t current_allocations = 0;
  for (const auto &alloc : allocations_) {
    if (alloc.second->device_id == selected_gpu->device_id) {
      current_allocations++;
    }
  }

  if (current_allocations >= max_concurrent_per_device_) {
    return nullptr;
  }

  auto allocation = std::make_shared<Allocation>();
  allocation->device_id = selected_gpu->device_id;
  allocation->memory_mb = memory_mb;
  allocation->resource_id = "gpu_" + std::to_string(allocation_counter_++);
  allocation->allocated_at = std::chrono::steady_clock::now();

  selected_gpu->free_memory_mb -= memory_mb;
  selected_gpu->used_memory_mb += memory_mb;
  selected_gpu->in_use = true;
  selected_gpu->last_used = std::chrono::steady_clock::now();
  updateGPUStats(selected_gpu->device_id);

  allocations_[allocation->resource_id] = allocation;

  return allocation;
}

std::shared_ptr<ResourceManager::Allocation>
ResourceManager::allocateGPU(size_t memory_mb, int preferred_device) {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  return allocateGPUImpl(memory_mb, preferred_device);
}

std::shared_ptr<ResourceManager::Allocation>
ResourceManager::tryAllocateGPU(size_t memory_mb, int preferred_device,
                                int timeout_ms) {
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (timeout_ms <= 0) {
    if (!lock.try_lock())
      return nullptr;
  } else if (!lock.try_lock_for(std::chrono::milliseconds(timeout_ms))) {
    return nullptr;
  }
  return allocateGPUImpl(memory_mb, preferred_device);
}

void ResourceManager::releaseGPU(std::shared_ptr<Allocation> allocation) {
  if (!allocation)
    return;

  std::lock_guard<std::timed_mutex> lock(mutex_);

  auto it = allocations_.find(allocation->resource_id);
  if (it == allocations_.end()) {
    return;
  }

  // Find GPU and update stats
  for (auto &gpu : gpus_) {
    if (gpu->device_id == allocation->device_id) {
      gpu->free_memory_mb += allocation->memory_mb;
      gpu->used_memory_mb -= allocation->memory_mb;
      updateGPUStats(gpu->device_id);

      // Check if GPU is still in use
      bool still_in_use = false;
      for (const auto &alloc : allocations_) {
        if (alloc.second->device_id == gpu->device_id &&
            alloc.first != allocation->resource_id) {
          still_in_use = true;
          break;
        }
      }
      gpu->in_use = still_in_use;
      break;
    }
  }

  allocations_.erase(it);
}

std::shared_ptr<ResourceManager::GPUInfo>
ResourceManager::getGPUInfo(int device_id) const {
  std::lock_guard<std::timed_mutex> lock(mutex_);

  for (const auto &gpu : gpus_) {
    if (gpu->device_id == device_id) {
      return gpu;
    }
  }
  return nullptr;
}

std::vector<std::shared_ptr<ResourceManager::GPUInfo>>
ResourceManager::getAllGPUs() const {
  std::lock_guard<std::timed_mutex> lock(mutex_);
  return gpus_;
}

std::shared_ptr<ResourceManager::GPUInfo> ResourceManager::getBestGPU() const {
  std::lock_guard<std::timed_mutex> lock(mutex_);

  if (gpus_.empty()) {
    return nullptr;
  }

  // Find GPU with lowest utilization and enough free memory
  std::shared_ptr<GPUInfo> best = nullptr;
  double best_score = std::numeric_limits<double>::max();

  for (const auto &gpu : gpus_) {
    if (gpu->free_memory_mb > 0) {
      // Score based on utilization and free memory
      double score =
          gpu->utilization_percent -
          (static_cast<double>(gpu->free_memory_mb) / gpu->total_memory_mb) *
              100.0;

      if (score < best_score) {
        best_score = score;
        best = gpu;
      }
    }
  }

  return best ? best : gpus_[0];
}

void ResourceManager::updateGPUStats(int device_id) {
  // Update utilization based on memory usage
  // NOTE: This only calculates utilization from existing data.
  // Do NOT call updateGPUMemoryUsage() here to avoid recursive nvidia-smi calls.
  for (auto &gpu : gpus_) {
    if (gpu->device_id == device_id) {
      if (gpu->total_memory_mb > 0) {
        gpu->utilization_percent =
            (static_cast<double>(gpu->used_memory_mb) / gpu->total_memory_mb) *
            100.0;
      }
      break;
    }
  }
}

ResourceManager::Stats ResourceManager::getStats() const {
  std::lock_guard<std::timed_mutex> lock(mutex_);

  Stats stats;
  stats.total_gpus = gpus_.size();
  stats.total_allocations = allocations_.size();
  stats.total_memory_mb = 0;
  stats.used_memory_mb = 0;

  for (const auto &gpu : gpus_) {
    if (!gpu->in_use) {
      stats.available_gpus++;
    }
    stats.total_memory_mb += gpu->total_memory_mb;
    stats.used_memory_mb += gpu->used_memory_mb;
  }

  return stats;
}
