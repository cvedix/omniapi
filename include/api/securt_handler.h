#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

using namespace drogon;

// Forward declarations
class SecuRTInstanceManager;
class AnalyticsEntitiesManager;
class SecuRTFeatureManager;
class ExclusionAreaManager;
class IInstanceManager;

/**
 * @brief SecuRT Instance Handler
 *
 * Handles SecuRT instance management endpoints.
 *
 * Endpoints:
 * - POST /v1/securt/instance - Create a new SecuRT instance
 * - PUT /v1/securt/instance/{instanceId} - Create SecuRT instance with ID
 * - PATCH /v1/securt/instance/{instanceId} - Update SecuRT instance
 * - DELETE /v1/securt/instance/{instanceId} - Delete SecuRT instance
 * - GET /v1/securt/instance/{instanceId}/stats - Get instance statistics
 * - GET /v1/securt/instance/{instanceId}/analytics_entities - Get analytics entities
 */
class SecuRTHandler : public drogon::HttpController<SecuRTHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(SecuRTHandler::createInstance, "/v1/securt/instance", Post);
  ADD_METHOD_TO(SecuRTHandler::createInstanceWithId,
                "/v1/securt/instance/{instanceId}", Put);
  ADD_METHOD_TO(SecuRTHandler::updateInstance,
                "/v1/securt/instance/{instanceId}", Patch);
  ADD_METHOD_TO(SecuRTHandler::deleteInstance,
                "/v1/securt/instance/{instanceId}", Delete);
  ADD_METHOD_TO(SecuRTHandler::getInstanceStats,
                "/v1/securt/instance/{instanceId}/stats", Get);
  ADD_METHOD_TO(SecuRTHandler::getAnalyticsEntities,
                "/v1/securt/instance/{instanceId}/analytics_entities", Get);
  // Input/Output endpoints
  ADD_METHOD_TO(SecuRTHandler::setInput,
                "/v1/securt/instance/{instanceId}/input", Post);
  ADD_METHOD_TO(SecuRTHandler::setOutput,
                "/v1/securt/instance/{instanceId}/output", Post);
  // Advanced features endpoints
  ADD_METHOD_TO(SecuRTHandler::setMotionArea,
                "/v1/securt/instance/{instanceId}/motion_area", Post);
  ADD_METHOD_TO(SecuRTHandler::setFeatureExtraction,
                "/v1/securt/instance/{instanceId}/feature_extraction", Post);
  ADD_METHOD_TO(SecuRTHandler::getFeatureExtraction,
                "/v1/securt/instance/{instanceId}/feature_extraction", Get);
  ADD_METHOD_TO(SecuRTHandler::setAttributesExtraction,
                "/v1/securt/instance/{instanceId}/attributes_extraction", Post);
  ADD_METHOD_TO(SecuRTHandler::getAttributesExtraction,
                "/v1/securt/instance/{instanceId}/attributes_extraction", Get);
  ADD_METHOD_TO(SecuRTHandler::setPerformanceProfile,
                "/v1/securt/instance/{instanceId}/performance_profile", Post);
  ADD_METHOD_TO(SecuRTHandler::getPerformanceProfile,
                "/v1/securt/instance/{instanceId}/performance_profile", Get);
  ADD_METHOD_TO(SecuRTHandler::setFaceDetection,
                "/v1/securt/instance/{instanceId}/face_detection", Post);
  ADD_METHOD_TO(SecuRTHandler::setLPR,
                "/v1/securt/instance/{instanceId}/lpr", Post);
  ADD_METHOD_TO(SecuRTHandler::getLPR,
                "/v1/securt/instance/{instanceId}/lpr", Get);
  ADD_METHOD_TO(SecuRTHandler::setPIP,
                "/v1/securt/instance/{instanceId}/pip", Post);
  ADD_METHOD_TO(SecuRTHandler::getPIP,
                "/v1/securt/instance/{instanceId}/pip", Get);
  ADD_METHOD_TO(SecuRTHandler::setSurrenderDetection,
                "/v1/securt/experimental/instance/{instanceId}/surrender_detection", Post);
  ADD_METHOD_TO(SecuRTHandler::getSurrenderDetection,
                "/v1/securt/experimental/instance/{instanceId}/surrender_detection", Get);
  ADD_METHOD_TO(SecuRTHandler::setMaskingAreas,
                "/v1/securt/instance/{instanceId}/masking_areas", Post);
  ADD_METHOD_TO(SecuRTHandler::addExclusionArea,
                "/v1/securt/instance/{instanceId}/exclusion_areas", Post);
  ADD_METHOD_TO(SecuRTHandler::getExclusionAreas,
                "/v1/securt/instance/{instanceId}/exclusion_areas", Get);
  ADD_METHOD_TO(SecuRTHandler::deleteExclusionAreas,
                "/v1/securt/instance/{instanceId}/exclusion_areas", Delete);
  // CORS options for new endpoints
  ADD_METHOD_TO(SecuRTHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/motion_area", Options);
  ADD_METHOD_TO(SecuRTHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/feature_extraction", Options);
  ADD_METHOD_TO(SecuRTHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/attributes_extraction", Options);
  ADD_METHOD_TO(SecuRTHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/performance_profile", Options);
  ADD_METHOD_TO(SecuRTHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/face_detection", Options);
  ADD_METHOD_TO(SecuRTHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/lpr", Options);
  ADD_METHOD_TO(SecuRTHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/pip", Options);
  ADD_METHOD_TO(SecuRTHandler::handleOptions,
                "/v1/securt/experimental/instance/{instanceId}/surrender_detection", Options);
  ADD_METHOD_TO(SecuRTHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/masking_areas", Options);
  ADD_METHOD_TO(SecuRTHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/exclusion_areas", Options);
  ADD_METHOD_TO(SecuRTHandler::handleOptions, "/v1/securt/instance", Options);
  ADD_METHOD_TO(SecuRTHandler::handleOptions,
                "/v1/securt/instance/{instanceId}", Options);
  ADD_METHOD_TO(SecuRTHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/stats", Options);
  ADD_METHOD_TO(SecuRTHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/analytics_entities", Options);
  ADD_METHOD_TO(SecuRTHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/input", Options);
  ADD_METHOD_TO(SecuRTHandler::handleOptions,
                "/v1/securt/instance/{instanceId}/output", Options);
  METHOD_LIST_END

  /**
   * @brief Handle POST /v1/securt/instance
   * Creates a new SecuRT instance
   */
  void createInstance(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle PUT /v1/securt/instance/{instanceId}
   * Creates a SecuRT instance with specific ID
   */
  void createInstanceWithId(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle PATCH /v1/securt/instance/{instanceId}
   * Updates a SecuRT instance
   */
  void updateInstance(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle DELETE /v1/securt/instance/{instanceId}
   * Deletes a SecuRT instance
   */
  void deleteInstance(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/securt/instance/{instanceId}/stats
   * Gets statistics for a SecuRT instance
   */
  void getInstanceStats(const HttpRequestPtr &req,
                        std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/securt/instance/{instanceId}/analytics_entities
   * Gets all analytics entities (areas and lines) for a SecuRT instance
   */
  void getAnalyticsEntities(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle OPTIONS request for CORS preflight
   */
  void handleOptions(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Set instance manager (dependency injection)
   */
  static void setInstanceManager(SecuRTInstanceManager *manager);

  /**
   * @brief Set analytics entities manager (dependency injection)
   */
  static void setAnalyticsEntitiesManager(AnalyticsEntitiesManager *manager);

  /**
   * @brief Set feature manager (dependency injection)
   */
  static void setFeatureManager(SecuRTFeatureManager *manager);

  /**
   * @brief Set exclusion area manager (dependency injection)
   */
  static void setExclusionAreaManager(ExclusionAreaManager *manager);

  /**
   * @brief Set core instance manager (dependency injection)
   */
  static void setCoreInstanceManager(IInstanceManager *manager);

  // Advanced features handlers
  void setMotionArea(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);
  void setFeatureExtraction(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);
  void getFeatureExtraction(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);
  void setAttributesExtraction(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);
  void getAttributesExtraction(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);
  void setPerformanceProfile(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);
  void getPerformanceProfile(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);
  void setFaceDetection(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);
  void setLPR(const HttpRequestPtr &req,
              std::function<void(const HttpResponsePtr &)> &&callback);
  void getLPR(const HttpRequestPtr &req,
              std::function<void(const HttpResponsePtr &)> &&callback);
  void setPIP(const HttpRequestPtr &req,
              std::function<void(const HttpResponsePtr &)> &&callback);
  void getPIP(const HttpRequestPtr &req,
              std::function<void(const HttpResponsePtr &)> &&callback);
  void setSurrenderDetection(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);
  void getSurrenderDetection(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);
  void setMaskingAreas(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);
  void addExclusionArea(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);
  void getExclusionAreas(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);
  void deleteExclusionAreas(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  // Input/Output handlers
  void setInput(const HttpRequestPtr &req,
                std::function<void(const HttpResponsePtr &)> &&callback);
  void setOutput(const HttpRequestPtr &req,
                 std::function<void(const HttpResponsePtr &)> &&callback);

private:
  static SecuRTInstanceManager *instance_manager_;
  static AnalyticsEntitiesManager *analytics_entities_manager_;
  static SecuRTFeatureManager *feature_manager_;
  static ExclusionAreaManager *exclusion_area_manager_;
  static IInstanceManager *core_instance_manager_;

  /**
   * @brief Extract instance ID from request path
   */
  std::string extractInstanceId(const HttpRequestPtr &req) const;

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

