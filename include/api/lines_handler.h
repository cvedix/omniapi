#pragma once

#include <cvedix/nodes/ba/cvedix_ba_line_crossline_node.h>
#include <cvedix/objects/shapes/cvedix_line.h>
#include <cvedix/objects/shapes/cvedix_point.h>
#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <map>
#include <memory>
#include <string>

using namespace drogon;

/**
 * @brief Lines Management Handler
 *
 * Handles crossing lines management for ba_crossline instances.
 *
 * Endpoints:
 * - GET /v1/core/instance/{instanceId}/lines - Get all lines
 * - POST /v1/core/instance/{instanceId}/lines - Create a new line
 * - DELETE /v1/core/instance/{instanceId}/lines - Delete all lines
 * - GET /v1/core/instance/{instanceId}/lines/{lineId} - Get a specific line
 * - PUT /v1/core/instance/{instanceId}/lines/{lineId} - Update a specific line
 * - DELETE /v1/core/instance/{instanceId}/lines/{lineId} - Delete a specific
 * line
 */
class LinesHandler : public drogon::HttpController<LinesHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(LinesHandler::getAllLines,
                "/v1/core/instance/{instanceId}/lines", Get);
  ADD_METHOD_TO(LinesHandler::createLine,
                "/v1/core/instance/{instanceId}/lines", Post);
  ADD_METHOD_TO(LinesHandler::deleteAllLines,
                "/v1/core/instance/{instanceId}/lines", Delete);
  ADD_METHOD_TO(LinesHandler::getLine,
                "/v1/core/instance/{instanceId}/lines/{lineId}", Get);
  ADD_METHOD_TO(LinesHandler::updateLine,
                "/v1/core/instance/{instanceId}/lines/{lineId}", Put);
  ADD_METHOD_TO(LinesHandler::deleteLine,
                "/v1/core/instance/{instanceId}/lines/{lineId}", Delete);
  ADD_METHOD_TO(LinesHandler::batchUpdateLines,
                "/v1/core/instance/{instanceId}/lines/batch", Post);
  ADD_METHOD_TO(LinesHandler::handleOptions,
                "/v1/core/instance/{instanceId}/lines", Options);
  ADD_METHOD_TO(LinesHandler::handleOptions,
                "/v1/core/instance/{instanceId}/lines/{lineId}", Options);
  ADD_METHOD_TO(LinesHandler::handleOptions,
                "/v1/core/instance/{instanceId}/lines/batch", Options);
  METHOD_LIST_END

  /**
   * @brief Handle GET /v1/core/instance/{instanceId}/lines
   * Gets all crossing lines for an instance
   */
  void getAllLines(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/{instanceId}/lines
   * Creates a new crossing line for an instance
   */
  void createLine(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle DELETE /v1/core/instance/{instanceId}/lines
   * Deletes all crossing lines for an instance
   */
  void deleteAllLines(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/instance/{instanceId}/lines/{lineId}
   * Gets a specific crossing line by ID
   */
  void getLine(const HttpRequestPtr &req,
               std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle PUT /v1/core/instance/{instanceId}/lines/{lineId}
   * Updates a specific crossing line by ID
   */
  void updateLine(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle DELETE /v1/core/instance/{instanceId}/lines/{lineId}
   * Deletes a specific crossing line by ID
   */
  void deleteLine(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/{instanceId}/lines/batch
   * Updates multiple lines in a single request
   */
  void
  batchUpdateLines(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle OPTIONS request for CORS preflight
   */
  void handleOptions(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Set instance manager (dependency injection)
   * Supports both InProcess and Subprocess modes
   */
  static void setInstanceManager(class IInstanceManager *manager);

private:
  static class IInstanceManager *instance_manager_;

  /**
   * @brief Extract instance ID from request path
   */
  std::string extractInstanceId(const HttpRequestPtr &req) const;

  /**
   * @brief Extract line ID from request path
   */
  std::string extractLineId(const HttpRequestPtr &req) const;

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

  /**
   * @brief Load lines from instance config
   * @param instanceId Instance ID
   * @return JSON array of lines, empty array if not found or error
   */
  Json::Value loadLinesFromConfig(const std::string &instanceId) const;

  /**
   * @brief Save lines to instance config
   * @param instanceId Instance ID
   * @param lines JSON array of lines
   * @return true if successful, false otherwise
   */
  bool saveLinesToConfig(const std::string &instanceId,
                         const Json::Value &lines) const;

  /**
   * @brief Validate line coordinates
   * @param coordinates JSON array of coordinate objects
   * @param error Error message output
   * @return true if valid, false otherwise
   */
  bool validateCoordinates(const Json::Value &coordinates,
                           std::string &error) const;

  /**
   * @brief Validate direction value
   * @param direction Direction string
   * @param error Error message output
   * @return true if valid, false otherwise
   */
  bool validateDirection(const std::string &direction,
                         std::string &error) const;

  /**
   * @brief Validate classes array
   * @param classes JSON array of class strings
   * @param error Error message output
   * @return true if valid, false otherwise
   */
  bool validateClasses(const Json::Value &classes, std::string &error) const;

  /**
   * @brief Validate color array
   * @param color JSON array of color values
   * @param error Error message output
   * @return true if valid, false otherwise
   */
  bool validateColor(const Json::Value &color, std::string &error) const;

  /**
   * @brief Restart instance to apply line changes
   * @param instanceId Instance ID
   * @return true if restart initiated successfully
   */
  bool restartInstanceForLineUpdate(const std::string &instanceId) const;

  /**
   * @brief Find ba_crossline_node in running instance pipeline
   * @param instanceId Instance ID
   * @return Shared pointer to ba_crossline_node if found, nullptr otherwise
   */
  std::shared_ptr<cvedix_nodes::cvedix_ba_line_crossline_node>
  findBACrosslineNode(const std::string &instanceId) const;

  /**
   * @brief Parse lines from JSON array to map<int, cvedix_line>
   * @param linesArray JSON array of line objects
   * @return Map of channel to cvedix_line, empty map if parse fails
   */
  std::map<int, cvedix_objects::cvedix_line>
  parseLinesFromJson(const Json::Value &linesArray) const;

  /**
   * @brief Parse lines from JSON array to map<int, vector<crossline_config>>
   * @param linesArray JSON array of line objects
   * @return Map of channel to vector of crossline_config, empty map if parse fails
   */
  std::map<int, std::vector<cvedix_nodes::crossline_config>>
  parseLinesFromJsonWithConfigs(const Json::Value &linesArray) const;

  /**
   * @brief Update lines in running ba_crossline_node without restart
   * @param instanceId Instance ID
   * @param linesArray JSON array of line objects
   * @return true if update successful, false if fallback to restart needed
   */
  bool updateLinesRuntime(const std::string &instanceId,
                          const Json::Value &linesArray) const;
};
