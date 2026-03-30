#include "core/pipeline_builder_request_utils.h"
#include "core/env_config.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

std::string PipelineBuilderRequestUtils::getFilePath(
    const CreateInstanceRequest &req) {
  const bool verbose = EnvConfig::getBool("EDGE_AI_VERBOSE", false);
  // Get file path from additionalParams
  auto it = req.additionalParams.find("FILE_PATH");
  if (it != req.additionalParams.end() && !it->second.empty()) {
    if (verbose) {
      std::cerr << "[PipelineBuilder] File path from request additionalParams: '"
                << it->second << "'" << std::endl;
    }
    return it->second;
  }

  // Default fallback: Try production path first, then development path
  std::string productionPath = "/opt/edgeos-api/videos/face.mp4";
  try {
    if (fs::exists(productionPath)) {
      if (verbose) {
        std::cerr << "[PipelineBuilder] Using default production file path: "
                  << productionPath << std::endl;
      }
      return productionPath;
    }
  } catch (const std::filesystem::filesystem_error &) {
    // Permission denied or other filesystem error - skip production path
  }

  // Development fallback (will not exist in production)
  std::cerr << "[PipelineBuilder] ⚠ WARNING: Using development default file "
               "path (./cvedix_data/test_video/face.mp4)"
            << std::endl;
  if (verbose) {
    std::cerr << "[PipelineBuilder] ℹ NOTE: In production, provide 'FILE_PATH' "
                 "in request body or upload videos to /opt/edgeos-api/videos/"
              << std::endl;
  }
  return "./cvedix_data/test_video/face.mp4";
}

std::string PipelineBuilderRequestUtils::getRTMPUrl(
    const CreateInstanceRequest &req) {
  const bool verbose = EnvConfig::getBool("EDGE_AI_VERBOSE", false);
  // Helper function to trim whitespace from string
  auto trim = [](const std::string &str) -> std::string {
    if (str.empty())
      return str;
    size_t first = str.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string::npos)
      return "";
    size_t last = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(first, (last - first + 1));
  };

  if (verbose) {
    std::cerr << "[PipelineBuilder] getRTMPUrl: Checking additionalParams (total keys: "
              << req.additionalParams.size() << ")" << std::endl;
    for (const auto &[key, value] : req.additionalParams) {
      if (key.find("RTMP") != std::string::npos) {
        std::cerr << "[PipelineBuilder] getRTMPUrl: Found RTMP-related key: '"
                  << key << "' = '" << value << "'" << std::endl;
      }
    }
  }

  // Get RTMP URL from additionalParams - check RTMP_DES_URL first (new format),
  // then RTMP_URL (backward compatibility)
  auto desIt = req.additionalParams.find("RTMP_DES_URL");
  if (desIt != req.additionalParams.end() && !desIt->second.empty()) {
    std::string trimmed = trim(desIt->second);
    if (verbose) {
      std::cerr << "[PipelineBuilder] RTMP URL from request additionalParams "
                   "(RTMP_DES_URL): '"
                << trimmed << "'" << std::endl;
    }
    if (!trimmed.empty()) {
      return trimmed;
    }
  }

  // Fallback to RTMP_URL for backward compatibility
  auto it = req.additionalParams.find("RTMP_URL");
  if (it != req.additionalParams.end() && !it->second.empty()) {
    std::string trimmed = trim(it->second);
    if (verbose) {
      std::cerr << "[PipelineBuilder] RTMP URL from request additionalParams "
                   "(RTMP_URL): '"
                << trimmed << "'" << std::endl;
    }
    if (!trimmed.empty()) {
      return trimmed;
    }
  }

  // Return empty string if RTMP URL is not provided (allows skipping RTMP
  // output)
  if (verbose) {
    std::cerr << "[PipelineBuilder] RTMP URL not provided, RTMP destination node "
                 "will be skipped"
              << std::endl;
    std::cerr << "[PipelineBuilder] NOTE: To enable RTMP output, provide "
                 "'RTMP_DES_URL' or 'RTMP_URL' in request body additionalParams"
              << std::endl;
  }
  return "";
}

std::string PipelineBuilderRequestUtils::getRTSPUrl(
    const CreateInstanceRequest &req) {
  const bool verbose = EnvConfig::getBool("EDGE_AI_VERBOSE", false);
  // Get RTSP URL from additionalParams - check both RTSP_URL and RTSP_SRC_URL
  auto it = req.additionalParams.find("RTSP_URL");
  if (it != req.additionalParams.end() && !it->second.empty()) {
    if (verbose) {
      std::cerr << "[PipelineBuilder] RTSP URL from request additionalParams "
                   "(RTSP_URL): '"
                << it->second << "'" << std::endl;
    }
    return it->second;
  }

  // Also check RTSP_SRC_URL (for consistency with RTMP_SRC_URL)
  auto srcIt = req.additionalParams.find("RTSP_SRC_URL");
  if (srcIt != req.additionalParams.end() && !srcIt->second.empty()) {
    if (verbose) {
      std::cerr << "[PipelineBuilder] RTSP URL from request additionalParams "
                   "(RTSP_SRC_URL): '"
                << srcIt->second << "'" << std::endl;
    }
    return srcIt->second;
  }

  // Default or from environment
  const char *envUrl = std::getenv("RTSP_URL");
  if (envUrl && strlen(envUrl) > 0) {
    if (verbose) {
      std::cerr << "[PipelineBuilder] RTSP URL from environment variable: '"
                << envUrl << "'" << std::endl;
    }
    return std::string(envUrl);
  }

  // Also check RTSP_SRC_URL environment variable
  const char *envSrcUrl = std::getenv("RTSP_SRC_URL");
  if (envSrcUrl && strlen(envSrcUrl) > 0) {
    if (verbose) {
      std::cerr << "[PipelineBuilder] RTSP URL from environment variable "
                   "(RTSP_SRC_URL): '"
                << envSrcUrl << "'" << std::endl;
    }
    return std::string(envSrcUrl);
  }

  // Default fallback (development only - should be overridden in production)
  // SECURITY: This is a development default. In production, always provide
  // RTSP_URL via request or environment variable.
  std::cerr << "[PipelineBuilder] WARNING: Using default RTSP URL "
               "(rtsp://localhost:8554/stream)"
            << std::endl;
  std::cerr << "[PipelineBuilder] WARNING: This is a development default. For "
               "production, provide 'RTSP_URL' or 'RTSP_SRC_URL' in request "
               "body or set RTSP_URL/RTSP_SRC_URL environment variable"
            << std::endl;
  return "rtsp://localhost:8554/stream";
}

