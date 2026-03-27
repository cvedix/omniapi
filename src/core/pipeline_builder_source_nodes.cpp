#include "core/pipeline_builder_source_nodes.h"
#include "core/pipeline_builder_request_utils.h"
#include "config/system_config.h"
#include <cstdlib> // For setenv
#include <cstring> // For strlen
#include <cvedix/nodes/src/cvedix_app_src_node.h>
#include <cvedix/nodes/src/cvedix_file_src_node.h>
#include <cvedix/nodes/src/cvedix_image_src_node.h>
#include <cvedix/nodes/src/cvedix_rtmp_src_node.h>
#include <cvedix/nodes/src/cvedix_rtsp_src_node.h>
#include <cvedix/nodes/src/cvedix_udp_src_node.h>
#include <filesystem>
#include <algorithm>
#include <iomanip>
#include <cmath>

namespace fs = std::filesystem;

// Helper function to check if GStreamer plugin/decoder is available
static bool isGStreamerDecoderAvailable(const std::string &decoderName) {
  // Use gst-inspect-1.0 to check if decoder plugin exists
  std::string command = "gst-inspect-1.0 " + decoderName + " >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return (status == 0);
}

// Helper function to select decoder with automatic fallback
// Priority: NVIDIA GPU (nvh264dec) -> Intel VAAPI (vaapih264dec) -> Software (avdec_h264)
static std::string selectDecoderWithFallback(const std::string &userSpecifiedDecoder = "") {
  // If user explicitly specified a decoder, use it (but still validate)
  if (!userSpecifiedDecoder.empty()) {
    if (isGStreamerDecoderAvailable(userSpecifiedDecoder)) {
      std::cerr << "[PipelineBuilderSourceNodes] Using user-specified decoder: '"
                << userSpecifiedDecoder << "'" << std::endl;
      return userSpecifiedDecoder;
    } else {
      std::cerr << "[PipelineBuilderSourceNodes] ⚠ WARNING: User-specified decoder '"
                << userSpecifiedDecoder << "' not available, falling back to auto-detection"
                << std::endl;
    }
  }

  // Auto-detect with fallback priority:
  // 1. NVIDIA GPU decoder (nvh264dec) - fastest, hardware accelerated
  // 2. Intel VAAPI decoder (vaapih264dec) - Intel GPU hardware acceleration
  // 3. Intel QuickSync decoder (qsvh264dec) - Intel QuickSync hardware acceleration
  // 4. Software decoder (avdec_h264) - always available, CPU-based

  std::vector<std::pair<std::string, std::string>> decoderCandidates = {
      {"nvh264dec", "NVIDIA GPU hardware decoder (NVENC/NVDEC)"},
      {"vaapih264dec", "Intel VAAPI hardware decoder (Intel GPU)"},
      {"qsvh264dec", "Intel QuickSync hardware decoder"},
      {"avdec_h264", "Software decoder (FFmpeg, CPU-based)"}
  };

  for (const auto &candidate : decoderCandidates) {
    if (isGStreamerDecoderAvailable(candidate.first)) {
      std::cerr << "[PipelineBuilderSourceNodes] ✓ Selected decoder: '"
                << candidate.first << "' (" << candidate.second << ")" << std::endl;
      return candidate.first;
    } else {
      std::cerr << "[PipelineBuilderSourceNodes] ✗ Decoder '"
                << candidate.first << "' not available" << std::endl;
    }
  }

  // Fallback to default (should never reach here if avdec_h264 is available)
  std::cerr << "[PipelineBuilderSourceNodes] ⚠ WARNING: No suitable decoder found, using default 'avdec_h264'"
            << std::endl;
  return "avdec_h264";
}

// Helper function to select decoder from priority list (legacy, for backward compatibility)
static std::string
selectDecoderFromPriority(const std::string &defaultDecoder) {
  try {
    auto &systemConfig = SystemConfig::getInstance();
    auto decoderList = systemConfig.getDecoderPriorityList();

    if (decoderList.empty()) {
      // If no priority list, use auto-detection with fallback
      return selectDecoderWithFallback();
    }

    // Map decoder priority names to GStreamer decoder names
    std::map<std::string, std::string> decoderMap = {
        {"blaize.auto",
         "avdec_h264"}, // Blaize hardware decoder (fallback to software)
        {"rockchip", "mppvideodec"}, // Rockchip MPP decoder
        {"nvidia.1", "nvh264dec"},   // NVIDIA hardware decoder
        {"intel.1", "qsvh264dec"},   // Intel QuickSync decoder
        {"software", "avdec_h264"}   // Software decoder
    };

    // Try decoders in priority order with availability check
    for (const auto &priorityDecoder : decoderList) {
      auto it = decoderMap.find(priorityDecoder);
      if (it != decoderMap.end()) {
        // Check if decoder is available
        if (isGStreamerDecoderAvailable(it->second)) {
          std::cerr << "[PipelineBuilderSourceNodes] Selected decoder from priority: "
                    << priorityDecoder << " -> " << it->second << std::endl;
          return it->second;
        } else {
          std::cerr << "[PipelineBuilderSourceNodes] Decoder from priority '"
                    << priorityDecoder << "' (" << it->second
                    << ") not available, trying next..." << std::endl;
        }
      }
    }

    // If no decoder from priority list is available, use auto-detection with fallback
    std::cerr << "[PipelineBuilderSourceNodes] No decoder from priority list available, using auto-detection"
              << std::endl;
    return selectDecoderWithFallback();
  } catch (...) {
    // On error, use auto-detection with fallback
    std::cerr << "[PipelineBuilderSourceNodes] Exception in selectDecoderFromPriority, using auto-detection"
              << std::endl;
    return selectDecoderWithFallback();
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderSourceNodes::createRTSPSourceNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    // Get RTSP URL - prioritize RTSP_SRC_URL (like sample code)
    std::string rtspUrl;
    if (params.count("rtsp_url") && !params.at("rtsp_url").empty()) {
      rtspUrl = params.at("rtsp_url");
    } else {
      // Check RTSP_SRC_URL first (like sample), then RTSP_URL
      auto rtspSrcIt = req.additionalParams.find("RTSP_SRC_URL");
      if (rtspSrcIt != req.additionalParams.end() &&
          !rtspSrcIt->second.empty()) {
        rtspUrl = rtspSrcIt->second;
        std::cerr
            << "[PipelineBuilderSourceNodes] Using RTSP_SRC_URL from additionalParams: '"
            << rtspUrl << "'" << std::endl;
      } else {
        rtspUrl = PipelineBuilderRequestUtils::getRTSPUrl(req);
      }
    }

    int channel = params.count("channel") ? std::stoi(params.at("channel")) : 0;

    // cvedix_rtsp_src_node constructor: (name, channel, rtsp_url, resize_ratio,
    // gst_decoder_name, skip_interval, codec_type) resize_ratio must be > 0.0
    // and <= 1.0 Default to 0.6 like sample code (rtsp_ba_crossline_sample.cpp
    // uses 0.6f)
    float resize_ratio = 0.6f; // Default to 0.6 like sample

    // Get decoder from config priority list if not specified
    // Priority: additionalParams GST_DECODER_NAME > params gst_decoder_name >
    // selectDecoderFromPriority > default
    std::string defaultDecoder = "avdec_h264";
    std::string gstDecoderName = defaultDecoder;

    // Check additionalParams first (highest priority)
    auto decoderIt = req.additionalParams.find("GST_DECODER_NAME");
    if (decoderIt != req.additionalParams.end() && !decoderIt->second.empty()) {
      gstDecoderName = decoderIt->second;
      std::cerr
          << "[PipelineBuilderSourceNodes] Using GST_DECODER_NAME from additionalParams: '"
          << gstDecoderName << "'" << std::endl;
    } else if (params.count("gst_decoder_name") &&
               !params.at("gst_decoder_name").empty()) {
      gstDecoderName = params.at("gst_decoder_name");
      std::cerr << "[PipelineBuilderSourceNodes] Using gst_decoder_name from params: '"
                << gstDecoderName << "'" << std::endl;
    } else {
      gstDecoderName = selectDecoderFromPriority(defaultDecoder);
      std::cerr << "[PipelineBuilderSourceNodes] Using decoder from priority list: '"
                << gstDecoderName << "'" << std::endl;
    }

    // If user (or template) named a decoder that is not installed, fall back:
    // nvh264dec → vaapih264dec → qsvh264dec → avdec_h264 (selectDecoderWithFallback).
    if (!isGStreamerDecoderAvailable(gstDecoderName)) {
      std::cerr << "[PipelineBuilderSourceNodes] ⚠ WARNING: Decoder '"
                << gstDecoderName
                << "' not available (gst-inspect), falling back: "
                   "nvh264dec → … → avdec_h264"
                << std::endl;
      gstDecoderName = selectDecoderWithFallback();
    }

    // Get skip_interval (0 means no skip)
    // Priority: additionalParams SKIP_INTERVAL > params skip_interval > default
    // (0)
    int skipInterval = 0;
    auto skipIt = req.additionalParams.find("SKIP_INTERVAL");
    if (skipIt != req.additionalParams.end() && !skipIt->second.empty()) {
      try {
        skipInterval = std::stoi(skipIt->second);
        std::cerr
            << "[PipelineBuilderSourceNodes] Using SKIP_INTERVAL from additionalParams: "
            << skipInterval << std::endl;
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderSourceNodes] Warning: Invalid SKIP_INTERVAL value '"
                  << skipIt->second << "', using default 0" << std::endl;
      }
    } else if (params.count("skip_interval")) {
      skipInterval = std::stoi(params.at("skip_interval"));
    }

    // Get codec_type (h264, h265, auto)
    // Priority: additionalParams CODEC_TYPE > params codec_type > default
    // ("h264")
    std::string codecType = "h264";
    auto codecIt = req.additionalParams.find("CODEC_TYPE");
    if (codecIt != req.additionalParams.end() && !codecIt->second.empty()) {
      codecType = codecIt->second;
      std::cerr << "[PipelineBuilderSourceNodes] Using CODEC_TYPE from additionalParams: '"
                << codecType << "'" << std::endl;
    } else if (params.count("codec_type") && !params.at("codec_type").empty()) {
      codecType = params.at("codec_type");
    }

    // Priority: additionalParams RESIZE_RATIO > params resize_ratio > params
    // scale > default This allows runtime override of resize_ratio for RTSP
    // streams
    bool resizeRatioFromAdditionalParams = false;
    auto resizeIt = req.additionalParams.find("RESIZE_RATIO");
    if (resizeIt != req.additionalParams.end() && !resizeIt->second.empty()) {
      try {
        resize_ratio = std::stof(resizeIt->second);
        resizeRatioFromAdditionalParams = true;
        std::cerr
            << "[PipelineBuilderSourceNodes] Using RESIZE_RATIO from additionalParams: "
            << resize_ratio << std::endl;
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderSourceNodes] Warning: Invalid RESIZE_RATIO value '"
                  << resizeIt->second << "', using value from params or default"
                  << std::endl;
      }
    }

    // Check if resize_ratio is specified in params (from solution config)
    // Only override if RESIZE_RATIO was NOT set in additionalParams (highest
    // priority)
    if (!resizeRatioFromAdditionalParams) {
      // Helper function to check if a string is a placeholder
      auto isPlaceholder = [](const std::string &str) -> bool {
        return str.size() > 3 && str[0] == '$' && str[1] == '{' && str.back() == '}';
      };
      
      if (params.count("resize_ratio")) {
        std::string resizeRatioStr = params.at("resize_ratio");
        // Check if it's a placeholder that wasn't replaced
        if (isPlaceholder(resizeRatioStr)) {
          std::cerr << "[PipelineBuilderSourceNodes] Warning: resize_ratio is placeholder "
                    << resizeRatioStr << " (not replaced), keeping current value" << std::endl;
        } else {
          try {
            resize_ratio = std::stof(resizeRatioStr);
            std::cerr
                << "[PipelineBuilderSourceNodes] Using resize_ratio from solution config: "
                << resize_ratio << std::endl;
          } catch (const std::exception &e) {
            std::cerr << "[PipelineBuilderSourceNodes] Warning: Invalid resize_ratio value: "
                      << resizeRatioStr << ", keeping current value" << std::endl;
          }
        }
      } else if (params.count("scale")) {
        std::string scaleStr = params.at("scale");
        // Check if it's a placeholder that wasn't replaced
        if (isPlaceholder(scaleStr)) {
          std::cerr << "[PipelineBuilderSourceNodes] Warning: scale is placeholder "
                    << scaleStr << " (not replaced), keeping current value" << std::endl;
        } else {
          try {
            resize_ratio = std::stof(scaleStr);
            std::cerr << "[PipelineBuilderSourceNodes] Using scale from solution config: "
                      << resize_ratio << std::endl;
          } catch (const std::exception &e) {
            std::cerr << "[PipelineBuilderSourceNodes] Warning: Invalid scale value: "
                      << scaleStr << ", keeping current value" << std::endl;
          }
        }
      }
    }

    // Validate resize_ratio (must be > 0 and <= 1.0)
    // Note: Assertion in SDK checks: resize_ratio > 0 && resize_ratio <= 1.0
    // So we need: 0 < resize_ratio <= 1.0
    if (resize_ratio <= 0.0f) {
      std::cerr << "[PipelineBuilderSourceNodes] Invalid resize_ratio (<= 0): "
                << resize_ratio << ", using default 0.5" << std::endl;
      resize_ratio = 0.5f; // Use a safe default
    }
    if (resize_ratio > 1.0f) {
      std::cerr << "[PipelineBuilderSourceNodes] Invalid resize_ratio (> 1.0): "
                << resize_ratio << ", using 1.0" << std::endl;
      resize_ratio = 1.0f;
    }

    // Final validation before creating node
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }
    if (rtspUrl.empty()) {
      throw std::invalid_argument("RTSP URL cannot be empty");
    }
    // Final check: must be strictly > 0 and <= 1.0
    if (resize_ratio <= 0.0f || resize_ratio > 1.0f) {
      throw std::invalid_argument(
          "resize_ratio must be > 0.0 and <= 1.0, got: " +
          std::to_string(resize_ratio));
    }

    // Check RTSP transport protocol configuration
    // Priority: RTSP_TRANSPORT in additionalParams > GST_RTSP_PROTOCOLS env >
    // RTSP_TRANSPORT env > default (none - let GStreamer choose)
    std::string rtspTransport =
        ""; // Default: empty - let GStreamer use its default (UDP/tcp+udp)
    const char *rtspProtocols = std::getenv("GST_RTSP_PROTOCOLS");

    // Check additionalParams first (highest priority)
    auto rtspTransportIt = req.additionalParams.find("RTSP_TRANSPORT");
    if (rtspTransportIt != req.additionalParams.end() &&
        !rtspTransportIt->second.empty()) {
      std::string transport = rtspTransportIt->second;
      std::transform(transport.begin(), transport.end(), transport.begin(),
                     ::tolower);
      if (transport == "tcp" || transport == "udp") {
        rtspTransport = transport;
        setenv("GST_RTSP_PROTOCOLS", rtspTransport.c_str(),
               1); // Overwrite to use user's choice
        std::cerr << "[PipelineBuilderSourceNodes] Using RTSP_TRANSPORT=" << rtspTransport
                  << " from additionalParams" << std::endl;
      } else {
        std::cerr << "[PipelineBuilderSourceNodes] WARNING: Invalid RTSP_TRANSPORT='"
                  << transport
                  << "', must be 'tcp' or 'udp'. Using default: tcp"
                  << std::endl;
      }
    } else if (rtspProtocols && strlen(rtspProtocols) > 0) {
      // Use environment variable if set
      rtspTransport = std::string(rtspProtocols);
      std::transform(rtspTransport.begin(), rtspTransport.end(),
                     rtspTransport.begin(), ::tolower);
      if (rtspTransport != "tcp" && rtspTransport != "udp") {
        std::cerr << "[PipelineBuilderSourceNodes] WARNING: Invalid GST_RTSP_PROTOCOLS='"
                  << rtspTransport
                  << "', must be 'tcp' or 'udp'. Using default: tcp"
                  << std::endl;
        rtspTransport = "tcp";
      }
    } else {
      // Check RTSP_TRANSPORT environment variable
      const char *rtspTransportEnv = std::getenv("RTSP_TRANSPORT");
      if (rtspTransportEnv && strlen(rtspTransportEnv) > 0) {
        rtspTransport = std::string(rtspTransportEnv);
        std::transform(rtspTransport.begin(), rtspTransport.end(),
                       rtspTransport.begin(), ::tolower);
        if (rtspTransport == "tcp" || rtspTransport == "udp") {
          setenv("GST_RTSP_PROTOCOLS", rtspTransport.c_str(), 1);
        } else {
          rtspTransport = "tcp";
        }
      }
    }

    std::string transportInfo =
        rtspTransport.empty() ? "auto (GStreamer default)" : rtspTransport;

    std::cerr << "[PipelineBuilderSourceNodes] ========================================"
              << std::endl;
    std::cerr << "[PipelineBuilderSourceNodes] Creating RTSP source node:" << std::endl;
    std::cerr << "[PipelineBuilderSourceNodes]   Name: '" << nodeName
              << "' (length: " << nodeName.length() << ")" << std::endl;
    std::cerr << "[PipelineBuilderSourceNodes]   URL: '" << rtspUrl
              << "' (length: " << rtspUrl.length() << ")" << std::endl;
    std::cerr << "[PipelineBuilderSourceNodes]   Channel: " << channel << std::endl;
    std::cerr << "[PipelineBuilderSourceNodes]   Resize ratio: " << std::fixed
              << std::setprecision(3) << resize_ratio
              << " (type: float, value: " << static_cast<double>(resize_ratio)
              << ")" << std::endl;
    std::cerr << "[PipelineBuilderSourceNodes]   Decoder: '" << gstDecoderName << "'"
              << std::endl;
    std::cerr << "[PipelineBuilderSourceNodes]   Skip interval: " << skipInterval
              << " (0 = no skip)" << std::endl;
    std::cerr << "[PipelineBuilderSourceNodes]   Codec type: '" << codecType
              << "' (h264/h265/auto)" << std::endl;
    std::cerr << "[PipelineBuilderSourceNodes]   Transport: " << transportInfo
              << std::endl;
    if (rtspTransport == "udp") {
      std::cerr << "[PipelineBuilderSourceNodes]   NOTE: Using UDP transport (faster but "
                   "may be blocked by firewall)"
                << std::endl;
      std::cerr << "[PipelineBuilderSourceNodes]   NOTE: If connection fails, try TCP by "
                   "setting RTSP_TRANSPORT=tcp in additionalParams"
                << std::endl;
    } else if (rtspTransport == "tcp") {
      std::cerr << "[PipelineBuilderSourceNodes]   NOTE: Using TCP transport (better "
                   "firewall compatibility)"
                << std::endl;
      std::cerr << "[PipelineBuilderSourceNodes]   NOTE: To use UDP, set "
                   "RTSP_TRANSPORT=udp in additionalParams"
                << std::endl;
    } else {
      std::cerr << "[PipelineBuilderSourceNodes]   NOTE: Using GStreamer default "
                   "transport (usually UDP or tcp+udp)"
                << std::endl;
      std::cerr << "[PipelineBuilderSourceNodes]   NOTE: To force TCP, set "
                   "RTSP_TRANSPORT=tcp in additionalParams"
                << std::endl;
      std::cerr << "[PipelineBuilderSourceNodes]   NOTE: To force UDP, set "
                   "RTSP_TRANSPORT=udp in additionalParams"
                << std::endl;
    }
    std::cerr << "[PipelineBuilderSourceNodes]   NOTE: If decoder fails, try different "
                 "GST_DECODER_NAME (e.g., 'nvh264dec', 'qsvh264dec')"
              << std::endl;
    std::cerr << "[PipelineBuilderSourceNodes] ========================================"
              << std::endl;

    // Double-check resize_ratio is valid float
    if (std::isnan(resize_ratio) || std::isinf(resize_ratio)) {
      std::cerr << "[PipelineBuilderSourceNodes] ERROR: resize_ratio is NaN or Inf!"
                << std::endl;
      throw std::invalid_argument("resize_ratio is NaN or Inf");
    }

    // Ensure resize_ratio is strictly > 0 (not == 0) and <= 1.0
    if (resize_ratio <= 0.0f) {
      std::cerr << "[PipelineBuilderSourceNodes] ERROR: resize_ratio must be > 0.0, got: "
                << resize_ratio << std::endl;
      resize_ratio = 0.1f; // Use minimum valid value
      std::cerr << "[PipelineBuilderSourceNodes] Using minimum valid value: "
                << resize_ratio << std::endl;
    }
    if (resize_ratio > 1.0f) {
      std::cerr << "[PipelineBuilderSourceNodes] ERROR: resize_ratio must be <= 1.0, got: "
                << resize_ratio << std::endl;
      resize_ratio = 1.0f;
      std::cerr << "[PipelineBuilderSourceNodes] Using maximum valid value: "
                << resize_ratio << std::endl;
    }

    // Ensure GST_RTSP_PROTOCOLS is set before creating node (only if user
    // specified) This must be set BEFORE node creation as SDK reads it during
    // initialization
    const char *currentProtocols = std::getenv("GST_RTSP_PROTOCOLS");
    if (!rtspTransport.empty()) {
      // User specified transport - set it
      if (!currentProtocols || strlen(currentProtocols) == 0 ||
          std::string(currentProtocols) != rtspTransport) {
        setenv("GST_RTSP_PROTOCOLS", rtspTransport.c_str(),
               1); // Set to chosen transport
        std::cerr << "[PipelineBuilderSourceNodes] Set GST_RTSP_PROTOCOLS="
                  << rtspTransport << " before node creation" << std::endl;
      }
    } else {
      // No transport specified - let GStreamer use its default
      if (currentProtocols && strlen(currentProtocols) > 0) {
        std::cerr << "[PipelineBuilderSourceNodes] Using GST_RTSP_PROTOCOLS="
                  << currentProtocols << " from environment" << std::endl;
      } else {
        std::cerr << "[PipelineBuilderSourceNodes] No RTSP transport specified - "
                     "GStreamer will use default (UDP/tcp+udp)"
                  << std::endl;
      }
    }

    // Configure GStreamer environment variables for unstable streams
    // These settings help handle network issues and prevent crashes
    std::cerr << "[PipelineBuilderSourceNodes] Configuring GStreamer for unstable RTSP "
                 "streams..."
              << std::endl;

    // Set buffer and timeout settings to handle unstable streams
    // rtspsrc buffer-mode: 0=auto, 1=synced, 2=slave
    // Use synced mode for better stability with unstable streams
    if (!std::getenv("GST_RTSP_BUFFER_MODE")) {
      setenv("GST_RTSP_BUFFER_MODE", "1", 0); // 1 = synced mode
      std::cerr << "[PipelineBuilderSourceNodes] Set GST_RTSP_BUFFER_MODE=1 (synced mode "
                   "for unstable streams)"
                << std::endl;
    }

    // Increase buffer size to handle network jitter
    if (!std::getenv("GST_RTSP_BUFFER_SIZE")) {
      setenv("GST_RTSP_BUFFER_SIZE", "10485760", 0); // 10MB buffer
      std::cerr << "[PipelineBuilderSourceNodes] Set GST_RTSP_BUFFER_SIZE=10485760 (10MB "
                   "buffer for unstable streams)"
                << std::endl;
    }

    // Increase timeout to handle slow/unstable connections
    if (!std::getenv("GST_RTSP_TIMEOUT")) {
      setenv("GST_RTSP_TIMEOUT", "10000000000",
             0); // 10 seconds (in nanoseconds)
      std::cerr
          << "[PipelineBuilderSourceNodes] Set GST_RTSP_TIMEOUT=10000000000 (10s timeout)"
          << std::endl;
    }

    // Enable drop-on-latency to prevent buffer overflow
    if (!std::getenv("GST_RTSP_DROP_ON_LATENCY")) {
      setenv("GST_RTSP_DROP_ON_LATENCY", "true", 0);
      std::cerr << "[PipelineBuilderSourceNodes] Set GST_RTSP_DROP_ON_LATENCY=true (drop "
                   "frames on latency)"
                << std::endl;
    }

    // Set latency to handle network jitter (in nanoseconds)
    if (!std::getenv("GST_RTSP_LATENCY")) {
      setenv("GST_RTSP_LATENCY", "2000000000", 0); // 2 seconds latency
      std::cerr << "[PipelineBuilderSourceNodes] Set GST_RTSP_LATENCY=2000000000 (2s "
                   "latency buffer)"
                << std::endl;
    }

    // Disable do-rtsp-keep-alive to reduce connection overhead (may help with
    // unstable streams) Note: This is a trade-off - keep-alive helps detect
    // disconnections, but can cause issues with unstable streams

    // Create node - wrap in try-catch to catch any assertion failures
    std::shared_ptr<cvedix_nodes::cvedix_rtsp_src_node> node;
    try {
      std::cerr
          << "[PipelineBuilderSourceNodes] Calling cvedix_rtsp_src_node constructor..."
          << std::endl;
      std::cerr << "[PipelineBuilderSourceNodes] Current GST_RTSP_PROTOCOLS="
                << (std::getenv("GST_RTSP_PROTOCOLS")
                        ? std::getenv("GST_RTSP_PROTOCOLS")
                        : "not set")
                << std::endl;
      std::cerr << "[PipelineBuilderSourceNodes] GStreamer RTSP settings configured for "
                   "unstable streams"
                << std::endl;
      node = std::make_shared<cvedix_nodes::cvedix_rtsp_src_node>(
          nodeName, channel, rtspUrl, resize_ratio, gstDecoderName,
          skipInterval, codecType);
      std::cerr << "[PipelineBuilderSourceNodes] RTSP source node created successfully"
                << std::endl;
      std::cerr << "[PipelineBuilderSourceNodes] WARNING: SDK may use 'protocols=tcp+udp' "
                   "in GStreamer pipeline despite GST_RTSP_PROTOCOLS=tcp"
                << std::endl;
      std::cerr << "[PipelineBuilderSourceNodes] WARNING: This is a CVEDIX SDK limitation "
                   "- it hardcodes protocols in the pipeline string"
                << std::endl;
      std::cerr << "[PipelineBuilderSourceNodes] NOTE: RTSP connection may take 10-30 "
                   "seconds to establish"
                << std::endl;
      std::cerr << "[PipelineBuilderSourceNodes] NOTE: SDK will automatically retry "
                   "connection if stream is not immediately available"
                << std::endl;
      std::cerr << "[PipelineBuilderSourceNodes] NOTE: If connection succeeds but no "
                   "frames are received, check:"
                << std::endl;
      std::cerr << "[PipelineBuilderSourceNodes]   1. Decoder compatibility (avdec_h264 "
                   "may not work with all H264 streams)"
                << std::endl;
      std::cerr << "[PipelineBuilderSourceNodes]   2. Stream format (check if stream uses "
                   "H264 High profile)"
                << std::endl;
      std::cerr << "[PipelineBuilderSourceNodes]   3. Enable GStreamer debug: export "
                   "GST_DEBUG=rtspsrc:4,avdec_h264:4"
                << std::endl;
    } catch (const std::bad_alloc &e) {
      std::cerr << "[PipelineBuilderSourceNodes] Memory allocation failed: " << e.what()
                << std::endl;
      throw;
    } catch (const std::exception &e) {
      std::cerr << "[PipelineBuilderSourceNodes] Standard exception in constructor: "
                << e.what() << std::endl;
      throw;
    } catch (...) {
      // This might catch assertion failures on some systems
      std::cerr << "[PipelineBuilderSourceNodes] Non-standard exception in "
                   "cvedix_rtsp_src_node constructor"
                << std::endl;
      std::cerr << "[PipelineBuilderSourceNodes] Parameters were: name='" << nodeName
                << "', channel=" << channel << ", url='" << rtspUrl
                << "', resize_ratio=" << resize_ratio << std::endl;
      throw std::runtime_error("Failed to create RTSP source node - check "
                               "parameters and CVEDIX SDK");
    }

    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderSourceNodes] Exception in createRTSPSourceNode: "
              << e.what() << std::endl;
    throw;
  } catch (...) {
    std::cerr << "[PipelineBuilderSourceNodes] Unknown exception in createRTSPSourceNode"
              << std::endl;
    throw std::runtime_error("Unknown error creating RTSP source node");
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderSourceNodes::createFileSourceNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    // Get file path from params or request
    std::string filePath =
        params.count("file_path") ? params.at("file_path") : PipelineBuilderRequestUtils::getFilePath(req);
    int channel = params.count("channel") ? std::stoi(params.at("channel")) : 0;

    // Get resize_ratio: Priority: additionalParams > params > default
    // additionalParams takes highest priority to allow runtime override
    float resizeRatio =
        0.25f; // Default to 0.25 for fixed size (320x180 from 1280x720)

    // Helper function to check if a string is a placeholder
    auto isPlaceholder = [](const std::string &str) -> bool {
      return str.size() > 3 && str[0] == '$' && str[1] == '{' && str.back() == '}';
    };

    // First check additionalParams (highest priority - allows runtime override)
    auto it = req.additionalParams.find("RESIZE_RATIO");
    if (it != req.additionalParams.end() && !it->second.empty()) {
      try {
        resizeRatio = std::stof(it->second);
        std::cerr
            << "[PipelineBuilderSourceNodes] ✓ Using RESIZE_RATIO from additionalParams: "
            << resizeRatio << std::endl;
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderSourceNodes] Warning: Invalid RESIZE_RATIO in "
                     "additionalParams: "
                  << it->second << ", trying params..." << std::endl;
        // Fall through to check params
        if (params.count("resize_ratio")) {
          std::string resizeRatioStr = params.at("resize_ratio");
          // Check if it's a placeholder that wasn't replaced
          if (isPlaceholder(resizeRatioStr)) {
            std::cerr << "[PipelineBuilderSourceNodes] Warning: resize_ratio is placeholder "
                      << resizeRatioStr << " (not replaced), using default" << std::endl;
          } else {
            try {
              resizeRatio = std::stof(resizeRatioStr);
              std::cerr
                  << "[PipelineBuilderSourceNodes] Using resize_ratio from solution config: "
                  << resizeRatio << std::endl;
            } catch (const std::exception &e2) {
              std::cerr << "[PipelineBuilderSourceNodes] Warning: Invalid resize_ratio value: "
                        << resizeRatioStr << ", using default" << std::endl;
            }
          }
        }
      }
    } else {
      // RESIZE_RATIO not in additionalParams, check params
      if (params.count("resize_ratio")) {
        std::string resizeRatioStr = params.at("resize_ratio");
        // Check if it's a placeholder that wasn't replaced
        if (isPlaceholder(resizeRatioStr)) {
          std::cerr << "[PipelineBuilderSourceNodes] Warning: resize_ratio is placeholder "
                    << resizeRatioStr << " (not replaced), using default" << std::endl;
        } else {
          try {
            resizeRatio = std::stof(resizeRatioStr);
            std::cerr
                << "[PipelineBuilderSourceNodes] Using resize_ratio from solution config: "
                << resizeRatio << std::endl;
          } catch (const std::exception &e) {
            std::cerr << "[PipelineBuilderSourceNodes] Warning: Invalid resize_ratio value: "
                      << resizeRatioStr << ", using default" << std::endl;
          }
        }
      } else {
        std::cerr << "[PipelineBuilderSourceNodes] Using default resize_ratio: "
                  << resizeRatio << std::endl;
      }
    }

    // Validate parameters
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }
    if (filePath.empty()) {
      throw std::invalid_argument("File path cannot be empty");
    }
    if (resizeRatio <= 0.0f || resizeRatio > 1.0f) {
      std::cerr << "[PipelineBuilderSourceNodes] Warning: resize_ratio out of range ("
                << resizeRatio << "), using 0.25" << std::endl;
      resizeRatio = 0.25f;
    }

    std::cerr << "[PipelineBuilderSourceNodes] Creating file source node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  File path: '" << filePath << "'" << std::endl;
    std::cerr << "  Channel: " << channel << std::endl;
    std::cerr << "  Resize ratio: " << resizeRatio << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_file_src_node>(
        nodeName, channel, filePath, resizeRatio);

    std::cerr << "[PipelineBuilderSourceNodes] ✓ File source node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderSourceNodes] Exception in createFileSourceNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderSourceNodes::createAppSourceNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    int channelIndex =
        params.count("channel") ? std::stoi(params.at("channel")) : 0;

    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderSourceNodes] Creating app source node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Channel index: " << channelIndex << std::endl;
    std::cerr << "  NOTE: Use push_frames() method to push frames into pipeline"
              << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_app_src_node>(
        nodeName, channelIndex);

    std::cerr << "[PipelineBuilderSourceNodes] ✓ App source node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderSourceNodes] Exception in createAppSourceNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderSourceNodes::createImageSourceNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    int channelIndex =
        params.count("channel") ? std::stoi(params.at("channel")) : 0;
    std::string portOrLocation =
        params.count("port_or_location") ? params.at("port_or_location") : "";
    int interval =
        params.count("interval") ? std::stoi(params.at("interval")) : 1;
    // Helper function to check if a string is a placeholder
    auto isPlaceholder = [](const std::string &str) -> bool {
      return str.size() > 3 && str[0] == '$' && str[1] == '{' && str.back() == '}';
    };
    float resizeRatio = 1.0f; // Default
    if (params.count("resize_ratio")) {
      std::string resizeRatioStr = params.at("resize_ratio");
      if (isPlaceholder(resizeRatioStr)) {
        std::cerr << "[PipelineBuilderSourceNodes] Warning: resize_ratio is placeholder "
                  << resizeRatioStr << " (not replaced), using default 1.0" << std::endl;
      } else {
        try {
          resizeRatio = std::stof(resizeRatioStr);
        } catch (const std::exception &e) {
          std::cerr << "[PipelineBuilderSourceNodes] Warning: Invalid resize_ratio value: "
                    << resizeRatioStr << ", using default 1.0" << std::endl;
        }
      }
    }
    bool cycle = params.count("cycle") ? (params.at("cycle") == "true" ||
                                          params.at("cycle") == "1")
                                       : true;
    // Get decoder from config priority list if not specified
    std::string defaultDecoder = "jpegdec";
    std::string gstDecoderName =
        params.count("gst_decoder_name")
            ? params.at("gst_decoder_name")
            : selectDecoderFromPriority(defaultDecoder);

    // Get from additionalParams if not in params
    if (portOrLocation.empty()) {
      auto it = req.additionalParams.find("IMAGE_SRC_PORT_OR_LOCATION");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        portOrLocation = it->second;
      } else {
        // Default: Try production path first, then development path
        std::string productionPath = "/opt/edgeos-api/data/test_images/%d.jpg";
        if (fs::exists("/opt/edgeos-api/data/test_images")) {
          portOrLocation = productionPath;
          std::cerr << "[PipelineBuilderSourceNodes] Using default production image path: "
                    << productionPath << std::endl;
        } else {
          // Development fallback (will not exist in production)
          portOrLocation = "./cvedix_data/test_images/%d.jpg";
          std::cerr << "[PipelineBuilderSourceNodes] ⚠ WARNING: Using development default "
                       "image path (./cvedix_data/test_images/%d.jpg)"
                    << std::endl;
          std::cerr << "[PipelineBuilderSourceNodes] ℹ NOTE: In production, provide "
                       "'IMAGE_SRC_PORT_OR_LOCATION' or upload images to "
                       "/opt/edgeos-api/data/test_images/"
                    << std::endl;
        }
      }
    }

    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }
    if (portOrLocation.empty()) {
      throw std::invalid_argument("port_or_location cannot be empty");
    }

    // Validate resize_ratio
    if (resizeRatio <= 0.0f || resizeRatio > 1.0f) {
      std::cerr
          << "[PipelineBuilderSourceNodes] Warning: resize_ratio out of range, using 1.0"
          << std::endl;
      resizeRatio = 1.0f;
    }

    std::cerr << "[PipelineBuilderSourceNodes] Creating image source node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Channel index: " << channelIndex << std::endl;
    std::cerr << "  Port/Location: '" << portOrLocation << "'" << std::endl;
    std::cerr << "  Interval: " << interval << " seconds" << std::endl;
    std::cerr << "  Resize ratio: " << resizeRatio << std::endl;
    std::cerr << "  Cycle: " << (cycle ? "true" : "false") << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_image_src_node>(
        nodeName, channelIndex, portOrLocation, interval, resizeRatio, cycle,
        gstDecoderName);

    std::cerr << "[PipelineBuilderSourceNodes] ✓ Image source node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderSourceNodes] Exception in createImageSourceNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderSourceNodes::createRTMPSourceNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    int channelIndex =
        params.count("channel") ? std::stoi(params.at("channel")) : 0;
    std::string rtmpUrl = params.count("rtmp_url") ? params.at("rtmp_url") : "";
    // Helper function to check if a string is a placeholder
    auto isPlaceholder = [](const std::string &str) -> bool {
      return str.size() > 3 && str[0] == '$' && str[1] == '{' && str.back() == '}';
    };
    float resizeRatio = 1.0f; // Default
    if (params.count("resize_ratio")) {
      std::string resizeRatioStr = params.at("resize_ratio");
      if (isPlaceholder(resizeRatioStr)) {
        std::cerr << "[PipelineBuilderSourceNodes] Warning: resize_ratio is placeholder "
                  << resizeRatioStr << " (not replaced), using default 1.0" << std::endl;
      } else {
        try {
          resizeRatio = std::stof(resizeRatioStr);
        } catch (const std::exception &e) {
          std::cerr << "[PipelineBuilderSourceNodes] Warning: Invalid resize_ratio value: "
                    << resizeRatioStr << ", using default 1.0" << std::endl;
        }
      }
    }
    // Get decoder with automatic fallback
    // Priority: additionalParams GST_DECODER_NAME > params gst_decoder_name > auto-detection with fallback
    std::string gstDecoderName;
    
    // Check additionalParams first (highest priority)
    auto decoderIt = req.additionalParams.find("GST_DECODER_NAME");
    if (decoderIt != req.additionalParams.end() && !decoderIt->second.empty()) {
      gstDecoderName = decoderIt->second;
      std::cerr << "[PipelineBuilderSourceNodes] Using GST_DECODER_NAME from additionalParams: '"
                << gstDecoderName << "'" << std::endl;
    } else if (params.count("gst_decoder_name") && !params.at("gst_decoder_name").empty()) {
      gstDecoderName = params.at("gst_decoder_name");
      std::cerr << "[PipelineBuilderSourceNodes] Using gst_decoder_name from params: '"
                << gstDecoderName << "'" << std::endl;
    } else {
      // Auto-detect with fallback: nvh264dec -> vaapih264dec -> qsvh264dec -> avdec_h264
      gstDecoderName = selectDecoderWithFallback();
    }
    
    // Validate decoder availability and fallback if needed
    if (!isGStreamerDecoderAvailable(gstDecoderName)) {
      std::cerr << "[PipelineBuilderSourceNodes] ⚠ WARNING: Specified decoder '"
                << gstDecoderName << "' not available, falling back to auto-detection"
                << std::endl;
      gstDecoderName = selectDecoderWithFallback();
    }
    
    int skipInterval = params.count("skip_interval")
                           ? std::stoi(params.at("skip_interval"))
                           : 0;

    // Get from additionalParams if not in params
    if (rtmpUrl.empty()) {
      auto it = req.additionalParams.find("RTMP_SRC_URL");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        rtmpUrl = it->second;
      } else {
        // Default fallback (development only - should be overridden in
        // production) SECURITY: This is a development default. In production,
        // always provide RTMP_URL via request or environment variable.
        rtmpUrl = "rtmp://localhost:1935/live/stream";
      }
    }

    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }
    if (rtmpUrl.empty()) {
      throw std::invalid_argument("RTMP URL cannot be empty");
    }

    // Validate resize_ratio
    if (resizeRatio <= 0.0f || resizeRatio > 1.0f) {
      std::cerr
          << "[PipelineBuilderSourceNodes] Warning: resize_ratio out of range, using 1.0"
          << std::endl;
      resizeRatio = 1.0f;
    }

    std::cerr << "[PipelineBuilderSourceNodes] Creating RTMP source node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Channel index: " << channelIndex << std::endl;
    std::cerr << "  RTMP URL: '" << rtmpUrl << "'" << std::endl;
    std::cerr << "  Decoder: '" << gstDecoderName << "'" << std::endl;
    std::cerr << "  Resize ratio: " << resizeRatio << std::endl;
    std::cerr << "  Skip interval: " << skipInterval << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_rtmp_src_node>(
        nodeName, channelIndex, rtmpUrl, resizeRatio, gstDecoderName,
        skipInterval);

    std::cerr << "[PipelineBuilderSourceNodes] ✓ RTMP source node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderSourceNodes] Exception in createRTMPSourceNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderSourceNodes::createUDPSourceNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    int channelIndex =
        params.count("channel") ? std::stoi(params.at("channel")) : 0;
    int port = params.count("port") ? std::stoi(params.at("port")) : 6000;
    // Helper function to check if a string is a placeholder
    auto isPlaceholder = [](const std::string &str) -> bool {
      return str.size() > 3 && str[0] == '$' && str[1] == '{' && str.back() == '}';
    };
    float resizeRatio = 1.0f; // Default
    if (params.count("resize_ratio")) {
      std::string resizeRatioStr = params.at("resize_ratio");
      if (isPlaceholder(resizeRatioStr)) {
        std::cerr << "[PipelineBuilderSourceNodes] Warning: resize_ratio is placeholder "
                  << resizeRatioStr << " (not replaced), using default 1.0" << std::endl;
      } else {
        try {
          resizeRatio = std::stof(resizeRatioStr);
        } catch (const std::exception &e) {
          std::cerr << "[PipelineBuilderSourceNodes] Warning: Invalid resize_ratio value: "
                    << resizeRatioStr << ", using default 1.0" << std::endl;
        }
      }
    }
    // Get decoder from config priority list if not specified
    std::string defaultDecoder = "avdec_h264";
    std::string gstDecoderName =
        params.count("gst_decoder_name")
            ? params.at("gst_decoder_name")
            : selectDecoderFromPriority(defaultDecoder);
    int skipInterval = params.count("skip_interval")
                           ? std::stoi(params.at("skip_interval"))
                           : 0;

    // Get from additionalParams if not in params
    if (params.count("port") == 0) {
      auto it = req.additionalParams.find("UDP_PORT");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        try {
          port = std::stoi(it->second);
        } catch (...) {
          std::cerr << "[PipelineBuilderSourceNodes] Warning: Invalid UDP_PORT, using "
                       "default 6000"
                    << std::endl;
        }
      }
    }

    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }
    if (port <= 0 || port > 65535) {
      throw std::invalid_argument("UDP port must be between 1 and 65535");
    }

    // Validate resize_ratio
    if (resizeRatio <= 0.0f || resizeRatio > 1.0f) {
      std::cerr
          << "[PipelineBuilderSourceNodes] Warning: resize_ratio out of range, using 1.0"
          << std::endl;
      resizeRatio = 1.0f;
    }

    std::cerr << "[PipelineBuilderSourceNodes] Creating UDP source node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Channel index: " << channelIndex << std::endl;
    std::cerr << "  Port: " << port << std::endl;
    std::cerr << "  Resize ratio: " << resizeRatio << std::endl;
    std::cerr << "  Skip interval: " << skipInterval << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_udp_src_node>(
        nodeName, channelIndex, port, resizeRatio, gstDecoderName,
        skipInterval);

    std::cerr << "[PipelineBuilderSourceNodes] ✓ UDP source node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderSourceNodes] Exception in createUDPSourceNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderSourceNodes::createFFmpegSourceNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    // Get URI from params or request
    std::string uri = params.count("uri") ? params.at("uri") : "";
    if (uri.empty()) {
      // Try to get from FILE_PATH or HLS_URL or HTTP_URL
      auto filePathIt = req.additionalParams.find("FILE_PATH");
      auto hlsIt = req.additionalParams.find("HLS_URL");
      auto httpIt = req.additionalParams.find("HTTP_URL");

      if (hlsIt != req.additionalParams.end() && !hlsIt->second.empty()) {
        uri = hlsIt->second;
      } else if (httpIt != req.additionalParams.end() &&
                 !httpIt->second.empty()) {
        uri = httpIt->second;
      } else if (filePathIt != req.additionalParams.end() &&
                 !filePathIt->second.empty()) {
        uri = filePathIt->second;
      }
    }

    int channel = params.count("channel") ? std::stoi(params.at("channel")) : 0;
    // Helper function to check if a string is a placeholder
    auto isPlaceholder = [](const std::string &str) -> bool {
      return str.size() > 3 && str[0] == '$' && str[1] == '{' && str.back() == '}';
    };
    float resizeRatio = 1.0f; // Default
    if (params.count("resize_ratio")) {
      std::string resizeRatioStr = params.at("resize_ratio");
      if (isPlaceholder(resizeRatioStr)) {
        std::cerr << "[PipelineBuilderSourceNodes] Warning: resize_ratio is placeholder "
                  << resizeRatioStr << " (not replaced), using default 1.0" << std::endl;
      } else {
        try {
          resizeRatio = std::stof(resizeRatioStr);
        } catch (const std::exception &e) {
          std::cerr << "[PipelineBuilderSourceNodes] Warning: Invalid resize_ratio value: "
                    << resizeRatioStr << ", using default 1.0" << std::endl;
        }
      }
    }

    // Get resize_ratio from additionalParams if available
    auto resizeIt = req.additionalParams.find("RESIZE_RATIO");
    if (resizeIt != req.additionalParams.end() && !resizeIt->second.empty()) {
      try {
        resizeRatio = std::stof(resizeIt->second);
      } catch (...) {
        std::cerr
            << "[PipelineBuilderSourceNodes] Warning: Invalid RESIZE_RATIO, using default"
            << std::endl;
      }
    }

    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }
    if (uri.empty()) {
      throw std::invalid_argument("URI cannot be empty for FFmpeg source");
    }
    if (resizeRatio <= 0.0f || resizeRatio > 1.0f) {
      std::cerr
          << "[PipelineBuilderSourceNodes] Warning: resize_ratio out of range, using 1.0"
          << std::endl;
      resizeRatio = 1.0f;
    }

    std::cerr << "[PipelineBuilderSourceNodes] Creating FFmpeg source node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  URI: '" << uri << "'" << std::endl;
    std::cerr << "  Channel: " << channel << std::endl;
    std::cerr << "  Resize ratio: " << resizeRatio << std::endl;

    // Note: CVEDIX SDK may not have ff_src node header
    // Fallback: Use file_src with GStreamer urisourcebin pipeline
    // For HLS/HTTP streams, we'll use file_src node which internally uses
    // GStreamer The GStreamer backend should handle HLS/HTTP streams via
    // urisourcebin

    // Since CVEDIX SDK file_src uses GStreamer internally, it should support
    // HLS/HTTP We'll use file_src node with the URI directly
    auto node = std::make_shared<cvedix_nodes::cvedix_file_src_node>(
        nodeName, channel,
        uri, // Pass URI directly - GStreamer should handle it
        resizeRatio);

    std::cerr << "[PipelineBuilderSourceNodes] ✓ FFmpeg source node created successfully "
                 "(using file_src with GStreamer)"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderSourceNodes] Exception in createFFmpegSourceNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::string PipelineBuilderSourceNodes::detectInputType(const std::string &uri) {
  if (uri.empty()) {
    return "file";
  }

  // Trim whitespace
  std::string trimmedUri = uri;
  trimmedUri.erase(0, trimmedUri.find_first_not_of(" \t\n\r"));
  trimmedUri.erase(trimmedUri.find_last_not_of(" \t\n\r") + 1);

  if (trimmedUri.empty()) {
    return "file";
  }

  // Convert to lowercase for comparison
  std::string lowerUri = trimmedUri;
  std::transform(lowerUri.begin(), lowerUri.end(), lowerUri.begin(), ::tolower);

  // Check protocol prefixes (must be at start of string)
  if (lowerUri.find("rtsp://") == 0) {
    return "rtsp";
  } else if (lowerUri.find("rtmp://") == 0) {
    return "rtmp";
  } else if (lowerUri.find("hls://") == 0) {
    return "hls";
  } else if (lowerUri.find("http://") == 0 || lowerUri.find("https://") == 0) {
    // Check if it's HLS playlist (.m3u8 extension)
    // Check for .m3u8 at the end or before query parameters
    size_t m3u8Pos = lowerUri.find(".m3u8");
    if (m3u8Pos != std::string::npos) {
      // Check if .m3u8 is at the end or followed by ? or #
      size_t afterM3u8 = m3u8Pos + 5;
      if (afterM3u8 >= lowerUri.length() || lowerUri[afterM3u8] == '?' ||
          lowerUri[afterM3u8] == '#' || lowerUri[afterM3u8] == '/') {
        return "hls";
      }
    }
    return "http";
  } else if (lowerUri.find("udp://") == 0) {
    return "udp";
  }

  // Check file extension for HLS (must be at end or before query params)
  size_t m3u8Pos = lowerUri.find(".m3u8");
  if (m3u8Pos != std::string::npos) {
    size_t afterM3u8 = m3u8Pos + 5;
    if (afterM3u8 >= lowerUri.length() || lowerUri[afterM3u8] == '?' ||
        lowerUri[afterM3u8] == '#' || lowerUri[afterM3u8] == '/') {
      return "hls";
    }
  }

  // Default to local file
  return "file";
}

