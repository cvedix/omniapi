#include "api/hls_handler.h"
#include "core/env_config.h"
#include "core/logging_flags.h"
#include "core/logger.h"
#include "core/metrics_interceptor.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

void HlsHandler::getPlaylist(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    // Extract instance ID - try getParameter first (like SwaggerHandler does)
    std::string instanceId = req->getParameter("instanceId");
    
    // Fallback: extract from path if getParameter doesn't work
    if (instanceId.empty()) {
      std::string path = req->getPath();
      // Pattern: /hls/{instanceId}/stream.m3u8
      size_t hlsPos = path.find("/hls/");
      if (hlsPos != std::string::npos) {
        size_t start = hlsPos + 5; // length of "/hls/"
        size_t end = path.find("/", start);
        if (end != std::string::npos) {
          instanceId = path.substr(start, end - start);
        }
      }
    }
    
    if (instanceId.empty()) {
      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k400BadRequest);
      resp->setBody("Instance ID is required");
      resp->addHeader("Access-Control-Allow-Origin", "*");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Get HLS directory
    std::string hlsDir = getHlsDirectory(instanceId);
    std::string playlistPath = hlsDir + "/stream.m3u8";

    // Check if playlist exists
    if (!fs::exists(playlistPath) || !fs::is_regular_file(playlistPath)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[HLS] Playlist not found: " << playlistPath;
      }
      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k404NotFound);
      resp->setBody("HLS playlist not found. Instance may not have HLS output enabled or segments not yet generated.");
      resp->addHeader("Access-Control-Allow-Origin", "*");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Read playlist file
    std::ifstream file(playlistPath, std::ios::binary);
    if (!file.is_open()) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[HLS] Cannot open playlist file: " << playlistPath;
      }
      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k500InternalServerError);
      resp->setBody("Cannot read HLS playlist file");
      resp->addHeader("Access-Control-Allow-Origin", "*");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    // Return playlist with correct MIME type
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_CUSTOM);
    resp->addHeader("Content-Type", "application/vnd.apple.mpegurl");
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->setBody(content);
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[HLS] Served playlist: " << playlistPath;
    }

  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[HLS] Exception in getPlaylist: " << e.what();
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k500InternalServerError);
    resp->setBody("Internal server error");
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (...) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[HLS] Unknown exception in getPlaylist";
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k500InternalServerError);
    resp->setBody("Internal server error");
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void HlsHandler::getSegment(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[HLS] getSegment called with path: " << req->getPath();
    }

    // Extract instance ID and segment ID from path (Drogon may not parse correctly with dots)
    std::string instanceId;
    std::string segmentId;
    std::string path = req->getPath();
    
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[HLS] Full path: " << path;
    }
    
    // Try getParameter first (like SwaggerHandler does)
    instanceId = req->getParameter("instanceId");
    segmentId = req->getParameter("segmentId");
    
    // Fallback: extract from path if getParameter doesn't work
    if (instanceId.empty() || segmentId.empty()) {
      // Pattern: /hls/{instanceId}/segment_{segmentId}.ts
      size_t hlsPos = path.find("/hls/");
      if (hlsPos != std::string::npos) {
        size_t start = hlsPos + 5; // length of "/hls/"
        size_t end = path.find("/", start);
        if (end != std::string::npos) {
          if (instanceId.empty()) {
            instanceId = path.substr(start, end - start);
          }
          
          // Extract segment ID from segment_{segmentId}.ts
          size_t segmentStart = path.find("segment_", end);
          if (segmentStart != std::string::npos) {
            segmentStart += 8; // length of "segment_"
            size_t segmentEnd = path.find(".ts", segmentStart);
            if (segmentEnd != std::string::npos && segmentId.empty()) {
              segmentId = path.substr(segmentStart, segmentEnd - segmentStart);
            }
          }
        }
      }
    }
    
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[HLS] Extracted instanceId: " << instanceId << ", segmentId: " << segmentId;
    }
    
    // Fallback: try getParameter (may work in some cases)
    if (instanceId.empty()) {
      instanceId = req->getParameter("instanceId");
      if (isApiLoggingEnabled() && !instanceId.empty()) {
        PLOG_DEBUG << "[HLS] Got instanceId from getParameter: " << instanceId;
      }
    }
    if (segmentId.empty()) {
      segmentId = req->getParameter("segmentId");
      if (isApiLoggingEnabled() && !segmentId.empty()) {
        PLOG_DEBUG << "[HLS] Got segmentId from getParameter: " << segmentId;
      }
    }
    
    if (instanceId.empty() || segmentId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[HLS] Missing parameters - instanceId: " << instanceId 
                     << ", segmentId: " << segmentId << ", path: " << path;
      }
      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k400BadRequest);
      resp->setBody("Instance ID and segment ID are required");
      resp->addHeader("Access-Control-Allow-Origin", "*");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Get HLS directory
    std::string hlsDir = getHlsDirectory(instanceId);
    std::string segmentPath = hlsDir + "/segment_" + segmentId + ".ts";

    // Check if segment exists
    if (!fs::exists(segmentPath) || !fs::is_regular_file(segmentPath)) {
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[HLS] Segment not found: " << segmentPath;
      }
      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k404NotFound);
      resp->setBody("HLS segment not found");
      resp->addHeader("Access-Control-Allow-Origin", "*");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Read segment file
    std::ifstream file(segmentPath, std::ios::binary);
    if (!file.is_open()) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[HLS] Cannot open segment file: " << segmentPath;
      }
      auto resp = HttpResponse::newHttpResponse();
      resp->setStatusCode(k500InternalServerError);
      resp->setBody("Cannot read HLS segment file");
      resp->addHeader("Access-Control-Allow-Origin", "*");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Get file size
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read file content
    std::vector<char> buffer(fileSize);
    file.read(buffer.data(), fileSize);
    file.close();

    // Return segment with correct MIME type
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_CUSTOM);
    resp->addHeader("Content-Type", "video/mp2t");
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->setBody(std::string(buffer.data(), fileSize));
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[HLS] Served segment: " << segmentPath << " (" << fileSize << " bytes)";
    }

  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[HLS] Exception in getSegment: " << e.what();
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k500InternalServerError);
    resp->setBody("Internal server error");
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (...) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[HLS] Unknown exception in getSegment";
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k500InternalServerError);
    resp->setBody("Internal server error");
    resp->addHeader("Access-Control-Allow-Origin", "*");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

std::string HlsHandler::getHlsDirectory(const std::string &instanceId) const {
  // Try multiple possible locations for HLS files (in priority order)
  // Priority: /opt/omniapi/hls/ (production) first, then fallbacks
  std::vector<std::string> possiblePaths = {
    "/opt/omniapi/hls/" + instanceId,  // Production directory (preferred)
    "/tmp/hls/" + instanceId,               // Temporary directory
    "./hls/" + instanceId,                  // Current directory
    "./build/hls/" + instanceId,
    "hls/" + instanceId,
    "build/hls/" + instanceId,
    EnvConfig::resolveDirectory("/opt/omniapi/hls/" + instanceId, "hls/" + instanceId)
  };

  // Check existing directories first
  for (const auto &path : possiblePaths) {
    if (fs::exists(path) && fs::is_directory(path)) {
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[HLS] Found HLS directory: " << path;
      }
      return path;
    }
  }

  // If no directory exists, try to create one in order of preference
  for (const auto &path : possiblePaths) {
    try {
      if (!fs::exists(path)) {
        fs::create_directories(path);
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[HLS] Created HLS directory: " << path;
        }
        return path;
      }
    } catch (const std::exception &e) {
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[HLS] Cannot create directory " << path << ": " << e.what();
      }
      continue; // Try next path
    }
  }

  // Last resort: return default resolved path (may not exist yet)
  std::string defaultPath = EnvConfig::resolveDirectory("/opt/omniapi/hls/" + instanceId, "hls/" + instanceId);
  if (isApiLoggingEnabled()) {
    PLOG_DEBUG << "[HLS] Using default HLS directory: " << defaultPath;
  }
  return defaultPath;
}

