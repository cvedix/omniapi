#pragma once

#include <drogon/HttpController.h>
#include <json/json.h>
#include <memory>
#include <string>
#include <cvedix/nodes/ba/cvedix_ba_area_jam_node.h>
#include <cvedix/objects/shapes/cvedix_point.h>

using namespace drogon;

/**
 * @brief Jams Management Handler
 *
 * Handles jam zones management for ba_jam instances.
 *
 * Endpoints:
 * - GET /v1/core/instance/{instanceId}/jams - Get all jam zones
 * - POST /v1/core/instance/{instanceId}/jams - Create a new jam zone
 * - DELETE /v1/core/instance/{instanceId}/jams - Delete all jam zones
 * - GET /v1/core/instance/{instanceId}/jams/{jamId} - Get a specific jam
 * zone
 * - PUT /v1/core/instance/{instanceId}/jams/{jamId} - Update a specific
 * jam zone
 * - DELETE /v1/core/instance/{instanceId}/jams/{jamId} - Delete a specific
 * jam zone
 */
class JamsHandler : public drogon::HttpController<JamsHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(JamsHandler::getAllJams, "/v1/core/instance/{instanceId}/jams", Get);
  ADD_METHOD_TO(JamsHandler::createJam, "/v1/core/instance/{instanceId}/jams", Post);
  ADD_METHOD_TO(JamsHandler::deleteAllJams, "/v1/core/instance/{instanceId}/jams", Delete);
  ADD_METHOD_TO(JamsHandler::getJam, "/v1/core/instance/{instanceId}/jams/{jamId}", Get);
  ADD_METHOD_TO(JamsHandler::updateJam, "/v1/core/instance/{instanceId}/jams/{jamId}", Put);
  ADD_METHOD_TO(JamsHandler::deleteJam, "/v1/core/instance/{instanceId}/jams/{jamId}", Delete);
  ADD_METHOD_TO(JamsHandler::batchUpdateJams, "/v1/core/instance/{instanceId}/jams/batch", Post);
  ADD_METHOD_TO(JamsHandler::handleOptions, "/v1/core/instance/{instanceId}/jams", Options);
  ADD_METHOD_TO(JamsHandler::handleOptions, "/v1/core/instance/{instanceId}/jams/{jamId}", Options);
  ADD_METHOD_TO(JamsHandler::handleOptions, "/v1/core/instance/{instanceId}/jams/batch", Options);
  METHOD_LIST_END

  void getAllJams(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
  void createJam(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
  void deleteAllJams(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
  void getJam(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
  void updateJam(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
  void deleteJam(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
  void batchUpdateJams(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
  void handleOptions(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);

  static void setInstanceManager(class IInstanceManager *manager);

private:
  static class IInstanceManager *instance_manager_;

  std::string extractInstanceId(const HttpRequestPtr &req) const;
  std::string extractJamId(const HttpRequestPtr &req) const;

  HttpResponsePtr createErrorResponse(int statusCode, const std::string &error, const std::string &message = "") const;
  HttpResponsePtr createSuccessResponse(const Json::Value &data, int statusCode = 200) const;

  Json::Value loadJamsFromConfig(const std::string &instanceId) const;
  bool saveJamsToConfig(const std::string &instanceId, const Json::Value &jams) const;

  bool validateROI(const Json::Value &roi, std::string &error) const;

  bool restartInstanceForJamUpdate(const std::string &instanceId) const;
  std::shared_ptr<cvedix_nodes::cvedix_ba_area_jam_node>
  findBAJamNode(const std::string &instanceId) const;

  std::map<int, std::vector<cvedix_objects::cvedix_point>> parseJamsFromJson(const Json::Value &jamsArray) const;

  // Validate jam parameters for a single jam object. Returns true if valid and fills
  // `error` with a short description when invalid.
  bool validateJamParameters(const Json::Value &jam, std::string &error) const;


  bool updateJamsRuntime(const std::string &instanceId, const Json::Value &jamsArray) const;
};