#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <cvedix/nodes/ba/cvedix_ba_stop_node.h>
#include <cvedix/objects/shapes/cvedix_point.h>
#include <map>
#include <memory>
#include <string>

using namespace drogon;

class StopsHandler : public drogon::HttpController<StopsHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(StopsHandler::getAllStops,
                "/v1/core/instance/{instanceId}/stops", Get);
  ADD_METHOD_TO(StopsHandler::createStop,
                "/v1/core/instance/{instanceId}/stops", Post);
  ADD_METHOD_TO(StopsHandler::deleteAllStops,
                "/v1/core/instance/{instanceId}/stops", Delete);
  ADD_METHOD_TO(StopsHandler::getStop,
                "/v1/core/instance/{instanceId}/stops/{stopId}", Get);
  ADD_METHOD_TO(StopsHandler::updateStop,
                "/v1/core/instance/{instanceId}/stops/{stopId}", Put);
  ADD_METHOD_TO(StopsHandler::deleteStop,
                "/v1/core/instance/{instanceId}/stops/{stopId}", Delete);
  ADD_METHOD_TO(StopsHandler::batchUpdateStops,
                "/v1/core/instance/{instanceId}/stops/batch", Post);
  ADD_METHOD_TO(StopsHandler::handleOptions,
                "/v1/core/instance/{instanceId}/stops", Options);
  ADD_METHOD_TO(StopsHandler::handleOptions,
                "/v1/core/instance/{instanceId}/stops/{stopId}", Options);
  ADD_METHOD_TO(StopsHandler::handleOptions,
                "/v1/core/instance/{instanceId}/stops/batch", Options);
  METHOD_LIST_END

  void getAllStops(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

  void createStop(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

  void deleteAllStops(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);

  void getStop(const HttpRequestPtr &req,
               std::function<void(const HttpResponsePtr &)> &&callback);

  void updateStop(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

  void deleteStop(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

  void batchUpdateStops(const HttpRequestPtr &req,
                        std::function<void(const HttpResponsePtr &)> &&callback);

  void handleOptions(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  static void setInstanceManager(class IInstanceManager *manager);

private:
  static class IInstanceManager *instance_manager_;

  std::string extractInstanceId(const HttpRequestPtr &req) const;
  std::string extractStopId(const HttpRequestPtr &req) const;

  HttpResponsePtr createErrorResponse(int statusCode,
                                      const std::string &error,
                                      const std::string &message = "") const;

  HttpResponsePtr createSuccessResponse(const Json::Value &data,
                                        int statusCode = 200) const;

  Json::Value loadStopsFromConfig(const std::string &instanceId) const;
  bool saveStopsToConfig(const std::string &instanceId,
                         const Json::Value &stops) const;

  bool validateROI(const Json::Value &roi, std::string &error) const;
  bool validateStopParameters(const Json::Value &stop, std::string &error) const;

  bool restartInstanceForStopUpdate(const std::string &instanceId) const;

  /**
   * Apply stop zone changes via hot swap (zero downtime) when runtime update fails.
   * @param instanceId Instance ID
   * @param stopsArray JSON array of stop zone objects (current desired state)
   * @return true if hot swap applied successfully
   */
  bool applyStopsUpdateViaHotSwap(const std::string &instanceId,
                                  const Json::Value &stopsArray) const;

  std::shared_ptr<cvedix_nodes::cvedix_ba_stop_node>
  findBAStopNode(const std::string &instanceId) const;

  std::map<int, std::vector<cvedix_objects::cvedix_point>>
  parseStopsFromJson(const Json::Value &stopsArray) const;

  bool updateStopsRuntime(const std::string &instanceId,
                          const Json::Value &stopsArray) const;
};
