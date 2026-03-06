#pragma once

#include "core/area_storage.h"
#include "core/area_types_specific.h"
#include "core/securt_instance_manager.h"
#include <string>

/**
 * @brief Area Manager
 *
 * Manages area CRUD operations with validation.
 * Integrates with AreaStorage and SecuRTInstanceManager.
 */
class AreaManager {
public:
  /**
   * @brief Constructor
   * @param storage Area storage instance
   * @param instanceManager SecuRT instance manager (for validation)
   */
  explicit AreaManager(AreaStorage *storage,
                       SecuRTInstanceManager *instanceManager);

  // ========================================================================
  // Create Area Methods (POST)
  // ========================================================================

  /**
   * @brief Create crossing area
   * @param instanceId Instance ID
   * @param write Area write data
   * @return Area ID if successful, empty string otherwise
   */
  std::string createCrossingArea(const std::string &instanceId,
                                  const CrossingAreaWrite &write);

  std::string createIntrusionArea(const std::string &instanceId,
                                   const IntrusionAreaWrite &write);
  std::string createLoiteringArea(const std::string &instanceId,
                                   const LoiteringAreaWrite &write);
  std::string createCrowdingArea(const std::string &instanceId,
                                  const CrowdingAreaWrite &write);
  std::string createOccupancyArea(const std::string &instanceId,
                                   const OccupancyAreaWrite &write);
  std::string createCrowdEstimationArea(const std::string &instanceId,
                                        const CrowdEstimationAreaWrite &write);
  std::string createDwellingArea(const std::string &instanceId,
                                  const DwellingAreaWrite &write);
  std::string createArmedPersonArea(const std::string &instanceId,
                                     const ArmedPersonAreaWrite &write);
  std::string createObjectLeftArea(const std::string &instanceId,
                                    const ObjectLeftAreaWrite &write);
  std::string createObjectRemovedArea(const std::string &instanceId,
                                       const ObjectRemovedAreaWrite &write);
  std::string createFallenPersonArea(const std::string &instanceId,
                                     const FallenPersonAreaWrite &write);
  std::string createVehicleGuardArea(const std::string &instanceId,
                                     const VehicleGuardAreaWrite &write);
  std::string createFaceCoveredArea(const std::string &instanceId,
                                     const FaceCoveredAreaWrite &write);
  std::string createObjectEnterExitArea(const std::string &instanceId,
                                         const ObjectEnterExitAreaWrite &write);

  // ========================================================================
  // Create Area with ID Methods (PUT)
  // ========================================================================

  /**
   * @brief Create crossing area with specific ID
   * @param instanceId Instance ID
   * @param areaId Area ID
   * @param write Area write data
   * @return Area ID if successful, empty string otherwise
   */
  std::string createCrossingAreaWithId(const std::string &instanceId,
                                        const std::string &areaId,
                                        const CrossingAreaWrite &write);

  std::string createIntrusionAreaWithId(const std::string &instanceId,
                                         const std::string &areaId,
                                         const IntrusionAreaWrite &write);
  std::string createLoiteringAreaWithId(const std::string &instanceId,
                                         const std::string &areaId,
                                         const LoiteringAreaWrite &write);
  std::string createCrowdingAreaWithId(const std::string &instanceId,
                                       const std::string &areaId,
                                       const CrowdingAreaWrite &write);
  std::string createOccupancyAreaWithId(const std::string &instanceId,
                                        const std::string &areaId,
                                        const OccupancyAreaWrite &write);
  std::string createCrowdEstimationAreaWithId(
      const std::string &instanceId, const std::string &areaId,
      const CrowdEstimationAreaWrite &write);
  std::string createDwellingAreaWithId(const std::string &instanceId,
                                        const std::string &areaId,
                                        const DwellingAreaWrite &write);
  std::string createArmedPersonAreaWithId(const std::string &instanceId,
                                           const std::string &areaId,
                                           const ArmedPersonAreaWrite &write);
  std::string createObjectLeftAreaWithId(const std::string &instanceId,
                                          const std::string &areaId,
                                          const ObjectLeftAreaWrite &write);
  std::string createObjectRemovedAreaWithId(
      const std::string &instanceId, const std::string &areaId,
      const ObjectRemovedAreaWrite &write);
  std::string createFallenPersonAreaWithId(const std::string &instanceId,
                                            const std::string &areaId,
                                            const FallenPersonAreaWrite &write);
  std::string createVehicleGuardAreaWithId(const std::string &instanceId,
                                            const std::string &areaId,
                                            const VehicleGuardAreaWrite &write);
  std::string createFaceCoveredAreaWithId(const std::string &instanceId,
                                            const std::string &areaId,
                                            const FaceCoveredAreaWrite &write);
  std::string createObjectEnterExitAreaWithId(const std::string &instanceId,
                                               const std::string &areaId,
                                               const ObjectEnterExitAreaWrite &write);

  // ========================================================================
  // Get Methods
  // ========================================================================

  /**
   * @brief Get all areas for an instance, grouped by type
   * @param instanceId Instance ID
   * @return Map of area type to vector of areas (as JSON)
   */
  std::unordered_map<std::string, std::vector<Json::Value>>
  getAllAreas(const std::string &instanceId) const;

  /**
   * @brief Get a specific area by ID
   * @param instanceId Instance ID
   * @param areaId Area ID
   * @return Area as JSON, or null if not found
   */
  Json::Value getArea(const std::string &instanceId,
                      const std::string &areaId) const;

  // ========================================================================
  // Delete Methods
  // ========================================================================

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

  // ========================================================================
  // Validation
  // ========================================================================

  /**
   * @brief Validate area base fields
   * @param write Area write data
   * @return Validation error message (empty if valid)
   */
  std::string validateAreaBase(const AreaBaseWrite &write) const;

  /**
   * @brief Validate crossing area
   */
  std::string validateCrossingArea(const CrossingAreaWrite &write) const;

  /**
   * @brief Validate loitering area
   */
  std::string validateLoiteringArea(const LoiteringAreaWrite &write) const;

  /**
   * @brief Validate crowding area
   */
  std::string validateCrowdingArea(const CrowdingAreaWrite &write) const;

  /**
   * @brief Validate dwelling area
   */
  std::string validateDwellingArea(const DwellingAreaWrite &write) const;

  /**
   * @brief Validate object left area
   */
  std::string validateObjectLeftArea(const ObjectLeftAreaWrite &write) const;

  /**
   * @brief Validate object removed area
   */
  std::string validateObjectRemovedArea(const ObjectRemovedAreaWrite &write) const;

private:
  AreaStorage *storage_;
  SecuRTInstanceManager *instance_manager_;

  /**
   * @brief Validate coordinates (minimum 3 points, valid polygon)
   */
  std::string validateCoordinates(const std::vector<Coordinate> &coordinates) const;

  /**
   * @brief Validate classes (at least one class)
   */
  std::string validateClasses(const std::vector<ObjectClass> &classes) const;

  /**
   * @brief Validate color (RGBA range 0.0-1.0)
   */
  std::string validateColor(const ColorRGBA &color) const;

  /**
   * @brief Validate instance exists
   */
  bool validateInstance(const std::string &instanceId) const;
};

