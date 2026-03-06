#pragma once

#include <json/json.h>
#include <map>
#include <mutex>
#include <string>

/**
 * @brief Decoder Detector
 *
 * Detects available hardware and software decoders in the system
 * Thread-safe singleton pattern
 */
class DecoderDetector {
public:
  /**
   * @brief Get singleton instance
   */
  static DecoderDetector &getInstance();

  /**
   * @brief Detect available decoders
   * @return true if detection was successful
   */
  bool detectDecoders();

  /**
   * @brief Get decoders information as JSON
   * @return JSON object with decoder information
   * Example: {"nvidia": {"hevc": 1, "h264": 1}}
   */
  Json::Value getDecodersJson() const;

  /**
   * @brief Get decoders information as map
   * @return Map of decoder type to codec capabilities
   */
  std::map<std::string, std::map<std::string, int>> getDecoders() const;

  /**
   * @brief Check if NVIDIA decoders are available
   */
  bool hasNvidiaDecoders() const;

  /**
   * @brief Check if Intel decoders are available
   */
  bool hasIntelDecoders() const;

  /**
   * @brief Get NVIDIA decoder count for a codec
   * @param codec Codec name (e.g., "h264", "hevc")
   * @return Number of available decoders (0 if not available)
   */
  int getNvidiaDecoderCount(const std::string &codec) const;

  /**
   * @brief Get Intel decoder count for a codec
   * @param codec Codec name (e.g., "h264", "hevc")
   * @return Number of available decoders (0 if not available)
   */
  int getIntelDecoderCount(const std::string &codec) const;

  /**
   * @brief Check if decoders have been detected
   */
  bool isDetected() const;

private:
  DecoderDetector() = default;
  ~DecoderDetector() = default;
  DecoderDetector(const DecoderDetector &) = delete;
  DecoderDetector &operator=(const DecoderDetector &) = delete;

  /**
   * @brief Detect NVIDIA decoders
   */
  void detectNvidiaDecoders();

  /**
   * @brief Detect Intel decoders
   */
  void detectIntelDecoders();

  /**
   * @brief Detect software decoders
   */
  void detectSoftwareDecoders();

  /**
   * @brief Check if CUDA is available
   */
  bool checkCudaAvailable() const;

  /**
   * @brief Check if NVENC is available
   */
  bool checkNvencAvailable() const;

  /**
   * @brief Check if Intel Quick Sync is available
   */
  bool checkIntelQuickSyncAvailable() const;

  /**
   * @brief Check if FFmpeg codec is available
   */
  bool checkFFmpegCodec(const std::string &codec) const;

  mutable std::mutex mutex_;
  std::map<std::string, std::map<std::string, int>> decoders_;
  bool detected_ = false;
};

