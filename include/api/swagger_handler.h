#pragma once

#include <chrono>
#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

using namespace drogon;

/**
 * @brief Swagger UI handler
 *
 * Endpoints:
 * - GET /swagger - Swagger UI interface (all versions)
 * - GET /v1/swagger - Swagger UI for API v1
 * - GET /v2/swagger - Swagger UI for API v2
 * - GET /openapi.yaml - OpenAPI specification file (all versions)
 * - GET /v1/openapi.yaml - OpenAPI specification for v1
 * - GET /v2/openapi.yaml - OpenAPI specification for v2
 * - GET /v1/openapi/{lang}/openapi.yaml - OpenAPI specification for v1 with language (en/vi)
 * - GET /v2/openapi/{lang}/openapi.yaml - OpenAPI specification for v2 with language (en/vi)
 * - GET /v1/openapi/{lang}/openapi.json - OpenAPI specification for v1 with language (en/vi) in JSON format
 * - GET /v2/openapi/{lang}/openapi.json - OpenAPI specification for v2 with language (en/vi) in JSON format
 */
class SwaggerHandler : public drogon::HttpController<SwaggerHandler> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(SwaggerHandler::getSwaggerUI, "/swagger", Get);
  ADD_METHOD_TO(SwaggerHandler::getSwaggerUI, "/v1/swagger", Get);
  ADD_METHOD_TO(SwaggerHandler::getSwaggerUI, "/v2/swagger", Get);
  ADD_METHOD_TO(SwaggerHandler::getOpenAPISpec, "/openapi.yaml", Get);
  ADD_METHOD_TO(SwaggerHandler::getOpenAPISpec, "/v1/openapi.yaml", Get);
  ADD_METHOD_TO(SwaggerHandler::getOpenAPISpec, "/v2/openapi.yaml", Get);
  ADD_METHOD_TO(SwaggerHandler::getOpenAPISpecWithLang, "/v1/openapi/{lang}/openapi.yaml", Get);
  ADD_METHOD_TO(SwaggerHandler::getOpenAPISpecWithLang, "/v2/openapi/{lang}/openapi.yaml", Get);
  ADD_METHOD_TO(SwaggerHandler::getOpenAPISpecWithLang, "/v1/openapi/{lang}/openapi.json", Get);
  ADD_METHOD_TO(SwaggerHandler::getOpenAPISpecWithLang, "/v2/openapi/{lang}/openapi.json", Get);
  ADD_METHOD_TO(SwaggerHandler::getOpenAPISpec, "/api-docs", Get);
  ADD_METHOD_TO(SwaggerHandler::handleOptions, "/swagger", Options);
  ADD_METHOD_TO(SwaggerHandler::handleOptions, "/v1/swagger", Options);
  ADD_METHOD_TO(SwaggerHandler::handleOptions, "/v2/swagger", Options);
  ADD_METHOD_TO(SwaggerHandler::handleOptions, "/openapi.yaml", Options);
  ADD_METHOD_TO(SwaggerHandler::handleOptions, "/v1/openapi.yaml", Options);
  ADD_METHOD_TO(SwaggerHandler::handleOptions, "/v2/openapi.yaml", Options);
  ADD_METHOD_TO(SwaggerHandler::handleOptions, "/v1/openapi/{lang}/openapi.yaml", Options);
  ADD_METHOD_TO(SwaggerHandler::handleOptions, "/v2/openapi/{lang}/openapi.yaml", Options);
  ADD_METHOD_TO(SwaggerHandler::handleOptions, "/v1/openapi/{lang}/openapi.json", Options);
  ADD_METHOD_TO(SwaggerHandler::handleOptions, "/v2/openapi/{lang}/openapi.json", Options);
  METHOD_LIST_END

  /**
   * @brief Serve Swagger UI HTML page
   */
  void getSwaggerUI(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Serve OpenAPI specification file
   */
  void getOpenAPISpec(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Serve OpenAPI specification file with language support
   */
  void getOpenAPISpecWithLang(const HttpRequestPtr &req,
                              std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Handle OPTIONS request for CORS preflight
   */
  void handleOptions(const HttpRequestPtr &req,
                     std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief Validate version format (e.g., "v1", "v2")
   * @param version Version string to validate
   * @return true if valid, false otherwise
   * @note Public for testing
   */
  bool validateVersionFormat(const std::string &version) const;

  /**
   * @brief Sanitize file path to prevent path traversal
   * @param path Path to sanitize
   * @return Sanitized path or empty if invalid
   * @note Public for testing
   */
  std::string sanitizePath(const std::string &path) const;

private:
  /**
   * @brief Extract API version from request path
   * @return Version string (e.g., "v1", "v2") or empty string for all versions
   */
  std::string extractVersionFromPath(const std::string &path) const;

  /**
   * @brief Extract language from request path
   * @param path Request path
   * @return Language string (e.g., "en", "vi") or empty string if not found
   */
  std::string extractLanguageFromPath(const std::string &path) const;

  /**
   * @brief Validate language code
   * @param lang Language code to validate
   * @return true if valid (en or vi), false otherwise
   */
  bool validateLanguageCode(const std::string &lang) const;

  /**
   * @brief Generate Swagger UI HTML content
   * @param version API version (e.g., "v1", "v2") or empty for all versions
   * @param baseUrl Base URL for the API server (e.g., "http://localhost:8080")
   */
  std::string generateSwaggerUIHTML(const std::string &version = "",
                                    const std::string &baseUrl = "") const;

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
   * @brief Read OpenAPI YAML file
   * @param version API version to filter (e.g., "v1", "v2") or empty for all
   * versions
   * @param requestHost Host from request header (for browser-accessible URL)
   * @param language Language code (e.g., "en", "vi") or empty for default
   */
  std::string readOpenAPIFile(const std::string &version = "",
                              const std::string &requestHost = "",
                              const std::string &language = "") const;

  /**
   * @brief Filter OpenAPI YAML to only include paths for specified version
   * @param yamlContent Original YAML content
   * @param version Version to filter (e.g., "v1", "v2")
   * @return Filtered YAML content
   */
  std::string filterOpenAPIByVersion(const std::string &yamlContent,
                                     const std::string &version) const;

  /**
   * @brief Update server URLs in OpenAPI spec from environment variables
   * @param yamlContent Original YAML content
   * @param requestHost Host from request header (for browser-accessible URL,
   * overrides env vars)
   * @return YAML content with updated server URLs
   */
  std::string
  updateOpenAPIServerURLs(const std::string &yamlContent,
                          const std::string &requestHost = "") const;

  /**
   * @brief Convert YAML content to JSON format
   * @param yamlContent YAML content to convert
   * @return JSON content as string, or empty string on error
   */
  std::string yamlToJson(const std::string &yamlContent) const;

  // Cache for OpenAPI file content
  struct CacheEntry {
    std::string content;
    std::chrono::steady_clock::time_point timestamp;
    std::chrono::seconds ttl;
    std::filesystem::path filePath;
    std::filesystem::file_time_type fileModTime;
  };

  static std::unordered_map<std::string, CacheEntry> cache_;
  static std::mutex cache_mutex_;
  static const std::chrono::seconds cache_ttl_;
};
