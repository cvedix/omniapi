#pragma once

#include <string>
#include <vector>
#include <set>
#include <unordered_map>

/**
 * @brief Codec Manager
 * 
 * Detects available codecs, validates codec support,
 * and maps codec IDs to decoders.
 */
class CodecManager {
public:
  /**
   * @brief Get singleton instance
   */
  static CodecManager& getInstance();
  
  /**
   * @brief Check if codec is supported
   * @param codecId Codec ID (e.g., "h264", "h265", "hevc")
   * @return true if codec is supported
   */
  bool isCodecSupported(const std::string& codecId) const;
  
  /**
   * @brief Get list of supported codecs
   * @return Vector of supported codec IDs
   */
  std::vector<std::string> getSupportedCodecs() const;
  
  /**
   * @brief Normalize codec ID (e.g., "hevc" -> "h265")
   * @param codecId Codec ID to normalize
   * @return Normalized codec ID
   */
  std::string normalizeCodecId(const std::string& codecId) const;
  
  /**
   * @brief Validate codec ID
   * @param codecId Codec ID to validate
   * @return true if codec ID is valid
   */
  bool validateCodecId(const std::string& codecId) const;

private:
  CodecManager();
  ~CodecManager() = default;
  CodecManager(const CodecManager&) = delete;
  CodecManager& operator=(const CodecManager&) = delete;
  
  // Supported codecs
  std::set<std::string> supported_codecs_;
  
  // Codec aliases (e.g., "hevc" -> "h265")
  std::unordered_map<std::string, std::string> codec_aliases_;
};

