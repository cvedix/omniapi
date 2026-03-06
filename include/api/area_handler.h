#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

using namespace drogon;

// Forward declarations
class AreaManager;

/**
 * @brief Area Handler
 *
 * Handles SecuRT area management endpoints for all area types.
 *
 * Endpoints pattern:
 * - POST /v1/securt/instance/{instanceId}/area/{areaType} - Create area
 * - PUT /v1/securt/instance/{instanceId}/area/{areaType}/{areaId} - Create area with ID
 * - GET /v1/securt/instance/{instanceId}/areas - Get all areas
 * - DELETE /v1/securt/instance/{instanceId}/areas - Delete all areas
 * - DELETE /v1/securt/instance/{instanceId}/area/{areaId} - Delete area
 */
class AreaHandler : public drogon::HttpController<AreaHandler> {
public:
  METHOD_LIST_BEGIN
  // Standard Areas - POST (create)
  ADD_METHOD_TO(AreaHandler::createCrossingArea,
                "/v1/securt/instance/{instanceId}/area/crossing", Post);
  ADD_METHOD_TO(AreaHandler::createIntrusionArea,
                "/v1/securt/instance/{instanceId}/area/intrusion", Post);
  ADD_METHOD_TO(AreaHandler::createLoiteringArea,
                "/v1/securt/instance/{instanceId}/area/loitering", Post);
  ADD_METHOD_TO(AreaHandler::createCrowdingArea,
                "/v1/securt/instance/{instanceId}/area/crowding", Post);
  ADD_METHOD_TO(AreaHandler::createOccupancyArea,
                "/v1/securt/instance/{instanceId}/area/occupancy", Post);
  ADD_METHOD_TO(AreaHandler::createCrowdEstimationArea,
                "/v1/securt/instance/{instanceId}/area/crowdEstimation", Post);
  ADD_METHOD_TO(AreaHandler::createDwellingArea,
                "/v1/securt/instance/{instanceId}/area/dwelling", Post);
  ADD_METHOD_TO(AreaHandler::createArmedPersonArea,
                "/v1/securt/instance/{instanceId}/area/armedPerson", Post);
  ADD_METHOD_TO(AreaHandler::createObjectLeftArea,
                "/v1/securt/instance/{instanceId}/area/objectLeft", Post);
  ADD_METHOD_TO(AreaHandler::createObjectRemovedArea,
                "/v1/securt/instance/{instanceId}/area/objectRemoved", Post);
  ADD_METHOD_TO(AreaHandler::createFallenPersonArea,
                "/v1/securt/instance/{instanceId}/area/fallenPerson", Post);
  ADD_METHOD_TO(AreaHandler::createObjectEnterExitArea,
                "/v1/securt/instance/{instanceId}/area/objectEnterExit", Post);

  // Standard Areas - PUT (create with ID)
  ADD_METHOD_TO(AreaHandler::createCrossingAreaWithId,
                "/v1/securt/instance/{instanceId}/area/crossing/{areaId}", Put);
  ADD_METHOD_TO(AreaHandler::createIntrusionAreaWithId,
                "/v1/securt/instance/{instanceId}/area/intrusion/{areaId}", Put);
  ADD_METHOD_TO(AreaHandler::createLoiteringAreaWithId,
                "/v1/securt/instance/{instanceId}/area/loitering/{areaId}", Put);
  ADD_METHOD_TO(AreaHandler::createCrowdingAreaWithId,
                "/v1/securt/instance/{instanceId}/area/crowding/{areaId}", Put);
  ADD_METHOD_TO(AreaHandler::createOccupancyAreaWithId,
                "/v1/securt/instance/{instanceId}/area/occupancy/{areaId}", Put);
  ADD_METHOD_TO(AreaHandler::createCrowdEstimationAreaWithId,
                "/v1/securt/instance/{instanceId}/area/crowdEstimation/{areaId}", Put);
  ADD_METHOD_TO(AreaHandler::createDwellingAreaWithId,
                "/v1/securt/instance/{instanceId}/area/dwelling/{areaId}", Put);
  ADD_METHOD_TO(AreaHandler::createArmedPersonAreaWithId,
                "/v1/securt/instance/{instanceId}/area/armedPerson/{areaId}", Put);
  ADD_METHOD_TO(AreaHandler::createObjectLeftAreaWithId,
                "/v1/securt/instance/{instanceId}/area/objectLeft/{areaId}", Put);
  ADD_METHOD_TO(AreaHandler::createObjectRemovedAreaWithId,
                "/v1/securt/instance/{instanceId}/area/objectRemoved/{areaId}", Put);
  ADD_METHOD_TO(AreaHandler::createFallenPersonAreaWithId,
                "/v1/securt/instance/{instanceId}/area/fallenPerson/{areaId}", Put);
  ADD_METHOD_TO(AreaHandler::createObjectEnterExitAreaWithId,
                "/v1/securt/instance/{instanceId}/area/objectEnterExit/{areaId}", Put);

  // Experimental Areas - POST
  ADD_METHOD_TO(AreaHandler::createVehicleGuardArea,
                "/v1/securt/instance/{instanceId}/area/vehicleGuard", Post);
  ADD_METHOD_TO(AreaHandler::createFaceCoveredArea,
                "/v1/securt/instance/{instanceId}/area/faceCovered", Post);

  // Experimental Areas - PUT
  ADD_METHOD_TO(AreaHandler::createVehicleGuardAreaWithId,
                "/v1/securt/instance/{instanceId}/area/vehicleGuard/{areaId}", Put);
  ADD_METHOD_TO(AreaHandler::createFaceCoveredAreaWithId,
                "/v1/securt/instance/{instanceId}/area/faceCovered/{areaId}", Put);

  // Common endpoints
  ADD_METHOD_TO(AreaHandler::getAllAreas,
                "/v1/securt/instance/{instanceId}/areas", Get);
  ADD_METHOD_TO(AreaHandler::deleteAllAreas,
                "/v1/securt/instance/{instanceId}/areas", Delete);
  ADD_METHOD_TO(AreaHandler::deleteArea,
                "/v1/securt/instance/{instanceId}/area/{areaId}", Delete);

  // CORS options
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/area/crossing", Options);
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/area/intrusion", Options);
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/area/loitering", Options);
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/area/crowding", Options);
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/area/occupancy", Options);
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/area/crowdEstimation", Options);
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/area/dwelling", Options);
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/area/armedPerson", Options);
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/area/objectLeft", Options);
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/area/objectRemoved", Options);
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/area/fallenPerson", Options);
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/area/vehicleGuard", Options);
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/area/faceCovered", Options);
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/area/objectEnterExit/{areaId}", Options);
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/areas", Options);
  ADD_METHOD_TO(AreaHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/area/{areaId}", Options);
  METHOD_LIST_END

  // Standard Areas - POST handlers
  void createCrossingArea(const HttpRequestPtr &req,
                          std::function<void(const HttpResponsePtr &)> &&callback);
  void createIntrusionArea(const HttpRequestPtr &req,
                           std::function<void(const HttpResponsePtr &)> &&callback);
  void createLoiteringArea(const HttpRequestPtr &req,
                           std::function<void(const HttpResponsePtr &)> &&callback);
  void createCrowdingArea(const HttpRequestPtr &req,
                          std::function<void(const HttpResponsePtr &)> &&callback);
  void createOccupancyArea(const HttpRequestPtr &req,
                           std::function<void(const HttpResponsePtr &)> &&callback);
  void createCrowdEstimationArea(const HttpRequestPtr &req,
                                 std::function<void(const HttpResponsePtr &)> &&callback);
  void createDwellingArea(const HttpRequestPtr &req,
                         std::function<void(const HttpResponsePtr &)> &&callback);
  void createArmedPersonArea(const HttpRequestPtr &req,
                            std::function<void(const HttpResponsePtr &)> &&callback);
  void createObjectLeftArea(const HttpRequestPtr &req,
                           std::function<void(const HttpResponsePtr &)> &&callback);
  void createObjectRemovedArea(const HttpRequestPtr &req,
                               std::function<void(const HttpResponsePtr &)> &&callback);
  void createFallenPersonArea(const HttpRequestPtr &req,
                             std::function<void(const HttpResponsePtr &)> &&callback);
  void createObjectEnterExitArea(const HttpRequestPtr &req,
                                 std::function<void(const HttpResponsePtr &)> &&callback);

  // Standard Areas - PUT handlers
  void createCrossingAreaWithId(const HttpRequestPtr &req,
                                 std::function<void(const HttpResponsePtr &)> &&callback);
  void createIntrusionAreaWithId(const HttpRequestPtr &req,
                                 std::function<void(const HttpResponsePtr &)> &&callback);
  void createLoiteringAreaWithId(const HttpRequestPtr &req,
                                 std::function<void(const HttpResponsePtr &)> &&callback);
  void createCrowdingAreaWithId(const HttpRequestPtr &req,
                                std::function<void(const HttpResponsePtr &)> &&callback);
  void createOccupancyAreaWithId(const HttpRequestPtr &req,
                                 std::function<void(const HttpResponsePtr &)> &&callback);
  void createCrowdEstimationAreaWithId(const HttpRequestPtr &req,
                                       std::function<void(const HttpResponsePtr &)> &&callback);
  void createDwellingAreaWithId(const HttpRequestPtr &req,
                               std::function<void(const HttpResponsePtr &)> &&callback);
  void createArmedPersonAreaWithId(const HttpRequestPtr &req,
                                  std::function<void(const HttpResponsePtr &)> &&callback);
  void createObjectLeftAreaWithId(const HttpRequestPtr &req,
                                  std::function<void(const HttpResponsePtr &)> &&callback);
  void createObjectRemovedAreaWithId(const HttpRequestPtr &req,
                                     std::function<void(const HttpResponsePtr &)> &&callback);
  void createFallenPersonAreaWithId(const HttpRequestPtr &req,
                                    std::function<void(const HttpResponsePtr &)> &&callback);
  void createObjectEnterExitAreaWithId(const HttpRequestPtr &req,
                                       std::function<void(const HttpResponsePtr &)> &&callback);

  // Experimental Areas
  void createVehicleGuardArea(const HttpRequestPtr &req,
                              std::function<void(const HttpResponsePtr &)> &&callback);
  void createFaceCoveredArea(const HttpRequestPtr &req,
                             std::function<void(const HttpResponsePtr &)> &&callback);
  void createVehicleGuardAreaWithId(const HttpRequestPtr &req,
                                    std::function<void(const HttpResponsePtr &)> &&callback);
  void createFaceCoveredAreaWithId(const HttpRequestPtr &req,
                                   std::function<void(const HttpResponsePtr &)> &&callback);

  // Common handlers
  void getAllAreas(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);
  void deleteAllAreas(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);
  void deleteArea(const HttpRequestPtr &req,
                 std::function<void(const HttpResponsePtr &)> &&callback);

  // CORS
  void handleOptions(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Set area manager (dependency injection)
   */
  static void setAreaManager(AreaManager *manager);

private:
  static AreaManager *area_manager_;

  /**
   * @brief Extract instance ID from request path
   */
  std::string extractInstanceId(const HttpRequestPtr &req) const;

  /**
   * @brief Extract area ID from request path
   */
  std::string extractAreaId(const HttpRequestPtr &req) const;

  /**
   * @brief Create error response
   */
  HttpResponsePtr createErrorResponse(int statusCode, const std::string &error,
                                      const std::string &message = "") const;

  /**
   * @brief Create success JSON response with CORS headers
   */
  HttpResponsePtr createSuccessResponse(const Json::Value &data,
                                        int statusCode = 200) const;
};

