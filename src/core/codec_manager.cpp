#include "core/codec_manager.h"
#include <algorithm>
#include <cctype>
#include <unordered_map>

CodecManager::CodecManager() {
  // Initialize supported codecs
  supported_codecs_.insert("h264");
  supported_codecs_.insert("h265");
  supported_codecs_.insert("hevc"); // Alias for h265
  
  // Initialize codec aliases
  codec_aliases_["hevc"] = "h265";
  codec_aliases_["H265"] = "h265";
  codec_aliases_["H264"] = "h264";
  codec_aliases_["HEVC"] = "h265";
}

CodecManager& CodecManager::getInstance() {
  static CodecManager instance;
  return instance;
}

bool CodecManager::isCodecSupported(const std::string& codecId) const {
  std::string normalized = normalizeCodecId(codecId);
  return supported_codecs_.find(normalized) != supported_codecs_.end();
}

std::vector<std::string> CodecManager::getSupportedCodecs() const {
  std::vector<std::string> codecs;
  codecs.reserve(supported_codecs_.size());
  
  for (const auto& codec : supported_codecs_) {
    // Skip aliases
    if (codec != "hevc") {
      codecs.push_back(codec);
    }
  }
  
  return codecs;
}

std::string CodecManager::normalizeCodecId(const std::string& codecId) const {
  if (codecId.empty()) {
    return "";
  }
  
  // Convert to lowercase
  std::string normalized = codecId;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  
  // Check for aliases
  auto it = codec_aliases_.find(normalized);
  if (it != codec_aliases_.end()) {
    return it->second;
  }
  
  return normalized;
}

bool CodecManager::validateCodecId(const std::string& codecId) const {
  if (codecId.empty()) {
    return false;
  }
  
  std::string normalized = normalizeCodecId(codecId);
  return isCodecSupported(normalized);
}

