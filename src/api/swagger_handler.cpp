#include "api/swagger_handler.h"
#include "core/env_config.h"
#include "core/logger.h"
#include "core/metrics_interceptor.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <fstream>
#include <json/json.h>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <vector>
#ifdef YAML_CPP_FOUND
#include <yaml-cpp/yaml.h>
#endif

// Static cache members
std::unordered_map<std::string, SwaggerHandler::CacheEntry>
    SwaggerHandler::cache_;
std::mutex SwaggerHandler::cache_mutex_;
const std::chrono::seconds SwaggerHandler::cache_ttl_(300); // 5 minutes cache

void SwaggerHandler::getSwaggerUI(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    std::string path = req->path();
    std::string version = extractVersionFromPath(path);

    // Validate version format if provided
    if (!version.empty() && !validateVersionFormat(version)) {
      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k400BadRequest);
      resp->setBody("Invalid API version format");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Build absolute URL for OpenAPI spec based on request
    std::string host = req->getHeader("Host");
    if (host.empty()) {
      host = "localhost:8080";
    }
    std::string scheme = req->getHeader("X-Forwarded-Proto");
    if (scheme.empty()) {
      // Check if request is secure
      scheme = req->isOnSecureConnection() ? "https" : "http";
    }

    std::string baseUrl = scheme + "://" + host;
    std::string html = generateSwaggerUIHTML(version, baseUrl);

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_TEXT_HTML);
    resp->setBody(html);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k500InternalServerError);
    resp->setBody("Internal server error");
    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (...) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k500InternalServerError);
    resp->setBody("Internal server error");
    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void SwaggerHandler::getOpenAPISpec(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);
  try {
    std::string path = req->path();
    std::string version = extractVersionFromPath(path);

    // Validate version format if provided
    if (!version.empty() && !validateVersionFormat(version)) {
      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k400BadRequest);
      resp->setBody("Invalid API version format");
      // Add CORS headers
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Get host from request for browser-accessible URL
    std::string requestHost = req->getHeader("Host");
    if (requestHost.empty()) {
      requestHost = "localhost:8080";
    }

    std::string yaml = readOpenAPIFile(version, requestHost, "");

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_TEXT_PLAIN);
    resp->addHeader("Content-Type", "text/yaml; charset=utf-8");
    resp->setBody(yaml);

    // Add CORS headers (restrictive for production)
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k500InternalServerError);
    resp->setBody("Internal server error");
    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (...) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k500InternalServerError);
    resp->setBody("Internal server error");
    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void SwaggerHandler::getOpenAPISpecWithLang(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);
  try {
    std::string path = req->path();
    std::string version = extractVersionFromPath(path);
    // Get language from path parameter (Drogon uses {lang} in route)
    std::string language = req->getParameter("lang");
    // Fallback to extracting from path if parameter not found
    if (language.empty()) {
      language = extractLanguageFromPath(path);
    }

    // Validate version format if provided
    if (!version.empty() && !validateVersionFormat(version)) {
      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k400BadRequest);
      resp->setBody("Invalid API version format");
      // Add CORS headers
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Validate language code
    if (!language.empty() && !validateLanguageCode(language)) {
      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k400BadRequest);
      resp->setBody("Invalid language code. Supported languages: en, vi");
      // Add CORS headers
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Get host from request for browser-accessible URL
    std::string requestHost = req->getHeader("Host");
    if (requestHost.empty()) {
      requestHost = "localhost:8080";
    }

    std::string yaml = readOpenAPIFile(version, requestHost, language);

    // Check if request is for JSON format
    bool isJsonRequest = path.find(".json") != std::string::npos;
    
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    
    if (isJsonRequest) {
      // Convert YAML to JSON
      std::string json = yamlToJson(yaml);
      if (json.empty()) {
        // Conversion failed, return error
        resp->setStatusCode(k500InternalServerError);
        resp->setBody("Failed to convert YAML to JSON");
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
        MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
        return;
      }
      resp->setContentTypeCode(CT_APPLICATION_JSON);
      resp->addHeader("Content-Type", "application/json; charset=utf-8");
      resp->setBody(json);
    } else {
      resp->setContentTypeCode(CT_TEXT_PLAIN);
      resp->addHeader("Content-Type", "text/yaml; charset=utf-8");
      resp->setBody(yaml);
    }

    // Add CORS headers (restrictive for production)
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k500InternalServerError);
    resp->setBody("Internal server error");
    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (...) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k500InternalServerError);
    resp->setBody("Internal server error");
    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

std::string
SwaggerHandler::extractVersionFromPath(const std::string &path) const {
  // Match patterns like /v1/swagger, /v2/openapi.yaml, etc.
  // Only match v followed by digits (v1, v2, v10, etc.)
  std::regex versionPattern(R"(/(v\d+)/)");
  std::smatch match;

  if (std::regex_search(path, match, versionPattern)) {
    std::string version = match[1].str();
    // Additional validation: ensure it's exactly "v" + digits
    if (validateVersionFormat(version)) {
      return version;
    }
  }

  return ""; // No version found, return all versions
}

bool SwaggerHandler::validateVersionFormat(const std::string &version) const {
  // Version must start with 'v' followed by one or more digits
  // Examples: v1, v2, v10, v99 (but not v, v0, v-1, v1.0, etc.)
  if (version.empty() || version.length() < 2) {
    return false;
  }

  if (version[0] != 'v') {
    return false;
  }

  // Check that all remaining characters are digits
  for (size_t i = 1; i < version.length(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(version[i]))) {
      return false;
    }
  }

  // Ensure at least one digit after 'v'
  return version.length() > 1;
}

std::string SwaggerHandler::extractLanguageFromPath(const std::string &path) const {
  // Match patterns like /v1/openapi/en/openapi.yaml or /v1/openapi/vi/openapi.yaml
  // Extract the language code (en or vi) from the path
  std::regex langPattern(R"(/openapi/(en|vi)/openapi\.yaml)");
  std::smatch match;

  if (std::regex_search(path, match, langPattern)) {
    return match[1].str();
  }

  return ""; // No language found
}

bool SwaggerHandler::validateLanguageCode(const std::string &lang) const {
  // Only support "en" and "vi"
  return lang == "en" || lang == "vi";
}

std::string SwaggerHandler::sanitizePath(const std::string &path) const {
  // Prevent path traversal attacks
  // Only allow simple filenames without directory traversal
  if (path.empty()) {
    return "";
  }

  // Check for path traversal patterns
  if (path.find("..") != std::string::npos ||
      path.find("/") != std::string::npos ||
      path.find("\\") != std::string::npos) {
    return "";
  }

  // Only allow alphanumeric, dash, underscore, dot
  for (char c : path) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' &&
        c != '.') {
      return "";
    }
  }

  return path;
}

std::string
SwaggerHandler::generateSwaggerUIHTML(const std::string &version,
                                      const std::string &baseUrl) const {
  // Determine the OpenAPI spec URL based on version
  std::string specUrl = "/openapi.yaml";
  std::string title = "OmniAPI - Swagger UI";

  if (!version.empty()) {
    specUrl = "/" + version + "/openapi.yaml";
    title = "OmniAPI " + version + " - Swagger UI";
  }

  // Use absolute URL if baseUrl is provided, otherwise use relative URL
  std::string fullSpecUrl = specUrl;
  if (!baseUrl.empty()) {
    // Remove trailing slash from baseUrl if present
    std::string cleanBaseUrl = baseUrl;
    if (cleanBaseUrl.back() == '/') {
      cleanBaseUrl.pop_back();
    }
    fullSpecUrl = cleanBaseUrl + specUrl;
  }

  // Swagger UI HTML with embedded CDN and fallback mechanism
  std::string html = R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)" + title +
                     R"(</title>
    <style>
        html {
            box-sizing: border-box;
            overflow: -moz-scrollbars-vertical;
            overflow-y: scroll;
        }
        *, *:before, *:after {
            box-sizing: inherit;
        }
        body {
            margin:0;
            background: #fafafa;
        }
        #loading-message {
            text-align: center;
            padding: 20px;
            font-family: Arial, sans-serif;
            color: #333;
        }
    </style>
    <script>
        // CDN fallback configuration - multiple CDN providers for redundancy
        const CDN_CONFIGS = {
            css: [
                'https://cdn.jsdelivr.net/npm/swagger-ui-dist@5.9.0/swagger-ui.css',
                'https://unpkg.com/swagger-ui-dist@5.9.0/swagger-ui.css',
                'https://fastly.jsdelivr.net/npm/swagger-ui-dist@5.9.0/swagger-ui.css'
            ],
            bundle: [
                'https://cdn.jsdelivr.net/npm/swagger-ui-dist@5.9.0/swagger-ui-bundle.js',
                'https://unpkg.com/swagger-ui-dist@5.9.0/swagger-ui-bundle.js',
                'https://fastly.jsdelivr.net/npm/swagger-ui-dist@5.9.0/swagger-ui-bundle.js'
            ],
            standalone: [
                'https://cdn.jsdelivr.net/npm/swagger-ui-dist@5.9.0/swagger-ui-standalone-preset.js',
                'https://unpkg.com/swagger-ui-dist@5.9.0/swagger-ui-standalone-preset.js',
                'https://fastly.jsdelivr.net/npm/swagger-ui-dist@5.9.0/swagger-ui-standalone-preset.js'
            ]
        };

        // Load resource with fallback
        function loadResource(urls, type, onSuccess, onError, index = 0) {
            if (index >= urls.length) {
                console.error('All CDN fallbacks failed for', type);
                if (onError) onError();
                return;
            }

            const url = urls[index];
            console.log('Trying to load', type, 'from:', url);

            if (type === 'css') {
                const link = document.createElement('link');
                link.rel = 'stylesheet';
                link.type = 'text/css';
                link.href = url;
                link.onerror = function() {
                    console.warn('Failed to load CSS from', url, ', trying next CDN...');
                    loadResource(urls, type, onSuccess, onError, index + 1);
                };
                link.onload = function() {
                    console.log('Successfully loaded CSS from', url);
                    if (onSuccess) onSuccess();
                };
                document.head.appendChild(link);
            } else {
                const script = document.createElement('script');
                script.src = url;
                script.onerror = function() {
                    console.warn('Failed to load script from', url, ', trying next CDN...');
                    loadResource(urls, type, onSuccess, onError, index + 1);
                };
                script.onload = function() {
                    console.log('Successfully loaded script from', url);
                    if (onSuccess) onSuccess();
                };
                document.head.appendChild(script);
            }
        }

        // Initialize Swagger UI after all resources are loaded
        let cssLoaded = false;
        let bundleLoaded = false;
        let standaloneLoaded = false;
        let swaggerInitialized = false;

        function checkAndInitSwagger() {
            // Check if DOM is ready
            if (document.readyState === 'loading') {
                document.addEventListener('DOMContentLoaded', checkAndInitSwagger);
                return;
            }

            // Check if all resources are loaded and Swagger is available
            if (!swaggerInitialized && cssLoaded && bundleLoaded && standaloneLoaded && 
                typeof SwaggerUIBundle !== 'undefined' && typeof SwaggerUIStandalonePreset !== 'undefined') {
                
                const swaggerContainer = document.getElementById('swagger-ui');
                if (!swaggerContainer) {
                    console.error('Swagger UI container not found');
                    return;
                }

                const loadingMsg = document.getElementById('loading-message');
                if (loadingMsg) loadingMsg.remove();

                try {
                    swaggerInitialized = true;
                    const ui = SwaggerUIBundle({
                        url: ')" +
                     fullSpecUrl + R"(',
                        dom_id: '#swagger-ui',
                        deepLinking: true,
                        presets: [
                            SwaggerUIBundle.presets.apis,
                            SwaggerUIStandalonePreset
                        ],
                        plugins: [
                            SwaggerUIBundle.plugins.DownloadUrl
                        ],
                        layout: "StandaloneLayout",
                        validatorUrl: null,
                        tryItOutEnabled: true,
                        requestInterceptor: function(request) {
                            console.log('Swagger request:', request);
                            return request;
                        },
                        onComplete: function() {
                            console.log("Swagger UI loaded successfully");
                            
                            // Add example body selector functionality
                            setTimeout(function() {
                                addExampleBodySelector();
                            }, 1000);
                        },
                        onFailure: function(data) {
                            console.error("Swagger UI failed to load:", data);
                            const container = document.getElementById('swagger-ui');
                            if (container) {
                                container.innerHTML = '<div style="padding: 20px; color: red;">Error loading Swagger UI. Please check console for details.</div>';
                            }
                        }
                    });
                } catch (error) {
                    console.error("Error initializing Swagger UI:", error);
                    const container = document.getElementById('swagger-ui');
                    if (container) {
                        container.innerHTML = '<div style="padding: 20px; color: red;">Error loading Swagger UI: ' + error.message + '. Please check console for details.</div>';
                    }
                    swaggerInitialized = false;
                }
            }
        }

        // Wait for DOM to be ready before loading resources
        if (document.readyState === 'loading') {
            document.addEventListener('DOMContentLoaded', function() {
                initializeSwaggerResources();
            });
        } else {
            initializeSwaggerResources();
        }

        function initializeSwaggerResources() {
            // Load CSS first
            loadResource(CDN_CONFIGS.css, 'css', function() {
                cssLoaded = true;
                checkAndInitSwagger();
            });

            // Load bundle script
            loadResource(CDN_CONFIGS.bundle, 'script', function() {
                bundleLoaded = true;
                checkAndInitSwagger();
            });

            // Load standalone preset script
            loadResource(CDN_CONFIGS.standalone, 'script', function() {
                standaloneLoaded = true;
                checkAndInitSwagger();
            });
        }
    </script>
</head>
<body>
    <div id="loading-message">Loading Swagger UI...</div>
    <div id="swagger-ui"></div>
</body>
</html>)";

  return html;
}

std::string
SwaggerHandler::readOpenAPIFile(const std::string &version,
                                const std::string &requestHost,
                                const std::string &language) const {
  // Check cache first - include language in cache key
  std::string cacheKey = version.empty() ? "all" : version;
  if (!language.empty()) {
    cacheKey += "_" + language;
  }

  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = cache_.find(cacheKey);
    if (it != cache_.end()) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
          now - it->second.timestamp);

      // Check if cache is still valid (not expired)
      if (elapsed < it->second.ttl) {
        // Check if file has been modified since cache was created
        try {
          if (!it->second.filePath.empty() &&
              std::filesystem::exists(it->second.filePath)) {
            auto currentModTime =
                std::filesystem::last_write_time(it->second.filePath);
            if (currentModTime <= it->second.fileModTime) {
              // File hasn't changed, return cached content
              return it->second.content;
            }
            // File has changed, invalidate cache
          }
        } catch (...) {
          // If we can't check file time, use cache anyway (fallback)
          return it->second.content;
        }
      }
      // Cache expired or file changed, remove it
      cache_.erase(it);
    }
  }

  // Cache miss or expired, read from file
  // Priority: 1) api-specs/openapi/{lang}/openapi.yaml (if language specified), 2) api-specs/openapi.yaml (project structure), 3) Current working directory (service install dir), 4)
  // Environment variable, 5) Search up from current dir
  std::vector<std::filesystem::path> possiblePaths;

  // 1. If language is specified, check language-specific file first
  if (!language.empty() && validateLanguageCode(language)) {
    // Try multiple possible locations for the file
    std::vector<std::filesystem::path> testLocations;
    
    // Location 1: Project root (relative to source/build)
    try {
      std::filesystem::path currentDir = std::filesystem::current_path();
      // If we're in build directory, go up to project root
      if (currentDir.filename() == "build") {
        testLocations.push_back(currentDir.parent_path() / "api-specs" / "openapi" / language / "openapi.yaml");
      }
      // Also try current directory
      testLocations.push_back(currentDir / "api-specs" / "openapi" / language / "openapi.yaml");
    } catch (...) {
      // Ignore
    }
    
    // Location 2: Search up from current directory (for development)
    try {
      std::filesystem::path basePath = std::filesystem::canonical(std::filesystem::current_path());
      std::filesystem::path current = basePath;
      for (int i = 0; i < 4; ++i) {
        std::filesystem::path testPath = current / "api-specs" / "openapi" / language / "openapi.yaml";
        testLocations.push_back(testPath);
        if (current.has_parent_path() && current != current.parent_path()) {
          current = current.parent_path();
        } else {
          break;
        }
      }
    } catch (...) {
      // Ignore
    }
    
    // Check all test locations
    for (const auto& testPath : testLocations) {
      try {
        PLOG_INFO << "[OpenAPI] Checking language-specific file: " << testPath;
        if (std::filesystem::exists(testPath) &&
            std::filesystem::is_regular_file(testPath)) {
          PLOG_INFO << "[OpenAPI] Found language-specific file: " << testPath << " for language: " << language;
          possiblePaths.push_back(testPath);
          break; // Found it, no need to check other locations
        }
      } catch (const std::exception& e) {
        PLOG_WARNING << "[OpenAPI] Error checking " << testPath << ": " << e.what();
      } catch (...) {
        // Ignore
      }
    }
    
    if (possiblePaths.empty()) {
      PLOG_WARNING << "[OpenAPI] Language-specific file not found for language: " << language;
    }

    // Also check in install directory
    const char *installDir = std::getenv("OMNIAPI_INSTALL_DIR");
    if (installDir && installDir[0] != '\0') {
      try {
        std::filesystem::path installPath(installDir);
        std::filesystem::path testPath = installPath / "api-specs" / "openapi" / language / "openapi.yaml";
        if (std::filesystem::exists(testPath) &&
            std::filesystem::is_regular_file(testPath)) {
          possiblePaths.push_back(testPath);
        }
      } catch (...) {
        // Ignore errors
      }
    }
  }

  // 2. Check api-specs/openapi.yaml (project structure) - fallback if language-specific not found
  try {
    std::filesystem::path currentDir = std::filesystem::current_path();
    std::filesystem::path testPath = currentDir / "api-specs" / "openapi.yaml";
    if (std::filesystem::exists(testPath) &&
        std::filesystem::is_regular_file(testPath)) {
      possiblePaths.push_back(testPath);
    }
  } catch (...) {
    // Ignore errors
  }

  // 3. Check current working directory (for service installations)
  try {
    std::filesystem::path currentDir = std::filesystem::current_path();
    std::filesystem::path testPath = currentDir / "openapi.yaml";
    if (std::filesystem::exists(testPath) &&
        std::filesystem::is_regular_file(testPath)) {
      possiblePaths.push_back(testPath);
    }
  } catch (...) {
    // Ignore errors
  }

  // 4. Check environment variable for installation directory
  const char *installDir = std::getenv("OMNIAPI_INSTALL_DIR");
  if (installDir && installDir[0] != '\0') {
    try {
      std::filesystem::path installPath(installDir);
      std::filesystem::path testPath = installPath / "openapi.yaml";
      if (std::filesystem::exists(testPath) &&
          std::filesystem::is_regular_file(testPath)) {
        possiblePaths.push_back(testPath);
      }
      // Also check api-specs subdirectory
      std::filesystem::path testPathApiSpecs = installPath / "api-specs" / "openapi.yaml";
      if (std::filesystem::exists(testPathApiSpecs) &&
          std::filesystem::is_regular_file(testPathApiSpecs)) {
        possiblePaths.push_back(testPathApiSpecs);
      }
    } catch (...) {
      // Ignore errors
    }
  }

  // 5. Search up from current directory (for development)
  std::filesystem::path basePath;
  try {
    basePath = std::filesystem::canonical(std::filesystem::current_path());
  } catch (...) {
    basePath = std::filesystem::current_path();
  }

  std::filesystem::path current = basePath;
  for (int i = 0; i < 4; ++i) {
    // Check api-specs/openapi.yaml first
    std::filesystem::path testPathApiSpecs = current / "api-specs" / "openapi.yaml";
    if (std::filesystem::exists(testPathApiSpecs)) {
      if (std::filesystem::is_regular_file(testPathApiSpecs)) {
        // Avoid duplicates
        bool alreadyAdded = false;
        for (const auto &existing : possiblePaths) {
          try {
            if (std::filesystem::equivalent(testPathApiSpecs, existing)) {
              alreadyAdded = true;
              break;
            }
          } catch (...) {
            if (testPathApiSpecs == existing) {
              alreadyAdded = true;
              break;
            }
          }
        }
        if (!alreadyAdded) {
          possiblePaths.push_back(testPathApiSpecs);
        }
      }
    }

    // Check openapi.yaml in current directory
    std::filesystem::path testPath = current / "openapi.yaml";
    if (std::filesystem::exists(testPath)) {
      // Verify it's a regular file and not a symlink to prevent attacks
      if (std::filesystem::is_regular_file(testPath)) {
        // Avoid duplicates
        bool alreadyAdded = false;
        for (const auto &existing : possiblePaths) {
          try {
            if (std::filesystem::equivalent(testPath, existing)) {
              alreadyAdded = true;
              break;
            }
          } catch (...) {
            // If equivalent fails, compare as strings
            if (testPath == existing) {
              alreadyAdded = true;
              break;
            }
          }
        }
        if (!alreadyAdded) {
          possiblePaths.push_back(testPath);
        }
      }
    }

    if (current.has_parent_path() && current != current.parent_path()) {
      current = current.parent_path();
    } else {
      break;
    }
  }

  // Try to read from found paths
  PLOG_INFO << "[OpenAPI] Found " << possiblePaths.size() << " possible paths for version: " << version << ", language: " << language;
  for (size_t i = 0; i < possiblePaths.size(); ++i) {
    PLOG_INFO << "[OpenAPI] Path " << (i+1) << ": " << possiblePaths[i];
  }
  
  std::string yamlContent;
  std::filesystem::path actualFilePath;
  std::filesystem::file_time_type fileModTime;

  for (const auto &path : possiblePaths) {
    try {
      // Use canonical path to ensure no symlink attacks
      std::filesystem::path canonicalPath = std::filesystem::canonical(path);
      PLOG_INFO << "[OpenAPI] Trying to read file: " << canonicalPath;

      std::ifstream file(canonicalPath);
      if (file.is_open() && file.good()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        yamlContent = buffer.str();
        if (!yamlContent.empty()) {
          PLOG_INFO << "[OpenAPI] Successfully read file: " << canonicalPath << ", size: " << yamlContent.length();
          // Log first few characters to verify language
          if (yamlContent.length() > 200) {
            std::string preview = yamlContent.substr(0, 200);
            PLOG_INFO << "[OpenAPI] File preview (first 200 chars): " << preview;
            // Check if it contains Vietnamese text
            if (yamlContent.find("Kiểm tra") != std::string::npos || 
                yamlContent.find("trạng thái") != std::string::npos) {
              PLOG_INFO << "[OpenAPI] File contains Vietnamese text - language: " << language;
            } else if (yamlContent.find("Health check") != std::string::npos ||
                       yamlContent.find("Returns the health") != std::string::npos) {
              PLOG_WARNING << "[OpenAPI] File contains English text but language requested is: " << language;
            }
          }
          actualFilePath = canonicalPath;
          fileModTime = std::filesystem::last_write_time(canonicalPath);
          break;
        } else {
          PLOG_WARNING << "[OpenAPI] File is empty: " << canonicalPath;
        }
      } else {
        PLOG_WARNING << "[OpenAPI] Cannot open file: " << canonicalPath;
      }
    } catch (const std::exception& e) {
      PLOG_ERROR << "[OpenAPI] Error reading file " << path << ": " << e.what();
    } catch (...) {
      PLOG_ERROR << "[OpenAPI] Unknown error reading file: " << path;
    }
  }

  if (yamlContent.empty()) {
    throw std::runtime_error("OpenAPI specification file not found");
  }

  // Update server URLs from environment variables
  // Use requestHost if provided (for browser-accessible URL), otherwise use env
  // vars
  std::string updatedContent =
      updateOpenAPIServerURLs(yamlContent, requestHost);

  // If version is specified, filter the content
  std::string finalContent = updatedContent;
  if (!version.empty()) {
    finalContent = filterOpenAPIByVersion(updatedContent, version);
  }

  // Update cache with file path and modification time
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    CacheEntry entry;
    entry.content = finalContent;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.ttl = cache_ttl_;
    entry.filePath = actualFilePath;
    entry.fileModTime = fileModTime;
    cache_[cacheKey] = entry;
  }

  return finalContent;
}

std::string
SwaggerHandler::filterOpenAPIByVersion(const std::string &yamlContent,
                                       const std::string &version) const {
  std::stringstream result;
  std::istringstream input(yamlContent);
  std::string line;
  bool inPaths = false;
  bool skipPath = false;
  std::string currentPath = "";

  // Version prefix to match (e.g., "/v1/")
  std::string versionPrefix = "/" + version + "/";

  while (std::getline(input, line)) {
    // Check if we're entering the paths section
    if (line.find("paths:") != std::string::npos) {
      inPaths = true;
      result << line << "\n";
      continue;
    }

    if (!inPaths) {
      // Before paths section, just copy everything
      result << line << "\n";
      continue;
    }

    // Calculate indentation level (count leading spaces)
    size_t indent = 0;
    while (indent < line.length() && line[indent] == ' ') {
      indent++;
    }

    // Check if this is a path definition (starts with / at indent level 2)
    if (indent == 2 && line.find("  /") == 0) {
      // This is a new path definition
      size_t colonPos = line.find(':');
      if (colonPos != std::string::npos) {
        currentPath =
            line.substr(2, colonPos - 2); // Extract path without leading spaces

        // Check if this path belongs to the requested version
        skipPath = (currentPath.find(versionPrefix) != 0);

        if (!skipPath) {
          result << line << "\n";
        }
      } else {
        if (!skipPath) {
          result << line << "\n";
        }
      }
    } else if (skipPath) {
      // Skip lines that are part of a path we don't want
      // Only include if we hit a new path at indent level 2 or less
      if (indent <= 2 && line.find("  /") == 0) {
        // This is a new path, reset skipPath and process it
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
          currentPath = line.substr(2, colonPos - 2);
          skipPath = (currentPath.find(versionPrefix) != 0);
          if (!skipPath) {
            result << line << "\n";
          }
        }
      }
      // Otherwise skip this line
    } else {
      // Include this line (it's part of a path we want)
      result << line << "\n";
    }
  }

  return result.str();
}

std::string
SwaggerHandler::updateOpenAPIServerURLs(const std::string &yamlContent,
                                        const std::string &requestHost) const {
  std::string serverUrl;

  // If requestHost is provided, use it (browser-accessible URL)
  if (!requestHost.empty()) {
    // Extract host and port from requestHost (format: "host:port" or "host")
    std::string host = requestHost;
    std::string portStr = "";

    size_t colonPos = requestHost.find(':');
    if (colonPos != std::string::npos) {
      host = requestHost.substr(0, colonPos);
      portStr = requestHost.substr(colonPos + 1);
    }

    // Determine scheme (http or https)
    std::string scheme = "http";
    const char *https_env = std::getenv("API_HTTPS");
    if (https_env &&
        (std::string(https_env) == "1" || std::string(https_env) == "true")) {
      scheme = "https";
    }

    // Build server URL from request host
    serverUrl = scheme + "://" + host;
    if (!portStr.empty()) {
      serverUrl += ":" + portStr;
    }
  } else {
    // Fallback to environment variables
    std::string host = EnvConfig::getString("API_HOST", "0.0.0.0");
    uint16_t port =
        static_cast<uint16_t>(EnvConfig::getInt("API_PORT", 8080, 1, 65535));

    // If host is 0.0.0.0, use localhost for browser compatibility
    if (host == "0.0.0.0") {
      host = "localhost";
    }

    // Determine scheme (http or https)
    std::string scheme = "http";
    const char *https_env = std::getenv("API_HTTPS");
    if (https_env &&
        (std::string(https_env) == "1" || std::string(https_env) == "true")) {
      scheme = "https";
    }

    // Build server URL
    serverUrl = scheme + "://" + host;
    if ((scheme == "http" && port != 80) ||
        (scheme == "https" && port != 443)) {
      serverUrl += ":" + std::to_string(port);
    }
  }

  // Replace server URLs in OpenAPI spec
  // Pattern: url: http://localhost:8080 or url: http://0.0.0.0:8080
  std::string result = yamlContent;

  // Replace all server URL patterns
  // Match: "  - url: http://..." or "  url: http://..."
  std::regex urlPattern(R"((\s+-\s+url:\s+)(https?://[^\s]+))");
  result = std::regex_replace(result, urlPattern, "$1" + serverUrl);

  // Also handle case without dash (single server)
  std::regex urlPattern2(R"((\s+url:\s+)(https?://[^\s]+))");
  result = std::regex_replace(result, urlPattern2, "$1" + serverUrl);

  return result;
}

void SwaggerHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
  resp->addHeader("Access-Control-Max-Age", "3600");

  // Record metrics and call callback
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

// Helper function to convert YAML::Node to Json::Value
#ifdef YAML_CPP_FOUND
Json::Value yamlNodeToJsonValue(const YAML::Node &node) {
  Json::Value jsonValue;

  if (node.IsNull()) {
    jsonValue = Json::nullValue;
  } else if (node.IsScalar()) {
    // Try to determine the type and convert appropriately
    std::string scalar = node.as<std::string>();
    
    // Try to parse as number
    try {
      if (scalar.find('.') != std::string::npos) {
        // Try double
        double d = node.as<double>();
        jsonValue = d;
      } else {
        // Try int first, then fallback to string
        try {
          int64_t i = node.as<int64_t>();
          jsonValue = static_cast<Json::Int64>(i);
        } catch (...) {
          // Try bool
          if (scalar == "true" || scalar == "True") {
            jsonValue = true;
          } else if (scalar == "false" || scalar == "False") {
            jsonValue = false;
          } else {
            jsonValue = scalar;
          }
        }
      }
    } catch (...) {
      // Fallback to string
      jsonValue = scalar;
    }
  } else if (node.IsSequence()) {
    jsonValue = Json::arrayValue;
    for (const auto &item : node) {
      jsonValue.append(yamlNodeToJsonValue(item));
    }
  } else if (node.IsMap()) {
    jsonValue = Json::objectValue;
    for (const auto &pair : node) {
      std::string key = pair.first.as<std::string>();
      jsonValue[key] = yamlNodeToJsonValue(pair.second);
    }
  }

  return jsonValue;
}
#endif

std::string SwaggerHandler::yamlToJson(const std::string &yamlContent) const {
#ifdef YAML_CPP_FOUND
  try {
    // Parse YAML
    YAML::Node yamlNode;
    try {
      yamlNode = YAML::Load(yamlContent);
    } catch (const YAML::Exception &e) {
      PLOG_ERROR << "[OpenAPI] Failed to parse YAML: " << e.what();
      return "";
    }

    // Convert YAML::Node to Json::Value
    Json::Value jsonValue = yamlNodeToJsonValue(yamlNode);

    // Serialize to JSON string
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    builder["commentStyle"] = "None";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    std::ostringstream oss;
    writer->write(jsonValue, &oss);
    
    return oss.str();
  } catch (const std::exception &e) {
    PLOG_ERROR << "[OpenAPI] Exception in yamlToJson: " << e.what();
    return "";
  } catch (...) {
    PLOG_ERROR << "[OpenAPI] Unknown exception in yamlToJson";
    return "";
  }
#else
  PLOG_ERROR << "[OpenAPI] yaml-cpp not available. Cannot convert YAML to JSON.";
  PLOG_ERROR << "[OpenAPI] Please install yaml-cpp or enable AUTO_DOWNLOAD_DEPENDENCIES";
  return "";
#endif
}

