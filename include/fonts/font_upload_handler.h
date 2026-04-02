#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <json/json.h>
#include <string>

using namespace drogon;

/**
 * @brief Font Upload Handler
 *
 * Handles font file uploads for AI instances.
 *
 * Endpoints:
 * - POST /v1/securt/font/upload - Upload a font file
 * - GET /v1/securt/font/list - List uploaded fonts
 * - PUT /v1/securt/font/{fontName} - Rename a font file
 * - DELETE /v1/securt/font/{fontName} - Delete a font file
 */
class FontUploadHandler : public drogon::HttpController<FontUploadHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(FontUploadHandler::uploadFont, "/v1/securt/font/upload", Post);
  ADD_METHOD_TO(FontUploadHandler::listFonts, "/v1/securt/font/list", Get);
  ADD_METHOD_TO(FontUploadHandler::renameFont, "/v1/securt/font/{fontName}", Put);
  ADD_METHOD_TO(FontUploadHandler::deleteFont, "/v1/securt/font/{fontName}",
                Delete);
  ADD_METHOD_TO(FontUploadHandler::handleOptions, "/v1/securt/font/upload",
                Options);
  METHOD_LIST_END

  /**
   * @brief Handle POST /v1/securt/font/upload
   * Uploads a font file (multipart/form-data)
   */
  void uploadFont(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle GET /v1/securt/font/list
   * Lists all uploaded font files
   */
  void listFonts(const HttpRequestPtr &req,
                 std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle PUT /v1/securt/font/{fontName}
   * Renames a font file
   */
  void renameFont(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle DELETE /v1/securt/font/{fontName}
   * Deletes a font file
   */
  void deleteFont(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle OPTIONS request for CORS preflight
   */
  void handleOptions(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Set fonts directory (dependency injection)
   */
  static void setFontsDirectory(const std::string &dir);

private:
  static std::string fonts_dir_;

  /**
   * @brief Get fonts directory path
   */
  std::string getFontsDirectory() const;

  /**
   * @brief Validate font file extension
   */
  bool isValidFontFile(const std::string &filename) const;

  /**
   * @brief Sanitize filename to prevent path traversal
   */
  std::string sanitizeFilename(const std::string &filename) const;

  /**
   * @brief Sanitize directory path to prevent path traversal
   */
  std::string sanitizeDirectoryPath(const std::string &dirPath) const;

  /**
   * @brief Extract font name from request path
   */
  std::string extractFontName(const HttpRequestPtr &req) const;

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
