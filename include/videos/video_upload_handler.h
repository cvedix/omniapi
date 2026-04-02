#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <json/json.h>
#include <string>

using namespace drogon;

/**
 * @brief Video Upload Handler
 *
 * Handles video file uploads for AI instances.
 *
 * Endpoints:
 * - POST /v1/securt/video/upload - Upload a video file
 * - GET /v1/securt/video/list - List uploaded videos
 * - PUT /v1/securt/video/{videoName} - Rename a video file
 * - DELETE /v1/securt/video/{videoName} - Delete a video file
 */
class VideoUploadHandler : public drogon::HttpController<VideoUploadHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(VideoUploadHandler::uploadVideo, "/v1/securt/video/upload", Post);
  ADD_METHOD_TO(VideoUploadHandler::listVideos, "/v1/securt/video/list", Get);
  ADD_METHOD_TO(VideoUploadHandler::renameVideo, "/v1/securt/video/{videoName}",
                Put);
  ADD_METHOD_TO(VideoUploadHandler::deleteVideo, "/v1/securt/video/{videoName}",
                Delete);
  ADD_METHOD_TO(VideoUploadHandler::handleOptions, "/v1/securt/video/upload",
                Options);
  METHOD_LIST_END

  /**
   * @brief Handle POST /v1/securt/video/upload
   * Uploads a video file (multipart/form-data)
   */
  void uploadVideo(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/securt/video/list
   * Lists all uploaded video files
   */
  void listVideos(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle PUT /v1/securt/video/{videoName}
   * Renames a video file
   */
  void renameVideo(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle DELETE /v1/securt/video/{videoName}
   * Deletes a video file
   */
  void deleteVideo(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle OPTIONS request for CORS preflight
   */
  void handleOptions(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Set videos directory (dependency injection)
   */
  static void setVideosDirectory(const std::string &dir);

private:
  static std::string videos_dir_;

  /**
   * @brief Get videos directory path
   */
  std::string getVideosDirectory() const;

  /**
   * @brief Validate video file extension
   */
  bool isValidVideoFile(const std::string &filename) const;

  /**
   * @brief Sanitize filename to prevent path traversal
   */
  std::string sanitizeFilename(const std::string &filename) const;

  /**
   * @brief Sanitize directory path to prevent path traversal
   */
  std::string sanitizeDirectoryPath(const std::string &dirPath) const;

  /**
   * @brief Extract video name from request path
   */
  std::string extractVideoName(const HttpRequestPtr &req) const;

  /**
   * @brief Get directory path from request (query parameter or form data)
   */
  std::string getDirectoryPath(const HttpRequestPtr &req) const;

  /**
   * @brief Create error response
   */
  HttpResponsePtr createErrorResponse(int statusCode, const std::string &error,
                                      const std::string &message) const;
};
