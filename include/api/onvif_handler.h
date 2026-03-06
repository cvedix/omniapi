#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

using namespace drogon;

/**
 * @brief ONVIF API Handler
 *
 * Handles ONVIF camera discovery and management operations.
 *
 * Endpoints:
 * - POST /v1/onvif/discover - Discover ONVIF cameras
 * - GET /v1/onvif/cameras - Get all discovered cameras
 * - GET /v1/onvif/streams/{cameraid} - Get streams for a camera
 * - POST /v1/onvif/camera/{cameraid}/credentials - Set camera credentials
 */
class ONVIFHandler : public drogon::HttpController<ONVIFHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(ONVIFHandler::discoverCameras, "/v1/onvif/discover", Post);
  ADD_METHOD_TO(ONVIFHandler::getCameras, "/v1/onvif/cameras", Get);
  ADD_METHOD_TO(ONVIFHandler::getStreams, "/v1/onvif/streams/{cameraid}", Get);
  ADD_METHOD_TO(ONVIFHandler::setCredentials,
                "/v1/onvif/camera/{cameraid}/credentials", Post);
  ADD_METHOD_TO(ONVIFHandler::handleOptions, "/v1/onvif/discover", Options);
  ADD_METHOD_TO(ONVIFHandler::handleOptions, "/v1/onvif/cameras", Options);
  ADD_METHOD_TO(ONVIFHandler::handleOptions, "/v1/onvif/streams/{cameraid}",
                Options);
  ADD_METHOD_TO(ONVIFHandler::handleOptions,
                "/v1/onvif/camera/{cameraid}/credentials", Options);
  METHOD_LIST_END

  /**
   * @brief Handle POST /v1/onvif/discover
   * Discover ONVIF cameras on the network
   */
  void discoverCameras(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/onvif/cameras
   * Get all discovered ONVIF cameras
   */
  void getCameras(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/onvif/streams/{cameraid}
   * Get streams for a camera
   */
  void getStreams(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/onvif/camera/{cameraid}/credentials
   * Set credentials for a camera
   */
  void setCredentials(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle OPTIONS request for CORS preflight
   */
  void handleOptions(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

private:
  /**
   * @brief Create success response
   */
  HttpResponsePtr createSuccessResponse(const Json::Value &data,
                                       int statusCode = 200) const;

  /**
   * @brief Create error response
   */
  HttpResponsePtr createErrorResponse(int statusCode, const std::string &error,
                                      const std::string &message) const;

  /**
   * @brief Convert ONVIFCamera to JSON
   */
  Json::Value cameraToJson(const struct ONVIFCamera &camera) const;

  /**
   * @brief Convert ONVIFStream to JSON
   */
  Json::Value streamToJson(const struct ONVIFStream &stream) const;

  /**
   * @brief Validate IP address format using regex
   * @param ip IP address string to validate
   * @return true if valid IP format (0-255.0-255.0-255.0-255)
   */
  bool isValidIPFormat(const std::string &ip) const;

  /**
   * @brief Extract and validate IP from camera ID
   * @param cameraId Camera ID (may be IP or UUID)
   * @return Valid IP address if found, empty string otherwise
   */
  std::string extractIPFromCameraId(const std::string &cameraId) const;

  /**
   * @brief Find similar IP address in camera list (for typo correction)
   * @param cameraId Camera ID with potential typo
   * @param allCameras List of all cameras
   * @return Suggested camera ID if similar IP found, empty string otherwise
   */
  std::string findSimilarIP(const std::string &cameraId,
                             const std::vector<ONVIFCamera> &allCameras) const;
};

