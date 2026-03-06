#include "core/decoder_detector.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

DecoderDetector &DecoderDetector::getInstance() {
  static DecoderDetector instance;
  return instance;
}

bool DecoderDetector::detectDecoders() {
  std::lock_guard<std::mutex> lock(mutex_);

  decoders_.clear();

  // Detect NVIDIA decoders
  detectNvidiaDecoders();

  // Detect Intel decoders
  detectIntelDecoders();

  // Detect software decoders (optional, can be added later)
  // detectSoftwareDecoders();

  detected_ = true;
  return true;
}

void DecoderDetector::detectNvidiaDecoders() {
  // Check if NVIDIA hardware is available
  if (!checkCudaAvailable() && !checkNvencAvailable()) {
    return;
  }

  std::map<std::string, int> nvidiaDecoders;

  // Try to detect NVIDIA decoders
  // For now, we'll use a simple heuristic: if CUDA/NVENC is available, assume decoders exist
  // In production, you might want to query NVENC API or check system capabilities

  // Check for H.264 decoder
  if (checkNvencAvailable() || checkCudaAvailable()) {
    // Try to query actual decoder count (simplified for now)
    // In production, use NVENC API: nvEncGetEncodeCaps
    nvidiaDecoders["h264"] = 1; // Assume at least 1 decoder
  }

  // Check for HEVC decoder
  if (checkNvencAvailable() || checkCudaAvailable()) {
    // Try to query actual decoder count (simplified for now)
    nvidiaDecoders["hevc"] = 1; // Assume at least 1 decoder
  }

  if (!nvidiaDecoders.empty()) {
    decoders_["nvidia"] = nvidiaDecoders;
  }
}

void DecoderDetector::detectIntelDecoders() {
  // Check if Intel Quick Sync is available
  if (!checkIntelQuickSyncAvailable()) {
    return;
  }

  std::map<std::string, int> intelDecoders;

  // Try to detect Intel decoders
  // For now, we'll use a simple heuristic
  // In production, you might want to query Intel Media SDK

  // Check for H.264 decoder
  if (checkIntelQuickSyncAvailable()) {
    intelDecoders["h264"] = 1; // Assume at least 1 decoder
  }

  // Check for HEVC decoder
  if (checkIntelQuickSyncAvailable()) {
    intelDecoders["hevc"] = 1; // Assume at least 1 decoder
  }

  if (!intelDecoders.empty()) {
    decoders_["intel"] = intelDecoders;
  }
}

void DecoderDetector::detectSoftwareDecoders() {
  // Software decoders are typically always available via FFmpeg
  // For now, we'll skip this as it's not required by the task
  // Can be implemented later if needed
}

bool DecoderDetector::checkCudaAvailable() const {
  // Check if CUDA is available by checking for nvidia-smi command
  // or checking for CUDA libraries
  std::string command = "command -v nvidia-smi >/dev/null 2>&1";
  int status = std::system(command.c_str());
  if (status == 0) {
    return true;
  }

  // Also check for CUDA library files
  std::vector<std::string> cudaLibs = {
      "/usr/lib/x86_64-linux-gnu/libcuda.so",
      "/usr/local/cuda/lib64/libcudart.so"
  };

  for (const auto &lib : cudaLibs) {
    std::ifstream file(lib);
    if (file.good()) {
      return true;
    }
  }

  return false;
}

bool DecoderDetector::checkNvencAvailable() const {
  // Check if NVENC is available
  // This is a simplified check - in production, you'd query NVENC API
  if (checkCudaAvailable()) {
    // If CUDA is available, NVENC is likely available
    // Check for NVENC library
    std::vector<std::string> nvencLibs = {
        "/usr/lib/x86_64-linux-gnu/libnvidia-encode.so",
        "/usr/lib/x86_64-linux-gnu/libnvidia-encode.so.1"
    };

    for (const auto &lib : nvencLibs) {
      std::ifstream file(lib);
      if (file.good()) {
        return true;
      }
    }
  }

  return false;
}

bool DecoderDetector::checkIntelQuickSyncAvailable() const {
  // Check if Intel Quick Sync is available
  // Check for Intel Media SDK or VAAPI
  std::vector<std::string> intelLibs = {
      "/usr/lib/x86_64-linux-gnu/libmfx.so",
      "/usr/lib/x86_64-linux-gnu/libva.so",
      "/usr/lib/x86_64-linux-gnu/libva-drm.so"
  };

  for (const auto &lib : intelLibs) {
    std::ifstream file(lib);
    if (file.good()) {
      return true;
    }
  }

  // Also check for Intel GPU in /sys/class/drm
  std::vector<std::string> drmPaths = {
      "/sys/class/drm/card0/device/vendor",
      "/sys/class/drm/card1/device/vendor"
  };

  for (const auto &path : drmPaths) {
    std::ifstream file(path);
    if (file.good()) {
      std::string vendor;
      file >> vendor;
      // Intel vendor ID is 0x8086
      if (vendor == "0x8086" || vendor == "8086") {
        return true;
      }
    }
  }

  return false;
}

bool DecoderDetector::checkFFmpegCodec(const std::string &codec) const {
  // Check if FFmpeg codec is available
  std::string command = "ffmpeg -codecs 2>/dev/null | grep -q " + codec;
  int status = std::system(command.c_str());
  return status == 0;
}

Json::Value DecoderDetector::getDecodersJson() const {
  std::lock_guard<std::mutex> lock(mutex_);

  Json::Value result;

  for (const auto &[decoderType, codecs] : decoders_) {
    Json::Value codecObj;
    for (const auto &[codec, count] : codecs) {
      codecObj[codec] = count;
    }
    result[decoderType] = codecObj;
  }

  return result;
}

std::map<std::string, std::map<std::string, int>> DecoderDetector::getDecoders() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return decoders_;
}

bool DecoderDetector::hasNvidiaDecoders() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return decoders_.find("nvidia") != decoders_.end();
}

bool DecoderDetector::hasIntelDecoders() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return decoders_.find("intel") != decoders_.end();
}

int DecoderDetector::getNvidiaDecoderCount(const std::string &codec) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = decoders_.find("nvidia");
  if (it != decoders_.end()) {
    auto codecIt = it->second.find(codec);
    if (codecIt != it->second.end()) {
      return codecIt->second;
    }
  }
  return 0;
}

int DecoderDetector::getIntelDecoderCount(const std::string &codec) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = decoders_.find("intel");
  if (it != decoders_.end()) {
    auto codecIt = it->second.find(codec);
    if (codecIt != it->second.end()) {
      return codecIt->second;
    }
  }
  return 0;
}

bool DecoderDetector::isDetected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return detected_;
}

