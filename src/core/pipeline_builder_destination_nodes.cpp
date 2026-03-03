#include "core/pipeline_builder_destination_nodes.h"
#include "core/pipeline_builder_request_utils.h"
#include "core/env_config.h"
#include "core/rtmp_lastframe_fallback_proxy_node.h"
#include <cvedix/nodes/des/cvedix_app_des_node.h>
#include <cvedix/nodes/des/cvedix_file_des_node.h>
#include <cvedix/nodes/des/cvedix_rtmp_des_node.h>
#include <cvedix/nodes/des/cvedix_screen_des_node.h>
#ifdef CVEDIX_WITH_GSTREAMER
#include <cvedix/nodes/des/cvedix_rtsp_des_node.h>
#endif
#include <cvedix/objects/shapes/cvedix_size.h>
#include <filesystem>
#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

std::string PipelineBuilderDestinationNodes::extractRTMPStreamKey(const std::string &rtmpUrl) {
  // Extract stream key from RTMP URL: rtmp://host:port/path/stream_key
  if (rtmpUrl.empty()) {
    return "";
  }
  
  size_t protocolPos = rtmpUrl.find("rtmp://");
  if (protocolPos == std::string::npos) {
    return "";
  }
  
  // Find the last '/' to locate stream key
  size_t lastSlash = rtmpUrl.find_last_of('/');
  if (lastSlash == std::string::npos || lastSlash >= rtmpUrl.length() - 1) {
    return "";
  }
  
  // Extract stream key (remove trailing _0 suffix if added by RTMP node)
  std::string streamKey = rtmpUrl.substr(lastSlash + 1);
  
  // Remove _0 suffix if present (RTMP node automatically adds this)
  if (streamKey.length() >= 2 && streamKey.substr(streamKey.length() - 2) == "_0") {
    streamKey = streamKey.substr(0, streamKey.length() - 2);
  }
  
  return streamKey;
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDestinationNodes::createFileDestinationNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const std::string &instanceId) {

  try {
    // Get parameters with defaults
    // Constructor: cvedix_file_des_node(node_name, channel_index, save_dir,
    // name_prefix,
    //                                   max_duration_for_single_file,
    //                                   resolution_w_h, bitrate, osd,
    //                                   gst_encoder_name)
    int channelIndex =
        params.count("channel") ? std::stoi(params.at("channel")) : 0;
    std::string saveDir = params.count("save_dir") ? params.at("save_dir")
                                                   : "./output/" + instanceId;
    std::string namePrefix =
        params.count("name_prefix") ? params.at("name_prefix") : "output";
    int maxDuration = params.count("max_duration")
                          ? std::stoi(params.at("max_duration"))
                          : 2; // minutes
    int bitrate =
        params.count("bitrate") ? std::stoi(params.at("bitrate")) : 1024;
    bool osd = params.count("osd") &&
               (params.at("osd") == "true" || params.at("osd") == "1");
    std::string encoderName =
        params.count("encoder") ? params.at("encoder") : "x264enc";

    // Parse resolution if provided (format: "widthxheight" or "width,height")
    cvedix_objects::cvedix_size resolution = {};
    if (params.count("resolution")) {
      std::string resStr = params.at("resolution");
      size_t xPos = resStr.find('x');
      size_t commaPos = resStr.find(',');
      if (xPos != std::string::npos) {
        resolution.width = std::stoi(resStr.substr(0, xPos));
        resolution.height = std::stoi(resStr.substr(xPos + 1));
      } else if (commaPos != std::string::npos) {
        resolution.width = std::stoi(resStr.substr(0, commaPos));
        resolution.height = std::stoi(resStr.substr(commaPos + 1));
      }
    }

    // Validate node name
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    // Validate save directory
    if (saveDir.empty()) {
      throw std::invalid_argument("Save directory cannot be empty");
    }

    // Ensure save directory exists (required by cvedix_file_des_node)
    // Use resolveDirectory with fallback if needed
    std::string resolvedSaveDir = saveDir;
    fs::path saveDirPath(saveDir);
    std::string subdir = saveDirPath.filename().string();
    if (subdir.empty()) {
      subdir = "output"; // Default fallback subdir
    }
    resolvedSaveDir = EnvConfig::resolveDirectory(saveDir, subdir);
    if (resolvedSaveDir != saveDir) {
      std::cerr << "[PipelineBuilderDestinationNodes] ⚠ Save directory changed from " << saveDir
                << " to " << resolvedSaveDir << " (fallback)" << std::endl;
      saveDir = resolvedSaveDir;
      saveDirPath = fs::path(saveDir);
    }
    if (!fs::exists(saveDirPath)) {
      std::cerr << "[PipelineBuilderDestinationNodes] Creating save directory: " << saveDir
                << std::endl;
      fs::create_directories(saveDirPath);
    }

    // Validate channel index
    if (channelIndex < 0) {
      std::cerr << "[PipelineBuilderDestinationNodes] Warning: channel_index < 0: "
                << channelIndex << ", using 0" << std::endl;
      channelIndex = 0;
    }

    // Validate max duration
    if (maxDuration <= 0) {
      std::cerr << "[PipelineBuilderDestinationNodes] Warning: max_duration <= 0: "
                << maxDuration << ", using default 2 minutes" << std::endl;
      maxDuration = 2;
    }

    // Validate bitrate
    if (bitrate <= 0) {
      std::cerr << "[PipelineBuilderDestinationNodes] Warning: bitrate <= 0: " << bitrate
                << ", using default 1024" << std::endl;
      bitrate = 1024;
    }

    std::cerr << "[PipelineBuilderDestinationNodes] Creating file destination node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Channel index: " << channelIndex << std::endl;
    std::cerr << "  Save directory: '" << saveDir << "'" << std::endl;
    std::cerr << "  Name prefix: '" << namePrefix << "'" << std::endl;
    std::cerr << "  Max duration: " << maxDuration << " minutes" << std::endl;
    std::cerr << "  Bitrate: " << bitrate << std::endl;
    std::cerr << "  OSD: " << (osd ? "true" : "false") << std::endl;
    std::cerr << "  Encoder: '" << encoderName << "'" << std::endl;
    if (resolution.width > 0 && resolution.height > 0) {
      std::cerr << "  Resolution: " << resolution.width << "x"
                << resolution.height << std::endl;
    }

    // Create the file destination node
    std::shared_ptr<cvedix_nodes::cvedix_file_des_node> node;
    try {
      std::cerr
          << "[PipelineBuilderDestinationNodes] Calling cvedix_file_des_node constructor..."
          << std::endl;
      node = std::make_shared<cvedix_nodes::cvedix_file_des_node>(
          nodeName, channelIndex, saveDir, namePrefix, maxDuration, resolution,
          bitrate, osd, encoderName);
      std::cerr
          << "[PipelineBuilderDestinationNodes] File destination node created successfully"
          << std::endl;
    } catch (const std::bad_alloc &e) {
      std::cerr << "[PipelineBuilderDestinationNodes] Memory allocation failed: " << e.what()
                << std::endl;
      throw;
    } catch (const std::exception &e) {
      std::cerr << "[PipelineBuilderDestinationNodes] Standard exception in constructor: "
                << e.what() << std::endl;
      throw;
    } catch (...) {
      std::cerr << "[PipelineBuilderDestinationNodes] Non-standard exception in "
                   "cvedix_file_des_node constructor"
                << std::endl;
      std::cerr << "[PipelineBuilderDestinationNodes] Parameters were: name='" << nodeName
                << "', channel=" << channelIndex << ", save_dir='" << saveDir
                << "', name_prefix='" << namePrefix
                << "', max_duration=" << maxDuration << ", bitrate=" << bitrate
                << ", osd=" << osd << ", encoder='" << encoderName << "'"
                << std::endl;
      throw std::runtime_error("Failed to create file destination node - check "
                               "parameters and CVEDIX SDK");
    }

    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDestinationNodes] Exception in createFileDestinationNode: "
              << e.what() << std::endl;
    throw;
  } catch (...) {
    std::cerr
        << "[PipelineBuilderDestinationNodes] Unknown exception in createFileDestinationNode"
        << std::endl;
    throw std::runtime_error("Unknown error creating file destination node");
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDestinationNodes::createRTMPDestinationNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req,
    const std::string &instanceId,
    const std::set<std::string> &existingRTMPStreamKeys,
    std::string &actualRtmpUrl,
    std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> *outExtraNodes) {

  try {
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

    // Get RTMP URL from params or request
    std::string rtmpUrl =
        params.count("rtmp_url") ? params.at("rtmp_url") : PipelineBuilderRequestUtils::getRTMPUrl(req);

    // Substitute placeholder if present (e.g., "${RTMP_URL}" or
    // "${RTMP_DES_URL}")
    if (rtmpUrl == "${RTMP_URL}" || rtmpUrl == "${RTMP_DES_URL}") {
      rtmpUrl = PipelineBuilderRequestUtils::getRTMPUrl(req);
    }

    // Trim whitespace from RTMP URL to prevent GStreamer pipeline errors
    rtmpUrl = trim(rtmpUrl);

    // CRITICAL FIX: Make RTMP URL unique per instance ONLY if stream key conflicts
    // with existing instances. This prevents "Could not open resource for writing"
    // errors when multiple instances try to use the same RTMP stream key.
    // Only modify URL if conflict is detected - this preserves user's original URL
    // when no conflict exists.
    if (!rtmpUrl.empty() && !instanceId.empty()) {
      // Extract stream key from RTMP URL
      std::string streamKey = extractRTMPStreamKey(rtmpUrl);
      
      // DEBUG: Log existing stream keys for debugging
      if (!streamKey.empty()) {
        std::cerr << "[PipelineBuilderDestinationNodes] DEBUG: Checking stream key: '"
                  << streamKey << "' for instance: '" << instanceId << "'" << std::endl;
        std::cerr << "[PipelineBuilderDestinationNodes] DEBUG: Existing RTMP stream keys count: "
                  << existingRTMPStreamKeys.size() << std::endl;
        if (!existingRTMPStreamKeys.empty()) {
          std::cerr << "[PipelineBuilderDestinationNodes] DEBUG: Existing stream keys: ";
          for (const auto &key : existingRTMPStreamKeys) {
            std::cerr << "'" << key << "' ";
          }
          std::cerr << std::endl;
        }
      }
      
      // Check if this stream key conflicts with existing instances
      if (!streamKey.empty() && existingRTMPStreamKeys.find(streamKey) != existingRTMPStreamKeys.end()) {
        // Conflict detected - make URL unique by appending instance ID
        size_t protocolPos = rtmpUrl.find("rtmp://");
        if (protocolPos != std::string::npos) {
          // Find the last '/' to locate stream key
          size_t lastSlash = rtmpUrl.find_last_of('/');
          if (lastSlash != std::string::npos && lastSlash < rtmpUrl.length() - 1) {
            std::string baseUrl = rtmpUrl.substr(0, lastSlash + 1);
            
            // Remove _0 suffix if present (RTMP node will add it back)
            std::string originalStreamKey = streamKey;
            if (originalStreamKey.length() >= 2 && 
                originalStreamKey.substr(originalStreamKey.length() - 2) == "_0") {
              originalStreamKey = originalStreamKey.substr(0, originalStreamKey.length() - 2);
            }
            
            // Generate short unique ID from instanceId (use first 8 chars)
            std::string shortId = instanceId.substr(0, 8);
            
            // Append instance ID to stream key: stream_key -> stream_key_<shortId>
            std::string uniqueStreamKey = originalStreamKey + "_" + shortId;
            rtmpUrl = baseUrl + uniqueStreamKey;
            std::cerr << "[PipelineBuilderDestinationNodes] RTMP stream key conflict detected: '"
                      << streamKey << "'" << std::endl;
            std::cerr << "[PipelineBuilderDestinationNodes] Made RTMP URL unique per instance: '"
                      << rtmpUrl << "'" << std::endl;
            std::cerr << "[PipelineBuilderDestinationNodes] Original stream key was appended with instance ID: '"
                      << shortId << "'" << std::endl;
          }
        }
      } else if (!streamKey.empty()) {
        // No conflict - keep original URL unchanged
        std::cerr << "[PipelineBuilderDestinationNodes] RTMP stream key '"
                  << streamKey << "' has no conflicts, using original URL: '"
                  << rtmpUrl << "'" << std::endl;
      }
    }

    int channel = params.count("channel") ? std::stoi(params.at("channel")) : 0;

    // Check if OSD overlay should be enabled
    // OSD parameter can be "true"/"1" or "false"/"0"
    // If not specified, default to false (assume OSD node exists in pipeline)
    bool enableOSD = params.count("osd") &&
                     (params.at("osd") == "true" || params.at("osd") == "1");

    // Validate parameters
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    // If RTMP URL is empty, return nullptr to skip this node (optional)
    if (rtmpUrl.empty()) {
      std::cerr << "[PipelineBuilderDestinationNodes] RTMP URL is empty, skipping RTMP "
                   "destination node: "
                << nodeName << std::endl;
      actualRtmpUrl = "";  // Set empty URL
      return nullptr;
    }

    std::cerr << "[PipelineBuilderDestinationNodes] Creating RTMP destination node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  RTMP URL: '" << rtmpUrl << "'" << std::endl;
    std::cerr << "  Channel: " << channel << std::endl;
    std::cerr << "  OSD overlay: " << (enableOSD ? "enabled" : "disabled")
              << std::endl;
    std::cerr
        << "  NOTE: RTMP node automatically adds '_0' suffix to stream key"
        << std::endl;

    std::shared_ptr<cvedix_nodes::cvedix_rtmp_des_node> rtmpNode;
    if (enableOSD) {
      int bitrate = 1024;
      if (params.count("bitrate")) {
        bitrate = std::stoi(params.at("bitrate"));
        if (bitrate <= 0) {
          std::cerr << "[PipelineBuilderDestinationNodes] Warning: bitrate <= 0: " << bitrate
                    << ", using default 1024" << std::endl;
          bitrate = 1024;
        }
      }
      cvedix_objects::cvedix_size resolution = {};
      std::cerr << "[PipelineBuilderDestinationNodes] Using bitrate: " << bitrate << " kbps"
                << std::endl;
      rtmpNode = std::make_shared<cvedix_nodes::cvedix_rtmp_des_node>(
          nodeName, channel, rtmpUrl, resolution, bitrate,
          true // Enable OSD overlay
      );
    } else {
      rtmpNode = std::make_shared<cvedix_nodes::cvedix_rtmp_des_node>(
          nodeName, channel, rtmpUrl);
    }

    // Proxy: when frame is empty, forwards last valid frame so RTMP connection stays alive
    std::string proxyName = "rtmp_lastframe_proxy_" + instanceId;
    auto proxy = std::make_shared<edgeos::RtmpLastFrameFallbackProxyNode>(proxyName);
    rtmpNode->attach_to({proxy});

    if (outExtraNodes) {
      outExtraNodes->push_back(rtmpNode);
    }

    std::cerr
        << "[PipelineBuilderDestinationNodes] ✓ RTMP destination node created (with last-frame fallback proxy)"
        << std::endl;
    std::cerr << "[PipelineBuilderDestinationNodes] RTMP node will start automatically when pipeline starts"
        << std::endl;
    std::cerr << "[PipelineBuilderDestinationNodes] NOTE: If RTMP stream is not working, check:"
              << std::endl;
    std::cerr << "  1. RTMP server is accessible: " << rtmpUrl << std::endl;
    std::cerr << "  2. RTMP server is running and accepting connections" << std::endl;
    std::cerr << "  3. Network connectivity to RTMP server" << std::endl;
    std::cerr << "  4. GStreamer pipeline logs for connection errors" << std::endl;

    actualRtmpUrl = rtmpUrl;
    return proxy;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDestinationNodes] Exception in createRTMPDestinationNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDestinationNodes::createScreenDestinationNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params) {

  try {
    // Check if screen_des is explicitly disabled via parameter
    if (params.count("enabled")) {
      std::string enabledStr = params.at("enabled");
      // Convert to lowercase for case-insensitive comparison
      std::transform(enabledStr.begin(), enabledStr.end(), enabledStr.begin(),
                     ::tolower);
      if (enabledStr == "false" || enabledStr == "0" || enabledStr == "no" ||
          enabledStr == "off") {
        std::cerr << "[PipelineBuilderDestinationNodes] screen_des node skipped: Explicitly "
                     "disabled via parameter (enabled=false)"
                  << std::endl;
        return nullptr;
      }
    }

    // Check if display is available
    const char *display = std::getenv("DISPLAY");
    const char *wayland = std::getenv("WAYLAND_DISPLAY");

    if (!display && !wayland) {
      std::cerr << "[PipelineBuilderDestinationNodes] screen_des node skipped: No DISPLAY or "
                   "WAYLAND_DISPLAY environment variable found"
                << std::endl;
      std::cerr << "[PipelineBuilderDestinationNodes] NOTE: screen_des requires a display to "
                   "work. Set DISPLAY or WAYLAND_DISPLAY to enable."
                << std::endl;
      return nullptr;
    }

    // Additional check for X11 display
    if (display && display[0] != '\0') {
      std::string displayStr(display);
      if (displayStr[0] == ':') {
        std::string socketPath = "/tmp/.X11-unix/X" + displayStr.substr(1);
        // Check if X11 socket exists (basic check)
        std::ifstream socketFile(socketPath);
        if (!socketFile.good()) {
          std::cerr << "[PipelineBuilderDestinationNodes] screen_des node skipped: X11 socket "
                       "not found at "
                    << socketPath << std::endl;
          std::cerr << "[PipelineBuilderDestinationNodes] NOTE: X server may not be running or "
                       "accessible"
                    << std::endl;
          return nullptr;
        }
      }
    }

    int channel = params.count("channel") ? std::stoi(params.at("channel")) : 0;

    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderDestinationNodes] Creating screen destination node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Channel: " << channel << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_screen_des_node>(nodeName,
                                                                       channel);

    std::cerr
        << "[PipelineBuilderDestinationNodes] ✓ Screen destination node created successfully"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDestinationNodes] Exception in createScreenDestinationNode: "
              << e.what() << std::endl;
    std::cerr
        << "[PipelineBuilderDestinationNodes] screen_des node will be skipped due to error"
        << std::endl;
    return nullptr; // Return nullptr instead of throwing to allow pipeline to
                    // continue
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDestinationNodes::createAppDesNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params) {

  try {
    // Get channel parameter (default: 0)
    int channel = params.count("channel") ? std::stoi(params.at("channel")) : 0;

    // Validate parameters
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderDestinationNodes] Creating app_des node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Channel: " << channel << std::endl;
    std::cerr
        << "  NOTE: app_des node is used for frame capture via callback hook"
        << std::endl;

    auto node =
        std::make_shared<cvedix_nodes::cvedix_app_des_node>(nodeName, channel);

    std::cerr << "[PipelineBuilderDestinationNodes] ✓ app_des node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDestinationNodes] Exception in createAppDesNode: " << e.what()
              << std::endl;
    throw;
  } catch (...) {
    std::cerr << "[PipelineBuilderDestinationNodes] Unknown exception in createAppDesNode"
              << std::endl;
    throw std::runtime_error("Unknown error creating app_des node");
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDestinationNodes::createRTSPDestinationNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const std::string &instanceId) {

#ifdef CVEDIX_WITH_GSTREAMER
  try {
    // Get parameters with defaults
    // Constructor: cvedix_rtsp_des_node(node_name, channel_index, rtsp_port, 
    //                                   rtsp_name, resolution_w_h, bitrate, osd, gst_encoder_name)
    int channelIndex = params.count("channel") ? std::stoi(params.at("channel")) : 0;
    int rtspPort = params.count("rtsp_port") ? std::stoi(params.at("rtsp_port")) : 9000;
    std::string rtspName = params.count("rtsp_name") ? params.at("rtsp_name") : 
                          (params.count("stream_name") ? params.at("stream_name") : 
                           ("stream_" + instanceId));
    int bitrate = params.count("bitrate") ? std::stoi(params.at("bitrate")) : 512;
    bool osd = params.count("osd") && 
               (params.at("osd") == "true" || params.at("osd") == "1");
    std::string encoderName = params.count("encoder") ? params.at("encoder") : "x264enc";

    // Parse resolution if provided (format: "widthxheight" or "width,height")
    cvedix_objects::cvedix_size resolution = {};
    if (params.count("resolution")) {
      std::string resStr = params.at("resolution");
      size_t xPos = resStr.find('x');
      size_t commaPos = resStr.find(',');
      if (xPos != std::string::npos) {
        resolution.width = std::stoi(resStr.substr(0, xPos));
        resolution.height = std::stoi(resStr.substr(xPos + 1));
      } else if (commaPos != std::string::npos) {
        resolution.width = std::stoi(resStr.substr(0, commaPos));
        resolution.height = std::stoi(resStr.substr(commaPos + 1));
      }
    }

    // Validate node name
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    // Validate RTSP port
    if (rtspPort < 1 || rtspPort > 65535) {
      throw std::invalid_argument("RTSP port must be between 1 and 65535");
    }

    std::cerr << "[PipelineBuilderDestinationNodes] Creating RTSP destination node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Channel: " << channelIndex << std::endl;
    std::cerr << "  RTSP Port: " << rtspPort << std::endl;
    std::cerr << "  Stream Name: '" << rtspName << "'" << std::endl;
    std::cerr << "  Bitrate: " << bitrate << " kbps" << std::endl;
    std::cerr << "  OSD: " << (osd ? "enabled" : "disabled") << std::endl;
    if (resolution.width > 0 && resolution.height > 0) {
      std::cerr << "  Resolution: " << resolution.width << "x" << resolution.height << std::endl;
    }

    auto node = std::make_shared<cvedix_nodes::cvedix_rtsp_des_node>(
        nodeName, channelIndex, rtspPort, rtspName, resolution, bitrate, osd, encoderName);

    std::cerr << "[PipelineBuilderDestinationNodes] ✓ RTSP destination node created successfully" << std::endl;
    std::cerr << "[PipelineBuilderDestinationNodes] Stream URL: rtsp://localhost:" << rtspPort 
              << "/" << rtspName << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDestinationNodes] Exception in createRTSPDestinationNode: "
              << e.what() << std::endl;
    throw;
  } catch (...) {
    std::cerr << "[PipelineBuilderDestinationNodes] Unknown exception in createRTSPDestinationNode"
              << std::endl;
    throw std::runtime_error("Unknown error creating rtsp_des node");
  }
#else
  std::cerr << "[PipelineBuilderDestinationNodes] ⚠ RTSP destination node requires "
               "CVEDIX_WITH_GSTREAMER to be enabled" << std::endl;
  std::cerr << "[PipelineBuilderDestinationNodes] ⚠ Install: sudo apt-get install "
               "libgstrtspserver-1.0-dev gstreamer1.0-rtsp" << std::endl;
  std::cerr << "[PipelineBuilderDestinationNodes] ⚠ Recompile with -DCVEDIX_WITH_GSTREAMER" 
            << std::endl;
  return nullptr;
#endif
}

