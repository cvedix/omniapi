#pragma once

#include <string>
#include <vector>

/**
 * @brief Pipeline Builder Model Resolver
 *
 * Utility class for resolving model file paths and names.
 * Extracted from PipelineBuilder for better code organization.
 */
class PipelineBuilderModelResolver {
public:
  /**
   * @brief Resolve model file path (supports CVEDIX_DATA_ROOT, CVEDIX_SDK_ROOT,
   * or relative paths)
   * @param relativePath Relative path from cvedix_data/models (e.g.,
   * "face/yunet.onnx")
   * @return Resolved absolute or relative path
   */
  static std::string resolveModelPath(const std::string &relativePath);

  /**
   * @brief Resolve model file by name (e.g., "yunet_2023mar", "yunet_2022mar",
   * "yolov8n_face")
   * @param modelName Model name (without extension)
   * @param category Model category (e.g., "face", "object") - defaults to
   * "face"
   * @return Resolved absolute or relative path, or empty string if not found
   */
  static std::string resolveModelByName(const std::string &modelName,
                                        const std::string &category = "face");

  /**
   * @brief List available models in system directories
   * @param category Model category (e.g., "face", "object") - empty string for
   * all categories
   * @return Vector of model file paths
   */
  static std::vector<std::string>
  listAvailableModels(const std::string &category = "");

  /**
   * @brief Map detection sensitivity to threshold value
   * @param sensitivity "Low", "Medium", or "High"
   * @return Threshold value (0.0-1.0)
   */
  static double mapDetectionSensitivity(const std::string &sensitivity);
};

