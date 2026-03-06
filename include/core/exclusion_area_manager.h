#pragma once

#include "core/securt_feature_config.h"
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Exclusion Area Manager
 *
 * Manages exclusion areas for SecuRT instances.
 * Thread-safe storage and retrieval of exclusion areas.
 */
class ExclusionAreaManager {
public:
  /**
   * @brief Add exclusion area
   * @param instanceId Instance ID
   * @param area Exclusion area to add
   * @return true if successful
   */
  bool addExclusionArea(const std::string &instanceId,
                        const ExclusionArea &area);

  /**
   * @brief Get all exclusion areas for instance
   * @param instanceId Instance ID
   * @return Vector of exclusion areas
   */
  std::vector<ExclusionArea>
  getExclusionAreas(const std::string &instanceId) const;

  /**
   * @brief Delete all exclusion areas for instance
   * @param instanceId Instance ID
   * @return true if successful
   */
  bool deleteExclusionAreas(const std::string &instanceId);

  /**
   * @brief Validate exclusion area
   * @param area Exclusion area to validate
   * @return true if valid
   */
  static bool validateExclusionArea(const ExclusionArea &area);

private:
  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, std::vector<ExclusionArea>> exclusion_areas_;
};

