#pragma once

#include <json/json.h>
#include <string>

// Forward declarations
class AreaManager;
class SecuRTLineManager;

/**
 * @brief Analytics Entities Manager
 *
 * Aggregates all analytics entities (areas and lines) for a SecuRT instance.
 * This integrates with Areas and Lines managers (TASK-008, TASK-009).
 */
class AnalyticsEntitiesManager {
public:
  /**
   * @brief Set area manager (dependency injection)
   */
  static void setAreaManager(AreaManager *manager);

  /**
   * @brief Set line manager (dependency injection)
   */
  void setLineManager(SecuRTLineManager *manager);
  /**
   * @brief Get all analytics entities for an instance
   * @param instanceId Instance ID
   * @return JSON object with all areas and lines
   */
  Json::Value getAnalyticsEntities(const std::string &instanceId) const;

private:
  /**
   * @brief Get crossing areas
   * @param instanceId Instance ID
   * @return JSON array of crossing areas
   */
  Json::Value getCrossingAreas(const std::string &instanceId) const;

  /**
   * @brief Get intrusion areas
   * @param instanceId Instance ID
   * @return JSON array of intrusion areas
   */
  Json::Value getIntrusionAreas(const std::string &instanceId) const;

  /**
   * @brief Get loitering areas
   * @param instanceId Instance ID
   * @return JSON array of loitering areas
   */
  Json::Value getLoiteringAreas(const std::string &instanceId) const;

  /**
   * @brief Get crowding areas
   * @param instanceId Instance ID
   * @return JSON array of crowding areas
   */
  Json::Value getCrowdingAreas(const std::string &instanceId) const;

  /**
   * @brief Get occupancy areas
   * @param instanceId Instance ID
   * @return JSON array of occupancy areas
   */
  Json::Value getOccupancyAreas(const std::string &instanceId) const;

  /**
   * @brief Get crowd estimation areas
   * @param instanceId Instance ID
   * @return JSON array of crowd estimation areas
   */
  Json::Value getCrowdEstimationAreas(const std::string &instanceId) const;

  /**
   * @brief Get dwelling areas
   * @param instanceId Instance ID
   * @return JSON array of dwelling areas
   */
  Json::Value getDwellingAreas(const std::string &instanceId) const;

  /**
   * @brief Get armed person areas
   * @param instanceId Instance ID
   * @return JSON array of armed person areas
   */
  Json::Value getArmedPersonAreas(const std::string &instanceId) const;

  /**
   * @brief Get object left areas
   * @param instanceId Instance ID
   * @return JSON array of object left areas
   */
  Json::Value getObjectLeftAreas(const std::string &instanceId) const;

  /**
   * @brief Get object removed areas
   * @param instanceId Instance ID
   * @return JSON array of object removed areas
   */
  Json::Value getObjectRemovedAreas(const std::string &instanceId) const;

  /**
   * @brief Get fallen person areas
   * @param instanceId Instance ID
   * @return JSON array of fallen person areas
   */
  Json::Value getFallenPersonAreas(const std::string &instanceId) const;

  /**
   * @brief Get crossing lines
   * @param instanceId Instance ID
   * @return JSON array of crossing lines
   */
  Json::Value getCrossingLines(const std::string &instanceId) const;

  /**
   * @brief Get counting lines
   * @param instanceId Instance ID
   * @return JSON array of counting lines
   */
  Json::Value getCountingLines(const std::string &instanceId) const;

  /**
   * @brief Get tailgating lines
   * @param instanceId Instance ID
   * @return JSON array of tailgating lines
   */
  Json::Value getTailgatingLines(const std::string &instanceId) const;

private:
  static AreaManager *area_manager_;
  SecuRTLineManager *line_manager_ = nullptr;
};

