#pragma once

#include <json/json.h>
#include <opencv2/core.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace core {

/**
 * @brief Input for a single inference request.
 */
struct InferenceInput {
  cv::Mat frame;
  std::string model_id;  // e.g. "face" for face detect + embed
  Json::Value options;   // det_prob_threshold, nms_threshold, extract_embedding, etc.
};

/**
 * @brief Result of inference.
 */
struct InferenceResult {
  bool success{false};
  std::string error;
  Json::Value data;      // e.g. faces array: box, landmarks, embedding
  uint64_t latency_ms{0};
};

/**
 * @brief Unified AI inference session (OpenCV DNN / ONNX).
 * Used by RecognitionHandler and AIProcessor; single integration point for SDK.
 */
class InferenceSession {
public:
  InferenceSession();
  ~InferenceSession();

  InferenceSession(const InferenceSession &) = delete;
  InferenceSession &operator=(const InferenceSession &) = delete;

  /**
   * @brief Load models (face detector + optional recognizer ONNX).
   * @param detector_path Path to YuNet detector ONNX
   * @param recognizer_path Path to face embedding ONNX (can be empty for detect-only)
   * @return true if at least detector loaded
   */
  bool load(const std::string &detector_path,
            const std::string &recognizer_path = "");

  void unload();
  bool isLoaded() const;

  /**
   * @brief Run inference. For model_id "face": detect faces, optionally extract embeddings.
   * @return InferenceResult with data["faces"] array (box, landmarks, embedding if requested)
   */
  InferenceResult infer(const InferenceInput &input);

  const std::string &getDetectorPath() const;
  const std::string &getRecognizerPath() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace core
