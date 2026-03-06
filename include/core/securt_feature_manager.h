#pragma once

#include "core/securt_feature_config.h"
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief SecuRT Feature Manager
 *
 * Manages feature configurations for SecuRT instances.
 * Thread-safe storage and retrieval of feature settings.
 */
class SecuRTFeatureManager {
public:
  /**
   * @brief Set motion detection area
   * @param instanceId Instance ID
   * @param coordinates Motion area coordinates
   * @return true if successful
   */
  bool setMotionArea(const std::string &instanceId,
                     const std::vector<Coordinate> &coordinates);

  /**
   * @brief Get motion detection area
   * @param instanceId Instance ID
   * @return Motion area coordinates if set
   */
  std::optional<std::vector<Coordinate>>
  getMotionArea(const std::string &instanceId) const;

  /**
   * @brief Set feature extraction types
   * @param instanceId Instance ID
   * @param types Feature extraction types ("Face", "Person", "Vehicle")
   * @return true if successful
   */
  bool setFeatureExtraction(const std::string &instanceId,
                            const std::vector<std::string> &types);

  /**
   * @brief Get feature extraction types
   * @param instanceId Instance ID
   * @return Feature extraction types if set
   */
  std::optional<std::vector<std::string>>
  getFeatureExtraction(const std::string &instanceId) const;

  /**
   * @brief Set attributes extraction mode
   * @param instanceId Instance ID
   * @param mode "Off", "Person", "Vehicle", or "Both"
   * @return true if successful
   */
  bool setAttributesExtraction(const std::string &instanceId,
                                const std::string &mode);

  /**
   * @brief Get attributes extraction mode
   * @param instanceId Instance ID
   * @return Mode string if set
   */
  std::optional<std::string>
  getAttributesExtraction(const std::string &instanceId) const;

  /**
   * @brief Set performance profile
   * @param instanceId Instance ID
   * @param profile "Performance", "Balanced", or "Accurate"
   * @return true if successful
   */
  bool setPerformanceProfile(const std::string &instanceId,
                              const std::string &profile);

  /**
   * @brief Get performance profile
   * @param instanceId Instance ID
   * @return Profile string if set
   */
  std::optional<std::string>
  getPerformanceProfile(const std::string &instanceId) const;

  /**
   * @brief Enable/disable face detection
   * @param instanceId Instance ID
   * @param enable Enable flag
   * @return true if successful
   */
  bool setFaceDetection(const std::string &instanceId, bool enable);

  /**
   * @brief Get face detection status
   * @param instanceId Instance ID
   * @return Enable status if set
   */
  std::optional<bool> getFaceDetection(const std::string &instanceId) const;

  /**
   * @brief Enable/disable LPR
   * @param instanceId Instance ID
   * @param enable Enable flag
   * @return true if successful
   */
  bool setLPR(const std::string &instanceId, bool enable);

  /**
   * @brief Get LPR status
   * @param instanceId Instance ID
   * @return Enable status if set
   */
  std::optional<bool> getLPR(const std::string &instanceId) const;

  /**
   * @brief Enable/disable PIP
   * @param instanceId Instance ID
   * @param enable Enable flag
   * @return true if successful
   */
  bool setPIP(const std::string &instanceId, bool enable);

  /**
   * @brief Get PIP status
   * @param instanceId Instance ID
   * @return Enable status if set
   */
  std::optional<bool> getPIP(const std::string &instanceId) const;

  /**
   * @brief Enable/disable surrender detection
   * @param instanceId Instance ID
   * @param enable Enable flag
   * @return true if successful
   */
  bool setSurrenderDetection(const std::string &instanceId, bool enable);

  /**
   * @brief Get surrender detection status
   * @param instanceId Instance ID
   * @return Enable status if set
   */
  std::optional<bool>
  getSurrenderDetection(const std::string &instanceId) const;

  /**
   * @brief Set masking areas
   * @param instanceId Instance ID
   * @param areas Vector of masking area coordinates
   * @return true if successful
   */
  bool setMaskingAreas(
      const std::string &instanceId,
      const std::vector<std::vector<Coordinate>> &areas);

  /**
   * @brief Get masking areas
   * @param instanceId Instance ID
   * @return Masking areas if set
   */
  std::optional<std::vector<std::vector<Coordinate>>>
  getMaskingAreas(const std::string &instanceId) const;

  /**
   * @brief Get full feature configuration
   * @param instanceId Instance ID
   * @return Feature configuration if exists
   */
  std::optional<SecuRTFeatureConfig>
  getFeatureConfig(const std::string &instanceId) const;

  /**
   * @brief Delete feature configuration for instance
   * @param instanceId Instance ID
   * @return true if deleted
   */
  bool deleteFeatureConfig(const std::string &instanceId);

  /**
   * @brief Validate feature extraction types
   * @param types Types to validate
   * @return true if valid
   */
  static bool validateFeatureExtractionTypes(
      const std::vector<std::string> &types);

  /**
   * @brief Validate attributes extraction mode
   * @param mode Mode to validate
   * @return true if valid
   */
  static bool validateAttributesExtractionMode(const std::string &mode);

  /**
   * @brief Validate performance profile
   * @param profile Profile to validate
   * @return true if valid
   */
  static bool validatePerformanceProfile(const std::string &profile);

  /**
   * @brief Validate coordinates (polygon)
   * @param coordinates Coordinates to validate
   * @return true if valid
   */
  static bool validateCoordinates(const std::vector<Coordinate> &coordinates);

private:
  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, SecuRTFeatureConfig> configs_;

  /**
   * @brief Get or create feature config for instance
   */
  SecuRTFeatureConfig &getOrCreateConfig(const std::string &instanceId);
};

