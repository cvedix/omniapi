#pragma once

#include "models/solution_config.h"
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Solution Registry
 *
 * Manages available solutions and their configurations.
 * Solutions define how to build pipelines for specific use cases.
 */
class SolutionRegistry {
public:
  static SolutionRegistry &getInstance() {
    static SolutionRegistry instance;
    return instance;
  }

  /**
   * @brief Register a solution configuration
   * @param config Solution configuration to register
   */
  void registerSolution(const SolutionConfig &config);

  /**
   * @brief Get solution configuration by ID
   * @param solutionId Solution ID
   * @return Solution config if found, empty optional otherwise
   */
  std::optional<SolutionConfig>
  getSolution(const std::string &solutionId) const;

  /**
   * @brief List all registered solution IDs
   * @return Vector of solution IDs
   */
  std::vector<std::string> listSolutions() const;

  /**
   * @brief Check if solution exists
   * @param solutionId Solution ID
   * @return true if solution exists
   */
  bool hasSolution(const std::string &solutionId) const;

  /**
   * @brief Get all solutions with details
   * @return Map of solution ID to SolutionConfig
   */
  std::unordered_map<std::string, SolutionConfig> getAllSolutions() const;

  /**
   * @brief Update an existing solution
   * @param config Updated solution configuration
   * @return true if successful, false if solution doesn't exist or is default
   */
  bool updateSolution(const SolutionConfig &config);

  /**
   * @brief Delete a solution
   * @param solutionId Solution ID to delete
   * @return true if successful, false if solution doesn't exist or is default
   */
  bool deleteSolution(const std::string &solutionId);

  /**
   * @brief Check if solution is default (non-deletable)
   * @param solutionId Solution ID
   * @return true if solution is default
   */
  bool isDefaultSolution(const std::string &solutionId) const;

  /**
   * @brief Initialize default solutions (face_detection, etc.)
   */
  void initializeDefaultSolutions();

  // ── Tiered Solution API ──────────────────────────────────────────────

  /**
   * @brief Resolve the best solution ID for a category + optional feature.
   *
   * Enables a tiered API where users specify a high-level category
   * (e.g. "security", "its") and optionally a feature (e.g. "crossline", "jam")
   * instead of knowing internal solution IDs.
   *
   * @param category  Application category: "security", "its", "armed",
   *                  "firefighting", "custom"
   * @param feature   Optional feature within the category (e.g. "crossline",
   *                  "jam", "loitering"). Empty string selects the default
   *                  solution for that category.
   * @return solutionId that matches the category/feature, or empty string
   */
  std::string resolveSolutionByCategory(
      const std::string &category,
      const std::string &feature = "") const;

  /**
   * @brief List all available solution categories
   * @return Vector of category names (e.g. "security", "its", ...)
   */
  std::vector<std::string> listCategories() const;

  /**
   * @brief List features available in a specific category
   * @param category  Category name
   * @return Vector of feature names available in that category
   */
  std::vector<std::string> listCategoryFeatures(
      const std::string &category) const;

private:
  SolutionRegistry() = default;
  ~SolutionRegistry() = default;
  SolutionRegistry(const SolutionRegistry &) = delete;
  SolutionRegistry &operator=(const SolutionRegistry &) = delete;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, SolutionConfig> solutions_;

  /**
   * @brief Register face detection solution
   */
  void registerFaceDetectionSolution();

  /**
   * @brief Register face detection with file source solution
   */
  void registerFaceDetectionFileSolution();

  /**
   * @brief Register object detection solution (YOLO)
   */
  void registerObjectDetectionSolution();

  /**
   * @brief Register face detection with RTMP streaming solution
   */
  void registerFaceDetectionRTMPSolution();

  /**
   * @brief Register behavior analysis crossline solution
   */
  void registerBACrosslineSolution();

  /**
   * @brief Register behavior analysis traffic jam solution
   */
  void registerBAJamSolution();

  /**
   * @brief Register behavior analysis stop detection solution
   */
  void registerBAStopSolution();

  /**
   * @brief Register YOLOv11 detection solution
   */
  void registerYOLOv11DetectionSolution();

  /**
   * @brief Register face swap solution
   */
  void registerFaceSwapSolution();

  /**
   * @brief Register InsightFace recognition solution
   */
  void registerInsightFaceRecognitionSolution();

  /**
   * @brief Register MLLM analysis solution
   */
  void registerMLLMAnalysisSolution();

  /**
   * @brief Register MaskRCNN detection solution (file source + file output)
   */
  void registerMaskRCNNDetectionSolution();

  /**
   * @brief Register MaskRCNN detection with RTMP streaming solution
   */
  void registerMaskRCNNRTMPSolution();

  /**
   * @brief Register face_detection_default solution
   */
  void registerFaceDetectionDefaultSolution();

  /**
   * @brief Register face_detection_rtmp_default solution
   */
  void registerFaceDetectionRTMPDefaultSolution();

  /**
   * @brief Register face_detection_rtsp_rtmp_default solution
   */
  void registerFaceDetectionRTSPRTMPDefaultSolution();

  /**
   * @brief Register face_detection_rtmp_rtmp_default solution
   */
  void registerFaceDetectionRTMPRTMPDefaultSolution();

  /**
   * @brief Register face_detection_rtsp_default solution
   */
  void registerFaceDetectionRTSPDefaultSolution();

  /**
   * @brief Register rtsp_face_detection_default solution
   */
  void registerRTSPFaceDetectionDefaultSolution();

  /**
   * @brief Register minimal_default solution
   */
  void registerMinimalDefaultSolution();

  /**
   * @brief Register object_detection_yolo_default solution
   */
  void registerObjectDetectionYOLODefaultSolution();

  /**
   * @brief Register ba_crossline_default solution
   */
  void registerBACrosslineDefaultSolution();

  /**
   * @brief Register ba_crossline_mqtt_default solution
   */
  void registerBACrosslineMQTTDefaultSolution();

  /**
   * @brief Register ba_jam_default solution
   */
  void registerBAJamDefaultSolution();

  /**
   * @brief Register ba_jam_mqtt_default solution
   */
  void registerBAJamMQTTDefaultSolution();

  /**
   * @brief Register ba_stop_default solution
   */
  void registerBAStopDefaultSolution();

  /**
   * @brief Register ba_stop_mqtt_default solution
   */
  void registerBAStopMQTTDefaultSolution();

  /**
   * @brief Register ba_loitering solution
   */
  void registerBALoiteringSolution();

  /**
   * @brief Register ba_area_enter_exit solution
   */
  void registerBAAreaEnterExitSolution();

  /**
   * @brief Register ba_line_counting solution
   */
  void registerBALineCountingSolution();

  /**
   * @brief Register ba_crowding solution
   */
  void registerBACrowdingSolution();

  /**
   * @brief Register securt solution
   */
  void registerSecuRTSolution();

  /**
   * @brief Register fire/smoke detection solution
   */
  void registerFireSmokeDetectionSolution();

  /**
   * @brief Register obstacle detection solution
   */
  void registerObstacleDetectionSolution();

  /**
   * @brief Register wrong way detection solution
   */
  void registerWrongWayDetectionSolution();

#ifdef CVEDIX_WITH_RKNN
  /**
   * @brief Register RKNN YOLOv11 detection solution
   */
  void registerRKNNYOLOv11DetectionSolution();
#endif

#ifdef CVEDIX_WITH_TRT
  /**
   * @brief Register TensorRT InsightFace recognition solution
   */
  void registerTRTInsightFaceRecognitionSolution();
#endif
};
