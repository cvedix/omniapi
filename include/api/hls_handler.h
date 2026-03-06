#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

using namespace drogon;

/**
 * @brief HLS Stream Handler
 *
 * Serves HLS playlist (.m3u8) and segments (.ts) files for instances.
 *
 * Endpoints:
 * - GET /hls/{instanceId}/stream.m3u8 - Get HLS playlist
 * - GET /hls/{instanceId}/segment_{segmentId}.ts - Get HLS segment
 */
class HlsHandler : public drogon::HttpController<HlsHandler> {
public:
  METHOD_LIST_BEGIN
  // Use exact path match for playlist (like SwaggerHandler does with openapi.yaml)
  ADD_METHOD_TO(HlsHandler::getPlaylist, "/hls/{instanceId}/stream.m3u8", Get);
  // Match segments - extract from path since Drogon may not parse segment_{segmentId}.ts correctly
  ADD_METHOD_TO(HlsHandler::getSegment, "/hls/{instanceId}/segment_{segmentId}.ts", Get);
  METHOD_LIST_END

  /**
   * @brief Handle GET /hls/{instanceId}/stream.m3u8
   * Returns HLS playlist file
   */
  void getPlaylist(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /hls/{instanceId}/segment_{segmentId}.ts
   * Returns HLS segment file
   */
  void getSegment(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

private:
  /**
   * @brief Get HLS directory path for instance
   * @param instanceId Instance ID
   * @return Directory path
   */
  std::string getHlsDirectory(const std::string &instanceId) const;
};

