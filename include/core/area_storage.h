#pragma once

#include "core/area_types_specific.h"
#include "core/uuid_generator.h"
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Area type enum for storage
 */
enum class AreaType {
  Crossing,
  Intrusion,
  Loitering,
  Crowding,
  Occupancy,
  CrowdEstimation,
  Dwelling,
  ArmedPerson,
  ObjectLeft,
  ObjectRemoved,
  FallenPerson,
  VehicleGuard,  // Experimental
  FaceCovered,   // Experimental
  ObjectEnterExit // BA Area Enter/Exit
};

/**
 * @brief Convert AreaType to string
 */
inline std::string areaTypeToString(AreaType type) {
  switch (type) {
  case AreaType::Crossing:
    return "crossing";
  case AreaType::Intrusion:
    return "intrusion";
  case AreaType::Loitering:
    return "loitering";
  case AreaType::Crowding:
    return "crowding";
  case AreaType::Occupancy:
    return "occupancy";
  case AreaType::CrowdEstimation:
    return "crowdEstimation";
  case AreaType::Dwelling:
    return "dwelling";
  case AreaType::ArmedPerson:
    return "armedPerson";
  case AreaType::ObjectLeft:
    return "objectLeft";
  case AreaType::ObjectRemoved:
    return "objectRemoved";
  case AreaType::FallenPerson:
    return "fallenPerson";
  case AreaType::VehicleGuard:
    return "vehicleGuard";
  case AreaType::FaceCovered:
    return "faceCovered";
  case AreaType::ObjectEnterExit:
    return "objectEnterExit";
  default:
    return "unknown";
  }
}

/**
 * @brief Convert string to AreaType
 */
inline AreaType stringToAreaType(const std::string &str) {
  if (str == "crossing")
    return AreaType::Crossing;
  if (str == "intrusion")
    return AreaType::Intrusion;
  if (str == "loitering")
    return AreaType::Loitering;
  if (str == "crowding")
    return AreaType::Crowding;
  if (str == "occupancy")
    return AreaType::Occupancy;
  if (str == "crowdEstimation")
    return AreaType::CrowdEstimation;
  if (str == "dwelling")
    return AreaType::Dwelling;
  if (str == "armedPerson")
    return AreaType::ArmedPerson;
  if (str == "objectLeft")
    return AreaType::ObjectLeft;
  if (str == "objectRemoved")
    return AreaType::ObjectRemoved;
  if (str == "fallenPerson")
    return AreaType::FallenPerson;
  if (str == "vehicleGuard")
    return AreaType::VehicleGuard;
  if (str == "faceCovered")
    return AreaType::FaceCovered;
  if (str == "objectEnterExit")
    return AreaType::ObjectEnterExit;
  return AreaType::Crossing; // Default
}

/**
 * @brief Base area pointer type
 * Using void* for type erasure, actual types handled by manager
 */
using AreaPtr = std::shared_ptr<void>;

/**
 * @brief Area Storage
 *
 * Thread-safe storage for areas per instance, grouped by type.
 * Storage structure: map<instanceId, map<areaType, vector<AreaPtr>>>
 */
class AreaStorage {
public:
  /**
   * @brief Create a crossing area
   * @param instanceId Instance ID
   * @param areaId Area ID (if empty, will be generated)
   * @param write Area write data
   * @return Area ID if successful, empty string otherwise
   */
  std::string createCrossingArea(const std::string &instanceId,
                                  const std::string &areaId,
                                  const CrossingAreaWrite &write);

  /**
   * @brief Create an intrusion area
   */
  std::string createIntrusionArea(const std::string &instanceId,
                                   const std::string &areaId,
                                   const IntrusionAreaWrite &write);

  /**
   * @brief Create a loitering area
   */
  std::string createLoiteringArea(const std::string &instanceId,
                                   const std::string &areaId,
                                   const LoiteringAreaWrite &write);

  /**
   * @brief Create a crowding area
   */
  std::string createCrowdingArea(const std::string &instanceId,
                                  const std::string &areaId,
                                  const CrowdingAreaWrite &write);

  /**
   * @brief Create an occupancy area
   */
  std::string createOccupancyArea(const std::string &instanceId,
                                   const std::string &areaId,
                                   const OccupancyAreaWrite &write);

  /**
   * @brief Create a crowd estimation area
   */
  std::string createCrowdEstimationArea(const std::string &instanceId,
                                        const std::string &areaId,
                                        const CrowdEstimationAreaWrite &write);

  /**
   * @brief Create a dwelling area
   */
  std::string createDwellingArea(const std::string &instanceId,
                                  const std::string &areaId,
                                  const DwellingAreaWrite &write);

  /**
   * @brief Create an armed person area
   */
  std::string createArmedPersonArea(const std::string &instanceId,
                                     const std::string &areaId,
                                     const ArmedPersonAreaWrite &write);

  /**
   * @brief Create an object left area
   */
  std::string createObjectLeftArea(const std::string &instanceId,
                                    const std::string &areaId,
                                    const ObjectLeftAreaWrite &write);

  /**
   * @brief Create an object removed area
   */
  std::string createObjectRemovedArea(const std::string &instanceId,
                                       const std::string &areaId,
                                       const ObjectRemovedAreaWrite &write);

  /**
   * @brief Create a fallen person area
   */
  std::string createFallenPersonArea(const std::string &instanceId,
                                     const std::string &areaId,
                                     const FallenPersonAreaWrite &write);

  /**
   * @brief Create a vehicle guard area (experimental)
   */
  std::string createVehicleGuardArea(const std::string &instanceId,
                                     const std::string &areaId,
                                     const VehicleGuardAreaWrite &write);

  /**
   * @brief Create a face covered area (experimental)
   */
  std::string createFaceCoveredArea(const std::string &instanceId,
                                     const std::string &areaId,
                                     const FaceCoveredAreaWrite &write);

  /**
   * @brief Create an object enter/exit area (for BA Area Enter/Exit solution)
   */
  std::string createObjectEnterExitArea(const std::string &instanceId,
                                         const std::string &areaId,
                                         const ObjectEnterExitAreaWrite &write);

  /**
   * @brief Get all areas for an instance, grouped by type
   * @param instanceId Instance ID
   * @return Map of area type to vector of areas (as JSON)
   */
  std::unordered_map<std::string, std::vector<Json::Value>>
  getAllAreas(const std::string &instanceId) const;

  /**
   * @brief Get areas of specific type for an instance
   * @param instanceId Instance ID
   * @param type Area type
   * @return Vector of areas (as JSON)
   */
  std::vector<Json::Value> getAreasByType(const std::string &instanceId,
                                           AreaType type) const;

  /**
   * @brief Get a specific area by ID
   * @param instanceId Instance ID
   * @param areaId Area ID
   * @return Area as JSON, or null if not found
   */
  Json::Value getArea(const std::string &instanceId,
                      const std::string &areaId) const;

  /**
   * @brief Delete a specific area
   * @param instanceId Instance ID
   * @param areaId Area ID
   * @return true if deleted, false if not found
   */
  bool deleteArea(const std::string &instanceId, const std::string &areaId);

  /**
   * @brief Delete all areas for an instance
   * @param instanceId Instance ID
   * @return true if successful
   */
  bool deleteAllAreas(const std::string &instanceId);

  /**
   * @brief Check if area exists
   * @param instanceId Instance ID
   * @param areaId Area ID
   * @return true if exists
   */
  bool hasArea(const std::string &instanceId,
               const std::string &areaId) const;

  /**
   * @brief Get area count for an instance
   * @param instanceId Instance ID
   * @return Total number of areas
   */
  size_t getAreaCount(const std::string &instanceId) const;

private:
  mutable std::shared_mutex mutex_;
  // Storage: map<instanceId, map<areaType, vector<AreaPtr>>>
  std::unordered_map<std::string,
                     std::unordered_map<AreaType, std::vector<AreaPtr>>>
      storage_;

  /**
   * @brief Helper to find area by ID across all types
   */
  std::pair<AreaType, size_t>
  findAreaIndex(const std::string &instanceId,
                const std::string &areaId) const;

  /**
   * @brief Helper to convert area to JSON based on type
   */
  Json::Value areaToJson(AreaType type, const AreaPtr &area) const;
};

