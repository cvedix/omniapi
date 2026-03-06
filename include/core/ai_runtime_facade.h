#pragma once

#include "core/ai_cache.h"
#include "core/inference_session.h"
#include <json/json.h>
#include <opencv2/core.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core {

/**
 * @brief Request to AI Runtime Facade (encoded/compressed payload + codec + model).
 */
struct AIRuntimeRequest {
  std::vector<uint8_t> payload;  // Raw image data (JPEG/PNG bytes or base64-decoded)
  std::string codec;             // "jpeg", "png", "webp", or "" for auto-detect
  std::string model_key;         // e.g. "face"
  Json::Value options;           // det_prob_threshold, limit, extract_embedding, detector_path, recognizer_path
};

/**
 * @brief Response from AI Runtime Facade.
 */
struct AIRuntimeResponse {
  bool success{false};
  std::string error;
  Json::Value result;       // e.g. data["faces"] from inference
  bool from_cache{false};
  uint64_t decode_ms{0};
  uint64_t inference_ms{0};
};

/**
 * @brief Facade: request (encoded/compressed, codec, model_key) → decode → cache? → infer → response.
 * Single entry point for recognition and push flows.
 */
class AIRuntimeFacade {
public:
  AIRuntimeFacade();
  ~AIRuntimeFacade();

  AIRuntimeFacade(const AIRuntimeFacade &) = delete;
  AIRuntimeFacade &operator=(const AIRuntimeFacade &) = delete;

  /**
   * @brief Process request: decode → optional cache lookup → infer → response.
   * @param request Payload, codec, model_key, options (may include detector_path, recognizer_path for "face")
   * @return Response with result, from_cache, decode_ms, inference_ms
   */
  AIRuntimeResponse request(const AIRuntimeRequest &request);

  /**
   * @brief Enable/disable result cache (default disabled for facade instance).
   */
  void setCacheEnabled(bool enabled) { cache_enabled_ = enabled; }
  bool isCacheEnabled() const { return cache_enabled_; }

private:
  bool decodePayload(const AIRuntimeRequest &request, cv::Mat &frame,
                     std::string &codec_used, uint64_t &decode_ms);
  InferenceSession &getSessionForModel(const std::string &model_key,
                                       const Json::Value &options);

  std::unique_ptr<InferenceSession> session_;
  std::string last_detector_path_;
  std::string last_recognizer_path_;
  bool cache_enabled_{false};
  std::unique_ptr<AICache> cache_;
};

}  // namespace core
