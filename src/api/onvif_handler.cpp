#include "api/onvif_handler.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/metrics_interceptor.h"
#include "core/onvif_camera_registry.h"
#include "core/onvif_credentials_manager.h"
#include "core/onvif_discovery.h"
#include "core/onvif_stream_manager.h"
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <regex>
#include <sstream>

void ONVIFHandler::discoverCameras(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/onvif/discover - Discover ONVIF cameras";
  }

  try {
    // Get timeout parameter (optional)
    int timeout = 5;
    auto timeoutParam = req->getParameter("timeout");
    if (!timeoutParam.empty()) {
      try {
        timeout = std::stoi(timeoutParam);
        if (timeout < 1) {
          timeout = 1;
        }
        if (timeout > 30) {
          timeout = 30;
        }
      } catch (const std::exception &e) {
        // Use default timeout
      }
    }

    // Start discovery
    ONVIFDiscovery discovery;
    discovery.discoverCamerasAsync(timeout);

    // Return 204 No Content (discovery started/completed)
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/onvif/discover - Discovery started";
    }

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/onvif/discover - Error: " << e.what();
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void ONVIFHandler::getCameras(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/onvif/cameras - Get all cameras";
  }

  try {
    auto &registry = ONVIFCameraRegistry::getInstance();
    std::vector<ONVIFCamera> cameras = registry.getAllCameras();

    Json::Value response(Json::arrayValue);
    for (const auto &camera : cameras) {
      response.append(cameraToJson(camera));
    }

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/onvif/cameras - Found " << cameras.size()
                << " cameras";
    }

    callback(createSuccessResponse(response));
  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/onvif/cameras - Error: " << e.what();
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void ONVIFHandler::getStreams(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  std::string cameraId = req->getParameter("cameraid");
  
  // Fallback: extract from path if getParameter doesn't work
  if (cameraId.empty()) {
    std::string path = req->getPath();
    // Pattern: /v1/onvif/streams/{cameraid}
    size_t streamsPos = path.find("/streams/");
    if (streamsPos != std::string::npos) {
      size_t start = streamsPos + 9; // length of "/streams/"
      size_t end = path.find("/", start);
      if (end == std::string::npos) {
        end = path.length();
      }
      cameraId = path.substr(start, end - start);
    }
  }
  
  // URL decode camera ID (in case it's encoded)
  if (!cameraId.empty()) {
    std::string decoded;
    decoded.reserve(cameraId.length());
    for (size_t i = 0; i < cameraId.length(); ++i) {
      if (cameraId[i] == '%' && i + 2 < cameraId.length()) {
        // Try to decode hex value
        char hex[3] = {cameraId[i + 1], cameraId[i + 2], '\0'};
        char *end;
        unsigned long value = std::strtoul(hex, &end, 16);
        if (*end == '\0' && value <= 255) {
          decoded += static_cast<char>(value);
          i += 2; // Skip the hex digits
        } else {
          decoded += cameraId[i]; // Invalid encoding, keep as-is
        }
      } else {
        decoded += cameraId[i];
      }
    }
    cameraId = decoded;
  }
  
  if (cameraId.empty()) {
    callback(createErrorResponse(400, "Bad request", "Missing cameraid parameter"));
    return;
  }

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/onvif/streams/" << cameraId
              << " - Get streams for camera";
  }

  try {
    ONVIFStreamManager streamManager;
    std::vector<ONVIFStream> streams = streamManager.getStreams(cameraId);

    Json::Value response(Json::arrayValue);
    for (const auto &stream : streams) {
      response.append(streamToJson(stream));
    }

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/onvif/streams/" << cameraId << " - Found "
                << streams.size() << " streams";
    }

    callback(createSuccessResponse(response));
  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/onvif/streams/" << cameraId
                 << " - Error: " << e.what();
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void ONVIFHandler::setCredentials(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  std::string cameraId = req->getParameter("cameraid");
  
  // Fallback: extract from path if getParameter doesn't work
  if (cameraId.empty()) {
    std::string path = req->getPath();
    // Pattern: /v1/onvif/camera/{cameraid}/credentials
    size_t cameraPos = path.find("/camera/");
    if (cameraPos != std::string::npos) {
      size_t start = cameraPos + 8; // length of "/camera/" is 8
      size_t end = path.find("/", start);
      if (end == std::string::npos) {
        end = path.length();
      }
      cameraId = path.substr(start, end - start);
    }
  }
  
  // URL decode camera ID (in case it's encoded)
  if (!cameraId.empty()) {
    std::string decoded;
    decoded.reserve(cameraId.length());
    for (size_t i = 0; i < cameraId.length(); ++i) {
      if (cameraId[i] == '%' && i + 2 < cameraId.length()) {
        // Try to decode hex value
        char hex[3] = {cameraId[i + 1], cameraId[i + 2], '\0'};
        char *end;
        unsigned long value = std::strtoul(hex, &end, 16);
        if (*end == '\0' && value <= 255) {
          decoded += static_cast<char>(value);
          i += 2; // Skip the hex digits
        } else {
          decoded += cameraId[i]; // Invalid encoding, keep as-is
        }
      } else {
        decoded += cameraId[i];
      }
    }
    cameraId = decoded;
  }
  
  // Debug: Log the raw path and extracted camera ID
  PLOG_INFO << "[API] POST /v1/onvif/camera/{cameraid}/credentials - Raw path: " 
            << req->getPath() << ", Extracted cameraId: '" << cameraId << "'";
  
  if (cameraId.empty()) {
    callback(createErrorResponse(400, "Bad request", "Missing cameraid parameter"));
    return;
  }

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/onvif/camera/" << cameraId
              << "/credentials - Set credentials";
  }

  try {
    // Check if camera exists
    auto &registry = ONVIFCameraRegistry::getInstance();
    
    PLOG_INFO << "[API] POST /v1/onvif/camera/" << cameraId
               << "/credentials - Checking if camera exists";
    
    // Validate camera ID format
    std::string extractedIP = extractIPFromCameraId(cameraId);
    bool isValidIP = !extractedIP.empty() && isValidIPFormat(extractedIP);
    
    if (!isValidIP && cameraId.find('.') != std::string::npos) {
      // Camera ID looks like IP but format is invalid
      PLOG_WARNING << "[API] POST /v1/onvif/camera/" << cameraId
                   << "/credentials - Invalid IP format detected";
    }
    
    // Try to get camera to verify it exists
    ONVIFCamera camera;
    if (!registry.getCamera(cameraId, camera)) {
      PLOG_WARNING << "[API] POST /v1/onvif/camera/" << cameraId
                   << "/credentials - Camera not found in registry";
      
      // Try to find similar camera using regex-based IP matching
      auto allCameras = registry.getAllCameras();
      std::string suggestedCameraId = findSimilarIP(cameraId, allCameras);
      
      PLOG_INFO << "[API] Available cameras in registry:";
      for (const auto &cam : allCameras) {
        std::string keyUsed = cam.uuid.empty() ? cam.ip : cam.uuid;
        std::string ipStatus = isValidIPFormat(cam.ip) ? "✓" : "✗";
        PLOG_INFO << "  - IP: " << cam.ip << " " << ipStatus 
                  << ", UUID: " << cam.uuid 
                  << ", Key used: " << keyUsed;
      }
      
      std::string errorMsg = "Camera not found";
      if (!suggestedCameraId.empty()) {
        errorMsg += ". Did you mean: " + suggestedCameraId + "?";
        PLOG_INFO << "[API] Suggested camera ID (regex match): " << suggestedCameraId;
      } else if (!isValidIP && cameraId.find('.') != std::string::npos) {
        errorMsg += ". Invalid IP format. Expected: 0-255.0-255.0-255.0-255";
      }
      
      callback(createErrorResponse(404, "Not found", errorMsg));
      return;
    }
    
    PLOG_INFO << "[API] POST /v1/onvif/camera/" << cameraId
               << "/credentials - Camera found: IP=" << camera.ip
               << ", UUID=" << camera.uuid;

    // Parse request body
    Json::Value jsonBody;
    Json::Reader reader;
    std::string body(req->getBody());
    if (!reader.parse(body, jsonBody)) {
      callback(createErrorResponse(400, "Bad request", "Invalid JSON"));
      return;
    }

    // Extract username and password
    if (!jsonBody.isMember("username") || !jsonBody.isMember("password")) {
      callback(createErrorResponse(400, "Bad request",
                                   "Missing username or password"));
      return;
    }

    ONVIFCredentials credentials;
    credentials.username = jsonBody["username"].asString();
    credentials.password = jsonBody["password"].asString();

    // Set credentials
    auto &credManager = ONVIFCredentialsManager::getInstance();
    credManager.setCredentials(cameraId, credentials);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/onvif/camera/" << cameraId
                << "/credentials - Credentials set";
    }

    // Return 204 No Content
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/onvif/camera/" << cameraId
                 << "/credentials - Error: " << e.what();
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  }
}

void ONVIFHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
  resp->addHeader("Access-Control-Max-Age", "3600");
  callback(resp);
}

HttpResponsePtr ONVIFHandler::createSuccessResponse(const Json::Value &data,
                                                    int statusCode) const {
  auto resp = HttpResponse::newHttpJsonResponse(data);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
  return resp;
}

HttpResponsePtr ONVIFHandler::createErrorResponse(int statusCode,
                                                   const std::string &error,
                                                   const std::string &message) const {
  Json::Value errorResponse;
  errorResponse["error"] = error;
  errorResponse["message"] = message;

  auto resp = HttpResponse::newHttpJsonResponse(errorResponse);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
  return resp;
}

Json::Value ONVIFHandler::cameraToJson(const struct ONVIFCamera &camera) const {
  Json::Value json;
  json["ip"] = camera.ip;
  json["uuid"] = camera.uuid;
  json["manufacturer"] = camera.manufacturer;
  json["model"] = camera.model;
  json["serialNumber"] = camera.serialNumber;
  return json;
}

Json::Value ONVIFHandler::streamToJson(const struct ONVIFStream &stream) const {
  Json::Value json;
  json["token"] = stream.token;
  json["width"] = stream.width;
  json["height"] = stream.height;
  json["fps"] = stream.fps;
  json["uri"] = stream.uri;
  return json;
}

bool ONVIFHandler::isValidIPFormat(const std::string &ip) const {
  // Regex pattern for valid IP address: 0-255.0-255.0-255.0-255
  // More strict than simple number.number.number.number
  std::regex ipPattern(
      R"(^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$)");
  return std::regex_match(ip, ipPattern);
}

std::string ONVIFHandler::extractIPFromCameraId(const std::string &cameraId) const {
  // Try to extract IP from camera ID using regex
  // Pattern: valid IP address (0-255.0-255.0-255.0-255)
  std::regex ipPattern(
      R"((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?))");
  std::smatch match;
  
  if (std::regex_search(cameraId, match, ipPattern)) {
    std::string extractedIP = match[0].str();
    // Validate the extracted IP
    if (isValidIPFormat(extractedIP)) {
      return extractedIP;
    }
  }
  
  return "";
}

std::string ONVIFHandler::findSimilarIP(
    const std::string &cameraId,
    const std::vector<ONVIFCamera> &allCameras) const {
  // Extract IP from camera ID if it looks like an IP
  std::string inputIP = extractIPFromCameraId(cameraId);
  
  if (inputIP.empty()) {
    // If cameraId doesn't contain a valid IP, check if it's a malformed IP
    // Try to find IPs with similar pattern (same last 3 octets)
    std::regex partialIPPattern(R"((\d+)\.(\d+)\.(\d+)\.(\d+))");
    std::smatch match;
    
    if (std::regex_search(cameraId, match, partialIPPattern)) {
      // Extract the last 3 octets for comparison
      std::string last3Octets = match[2].str() + "." + match[3].str() + "." + match[4].str();
      
      for (const auto &cam : allCameras) {
        if (!cam.ip.empty() && isValidIPFormat(cam.ip)) {
          // Check if last 3 octets match
          std::smatch camMatch;
          if (std::regex_search(cam.ip, camMatch, partialIPPattern) && 
              camMatch.size() >= 4) {
            std::string camLast3 = camMatch[2].str() + "." + camMatch[3].str() + "." + camMatch[4].str();
            if (camLast3 == last3Octets) {
              return cam.ip; // Return the correct IP
            }
          }
        }
      }
    }
    return "";
  }
  
  // If we have a valid IP format, check for typos in individual octets
  // Split input IP into octets
  std::regex octetPattern(R"((\d+)\.(\d+)\.(\d+)\.(\d+))");
  std::smatch inputMatch;
  
  if (!std::regex_match(inputIP, inputMatch, octetPattern) || inputMatch.size() < 5) {
    return "";
  }
  
  std::vector<int> inputOctets;
  for (size_t i = 1; i <= 4; ++i) {
    try {
      inputOctets.push_back(std::stoi(inputMatch[i].str()));
    } catch (...) {
      return "";
    }
  }
  
  // Compare with all cameras
  for (const auto &cam : allCameras) {
    if (!cam.ip.empty() && isValidIPFormat(cam.ip)) {
      std::smatch camMatch;
      if (std::regex_match(cam.ip, camMatch, octetPattern) && camMatch.size() >= 5) {
        std::vector<int> camOctets;
        bool valid = true;
        for (size_t i = 1; i <= 4; ++i) {
          try {
            camOctets.push_back(std::stoi(camMatch[i].str()));
          } catch (...) {
            valid = false;
            break;
          }
        }
        
        if (!valid) continue;
        
        // Check if last 3 octets match exactly (common typo: first octet wrong)
        if (inputOctets.size() == 4 && camOctets.size() == 4) {
          if (inputOctets[1] == camOctets[1] && 
              inputOctets[2] == camOctets[2] && 
              inputOctets[3] == camOctets[3] &&
              inputOctets[0] != camOctets[0]) {
            // First octet differs but last 3 match - likely typo
            return cam.ip;
          }
        }
      }
    }
  }
  
  return "";
}

