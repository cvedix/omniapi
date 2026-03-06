#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <string>

using namespace drogon;

/**
 * @brief Scalar API Reference handler
 *
 * Endpoints:
 * - GET /v1/document - Scalar API documentation for API v1
 * - GET /v2/document - Scalar API documentation for API v2
 */
class ScalarHandler : public drogon::HttpController<ScalarHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(ScalarHandler::getScalarDocument, "/v1/document", Get);
  ADD_METHOD_TO(ScalarHandler::getScalarDocument, "/v2/document", Get);
  ADD_METHOD_TO(ScalarHandler::getScalarCSS, "/v1/scalar/standalone.css", Get);
  ADD_METHOD_TO(ScalarHandler::getScalarCSS, "/v2/scalar/standalone.css", Get);
  ADD_METHOD_TO(ScalarHandler::listExampleBodies, "/v1/document/examples", Get);
  ADD_METHOD_TO(ScalarHandler::getExampleBody, "/v1/document/examples/{path}", Get);
  ADD_METHOD_TO(ScalarHandler::handleOptions, "/v1/document", Options);
  ADD_METHOD_TO(ScalarHandler::handleOptions, "/v2/document", Options);
  ADD_METHOD_TO(ScalarHandler::handleOptions, "/v1/scalar/standalone.css", Options);
  ADD_METHOD_TO(ScalarHandler::handleOptions, "/v2/scalar/standalone.css", Options);
  ADD_METHOD_TO(ScalarHandler::handleOptions, "/v1/document/examples", Options);
  ADD_METHOD_TO(ScalarHandler::handleOptions, "/v1/document/examples/{path}", Options);
  METHOD_LIST_END

  /**
   * @brief Serve Scalar API documentation HTML page
   */
  void getScalarDocument(const HttpRequestPtr &req,
                         std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Serve Scalar CSS file with correct MIME type
   */
  void getScalarCSS(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle OPTIONS request for CORS preflight
   */
  void handleOptions(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief List available example body files
   */
  void listExampleBodies(const HttpRequestPtr &req,
                         std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Get a specific example body file content
   */
  void getExampleBody(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Validate version format (e.g., "v1", "v2")
   * @param version Version string to validate
   * @return true if valid, false otherwise
   */
  bool validateVersionFormat(const std::string &version) const;

  /**
   * @brief Validate language code
   * @param lang Language code to validate
   * @return true if valid (en or vi), false otherwise
   */
  bool validateLanguageCode(const std::string &lang) const;

private:
  /**
   * @brief Extract API version from request path
   * @return Version string (e.g., "v1", "v2") or empty string for all versions
   */
  std::string extractVersionFromPath(const std::string &path) const;

  /**
   * @brief Generate Scalar API documentation HTML content
   * @param version API version (e.g., "v1", "v2") or empty for all versions
   * @param baseUrl Base URL for the API server (e.g., "http://localhost:8080")
   * @param language Language code (e.g., "en", "vi") or empty for default
   */
  std::string generateScalarDocumentHTML(const std::string &version = "",
                                         const std::string &baseUrl = "",
                                         const std::string &language = "en") const;

  /**
   * @brief Read Scalar HTML template file
   * @return HTML content from api-specs/scalar/index.html
   */
  std::string readScalarHTMLFile() const;

  /**
   * @brief Read Scalar CSS file
   * @return CSS content from api-specs/scalar/standalone.css or empty if not found
   */
  std::string readScalarCSSFile() const;

  /**
   * @brief Get the examples/instances directory path
   * @return Path to examples/instances directory
   */
  std::string getExamplesInstancesDir() const;

  /**
   * @brief Recursively list all JSON files in examples/instances directory
   * @param dir Directory to search
   * @param basePath Base path for relative file paths
   * @return Vector of relative file paths
   */
  std::vector<std::string> listExampleFiles(const std::filesystem::path &dir,
                                            const std::filesystem::path &basePath) const;

  /**
   * @brief Sanitize file path to prevent path traversal
   * @param path Path to sanitize
   * @return Sanitized path or empty if invalid
   */
  std::string sanitizePath(const std::string &path) const;
};

