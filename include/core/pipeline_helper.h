#pragma once

#include "core/inference_session.h"
#include <json/json.h>
#include <opencv2/core.hpp>
#include <functional>
#include <memory>
#include <string>

namespace core {

/**
 * @brief Helper for short pipeline: app_src (frame source) → detector → callback.
 * Does not use InstanceRegistry; runs inference on frames and invokes callback with result.
 */
class PipelineHelper {
public:
  using ResultCallback = std::function<void(const Json::Value &result)>;

  /**
   * @brief Run a single frame through detector and call callback with inference result.
   * Uses InferenceSession (face detector + optional recognizer); no full SDK pipeline.
   * @param frame BGR image
   * @param detector_path Path to YuNet detector ONNX
   * @param recognizer_path Path to face embedding ONNX (optional)
   * @param options Inference options (det_prob_threshold, limit, extract_embedding)
   * @param callback Called with result JSON (e.g. data["faces"]) or empty on failure
   */
  static void runFrameThroughDetector(
      const cv::Mat &frame,
      const std::string &detector_path,
      const std::string &recognizer_path,
      const Json::Value &options,
      ResultCallback callback);

  /**
   * @brief Descriptor for short pipeline design: app_src → detector → callback.
   * Used for documentation / config without building full InstanceRegistry pipeline.
   */
  struct ShortPipelineDescriptor {
    std::string source_type;   // e.g. "app_src", "push"
    std::string detector_key;  // e.g. "face"
    std::string detector_path;
    std::string recognizer_path;
  };

  /**
   * @brief Build a short pipeline descriptor (no InstanceRegistry).
   */
  static ShortPipelineDescriptor buildShortPipelineDescriptor(
      const std::string &source_type,
      const std::string &detector_path,
      const std::string &recognizer_path = "");
};

}  // namespace core
