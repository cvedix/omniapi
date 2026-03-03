#include "api/scalar_handler.h"
#include "core/logger.h"
#include "core/metrics_interceptor.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <fstream>
#include <jsoncpp/json/json.h>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <vector>

void ScalarHandler::getScalarDocument(
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
    
    // Get language from URL parameter if present
    std::string language = req->getParameter("lang");
    PLOG_INFO << "[Scalar] Request path: " << path;
    PLOG_INFO << "[Scalar] Language from getParameter('lang'): " << (language.empty() ? "(empty)" : language);
    
    if (language.empty()) {
      // Try to get from query string
      auto queryParams = req->getParameters();
      PLOG_INFO << "[Scalar] Query params count: " << queryParams.size();
      for (const auto& [key, value] : queryParams) {
        PLOG_INFO << "[Scalar] Query param: " << key << " = " << value;
      }
      auto it = queryParams.find("lang");
      if (it != queryParams.end()) {
        language = it->second;
        PLOG_INFO << "[Scalar] Language from query params: " << language;
      }
    }
    
    // Validate language
    if (!language.empty() && language != "en" && language != "vi") {
      PLOG_WARNING << "[Scalar] Invalid language: " << language << ", defaulting to 'en'";
      language = "en"; // Default to English if invalid
    }
    if (language.empty()) {
      PLOG_INFO << "[Scalar] No language specified, defaulting to 'en'";
      language = "en"; // Default to English
    }
    
    PLOG_INFO << "[Scalar] Final language: " << language;
    PLOG_INFO << "[Scalar] Base URL: " << baseUrl;
    PLOG_INFO << "[Scalar] Version: " << (version.empty() ? "(empty)" : version);
    
    std::string html = generateScalarDocumentHTML(version, baseUrl, language);
    
    PLOG_INFO << "[Scalar] Generated HTML length: " << html.length();
    // Log if initial spec URL placeholders were replaced
    if (html.find("{{INITIAL_SPEC_URL}}") != std::string::npos) {
      PLOG_WARNING << "[Scalar] WARNING: {{INITIAL_SPEC_URL}} placeholder not replaced!";
    } else {
      PLOG_INFO << "[Scalar] {{INITIAL_SPEC_URL}} placeholder replaced successfully";
    }
    if (html.find("{{INITIAL_SPEC_URL_ATTR}}") != std::string::npos) {
      PLOG_WARNING << "[Scalar] WARNING: {{INITIAL_SPEC_URL_ATTR}} placeholder not replaced!";
    } else {
      PLOG_INFO << "[Scalar] {{INITIAL_SPEC_URL_ATTR}} placeholder replaced successfully";
    }

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

void ScalarHandler::getScalarCSS(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    std::string cssContent = readScalarCSSFile();
    
    auto resp = HttpResponse::newHttpResponse();
    
    if (cssContent.empty()) {
      // CSS file not found locally, return 404 or fetch from CDN
      // For now, return 404 - user needs to download CSS file
      resp->setStatusCode(k404NotFound);
      resp->setBody("CSS file not found. Please download standalone.css from https://cdn.jsdelivr.net/npm/@scalar/api-reference@1.24.0/dist/browser/standalone.css and save it to api-specs/scalar/standalone.css");
    } else {
      resp->setStatusCode(k200OK);
      resp->setBody(cssContent);
      // Set correct MIME type for CSS
      resp->addHeader("Content-Type", "text/css; charset=utf-8");
      // Add cache headers
      resp->addHeader("Cache-Control", "public, max-age=3600");
    }

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

void ScalarHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
  resp->addHeader("Access-Control-Max-Age", "3600");
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

std::string
ScalarHandler::extractVersionFromPath(const std::string &path) const {
  // Match patterns like /v1/document, /v2/document, etc.
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

bool ScalarHandler::validateVersionFormat(const std::string &version) const {
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

bool ScalarHandler::validateLanguageCode(const std::string &lang) const {
  return lang == "en" || lang == "vi";
}

std::string ScalarHandler::readScalarHTMLFile() const {
  // Try to find api-specs/scalar/index.html
  std::vector<std::filesystem::path> possiblePaths;

  // 1. Check api-specs/scalar/index.html first (project structure)
  try {
    std::filesystem::path currentDir = std::filesystem::current_path();
    std::filesystem::path testPath = currentDir / "api-specs" / "scalar" / "index.html";
    if (std::filesystem::exists(testPath) &&
        std::filesystem::is_regular_file(testPath)) {
      possiblePaths.push_back(testPath);
    }
  } catch (...) {
    // Ignore errors
  }

  // 2. Check environment variable for installation directory
  const char *installDir = std::getenv("EDGEOS_API_INSTALL_DIR");
  if (!installDir || installDir[0] == '\0')
    installDir = std::getenv("EDGE_AI_API_INSTALL_DIR");
  if (installDir && installDir[0] != '\0') {
    try {
      std::filesystem::path installPath(installDir);
      std::filesystem::path testPath = installPath / "api-specs" / "scalar" / "index.html";
      if (std::filesystem::exists(testPath) &&
          std::filesystem::is_regular_file(testPath)) {
        possiblePaths.push_back(testPath);
      }
    } catch (...) {
      // Ignore errors
    }
  }

  // 3. Search up from current directory (for development)
  std::filesystem::path basePath;
  try {
    basePath = std::filesystem::canonical(std::filesystem::current_path());
  } catch (...) {
    basePath = std::filesystem::current_path();
  }

  std::filesystem::path current = basePath;
  for (int i = 0; i < 4; ++i) {
    std::filesystem::path testPath = current / "api-specs" / "scalar" / "index.html";
    if (std::filesystem::exists(testPath) &&
        std::filesystem::is_regular_file(testPath)) {
      possiblePaths.push_back(testPath);
    }

    if (current.has_parent_path() && current != current.parent_path()) {
      current = current.parent_path();
    } else {
      break;
    }
  }

  // Try to read from found paths
  for (const auto &path : possiblePaths) {
    try {
      std::filesystem::path canonicalPath = std::filesystem::canonical(path);
      std::ifstream file(canonicalPath);
      if (file.is_open() && file.good()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        std::string htmlContent = buffer.str();
        if (!htmlContent.empty()) {
          return htmlContent;
        }
      }
    } catch (...) {
      // Continue to next path
    }
  }

  // Return empty string if file not found (fallback to hardcoded HTML)
  return "";
}

std::string ScalarHandler::readScalarCSSFile() const {
  // Try to find api-specs/scalar/standalone.css
  std::vector<std::filesystem::path> possiblePaths;

  // 1. Check api-specs/scalar/standalone.css first (project structure)
  try {
    std::filesystem::path currentDir = std::filesystem::current_path();
    std::filesystem::path testPath = currentDir / "api-specs" / "scalar" / "standalone.css";
    if (std::filesystem::exists(testPath) &&
        std::filesystem::is_regular_file(testPath)) {
      possiblePaths.push_back(testPath);
    }
  } catch (...) {
    // Ignore errors
  }

  // 2. Check environment variable for installation directory
  const char *installDir = std::getenv("EDGEOS_API_INSTALL_DIR");
  if (!installDir || installDir[0] == '\0')
    installDir = std::getenv("EDGE_AI_API_INSTALL_DIR");
  if (installDir && installDir[0] != '\0') {
    try {
      std::filesystem::path installPath(installDir);
      std::filesystem::path testPath = installPath / "api-specs" / "scalar" / "standalone.css";
      if (std::filesystem::exists(testPath) &&
          std::filesystem::is_regular_file(testPath)) {
        possiblePaths.push_back(testPath);
      }
    } catch (...) {
      // Ignore errors
    }
  }

  // 3. Search up from current directory (for development)
  std::filesystem::path basePath;
  try {
    basePath = std::filesystem::canonical(std::filesystem::current_path());
  } catch (...) {
    basePath = std::filesystem::current_path();
  }

  std::filesystem::path current = basePath;
  for (int i = 0; i < 4; ++i) {
    std::filesystem::path testPath = current / "api-specs" / "scalar" / "standalone.css";
    if (std::filesystem::exists(testPath) &&
        std::filesystem::is_regular_file(testPath)) {
      possiblePaths.push_back(testPath);
    }

    if (current.has_parent_path() && current != current.parent_path()) {
      current = current.parent_path();
    } else {
      break;
    }
  }

  // Try to read from found paths
  for (const auto &path : possiblePaths) {
    try {
      std::filesystem::path canonicalPath = std::filesystem::canonical(path);
      std::ifstream file(canonicalPath);
      if (file.is_open() && file.good()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        std::string cssContent = buffer.str();
        if (!cssContent.empty()) {
          return cssContent;
        }
      }
    } catch (...) {
      // Continue to next path
    }
  }

  // Return empty string if file not found
  return "";
}

std::string
ScalarHandler::generateScalarDocumentHTML(const std::string &version,
                                         const std::string &baseUrl,
                                         const std::string &language) const {
  // Try to read HTML from file first
  std::string html = readScalarHTMLFile();
  
  if (!html.empty()) {
    // Determine the OpenAPI spec URL based on version
    // Default to English, but will be updated by JavaScript based on language selector
    std::string specUrl = "/openapi.yaml";
    if (!version.empty()) {
      specUrl = "/" + version + "/openapi.yaml";
    }

    // Use absolute URL if baseUrl is provided, otherwise use relative URL
    std::string fullSpecUrl = specUrl;
    std::string cleanBaseUrl = baseUrl;
    if (!baseUrl.empty()) {
      if (cleanBaseUrl.back() == '/') {
        cleanBaseUrl.pop_back();
      }
      fullSpecUrl = cleanBaseUrl + specUrl;
    }

    // Build initial spec URL based on language parameter
    std::string lang = language.empty() ? "en" : language;
    if (lang != "en" && lang != "vi") {
      PLOG_WARNING << "[Scalar] Invalid language in generateScalarDocumentHTML: " << lang << ", defaulting to 'en'";
      lang = "en"; // Default to English if invalid
    }
    std::string initialSpecUrl = fullSpecUrl;
    if (!version.empty()) {
      initialSpecUrl = cleanBaseUrl + "/" + version + "/openapi/" + lang + "/openapi.yaml";
    } else {
      initialSpecUrl = cleanBaseUrl + "/openapi/" + lang + "/openapi.yaml";
    }
    
    // Build CSS URL - use local endpoint instead of CDN
    std::string cssUrl = "/v1/scalar/standalone.css";
    if (!version.empty()) {
      cssUrl = "/" + version + "/scalar/standalone.css";
    }
    
    PLOG_INFO << "[Scalar] Initial spec URL: " << initialSpecUrl;
    PLOG_INFO << "[Scalar] Language for initial URL: " << lang;
    PLOG_INFO << "[Scalar] CSS URL: " << cssUrl;

    // Replace placeholders in HTML
    // For JSON script tag, we need proper JSON strings
    std::string baseUrlJson = baseUrl.empty() ? "window.location.origin" : "\"" + cleanBaseUrl + "\"";
    std::string versionJson = version.empty() ? "\"\"" : "\"" + version + "\"";
    std::string fallbackUrlJson = "\"" + fullSpecUrl + "\"";
    std::string initialSpecUrlJson = "\"" + initialSpecUrl + "\"";
    std::string cssUrlJson = "\"" + cssUrl + "\"";
    
    // Replace placeholders - use replace_all equivalent
    std::string placeholder;
    
    // Replace {{BASE_URL}}
    placeholder = "{{BASE_URL}}";
    size_t pos = 0;
    while ((pos = html.find(placeholder, pos)) != std::string::npos) {
      html.replace(pos, placeholder.length(), baseUrlJson);
      pos += baseUrlJson.length();
    }
    
    // Replace {{VERSION}}
    placeholder = "{{VERSION}}";
    pos = 0;
    while ((pos = html.find(placeholder, pos)) != std::string::npos) {
      html.replace(pos, placeholder.length(), versionJson);
      pos += versionJson.length();
    }
    
    // Replace {{FALLBACK_URL}}
    placeholder = "{{FALLBACK_URL}}";
    pos = 0;
    while ((pos = html.find(placeholder, pos)) != std::string::npos) {
      html.replace(pos, placeholder.length(), fallbackUrlJson);
      pos += fallbackUrlJson.length();
    }
    
    // Replace {{INITIAL_SPEC_URL_ATTR}} - for HTML attributes (no quotes)
    placeholder = "{{INITIAL_SPEC_URL_ATTR}}";
    pos = 0;
    while ((pos = html.find(placeholder, pos)) != std::string::npos) {
      html.replace(pos, placeholder.length(), initialSpecUrl);
      pos += initialSpecUrl.length();
    }
    
    // Replace {{INITIAL_SPEC_URL}} - for JSON/JavaScript (with quotes)
    placeholder = "{{INITIAL_SPEC_URL}}";
    pos = 0;
    while ((pos = html.find(placeholder, pos)) != std::string::npos) {
      html.replace(pos, placeholder.length(), initialSpecUrlJson);
      pos += initialSpecUrlJson.length();
    }
    
    // Replace {{CSS_URL}} - use local endpoint instead of CDN
    placeholder = "{{CSS_URL}}";
    pos = 0;
    while ((pos = html.find(placeholder, pos)) != std::string::npos) {
      html.replace(pos, placeholder.length(), cssUrlJson);
      pos += cssUrlJson.length();
    }
    
    // Also replace CDN URL directly if placeholder not found
    std::string cdnCssUrl = "https://cdn.jsdelivr.net/npm/@scalar/api-reference@1.24.0/dist/browser/standalone.css";
    pos = 0;
    while ((pos = html.find(cdnCssUrl, pos)) != std::string::npos) {
      html.replace(pos, cdnCssUrl.length(), cssUrl);
      pos += cssUrl.length();
    }
    
    return html;
  }

  // Fallback to hardcoded HTML if file not found
  // Determine the OpenAPI spec URL based on version
  std::string specUrl = "/openapi.yaml";
  std::string title = "Edge AI API - Scalar Documentation";

  if (!version.empty()) {
    specUrl = "/" + version + "/openapi.yaml";
    title = "Edge AI API " + version + " - Scalar Documentation";
  }

  // Use absolute URL if baseUrl is provided, otherwise use relative URL
  std::string fullSpecUrl = specUrl;
  std::string cleanBaseUrl = baseUrl;
  if (!baseUrl.empty()) {
    // Remove trailing slash from baseUrl if present
    if (cleanBaseUrl.back() == '/') {
      cleanBaseUrl.pop_back();
    }
    fullSpecUrl = cleanBaseUrl + specUrl;
  }

  // Build initial spec URL based on language parameter
  std::string lang = language.empty() ? "en" : language;
  if (lang != "en" && lang != "vi") {
    lang = "en"; // Default to English if invalid
  }
  std::string initialSpecUrl = fullSpecUrl;
  if (!version.empty() && !cleanBaseUrl.empty()) {
    initialSpecUrl = cleanBaseUrl + "/" + version + "/openapi/" + lang + "/openapi.yaml";
  } else if (!cleanBaseUrl.empty()) {
    initialSpecUrl = cleanBaseUrl + "/openapi/" + lang + "/openapi.yaml";
  }

  // Build CSS URL - use local endpoint instead of CDN
  std::string cssUrl = "/v1/scalar/standalone.css";
  if (!version.empty()) {
    cssUrl = "/" + version + "/scalar/standalone.css";
  }

  // Scalar API Reference HTML - using new script tag API format
  std::string fallbackHtml = R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)" + title +
                     R"(</title>
    <!-- Import Scalar CSS directly (required for v1.24.0+) -->
    <!-- Use local endpoint to avoid MIME type issues from CDN -->
    <link rel="stylesheet" href=")" + cssUrl + R"(">
    <!-- Preconnect to CDN for faster loading -->
    <link rel="preconnect" href="https://cdn.jsdelivr.net">
    <link rel="dns-prefetch" href="https://cdn.jsdelivr.net">
    <style>
        body {
            margin: 0;
            padding: 0;
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
        }
        .language-selector {
            position: fixed;
            top: 20px;
            right: 20px;
            z-index: 10000;
            background: white;
            padding: 10px 15px;
            border-radius: 8px;
            box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);
            display: flex;
            align-items: center;
            gap: 10px;
        }
        .language-selector label {
            font-weight: 600;
            font-size: 14px;
            color: #333;
        }
        .language-selector select {
            padding: 6px 12px;
            border: 1px solid #ddd;
            border-radius: 4px;
            font-size: 14px;
            cursor: pointer;
            background: white;
        }
        .language-selector select:hover {
            border-color: #999;
        }
        .language-selector select:focus {
            outline: none;
            border-color: #0066cc;
            box-shadow: 0 0 0 2px rgba(0, 102, 204, 0.1);
        }
        #scalar-api-reference {
            position: relative;
            z-index: 1;
            min-height: 100vh;
            width: 100%;
            display: block !important;
            visibility: visible !important;
            opacity: 1 !important;
        }
    </style>
</head>
<body>
    <div class="language-selector">
        <label for="language-select">Language / Ngôn ngữ:</label>
        <select id="language-select">
            <option value="en">English</option>
            <option value="vi">Tiếng Việt</option>
        </select>
    </div>
    <!-- Container for Scalar API Reference -->
    <div id="scalar-api-reference"></div>
    
    <!-- Load Scalar script (using official method from GitHub) -->
    <script src="https://cdn.jsdelivr.net/npm/@scalar/api-reference@1.24.0"></script>
    
    <!-- Initialize Scalar using official API -->
    <script>
        Scalar.createApiReference('#scalar-api-reference', {
            url: ')" + initialSpecUrl + R"('
        });
    </script>
</body>
</html>
)";

  return fallbackHtml;
}

void ScalarHandler::listExampleBodies(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    std::string examplesDir = getExamplesInstancesDir();
    std::filesystem::path dirPath(examplesDir);
    std::filesystem::path basePath(examplesDir);

    if (!std::filesystem::exists(dirPath) ||
        !std::filesystem::is_directory(dirPath)) {
      Json::Value response(Json::objectValue);
      response["error"] = "Examples directory not found";
      response["message"] = "Examples directory does not exist: " + examplesDir;
      auto resp = HttpResponse::newHttpJsonResponse(response);
      resp->setStatusCode(k404NotFound);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    std::vector<std::string> files = listExampleFiles(dirPath, basePath);

    Json::Value response(Json::objectValue);
    Json::Value examples(Json::arrayValue);
    for (const auto &file : files) {
      examples.append(file);
    }
    response["examples"] = examples;
    response["count"] = static_cast<int>(files.size());

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    Json::Value response(Json::objectValue);
    response["error"] = "Internal server error";
    response["message"] = e.what();
    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k500InternalServerError);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (...) {
    Json::Value response(Json::objectValue);
    response["error"] = "Internal server error";
    response["message"] = "Unknown error occurred";
    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k500InternalServerError);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void ScalarHandler::getExampleBody(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    std::string filePath = req->getParameter("path");
    if (filePath.empty()) {
      Json::Value response(Json::objectValue);
      response["error"] = "Bad request";
      response["message"] = "File path parameter is required";
      auto resp = HttpResponse::newHttpJsonResponse(response);
      resp->setStatusCode(k400BadRequest);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Sanitize path to prevent directory traversal
    std::string sanitizedPath = sanitizePath(filePath);
    if (sanitizedPath.empty()) {
      Json::Value response(Json::objectValue);
      response["error"] = "Bad request";
      response["message"] = "Invalid file path";
      auto resp = HttpResponse::newHttpJsonResponse(response);
      resp->setStatusCode(k400BadRequest);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    std::string examplesDir = getExamplesInstancesDir();
    std::filesystem::path fullPath =
        std::filesystem::path(examplesDir) / sanitizedPath;

    // Ensure the path is within examples directory (prevent directory traversal)
    std::filesystem::path canonicalExamplesDir =
        std::filesystem::canonical(examplesDir);
    std::filesystem::path canonicalFullPath =
        std::filesystem::canonical(fullPath);

    if (canonicalFullPath.string().find(canonicalExamplesDir.string()) != 0) {
      Json::Value response(Json::objectValue);
      response["error"] = "Forbidden";
      response["message"] = "Access denied: path outside examples directory";
      auto resp = HttpResponse::newHttpJsonResponse(response);
      resp->setStatusCode(k403Forbidden);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    if (!std::filesystem::exists(fullPath) ||
        !std::filesystem::is_regular_file(fullPath)) {
      Json::Value response(Json::objectValue);
      response["error"] = "Not found";
      response["message"] = "Example file not found: " + filePath;
      auto resp = HttpResponse::newHttpJsonResponse(response);
      resp->setStatusCode(k404NotFound);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Read file content
    std::ifstream file(fullPath);
    if (!file.is_open()) {
      Json::Value response(Json::objectValue);
      response["error"] = "Internal server error";
      response["message"] = "Cannot open file: " + filePath;
      auto resp = HttpResponse::newHttpJsonResponse(response);
      resp->setStatusCode(k500InternalServerError);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    // Parse JSON to validate it
    Json::Value jsonContent;
    Json::Reader reader;
    if (!reader.parse(content, jsonContent)) {
      Json::Value response(Json::objectValue);
      response["error"] = "Invalid JSON";
      response["message"] = "File does not contain valid JSON";
      auto resp = HttpResponse::newHttpJsonResponse(response);
      resp->setStatusCode(k400BadRequest);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Return the JSON content
    auto resp = HttpResponse::newHttpJsonResponse(jsonContent);
    resp->setStatusCode(k200OK);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    Json::Value response(Json::objectValue);
    response["error"] = "Internal server error";
    response["message"] = e.what();
    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k500InternalServerError);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (...) {
    Json::Value response(Json::objectValue);
    response["error"] = "Internal server error";
    response["message"] = "Unknown error occurred";
    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k500InternalServerError);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

std::string ScalarHandler::getExamplesInstancesDir() const {
  // Try multiple paths to find examples/instances directory
  std::vector<std::string> possiblePaths = {
      "./examples/instances",
      "../examples/instances",
      "../../examples/instances",
      "/opt/edgeos-api/examples/instances",
  };

  // Also try relative to executable path
  try {
    char exePath[1024];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len != -1) {
      exePath[len] = '\0';
      std::filesystem::path exe(exePath);
      std::filesystem::path exeDir = exe.parent_path();

      // Try going up from bin/ or build/bin/
      for (int i = 0; i < 5; i++) {
        std::filesystem::path testPath = exeDir;
        for (int j = 0; j < i; j++) {
          testPath = testPath.parent_path();
        }
        std::filesystem::path examplesPath =
            testPath / "examples" / "instances";
        if (std::filesystem::exists(examplesPath) &&
            std::filesystem::is_directory(examplesPath)) {
          return examplesPath.string();
        }
      }
    }
  } catch (...) {
    // Ignore errors, continue with other paths
  }

  // Try current directory and common paths
  for (const auto &path : possiblePaths) {
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
      return path;
    }
  }

  // Default fallback
  return "./examples/instances";
}

std::vector<std::string>
ScalarHandler::listExampleFiles(const std::filesystem::path &dir,
                                 const std::filesystem::path &basePath) const {
  std::vector<std::string> files;

  try {
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
      return files;
    }

    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".json") {
        // Get relative path from basePath
        std::filesystem::path relativePath =
            std::filesystem::relative(entry.path(), basePath);
        std::string pathStr = relativePath.string();
        // Convert backslashes to forward slashes for consistency
        std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
        files.push_back(pathStr);
      }
    }
  } catch (const std::exception &e) {
    PLOG_ERROR << "[Scalar] Error listing example files: " << e.what();
  } catch (...) {
    PLOG_ERROR << "[Scalar] Unknown error listing example files";
  }

  // Sort files for consistent ordering
  std::sort(files.begin(), files.end());

  return files;
}

std::string ScalarHandler::sanitizePath(const std::string &path) const {
  // Prevent path traversal attacks
  // Only allow simple filenames without directory traversal
  if (path.empty()) {
    return "";
  }

  // Check for path traversal patterns
  if (path.find("..") != std::string::npos ||
      path.find("/") != std::string::npos ||
      path.find("\\") != std::string::npos) {
    // Allow forward slashes for subdirectories, but check for .. patterns
    if (path.find("..") != std::string::npos) {
      return "";
    }
    // Allow forward slashes for relative paths within examples directory
  }

  // Only allow alphanumeric, dash, underscore, dot, forward slash
  for (char c : path) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' &&
        c != '.' && c != '/') {
      return "";
    }
  }

  return path;
}

