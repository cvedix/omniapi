#include "core/pipeline_builder.h"
#include "config/system_config.h"
#include "core/cvedix_validator.h"
#include "core/frame_router.h"
#include "core/frame_router_sink_node.h"
#include "core/env_config.h"
#include "core/platform_detector.h"
#include "core/area_manager.h"
#include "core/securt_line_manager.h"
#include "core/securt_pipeline_integration.h"
#include "core/pipeline_builder_model_resolver.h"
#include "core/pipeline_builder_request_utils.h"
#include "core/pipeline_builder_source_nodes.h"
#include "core/pipeline_builder_destination_nodes.h"
#include "core/pipeline_builder_detector_nodes.h"
#include "core/pipeline_builder_broker_nodes.h"
#include "core/pipeline_builder_behavior_analysis_nodes.h"
#include "core/pipeline_builder_other_nodes.h"
#include <cstdlib> // For setenv
#include <cstring> // For strlen
#include <cvedix/nodes/ba/cvedix_ba_line_crossline_node.h>
#include <cvedix/nodes/ba/cvedix_ba_area_jam_node.h>
#include <cvedix/nodes/ba/cvedix_ba_stop_node.h>
#include <cvedix/nodes/ba/cvedix_ba_area_loitering_node.h>
#include <cvedix/nodes/des/cvedix_app_des_node.h>
#include <cvedix/nodes/des/cvedix_file_des_node.h>
#include <cvedix/nodes/des/cvedix_rtmp_des_node.h>
#include <cvedix/nodes/des/cvedix_screen_des_node.h>
#ifdef CVEDIX_WITH_GSTREAMER
#include <cvedix/nodes/des/cvedix_rtsp_des_node.h>
#endif
#ifdef CVEDIX_USE_SFACE_FEATURE_ENCODER
#include <cvedix/nodes/infers/cvedix_sface_feature_encoder_node.h>
#else
#include <cvedix/nodes/infers/cvedix_feature_encoder_node.h>
#endif
#include <cvedix/nodes/infers/cvedix_face_detector_node.h>
#include <cvedix/nodes/osd/cvedix_ba_line_crossline_osd_node.h>
#include <cvedix/nodes/osd/cvedix_ba_area_jam_osd_node.h>
#include <cvedix/nodes/osd/cvedix_ba_stop_osd_node.h>
#include <cvedix/nodes/osd/cvedix_ba_area_enter_exit_osd_node.h>
#include <cvedix/nodes/osd/cvedix_face_osd_node_v2.h>
#include <cvedix/nodes/osd/cvedix_osd_node_v3.h>
#include <cvedix/nodes/src/cvedix_app_src_node.h>
#include <cvedix/nodes/src/cvedix_file_src_node.h>
#include <cvedix/nodes/src/cvedix_image_src_node.h>
#include <cvedix/nodes/src/cvedix_rtmp_src_node.h>
#include <cvedix/nodes/src/cvedix_rtsp_src_node.h>
#include <cvedix/nodes/src/cvedix_udp_src_node.h>
#include <cvedix/nodes/track/cvedix_sort_track_node.h>
#include <cvedix/nodes/track/cvedix_bytetrack_node.h>
// TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
// #include <cvedix/nodes/track/cvedix_ocsort_track_node.h>
#include <cvedix/objects/shapes/cvedix_line.h>
#include <cvedix/objects/shapes/cvedix_point.h>
#include <cvedix/objects/shapes/cvedix_size.h>
#include <cvedix/utils/cvedix_utils.h>
#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>
#include <chrono>
#include <opencv2/core.hpp> // For cv::Exception

// TensorRT Inference Nodes
#ifdef CVEDIX_WITH_TRT
#include <cvedix/nodes/infers/cvedix_trt_vehicle_color_classifier.h>
#include <cvedix/nodes/infers/cvedix_trt_vehicle_detector.h>
#include <cvedix/nodes/infers/cvedix_trt_vehicle_feature_encoder.h>
#include <cvedix/nodes/infers/cvedix_trt_vehicle_plate_detector.h>
#include <cvedix/nodes/infers/cvedix_trt_vehicle_plate_detector_v2.h>
#include <cvedix/nodes/infers/cvedix_trt_vehicle_scanner.h>
#include <cvedix/nodes/infers/cvedix_trt_vehicle_type_classifier.h>
#include <cvedix/nodes/infers/cvedix_trt_yolov8_classifier.h>
#include <cvedix/nodes/infers/cvedix_trt_yolov8_detector.h>
#include <cvedix/nodes/infers/cvedix_trt_yolov8_pose_detector.h>
#include <cvedix/nodes/infers/cvedix_trt_yolov8_seg_detector.h>
#endif

// RKNN Inference Nodes
#ifdef CVEDIX_WITH_RKNN
#include <cvedix/nodes/infers/cvedix_rknn_face_detector_node.h>
#include <cvedix/nodes/infers/cvedix_rknn_yolov11_detector_node.h>
#include <cvedix/nodes/infers/cvedix_rknn_yolov8_detector_node.h>
#endif

// Other Inference Nodes
#include <cvedix/nodes/infers/cvedix_yolo_detector_node.h>
// Note: cvedix_yolov11_detector_node.h is not available in CVEDIX SDK
// Use rknn_yolov11_detector (with CVEDIX_WITH_RKNN) or yolo_detector instead
// #include <cvedix/nodes/infers/cvedix_yolov11_detector_node.h>
#include <cvedix/nodes/infers/cvedix_classifier_node.h>
#include <cvedix/nodes/infers/cvedix_enet_seg_node.h>
#include <cvedix/nodes/infers/cvedix_mask_rcnn_detector_node.h>
#ifdef CVEDIX_HAS_OPENPOSE
#include <cvedix/nodes/infers/cvedix_openpose_detector_node.h>
#endif
// Generic embedding encoder (SDK: cvedix_feature_encoder_node; older SDKs used
// cvedix_sface_feature_encoder_node). Vehicle TRT path uses
// cvedix_trt_vehicle_feature_encoder separately.
#ifdef CVEDIX_HAS_FACE_SWAP
#include <cvedix/nodes/infers/cvedix_face_swap_node.h>
#endif
#include <cvedix/nodes/infers/cvedix_lane_detector_node.h>
#ifdef CVEDIX_WITH_LLM
#include <cvedix/nodes/infers/cvedix_mllm_analyser_node.h>
#endif
#ifdef CVEDIX_WITH_PADDLE
#include <cvedix/nodes/infers/cvedix_ppocr_text_detector_node.h>
#endif
#ifdef CVEDIX_HAS_RESTORATION
#include <cvedix/nodes/infers/cvedix_restoration_node.h>
#endif

// TensorRT Additional Inference Nodes
#ifdef CVEDIX_WITH_TRT
#include <cvedix/nodes/infers/cvedix_trt_insight_face_recognition_node.h>
#endif
#include <atomic>
#include <functional>
#include <tuple>
#include <vector>

// Broker Nodes
#ifdef CVEDIX_WITH_MQTT
#include <chrono>
#include <ctime>
// TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
// #include <cvedix/nodes/broker/cereal_archive/cvedix_objects_cereal_archive.h>
#include <cvedix/nodes/broker/cvedix_json_enhanced_console_broker_node.h>
#include <cvedix/nodes/broker/cvedix_json_mqtt_broker_node.h>
#include <cvedix/utils/mqtt_client/cvedix_mqtt_client.h>
#include <future>
#include <mutex>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif
#ifdef CVEDIX_WITH_KAFKA
// #include <cvedix/nodes/broker/cvedix_json_kafka_broker_node.h>
#endif
// Broker nodes (require cereal - now enabled)
// TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
// #include <cvedix/nodes/broker/cvedix_xml_file_broker_node.h>
// #include <cvedix/nodes/broker/cvedix_xml_socket_broker_node.h>
#include <cvedix/nodes/broker/cvedix_msg_broker_node.h>
#include <cvedix/nodes/broker/cvedix_ba_socket_broker_node.h>
#include <cvedix/nodes/broker/cvedix_embeddings_socket_broker_node.h>
#include <cvedix/nodes/broker/cvedix_embeddings_properties_socket_broker_node.h>
#include <cvedix/nodes/broker/cvedix_plate_socket_broker_node.h>
#include <cvedix/nodes/broker/cvedix_expr_socket_broker_node.h>
#ifdef CVEDIX_WITH_KAFKA
#include <cvedix/nodes/broker/cvedix_json_kafka_broker_node.h>
#endif
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <typeinfo>
// Include cmath AFTER CVEDIX SDK headers to avoid macro conflict with rapidxml
// The macro 'pi' from cmath conflicts with variable 'pi' in rapidxml
#include <cmath>
// Use standard filesystem (C++17)
#include <filesystem>
namespace fs = std::filesystem;

// Static flag to ensure CVEDIX logger is initialized only once
static std::once_flag cvedix_init_flag;

// Helper function to select decoder from priority list (currently unused; kept for future use)
static __attribute__((unused)) std::string
selectDecoderFromPriority(const std::string &defaultDecoder) {
  try {
    auto &systemConfig = SystemConfig::getInstance();
    auto decoderList = systemConfig.getDecoderPriorityList();

    if (decoderList.empty()) {
      return defaultDecoder;
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

    // Try decoders in priority order
    for (const auto &priorityDecoder : decoderList) {
      auto it = decoderMap.find(priorityDecoder);
      if (it != decoderMap.end()) {
        // Check if decoder is available (simple check - could be enhanced)
        // For now, return first available in priority order
        std::cerr << "[PipelineBuilder] Selected decoder from priority: "
                  << priorityDecoder << " -> " << it->second << std::endl;
        return it->second;
      }
    }

    // If no match, return default
    return defaultDecoder;
  } catch (...) {
    // On error, return default
    return defaultDecoder;
  }
}

// Helper function to log GPU availability (currently unused; kept for debugging)
static __attribute__((unused)) void logGPUAvailability() {
  std::cerr << "[PipelineBuilder] ========================================"
            << std::endl;
  std::cerr << "[PipelineBuilder] Checking GPU availability for inference..."
            << std::endl;

  bool hasGPU = false;

  // Check NVIDIA GPU
  if (PlatformDetector::isNVIDIA()) {
    std::cerr << "[PipelineBuilder] ✓ NVIDIA GPU detected" << std::endl;
    std::cerr << "[PipelineBuilder]   → TensorRT devices (tensorrt.1, "
                 "tensorrt.2) may be available"
              << std::endl;
    hasGPU = true;
  }

  // Check Intel GPU
  if (PlatformDetector::isVAAPI()) {
    std::cerr << "[PipelineBuilder] ✓ Intel GPU (VAAPI) detected" << std::endl;
    hasGPU = true;
  }

  if (PlatformDetector::isMSDK()) {
    std::cerr << "[PipelineBuilder] ✓ Intel GPU (MSDK) detected" << std::endl;
    hasGPU = true;
  }

  // Check Jetson
  if (PlatformDetector::isJetson()) {
    std::cerr << "[PipelineBuilder] ✓ NVIDIA Jetson detected" << std::endl;
    std::cerr << "[PipelineBuilder]   → TensorRT devices may be available"
              << std::endl;
    hasGPU = true;
  }

  if (!hasGPU) {
    std::cerr << "[PipelineBuilder] ⚠ No GPU detected - inference will use CPU"
              << std::endl;
    std::cerr << "[PipelineBuilder]   → CPU inference is slower and may cause "
                 "queue overflow"
              << std::endl;
    std::cerr << "[PipelineBuilder]   → Consider using frame dropping (already "
                 "enabled) or reducing FPS"
              << std::endl;
  } else {
    std::cerr << "[PipelineBuilder] ✓ GPU detected - check config.json "
                 "auto_device_list to ensure GPU is prioritized"
              << std::endl;
    std::cerr << "[PipelineBuilder]   → GPU devices should be listed before "
                 "CPU in auto_device_list"
              << std::endl;
  }

  // Check auto_device_list configuration
  try {
    auto &systemConfig = SystemConfig::getInstance();
    auto deviceList = systemConfig.getAutoDeviceList();
    if (!deviceList.empty()) {
      std::cerr << "[PipelineBuilder] Current auto_device_list (first 5): ";
      size_t count = 0;
      for (const auto &device : deviceList) {
        if (count++ < 5) {
          std::cerr << device << " ";
        }
      }
      std::cerr << "..." << std::endl;
      std::cerr << "[PipelineBuilder] TIP: Ensure GPU devices (openvino.GPU, "
                   "tensorrt.1, etc.) are listed before CPU"
                << std::endl;
    }
  } catch (...) {
    // Ignore errors
  }

  std::cerr << "[PipelineBuilder] ========================================"
            << std::endl;
}

// Helper function to get GStreamer pipeline from config
// Note: Currently not used but available for future integration
[[maybe_unused]] static std::string getGStreamerPipelineForPlatform() {
  try {
    auto &systemConfig = SystemConfig::getInstance();
    std::string platform = PlatformDetector::detectPlatform();
    std::string pipeline = systemConfig.getGStreamerPipeline(platform);

    if (!pipeline.empty()) {
      std::cerr << "[PipelineBuilder] Using GStreamer pipeline from config for "
                   "platform '"
                << platform << "': " << pipeline << std::endl;
      return pipeline;
    }

    // Fallback to auto if platform-specific not found
    pipeline = systemConfig.getGStreamerPipeline("auto");
    if (!pipeline.empty()) {
      std::cerr
          << "[PipelineBuilder] Using GStreamer pipeline from config (auto): "
          << pipeline << std::endl;
      return pipeline;
    }
  } catch (...) {
    // On error, return empty (will use default)
  }

  return "";
}

// Initialize CVEDIX SDK logger (required before creating nodes)
static void initCVEDIXLoggerOnce() {
  std::call_once(cvedix_init_flag, []() {
    try {
      // Configure GStreamer RTSP transport protocol if specified
      // This helps avoid UDP firewall issues by forcing TCP
      const char *rtspProtocols = std::getenv("GST_RTSP_PROTOCOLS");
      if (!rtspProtocols || strlen(rtspProtocols) == 0) {
        // Check if RTSP_TRANSPORT is set (alternative name)
        const char *rtspTransport = std::getenv("RTSP_TRANSPORT");
        if (rtspTransport && strlen(rtspTransport) > 0) {
          std::string transport = rtspTransport;
          std::transform(transport.begin(), transport.end(), transport.begin(),
                         ::tolower);
          if (transport == "tcp" || transport == "udp") {
            setenv("GST_RTSP_PROTOCOLS", transport.c_str(),
                   0); // Don't overwrite if already set
            std::cerr << "[PipelineBuilder] Set GST_RTSP_PROTOCOLS="
                      << transport
                      << " from RTSP_TRANSPORT environment variable"
                      << std::endl;
          }
        } else {
          // Don't set default - let GStreamer use its default (UDP, or tcp+udp)
          // This allows VLC RTSP server and other UDP-only servers to work
          // User can explicitly set GST_RTSP_PROTOCOLS=tcp if needed for
          // firewall compatibility
          std::cerr << "[PipelineBuilder] GST_RTSP_PROTOCOLS not set - using "
                       "GStreamer default (UDP/tcp+udp)"
                    << std::endl;
          std::cerr << "[PipelineBuilder] NOTE: To force TCP, set "
                       "GST_RTSP_PROTOCOLS=tcp before starting"
                    << std::endl;
          std::cerr << "[PipelineBuilder] NOTE: To force UDP, set "
                       "GST_RTSP_PROTOCOLS=udp before starting"
                    << std::endl;
        }
      } else {
        std::cerr << "[PipelineBuilder] Using GST_RTSP_PROTOCOLS="
                  << rtspProtocols << " from environment" << std::endl;
      }

      // Apply GStreamer plugin ranks from config
      try {
        auto &systemConfig = SystemConfig::getInstance();
        // Get all plugin ranks from config
        // Note: GStreamer plugin ranks are set via environment variables
        // Format: GST_PLUGIN_FEATURE_RANK=plugin1:rank1,plugin2:rank2,...
        std::vector<std::string> pluginRanks;
        std::vector<std::string> knownPlugins = {
            "nvv4l2decoder",   "nvjpegdec",
            "nvjpegenc",       "nvvidconv",
            "msdkvpp",         "vaapipostproc",
            "vpldec",          "qsv",
            "qsvh265dec",      "qsvh264dec",
            "qsvh265enc",      "qsvh264enc",
            "amfh264dec",      "amfh265dec",
            "amfhvp9dec",      "amfhav1dec",
            "nvh264dec",       "nvh265dec",
            "nvh264enc",       "nvh265enc",
            "nvvp9dec",        "nvvp9enc",
            "nvmpeg4videodec", "nvmpeg2videodec",
            "nvmpegvideodec",  "mpph264enc",
            "mpph265enc",      "mppvp8enc",
            "mppjpegenc",      "mppvideodec",
            "mppjpegdec"};

        for (const auto &plugin : knownPlugins) {
          std::string rank = systemConfig.getGStreamerPluginRank(plugin);
          if (!rank.empty()) {
            pluginRanks.push_back(plugin + ":" + rank);
          }
        }

        if (!pluginRanks.empty()) {
          std::string rankEnv = "";
          for (size_t i = 0; i < pluginRanks.size(); ++i) {
            if (i > 0)
              rankEnv += ",";
            rankEnv += pluginRanks[i];
          }
          setenv("GST_PLUGIN_FEATURE_RANK", rankEnv.c_str(), 0);
          std::cerr
              << "[PipelineBuilder] Set GStreamer plugin ranks from config: "
              << pluginRanks.size() << " plugins" << std::endl;
        }
      } catch (...) {
        std::cerr << "[PipelineBuilder] Warning: Failed to apply GStreamer "
                     "plugin ranks from config"
                  << std::endl;
      }

      // Set CVEDIX log level. Priority: CVEDIX_LOG_LEVEL env > config value.
      // Options: ERROR, WARNING/WARN, INFO, DEBUG (case-insensitive).
      auto parse_cvedix = [](const char *s) {
        cvedix_utils::cvedix_log_level lv =
            cvedix_utils::cvedix_log_level::WARN;
        if (!s || !s[0]) {
          return lv;
        }
        std::string u(s);
        std::transform(u.begin(), u.end(), u.begin(), ::toupper);
        if (u == "DEBUG") return cvedix_utils::cvedix_log_level::DEBUG;
        if (u == "INFO") return cvedix_utils::cvedix_log_level::INFO;
        if (u == "WARNING" || u == "WARN")
          return cvedix_utils::cvedix_log_level::WARN;
        if (u == "ERROR") return cvedix_utils::cvedix_log_level::ERROR;
        return lv;
      };

      const std::string cfg_level =
          SystemConfig::getInstance().getLoggingConfig().cvedixLogLevel;
      const char *env_cvedix_log = std::getenv("CVEDIX_LOG_LEVEL");
      cvedix_utils::cvedix_log_level cvedix_log_level =
          (env_cvedix_log && env_cvedix_log[0])
              ? parse_cvedix(env_cvedix_log)
              : parse_cvedix(cfg_level.c_str());

      CVEDIX_SET_LOG_LEVEL(cvedix_log_level);
      CVEDIX_LOGGER_INIT();
      std::string log_level_name = (env_cvedix_log && env_cvedix_log[0])
                                       ? std::string(env_cvedix_log)
                                       : (cfg_level.empty() ? "WARN" : cfg_level);
      std::cerr
          << "[PipelineBuilder] CVEDIX SDK logger initialized (log level: "
          << log_level_name << ")" << std::endl;
      if (cvedix_log_level == cvedix_utils::cvedix_log_level::ERROR) {
        std::cerr << "[PipelineBuilder] NOTE: WARNING logs suppressed; set "
                     "cvedix_log_level=warning or CVEDIX_LOG_LEVEL=WARNING\n";
      }
    } catch (const std::exception &e) {
      std::cerr
          << "[PipelineBuilder] Warning: Failed to initialize CVEDIX logger: "
          << e.what() << std::endl;
    } catch (...) {
      std::cerr << "[PipelineBuilder] Warning: Unknown error initializing "
                   "CVEDIX logger"
                << std::endl;
    }
  });
}

void PipelineBuilder::ensureCVEDIXInitialized() {
  initCVEDIXLoggerOnce();
}

std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>>
PipelineBuilder::buildPipeline(const SolutionConfig &solution,
                               const CreateInstanceRequest &req,
                               const std::string &instanceId,
                               const std::set<std::string> &existingRTMPStreamKeys,
                               edgeos::FrameRouter* frameRouter) {

  frame_router_ = frameRouter;
  if (frame_router_) {
    std::cerr << "[PipelineBuilder] Zero-downtime mode: rtmp_des will use FrameRouterSinkNode"
              << std::endl;
  }

  // Ensure CVEDIX SDK is initialized before creating nodes
  initCVEDIXLoggerOnce();

  if (EnvConfig::getBool("EDGE_AI_VERBOSE", false)) {
    std::cerr << "[PipelineBuilder] ========================================"
              << std::endl;
    std::cerr << "[PipelineBuilder] Building pipeline for solution: "
              << solution.solutionId << std::endl;
    std::cerr << "[PipelineBuilder] Solution name: " << solution.solutionName
              << std::endl;
    std::cerr << "[PipelineBuilder] Instance ID: " << instanceId << std::endl;
    std::cerr << "[PipelineBuilder] NOTE: This may be a new instance or "
                 "rebuilding after stop/restart"
              << std::endl;
    std::cerr << "[PipelineBuilder] Pipeline will contain "
              << solution.pipeline.size() << " nodes:" << std::endl;
    for (size_t i = 0; i < solution.pipeline.size(); ++i) {
      const auto &nodeConfig = solution.pipeline[i];
      std::cerr << "[PipelineBuilder]   " << (i + 1) << ". "
                << nodeConfig.nodeType << " (" << nodeConfig.nodeName << ")"
                << std::endl;
    }
    std::cerr << "[PipelineBuilder] ========================================"
              << std::endl;
  }

  // ========================================================================
  // SecuRT Integration: Load areas and lines from managers
  // ========================================================================
  // Create mutable copy of request for SecuRT integration
  CreateInstanceRequest mutableReq = req;
  loadSecuRTData(solution, mutableReq, instanceId);

  std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> nodes;
  std::vector<std::string> nodeTypes; // Track node types for connection logic

  // Helper function to check if a node type already exists in pipeline
  auto hasNodeType = [&nodeTypes](const std::string &nodeType) -> bool {
    for (const auto &type : nodeTypes) {
      if (type == nodeType) {
        return true;
      }
    }
    return false;
  };

  // Check for multiple video sources (FILE_PATHS or RTSP_URLS array)
  auto [hasMultipleSources, hasCustomSourceNodes, multipleSourceType, multipleSourceNodes] =
      handleMultipleSources(mutableReq, instanceId, nodes, nodeTypes);

  // Check if pipeline has OSD node (for RTMP node OSD auto-enable logic)
  bool hasOSDNode = false;
  for (const auto &nodeConfig : solution.pipeline) {
    if (nodeConfig.nodeType == "face_osd_v2" ||
        nodeConfig.nodeType == "osd_v3" ||
        nodeConfig.nodeType == "ba_crossline_osd" ||
        nodeConfig.nodeType == "ba_jam_osd" ||
        nodeConfig.nodeType == "ba_stop_osd" ||
        nodeConfig.nodeType == "ba_loitering_osd" ||
        nodeConfig.nodeType == "ba_area_enter_exit_osd") {
      hasOSDNode = true;
      break;
    }
  }

  // Effective input type: only use destination node that matches input for optimization.
  // file_des only when input is a local file path; RTMP/RTSP/HLS/HTTP use rtmp_des/rtsp_des etc.
  std::string effectiveInputType = "file";
  if (hasCustomSourceNodes) {
    if (multipleSourceType == "file_src") effectiveInputType = "file";
    else if (multipleSourceType == "rtsp_src") effectiveInputType = "rtsp";
    else if (multipleSourceType == "rtmp_src") effectiveInputType = "rtmp";
    else effectiveInputType = "file";
  } else {
    auto rtmpSrcIt = mutableReq.additionalParams.find("RTMP_SRC_URL");
    if (rtmpSrcIt != mutableReq.additionalParams.end() && !rtmpSrcIt->second.empty()) {
      effectiveInputType = "rtmp";
    } else {
      auto rtspSrcIt = mutableReq.additionalParams.find("RTSP_URL");
      if (rtspSrcIt != mutableReq.additionalParams.end() && !rtspSrcIt->second.empty()) {
        effectiveInputType = "rtsp";
      } else {
        auto filePathIt = mutableReq.additionalParams.find("FILE_PATH");
        if (filePathIt != mutableReq.additionalParams.end() && !filePathIt->second.empty()) {
          std::string fp = filePathIt->second;
          fp.erase(0, fp.find_first_not_of(" \t\n\r"));
          fp.erase(fp.find_last_not_of(" \t\n\r") + 1);
          if (!fp.empty()) {
            effectiveInputType = PipelineBuilderSourceNodes::detectInputType(fp);
          }
        }
      }
    }
  }

  // Build nodes in pipeline order
  auto parseBoolParam = [](const std::map<std::string, std::string> &params,
                           const std::string &key) -> bool {
    auto it = params.find(key);
    if (it == params.end()) {
      return false;
    }
    std::string value = it->second;
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return (value == "1" || value == "true" || value == "yes" ||
            value == "on");
  };

  auto hasFaceDetectorInBasePipeline = [&solution]() -> bool {
    for (const auto &cfg : solution.pipeline) {
      if (cfg.nodeType == "yunet_face_detector" ||
          cfg.nodeType == "rknn_face_detector" ||
          cfg.nodeType == "trt_yolov11_face_detector") {
        return true;
      }
    }
    return false;
  };

  auto isDestinationNodeType = [](const std::string &nodeType) -> bool {
    return nodeType == "file_des" || nodeType == "rtmp_des" ||
           nodeType == "frame_router_sink" || nodeType == "rtsp_des" ||
           nodeType == "screen_des" || nodeType == "app_des";
  };

  const bool globalFaceDetectionEnabled =
      parseBoolParam(mutableReq.additionalParams, "ENABLE_FACE_DETECTION");
  const bool pipelineAlreadyHasFaceDetector = hasFaceDetectorInBasePipeline();
  bool optionalFaceDetectorInjected = false;

  for (const auto &nodeConfig : solution.pipeline) {
    try {
      std::cerr << "[PipelineBuilder] Creating node: " << nodeConfig.nodeType
                << " (" << nodeConfig.nodeName << ")" << std::endl;

      // For RTMP nodes: if pipeline doesn't have OSD node and OSD parameter not
      // explicitly set, enable OSD overlay in RTMP node
      SolutionConfig::NodeConfig modifiedNodeConfig = nodeConfig;
      if (nodeConfig.nodeType == "rtmp_des" && !hasOSDNode) {
        // Check if OSD parameter is already set
        if (modifiedNodeConfig.parameters.find("osd") ==
            modifiedNodeConfig.parameters.end()) {
          modifiedNodeConfig.parameters["osd"] = "true";
          std::cerr << "[PipelineBuilder] NOTE: Pipeline has no OSD node, "
                       "enabling OSD overlay in RTMP node"
                    << std::endl;
        }
      }

      // Skip source node creation if we already have multiple source nodes
      // Skip source nodes from solution config if we already created custom source nodes
      if (hasCustomSourceNodes &&
          (nodeConfig.nodeType == "file_src" || nodeConfig.nodeType == "rtsp_src" ||
           nodeConfig.nodeType == "rtmp_src")) {
        std::cerr << "[PipelineBuilder] Skipping " << nodeConfig.nodeType << " node from solution config (using " 
                  << (multipleSourceType == "file_src"
                          ? "FILE_PATHS"
                          : (multipleSourceType == "rtsp_src" ? "RTSP_URLS"
                                                                : "custom source array"))
                  << " array instead)" << std::endl;
        continue;
      }
      
      // Skip ba_crossline and ba_crossline_osd nodes if no CrossingLines (or line params) are provided
      // This allows running only ba_loitering without ba_crossline
      if (nodeConfig.nodeType == "ba_crossline" || nodeConfig.nodeType == "ba_crossline_osd") {
        auto crossingLinesIt = mutableReq.additionalParams.find("CrossingLines");
        bool hasCrossingLines = (crossingLinesIt != mutableReq.additionalParams.end() &&
                                !crossingLinesIt->second.empty());
        if (!hasCrossingLines) {
          auto sx = mutableReq.additionalParams.find("CROSSLINE_START_X");
          auto sy = mutableReq.additionalParams.find("CROSSLINE_START_Y");
          auto ex = mutableReq.additionalParams.find("CROSSLINE_END_X");
          auto ey = mutableReq.additionalParams.find("CROSSLINE_END_Y");
          if (sx != mutableReq.additionalParams.end() && !sx->second.empty() &&
              sy != mutableReq.additionalParams.end() && !sy->second.empty() &&
              ex != mutableReq.additionalParams.end() && !ex->second.empty() &&
              ey != mutableReq.additionalParams.end() && !ey->second.empty()) {
            hasCrossingLines = true;
          }
        }
        if (!hasCrossingLines) {
          std::cerr << "[PipelineBuilder] Skipping " << nodeConfig.nodeType 
                    << " node (no CrossingLines or CROSSLINE_* params - running ba_loitering only)"
                    << std::endl;
          continue;
        }
      }

      // Backward compatibility for SecuRT-specific optional face detector node.
      if (nodeConfig.nodeType == "yunet_face_detector" &&
          nodeConfig.nodeName.find("securt_face_detector_") == 0) {
        const bool securtFaceEnabled =
            parseBoolParam(mutableReq.additionalParams,
                           "SECURT_FACE_DETECTION_ENABLE") ||
            parseBoolParam(mutableReq.additionalParams,
                           "ENABLE_FACE_DETECTION");
        if (!securtFaceEnabled) {
          std::cerr << "[PipelineBuilder] Skipping securt_face_detector node "
                       "(face detection disabled)"
                    << std::endl;
          continue;
        }
      }

      // Global optional face detector for all solutions:
      // inject once, right before the first destination node.
      if (!optionalFaceDetectorInjected && globalFaceDetectionEnabled &&
          !pipelineAlreadyHasFaceDetector &&
          isDestinationNodeType(nodeConfig.nodeType)) {
        SolutionConfig::NodeConfig optFaceCfg;
        optFaceCfg.nodeType = "yunet_face_detector";
        optFaceCfg.nodeName = "optional_face_detector_{instanceId}";
        optFaceCfg.parameters["model_path"] = "${FACE_DETECTION_MODEL_PATH}";
        optFaceCfg.parameters["score_threshold"] = "${detectionSensitivity}";
        optFaceCfg.parameters["nms_threshold"] = "0.5";
        optFaceCfg.parameters["top_k"] = "50";

        std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> optExtraNodes;
        auto optFaceNode = createNode(optFaceCfg, mutableReq, instanceId,
                                      existingRTMPStreamKeys, &optExtraNodes);
        if (optFaceNode) {
          nodes.push_back(optFaceNode);
          nodeTypes.push_back(optFaceCfg.nodeType);
          for (const auto &extra : optExtraNodes) {
            nodes.push_back(extra);
            nodeTypes.push_back("rtmp_des");
          }

          if (nodes.size() > 1) {
            size_t attachIndex = nodes.size() - 2 - optExtraNodes.size();
            std::shared_ptr<cvedix_nodes::cvedix_node> attachTarget = nullptr;
            if (attachIndex < nodes.size()) {
              attachTarget = nodes[attachIndex];
            }
            if (attachTarget) {
              optFaceNode->attach_to({attachTarget});
            }
          }

          // Add face OSD right after optional face detector so face boxes/keypoints
          // are rendered on stream output (especially for BA solutions with their
          // own OSD node already present).
          SolutionConfig::NodeConfig optFaceOsdCfg;
          optFaceOsdCfg.nodeType = "face_osd_v2";
          optFaceOsdCfg.nodeName = "optional_face_osd_{instanceId}";
          std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>>
              optFaceOsdExtraNodes;
          auto optFaceOsdNode = createNode(optFaceOsdCfg, mutableReq, instanceId,
                                           existingRTMPStreamKeys,
                                           &optFaceOsdExtraNodes);
          if (optFaceOsdNode) {
            optFaceOsdNode->attach_to({optFaceNode});
            nodes.push_back(optFaceOsdNode);
            nodeTypes.push_back(optFaceOsdCfg.nodeType);
            std::cerr << "[PipelineBuilder] Injected optional_face_osd node "
                         "(face overlay enabled)"
                      << std::endl;
          } else {
            std::cerr << "[PipelineBuilder] ⚠ Failed to inject optional "
                         "face OSD node; face detector still enabled"
                      << std::endl;
          }

          optionalFaceDetectorInjected = true;
          std::cerr << "[PipelineBuilder] Injected optional_face_detector node "
                       "(ENABLE_FACE_DETECTION=true)"
                    << std::endl;
        } else {
          std::cerr << "[PipelineBuilder] ⚠ Failed to inject optional "
                       "face detector node"
                    << std::endl;
        }
      }

      // Input-type routing: file_des only when input is a local file path.
      // Other input types (RTMP, RTSP, HLS, HTTP, UDP) use the matching destination node only.
      if (nodeConfig.nodeType == "file_des" && effectiveInputType != "file") {
        std::cerr << "[PipelineBuilder] Skipping file_des (input is " << effectiveInputType
                  << "; use " << effectiveInputType << "_des only. file_des only for file path input.)"
                  << std::endl;
        continue;
      }

      // Use mutableReq for SecuRT integration (has areas/lines loaded)
      std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> extraNodes;
      auto node = createNode(modifiedNodeConfig, mutableReq, instanceId, existingRTMPStreamKeys, &extraNodes);
      if (node) {
        nodes.push_back(node);
        for (const auto &extra : extraNodes) {
          nodes.push_back(extra);
        }
        if (!extraNodes.empty()) {
          nodeTypes.push_back("rtmp_lastframe_proxy");
          for (size_t i = 0; i < extraNodes.size(); ++i) {
            nodeTypes.push_back("rtmp_des");
          }
        } else {
          nodeTypes.push_back(nodeConfig.nodeType);
        }

        // Connect to previous node(s)
        // For nodes that should attach to multiple sources (detector, tracker), attach to all source nodes
        if (hasMultipleSources && !multipleSourceNodes.empty()) {
          // Check if this node should attach to all sources (detector, tracker, etc.)
          if (nodeConfig.nodeType == "yolo_detector" || 
              nodeConfig.nodeType == "trt_vehicle_detector" ||
              nodeConfig.nodeType == "sort_track" ||
              nodeConfig.nodeType == "sort_tracker") {
            // Attach to all source nodes
            node->attach_to(multipleSourceNodes);
            std::cerr << "[PipelineBuilder] Attached " << nodeConfig.nodeType 
                      << " to " << multipleSourceNodes.size() << " " << multipleSourceType << " nodes" << std::endl;
            continue; // Skip normal connection logic
          }
        }
        
        // Normal connection logic
        // attach_to() expects a vector of shared_ptr, not a raw pointer
        if (nodes.size() > 1) {
          // Check if current node is a destination node
          bool isDestNode = (nodeConfig.nodeType == "file_des" ||
                             nodeConfig.nodeType == "rtmp_des" ||
                             nodeConfig.nodeType == "rtsp_des" ||
                             nodeConfig.nodeType == "screen_des");

          // Special handling for BA OSD nodes: attach to their respective BA
          // node This is critical - OSD node must attach to BA node
          // (crossline/jam/stop) to get zone/line metadata for drawing
          std::shared_ptr<cvedix_nodes::cvedix_node> attachTarget = nullptr;
          if (nodeConfig.nodeType == "ba_crossline_osd" ||
              nodeConfig.nodeType == "ba_jam_osd" ||
              nodeConfig.nodeType == "ba_stop_osd" ||
              nodeConfig.nodeType == "ba_loitering_osd" ||
              nodeConfig.nodeType == "ba_area_enter_exit_osd") {
            // Determine which BA node type to look for
            std::string targetBAType = "";
            if (nodeConfig.nodeType == "ba_crossline_osd")
              targetBAType = "ba_crossline";
            else if (nodeConfig.nodeType == "ba_jam_osd")
              targetBAType = "ba_jam";
            else if (nodeConfig.nodeType == "ba_stop_osd")
              targetBAType = "ba_stop";
            else if (nodeConfig.nodeType == "ba_loitering_osd")
              targetBAType = "ba_loitering";
            else if (nodeConfig.nodeType == "ba_area_enter_exit_osd")
              targetBAType = "ba_area_enter_exit";

            // Find corresponding BA node to attach to
            for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
              if (nodeTypes[i] == targetBAType) {
                attachTarget = nodes[i];
                std::cerr << "[PipelineBuilder] Attaching "
                          << nodeConfig.nodeType << " node to " << targetBAType
                          << " node "
                          << "(required to get zones/lines for drawing)"
                          << std::endl;
                break;
              }
            }
            if (!attachTarget) {
              std::cerr << "[PipelineBuilder] ⚠ WARNING: "
                        << nodeConfig.nodeType << " node created but "
                        << targetBAType << " node not found! "
                        << "OSD will attach to previous node but may not "
                           "receive metadata."
                        << std::endl;
            }
          }

          // Special handling for file_des nodes: if pipeline has multiple OSD nodes
          // (ba_crossline_osd and ba_loitering_osd), attach to the appropriate OSD node
          // This ensures each pipeline branch (ba_crossline and ba_loitering) has its own file_des
          if (!attachTarget && nodeConfig.nodeType == "file_des") {
            // Check if we have both ba_crossline_osd and ba_loitering_osd
            bool hasBACrosslineOSD = hasNodeType("ba_crossline_osd");
            bool hasBALoiteringOSD = hasNodeType("ba_loitering_osd");
            
            if (hasBACrosslineOSD && hasBALoiteringOSD) {
              // We have both OSD nodes - need to determine which one to attach to
              // Strategy: If this is the first file_des, attach to ba_crossline_osd
              // If there's already a file_des, attach to ba_loitering_osd
              bool hasExistingFileDes = false;
              for (size_t i = 0; i < nodeTypes.size(); ++i) {
                if (nodeTypes[i] == "file_des") {
                  hasExistingFileDes = true;
                  break;
                }
              }
              
              if (hasExistingFileDes) {
                // Second file_des - attach to ba_loitering_osd
                for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
                  if (nodeTypes[i] == "ba_loitering_osd") {
                    attachTarget = nodes[i];
                    std::cerr << "[PipelineBuilder] Attaching file_des node to ba_loitering_osd "
                                 "node (separate pipeline branch for loitering detection)"
                              << std::endl;
                    break;
                  }
                }
              } else {
                // First file_des - attach to ba_crossline_osd
                for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
                  if (nodeTypes[i] == "ba_crossline_osd") {
                    attachTarget = nodes[i];
                    std::cerr << "[PipelineBuilder] Attaching file_des node to ba_crossline_osd "
                                 "node (separate pipeline branch for crossline detection)"
                              << std::endl;
                    break;
                  }
                }
              }
            } else if (hasBACrosslineOSD) {
              // Only ba_crossline_osd - attach to it
              for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
                if (nodeTypes[i] == "ba_crossline_osd") {
                  attachTarget = nodes[i];
                  std::cerr << "[PipelineBuilder] Attaching file_des node to ba_crossline_osd "
                               "node"
                            << std::endl;
                  break;
                }
              }
            } else if (hasBALoiteringOSD) {
              // Only ba_loitering_osd - attach to it
              for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
                if (nodeTypes[i] == "ba_loitering_osd") {
                  attachTarget = nodes[i];
                  std::cerr << "[PipelineBuilder] Attaching file_des node to ba_loitering_osd "
                               "node"
                            << std::endl;
                  break;
                }
              }
            }
          }

          // For RTMP nodes: if pipeline has OSD node, attach to OSD node (to
          // get frames with overlay)
          if (!attachTarget && nodeConfig.nodeType == "rtmp_des" &&
              hasOSDNode) {
            // Find OSD node
            for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
              auto checkNode = nodes[i];
              bool isOSDNode =
                  std::dynamic_pointer_cast<
                      cvedix_nodes::cvedix_face_osd_node_v2>(checkNode) ||
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_osd_node_v3>(
                      checkNode) ||
                  std::dynamic_pointer_cast<
                      cvedix_nodes::cvedix_ba_line_crossline_osd_node>(checkNode) ||
                  std::dynamic_pointer_cast<
                      cvedix_nodes::cvedix_ba_area_jam_osd_node>(checkNode) ||
                  std::dynamic_pointer_cast<
                      cvedix_nodes::cvedix_ba_stop_osd_node>(checkNode) ||
                  std::dynamic_pointer_cast<
                      cvedix_nodes::cvedix_ba_stop_osd_node>(checkNode) || // ba_loitering_osd uses ba_stop_osd_node
                  std::dynamic_pointer_cast<
                      cvedix_nodes::cvedix_ba_area_enter_exit_osd_node>(checkNode);
              if (isOSDNode) {
                attachTarget = checkNode;
                std::cerr << "[PipelineBuilder] Attaching RTMP node to OSD "
                             "node for overlay frames"
                          << std::endl;
                break;
              }
            }
          }

          // If no OSD node found or not RTMP node, use default logic
          if (!attachTarget) {
            // Find the node to attach to
            // When rtmp_des returns extraNodes (proxy + rtmp node), "node" is the proxy;
            // we must attach it to the node *before* the proxy, not to the last pushed node.
            size_t attachIndex = nodes.size() - 2;
            if (isDestNode && !extraNodes.empty()) {
              attachIndex = nodes.size() - 2 - extraNodes.size();
            }
            if (isDestNode && attachIndex > 0) {
              // Check if previous node is also a destination node
              bool prevIsDestNode = (nodeTypes[attachIndex] == "file_des" ||
                                     nodeTypes[attachIndex] == "rtmp_des" || nodeTypes[attachIndex] == "frame_router_sink" ||
                                     nodeTypes[attachIndex] == "rtsp_des" ||
                                     nodeTypes[attachIndex] == "screen_des");
              if (prevIsDestNode) {
                // Find the last non-destination node
                for (int i = static_cast<int>(attachIndex) - 1; i >= 0; --i) {
                  bool nodeIsDest = (nodeTypes[i] == "file_des" ||
                                     nodeTypes[i] == "rtmp_des" || nodeTypes[i] == "frame_router_sink" ||
                                     nodeTypes[i] == "rtsp_des" ||
                                     nodeTypes[i] == "screen_des");
                  if (!nodeIsDest) {
                    attachIndex = i;
                    break;
                  }
                }
              }
            }

            if (attachIndex < nodes.size()) {
              attachTarget = nodes[attachIndex];
            } else {
              attachTarget = nodes[nodes.size() - 2];
            }
          }

          if (attachTarget) {
            node->attach_to({attachTarget});
          }
        }
        std::cerr
            << "[PipelineBuilder] Successfully created and connected node: "
            << nodeConfig.nodeType << std::endl;
      } else {
        // Check if this is an optional node that can be skipped
        bool isOptionalNode =
            (nodeConfig.nodeType == "screen_des" ||
             nodeConfig.nodeType == "rtmp_des" ||
             nodeConfig.nodeType == "rtsp_des" ||
             nodeConfig.nodeType == "json_console_broker" ||
             nodeConfig.nodeType == "json_enhanced_console_broker" ||
             nodeConfig.nodeType == "json_mqtt_broker" ||
             nodeConfig.nodeType == "json_crossline_mqtt_broker" ||
             nodeConfig.nodeType == "json_jam_mqtt_broker" ||
             nodeConfig.nodeType == "json_stop_mqtt_broker" ||
             nodeConfig.nodeType == "json_kafka_broker" ||
             nodeConfig.nodeType == "xml_file_broker" ||
             nodeConfig.nodeType == "xml_socket_broker" ||
             nodeConfig.nodeType == "msg_broker" ||
             nodeConfig.nodeType == "ba_socket_broker" ||
             nodeConfig.nodeType == "embeddings_socket_broker" ||
             nodeConfig.nodeType == "embeddings_properties_socket_broker" ||
             nodeConfig.nodeType == "plate_socket_broker" ||
             nodeConfig.nodeType == "expr_socket_broker");

        if (isOptionalNode) {
          std::cerr << "[PipelineBuilder] Skipping optional node: "
                    << nodeConfig.nodeType
                    << " (node creation returned nullptr)" << std::endl;
          // Continue to next node without throwing exception
        } else {
          std::cerr << "[PipelineBuilder] Failed to create node: "
                    << nodeConfig.nodeType << " (createNode returned nullptr)"
                    << std::endl;
          throw std::runtime_error("Failed to create node: " +
                                   nodeConfig.nodeType);
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "[PipelineBuilder] Exception creating node "
                << nodeConfig.nodeType << ": " << e.what() << std::endl;
      throw; // Re-throw to be caught by caller
    } catch (...) {
      std::cerr << "[PipelineBuilder] Unknown exception creating node: "
                << nodeConfig.nodeType << std::endl;
      throw std::runtime_error("Unknown error creating node: " +
                               nodeConfig.nodeType);
    }
  }

  // Auto-add file_des node if RECORD_PATH is set in additionalParams
  auto recordPathIt = req.additionalParams.find("RECORD_PATH");
  if (recordPathIt != req.additionalParams.end() &&
      !recordPathIt->second.empty()) {
    std::string recordPath = recordPathIt->second;
    std::cerr << "[PipelineBuilder] RECORD_PATH detected: " << recordPath
              << std::endl;
    std::cerr << "[PipelineBuilder] Auto-adding file_des node for recording..."
              << std::endl;

    try {
      // Create file_des node config
      SolutionConfig::NodeConfig fileDesConfig;
      fileDesConfig.nodeType = "file_des";
      fileDesConfig.nodeName = "file_des_record_{instanceId}";
      fileDesConfig.parameters["save_dir"] = recordPath;
      fileDesConfig.parameters["name_prefix"] = "record";
      fileDesConfig.parameters["max_duration"] = "10"; // 10 minutes per file
      fileDesConfig.parameters["osd"] = "true";        // Include OSD overlay

      // Substitute {instanceId} in node name
      std::string fileDesNodeName = fileDesConfig.nodeName;
      size_t pos = fileDesNodeName.find("{instanceId}");
      while (pos != std::string::npos) {
        fileDesNodeName.replace(pos, 12, instanceId);
        pos = fileDesNodeName.find("{instanceId}", pos + instanceId.length());
      }

      // Create file_des node
      auto fileDesNode = PipelineBuilderDestinationNodes::createFileDestinationNode(
          fileDesNodeName, fileDesConfig.parameters, instanceId);

      if (fileDesNode && !nodes.empty()) {
        // Find the last non-destination node to attach to
        // Destination nodes (file_des, rtmp_des, rtsp_des, screen_des) don't forward
        // frames, so we need to attach to the source node (usually OSD node)
        std::shared_ptr<cvedix_nodes::cvedix_node> attachTarget = nullptr;
        for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
          auto node = nodes[i];
          // Check if this is a destination node
          bool isDestNode =
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_des_node>(
                  node) ||
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(
                  node) ||
#ifdef CVEDIX_WITH_GSTREAMER
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_des_node>(
                  node) ||
#endif
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_screen_des_node>(
                  node);
          if (!isDestNode) {
            attachTarget = node;
            break;
          }
        }

        // Fallback to last node if no non-destination node found
        if (!attachTarget) {
          attachTarget = nodes.back();
        }

        // Connect file_des node to the target node
        fileDesNode->attach_to({attachTarget});
        nodes.push_back(fileDesNode);
        std::cerr
            << "[PipelineBuilder] ✓ Auto-added file_des node for recording to: "
            << recordPath << std::endl;
      } else {
        std::cerr << "[PipelineBuilder] ⚠ Failed to create file_des node for "
                     "recording"
                  << std::endl;
      }
    } catch (const std::exception &e) {
      std::cerr << "[PipelineBuilder] ⚠ Exception auto-adding file_des node: "
                << e.what() << std::endl;
      std::cerr << "[PipelineBuilder] ⚠ Recording will not be available, but "
                   "pipeline will continue"
                << std::endl;
    }
  }

  // Auto-add ba_stop node if StopZones are present (for SecuRT areas)
  // Check if StopZones exist and ba_stop node is not already in pipeline
  auto stopZonesIt = mutableReq.additionalParams.find("StopZones");
  bool hasStopZones = (stopZonesIt != mutableReq.additionalParams.end() &&
                       !stopZonesIt->second.empty());
  bool hasBAStopNode = false;
  for (const auto &type : nodeTypes) {
    if (type == "ba_stop") {
      hasBAStopNode = true;
      break;
    }
  }

  if (hasStopZones && !hasBAStopNode && solution.solutionId == "securt") {
    std::cerr << "[PipelineBuilder] StopZones detected for SecuRT areas - "
                 "auto-adding ba_stop node..."
              << std::endl;

    try {
      // Create ba_stop node config
      SolutionConfig::NodeConfig baStopConfig;
      baStopConfig.nodeType = "ba_stop";
      baStopConfig.nodeName = "ba_stop_{instanceId}";
      baStopConfig.parameters.clear(); // Parameters will come from StopZones in additionalParams

      // Substitute {instanceId} in node name
      std::string baStopNodeName = baStopConfig.nodeName;
      size_t pos = baStopNodeName.find("{instanceId}");
      while (pos != std::string::npos) {
        baStopNodeName.replace(pos, 12, instanceId);
        pos = baStopNodeName.find("{instanceId}", pos + instanceId.length());
      }

      // Create ba_stop node (it will read StopZones from mutableReq.additionalParams)
      auto baStopNode = PipelineBuilderBehaviorAnalysisNodes::createBAStopNode(
          baStopNodeName, baStopConfig.parameters, mutableReq);

      if (baStopNode && !nodes.empty()) {
        // Find sort_track node to attach to (ba_stop needs tracked objects)
        std::shared_ptr<cvedix_nodes::cvedix_node> attachTarget = nullptr;
        for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
          if (nodeTypes[i] == "sort_track" || nodeTypes[i] == "sort_tracker") {
            attachTarget = nodes[i];
            break;
          }
        }

        // Fallback to last non-destination node
        if (!attachTarget) {
          for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
            bool isDestNode = (nodeTypes[i] == "file_des" ||
                               nodeTypes[i] == "rtmp_des" || nodeTypes[i] == "frame_router_sink" ||
                               nodeTypes[i] == "rtsp_des" ||
                               nodeTypes[i] == "screen_des" ||
                               nodeTypes[i] == "app_des");
            if (!isDestNode) {
              attachTarget = nodes[i];
              break;
            }
          }
          // Final fallback to last node if no non-destination node found
          if (!attachTarget && !nodes.empty()) {
            attachTarget = nodes.back();
          }
        }

        if (attachTarget) {
          baStopNode->attach_to({attachTarget});
          nodes.push_back(baStopNode);
          nodeTypes.push_back("ba_stop");
          std::cerr << "[PipelineBuilder] ✓ Auto-added ba_stop node for area "
                       "detection"
                    << std::endl;

          // Also auto-add ba_stop_osd node for visualization
          std::cerr << "[PipelineBuilder] Auto-adding ba_stop_osd node for area "
                       "visualization..."
                    << std::endl;

          SolutionConfig::NodeConfig baStopOSDConfig;
          baStopOSDConfig.nodeType = "ba_stop_osd";
          baStopOSDConfig.nodeName = "ba_stop_osd_{instanceId}";
          baStopOSDConfig.parameters.clear();

          std::string baStopOSDNodeName = baStopOSDConfig.nodeName;
          pos = baStopOSDNodeName.find("{instanceId}");
          while (pos != std::string::npos) {
            baStopOSDNodeName.replace(pos, 12, instanceId);
            pos = baStopOSDNodeName.find("{instanceId}", pos + instanceId.length());
          }

          auto baStopOSDNode =
              PipelineBuilderBehaviorAnalysisNodes::createBAStopOSDNode(
                  baStopOSDNodeName, baStopOSDConfig.parameters);

          if (baStopOSDNode) {
            // Attach OSD node to ba_stop node
            baStopOSDNode->attach_to({baStopNode});
            nodes.push_back(baStopOSDNode);
            nodeTypes.push_back("ba_stop_osd");
            hasOSDNode = true; // Update hasOSDNode flag
            std::cerr << "[PipelineBuilder] ✓ Auto-added ba_stop_osd node for "
                         "area visualization"
                      << std::endl;
          } else {
            std::cerr << "[PipelineBuilder] ⚠ Failed to create ba_stop_osd node"
                      << std::endl;
          }
        } else {
          std::cerr << "[PipelineBuilder] ⚠ Failed to find node to attach ba_stop "
                       "node to"
                    << std::endl;
        }
      } else {
        std::cerr << "[PipelineBuilder] ⚠ Failed to create ba_stop node"
                  << std::endl;
      }
    } catch (const std::exception &e) {
      std::cerr << "[PipelineBuilder] ⚠ Exception auto-adding ba_stop node: "
                << e.what() << std::endl;
      std::cerr << "[PipelineBuilder] ⚠ Area detection will not be available, "
                   "but pipeline will continue"
                << std::endl;
    }
  }

  // Auto-add ba_loitering node if LoiteringAreas are present (for SecuRT loitering areas)
  // Check if LoiteringAreas exist and ba_loitering node is not already in pipeline
  auto loiteringAreasIt = mutableReq.additionalParams.find("LoiteringAreas");
  bool hasLoiteringAreas = (loiteringAreasIt != mutableReq.additionalParams.end() &&
                            !loiteringAreasIt->second.empty());
  bool hasBALoiteringNode = false;
  for (const auto &type : nodeTypes) {
    if (type == "ba_loitering") {
      hasBALoiteringNode = true;
      break;
    }
  }

  if (hasLoiteringAreas && !hasBALoiteringNode && solution.solutionId == "securt") {
    std::cerr << "[PipelineBuilder] LoiteringAreas detected for SecuRT areas - "
                 "auto-adding ba_loitering node..."
              << std::endl;

    try {
      // Create ba_loitering node config
      SolutionConfig::NodeConfig baLoiteringConfig;
      baLoiteringConfig.nodeType = "ba_loitering";
      baLoiteringConfig.nodeName = "ba_loitering_{instanceId}";
      baLoiteringConfig.parameters.clear(); // Parameters will come from LoiteringAreas in additionalParams

      // Substitute {instanceId} in node name
      std::string baLoiteringNodeName = baLoiteringConfig.nodeName;
      size_t pos = baLoiteringNodeName.find("{instanceId}");
      while (pos != std::string::npos) {
        baLoiteringNodeName.replace(pos, 12, instanceId);
        pos = baLoiteringNodeName.find("{instanceId}", pos + instanceId.length());
      }

      // Create ba_loitering node (it will read LoiteringAreas from mutableReq.additionalParams)
      auto baLoiteringNode = PipelineBuilderBehaviorAnalysisNodes::createBALoiteringNode(
          baLoiteringNodeName, baLoiteringConfig.parameters, mutableReq);

      if (baLoiteringNode && !nodes.empty()) {
        // Find sort_track node to attach to (ba_loitering needs tracked objects)
        std::shared_ptr<cvedix_nodes::cvedix_node> attachTarget = nullptr;
        for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
          if (nodeTypes[i] == "sort_track" || nodeTypes[i] == "sort_tracker") {
            attachTarget = nodes[i];
            break;
          }
        }

        // Fallback to last non-destination node
        if (!attachTarget) {
          for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
            bool isDestNode = (nodeTypes[i] == "file_des" ||
                               nodeTypes[i] == "rtmp_des" || nodeTypes[i] == "frame_router_sink" ||
                               nodeTypes[i] == "rtsp_des" ||
                               nodeTypes[i] == "screen_des" ||
                               nodeTypes[i] == "app_des");
            if (!isDestNode) {
              attachTarget = nodes[i];
              break;
            }
          }
          // Final fallback to last node if no non-destination node found
          if (!attachTarget && !nodes.empty()) {
            attachTarget = nodes.back();
          }
        }

        if (attachTarget) {
          baLoiteringNode->attach_to({attachTarget});
          nodes.push_back(baLoiteringNode);
          nodeTypes.push_back("ba_loitering");
          std::cerr << "[PipelineBuilder] ✓ Auto-added ba_loitering node for loitering "
                       "detection"
                    << std::endl;

          // Also auto-add ba_loitering_osd node for visualization
          std::cerr << "[PipelineBuilder] Auto-adding ba_loitering_osd node for loitering "
                       "visualization..."
                    << std::endl;

          SolutionConfig::NodeConfig baLoiteringOSDConfig;
          baLoiteringOSDConfig.nodeType = "ba_loitering_osd";
          baLoiteringOSDConfig.nodeName = "ba_loitering_osd_{instanceId}";
          baLoiteringOSDConfig.parameters.clear();

          std::string baLoiteringOSDNodeName = baLoiteringOSDConfig.nodeName;
          pos = baLoiteringOSDNodeName.find("{instanceId}");
          while (pos != std::string::npos) {
            baLoiteringOSDNodeName.replace(pos, 12, instanceId);
            pos = baLoiteringOSDNodeName.find("{instanceId}", pos + instanceId.length());
          }

          auto baLoiteringOSDNode =
              PipelineBuilderBehaviorAnalysisNodes::createBALoiteringOSDNode(
                  baLoiteringOSDNodeName, baLoiteringOSDConfig.parameters);

          if (baLoiteringOSDNode) {
            // Attach OSD node to ba_loitering node
            baLoiteringOSDNode->attach_to({baLoiteringNode});
            nodes.push_back(baLoiteringOSDNode);
            nodeTypes.push_back("ba_loitering_osd");
            hasOSDNode = true; // Update hasOSDNode flag
            std::cerr << "[PipelineBuilder] ✓ Auto-added ba_loitering_osd node for "
                         "loitering visualization"
                      << std::endl;

            // CRITICAL: If we have both ba_crossline_osd and ba_loitering_osd,
            // auto-add a separate file_des for ba_loitering_osd
            // This ensures each pipeline branch (ba_crossline and ba_loitering) 
            // has its own file_des, avoiding bottleneck
            bool hasBACrosslineOSD = false;
            for (const auto &type : nodeTypes) {
              if (type == "ba_crossline_osd") {
                hasBACrosslineOSD = true;
                break;
              }
            }
            if (hasBACrosslineOSD) {
              std::cerr << "[PipelineBuilder] Detected both ba_crossline_osd and ba_loitering_osd - "
                           "auto-adding separate file_des for ba_loitering_osd to avoid bottleneck"
                        << std::endl;
              
              try {
                // Find existing file_des to get its configuration
                std::string baseSaveDir = "./output/" + instanceId;
                std::string namePrefix = "securt";
                int maxDuration = 2; // minutes
                int bitrate = 1024;
                bool osd = true;
                
                // Look for existing file_des to inherit config
                for (size_t i = 0; i < nodes.size(); ++i) {
                  if (nodeTypes[i] == "file_des") {
                    // Try to get config from existing file_des if possible
                    // For now, use defaults with separate directory
                    break;
                  }
                }
                
                // Create separate file_des for loitering branch
                SolutionConfig::NodeConfig loiteringFileDesConfig;
                loiteringFileDesConfig.nodeType = "file_des";
                loiteringFileDesConfig.nodeName = "file_des_loitering_{instanceId}";
                loiteringFileDesConfig.parameters["save_dir"] = "./output/" + instanceId + "/loitering";
                loiteringFileDesConfig.parameters["name_prefix"] = "loitering";
                loiteringFileDesConfig.parameters["max_duration"] = std::to_string(maxDuration);
                loiteringFileDesConfig.parameters["bitrate"] = std::to_string(bitrate);
                loiteringFileDesConfig.parameters["osd"] = osd ? "true" : "false";
                
                std::string loiteringFileDesNodeName = loiteringFileDesConfig.nodeName;
                pos = loiteringFileDesNodeName.find("{instanceId}");
                while (pos != std::string::npos) {
                  loiteringFileDesNodeName.replace(pos, 12, instanceId);
                  pos = loiteringFileDesNodeName.find("{instanceId}", pos + instanceId.length());
                }
                
                auto loiteringFileDesNode = PipelineBuilderDestinationNodes::createFileDestinationNode(
                    loiteringFileDesNodeName, loiteringFileDesConfig.parameters, instanceId);
                
                if (loiteringFileDesNode) {
                  // Attach to ba_loitering_osd node
                  loiteringFileDesNode->attach_to({baLoiteringOSDNode});
                  nodes.push_back(loiteringFileDesNode);
                  nodeTypes.push_back("file_des");
                  std::cerr << "[PipelineBuilder] ✓ Auto-added separate file_des for ba_loitering_osd "
                               "(save_dir: ./output/" << instanceId << "/loitering)"
                            << std::endl;
                  std::cerr << "[PipelineBuilder] NOTE: ba_crossline and ba_loitering now have "
                               "separate file_des nodes - each pipeline branch is independent"
                            << std::endl;
                } else {
                  std::cerr << "[PipelineBuilder] ⚠ Failed to create separate file_des for ba_loitering_osd"
                            << std::endl;
                }
              } catch (const std::exception &e) {
                std::cerr << "[PipelineBuilder] ⚠ Exception auto-adding file_des for ba_loitering_osd: "
                          << e.what() << std::endl;
                std::cerr << "[PipelineBuilder] ⚠ Pipeline will continue, but ba_loitering may share "
                             "file_des with ba_crossline (potential bottleneck)"
                          << std::endl;
              }
            }
          } else {
            std::cerr << "[PipelineBuilder] ⚠ Failed to create ba_loitering_osd node"
                      << std::endl;
          }
        } else {
          std::cerr << "[PipelineBuilder] ⚠ Failed to find node to attach ba_loitering "
                       "node to"
                    << std::endl;
        }
      } else {
        std::cerr << "[PipelineBuilder] ⚠ Failed to create ba_loitering node"
                  << std::endl;
      }
    } catch (const std::exception &e) {
      std::cerr << "[PipelineBuilder] ⚠ Exception auto-adding ba_loitering node: "
                << e.what() << std::endl;
      std::cerr << "[PipelineBuilder] ⚠ Loitering detection will not be available, "
                   "but pipeline will continue"
                << std::endl;
    }
  }

  // Check if pipeline has app_des_node for frame capture
  bool hasAppDesNode = false;
  for (const auto &node : nodes) {
    if (std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_des_node>(node)) {
      hasAppDesNode = true;
      break;
    }
  }

  // Automatically add app_des_node if not present (for frame capture support)
  if (!hasAppDesNode && !nodes.empty()) {
    std::cerr << "[PipelineBuilder] No app_des_node found in pipeline"
              << std::endl;
    std::cerr << "[PipelineBuilder] Automatically adding app_des_node for "
                 "frame capture support..."
              << std::endl;

    try {
      std::string appDesNodeName = "app_des_" + instanceId;
      std::map<std::string, std::string> appDesParams;
      appDesParams["channel"] = "0";

      auto appDesNode = PipelineBuilderDestinationNodes::createAppDesNode(appDesNodeName, appDesParams);
      if (appDesNode) {
        // CRITICAL FIX: Find OSD node FIRST to ensure we get processed frames
        // Priority 1: Find OSD node (guarantees processed frames with overlays)
        // Priority 2: If no OSD node, find last non-DES node as fallback
        std::shared_ptr<cvedix_nodes::cvedix_node> attachTarget = nullptr;
        std::shared_ptr<cvedix_nodes::cvedix_node> fallbackTarget = nullptr;

        // First pass: Find OSD node (highest priority for processed frames)
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
          auto node = *it;
          if (!node)
            continue;

          // Check if it's a DES node (skip DES nodes)
          bool isDesNode =
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(
                  node) != nullptr ||
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_des_node>(
                  node) != nullptr ||
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_des_node>(
                  node) != nullptr;

          if (isDesNode) {
            continue; // Skip DES nodes
          }

          // Check if this is an OSD node
          bool isOSDNode =
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_face_osd_node_v2>(
                  node) != nullptr ||
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_osd_node_v3>(
                  node) != nullptr ||
              std::dynamic_pointer_cast<
                  cvedix_nodes::cvedix_ba_line_crossline_osd_node>(node) != nullptr ||
              std::dynamic_pointer_cast<
                  cvedix_nodes::cvedix_ba_area_jam_osd_node>(node) != nullptr ||
              std::dynamic_pointer_cast<
                  cvedix_nodes::cvedix_ba_stop_osd_node>(node) != nullptr || // ba_loitering_osd uses ba_stop_osd_node
              std::dynamic_pointer_cast<
                  cvedix_nodes::cvedix_ba_area_enter_exit_osd_node>(node) != nullptr;

          if (isOSDNode) {
            attachTarget = node;
            std::cerr
                << "[PipelineBuilder] ✓ Found OSD node to attach app_des_node: "
                << typeid(*node).name() << std::endl;
            std::cerr << "[PipelineBuilder] ✓ app_des_node will receive "
                         "PROCESSED frames with overlays"
                      << std::endl;
            break; // Found OSD node, use it
          }

          // Store last non-DES node as fallback (if not found yet)
          if (!fallbackTarget) {
            fallbackTarget = node;
          }
        }

        // If no OSD node found, use fallback (last non-DES node)
        if (!attachTarget && fallbackTarget) {
          attachTarget = fallbackTarget;
          std::cerr << "[PipelineBuilder] ⚠ No OSD node found, using fallback: "
                    << typeid(*fallbackTarget).name() << std::endl;
          std::cerr << "[PipelineBuilder] ⚠ WARNING: app_des_node attached to "
                       "non-OSD node. "
                    << "Frame may not be processed (no overlays)." << std::endl;
        }

        if (attachTarget) {
          // Attach app_des_node to the same node as the last DES node
          bool isOSDTarget =
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_face_osd_node_v2>(
                  attachTarget) != nullptr ||
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_osd_node_v3>(
                  attachTarget) != nullptr ||
              std::dynamic_pointer_cast<
                  cvedix_nodes::cvedix_ba_line_crossline_osd_node>(attachTarget) !=
                  nullptr;

          appDesNode->attach_to({attachTarget});
          nodes.push_back(appDesNode);
          std::cerr << "[PipelineBuilder] ✓ app_des_node added successfully "
                       "for frame capture"
                    << std::endl;
          std::cerr
              << "[PipelineBuilder] DEBUG: app_des_node attached to: "
              << typeid(*attachTarget).name()
              << (isOSDTarget
                      ? " [OSD NODE - will receive processed frames]"
                      : " [NON-OSD NODE - will receive unprocessed frames]")
              << std::endl;
          std::cerr << "[PipelineBuilder] NOTE: app_des_node attached to same "
                       "source as other DES nodes (parallel connection)"
                    << std::endl;

          if (!isOSDTarget) {
            std::cerr << "[PipelineBuilder] ⚠ CRITICAL WARNING: app_des_node "
                         "is NOT attached to OSD node! "
                      << "This means getLastFrame API will return unprocessed "
                         "frames. "
                      << "Please check pipeline configuration to ensure OSD "
                         "node exists and is properly connected."
                      << std::endl;
          }
        } else {
          std::cerr << "[PipelineBuilder] ⚠ Warning: Could not find suitable "
                       "node to attach app_des_node"
                    << std::endl;
          std::cerr << "[PipelineBuilder] Frame capture will not be available "
                       "for this instance"
                    << std::endl;
        }
      } else {
        std::cerr << "[PipelineBuilder] ⚠ Warning: Failed to create "
                     "app_des_node (frame capture will not be available)"
                  << std::endl;
      }
    } catch (const std::exception &e) {
      std::cerr
          << "[PipelineBuilder] ⚠ Warning: Exception adding app_des_node: "
          << e.what() << std::endl;
      std::cerr << "[PipelineBuilder] Frame capture will not be available for "
                   "this instance"
                << std::endl;
    } catch (...) {
      std::cerr << "[PipelineBuilder] ⚠ Warning: Unknown exception adding "
                   "app_des_node"
                << std::endl;
      std::cerr << "[PipelineBuilder] Frame capture will not be available for "
                   "this instance"
                << std::endl;
    }
  } else if (hasAppDesNode) {
    std::cerr << "[PipelineBuilder] ✓ app_des_node found in pipeline (frame "
                 "capture supported)"
              << std::endl;
  }

  // ========== Auto-inject optional output nodes based on parameters ==========
  // This allows any instance to freely choose input/output without needing to
  // define in solution

  // Helper function to find the appropriate node for attaching broker/output
  // nodes For ba_crossline: attach broker to ba_crossline node Otherwise: find
  // last non-DES node (excluding app_des)
  auto findAttachTargetForBrokerCrossline = [&nodes,
                                             &nodeTypes](bool isCrosslineBroker)
      -> std::shared_ptr<cvedix_nodes::cvedix_node> {
    if (isCrosslineBroker) {
      // For crossline broker, find ba_crossline node specifically
      for (size_t i = 0; i < nodeTypes.size(); ++i) {
        if (nodeTypes[i] == "ba_crossline") {
          return nodes[i];
        }
      }
    }

    // For other brokers, find last non-DES node (excluding app_des)
    for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
      bool isDestNode =
          (nodeTypes[i] == "file_des" || nodeTypes[i] == "rtmp_des" || nodeTypes[i] == "frame_router_sink" ||
           nodeTypes[i] == "rtsp_des" || nodeTypes[i] == "screen_des" || nodeTypes[i] == "app_des");
      // Also exclude broker nodes (they should be in sequence, not parallel)
      bool isBrokerNode = (nodeTypes[i] == "json_mqtt_broker" ||
                           nodeTypes[i] == "json_crossline_mqtt_broker" ||
                           nodeTypes[i] == "json_console_broker");
      if (!isDestNode && !isBrokerNode) {
        return nodes[i];
      }
    }
    return nodes.empty() ? nullptr : nodes[0];
  };

  // Helper function to find the appropriate node for attaching broker/output
  // nodes For ba_jam: attach broker to ba_jam node Otherwise: find
  // last non-DES node (excluding app_des)
  auto findAttachTargetForBrokerJam =
      [&nodes, &nodeTypes](
          bool isJamBroker) -> std::shared_ptr<cvedix_nodes::cvedix_node> {
    if (isJamBroker) {
      // For jam broker, find ba_jam node specifically
      for (size_t i = 0; i < nodeTypes.size(); ++i) {
        if (nodeTypes[i] == "ba_jam") {
          return nodes[i];
        }
      }
    }

    // For other brokers, find last non-DES node (excluding app_des)
    for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
      bool isDestNode =
          (nodeTypes[i] == "file_des" || nodeTypes[i] == "rtmp_des" || nodeTypes[i] == "frame_router_sink" ||
           nodeTypes[i] == "rtsp_des" || nodeTypes[i] == "screen_des" || nodeTypes[i] == "app_des");
      // Also exclude broker nodes (they should be in sequence, not parallel)
      bool isBrokerNode = (nodeTypes[i] == "json_mqtt_broker" ||
                           nodeTypes[i] == "json_jam_mqtt_broker" ||
                           nodeTypes[i] == "json_console_broker");
      if (!isDestNode && !isBrokerNode) {
        return nodes[i];
      }
    }
    return nodes.empty() ? nullptr : nodes[0];
  };

  // Helper function to find the appropriate node for attaching broker/output
  // nodes For ba_stop: attach broker to ba_stop node Otherwise: find
  // last non-DES node (excluding app_des)
  auto findAttachTargetForBrokerStop =
      [&nodes, &nodeTypes](
          bool isStopBroker) -> std::shared_ptr<cvedix_nodes::cvedix_node> {
    if (isStopBroker) {
      // For stop broker, find ba_stop node specifically
      for (size_t i = 0; i < nodeTypes.size(); ++i) {
        if (nodeTypes[i] == "ba_stop") {
          return nodes[i];
        }
      }
    }

    // For other brokers, find last non-DES node (excluding app_des)
    for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
      bool isDestNode =
          (nodeTypes[i] == "file_des" || nodeTypes[i] == "rtmp_des" || nodeTypes[i] == "frame_router_sink" ||
           nodeTypes[i] == "rtsp_des" || nodeTypes[i] == "screen_des" || nodeTypes[i] == "app_des");
      // Also exclude broker nodes (they should be in sequence, not parallel)
      bool isBrokerNode = (nodeTypes[i] == "json_mqtt_broker" ||
                           nodeTypes[i] == "json_stop_mqtt_broker" ||
                           nodeTypes[i] == "json_console_broker");
      if (!isDestNode && !isBrokerNode) {
        return nodes[i];
      }
    }
    return nodes.empty() ? nullptr : nodes[0];
  };

  // Helper function to find the last non-destination node for attaching DES
  // nodes
  auto findLastNonDestNode =
      [&nodes, &nodeTypes]() -> std::shared_ptr<cvedix_nodes::cvedix_node> {
    for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
      bool isDestNode =
          (nodeTypes[i] == "file_des" || nodeTypes[i] == "rtmp_des" || nodeTypes[i] == "frame_router_sink" ||
           nodeTypes[i] == "rtsp_des" || nodeTypes[i] == "screen_des" || nodeTypes[i] == "app_des");
      if (!isDestNode) {
        return nodes[i];
      }
    }
    return nodes.empty() ? nullptr : nodes[0];
  };
  (void)findLastNonDestNode; // Reserved for DES attachment logic

// 1. Auto-add MQTT broker nodes if MQTT config is provided but not in pipeline
#ifdef CVEDIX_WITH_MQTT
  auto mqttBrokerIt = req.additionalParams.find("MQTT_BROKER_URL");
  if (mqttBrokerIt != req.additionalParams.end() &&
      !mqttBrokerIt->second.empty()) {
    std::string mqttBroker = mqttBrokerIt->second;
    mqttBroker.erase(0, mqttBroker.find_first_not_of(" \t\n\r"));
    mqttBroker.erase(mqttBroker.find_last_not_of(" \t\n\r") + 1);

    if (!mqttBroker.empty()) {
      // Check if we have ba_crossline in pipeline - use crossline MQTT broker
      bool hasBACrossline = hasNodeType("ba_crossline");
      // Check if we have ba_jam in pipeline - use jam MQTT broker
      bool hasBAJam = hasNodeType("ba_jam");
      // Check if we have ba_stop in pipeline - use stop MQTT broker
      bool hasBAStop = hasNodeType("ba_stop");
      bool hasMQTTBroker = hasNodeType("json_mqtt_broker") ||
                           hasNodeType("json_crossline_mqtt_broker") ||
                           hasNodeType("json_jam_mqtt_broker") ||
                           hasNodeType("json_stop_mqtt_broker") ||
                           hasNodeType("json_console_broker");
      (void)hasMQTTBroker; // May be used for conditional broker add

      if (hasBACrossline && !hasNodeType("json_crossline_mqtt_broker")) {
        // Auto-add crossline MQTT broker - attach to ba_crossline node
        std::cerr << "[PipelineBuilder] Auto-adding json_crossline_mqtt_broker "
                     "node (MQTT_BROKER_URL detected)"
                  << std::endl;
        try {
          SolutionConfig::NodeConfig mqttConfig;
          mqttConfig.nodeType = "json_crossline_mqtt_broker";
          mqttConfig.nodeName = "json_crossline_mqtt_broker_{instanceId}";
          mqttConfig.parameters = {};

          std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> mqttExtraNodes;
          std::shared_ptr<cvedix_nodes::cvedix_node> mqttNode =
              createNode(mqttConfig, mutableReq, instanceId, existingRTMPStreamKeys, &mqttExtraNodes);
          if (mqttNode) {
            // Find ba_crossline node to attach to (like in sample code)
            auto attachTarget = findAttachTargetForBrokerCrossline(true);
            // Verify that attachTarget is actually ba_crossline node by
            // checking nodeTypes
            bool isBACrosslineNode = false;
            for (size_t i = 0; i < nodes.size(); ++i) {
              if (nodes[i] == attachTarget && nodeTypes[i] == "ba_crossline") {
                isBACrosslineNode = true;
                break;
              }
            }

            if (attachTarget && isBACrosslineNode) {
              // Attach MQTT broker to ba_crossline node
              mqttNode->attach_to({attachTarget});
              nodes.push_back(mqttNode);
              nodeTypes.push_back("json_crossline_mqtt_broker");
              std::cerr << "[PipelineBuilder] ✓ Auto-added "
                           "json_crossline_mqtt_broker node (attached to "
                           "ba_crossline)"
                        << std::endl;

              // IMPORTANT: OSD node should attach to ba_crossline node, NOT
              // MQTT broker This is critical - OSD node needs lines from
              // ba_crossline node to draw From SDK sample:
              // osd->attach_to({ba_crossline}) Pipeline should be: ba_crossline
              // -> mqtt_broker (parallel) and ba_crossline -> osd OSD node will
              // get lines from ba_crossline node via pipeline metadata MQTT
              // broker runs in parallel and doesn't need OSD connection So we
              // DON'T reconnect OSD to MQTT broker - OSD stays attached to
              // ba_crossline
              std::cerr << "[PipelineBuilder] NOTE: ba_crossline_osd node "
                           "should remain attached to ba_crossline node "
                           "to receive lines for drawing. MQTT broker runs in "
                           "parallel."
                        << std::endl;
            } else {
              std::cerr << "[PipelineBuilder] ⚠ Failed to find ba_crossline "
                           "node to attach MQTT broker"
                        << std::endl;
              std::cerr << "[PipelineBuilder] ⚠ attachTarget: "
                        << (attachTarget ? "found" : "nullptr") << std::endl;
              std::cerr << "[PipelineBuilder] ⚠ isBACrosslineNode: "
                        << (isBACrosslineNode ? "true" : "false") << std::endl;
            }
          } else {
            std::cerr << "[PipelineBuilder] ⚠ Failed to create "
                         "json_crossline_mqtt_broker node (returned nullptr)"
                      << std::endl;
          }
        } catch (const std::exception &e) {
          std::cerr << "[PipelineBuilder] ⚠ Failed to auto-add "
                       "json_crossline_mqtt_broker: "
                    << e.what() << std::endl;
        }
      } else if (hasBAJam && !hasNodeType("json_jam_mqtt_broker")) {
        // Auto-add jam MQTT broker - attach to ba_jam node
        std::cerr << "[PipelineBuilder] Auto-adding json_jam_mqtt_broker "
                     "node (MQTT_BROKER_URL detected)"
                  << std::endl;
        try {
          SolutionConfig::NodeConfig mqttConfig;
          mqttConfig.nodeType = "json_jam_mqtt_broker";
          mqttConfig.nodeName = "json_jam_mqtt_broker_{instanceId}";
          mqttConfig.parameters = {};

          std::string mqttNodeName = mqttConfig.nodeName;
          size_t pos = mqttNodeName.find("{instanceId}");
          while (pos != std::string::npos) {
            mqttNodeName.replace(pos, 12, instanceId);
            pos = mqttNodeName.find("{instanceId}", pos + instanceId.length());
          }

          // TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
          std::cerr << "[PipelineBuilder] json_jam_mqtt_broker is temporarily disabled due to CVEDIX SDK cereal/rapidxml issue"
                    << std::endl;
          // auto mqttNode = PipelineBuilderBrokerNodes::createJSONJamMQTTBrokerNode(
          //     mqttNodeName, mqttConfig.parameters, req, instanceId);
          std::shared_ptr<cvedix_nodes::cvedix_node> mqttNode = nullptr;
          if (mqttNode) {
            // Find ba_jam node to attach to (like in sample code)
            auto attachTarget = findAttachTargetForBrokerJam(true);
            // Verify that attachTarget is actually ba_jam node by
            // checking nodeTypes
            bool isBAJamNode = false;
            for (size_t i = 0; i < nodes.size(); ++i) {
              if (nodes[i] == attachTarget && nodeTypes[i] == "ba_jam") {
                isBAJamNode = true;
                break;
              }
            }

            if (attachTarget && isBAJamNode) {
              // Attach MQTT broker to ba_jam node
              mqttNode->attach_to({attachTarget});
              nodes.push_back(mqttNode);
              nodeTypes.push_back("json_jam_mqtt_broker");
              std::cerr << "[PipelineBuilder] ✓ Auto-added "
                           "json_jam_mqtt_broker node (attached to "
                           "ba_jam)"
                        << std::endl;

              // IMPORTANT: OSD node should attach to ba_jam node, NOT
              // MQTT broker This is critical - OSD node needs lines from
              // ba_jam node to draw From SDK sample:
              // osd->attach_to({ba_jam}) Pipeline should be: ba_jam
              // -> mqtt_broker (parallel) and ba_jam -> osd OSD node will
              // get lines from ba_jam node via pipeline metadata MQTT
              // broker runs in parallel and doesn't need OSD connection So we
              // DON'T reconnect OSD to MQTT broker - OSD stays attached to
              // ba_jam
              std::cerr << "[PipelineBuilder] NOTE: ba_jam_osd node "
                           "should remain attached to ba_jam node "
                           "to receive lines for drawing. MQTT broker runs in "
                           "parallel."
                        << std::endl;
            } else {
              std::cerr << "[PipelineBuilder] ⚠ Failed to find ba_jam "
                           "node to attach MQTT broker"
                        << std::endl;
              std::cerr << "[PipelineBuilder] ⚠ attachTarget: "
                        << (attachTarget ? "found" : "nullptr") << std::endl;
              std::cerr << "[PipelineBuilder] ⚠ isBAJamNode: "
                        << (isBAJamNode ? "true" : "false") << std::endl;
            }
          } else {
            std::cerr << "[PipelineBuilder] ⚠ Failed to create "
                         "json_jam_mqtt_broker node (returned nullptr)"
                      << std::endl;
          }
        } catch (const std::exception &e) {
          std::cerr << "[PipelineBuilder] ⚠ Failed to auto-add "
                       "json_jam_mqtt_broker: "
                    << e.what() << std::endl;
        }
      } else if (hasBAStop && !hasNodeType("json_stop_mqtt_broker")) {
        // Auto-add stop MQTT broker - attach to ba_stop node
        std::cerr << "[PipelineBuilder] Auto-adding json_stop_mqtt_broker "
                     "node (MQTT_BROKER_URL detected)"
                  << std::endl;
        try {
          SolutionConfig::NodeConfig mqttConfig;
          mqttConfig.nodeType = "json_stop_mqtt_broker";
          mqttConfig.nodeName = "json_stop_mqtt_broker_{instanceId}";
          mqttConfig.parameters = {};

          std::string mqttNodeName = mqttConfig.nodeName;
          size_t pos = mqttNodeName.find("{instanceId}");
          while (pos != std::string::npos) {
            mqttNodeName.replace(pos, 12, instanceId);
            pos = mqttNodeName.find("{instanceId}", pos + instanceId.length());
          }

          auto mqttNode = PipelineBuilderBrokerNodes::createJSONStopMQTTBrokerNode(
              mqttNodeName, mqttConfig.parameters, req, instanceId);
          if (mqttNode) {
            // Find ba_stop node to attach to (like in sample code)
            auto attachTarget = findAttachTargetForBrokerStop(true);
            // Verify that attachTarget is actually ba_stop node by
            // checking nodeTypes
            bool isBAStopNode = false;
            for (size_t i = 0; i < nodes.size(); ++i) {
              if (nodes[i] == attachTarget && nodeTypes[i] == "ba_stop") {
                isBAStopNode = true;
                break;
              }
            }

            if (attachTarget && isBAStopNode) {
              // Attach MQTT broker to ba_stop node
              mqttNode->attach_to({attachTarget});
              nodes.push_back(mqttNode);
              nodeTypes.push_back("json_stop_mqtt_broker");
              std::cerr << "[PipelineBuilder] ✓ Auto-added "
                           "json_stop_mqtt_broker node (attached to "
                           "ba_stop)"
                        << std::endl;

              // IMPORTANT: OSD node should attach to ba_stop node, NOT
              // MQTT broker This is critical - OSD node needs lines from
              // ba_stop node to draw From SDK sample:
              // osd->attach_to({ba_stop}) Pipeline should be: ba_stop
              // -> mqtt_broker (parallel) and ba_stop -> osd OSD node will
              // get lines from ba_stop node via pipeline metadata MQTT
              // broker runs in parallel and doesn't need OSD connection So we
              // DON'T reconnect OSD to MQTT broker - OSD stays attached to
              // ba_stop
              std::cerr << "[PipelineBuilder] NOTE: ba_stop_osd node "
                           "should remain attached to ba_stop node "
                           "to receive lines for drawing. MQTT broker runs in "
                           "parallel."
                        << std::endl;
            } else {
              std::cerr << "[PipelineBuilder] ⚠ Failed to find ba_stop "
                           "node to attach MQTT broker"
                        << std::endl;
              std::cerr << "[PipelineBuilder] ⚠ attachTarget: "
                        << (attachTarget ? "found" : "nullptr") << std::endl;
              std::cerr << "[PipelineBuilder] ⚠ isBAStopNode: "
                        << (isBAStopNode ? "true" : "false") << std::endl;
            }
          } else {
            std::cerr << "[PipelineBuilder] ⚠ Failed to create "
                         "json_stop_mqtt_broker node (returned nullptr)"
                      << std::endl;
          }
        } catch (const std::exception &e) {
          std::cerr << "[PipelineBuilder] ⚠ Failed to auto-add "
                       "json_stop_mqtt_broker: "
                    << e.what() << std::endl;
        }
      }
    }
  }
#endif

  // 2. Auto-add RTMP destination if RTMP_URL or RTMP_DES_URL is provided but not in pipeline
  // Check RTMP_DES_URL first (new format), then RTMP_URL (backward compatibility)
  auto rtmpDesUrlIt = req.additionalParams.find("RTMP_DES_URL");
  auto rtmpUrlIt = req.additionalParams.find("RTMP_URL");
  
  // Determine which RTMP URL parameter to use
  std::string rtmpUrl;
  std::string rtmpUrlParamName = "RTMP_URL"; // Default placeholder name
  bool hasRtmpUrl = false;
  
  if (rtmpDesUrlIt != req.additionalParams.end() && !rtmpDesUrlIt->second.empty()) {
    rtmpUrl = rtmpDesUrlIt->second;
    rtmpUrlParamName = "RTMP_DES_URL";
    hasRtmpUrl = true;
  } else if (rtmpUrlIt != req.additionalParams.end() && !rtmpUrlIt->second.empty()) {
    rtmpUrl = rtmpUrlIt->second;
    rtmpUrlParamName = "RTMP_URL";
    hasRtmpUrl = true;
  }
  
  if (hasRtmpUrl) {
    rtmpUrl.erase(0, rtmpUrl.find_first_not_of(" \t\n\r"));
    rtmpUrl.erase(rtmpUrl.find_last_not_of(" \t\n\r") + 1);

    if (!rtmpUrl.empty() && !hasNodeType("rtmp_des") &&
        !(frame_router_ && hasNodeType("frame_router_sink"))) {
      std::cerr
          << "[PipelineBuilder] Auto-adding rtmp_des/frame_router_sink node (" << rtmpUrlParamName << " detected)"
          << std::endl;
      try {
        // Check if pipeline has OSD node (face_osd_v2, osd_v3,
        // ba_crossline_osd)
        bool hasOSDNode = hasNodeType("face_osd_v2") || hasNodeType("osd_v3") ||
                          hasNodeType("ba_crossline_osd") ||
                          hasNodeType("ba_jam_osd") ||
                          hasNodeType("ba_stop_osd") ||
                          hasNodeType("ba_loitering_osd");

        // If no OSD node, auto-add face_osd_v2 node for face detection
        // solutions Check if this is a face detection solution by looking for
        // yunet_face_detector
        bool hasFaceDetector = hasNodeType("yunet_face_detector");
        if (!hasOSDNode && hasFaceDetector) {
          std::cerr << "[PipelineBuilder] Auto-adding face_osd_v2 node for "
                       "RTMP overlay"
                    << std::endl;
          try {
            SolutionConfig::NodeConfig osdConfig;
            osdConfig.nodeType = "face_osd_v2";
            osdConfig.nodeName = "osd_{instanceId}";

            std::string osdNodeName = osdConfig.nodeName;
            size_t pos = osdNodeName.find("{instanceId}");
            while (pos != std::string::npos) {
              osdNodeName.replace(pos, 12, instanceId);
              pos = osdNodeName.find("{instanceId}", pos + instanceId.length());
            }

            auto osdNode = PipelineBuilderOtherNodes::createFaceOSDNode(osdNodeName, osdConfig.parameters);
            if (osdNode) {
              // Find detector node to attach OSD to
              std::shared_ptr<cvedix_nodes::cvedix_node> detectorNode = nullptr;
              for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
                auto node = nodes[i];
                bool isDestNode =
                    std::dynamic_pointer_cast<
                        cvedix_nodes::cvedix_file_des_node>(node) ||
                    std::dynamic_pointer_cast<
                        cvedix_nodes::cvedix_rtmp_des_node>(node) ||
                    std::dynamic_pointer_cast<
                        cvedix_nodes::cvedix_screen_des_node>(node) ||
                    std::dynamic_pointer_cast<
                        cvedix_nodes::cvedix_app_des_node>(node);
                if (!isDestNode) {
                  detectorNode = node;
                  break;
                }
              }

              if (detectorNode) {
                osdNode->attach_to({detectorNode});
                nodes.push_back(osdNode);
                nodeTypes.push_back("face_osd_v2");
                hasOSDNode = true;
                std::cerr << "[PipelineBuilder] ✓ Auto-added face_osd_v2 node"
                          << std::endl;
              } else {
                std::cerr << "[PipelineBuilder] ⚠ Failed to find detector node "
                             "to attach OSD node"
                          << std::endl;
              }
            }
          } catch (const std::exception &e) {
            std::cerr
                << "[PipelineBuilder] ⚠ Failed to auto-add face_osd_v2 node: "
                << e.what() << std::endl;
          }
        }

        // Zero-downtime mode: use FrameRouterSinkNode instead of rtmp_des (output leg is persistent)
        if (frame_router_) {
          std::string sinkName = "frame_router_sink_" + instanceId;
          auto sinkNode = std::make_shared<edgeos::FrameRouterSinkNode>(sinkName, frame_router_);
          std::shared_ptr<cvedix_nodes::cvedix_node> attachTarget = nullptr;
          if (hasOSDNode) {
            for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
              auto node = nodes[i];
              bool isOSDNode =
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_face_osd_node_v2>(node) ||
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_osd_node_v3>(node) ||
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_line_crossline_osd_node>(node) ||
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_area_jam_osd_node>(node) ||
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_stop_osd_node>(node) ||
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_area_enter_exit_osd_node>(node);
              if (isOSDNode) {
                attachTarget = node;
                break;
              }
            }
          }
          if (!attachTarget) {
            for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
              auto node = nodes[i];
              bool isDestNode =
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_des_node>(node) ||
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(node) ||
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_screen_des_node>(node) ||
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_des_node>(node) ||
                  std::dynamic_pointer_cast<edgeos::FrameRouterSinkNode>(node);
              if (!isDestNode) {
                attachTarget = node;
                break;
              }
            }
          }
          if (!attachTarget && !nodes.empty()) {
            attachTarget = nodes.back();
          }
          if (attachTarget) {
            sinkNode->attach_to({attachTarget});
            nodes.push_back(sinkNode);
            nodeTypes.push_back("frame_router_sink");
            std::cerr << "[PipelineBuilder] ✓ Auto-added frame_router_sink (zero-downtime)"
                      << std::endl;
          }
        } else {
        SolutionConfig::NodeConfig rtmpConfig;
        rtmpConfig.nodeType = "rtmp_des";
        rtmpConfig.nodeName = "rtmp_des_{instanceId}";
        // Use the appropriate placeholder based on which parameter was found
        rtmpConfig.parameters["rtmp_url"] = "${" + rtmpUrlParamName + "}";
        rtmpConfig.parameters["channel"] = "0";
        // Don't enable OSD in RTMP node - use OSD node instead

        std::string rtmpNodeName = rtmpConfig.nodeName;
        size_t pos = rtmpNodeName.find("{instanceId}");
        while (pos != std::string::npos) {
          rtmpNodeName.replace(pos, 12, instanceId);
          pos = rtmpNodeName.find("{instanceId}", pos + instanceId.length());
        }

        std::cerr << "[PipelineBuilder] Attempting to create RTMP destination node with parameters:" << std::endl;
        std::cerr << "  rtmp_url param: '" << rtmpConfig.parameters.at("rtmp_url") << "'" << std::endl;
        std::cerr << "  " << rtmpUrlParamName << " from req.additionalParams: ";
        auto rtmpParamIt = req.additionalParams.find(rtmpUrlParamName);
        if (rtmpParamIt != req.additionalParams.end()) {
          std::cerr << "'" << rtmpParamIt->second << "'" << std::endl;
        } else {
          std::cerr << "NOT FOUND" << std::endl;
        }
        
        std::string actualRtmpUrl;
        std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> rtmpExtraNodes;
        auto rtmpNode =
            PipelineBuilderDestinationNodes::createRTMPDestinationNode(rtmpNodeName, rtmpConfig.parameters, req, instanceId, existingRTMPStreamKeys, actualRtmpUrl, &rtmpExtraNodes);
        if (rtmpNode) {
          if (!actualRtmpUrl.empty()) {
            actual_rtmp_urls_[instanceId] = actualRtmpUrl;
            std::cerr << "[PipelineBuilder] Stored actual RTMP URL for instance " << instanceId << ": '" << actualRtmpUrl << "'" << std::endl;
          }
          std::cerr << "[PipelineBuilder] ✓ RTMP destination node created successfully: '"
                    << rtmpNodeName << "'" << std::endl;
          // Find the best node to attach to:
          // 1. If pipeline has OSD node, attach RTMP node to OSD node (to get
          // frames with overlay)
          // 2. If RTMP node has OSD enabled, attach to detector node (to get
          // metadata for overlay)
          // 3. Otherwise, attach to last non-destination node
          std::shared_ptr<cvedix_nodes::cvedix_node> attachTarget = nullptr;
          bool rtmpHasOSD = rtmpConfig.parameters.find("osd") !=
                                rtmpConfig.parameters.end() &&
                            (rtmpConfig.parameters.at("osd") == "true" ||
                             rtmpConfig.parameters.at("osd") == "1");

          // First, try to find OSD node (if pipeline has one and RTMP doesn't
          // have OSD enabled)
          if (hasOSDNode && !rtmpHasOSD) {
            for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
              auto node = nodes[i];
              // Check if this is an OSD node
              bool isOSDNode =
                  std::dynamic_pointer_cast<
                      cvedix_nodes::cvedix_face_osd_node_v2>(node) ||
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_osd_node_v3>(
                      node) ||
                  std::dynamic_pointer_cast<
                      cvedix_nodes::cvedix_ba_line_crossline_osd_node>(node) ||
                  std::dynamic_pointer_cast<
                      cvedix_nodes::cvedix_ba_area_jam_osd_node>(node) ||
                  std::dynamic_pointer_cast<
                      cvedix_nodes::cvedix_ba_stop_osd_node>(node) || // ba_loitering_osd uses ba_stop_osd_node
                  std::dynamic_pointer_cast<
                      cvedix_nodes::cvedix_ba_area_enter_exit_osd_node>(node);
              if (isOSDNode) {
                attachTarget = node;
                std::cerr << "[PipelineBuilder] Attaching RTMP node to OSD "
                             "node for overlay frames"
                          << std::endl;
                break;
              }
            }
          }

          // If RTMP node has OSD enabled, attach to detector node (to get
          // metadata) Or if no OSD node found, attach to last non-destination
          // node
          if (!attachTarget) {
            for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
              auto node = nodes[i];
              // Check if this is a destination node using dynamic_cast
              bool isDestNode =
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_des_node>(
                      node) ||
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(
                      node) ||
                  std::dynamic_pointer_cast<
                      cvedix_nodes::cvedix_screen_des_node>(node) ||
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_des_node>(
                      node);
              if (!isDestNode) {
                attachTarget = node;
                if (rtmpHasOSD) {
                  std::cerr << "[PipelineBuilder] Attaching RTMP node (with "
                               "OSD enabled) to detector node for metadata"
                            << std::endl;
                }
                break;
              }
            }
          }

          // Fallback to last node if no non-destination node found
          if (!attachTarget && !nodes.empty()) {
            attachTarget = nodes.back();
          }

          if (attachTarget) {
            rtmpNode->attach_to({attachTarget});
            nodes.push_back(rtmpNode);
            for (const auto &extra : rtmpExtraNodes) {
              nodes.push_back(extra);
            }
            nodeTypes.push_back("rtmp_lastframe_proxy");
            nodeTypes.push_back("rtmp_des");
            std::cerr
                << "[PipelineBuilder] ✓ Auto-added rtmp_des node (attached to "
                   "same source as other DES nodes - parallel connection)"
                << std::endl;
            std::cerr << "[PipelineBuilder] RTMP node attached to: '"
                      << typeid(*attachTarget).name() << "'" << std::endl;
          } else {
            std::cerr << "[PipelineBuilder] ⚠ Failed to find suitable node to "
                         "attach rtmp_des"
                      << std::endl;
            std::cerr << "[PipelineBuilder] Available nodes count: "
                      << nodes.size() << std::endl;
          }
        } else {
          std::cerr << "[PipelineBuilder] ✗ Failed to create RTMP destination node (returned nullptr)"
                    << std::endl;
          std::cerr << "[PipelineBuilder] This usually means RTMP_URL was empty or invalid"
                    << std::endl;
        }
        }  // end else (non-frame_router_ rtmp_des path)
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilder] ⚠ Failed to auto-add rtmp_des: "
                  << e.what() << std::endl;
      }
    }
  }

  // 3. Auto-add screen destination if ENABLE_SCREEN_DES is true but not in
  // pipeline
  auto screenDesIt = req.additionalParams.find("ENABLE_SCREEN_DES");
  if (screenDesIt != req.additionalParams.end()) {
    std::string enableScreen = screenDesIt->second;
    std::transform(enableScreen.begin(), enableScreen.end(),
                   enableScreen.begin(), ::tolower);
    if ((enableScreen == "true" || enableScreen == "1" ||
         enableScreen == "yes" || enableScreen == "on") &&
        !hasNodeType("screen_des")) {
      std::cerr << "[PipelineBuilder] Auto-adding screen_des node "
                   "(ENABLE_SCREEN_DES=true detected)"
                << std::endl;
      try {
        SolutionConfig::NodeConfig screenConfig;
        screenConfig.nodeType = "screen_des";
        screenConfig.nodeName = "screen_des_{instanceId}";
        screenConfig.parameters["channel"] = "0";
        screenConfig.parameters["enabled"] = "true";

        std::string screenNodeName = screenConfig.nodeName;
        size_t pos = screenNodeName.find("{instanceId}");
        while (pos != std::string::npos) {
          screenNodeName.replace(pos, 12, instanceId);
          pos = screenNodeName.find("{instanceId}", pos + instanceId.length());
        }

        auto screenNode = PipelineBuilderDestinationNodes::createScreenDestinationNode(screenNodeName,
                                                      screenConfig.parameters);
        if (screenNode) {
          // Find the last non-destination node to attach to
          // Use dynamic_cast to check actual node type, not just nodeTypes
          std::shared_ptr<cvedix_nodes::cvedix_node> attachTarget = nullptr;
          for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
            auto node = nodes[i];
            // Check if this is a destination node using dynamic_cast
            bool isDestNode =
                std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_des_node>(
                    node) ||
                std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_des_node>(
                    node) ||
                std::dynamic_pointer_cast<cvedix_nodes::cvedix_screen_des_node>(
                    node) ||
                std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_des_node>(
                    node);
            if (!isDestNode) {
              attachTarget = node;
              break;
            }
          }

          // Fallback to last node if no non-destination node found
          if (!attachTarget && !nodes.empty()) {
            attachTarget = nodes.back();
          }

          if (attachTarget) {
            screenNode->attach_to({attachTarget});
            nodes.push_back(screenNode);
            nodeTypes.push_back("screen_des");
            std::cerr
                << "[PipelineBuilder] ✓ Auto-added screen_des node (attached "
                   "to same source as other DES nodes - parallel connection)"
                << std::endl;
          } else {
            std::cerr << "[PipelineBuilder] ⚠ Failed to find suitable node to "
                         "attach screen_des"
                      << std::endl;
          }
        }
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilder] ⚠ Failed to auto-add screen_des: "
                  << e.what() << std::endl;
      }
    }
  }

  std::cerr << "[PipelineBuilder] Successfully built pipeline with "
            << nodes.size() << " nodes" << std::endl;
  edgeos::FrameRouter* unused = frame_router_;
  frame_router_ = nullptr;
  (void)unused;
  return nodes;
}

#ifdef CVEDIX_WITH_MQTT
namespace pipeline_builder_crossline_mqtt {
std::shared_ptr<cvedix_nodes::cvedix_node> createCrosslineMQTTBrokerNode(
    const std::string &nodeName,
    std::function<void(const std::string &)> mqtt_publish_func,
    const std::string &instance_id, const std::string &instance_name,
    const std::string &zone_id, const std::string &zone_name,
    const std::string &crossing_lines_json);
}
#endif

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilder::createNode(const SolutionConfig::NodeConfig &nodeConfig,
                            const CreateInstanceRequest &req,
                            const std::string &instanceId,
                            const std::set<std::string> &existingRTMPStreamKeys,
                            std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> *outExtraNodes) {

  // Get node name with instanceId substituted
  std::string nodeName = substituteNodeName(nodeConfig.nodeName, instanceId);

  // Debug: log the final node name
  std::cerr << "[PipelineBuilder] Original node name template: '"
            << nodeConfig.nodeName << "'" << std::endl;
  std::cerr << "[PipelineBuilder] Node name after substitution: '" << nodeName
            << "'" << std::endl;

  // Build parameter map with substitutions
  std::map<std::string, std::string> params = buildParameterMap(nodeConfig, req, instanceId);

  // Create node based on type
  try {
    // Source nodes - Auto-detect source type for file_src based on FILE_PATH or
    // explicit parameters
    std::string actualNodeType = detectSourceType(nodeConfig, req, nodeName, params);

    // Source nodes
    if (actualNodeType == "rtsp_src") {
      return PipelineBuilderSourceNodes::createRTSPSourceNode(nodeName, params, req);
    } else if (actualNodeType == "file_src") {
      return PipelineBuilderSourceNodes::createFileSourceNode(nodeName, params, req);
    } else if (actualNodeType == "app_src") {
      return PipelineBuilderSourceNodes::createAppSourceNode(nodeName, params, req);
    } else if (actualNodeType == "image_src") {
      return PipelineBuilderSourceNodes::createImageSourceNode(nodeName, params, req);
    } else if (actualNodeType == "rtmp_src") {
      return PipelineBuilderSourceNodes::createRTMPSourceNode(nodeName, params, req);
    } else if (actualNodeType == "udp_src") {
      return PipelineBuilderSourceNodes::createUDPSourceNode(nodeName, params, req);
    } else if (actualNodeType == "ff_src") {
      return PipelineBuilderSourceNodes::createFFmpegSourceNode(nodeName, params, req);
    }
    // Face detection nodes
    else if (nodeConfig.nodeType == "yunet_face_detector") {
      return PipelineBuilderDetectorNodes::createFaceDetectorNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "sface_feature_encoder") {
      return PipelineBuilderDetectorNodes::createSFaceEncoderNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "face_osd_v2") {
      return PipelineBuilderOtherNodes::createFaceOSDNode(nodeName, params);
    }
#ifdef CVEDIX_WITH_TRT
    // TensorRT YOLOv8 nodes
    else if (nodeConfig.nodeType == "trt_yolov8_detector") {
      return PipelineBuilderDetectorNodes::createTRTYOLOv8DetectorNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "trt_yolov8_seg_detector") {
      return PipelineBuilderDetectorNodes::createTRTYOLOv8SegDetectorNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "trt_yolov8_pose_detector") {
      return PipelineBuilderDetectorNodes::createTRTYOLOv8PoseDetectorNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "trt_yolov8_classifier") {
      return PipelineBuilderDetectorNodes::createTRTYOLOv8ClassifierNode(nodeName, params, req);
    }
    // TensorRT Vehicle nodes
    else if (nodeConfig.nodeType == "trt_vehicle_detector") {
      return PipelineBuilderDetectorNodes::createTRTVehicleDetectorNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "trt_vehicle_plate_detector") {
      return PipelineBuilderDetectorNodes::createTRTVehiclePlateDetectorNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "trt_vehicle_plate_detector_v2") {
      return PipelineBuilderDetectorNodes::createTRTVehiclePlateDetectorV2Node(nodeName, params, req);
    } else if (nodeConfig.nodeType == "trt_vehicle_color_classifier") {
      return PipelineBuilderDetectorNodes::createTRTVehicleColorClassifierNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "trt_vehicle_type_classifier") {
      return PipelineBuilderDetectorNodes::createTRTVehicleTypeClassifierNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "trt_vehicle_feature_encoder") {
      return PipelineBuilderDetectorNodes::createTRTVehicleFeatureEncoderNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "trt_vehicle_scanner") {
      return PipelineBuilderDetectorNodes::createTRTVehicleScannerNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "trt_insight_face_recognition") {
      return PipelineBuilderDetectorNodes::createTRTInsightFaceRecognitionNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "trt_yolov11_face_detector") {
      return PipelineBuilderDetectorNodes::createTRTYOLOv11FaceDetectorNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "trt_yolov11_plate_detector") {
      return PipelineBuilderDetectorNodes::createTRTYOLOv11PlateDetectorNode(nodeName, params, req);
    }
#endif
#ifdef CVEDIX_WITH_RKNN
    // RKNN nodes
    else if (nodeConfig.nodeType == "rknn_yolov8_detector") {
      return PipelineBuilderDetectorNodes::createRKNNYOLOv8DetectorNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "rknn_yolov11_detector") {
      return PipelineBuilderDetectorNodes::createRKNNYOLOv11DetectorNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "rknn_face_detector") {
      return PipelineBuilderDetectorNodes::createRKNNFaceDetectorNode(nodeName, params, req);
    }
#endif
    // Other inference nodes
    else if (nodeConfig.nodeType == "yolo_detector") {
      return PipelineBuilderDetectorNodes::createYOLODetectorNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "yolov11_detector") {
      return PipelineBuilderDetectorNodes::createYOLOv11DetectorNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "yolov11_plate_detector") {
      return PipelineBuilderDetectorNodes::createYOLOv11PlateDetectorNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "enet_seg") {
      return PipelineBuilderDetectorNodes::createENetSegNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "mask_rcnn_detector") {
      return PipelineBuilderDetectorNodes::createMaskRCNNDetectorNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "openpose_detector") {
#ifdef CVEDIX_HAS_OPENPOSE
      return PipelineBuilderDetectorNodes::createOpenPoseDetectorNode(nodeName, params, req);
#else
      throw std::runtime_error(
          "openpose_detector is not available in this EdgeOS/CVEDIX SDK build");
#endif
    } else if (nodeConfig.nodeType == "classifier") {
      return PipelineBuilderDetectorNodes::createClassifierNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "feature_encoder") {
      std::cerr << "[PipelineBuilder] feature_encoder node type is not "
                   "supported (abstract class). Use 'sface_feature_encoder' or "
                   "'trt_vehicle_feature_encoder' instead."
                << std::endl;
      throw std::runtime_error(
          "feature_encoder node type is not supported. Use "
          "'sface_feature_encoder' or 'trt_vehicle_feature_encoder' instead.");
    } else if (nodeConfig.nodeType == "lane_detector") {
      return PipelineBuilderDetectorNodes::createLaneDetectorNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "face_swap") {
#ifdef CVEDIX_HAS_FACE_SWAP
      return PipelineBuilderDetectorNodes::createFaceSwapNode(nodeName, params, req);
#else
      throw std::runtime_error(
          "face_swap is not available in this EdgeOS/CVEDIX SDK build");
#endif
    } else if (nodeConfig.nodeType == "insight_face_recognition") {
#ifdef CVEDIX_HAS_FACENET
      return PipelineBuilderDetectorNodes::createInsightFaceRecognitionNode(nodeName, params, req);
#else
      throw std::runtime_error(
          "insight_face_recognition is not available in this EdgeOS/CVEDIX SDK build");
#endif
#ifdef CVEDIX_WITH_LLM
    } else if (nodeConfig.nodeType == "mllm_analyser") {
      return PipelineBuilderDetectorNodes::createMLLMAnalyserNode(nodeName, params, req);
#endif
#ifdef CVEDIX_WITH_PADDLE
    } else if (nodeConfig.nodeType == "ppocr_text_detector") {
      return PipelineBuilderDetectorNodes::createPaddleOCRTextDetectorNode(nodeName, params, req);
#endif
    } else if (nodeConfig.nodeType == "restoration") {
#ifdef CVEDIX_HAS_RESTORATION
      return PipelineBuilderDetectorNodes::createRestorationNode(nodeName, params, req);
#else
      throw std::runtime_error(
          "restoration is not available in this EdgeOS/CVEDIX SDK build");
#endif
    } else if (nodeConfig.nodeType == "frame_fusion") {
      return PipelineBuilderOtherNodes::createFrameFusionNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "record") {
      return PipelineBuilderOtherNodes::createRecordNode(nodeName, params, req);
    }
    // Tracking nodes
    else if (nodeConfig.nodeType == "sort_track") {
      return PipelineBuilderOtherNodes::createSORTTrackNode(nodeName, params);
    } else if (nodeConfig.nodeType == "bytetrack" || nodeConfig.nodeType == "bytetrack_track") {
      return PipelineBuilderOtherNodes::createByteTrackNode(nodeName, params);
    } else if (nodeConfig.nodeType == "ocsort" || nodeConfig.nodeType == "ocsort_track") {
      // TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
      std::cerr << "[PipelineBuilder] ocsort_track is temporarily disabled due to CVEDIX SDK cereal/rapidxml issue"
                << std::endl;
      return nullptr;
      // return PipelineBuilderOtherNodes::createOCSortTrackNode(nodeName, params);
    }
    // Behavior Analysis nodes
    else if (nodeConfig.nodeType == "ba_crossline") {
      return PipelineBuilderBehaviorAnalysisNodes::createBACrosslineNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "ba_jam") {
      return PipelineBuilderBehaviorAnalysisNodes::createBAJamNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "ba_stop") {
      return PipelineBuilderBehaviorAnalysisNodes::createBAStopNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "ba_loitering") {
      return PipelineBuilderBehaviorAnalysisNodes::createBALoiteringNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "ba_area_enter_exit") {
      return PipelineBuilderBehaviorAnalysisNodes::createBAAreaEnterExitNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "ba_line_counting") {
      return PipelineBuilderBehaviorAnalysisNodes::createBALineCountingNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "ba_crowding") {
      return PipelineBuilderBehaviorAnalysisNodes::createBACrowdingNode(nodeName, params, req);
    }
    // OSD nodes
    else if (nodeConfig.nodeType == "face_osd_v2") {
      return PipelineBuilderOtherNodes::createFaceOSDNode(nodeName, params);
    } else if (nodeConfig.nodeType == "osd_v3") {
      return PipelineBuilderOtherNodes::createOSDv3Node(nodeName, params, req);
    } else if (nodeConfig.nodeType == "ba_crossline_osd") {
      return PipelineBuilderBehaviorAnalysisNodes::createBACrosslineOSDNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "ba_jam_osd") {
      return PipelineBuilderBehaviorAnalysisNodes::createBAJamOSDNode(nodeName, params);
    } else if (nodeConfig.nodeType == "ba_stop_osd") {
      return PipelineBuilderBehaviorAnalysisNodes::createBAStopOSDNode(nodeName, params);
    } else if (nodeConfig.nodeType == "ba_loitering_osd") {
      return PipelineBuilderBehaviorAnalysisNodes::createBALoiteringOSDNode(nodeName, params);
    } else if (nodeConfig.nodeType == "ba_area_enter_exit_osd") {
      return PipelineBuilderBehaviorAnalysisNodes::createBAAreaEnterExitOSDNode(nodeName, params);
    } else if (nodeConfig.nodeType == "ba_crowding_osd") {
      return PipelineBuilderBehaviorAnalysisNodes::createBACrowdingOSDNode(nodeName, params);
    }
    // Broker nodes
    else if (nodeConfig.nodeType == "json_console_broker") {
      return PipelineBuilderBrokerNodes::createJSONConsoleBrokerNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "json_enhanced_console_broker") {
      // json_enhanced_console_broker requires cereal - now enabled
      return PipelineBuilderBrokerNodes::createJSONEnhancedConsoleBrokerNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "json_mqtt_broker") {
#ifdef CVEDIX_WITH_MQTT
      // CRITICAL: cvedix_json_mqtt_broker_node is currently broken and causes
      // crashes Disabled to prevent application crashes Use json_console_broker
      // with MQTT publishing in instance_registry instead
      std::cerr << "[PipelineBuilder] WARNING: json_mqtt_broker_node is "
                   "DISABLED due to crash issues"
                << std::endl;
      std::cerr << "[PipelineBuilder] Use json_console_broker node instead - "
                   "MQTT will be handled by instance_registry"
                << std::endl;
      return nullptr;
#else
      std::cerr << "[PipelineBuilder] json_mqtt_broker requires "
                   "CVEDIX_WITH_MQTT to be enabled"
                << std::endl;
      return nullptr;
#endif
    } else if (nodeConfig.nodeType == "json_crossline_mqtt_broker") {
#ifdef CVEDIX_WITH_MQTT
      // Create crossline MQTT broker node inline (class is defined in this file)
      // Supports CrossingLines from additionalParams (top-level or from input/output merge)
      std::string instance_id = instanceId;
      std::string instance_name = req.name.empty() ? instanceId : req.name;
      std::string zone_id = "default_zone";
      std::string zone_name = "CrosslineZone";
      auto zoneIdIt = req.additionalParams.find("ZONE_ID");
      if (zoneIdIt != req.additionalParams.end() && !zoneIdIt->second.empty())
        zone_id = zoneIdIt->second;
      auto zoneNameIt = req.additionalParams.find("ZONE_NAME");
      if (zoneNameIt != req.additionalParams.end() && !zoneNameIt->second.empty())
        zone_name = zoneNameIt->second;

      std::string mqtt_broker;
      int mqtt_port = 1883;
      std::string mqtt_topic = "events";
      std::string mqtt_username, mqtt_password;
      auto brokerIt = req.additionalParams.find("MQTT_BROKER_URL");
      if (brokerIt != req.additionalParams.end() && !brokerIt->second.empty()) {
        mqtt_broker = brokerIt->second;
        size_t p = mqtt_broker.find_first_not_of(" \t\n\r");
        if (p != std::string::npos) {
          size_t q = mqtt_broker.find_last_not_of(" \t\n\r");
          mqtt_broker = mqtt_broker.substr(p, q != std::string::npos ? q - p + 1 : std::string::npos);
        }
      }
      auto portIt = req.additionalParams.find("MQTT_PORT");
      if (portIt != req.additionalParams.end() && !portIt->second.empty()) {
        try { mqtt_port = std::stoi(portIt->second); } catch (...) {}
      }
      auto topicIt = req.additionalParams.find("MQTT_TOPIC");
      if (topicIt != req.additionalParams.end() && !topicIt->second.empty())
        mqtt_topic = topicIt->second;
      auto usernameIt = req.additionalParams.find("MQTT_USERNAME");
      if (usernameIt != req.additionalParams.end()) mqtt_username = usernameIt->second;
      auto passwordIt = req.additionalParams.find("MQTT_PASSWORD");
      if (passwordIt != req.additionalParams.end()) mqtt_password = passwordIt->second;

      std::string crossing_lines_json;
      auto crossingLinesIt = req.additionalParams.find("CrossingLines");
      if (crossingLinesIt != req.additionalParams.end() && !crossingLinesIt->second.empty())
        crossing_lines_json = crossingLinesIt->second;

      if (mqtt_broker.empty()) {
        std::cerr << "[PipelineBuilder] json_crossline_mqtt_broker: MQTT_BROKER_URL empty, skipping"
                  << std::endl;
        return nullptr;
      }

      std::string client_id = "edgeos_api_" + instance_id;
      auto mqtt_client = std::make_unique<cvedix_utils::cvedix_mqtt_client>(
          mqtt_broker, mqtt_port, client_id, 60);
      mqtt_client->set_auto_reconnect(true, 5000);
      bool connected = mqtt_client->connect(mqtt_username, mqtt_password);
      if (connected)
        std::cerr << "[PipelineBuilder] [MQTT] Crossline broker connected to " << mqtt_broker << ":"
                  << mqtt_port << std::endl;
      else
        std::cerr << "[PipelineBuilder] [MQTT] Crossline broker connection pending (auto-reconnect)"
                  << std::endl;

      static std::mutex mqtt_crossline_publish_mutex;
      auto mqtt_client_ptr = std::shared_ptr<cvedix_utils::cvedix_mqtt_client>(mqtt_client.release());
      auto mqtt_publish_func = [mqtt_client_ptr, mqtt_topic](const std::string &json_message) {
        std::lock_guard<std::mutex> lock(mqtt_crossline_publish_mutex);
        if (!mqtt_client_ptr || !mqtt_client_ptr->is_ready()) return;
        if (json_message.empty()) return;
        mqtt_client_ptr->publish(mqtt_topic, json_message, 1, false);
        std::cerr << "[MQTT] ✓ Published crossline event to " << mqtt_topic << std::endl;
      };

      try {
        auto node = pipeline_builder_crossline_mqtt::createCrosslineMQTTBrokerNode(
            nodeName, mqtt_publish_func, instance_id, instance_name, zone_id, zone_name,
            crossing_lines_json);
        if (node) {
          std::cerr << "[PipelineBuilder] ✓ json_crossline_mqtt_broker created (topic: "
                    << mqtt_topic << ", CrossingLines: " << (crossing_lines_json.empty() ? "none" : "set")
                    << ")" << std::endl;
        }
        return node;
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilder] Failed to create json_crossline_mqtt_broker: " << e.what()
                  << std::endl;
        return nullptr;
      }
#else
      std::cerr << "[PipelineBuilder] json_crossline_mqtt_broker requires "
                   "CVEDIX_WITH_MQTT to be enabled"
                << std::endl;
      return nullptr;
#endif
    } else if (nodeConfig.nodeType == "json_jam_mqtt_broker") {
#ifdef CVEDIX_WITH_MQTT
      // Use generic JSON MQTT broker for jam events (custom broker can be
      // added later)
      // return createJSONJamMQTTBrokerNode(nodeName, params, req);
#else
      std::cerr << "[PipelineBuilder] json_jam_mqtt_broker requires "
                   "CVEDIX_WITH_MQTT to be enabled"
                << std::endl;
      return nullptr;
#endif
    } else if (nodeConfig.nodeType == "json_stop_mqtt_broker") {
#ifdef CVEDIX_WITH_MQTT
      // Use generic JSON MQTT broker for stop events
      // return createJSONMQTTBrokerNode(nodeName, params, req);
#else
      std::cerr << "[PipelineBuilder] json_stop_mqtt_broker requires "
                   "CVEDIX_WITH_MQTT to be enabled"
                << std::endl;
      return nullptr;
#endif
    } else if (nodeConfig.nodeType == "json_kafka_broker") {
#ifdef CVEDIX_WITH_KAFKA
      return PipelineBuilderBrokerNodes::createJSONKafkaBrokerNode(nodeName, params, req);
#else
      std::cerr << "[PipelineBuilder] json_kafka_broker requires "
                   "CVEDIX_WITH_KAFKA to be enabled"
                << std::endl;
      return nullptr;
#endif
    } else if (nodeConfig.nodeType == "sse_broker") {
      return PipelineBuilderBrokerNodes::createSSEBrokerNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "xml_file_broker") {
      // TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
      std::cerr << "[PipelineBuilder] xml_file_broker is temporarily disabled due to CVEDIX SDK cereal/rapidxml issue"
                << std::endl;
      return nullptr;
      // return PipelineBuilderBrokerNodes::createXMLFileBrokerNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "xml_socket_broker") {
      // TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
      std::cerr << "[PipelineBuilder] xml_socket_broker is temporarily disabled due to CVEDIX SDK cereal/rapidxml issue"
                << std::endl;
      return nullptr;
      // return PipelineBuilderBrokerNodes::createXMLSocketBrokerNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "msg_broker") {
      // msg_broker is abstract class, cannot be instantiated
      std::cerr << "[PipelineBuilder] msg_broker is an abstract class and cannot be instantiated. "
                   "Use specific broker types (xml_file_broker, xml_socket_broker, etc.) instead."
                << std::endl;
      return nullptr;
    } else if (nodeConfig.nodeType == "ba_socket_broker") {
      return PipelineBuilderBrokerNodes::createBASocketBrokerNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "embeddings_socket_broker") {
      return PipelineBuilderBrokerNodes::createEmbeddingsSocketBrokerNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "embeddings_properties_socket_broker") {
      return PipelineBuilderBrokerNodes::createEmbeddingsPropertiesSocketBrokerNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "plate_socket_broker") {
      return PipelineBuilderBrokerNodes::createPlateSocketBrokerNode(nodeName, params, req);
    } else if (nodeConfig.nodeType == "expr_socket_broker") {
      return PipelineBuilderBrokerNodes::createExpressionSocketBrokerNode(nodeName, params, req);
    }
    // Destination nodes
    else if (nodeConfig.nodeType == "file_des") {
      return PipelineBuilderDestinationNodes::createFileDestinationNode(nodeName, params, instanceId);
    } else if (nodeConfig.nodeType == "rtmp_des") {
      if (frame_router_) {
        std::string sinkName = "frame_router_sink_" + instanceId;
        auto sinkNode = std::make_shared<edgeos::FrameRouterSinkNode>(sinkName, frame_router_);
        std::cerr << "[PipelineBuilder] Created FrameRouterSinkNode (zero-downtime) instead of rtmp_des"
                  << std::endl;
        return sinkNode;
      }
      std::string actualRtmpUrl;
      auto node = PipelineBuilderDestinationNodes::createRTMPDestinationNode(
          nodeName, params, req, instanceId, existingRTMPStreamKeys,
          actualRtmpUrl, outExtraNodes);
      if (!actualRtmpUrl.empty()) {
        actual_rtmp_urls_[instanceId] = actualRtmpUrl;
        std::cerr << "[PipelineBuilder] Stored actual RTMP URL for instance " << instanceId << ": '" << actualRtmpUrl << "'" << std::endl;
      }
      return node;
    } else if (nodeConfig.nodeType == "rtsp_des") {
      return PipelineBuilderDestinationNodes::createRTSPDestinationNode(nodeName, params, instanceId);
    } else if (nodeConfig.nodeType == "screen_des") {
      return PipelineBuilderDestinationNodes::createScreenDestinationNode(nodeName, params);
    } else {
      std::cerr << "[PipelineBuilder] Unknown node type: "
                << nodeConfig.nodeType << std::endl;
      throw std::runtime_error("Unknown node type: " + nodeConfig.nodeType);
    }
    // This should never be reached, but added to satisfy compiler warning
    return nullptr;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilder] Error in createNode for type "
              << nodeConfig.nodeType << ": " << e.what() << std::endl;
    throw;
  } catch (...) {
    std::cerr << "[PipelineBuilder] Unknown error in createNode for type "
              << nodeConfig.nodeType << std::endl;
    throw std::runtime_error("Unknown error creating node type: " +
                             nodeConfig.nodeType);
  }
}

// ========== Helper Methods for createNode() ==========

std::string PipelineBuilder::substituteNodeName(const std::string &nodeName,
                                                  const std::string &instanceId) {
  std::string result = nodeName;
  std::string placeholder = "{instanceId}";
  size_t pos = result.find(placeholder);
  while (pos != std::string::npos) {
    result.replace(pos, placeholder.length(), instanceId);
    pos = result.find(placeholder, pos + instanceId.length());
  }
  return result;
}

std::map<std::string, std::string>
PipelineBuilder::buildParameterMap(const SolutionConfig::NodeConfig &nodeConfig,
                                    const CreateInstanceRequest &req,
                                    const std::string &instanceId) {
  std::map<std::string, std::string> params;
  const bool isFaceDetectorNode =
      nodeConfig.nodeType == "yunet_face_detector" ||
      nodeConfig.nodeType == "rknn_face_detector" ||
      nodeConfig.nodeType == "trt_yolov11_face_detector";
  const bool isBaSolution =
      req.solution == "ba_crossline" || req.solution == "ba_jam" ||
      req.solution == "ba_stop" || req.solution == "ba_loitering";
  
  for (const auto &param : nodeConfig.parameters) {
    std::string value = param.second;

    // Replace {instanceId}
    value = substituteNodeName(value, instanceId);

    // Replace ${variable} from request
    // Map detectionSensitivity to threshold value
    if (param.first == "score_threshold" &&
        value == "${detectionSensitivity}") {
      double threshold = PipelineBuilderModelResolver::mapDetectionSensitivity(req.detectionSensitivity);
      std::ostringstream oss;
      oss << threshold;
      value = oss.str();
    } else if (value == "${frameRateLimit}") {
      std::ostringstream oss;
      oss << req.frameRateLimit;
      value = oss.str();
    } else if (value == "${RTSP_URL}" || value == "${RTSP_SRC_URL}") {
      value = PipelineBuilderRequestUtils::getRTSPUrl(req);
    } else if (value == "${FILE_PATH}") {
      value = PipelineBuilderRequestUtils::getFilePath(req);
    } else if (value == "${RTMP_URL}" || value == "${RTMP_DES_URL}") {
      value = PipelineBuilderRequestUtils::getRTMPUrl(req);
    } else if (value == "${WEIGHTS_PATH}") {
      auto it = req.additionalParams.find("WEIGHTS_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        value = it->second;
      } else {
        // Backward compatibility: many requests provide MODEL_PATH only.
        auto modelIt = req.additionalParams.find("MODEL_PATH");
        if (modelIt != req.additionalParams.end() && !modelIt->second.empty()) {
          value = modelIt->second;
        }
      }
    } else if (value == "${CONFIG_PATH}") {
      auto it = req.additionalParams.find("CONFIG_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        value = it->second;
      }
    } else if (value == "${LABELS_PATH}") {
      auto it = req.additionalParams.find("LABELS_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        value = it->second;
      }
    } else if (value == "${ENABLE_SCREEN_DES}") {
      auto it = req.additionalParams.find("ENABLE_SCREEN_DES");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        value = it->second;
      } else {
        // Default: empty string means enabled if display available
        value = "";
      }
    } else if (param.first == "model_path" && value == "${MODEL_PATH}") {
      // Priority: MODEL_NAME > MODEL_PATH > default
      std::string modelPath;

      // 1. Check MODEL_NAME first (allows user to select model by name)
      auto modelNameIt = req.additionalParams.find("MODEL_NAME");
      if (modelNameIt != req.additionalParams.end() &&
          !modelNameIt->second.empty()) {
        std::string modelName = modelNameIt->second;
        std::string category = "face"; // Default category

        // Check if category is specified (format: "category:modelname" or just
        // "modelname")
        size_t colonPos = modelName.find(':');
        if (colonPos != std::string::npos) {
          category = modelName.substr(0, colonPos);
          modelName = modelName.substr(colonPos + 1);
        }

        modelPath = PipelineBuilderModelResolver::resolveModelByName(modelName, category);
        if (!modelPath.empty()) {
          std::cerr << "[PipelineBuilder] Using model by name: '"
                    << modelNameIt->second << "' -> " << modelPath << std::endl;
          value = modelPath;
        } else {
          std::cerr << "[PipelineBuilder] WARNING: Model name '"
                    << modelNameIt->second
                    << "' not found, falling back to default" << std::endl;
        }
      }

      // 2. If MODEL_NAME not found or not provided, check MODEL_PATH
      if (modelPath.empty()) {
        auto it = req.additionalParams.find("MODEL_PATH");
        if (it != req.additionalParams.end() && !it->second.empty()) {
          value = it->second;
        } else {
          // Default to yunet.onnx - resolve path intelligently
          value = PipelineBuilderModelResolver::resolveModelPath("models/face/yunet.onnx");
        }
      } else {
        value = modelPath;
      }
    } else if (param.first == "model_path" &&
               value == "${FACE_DETECTION_MODEL_PATH}") {
      // Face detector must not inherit MODEL_PATH from the main detector.
      auto it = req.additionalParams.find("FACE_DETECTION_MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        value = it->second;
      } else {
        value = PipelineBuilderModelResolver::resolveModelPath(
            "models/face/yunet.onnx");
      }
    } else if (param.first == "model_path" && value == "${SFACE_MODEL_PATH}") {
      // Handle SFace model path
      std::string modelPath;

      // 1. Check SFACE_MODEL_NAME first
      auto modelNameIt = req.additionalParams.find("SFACE_MODEL_NAME");
      if (modelNameIt != req.additionalParams.end() &&
          !modelNameIt->second.empty()) {
        std::string modelName = modelNameIt->second;
        std::string category = "face";

        size_t colonPos = modelName.find(':');
        if (colonPos != std::string::npos) {
          category = modelName.substr(0, colonPos);
          modelName = modelName.substr(colonPos + 1);
        }

        modelPath = PipelineBuilderModelResolver::resolveModelByName(modelName, category);
        if (!modelPath.empty()) {
          value = modelPath;
        }
      }

      // 2. If SFACE_MODEL_NAME not found, check SFACE_MODEL_PATH
      if (modelPath.empty()) {
        auto it = req.additionalParams.find("SFACE_MODEL_PATH");
        if (it != req.additionalParams.end() && !it->second.empty()) {
          value = it->second;
        } else {
          // Default to sface model - resolve path intelligently
          value = PipelineBuilderModelResolver::resolveModelPath(
              "models/face/face_recognition_sface_2021dec.onnx");
        }
      } else {
        value = modelPath;
      }
    }

    // Override model_path if provided in additionalParams (even if not using
    // ${MODEL_PATH} placeholder)
    if (param.first == "model_path" && !(isFaceDetectorNode && isBaSolution)) {
      // Priority: MODEL_NAME > MODEL_PATH
      std::string modelPath;

      auto modelNameIt = req.additionalParams.find("MODEL_NAME");
      if (modelNameIt != req.additionalParams.end() &&
          !modelNameIt->second.empty()) {
        std::string modelName = modelNameIt->second;
        std::string category = "face";

        size_t colonPos = modelName.find(':');
        if (colonPos != std::string::npos) {
          category = modelName.substr(0, colonPos);
          modelName = modelName.substr(colonPos + 1);
        }

        modelPath = PipelineBuilderModelResolver::resolveModelByName(modelName, category);
        if (!modelPath.empty()) {
          value = modelPath;
        }
      }

      if (modelPath.empty()) {
        auto it = req.additionalParams.find("MODEL_PATH");
        if (it != req.additionalParams.end() && !it->second.empty()) {
          value = it->second;
        }
      } else {
        value = modelPath;
      }
    }

    // Generic placeholder substitution: Replace ${VARIABLE_NAME} with values
    // from additionalParams This handles placeholders that weren't explicitly
    // handled above (e.g., ${BROKE_FOR})
    std::regex placeholderPattern("\\$\\{([A-Za-z0-9_]+)\\}");
    std::sregex_iterator iter(value.begin(), value.end(), placeholderPattern);
    std::sregex_iterator end;
    std::set<std::string> processedVars; // Track processed variables to avoid
                                         // duplicate replacements

    for (; iter != end; ++iter) {
      std::string varName = (*iter)[1].str();

      // Skip if already processed
      if (processedVars.find(varName) != processedVars.end()) {
        continue;
      }
      processedVars.insert(varName);

      auto it = req.additionalParams.find(varName);
      if (it != req.additionalParams.end() && !it->second.empty()) {
        // Replace all occurrences of this placeholder
        value = std::regex_replace(
            value, std::regex("\\$\\{" + varName + "\\}"), it->second);
        std::cerr << "[PipelineBuilder] Replaced ${" << varName
                  << "} with: " << it->second << std::endl;
      } else {
        // Placeholder not found in additionalParams - leave as is
        std::cerr << "[PipelineBuilder] WARNING: Placeholder ${" << varName
                  << "} not found in additionalParams, leaving as literal"
                  << std::endl;
      }
    }

    params[param.first] = value;
  }
  
  return params;
}

std::string PipelineBuilder::detectSourceType(const SolutionConfig::NodeConfig &nodeConfig,
                                               const CreateInstanceRequest &req,
                                               std::string &nodeName,
                                               std::map<std::string, std::string> &params) {
  std::string actualNodeType = nodeConfig.nodeType;
  
  if (nodeConfig.nodeType != "file_src") {
    return actualNodeType;
  }

  // Priority 0: Check INPUT_SRC (auto-detect input type from single source parameter)
  auto inputSrcIt = req.additionalParams.find("INPUT_SRC");
  if (inputSrcIt != req.additionalParams.end() && !inputSrcIt->second.empty()) {
    std::string inputSrc = inputSrcIt->second;
    // Trim whitespace
    inputSrc.erase(0, inputSrc.find_first_not_of(" \t\n\r"));
    inputSrc.erase(inputSrc.find_last_not_of(" \t\n\r") + 1);
    
    if (!inputSrc.empty()) {
      std::string inputType = PipelineBuilderSourceNodes::detectInputType(inputSrc);
      std::cerr << "[PipelineBuilder] INPUT_SRC detected: '" << inputSrc 
                << "' (auto-detected type: " << inputType << ")" << std::endl;
      
      if (inputType == "rtsp") {
        std::cerr << "[PipelineBuilder] Auto-detected RTSP source from INPUT_SRC, using rtsp_src" << std::endl;
        actualNodeType = "rtsp_src";
        params["rtsp_url"] = inputSrc;
        size_t fileSrcPos = nodeName.find("file_src");
        if (fileSrcPos != std::string::npos) {
          nodeName.replace(fileSrcPos, 8, "rtsp_src");
          std::cerr << "[PipelineBuilder] Updated node name to reflect RTSP source: '"
                    << nodeName << "'" << std::endl;
        }
        return actualNodeType;
      } else if (inputType == "rtmp") {
        std::cerr << "[PipelineBuilder] Auto-detected RTMP source from INPUT_SRC, using rtmp_src" << std::endl;
        actualNodeType = "rtmp_src";
        params["rtmp_url"] = inputSrc;
        size_t fileSrcPos = nodeName.find("file_src");
        if (fileSrcPos != std::string::npos) {
          nodeName.replace(fileSrcPos, 8, "rtmp_src");
          std::cerr << "[PipelineBuilder] Updated node name to reflect RTMP source: '"
                    << nodeName << "'" << std::endl;
        }
        return actualNodeType;
      } else if (inputType == "hls" || inputType == "http") {
        std::cerr << "[PipelineBuilder] Auto-detected " << inputType 
                  << " source from INPUT_SRC, using ff_src" << std::endl;
        actualNodeType = "ff_src";
        params["uri"] = inputSrc;
        size_t fileSrcPos = nodeName.find("file_src");
        if (fileSrcPos != std::string::npos) {
          nodeName.replace(fileSrcPos, 8, "ff_src");
          std::cerr << "[PipelineBuilder] Updated node name to reflect FFmpeg source: '"
                    << nodeName << "'" << std::endl;
        }
        return actualNodeType;
      } else {
        // If inputType == "file" or "udp", use file_src (default)
        std::cerr << "[PipelineBuilder] Using file_src for INPUT_SRC: '"
                  << inputSrc << "' (detected type: " << inputType << ")" << std::endl;
        // Set file_path parameter for file_src node
        params["file_path"] = inputSrc;
        return actualNodeType;
      }
    }
  }

  // Priority 1: Check explicit source URL parameters
  auto rtspSrcIt = req.additionalParams.find("RTSP_SRC_URL");
  auto rtspIt = req.additionalParams.find("RTSP_URL"); // Backward compatibility
  auto rtmpSrcIt = req.additionalParams.find("RTMP_SRC_URL");
  auto rtmpIt = req.additionalParams.find("RTMP_URL"); // Backward compatibility (for input)
  auto hlsIt = req.additionalParams.find("HLS_URL");
  auto httpIt = req.additionalParams.find("HTTP_URL");

  // Validate and trim URLs before checking
  // RTSP: Check RTSP_SRC_URL first, then RTSP_URL for backward compatibility
  std::string rtspUrl = "";
  if (rtspSrcIt != req.additionalParams.end() && !rtspSrcIt->second.empty()) {
    rtspUrl = rtspSrcIt->second;
  } else if (rtspIt != req.additionalParams.end() && !rtspIt->second.empty()) {
    rtspUrl = rtspIt->second; // Backward compatibility: RTSP_URL can be used for input
  }

  // RTMP: Check RTMP_SRC_URL first, then RTMP_URL (but only if not used for output)
  std::string rtmpUrl = "";
  if (rtmpSrcIt != req.additionalParams.end() && !rtmpSrcIt->second.empty()) {
    // RTMP_SRC_URL always means input (highest priority)
    rtmpUrl = rtmpSrcIt->second;
  } else if (rtmpIt != req.additionalParams.end() && !rtmpIt->second.empty()) {
    // RTMP_URL can be used for input OR output
    // Priority: Check RTMP_DES_URL first - if provided, RTMP_URL should be for output
    auto rtmpDesIt = req.additionalParams.find("RTMP_DES_URL");
    bool rtmpUrlUsedForOutput = false;
    if (rtmpDesIt != req.additionalParams.end() && !rtmpDesIt->second.empty()) {
      // RTMP_DES_URL is provided, so RTMP_URL should be for output, not input
      rtmpUrlUsedForOutput = true;
      std::cerr << "[PipelineBuilder] RTMP_DES_URL detected, RTMP_URL will "
                   "be used for output (rtmp_des), not input"
                << std::endl;
    } else {
      // Check if FILE_PATH is provided - if yes, RTMP_URL is likely for output
      auto filePathIt = req.additionalParams.find("FILE_PATH");
      if (filePathIt != req.additionalParams.end() && !filePathIt->second.empty()) {
        std::string filePath = filePathIt->second;
        filePath.erase(0, filePath.find_first_not_of(" \t\n\r"));
        filePath.erase(filePath.find_last_not_of(" \t\n\r") + 1);
        // If FILE_PATH is not an RTMP URL, then RTMP_URL is likely for output
        if (!filePath.empty() && PipelineBuilderSourceNodes::detectInputType(filePath) != "rtmp") {
          rtmpUrlUsedForOutput = true;
          std::cerr << "[PipelineBuilder] FILE_PATH detected (not RTMP), "
                       "RTMP_URL will be used for output (rtmp_des), not input"
                    << std::endl;
        }
      }
    }
    // If RTMP_URL is not used for output, use it for input (backward compatibility)
    if (!rtmpUrlUsedForOutput) {
      rtmpUrl = rtmpIt->second;
    }
  }

  std::string hlsUrl = (hlsIt != req.additionalParams.end() && !hlsIt->second.empty()) ? hlsIt->second : "";
  std::string httpUrl = (httpIt != req.additionalParams.end() && !httpIt->second.empty()) ? httpIt->second : "";

  // Trim whitespace
  if (!rtspUrl.empty()) {
    rtspUrl.erase(0, rtspUrl.find_first_not_of(" \t\n\r"));
    rtspUrl.erase(rtspUrl.find_last_not_of(" \t\n\r") + 1);
  }
  if (!rtmpUrl.empty()) {
    rtmpUrl.erase(0, rtmpUrl.find_first_not_of(" \t\n\r"));
    rtmpUrl.erase(rtmpUrl.find_last_not_of(" \t\n\r") + 1);
  }
  if (!hlsUrl.empty()) {
    hlsUrl.erase(0, hlsUrl.find_first_not_of(" \t\n\r"));
    hlsUrl.erase(hlsUrl.find_last_not_of(" \t\n\r") + 1);
  }
  if (!httpUrl.empty()) {
    httpUrl.erase(0, httpUrl.find_first_not_of(" \t\n\r"));
    httpUrl.erase(httpUrl.find_last_not_of(" \t\n\r") + 1);
  }

  if (!rtspUrl.empty()) {
    // Check if RTMP_URL also provided (potential conflict)
    if (!rtmpUrl.empty()) {
      std::cerr << "[PipelineBuilder] ⚠ WARNING: Both RTSP_URL and RTMP_URL are provided." << std::endl;
      std::cerr << "[PipelineBuilder] ⚠ Using RTSP_URL (priority). RTMP_URL will be ignored for input." << std::endl;
    }
    // Check if FILE_PATH also contains RTSP URL (potential conflict)
    auto filePathIt = req.additionalParams.find("FILE_PATH");
    if (filePathIt != req.additionalParams.end() && !filePathIt->second.empty()) {
      std::string filePath = filePathIt->second;
      filePath.erase(0, filePath.find_first_not_of(" \t\n\r"));
      filePath.erase(filePath.find_last_not_of(" \t\n\r") + 1);
      if (!filePath.empty() && PipelineBuilderSourceNodes::detectInputType(filePath) == "rtsp") {
        std::cerr << "[PipelineBuilder] ⚠ WARNING: Both RTSP_SRC_URL and "
                     "FILE_PATH (with RTSP URL) are provided."
                  << std::endl;
        std::cerr << "[PipelineBuilder] ⚠ Using RTSP_SRC_URL (priority). "
                     "FILE_PATH will be ignored."
                  << std::endl;
      }
    }
    std::cerr << "[PipelineBuilder] Auto-detected RTSP source from "
                 "RTSP_SRC_URL parameter, overriding file_src"
              << std::endl;
    actualNodeType = "rtsp_src";
    params["rtsp_url"] = rtspUrl;
    size_t fileSrcPos = nodeName.find("file_src");
    if (fileSrcPos != std::string::npos) {
      nodeName.replace(fileSrcPos, 8, "rtsp_src");
      std::cerr << "[PipelineBuilder] Updated node name to reflect RTSP source: '"
                << nodeName << "'" << std::endl;
    }
  } else if (!rtmpUrl.empty()) {
    // Check if FILE_PATH also contains RTMP URL (potential conflict)
    auto filePathIt = req.additionalParams.find("FILE_PATH");
    if (filePathIt != req.additionalParams.end() && !filePathIt->second.empty()) {
      std::string filePath = filePathIt->second;
      filePath.erase(0, filePath.find_first_not_of(" \t\n\r"));
      filePath.erase(filePath.find_last_not_of(" \t\n\r") + 1);
      if (!filePath.empty() && PipelineBuilderSourceNodes::detectInputType(filePath) == "rtmp") {
        std::cerr << "[PipelineBuilder] ⚠ WARNING: Both RTMP_SRC_URL and "
                     "FILE_PATH (with RTMP URL) are provided."
                  << std::endl;
        std::cerr << "[PipelineBuilder] ⚠ Using RTMP_SRC_URL (priority). "
                     "FILE_PATH will be ignored."
                  << std::endl;
      }
    }
    std::cerr << "[PipelineBuilder] Auto-detected RTMP source from "
                 "RTMP_SRC_URL parameter, overriding file_src"
              << std::endl;
    actualNodeType = "rtmp_src";
    params["rtmp_url"] = rtmpUrl;
    size_t fileSrcPos = nodeName.find("file_src");
    if (fileSrcPos != std::string::npos) {
      nodeName.replace(fileSrcPos, 8, "rtmp_src");
      std::cerr << "[PipelineBuilder] Updated node name to reflect RTMP source: '"
                << nodeName << "'" << std::endl;
    }
  } else if (!hlsUrl.empty()) {
    // Check if FILE_PATH also contains HLS URL (potential conflict)
    auto filePathIt = req.additionalParams.find("FILE_PATH");
    if (filePathIt != req.additionalParams.end() && !filePathIt->second.empty()) {
      std::string filePath = filePathIt->second;
      filePath.erase(0, filePath.find_first_not_of(" \t\n\r"));
      filePath.erase(filePath.find_last_not_of(" \t\n\r") + 1);
      if (!filePath.empty() && (PipelineBuilderSourceNodes::detectInputType(filePath) == "hls" ||
                                PipelineBuilderSourceNodes::detectInputType(filePath) == "http")) {
        std::cerr << "[PipelineBuilder] ⚠ WARNING: Both HLS_URL and "
                     "FILE_PATH (with HLS/HTTP URL) are provided."
                  << std::endl;
        std::cerr << "[PipelineBuilder] ⚠ Using HLS_URL (priority). "
                     "FILE_PATH will be ignored."
                  << std::endl;
      }
    }
    std::cerr << "[PipelineBuilder] Auto-detected HLS source from HLS_URL "
                 "parameter, using ff_src"
              << std::endl;
    actualNodeType = "ff_src";
    params["uri"] = hlsUrl;
    size_t fileSrcPos = nodeName.find("file_src");
    if (fileSrcPos != std::string::npos) {
      nodeName.replace(fileSrcPos, 8, "ff_src");
      std::cerr << "[PipelineBuilder] Updated node name to reflect FFmpeg source: '"
                << nodeName << "'" << std::endl;
    }
  } else if (!httpUrl.empty()) {
    // Check if FILE_PATH also contains HTTP URL (potential conflict)
    auto filePathIt = req.additionalParams.find("FILE_PATH");
    if (filePathIt != req.additionalParams.end() && !filePathIt->second.empty()) {
      std::string filePath = filePathIt->second;
      filePath.erase(0, filePath.find_first_not_of(" \t\n\r"));
      filePath.erase(filePath.find_last_not_of(" \t\n\r") + 1);
      if (!filePath.empty() && (PipelineBuilderSourceNodes::detectInputType(filePath) == "hls" ||
                                PipelineBuilderSourceNodes::detectInputType(filePath) == "http")) {
        std::cerr << "[PipelineBuilder] ⚠ WARNING: Both HTTP_URL and "
                     "FILE_PATH (with HLS/HTTP URL) are provided."
                  << std::endl;
        std::cerr << "[PipelineBuilder] ⚠ Using HTTP_URL (priority). "
                     "FILE_PATH will be ignored."
                  << std::endl;
      }
    }
    std::cerr << "[PipelineBuilder] Auto-detected HTTP source from "
                 "HTTP_URL parameter, using ff_src"
              << std::endl;
    actualNodeType = "ff_src";
    params["uri"] = httpUrl;
    size_t fileSrcPos = nodeName.find("file_src");
    if (fileSrcPos != std::string::npos) {
      nodeName.replace(fileSrcPos, 8, "ff_src");
      std::cerr << "[PipelineBuilder] Updated node name to reflect FFmpeg source: '"
                << nodeName << "'" << std::endl;
    }
  } else {
    // Priority 2: Auto-detect from FILE_PATH
    std::string filePath = params.count("file_path") ? params.at("file_path") : "";
    if (filePath.empty()) {
      auto filePathIt = req.additionalParams.find("FILE_PATH");
      if (filePathIt != req.additionalParams.end() && !filePathIt->second.empty()) {
        filePath = filePathIt->second;
      }
    }

    // Trim whitespace from filePath
    if (!filePath.empty()) {
      filePath.erase(0, filePath.find_first_not_of(" \t\n\r"));
      filePath.erase(filePath.find_last_not_of(" \t\n\r") + 1);
    }

    if (!filePath.empty()) {
      std::string inputType = PipelineBuilderSourceNodes::detectInputType(filePath);
      if (inputType == "rtsp") {
        std::cerr << "[PipelineBuilder] Auto-detected RTSP source from "
                     "FILE_PATH: '"
                  << filePath << "'" << std::endl;
        actualNodeType = "rtsp_src";
        params["rtsp_url"] = filePath;
        size_t fileSrcPos = nodeName.find("file_src");
        if (fileSrcPos != std::string::npos) {
          nodeName.replace(fileSrcPos, 8, "rtsp_src");
        }
      } else if (inputType == "rtmp") {
        std::cerr << "[PipelineBuilder] Auto-detected RTMP source from "
                     "FILE_PATH: '"
                  << filePath << "'" << std::endl;
        actualNodeType = "rtmp_src";
        params["rtmp_url"] = filePath;
        size_t fileSrcPos = nodeName.find("file_src");
        if (fileSrcPos != std::string::npos) {
          nodeName.replace(fileSrcPos, 8, "rtmp_src");
        }
      } else if (inputType == "hls" || inputType == "http") {
        std::cerr << "[PipelineBuilder] Auto-detected " << inputType
                  << " source from FILE_PATH: '" << filePath
                  << "', using ff_src" << std::endl;
        actualNodeType = "ff_src";
        params["uri"] = filePath;
        size_t fileSrcPos = nodeName.find("file_src");
        if (fileSrcPos != std::string::npos) {
          nodeName.replace(fileSrcPos, 8, "ff_src");
        }
      } else {
        // If inputType == "file" or "udp", keep as file_src (default)
        std::cerr << "[PipelineBuilder] Using file_src for FILE_PATH: '"
                  << filePath << "' (detected type: " << inputType << ")"
                  << std::endl;
      }
    }
  }
  
  return actualNodeType;
}

// ========== Helper Methods for buildPipeline() ==========

void PipelineBuilder::loadSecuRTData(const SolutionConfig &solution,
                                      CreateInstanceRequest &req,
                                      const std::string &instanceId) {
  if (solution.solutionId != "securt") {
    return;
  }

  std::cerr << "[PipelineBuilder] SecuRT solution detected - loading areas and lines"
            << std::endl;

  // Load lines from SecuRT Line Manager and convert to CrossingLines format
  if (line_manager_) {
    std::string crossingLinesJson =
        SecuRTPipelineIntegration::convertLinesToCrossingLinesFormat(
            line_manager_, instanceId);
    if (!crossingLinesJson.empty()) {
      // Add to additionalParams if not already set (API takes priority)
      if (req.additionalParams.find("CrossingLines") ==
          req.additionalParams.end()) {
        req.additionalParams["CrossingLines"] = crossingLinesJson;
        std::cerr << "[PipelineBuilder] ✓ Loaded " << crossingLinesJson.length()
                  << " bytes of lines data from SecuRT Line Manager"
                  << std::endl;
      } else {
        std::cerr << "[PipelineBuilder] NOTE: CrossingLines already set in "
                     "request, keeping API value"
                  << std::endl;
      }
    } else {
      std::cerr << "[PipelineBuilder] No lines found in SecuRT Line Manager"
                << std::endl;
    }
  } else {
    std::cerr << "[PipelineBuilder] WARNING: Line Manager not set for SecuRT"
              << std::endl;
  }

  // Load areas from Area Manager and convert to StopZones format for ba_stop node
  if (area_manager_) {
    // Convert areas to StopZones format (for ba_stop node processing)
    std::string stopZonesJson =
        SecuRTPipelineIntegration::convertAreasToStopZonesFormat(area_manager_,
                                                                 instanceId);
    if (!stopZonesJson.empty()) {
      // Store StopZones in additionalParams for ba_stop node
      req.additionalParams["StopZones"] = stopZonesJson;
      std::cerr << "[PipelineBuilder] ✓ Loaded " << stopZonesJson.length()
                << " bytes of areas data from SecuRT Area Manager (converted to StopZones format)"
                << std::endl;
      std::cerr << "[PipelineBuilder] Areas will be processed by ba_stop node in pipeline"
                << std::endl;
    } else {
      std::cerr << "[PipelineBuilder] No areas found in SecuRT Area Manager"
                << std::endl;
    }

    // Convert loitering areas to LoiteringAreas format for ba_loitering node
    std::string loiteringAreasJson =
        SecuRTPipelineIntegration::convertAreasToLoiteringFormat(area_manager_,
                                                                 instanceId);
    if (!loiteringAreasJson.empty()) {
      // Store LoiteringAreas in additionalParams for ba_loitering node
      req.additionalParams["LoiteringAreas"] = loiteringAreasJson;
      
      // Also set LOITERING_AREAS_JSON for ba_loitering solution placeholder substitution
      // This is needed when using ba_loitering solution with ${LOITERING_AREAS_JSON} placeholder
      req.additionalParams["LOITERING_AREAS_JSON"] = loiteringAreasJson;
      
      // Extract ALARM_SECONDS from loitering areas (use max seconds value from all areas)
      // This is needed for ba_loitering solution with ${ALARM_SECONDS} placeholder
      try {
        Json::Reader reader;
        Json::Value parsedAreas;
        if (reader.parse(loiteringAreasJson, parsedAreas) && parsedAreas.isArray() && parsedAreas.size() > 0) {
          double maxSeconds = 5.0; // Default
          for (Json::ArrayIndex i = 0; i < parsedAreas.size(); ++i) {
            if (parsedAreas[i].isMember("seconds") && parsedAreas[i]["seconds"].isNumeric()) {
              double seconds = parsedAreas[i]["seconds"].asDouble();
              if (seconds > maxSeconds) {
                maxSeconds = seconds;
              }
            }
          }
          req.additionalParams["ALARM_SECONDS"] = std::to_string(static_cast<int>(maxSeconds));
          std::cerr << "[PipelineBuilder] ✓ Set ALARM_SECONDS to " << static_cast<int>(maxSeconds)
                    << " (from loitering areas)" << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilder] WARNING: Failed to parse loitering areas for ALARM_SECONDS: "
                  << e.what() << std::endl;
        // Set default ALARM_SECONDS if parsing fails
        req.additionalParams["ALARM_SECONDS"] = "5";
      }
      
      std::cerr << "[PipelineBuilder] ✓ Loaded " << loiteringAreasJson.length()
                << " bytes of loitering areas data from SecuRT Area Manager (converted to LoiteringAreas format)"
                << std::endl;
      std::cerr << "[PipelineBuilder] Loitering areas will be processed by ba_loitering node in pipeline"
                << std::endl;
    } else {
      std::cerr << "[PipelineBuilder] No loitering areas found in SecuRT Area Manager"
                << std::endl;
    }

    // Also store original areas JSON for reference
    std::string areasJson =
        SecuRTPipelineIntegration::convertAreasToJsonFormat(area_manager_,
                                                            instanceId);
    if (!areasJson.empty()) {
      req.additionalParams["SecuRTAreas"] = areasJson;
    }
  } else {
    std::cerr << "[PipelineBuilder] WARNING: Area Manager not set for SecuRT"
              << std::endl;
  }
}

std::tuple<bool, bool, std::string, std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>>>
PipelineBuilder::handleMultipleSources(const CreateInstanceRequest &req,
                                        const std::string &instanceId,
                                        std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes,
                                        std::vector<std::string> &nodeTypes) {
  std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> multipleSourceNodes;
  bool hasMultipleSources = false;
  bool hasCustomSourceNodes = false;
  std::string multipleSourceType = "";

  // Check FILE_PATHS first
  auto filePathsIt = req.additionalParams.find("FILE_PATHS");
  if (filePathsIt != req.additionalParams.end() && !filePathsIt->second.empty()) {
    try {
      Json::Value filePathsJson;
      Json::Reader reader;
      if (reader.parse(filePathsIt->second, filePathsJson) && filePathsJson.isArray() && filePathsJson.size() >= 1) {
        hasMultipleSources = (filePathsJson.size() > 1);
        hasCustomSourceNodes = true;
        multipleSourceType = "file_src";
        std::cerr << "[PipelineBuilder] Detected FILE_PATHS array with " << filePathsJson.size() << " video source(s)" << std::endl;
        
        // Create multiple file_src nodes
        for (Json::ArrayIndex i = 0; i < filePathsJson.size(); ++i) {
          std::string filePath;
          int channel = static_cast<int>(i);
          float resizeRatio = 0.4f; // Default
          
          if (filePathsJson[i].isString()) {
            filePath = filePathsJson[i].asString();
          } else if (filePathsJson[i].isObject()) {
            if (filePathsJson[i].isMember("file_path")) {
              filePath = filePathsJson[i]["file_path"].asString();
            }
            if (filePathsJson[i].isMember("channel")) {
              channel = filePathsJson[i]["channel"].asInt();
            }
            if (filePathsJson[i].isMember("resize_ratio")) {
              resizeRatio = filePathsJson[i]["resize_ratio"].asFloat();
            }
          }
          
          if (!filePath.empty()) {
            std::string inputType = PipelineBuilderSourceNodes::detectInputType(filePath);
            
            if (inputType == "rtsp") {
              std::string nodeName = "rtsp_src_" + instanceId + "_" + std::to_string(channel);
              std::cerr << "[PipelineBuilder] Auto-detected RTSP source from FILE_PATHS[" << i << "]" << std::endl;
              std::cerr << "[PipelineBuilder] Creating RTSP source node " << (i + 1) << "/" << filePathsJson.size() 
                        << ": channel=" << channel << ", url='" << filePath << "', resize_ratio=" << resizeRatio << std::endl;
              
              std::string gstDecoderName = "avdec_h264";
              int skipInterval = 0;
              std::string codecType = "h264";
              
              if (filePathsJson[i].isObject()) {
                if (filePathsJson[i].isMember("gst_decoder_name")) {
                  gstDecoderName = filePathsJson[i]["gst_decoder_name"].asString();
                }
                if (filePathsJson[i].isMember("skip_interval")) {
                  skipInterval = filePathsJson[i]["skip_interval"].asInt();
                }
                if (filePathsJson[i].isMember("codec_type")) {
                  codecType = filePathsJson[i]["codec_type"].asString();
                }
              }
              
              auto rtspSrcNode = std::make_shared<cvedix_nodes::cvedix_rtsp_src_node>(
                  nodeName, channel, filePath, resizeRatio, gstDecoderName, skipInterval, codecType);
              multipleSourceNodes.push_back(rtspSrcNode);
              nodes.push_back(rtspSrcNode);
              nodeTypes.push_back("rtsp_src");
            } else if (inputType == "rtmp") {
              std::string nodeName = "rtmp_src_" + instanceId + "_" + std::to_string(channel);
              std::cerr << "[PipelineBuilder] Auto-detected RTMP source from FILE_PATHS[" << i << "]" << std::endl;
              std::cerr << "[PipelineBuilder] Creating RTMP source node " << (i + 1) << "/" << filePathsJson.size() 
                        << ": channel=" << channel << ", url='" << filePath << "', resize_ratio=" << resizeRatio << std::endl;
              
              std::string gstDecoderName = "avdec_h264";
              int skipInterval = 0;
              
              if (filePathsJson[i].isObject()) {
                if (filePathsJson[i].isMember("gst_decoder_name")) {
                  gstDecoderName = filePathsJson[i]["gst_decoder_name"].asString();
                }
                if (filePathsJson[i].isMember("skip_interval")) {
                  skipInterval = filePathsJson[i]["skip_interval"].asInt();
                }
              }
              
              auto rtmpSrcNode = std::make_shared<cvedix_nodes::cvedix_rtmp_src_node>(
                  nodeName, channel, filePath, resizeRatio, gstDecoderName, skipInterval);
              multipleSourceNodes.push_back(rtmpSrcNode);
              nodes.push_back(rtmpSrcNode);
              nodeTypes.push_back("rtmp_src");
            } else {
              std::string nodeName = "file_src_" + instanceId + "_" + std::to_string(channel);
              std::cerr << "[PipelineBuilder] Creating file source node " << (i + 1) << "/" << filePathsJson.size() 
                        << ": channel=" << channel << ", path='" << filePath << "', resize_ratio=" << resizeRatio << std::endl;
              
              auto fileSrcNode = std::make_shared<cvedix_nodes::cvedix_file_src_node>(
                  nodeName, channel, filePath, resizeRatio);
              multipleSourceNodes.push_back(fileSrcNode);
              nodes.push_back(fileSrcNode);
              nodeTypes.push_back("file_src");
            }
          }
        }
        
        int rtspCount = 0, rtmpCount = 0, fileCount = 0;
        for (const auto &type : nodeTypes) {
          if (type == "rtsp_src") rtspCount++;
          else if (type == "rtmp_src") rtmpCount++;
          else if (type == "file_src") fileCount++;
        }
        
        std::cerr << "[PipelineBuilder] ✓ Created " << multipleSourceNodes.size() << " source node(s) for multi-channel processing:";
        if (rtspCount > 0) std::cerr << " " << rtspCount << " RTSP";
        if (rtmpCount > 0) std::cerr << " " << rtmpCount << " RTMP";
        if (fileCount > 0) std::cerr << " " << fileCount << " file";
        std::cerr << std::endl;
      }
    } catch (const std::exception &e) {
      std::cerr << "[PipelineBuilder] Warning: Failed to parse FILE_PATHS: " << e.what() 
                << ", falling back to single source" << std::endl;
      hasMultipleSources = false;
    }
  }
  
  // Check RTSP_URLS if FILE_PATHS not found
  if (!hasMultipleSources) {
    auto rtspUrlsIt = req.additionalParams.find("RTSP_URLS");
    if (rtspUrlsIt != req.additionalParams.end() && !rtspUrlsIt->second.empty()) {
      try {
        Json::Value rtspUrlsJson;
        Json::Reader reader;
        if (reader.parse(rtspUrlsIt->second, rtspUrlsJson) && rtspUrlsJson.isArray() && rtspUrlsJson.size() >= 1) {
          hasMultipleSources = (rtspUrlsJson.size() > 1);
          hasCustomSourceNodes = true;
          multipleSourceType = "rtsp_src";
          std::cerr << "[PipelineBuilder] Detected RTSP_URLS array with " << rtspUrlsJson.size() << " RTSP stream(s)" << std::endl;
          
          for (Json::ArrayIndex i = 0; i < rtspUrlsJson.size(); ++i) {
            std::string rtspUrl;
            int channel = static_cast<int>(i);
            float resizeRatio = 0.6f;
            std::string gstDecoderName = "avdec_h264";
            int skipInterval = 0;
            std::string codecType = "h264";
            
            if (rtspUrlsJson[i].isString()) {
              rtspUrl = rtspUrlsJson[i].asString();
            } else if (rtspUrlsJson[i].isObject()) {
              if (rtspUrlsJson[i].isMember("rtsp_url")) {
                rtspUrl = rtspUrlsJson[i]["rtsp_url"].asString();
              } else if (rtspUrlsJson[i].isMember("url")) {
                rtspUrl = rtspUrlsJson[i]["url"].asString();
              }
              if (rtspUrlsJson[i].isMember("channel")) {
                channel = rtspUrlsJson[i]["channel"].asInt();
              }
              if (rtspUrlsJson[i].isMember("resize_ratio")) {
                resizeRatio = rtspUrlsJson[i]["resize_ratio"].asFloat();
              }
              if (rtspUrlsJson[i].isMember("gst_decoder_name")) {
                gstDecoderName = rtspUrlsJson[i]["gst_decoder_name"].asString();
              }
              if (rtspUrlsJson[i].isMember("skip_interval")) {
                skipInterval = rtspUrlsJson[i]["skip_interval"].asInt();
              }
              if (rtspUrlsJson[i].isMember("codec_type")) {
                codecType = rtspUrlsJson[i]["codec_type"].asString();
              }
            }
            
            if (!rtspUrl.empty()) {
              std::string nodeName = "rtsp_src_" + instanceId + "_" + std::to_string(channel);
              std::cerr << "[PipelineBuilder] Creating RTSP source node " << (i + 1) << "/" << rtspUrlsJson.size() 
                        << ": channel=" << channel << ", url='" << rtspUrl << "', resize_ratio=" << resizeRatio << std::endl;
              
              auto rtspSrcNode = std::make_shared<cvedix_nodes::cvedix_rtsp_src_node>(
                  nodeName, channel, rtspUrl, resizeRatio, gstDecoderName, skipInterval, codecType);
              multipleSourceNodes.push_back(rtspSrcNode);
              nodes.push_back(rtspSrcNode);
              nodeTypes.push_back("rtsp_src");
            }
          }
          
          std::cerr << "[PipelineBuilder] ✓ Created " << multipleSourceNodes.size() << " RTSP source nodes for multi-channel processing" << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilder] Warning: Failed to parse RTSP_URLS: " << e.what() 
                  << ", falling back to single source" << std::endl;
        hasMultipleSources = false;
      }
    }
  }

  return std::make_tuple(hasMultipleSources, hasCustomSourceNodes, multipleSourceType, multipleSourceNodes);
}

// Note: createRTSPSourceNode has been moved to PipelineBuilderSourceNodes

// Note: mapDetectionSensitivity, getRTSPUrl, getRTMPUrl, getFilePath, 
// resolveModelPath, resolveModelByName, and listAvailableModels have been 
// moved to utility classes:
// - PipelineBuilderModelResolver
// - PipelineBuilderRequestUtils






















// ========== TensorRT YOLOv8 Nodes Implementation ==========

#ifdef CVEDIX_WITH_TRT








// ========== TensorRT Vehicle Nodes Implementation ==========














#endif // CVEDIX_WITH_TRT

// ========== RKNN Nodes Implementation ==========

#ifdef CVEDIX_WITH_RKNN



#endif // CVEDIX_WITH_RKNN

// ========== Other Inference Nodes Implementation ==========











// Note: createFeatureEncoderNode is disabled because
// cvedix_feature_encoder_node is abstract Use createSFaceEncoderNode (for
// "sface_feature_encoder") or createTRTVehicleFeatureEncoderNode (for
// "trt_vehicle_feature_encoder") instead
/*
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilder::createFeatureEncoderNode( const std::string& nodeName, const
std::map<std::string, std::string>& params, const CreateInstanceRequest& req) {

    try {
        std::string modelPath = params.count("model_path") ?
params.at("model_path") : "";

        if (modelPath.empty()) {
            auto it = req.additionalParams.find("MODEL_PATH");
            if (it != req.additionalParams.end() && !it->second.empty()) {
                modelPath = it->second;
            }
        }

        if (nodeName.empty() || modelPath.empty()) {
            throw std::invalid_argument("Node name and model path are
required");
        }

        std::cerr << "[PipelineBuilder] Creating feature encoder node:" <<
std::endl; std::cerr << "  Name: '" << nodeName << "'" << std::endl; std::cerr
<< "  Model path: '" << modelPath << "'" << std::endl;

        auto node = std::make_shared<cvedix_nodes::cvedix_feature_encoder_node>(
            nodeName,
            modelPath
        );

        std::cerr << "[PipelineBuilder] ✓ Feature encoder node created
successfully" << std::endl; return node; } catch (const std::exception& e) {
        std::cerr << "[PipelineBuilder] Exception in createFeatureEncoderNode: "
<< e.what() << std::endl; throw;
    }
}
*/



#ifdef CVEDIX_WITH_PADDLE

#endif



// ========== New Inference Nodes Implementation ==========









#ifdef CVEDIX_WITH_RKNN

#endif // CVEDIX_WITH_RKNN

#ifdef CVEDIX_WITH_TRT

#endif // CVEDIX_WITH_TRT

// ========== Helper Functions for Crossline MQTT Broker ==========
#ifdef CVEDIX_WITH_MQTT
static std::string get_current_timestamp() {
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
  return std::to_string(ms);
}

static std::string get_current_date_iso() {
  auto now = std::time(nullptr);
  std::tm tm;
  gmtime_r(&now, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf);
}

static std::string get_current_date_system() {
  auto now = std::time(nullptr);
  std::tm tm;
  localtime_r(&now, &tm);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y", &tm);
  return std::string(buf);
}
#endif // CVEDIX_WITH_MQTT

// Event format structures for crossline MQTT
namespace crossline_event_format {
struct normalized_bbox {
  double x, y, width, height;

  template <typename Archive> void serialize(Archive &archive) {
    archive(cereal::make_nvp("x", x), cereal::make_nvp("y", y),
            cereal::make_nvp("width", width),
            cereal::make_nvp("height", height));
  }
};

struct track_info {
  normalized_bbox bbox;
  std::string class_label;
  std::string external_id;
  std::string id;
  int last_seen;
  int source_tracker_track_id;

  template <typename Archive> void serialize(Archive &archive) {
    archive(
        cereal::make_nvp("bbox", bbox),
        cereal::make_nvp("class_label", class_label),
        cereal::make_nvp("external_id", external_id),
        cereal::make_nvp("id", id), cereal::make_nvp("last_seen", last_seen),
        cereal::make_nvp("source_tracker_track_id", source_tracker_track_id));
  }
};

struct best_thumbnail {
  double confidence;
  std::string image;
  std::string instance_id;
  std::string label;
  std::string system_date;
  std::vector<track_info> tracks;

  template <typename Archive> void serialize(Archive &archive) {
    archive(cereal::make_nvp("confidence", confidence),
            cereal::make_nvp("image", image),
            cereal::make_nvp("instance_id", instance_id),
            cereal::make_nvp("label", label),
            cereal::make_nvp("system_date", system_date),
            cereal::make_nvp("tracks", tracks));
  }
};

struct line_count {
  std::string line_id;
  std::string line_name;
  int count;
  template <typename Archive> void serialize(Archive &archive) {
    archive(cereal::make_nvp("line_id", line_id),
            cereal::make_nvp("line_name", line_name),
            cereal::make_nvp("count", count));
  }
};

struct event {
  best_thumbnail best_thumbnail_obj;
  std::string type;
  std::string zone_id;
  std::string zone_name;
  std::string line_id;

  template <typename Archive> void serialize(Archive &archive) {
    archive(cereal::make_nvp("best_thumbnail", best_thumbnail_obj),
            cereal::make_nvp("type", type),
            cereal::make_nvp("zone_id", zone_id),
            cereal::make_nvp("zone_name", zone_name),
            cereal::make_nvp("line_id", line_id));
  }
};

struct event_message {
  std::vector<event> events;
  int frame_id;
  double frame_time;
  std::string system_date;
  std::string system_timestamp;
  std::vector<line_count> line_counts;
  std::string instance_id;
  std::string instance_name;
  /** Timestamp (ms, system_clock) when crossline was detected / message built. Used for detection-to-MQTT latency. */
  int64_t detection_ts_ms = 0;

  template <typename Archive> void serialize(Archive &archive) {
    archive(cereal::make_nvp("events", events),
            cereal::make_nvp("frame_id", frame_id),
            cereal::make_nvp("frame_time", frame_time),
            cereal::make_nvp("system_date", system_date),
            cereal::make_nvp("system_timestamp", system_timestamp),
            cereal::make_nvp("line_counts", line_counts),
            cereal::make_nvp("instance_id", instance_id),
            cereal::make_nvp("instance_name", instance_name),
            cereal::make_nvp("detection_ts_ms", detection_ts_ms));
  }
};
} // namespace crossline_event_format

// Custom Broker Node for Crossline MQTT
class cvedix_json_crossline_mqtt_broker_node
    : public cvedix_nodes::cvedix_json_enhanced_console_broker_node {
private:
  std::function<void(const std::string &)> mqtt_publisher_;
  std::string instance_id_;   // UUID thực sự của instance
  std::string instance_name_; // Tên instance (từ req.name)
  std::string zone_id_;
  std::string zone_name_;
  // Track counts per line: line_id -> count
  std::map<std::string, int> line_counts_;
  // Track line names: line_id -> line_name
  std::map<std::string, std::string> line_names_;
  // Track line info: channel -> (line_id, line_name)
  std::map<int, std::pair<std::string, std::string>> channel_to_line_info_;
  // Mutex for thread-safe access to counters
  std::mutex counts_mutex_;

  virtual void
  format_msg(const std::shared_ptr<cvedix_objects::cvedix_frame_meta> &meta,
             std::string &msg) override {
    try {
      if (meta->ba_results.empty()) {
        msg = "";
        return;
      }
      // Ghi nhận thời điểm ngay khi có kết quả crossline (đầu format_msg) để đo
      // chính xác toàn bộ thời gian từ detect → gửi MQTT (build message + serialize + publish).
      const int64_t detection_ts_ms_at_start = static_cast<int64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count());

      crossline_event_format::event_message event_msg;
      bool has_events = false;

      double frame_width = static_cast<double>(meta->frame.cols);
      double frame_height = static_cast<double>(meta->frame.rows);

      // Track crossline event index to map to line channel
      // Note: This assumes ba_results order matches line order, which may not
      // always be accurate but is the best we can do without line index in
      // ba_res
      int crossline_event_index = 0;
      for (const auto &ba_res : meta->ba_results) {
        // Check if it's a crossline event
        if (ba_res->type == cvedix_objects::cvedix_ba_type::CROSSLINE) {
          // Try to determine line_id from channel mapping
          // Use crossline_event_index as channel (assuming ba_results order
          // matches line order)
          int channel = crossline_event_index;

          // Iterate over all targets involved in this crossline event
          for (int track_id : ba_res->involve_target_ids_in_frame) {
            // Find the target object
            std::shared_ptr<cvedix_objects::cvedix_frame_target> target =
                nullptr;
            for (const auto &t : meta->targets) {
              if (t->track_id == track_id) {
                target = t;
                break;
              }
            }

            if (!target)
              continue;

            has_events = true;
            crossline_event_format::event evt;

            // Crop image logic
            std::string thumbnail_image = "";
            try {
              if (!meta->frame.empty()) {
                const cv::Mat &original_frame = meta->frame;

                const float expand_ratio = 0.35f;
                int center_x = target->x + target->width / 2;
                int center_y = target->y + target->height / 2;

                int expanded_width =
                    static_cast<int>(target->width * (1.0f + expand_ratio));
                int expanded_height =
                    static_cast<int>(target->height * (1.0f + expand_ratio));

                int x1 = std::max(0, center_x - expanded_width / 2);
                int y1 = std::max(0, center_y - expanded_height / 2);
                int x2 = std::min(original_frame.cols, x1 + expanded_width);
                int y2 = std::min(original_frame.rows, y1 + expanded_height);

                if (x2 > x1 && y2 > y1) {
                  cv::Rect roi(x1, y1, x2 - x1, y2 - y1);
                  cv::Mat cropped = original_frame(roi);

                  if (!cropped.empty()) {
                    cv::Mat resized;
                    cv::resize(cropped, resized, cv::Size(150, 150), 0, 0,
                               cv::INTER_LINEAR);
                    std::vector<uchar> buf;
                    cv::imencode(".jpg", resized, buf);
                    thumbnail_image = base64_encode(buf.data(), buf.size());
                  }
                }
              }
            } catch (...) {
              thumbnail_image = "";
            }

            // Track info
            crossline_event_format::track_info track;
            track.bbox.x = target->x / frame_width;
            track.bbox.y = target->y / frame_height;
            track.bbox.width = target->width / frame_width;
            track.bbox.height = target->height / frame_height;

            track.class_label = target->primary_label.empty()
                                    ? "Object"
                                    : target->primary_label;
            track.external_id = "crossline-event";
            track.id = "Tracker_" + std::to_string(target->track_id);
            track.last_seen = 0;
            track.source_tracker_track_id = target->track_id;

            evt.best_thumbnail_obj.confidence = target->primary_score;
            evt.best_thumbnail_obj.image = thumbnail_image;
            evt.best_thumbnail_obj.instance_id = instance_id_;
            evt.best_thumbnail_obj.label = ba_res->ba_label;
            evt.best_thumbnail_obj.system_date = get_current_date_iso();
            evt.best_thumbnail_obj.tracks.push_back(track);

            evt.type = "crossline";
            evt.zone_id = zone_id_;
            evt.zone_name = zone_name_;

            // Determine line_id from channel mapping or fallback to zone_id
            std::string line_id = zone_id_;
            std::string line_name = zone_name_;

            // Try to get line_id from channel mapping (if CrossingLines config
            // was parsed)
            {
              std::lock_guard<std::mutex> lock(counts_mutex_);
              auto it = channel_to_line_info_.find(channel);
              if (it != channel_to_line_info_.end()) {
                line_id = it->second.first;
                line_name = it->second.second;
              } else {
                // Fallback: use zone_id or generate from channel
                if (line_id.empty() || line_id == "default_zone") {
                  line_id = "line_" + std::to_string(channel + 1);
                  line_name = "Line " + std::to_string(channel + 1);
                }
              }
            }
            evt.line_id = line_id;

            // Increment counter for this line
            {
              std::lock_guard<std::mutex> lock(counts_mutex_);
              line_counts_[line_id]++;
              // Store line name if not already stored
              if (line_names_.find(line_id) == line_names_.end()) {
                line_names_[line_id] = line_name.empty() ? line_id : line_name;
              }
            }

            event_msg.events.push_back(evt);
          }
          // Increment crossline event index only for crossline events
          crossline_event_index++;
        }
      }

      if (!has_events) {
        msg = "";
        return;
      }

      event_msg.frame_id = meta->frame_index;
      event_msg.frame_time =
          meta->frame_index * 1000.0 / (meta->fps > 0 ? meta->fps : 30.0);
      event_msg.system_date = get_current_date_system();
      event_msg.system_timestamp = get_current_timestamp();
      event_msg.instance_id = instance_id_;     // UUID thực sự
      event_msg.instance_name = instance_name_; // Tên instance
      event_msg.detection_ts_ms = detection_ts_ms_at_start;

      // Add line counts summary
      {
        std::lock_guard<std::mutex> lock(counts_mutex_);
        for (const auto &pair : line_counts_) {
          crossline_event_format::line_count lc;
          lc.line_id = pair.first;
          lc.line_name = (line_names_.find(pair.first) != line_names_.end())
                             ? line_names_[pair.first]
                             : pair.first;
          lc.count = pair.second;
          event_msg.line_counts.push_back(lc);
        }
      }

      std::stringstream msg_stream;
      {
        cereal::JSONOutputArchive json_archive(msg_stream);
        std::vector<crossline_event_format::event_message> result_array;
        result_array.push_back(event_msg);
        json_archive(result_array);
      }

      // Clean up JSON wrapper
      std::string json_str = msg_stream.str();
      size_t value0_pos = json_str.find("\"value0\"");
      if (value0_pos != std::string::npos) {
        size_t array_start = json_str.find('[', value0_pos);
        if (array_start != std::string::npos) {
          size_t array_end = json_str.rfind(']');
          if (array_end != std::string::npos && array_end > array_start) {
            msg = json_str.substr(array_start, array_end - array_start + 1);
          } else {
            msg = json_str;
          }
        } else {
          msg = json_str;
        }
      } else {
        msg = json_str;
      }

    } catch (...) {
      msg = "";
    }
  }

  virtual void broke_msg(const std::string &msg) override {
    if (!mqtt_publisher_) {
      std::cerr << "[MQTT] ⚠ broke_msg called but mqtt_publisher_ is null"
                << std::endl;
      return;
    }
    if (msg.empty()) {
      std::cerr << "[MQTT] ⚠ broke_msg called with empty message" << std::endl;
      return;
    }
    try {
      const int64_t mqtt_sent_ts_ms = static_cast<int64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count());
      std::string out_msg = msg;
      // Nhẹ: đọc detection_ts_ms từ chuỗi + chèn 2 field, không parse/serialize
      // toàn bộ JSON → tránh ảnh hưởng hiệu năng (payload có thể rất lớn do base64).
      const std::string key = "\"detection_ts_ms\":";
      const size_t key_pos = msg.find(key);
      int64_t detection_ts = 0;
      if (key_pos != std::string::npos) {
        const size_t val_start = key_pos + key.size();
        size_t val_end = val_start;
        while (val_end < msg.size() &&
               (std::isdigit(static_cast<unsigned char>(msg[val_end])) ||
                msg[val_end] == '-')) {
          ++val_end;
        }
        if (val_end > val_start) {
          try {
            detection_ts = std::stoll(msg.substr(val_start, val_end - val_start));
          } catch (...) {
          }
        }
      }
      const int64_t d2m_ms = mqtt_sent_ts_ms - detection_ts;
      const std::string suffix =
          ",\"mqtt_sent_ts_ms\":" + std::to_string(mqtt_sent_ts_ms) +
          ",\"detection_to_mqtt_ms\":" + std::to_string(d2m_ms) + "}";
      const size_t insert_pos = msg.rfind("}]");
      if (insert_pos != std::string::npos) {
        out_msg.reserve(msg.size() + suffix.size());
        out_msg = msg.substr(0, insert_pos + 1) + suffix + msg.substr(insert_pos + 1);
      }
      std::cerr
          << "[MQTT] [broke_msg] Calling mqtt_publisher_ with message length: "
          << out_msg.length() << std::endl;
      mqtt_publisher_(out_msg);
    } catch (const std::exception &e) {
      std::cerr << "[MQTT] ⚠ Exception in mqtt_publisher_: " << e.what()
                << std::endl;
    } catch (...) {
      std::cerr << "[MQTT] ⚠ Unknown exception in mqtt_publisher_" << std::endl;
    }
  }

public:
  cvedix_json_crossline_mqtt_broker_node(
      std::string node_name,
      std::function<void(const std::string &)> mqtt_publisher,
      std::string instance_id, std::string instance_name, std::string zone_id,
      std::string zone_name, const std::string &crossing_lines_json = "")
      : cvedix_nodes::cvedix_json_enhanced_console_broker_node(
            node_name, cvedix_nodes::cvedix_broke_for::NORMAL, 100, 500, false),
        mqtt_publisher_(mqtt_publisher), instance_id_(instance_id),
        instance_name_(instance_name), zone_id_(zone_id),
        zone_name_(zone_name) {
    // Parse CrossingLines config to build channel -> line_id mapping
    if (!crossing_lines_json.empty()) {
      try {
        Json::Reader reader;
        Json::Value parsedLines;
        if (reader.parse(crossing_lines_json, parsedLines) &&
            parsedLines.isArray()) {
          for (Json::ArrayIndex i = 0; i < parsedLines.size(); ++i) {
            const Json::Value &lineObj = parsedLines[i];
            int channel = static_cast<int>(i);

            // Get line_id (use id if available, otherwise generate from index)
            std::string line_id;
            if (lineObj.isMember("id") && lineObj["id"].isString() &&
                !lineObj["id"].asString().empty()) {
              line_id = lineObj["id"].asString();
            } else {
              line_id = "line_" + std::to_string(channel + 1);
            }

            // Get line_name (use name if available, otherwise use line_id)
            std::string line_name;
            if (lineObj.isMember("name") && lineObj["name"].isString() &&
                !lineObj["name"].asString().empty()) {
              line_name = lineObj["name"].asString();
            } else {
              line_name = "Line " + std::to_string(channel + 1);
            }

            channel_to_line_info_[channel] = std::make_pair(line_id, line_name);
          }
        }
      } catch (...) {
        // If parsing fails, channel_to_line_info_ will remain empty
        // and code will fallback to zone_id
      }
    }
  }
};

#ifdef CVEDIX_WITH_MQTT
namespace pipeline_builder_crossline_mqtt {
std::shared_ptr<cvedix_nodes::cvedix_node> createCrosslineMQTTBrokerNode(
    const std::string &nodeName,
    std::function<void(const std::string &)> mqtt_publish_func,
    const std::string &instance_id, const std::string &instance_name,
    const std::string &zone_id, const std::string &zone_name,
    const std::string &crossing_lines_json) {
  return std::make_shared<cvedix_json_crossline_mqtt_broker_node>(
      nodeName, mqtt_publish_func, instance_id, instance_name, zone_id, zone_name,
      crossing_lines_json);
}
}
#endif

// ========== Helper Functions for Jam MQTT Broker ==========
namespace {} // namespace

// Event format structures for Jam MQTT
namespace jam_event_format {
struct normalized_bbox {
  double x, y, width, height;

  template <typename Archive> void serialize(Archive &archive) {
    archive(cereal::make_nvp("x", x), cereal::make_nvp("y", y),
            cereal::make_nvp("width", width),
            cereal::make_nvp("height", height));
  }
};

struct track_info {
  normalized_bbox bbox;
  std::string class_label;
  std::string external_id;
  std::string id;
  int last_seen;
  int source_tracker_track_id;

  template <typename Archive> void serialize(Archive &archive) {
    archive(
        cereal::make_nvp("bbox", bbox),
        cereal::make_nvp("class_label", class_label),
        cereal::make_nvp("external_id", external_id),
        cereal::make_nvp("id", id), cereal::make_nvp("last_seen", last_seen),
        cereal::make_nvp("source_tracker_track_id", source_tracker_track_id));
  }
};

struct best_thumbnail {
  double confidence;
  std::string image;
  std::string instance_id;
  std::string label;
  std::string system_date;
  std::vector<track_info> tracks;

  template <typename Archive> void serialize(Archive &archive) {
    archive(cereal::make_nvp("confidence", confidence),
            cereal::make_nvp("image", image),
            cereal::make_nvp("instance_id", instance_id),
            cereal::make_nvp("label", label),
            cereal::make_nvp("system_date", system_date),
            cereal::make_nvp("tracks", tracks));
  }
};

struct line_count {
  std::string line_id;
  std::string line_name;
  int count;
  template <typename Archive> void serialize(Archive &archive) {
    archive(cereal::make_nvp("line_id", line_id),
            cereal::make_nvp("line_name", line_name),
            cereal::make_nvp("count", count));
  }
};

struct event {
  best_thumbnail best_thumbnail_obj;
  std::string type;
  std::string zone_id;
  std::string zone_name;
  std::string line_id;

  template <typename Archive> void serialize(Archive &archive) {
    archive(cereal::make_nvp("best_thumbnail", best_thumbnail_obj),
            cereal::make_nvp("type", type),
            cereal::make_nvp("zone_id", zone_id),
            cereal::make_nvp("zone_name", zone_name),
            cereal::make_nvp("line_id", line_id));
  }
};

struct event_message {
  std::vector<event> events;
  int frame_id;
  double frame_time;
  std::string system_date;
  std::string system_timestamp;
  std::vector<line_count> line_counts;
  std::string instance_id;
  std::string instance_name;

  template <typename Archive> void serialize(Archive &archive) {
    archive(cereal::make_nvp("events", events),
            cereal::make_nvp("frame_id", frame_id),
            cereal::make_nvp("frame_time", frame_time),
            cereal::make_nvp("system_date", system_date),
            cereal::make_nvp("system_timestamp", system_timestamp),
            cereal::make_nvp("line_counts", line_counts),
            cereal::make_nvp("instance_id", instance_id),
            cereal::make_nvp("instance_name", instance_name));
  }
};
} // namespace jam_event_format

// Custom Broker Node for Jam MQTT
class cvedix_json_jam_mqtt_broker_node
    : public cvedix_nodes::cvedix_json_enhanced_console_broker_node {
private:
  std::function<void(const std::string &)> mqtt_publisher_;
  std::string instance_id_;   // UUID thực sự của instance
  std::string instance_name_; // Tên instance (từ req.name)
  std::string zone_id_;
  std::string zone_name_;
  // Track counts per line: line_id -> count
  std::map<std::string, int> line_counts_;
  // Track line names: line_id -> line_name
  std::map<std::string, std::string> line_names_;
  // Track line info: channel -> (line_id, line_name)
  std::map<int, std::pair<std::string, std::string>> channel_to_line_info_;
  // Mutex for thread-safe access to counters
  std::mutex counts_mutex_;

  virtual void
  format_msg(const std::shared_ptr<cvedix_objects::cvedix_frame_meta> &meta,
             std::string &msg) override {
    try {
      if (meta->ba_results.empty()) {
        msg = "";
        return;
      }

      jam_event_format::event_message event_msg;
      bool has_events = false;

      double frame_width = static_cast<double>(meta->frame.cols);
      double frame_height = static_cast<double>(meta->frame.rows);

      // Track jam event index to map to line channel
      // Note: This assumes ba_results order matches line order, which may not
      // always be accurate but is the best we can do without line index in
      // ba_res
      int jam_event_index = 0;
      for (const auto &ba_res : meta->ba_results) {
        // Check if it's a jam event
        if (ba_res->type == cvedix_objects::cvedix_ba_type::JAM) {
          // Try to determine line_id from channel mapping
          // Use jam_event_index as channel (assuming ba_results order
          // matches line order)
          int channel = jam_event_index;
          // Iterate over all targets involved in this jam event
          for (int track_id : ba_res->involve_target_ids_in_frame) {
            // Find the target object
            std::shared_ptr<cvedix_objects::cvedix_frame_target> target =
                nullptr;
            for (const auto &t : meta->targets) {
              if (t->track_id == track_id) {
                target = t;
                break;
              }
            }

            if (!target)
              continue;

            has_events = true;
            jam_event_format::event evt;

            // Crop image logic
            std::string thumbnail_image = "";
            try {
              if (!meta->frame.empty()) {
                const cv::Mat &original_frame = meta->frame;

                const float expand_ratio = 0.35f;
                int center_x = target->x + target->width / 2;
                int center_y = target->y + target->height / 2;

                int expanded_width =
                    static_cast<int>(target->width * (1.0f + expand_ratio));
                int expanded_height =
                    static_cast<int>(target->height * (1.0f + expand_ratio));

                int x1 = std::max(0, center_x - expanded_width / 2);
                int y1 = std::max(0, center_y - expanded_height / 2);
                int x2 = std::min(original_frame.cols, x1 + expanded_width);
                int y2 = std::min(original_frame.rows, y1 + expanded_height);

                if (x2 > x1 && y2 > y1) {
                  cv::Rect roi(x1, y1, x2 - x1, y2 - y1);
                  cv::Mat cropped = original_frame(roi);

                  if (!cropped.empty()) {
                    cv::Mat resized;
                    cv::resize(cropped, resized, cv::Size(150, 150), 0, 0,
                               cv::INTER_LINEAR);
                    std::vector<uchar> buf;
                    cv::imencode(".jpg", resized, buf);
                    thumbnail_image = base64_encode(buf.data(), buf.size());
                  }
                }
              }
            } catch (...) {
              thumbnail_image = "";
            }

            // Track info
            jam_event_format::track_info track;
            track.bbox.x = target->x / frame_width;
            track.bbox.y = target->y / frame_height;
            track.bbox.width = target->width / frame_width;
            track.bbox.height = target->height / frame_height;

            track.class_label = target->primary_label.empty()
                                    ? "Object"
                                    : target->primary_label;
            track.external_id = "jam-event";
            track.id = "Tracker_" + std::to_string(target->track_id);
            track.last_seen = 0;
            track.source_tracker_track_id = target->track_id;

            evt.best_thumbnail_obj.confidence = target->primary_score;
            evt.best_thumbnail_obj.image = thumbnail_image;
            evt.best_thumbnail_obj.instance_id = instance_id_;
            evt.best_thumbnail_obj.label = ba_res->ba_label;
            evt.best_thumbnail_obj.system_date = get_current_date_iso();
            evt.best_thumbnail_obj.tracks.push_back(track);

            evt.type = "jam";
            evt.zone_id = zone_id_;
            evt.zone_name = zone_name_;

            // Determine line_id from channel mapping or fallback to zone_id
            std::string line_id = zone_id_;
            std::string line_name = zone_name_;

            // Try to get line_id from channel mapping (if CrossingLines config
            // was parsed)
            {
              std::lock_guard<std::mutex> lock(counts_mutex_);
              auto it = channel_to_line_info_.find(channel);
              if (it != channel_to_line_info_.end()) {
                line_id = it->second.first;
                line_name = it->second.second;
              } else {
                // Fallback: use zone_id or generate from channel
                if (line_id.empty() || line_id == "default_zone") {
                  line_id = "line_" + std::to_string(channel + 1);
                  line_name = "Line " + std::to_string(channel + 1);
                }
              }
            }
            evt.line_id = line_id;

            // Increment counter for this line
            {
              std::lock_guard<std::mutex> lock(counts_mutex_);
              line_counts_[line_id]++;
              // Store line name if not already stored
              if (line_names_.find(line_id) == line_names_.end()) {
                line_names_[line_id] = line_name.empty() ? line_id : line_name;
              }
            }

            event_msg.events.push_back(evt);
          }
          // Increment jam event index only for jam events
          jam_event_index++;
        }
      }

      if (!has_events) {
        msg = "";
        return;
      }

      event_msg.frame_id = meta->frame_index;
      event_msg.frame_time =
          meta->frame_index * 1000.0 / (meta->fps > 0 ? meta->fps : 30.0);
      event_msg.system_date = get_current_date_system();
      event_msg.system_timestamp = get_current_timestamp();
      event_msg.instance_id = instance_id_;     // UUID thực sự
      event_msg.instance_name = instance_name_; // Tên instance

      // Add line counts summary
      {
        std::lock_guard<std::mutex> lock(counts_mutex_);
        for (const auto &pair : line_counts_) {
          jam_event_format::line_count lc;
          lc.line_id = pair.first;
          lc.line_name = (line_names_.find(pair.first) != line_names_.end())
                             ? line_names_[pair.first]
                             : pair.first;
          lc.count = pair.second;
          event_msg.line_counts.push_back(lc);
        }
      }

      std::stringstream msg_stream;
      {
        cereal::JSONOutputArchive json_archive(msg_stream);
        std::vector<jam_event_format::event_message> result_array;
        result_array.push_back(event_msg);
        json_archive(result_array);
      }

      // Clean up JSON wrapper
      std::string json_str = msg_stream.str();
      size_t value0_pos = json_str.find("\"value0\"");
      if (value0_pos != std::string::npos) {
        size_t array_start = json_str.find('[', value0_pos);
        if (array_start != std::string::npos) {
          size_t array_end = json_str.rfind(']');
          if (array_end != std::string::npos && array_end > array_start) {
            msg = json_str.substr(array_start, array_end - array_start + 1);
          } else {
            msg = json_str;
          }
        } else {
          msg = json_str;
        }
      } else {
        msg = json_str;
      }

    } catch (...) {
      msg = "";
    }
  }

  virtual void broke_msg(const std::string &msg) override {
    if (!mqtt_publisher_) {
      std::cerr << "[MQTT] ⚠ broke_msg called but mqtt_publisher_ is null"
                << std::endl;
      return;
    }
    if (msg.empty()) {
      std::cerr << "[MQTT] ⚠ broke_msg called with empty message" << std::endl;
      return;
    }
    try {
      std::cerr
          << "[MQTT] [broke_msg] Calling mqtt_publisher_ with message length: "
          << msg.length() << std::endl;
      mqtt_publisher_(msg);
    } catch (const std::exception &e) {
      std::cerr << "[MQTT] ⚠ Exception in mqtt_publisher_: " << e.what()
                << std::endl;
    } catch (...) {
      std::cerr << "[MQTT] ⚠ Unknown exception in mqtt_publisher_" << std::endl;
    }
  }

public:
  cvedix_json_jam_mqtt_broker_node(
      std::string node_name,
      std::function<void(const std::string &)> mqtt_publisher,
      std::string instance_id, std::string instance_name, std::string zone_id,
      std::string zone_name, const std::string &crossing_lines_json = "")
      : cvedix_nodes::cvedix_json_enhanced_console_broker_node(
            node_name, cvedix_nodes::cvedix_broke_for::NORMAL, 100, 500, false),
        mqtt_publisher_(mqtt_publisher), instance_id_(instance_id),
        instance_name_(instance_name), zone_id_(zone_id),
        zone_name_(zone_name) {
    // Parse CrossingLines config to build channel -> line_id mapping
    if (!crossing_lines_json.empty()) {
      try {
        Json::Reader reader;
        Json::Value parsedLines;
        if (reader.parse(crossing_lines_json, parsedLines) &&
            parsedLines.isArray()) {
          for (Json::ArrayIndex i = 0; i < parsedLines.size(); ++i) {
            const Json::Value &lineObj = parsedLines[i];
            int channel = static_cast<int>(i);

            // Get line_id (use id if available, otherwise generate from index)
            std::string line_id;
            if (lineObj.isMember("id") && lineObj["id"].isString() &&
                !lineObj["id"].asString().empty()) {
              line_id = lineObj["id"].asString();
            } else {
              line_id = "line_" + std::to_string(channel + 1);
            }

            // Get line_name (use name if available, otherwise use line_id)
            std::string line_name;
            if (lineObj.isMember("name") && lineObj["name"].isString() &&
                !lineObj["name"].asString().empty()) {
              line_name = lineObj["name"].asString();
            } else {
              line_name = "Line " + std::to_string(channel + 1);
            }

            channel_to_line_info_[channel] = std::make_pair(line_id, line_name);
          }
        }
      } catch (...) {
        // If parsing fails, channel_to_line_info_ will remain empty
        // and code will fallback to zone_id
      }
    }
  }
};
namespace {} // namespace

// Event format structures for Jam MQTT
namespace stop_event_format {
struct normalized_bbox {
  double x, y, width, height;

  template <typename Archive> void serialize(Archive &archive) {
    archive(cereal::make_nvp("x", x), cereal::make_nvp("y", y),
            cereal::make_nvp("width", width),
            cereal::make_nvp("height", height));
  }
};

struct track_info {
  normalized_bbox bbox;
  std::string class_label;
  std::string external_id;
  std::string id;
  int last_seen;
  int source_tracker_track_id;

  template <typename Archive> void serialize(Archive &archive) {
    archive(
        cereal::make_nvp("bbox", bbox),
        cereal::make_nvp("class_label", class_label),
        cereal::make_nvp("external_id", external_id),
        cereal::make_nvp("id", id), cereal::make_nvp("last_seen", last_seen),
        cereal::make_nvp("source_tracker_track_id", source_tracker_track_id));
  }
};

struct best_thumbnail {
  double confidence;
  std::string image;
  std::string instance_id;
  std::string label;
  std::string system_date;
  std::vector<track_info> tracks;

  template <typename Archive> void serialize(Archive &archive) {
    archive(cereal::make_nvp("confidence", confidence),
            cereal::make_nvp("image", image),
            cereal::make_nvp("instance_id", instance_id),
            cereal::make_nvp("label", label),
            cereal::make_nvp("system_date", system_date),
            cereal::make_nvp("tracks", tracks));
  }
};

struct line_count {
  std::string line_id;
  std::string line_name;
  int count;
  template <typename Archive> void serialize(Archive &archive) {
    archive(cereal::make_nvp("line_id", line_id),
            cereal::make_nvp("line_name", line_name),
            cereal::make_nvp("count", count));
  }
};

struct event {
  best_thumbnail best_thumbnail_obj;
  std::string type;
  std::string zone_id;
  std::string zone_name;
  std::string line_id;

  template <typename Archive> void serialize(Archive &archive) {
    archive(cereal::make_nvp("best_thumbnail", best_thumbnail_obj),
            cereal::make_nvp("type", type),
            cereal::make_nvp("zone_id", zone_id),
            cereal::make_nvp("zone_name", zone_name),
            cereal::make_nvp("line_id", line_id));
  }
};

struct event_message {
  std::vector<event> events;
  int frame_id;
  double frame_time;
  std::string system_date;
  std::string system_timestamp;
  std::vector<line_count> line_counts;
  std::string instance_id;
  std::string instance_name;

  template <typename Archive> void serialize(Archive &archive) {
    archive(cereal::make_nvp("events", events),
            cereal::make_nvp("frame_id", frame_id),
            cereal::make_nvp("frame_time", frame_time),
            cereal::make_nvp("system_date", system_date),
            cereal::make_nvp("system_timestamp", system_timestamp),
            cereal::make_nvp("line_counts", line_counts),
            cereal::make_nvp("instance_id", instance_id),
            cereal::make_nvp("instance_name", instance_name));
  }
};
} // namespace stop_event_format

// Custom Broker Node for stop MQTT
class cvedix_json_stop_mqtt_broker_node
    : public cvedix_nodes::cvedix_json_enhanced_console_broker_node {
private:
  std::function<void(const std::string &)> mqtt_publisher_;
  std::string instance_id_;   // UUID thực sự của instance
  std::string instance_name_; // Tên instance (từ req.name)
  std::string zone_id_;
  std::string zone_name_;
  // Track counts per line: line_id -> count
  std::map<std::string, int> line_counts_;
  // Track line names: line_id -> line_name
  std::map<std::string, std::string> line_names_;
  // Track line info: channel -> (line_id, line_name)
  std::map<int, std::pair<std::string, std::string>> channel_to_line_info_;
  // Mutex for thread-safe access to counters
  std::mutex counts_mutex_;

  virtual void
  format_msg(const std::shared_ptr<cvedix_objects::cvedix_frame_meta> &meta,
             std::string &msg) override {
    try {
      if (meta->ba_results.empty()) {
        msg = "";
        return;
      }

      stop_event_format::event_message event_msg;
      bool has_events = false;

      double frame_width = static_cast<double>(meta->frame.cols);
      double frame_height = static_cast<double>(meta->frame.rows);

      // Track stop event index to map to line channel
      // Note: This assumes ba_results order matches line order, which may not
      // always be accurate but is the best we can do without line index in
      // ba_res
      int stop_event_index = 0;
      for (const auto &ba_res : meta->ba_results) {
        // Check if it's a stop event
        if (ba_res->type == cvedix_objects::cvedix_ba_type::STOP) {
          // Try to determine line_id from channel mapping
          // Use stop_event_index as channel (assuming ba_results order
          // matches line order)
          int channel = stop_event_index;
          // Iterate over all targets involved in this stop event
          for (int track_id : ba_res->involve_target_ids_in_frame) {
            // Find the target object
            std::shared_ptr<cvedix_objects::cvedix_frame_target> target =
                nullptr;
            for (const auto &t : meta->targets) {
              if (t->track_id == track_id) {
                target = t;
                break;
              }
            }

            if (!target)
              continue;

            has_events = true;
            stop_event_format::event evt;

            // Crop image logic
            std::string thumbnail_image = "";
            try {
              if (!meta->frame.empty()) {
                const cv::Mat &original_frame = meta->frame;

                const float expand_ratio = 0.35f;
                int center_x = target->x + target->width / 2;
                int center_y = target->y + target->height / 2;

                int expanded_width =
                    static_cast<int>(target->width * (1.0f + expand_ratio));
                int expanded_height =
                    static_cast<int>(target->height * (1.0f + expand_ratio));

                int x1 = std::max(0, center_x - expanded_width / 2);
                int y1 = std::max(0, center_y - expanded_height / 2);
                int x2 = std::min(original_frame.cols, x1 + expanded_width);
                int y2 = std::min(original_frame.rows, y1 + expanded_height);

                if (x2 > x1 && y2 > y1) {
                  cv::Rect roi(x1, y1, x2 - x1, y2 - y1);
                  cv::Mat cropped = original_frame(roi);

                  if (!cropped.empty()) {
                    cv::Mat resized;
                    cv::resize(cropped, resized, cv::Size(150, 150), 0, 0,
                               cv::INTER_LINEAR);
                    std::vector<uchar> buf;
                    cv::imencode(".jpg", resized, buf);
                    thumbnail_image = base64_encode(buf.data(), buf.size());
                  }
                }
              }
            } catch (...) {
              thumbnail_image = "";
            }

            // Track info
            stop_event_format::track_info track;
            track.bbox.x = target->x / frame_width;
            track.bbox.y = target->y / frame_height;
            track.bbox.width = target->width / frame_width;
            track.bbox.height = target->height / frame_height;

            track.class_label = target->primary_label.empty()
                                    ? "Object"
                                    : target->primary_label;
            track.external_id = "stop-event";
            track.id = "Tracker_" + std::to_string(target->track_id);
            track.last_seen = 0;
            track.source_tracker_track_id = target->track_id;

            evt.best_thumbnail_obj.confidence = target->primary_score;
            evt.best_thumbnail_obj.image = thumbnail_image;
            evt.best_thumbnail_obj.instance_id = instance_id_;
            evt.best_thumbnail_obj.label = ba_res->ba_label;
            evt.best_thumbnail_obj.system_date = get_current_date_iso();
            evt.best_thumbnail_obj.tracks.push_back(track);

            evt.type = "stop";
            evt.zone_id = zone_id_;
            evt.zone_name = zone_name_;

            // Determine line_id from channel mapping or fallback to zone_id
            std::string line_id = zone_id_;
            std::string line_name = zone_name_;

            // Try to get line_id from channel mapping (if CrossingLines config
            // was parsed)
            {
              std::lock_guard<std::mutex> lock(counts_mutex_);
              auto it = channel_to_line_info_.find(channel);
              if (it != channel_to_line_info_.end()) {
                line_id = it->second.first;
                line_name = it->second.second;
              } else {
                // Fallback: use zone_id or generate from channel
                if (line_id.empty() || line_id == "default_zone") {
                  line_id = "line_" + std::to_string(channel + 1);
                  line_name = "Line " + std::to_string(channel + 1);
                }
              }
            }
            evt.line_id = line_id;

            // Increment counter for this line
            {
              std::lock_guard<std::mutex> lock(counts_mutex_);
              line_counts_[line_id]++;
              // Store line name if not already stored
              if (line_names_.find(line_id) == line_names_.end()) {
                line_names_[line_id] = line_name.empty() ? line_id : line_name;
              }
            }

            event_msg.events.push_back(evt);
          }
          // Increment stop event index only for stop events
          stop_event_index++;
        }
      }

      if (!has_events) {
        msg = "";
        return;
      }

      event_msg.frame_id = meta->frame_index;
      event_msg.frame_time =
          meta->frame_index * 1000.0 / (meta->fps > 0 ? meta->fps : 30.0);
      event_msg.system_date = get_current_date_system();
      event_msg.system_timestamp = get_current_timestamp();
      event_msg.instance_id = instance_id_;     // UUID thực sự
      event_msg.instance_name = instance_name_; // Tên instance

      // Add line counts summary
      {
        std::lock_guard<std::mutex> lock(counts_mutex_);
        for (const auto &pair : line_counts_) {
          stop_event_format::line_count lc;
          lc.line_id = pair.first;
          lc.line_name = (line_names_.find(pair.first) != line_names_.end())
                             ? line_names_[pair.first]
                             : pair.first;
          lc.count = pair.second;
          event_msg.line_counts.push_back(lc);
        }
      }

      std::stringstream msg_stream;
      {
        cereal::JSONOutputArchive json_archive(msg_stream);
        std::vector<stop_event_format::event_message> result_array;
        result_array.push_back(event_msg);
        json_archive(result_array);
      }

      // Clean up JSON wrapper
      std::string json_str = msg_stream.str();
      size_t value0_pos = json_str.find("\"value0\"");
      if (value0_pos != std::string::npos) {
        size_t array_start = json_str.find('[', value0_pos);
        if (array_start != std::string::npos) {
          size_t array_end = json_str.rfind(']');
          if (array_end != std::string::npos && array_end > array_start) {
            msg = json_str.substr(array_start, array_end - array_start + 1);
          } else {
            msg = json_str;
          }
        } else {
          msg = json_str;
        }
      } else {
        msg = json_str;
      }

    } catch (...) {
      msg = "";
    }
  }

  virtual void broke_msg(const std::string &msg) override {
    if (!mqtt_publisher_) {
      std::cerr << "[MQTT] ⚠ broke_msg called but mqtt_publisher_ is null"
                << std::endl;
      return;
    }
    if (msg.empty()) {
      std::cerr << "[MQTT] ⚠ broke_msg called with empty message" << std::endl;
      return;
    }
    try {
      std::cerr
          << "[MQTT] [broke_msg] Calling mqtt_publisher_ with message length: "
          << msg.length() << std::endl;
      mqtt_publisher_(msg);
    } catch (const std::exception &e) {
      std::cerr << "[MQTT] ⚠ Exception in mqtt_publisher_: " << e.what()
                << std::endl;
    } catch (...) {
      std::cerr << "[MQTT] ⚠ Unknown exception in mqtt_publisher_" << std::endl;
    }
  }

public:
  cvedix_json_stop_mqtt_broker_node(
      std::string node_name,
      std::function<void(const std::string &)> mqtt_publisher,
      std::string instance_id, std::string instance_name, std::string zone_id,
      std::string zone_name, const std::string &crossing_lines_json = "")
      : cvedix_nodes::cvedix_json_enhanced_console_broker_node(
            node_name, cvedix_nodes::cvedix_broke_for::NORMAL, 100, 500, false),
        mqtt_publisher_(mqtt_publisher), instance_id_(instance_id),
        instance_name_(instance_name), zone_id_(zone_id),
        zone_name_(zone_name) {
    // Parse CrossingLines config to build channel -> line_id mapping
    if (!crossing_lines_json.empty()) {
      try {
        Json::Reader reader;
        Json::Value parsedLines;
        if (reader.parse(crossing_lines_json, parsedLines) &&
            parsedLines.isArray()) {
          for (Json::ArrayIndex i = 0; i < parsedLines.size(); ++i) {
            const Json::Value &lineObj = parsedLines[i];
            int channel = static_cast<int>(i);

            // Get line_id (use id if available, otherwise generate from index)
            std::string line_id;
            if (lineObj.isMember("id") && lineObj["id"].isString() &&
                !lineObj["id"].asString().empty()) {
              line_id = lineObj["id"].asString();
            } else {
              line_id = "line_" + std::to_string(channel + 1);
            }

            // Get line_name (use name if available, otherwise use line_id)
            std::string line_name;
            if (lineObj.isMember("name") && lineObj["name"].isString() &&
                !lineObj["name"].asString().empty()) {
              line_name = lineObj["name"].asString();
            } else {
              line_name = "Line " + std::to_string(channel + 1);
            }

            channel_to_line_info_[channel] = std::make_pair(line_id, line_name);
          }
        }
      } catch (...) {
        // If parsing fails, channel_to_line_info_ will remain empty
        // and code will fallback to zone_id
      }
    }
  }
};

// ========== Broker Nodes Implementation ==========





#ifdef CVEDIX_WITH_KAFKA

#endif

#ifdef CVEDIX_WITH_MQTT







#endif
// Note: Broker node implementations have been moved to PipelineBuilderBrokerNodes
// All broker nodes are now enabled (cereal dependency resolved)
/*















*/
// Note: Broker node implementations have been moved to PipelineBuilderBrokerNodes

// Static member definitions for SecuRT integration
AreaManager *PipelineBuilder::area_manager_ = nullptr;
SecuRTLineManager *PipelineBuilder::line_manager_ = nullptr;
std::map<std::string, std::string> PipelineBuilder::actual_rtmp_urls_;

void PipelineBuilder::setAreaManager(AreaManager *manager) {
  area_manager_ = manager;
}

void PipelineBuilder::setLineManager(SecuRTLineManager *manager) {
  line_manager_ = manager;
}

std::string PipelineBuilder::getActualRTMPUrl(const std::string &instanceId) {
  auto it = actual_rtmp_urls_.find(instanceId);
  if (it != actual_rtmp_urls_.end()) {
    return it->second;
  }
  return "";
}

void PipelineBuilder::clearActualRTMPUrl(const std::string &instanceId) {
  actual_rtmp_urls_.erase(instanceId);
}
