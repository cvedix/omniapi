#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

using namespace drogon;

// Forward declarations
class IInstanceManager;
class InstanceInfo;
class UpdateInstanceRequest;

/**
 * @brief Instance Management Handler
 *
 * Handles instance management operations.
 *
 * Endpoints:
 * - GET /v1/core/instance/status/summary - Get instance status summary (total,
 * running, stopped counts)
 * - GET /v1/core/instance - List all instances
 * - GET /v1/core/instance/{instanceId} - Get instance details
 * - PUT /v1/core/instance/{instanceId} - Update instance information
 * - POST /v1/core/instance/{instanceId}/start - Start an instance
 * - POST /v1/core/instance/{instanceId}/stop - Stop an instance
 * - POST /v1/core/instance/{instanceId}/restart - Restart an instance
 * - DELETE /v1/core/instance/{instanceId} - Delete an instance
 * - DELETE /v1/core/instance - Delete all instances
 * - POST /v1/core/instance/batch/start - Start multiple instances concurrently
 * - POST /v1/core/instance/batch/stop - Stop multiple instances concurrently
 * - POST /v1/core/instance/batch/restart - Restart multiple instances
 * concurrently
 * - GET /v1/core/instance/{instanceId}/output - Get instance output/processing
 * results
 * - POST /v1/core/instance/{instanceId}/input - Set input source for an
 * instance
 * - GET /v1/core/instance/{instanceId}/config - Get instance configuration
 * - POST /v1/core/instance/{instanceId}/config - Set config value at a
 * specific path
 * - GET /v1/core/instance/{instanceId}/statistics - Get instance statistics
 * - GET /v1/core/instance/{instanceId}/output/stream - Get stream output
 * configuration
 * - POST /v1/core/instance/{instanceId}/output/stream - Configure stream
 * output (RTMP/RTSP/HLS)
 */
class InstanceHandler : public drogon::HttpController<InstanceHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(InstanceHandler::getStatusSummary,
                "/v1/core/instance/status/summary", Get);
  // Routes for /v1/core/instance (without path parameter) - must come before
  // routes with {instanceId}
  ADD_METHOD_TO(InstanceHandler::listInstances, "/v1/core/instance", Get);
  ADD_METHOD_TO(InstanceHandler::deleteAllInstances, "/v1/core/instance",
                Delete);
  // Routes for /v1/core/instance/{instanceId} (with path parameter)
  ADD_METHOD_TO(InstanceHandler::getInstance, "/v1/core/instance/{instanceId}",
                Get);
  ADD_METHOD_TO(InstanceHandler::updateInstance,
                "/v1/core/instance/{instanceId}", Put);
  ADD_METHOD_TO(InstanceHandler::deleteInstance,
                "/v1/core/instance/{instanceId}", Delete);
  ADD_METHOD_TO(InstanceHandler::startInstance,
                "/v1/core/instance/{instanceId}/start", Post);
  ADD_METHOD_TO(InstanceHandler::stopInstance,
                "/v1/core/instance/{instanceId}/stop", Post);
  ADD_METHOD_TO(InstanceHandler::restartInstance,
                "/v1/core/instance/{instanceId}/restart", Post);
  ADD_METHOD_TO(InstanceHandler::getInstanceOutput,
                "/v1/core/instance/{instanceId}/output", Get);
  ADD_METHOD_TO(InstanceHandler::batchStartInstances,
                "/v1/core/instance/batch/start", Post);
  ADD_METHOD_TO(InstanceHandler::batchStopInstances,
                "/v1/core/instance/batch/stop", Post);
  ADD_METHOD_TO(InstanceHandler::batchRestartInstances,
                "/v1/core/instance/batch/restart", Post);
  ADD_METHOD_TO(InstanceHandler::setInstanceInput,
                "/v1/core/instance/{instanceId}/input", Post);
  ADD_METHOD_TO(InstanceHandler::getConfig,
                "/v1/core/instance/{instanceId}/config", Get);
  ADD_METHOD_TO(InstanceHandler::setConfig,
                "/v1/core/instance/{instanceId}/config", Post);
  ADD_METHOD_TO(InstanceHandler::getStatistics,
                "/v1/core/instance/{instanceId}/statistics", Get);
  ADD_METHOD_TO(InstanceHandler::getLastFrame,
                "/v1/core/instance/{instanceId}/frame", Get);
  ADD_METHOD_TO(InstanceHandler::getStreamOutput,
                "/v1/core/instance/{instanceId}/output/stream", Get);
  ADD_METHOD_TO(InstanceHandler::configureStreamOutput,
                "/v1/core/instance/{instanceId}/output/stream", Post);
  ADD_METHOD_TO(InstanceHandler::getInstanceClasses,
                "/v1/core/instance/{instanceId}/classes", Get);
  ADD_METHOD_TO(InstanceHandler::getInstancePreview,
                "/v1/core/instance/{instanceId}/preview", Get);
  ADD_METHOD_TO(InstanceHandler::loadInstance,
                "/v1/core/instance/{instanceId}/load", Post);
  ADD_METHOD_TO(InstanceHandler::unloadInstance,
                "/v1/core/instance/{instanceId}/unload", Post);
  ADD_METHOD_TO(InstanceHandler::getInstanceState,
                "/v1/core/instance/{instanceId}/state", Get);
  ADD_METHOD_TO(InstanceHandler::setInstanceState,
                "/v1/core/instance/{instanceId}/state", Post);
  ADD_METHOD_TO(InstanceHandler::patchInstance,
                "/v1/core/instance/{instanceId}", Patch);
  ADD_METHOD_TO(InstanceHandler::consumeEvents,
                "/v1/core/instance/{instanceId}/consume_events", Get);
  ADD_METHOD_TO(InstanceHandler::configureHlsOutput,
                "/v1/core/instance/{instanceId}/output/hls", Post);
  ADD_METHOD_TO(InstanceHandler::configureRtspOutput,
                "/v1/core/instance/{instanceId}/output/rtsp", Post);
  ADD_METHOD_TO(InstanceHandler::pushEncodedFrame,
                "/v1/core/instance/{instanceId}/push/encoded/{codecId}", Post);
  ADD_METHOD_TO(InstanceHandler::pushCompressedFrame,
                "/v1/core/instance/{instanceId}/push/compressed", Post);
  ADD_METHOD_TO(InstanceHandler::handleOptions, "/v1/core/instance", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/start", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/stop", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/restart", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/output", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/input", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/config", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/statistics", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/frame", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/output/stream", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/classes", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/preview", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/load", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/unload", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/state", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/consume_events", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/output/hls", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/output/rtsp", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/push/encoded/{codecId}", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/{instanceId}/push/compressed", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions, "/v1/core/instance/batch/start",
                Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions, "/v1/core/instance/batch/stop",
                Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/batch/restart", Options);
  ADD_METHOD_TO(InstanceHandler::handleOptions,
                "/v1/core/instance/status/summary", Options);
  METHOD_LIST_END

  /**
   * @brief Handle GET /v1/core/instance/status/summary
   * Returns status summary about instances (total, configured, running, stopped
   * counts)
   */
  void
  getStatusSummary(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/instance
   * Lists all instances with summary information
   */
  void listInstances(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/instance/{instanceId}
   * Gets detailed information about a specific instance
   */
  void getInstance(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/instance/{instanceId}/output
   * Gets real-time output/processing results for a specific instance
   */
  void
  getInstanceOutput(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/{instanceId}/start
   * Starts an instance pipeline
   */
  void startInstance(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/{instanceId}/stop
   * Stops an instance pipeline
   */
  void stopInstance(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/{instanceId}/restart
   * Restarts an instance (stops then starts)
   */
  void restartInstance(const HttpRequestPtr &req,
                       std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle DELETE /v1/core/instance/{instanceId}
   * Deletes an instance
   */
  void deleteInstance(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle DELETE /v1/core/instance
   * Deletes all instances
   */
  void
  deleteAllInstances(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle PUT /v1/core/instance/{instanceId}
   * Updates instance information
   */
  void updateInstance(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/batch/start
   * Starts multiple instances concurrently
   */
  void
  batchStartInstances(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/batch/stop
   * Stops multiple instances concurrently
   */
  void
  batchStopInstances(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/batch/restart
   * Restarts multiple instances concurrently
   */
  void batchRestartInstances(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/{instanceId}/input
   * Sets input source for an instance
   */
  void
  setInstanceInput(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/instance/{instanceId}/config
   * Gets instance configuration (config format, not runtime state)
   */
  void getConfig(const HttpRequestPtr &req,
                 std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/{instanceId}/config
   * Sets config value at a specific path (nested path supported with "/"
   * separator)
   */
  void setConfig(const HttpRequestPtr &req,
                 std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/instance/{instanceId}/statistics
   * Gets real-time statistics for a specific instance
   */
  void getStatistics(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/instance/{instanceId}/frame
   * Gets the last frame from a running instance
   */
  void getLastFrame(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/instance/{instanceId}/output/stream
   * Gets stream output configuration for an instance
   */
  void getStreamOutput(const HttpRequestPtr &req,
                       std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/{instanceId}/output/stream
   * Configures stream output for an instance (RTMP/RTSP/HLS)
   */
  void configureStreamOutput(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/instance/{instanceId}/classes
   * Gets available classes from labels file
   */
  void
  getInstanceClasses(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/instance/{instanceId}/preview
   * Gets preview frame (screenshot) from instance
   */
  void
  getInstancePreview(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/{instanceId}/load
   * Loads instance into memory
   */
  void loadInstance(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/{instanceId}/unload
   * Unloads instance from memory
   */
  void unloadInstance(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/instance/{instanceId}/state
   * Gets runtime state of instance
   */
  void getInstanceState(const HttpRequestPtr &req,
                       std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/{instanceId}/state
   * Sets runtime state value at a specific path
   */
  void setInstanceState(const HttpRequestPtr &req,
                       std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle PATCH /v1/core/instance/{instanceId}
   * Updates instance with partial data
   */
  void patchInstance(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/core/instance/{instanceId}/consume_events
   * Consumes events from instance event queue
   */
  void consumeEvents(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/{instanceId}/output/hls
   * Configures HLS output for instance
   */
  void configureHlsOutput(const HttpRequestPtr &req,
                          std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/{instanceId}/output/rtsp
   * Configures RTSP output for instance
   */
  void configureRtspOutput(const HttpRequestPtr &req,
                           std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/{instanceId}/push/encoded/{codecId}
   * Push encoded frame (H.264/H.265) into instance
   */
  void pushEncodedFrame(const HttpRequestPtr &req,
                        std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle POST /v1/core/instance/{instanceId}/push/compressed
   * Push compressed frame (JPEG/PNG) into instance
   */
  void pushCompressedFrame(const HttpRequestPtr &req,
                           std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle OPTIONS request for CORS preflight
   */
  void handleOptions(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Set instance manager (dependency injection)
   * @param manager Pointer to IInstanceManager implementation
   */
  static void setInstanceManager(IInstanceManager *manager);

private:
  static IInstanceManager *instance_manager_;

  /**
   * @brief Parse JSON request body to UpdateInstanceRequest
   */
  bool parseUpdateRequest(const Json::Value &json, UpdateInstanceRequest &req,
                          std::string &error);

  /**
   * @brief Convert InstanceInfo to JSON
   */
  Json::Value instanceInfoToJson(const InstanceInfo &info) const;

  /**
   * @brief Create error response
   */
  HttpResponsePtr createErrorResponse(int statusCode, const std::string &error,
                                      const std::string &message = "") const;

  /**
   * @brief Extract instance ID from request path
   * @param req HTTP request
   * @return Instance ID if found, empty string otherwise
   */
  std::string extractInstanceId(const HttpRequestPtr &req) const;

  /**
   * @brief Create success JSON response with CORS headers
   */
  HttpResponsePtr createSuccessResponse(const Json::Value &data,
                                        int statusCode = 200) const;

  /**
   * @brief Get output file information for an instance
   * @param instanceId Instance ID
   * @return JSON object with file output information
   */
  Json::Value getOutputFileInfo(const std::string &instanceId) const;

  /**
   * @brief Set nested JSON value at a path (e.g., "Output/handlers/Mqtt")
   * @param root Root JSON object to modify
   * @param path Path string with "/" separator (e.g., "Output/handlers/Mqtt")
   * @param value JSON value to set
   * @return true if successful, false otherwise
   */
  bool setNestedJsonValue(Json::Value &root, const std::string &path,
                          const Json::Value &value) const;

  /**
   * @brief Read classes from labels file
   * @param labelsPath Path to labels file
   * @return Vector of class names
   */
  std::vector<std::string>
  readClassesFromFile(const std::string &labelsPath) const;
};
