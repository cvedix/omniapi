#include "core/pipeline_builder_other_nodes.h"
#include "core/pipeline_builder_model_resolver.h"
#include "core/env_config.h"
#include <iostream>
#include <stdexcept>
#include <cvedix/nodes/track/cvedix_sort_track_node.h>
#include <cvedix/nodes/track/cvedix_bytetrack_node.h>
// TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
// #include <cvedix/nodes/track/cvedix_ocsort_track_node.h>
#include <cvedix/nodes/osd/cvedix_face_osd_node_v2.h>
#include <cvedix/nodes/osd/cvedix_osd_node_v3.h>
#include <cvedix/nodes/proc/cvedix_frame_fusion_node.h>
#include <cvedix/nodes/record/cvedix_record_node.h>
#include <cvedix/objects/shapes/cvedix_point.h>
#include <cvedix/objects/shapes/cvedix_size.h>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <json/reader.h>
#include <json/value.h>

namespace fs = std::filesystem;



std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderOtherNodes::createSORTTrackNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params) {

  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderOtherNodes] Creating SORT tracker node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;

    auto node =
        std::make_shared<cvedix_nodes::cvedix_sort_track_node>(nodeName);

    std::cerr << "[PipelineBuilderOtherNodes] ✓ SORT tracker node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderOtherNodes] Exception in createSORTTrackNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderOtherNodes::createByteTrackNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params) {

  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderOtherNodes] Creating ByteTrack tracker node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;

    // Parse parameters with defaults
    float track_thresh = 0.5f;
    float high_thresh = 0.6f;
    float match_thresh = 0.8f;
    int track_buffer = 30;
    int frame_rate = 30;
    cvedix_nodes::cvedix_track_for track_for = cvedix_nodes::cvedix_track_for::NORMAL;

    // Parse track_thresh
    if (params.count("track_thresh")) {
      try {
        track_thresh = std::stof(params.at("track_thresh"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid track_thresh, using default: 0.5" << std::endl;
      }
    }

    // Parse high_thresh
    if (params.count("high_thresh")) {
      try {
        high_thresh = std::stof(params.at("high_thresh"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid high_thresh, using default: 0.6" << std::endl;
      }
    }

    // Parse match_thresh
    if (params.count("match_thresh")) {
      try {
        match_thresh = std::stof(params.at("match_thresh"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid match_thresh, using default: 0.8" << std::endl;
      }
    }

    // Parse track_buffer
    if (params.count("track_buffer")) {
      try {
        track_buffer = std::stoi(params.at("track_buffer"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid track_buffer, using default: 30" << std::endl;
      }
    }

    // Parse frame_rate
    if (params.count("frame_rate")) {
      try {
        frame_rate = std::stoi(params.at("frame_rate"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid frame_rate, using default: 30" << std::endl;
      }
    }

    // Parse track_for (optional)
    // Note: cvedix_track_for enum only has NORMAL and FACE
    if (params.count("track_for")) {
      std::string track_for_str = params.at("track_for");
      if (track_for_str == "FACE") {
        track_for = cvedix_nodes::cvedix_track_for::FACE;
      }
      // All other values default to NORMAL
    }

    std::cerr << "  track_thresh: " << track_thresh << std::endl;
    std::cerr << "  high_thresh: " << high_thresh << std::endl;
    std::cerr << "  match_thresh: " << match_thresh << std::endl;
    std::cerr << "  track_buffer: " << track_buffer << std::endl;
    std::cerr << "  frame_rate: " << frame_rate << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_bytetrack_node>(
        nodeName, track_for, track_thresh, high_thresh, match_thresh, track_buffer, frame_rate);

    std::cerr << "[PipelineBuilderOtherNodes] ✓ ByteTrack tracker node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderOtherNodes] Exception in createByteTrackNode: "
              << e.what() << std::endl;
    throw;
  }
}

// TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderOtherNodes::createOCSortTrackNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params) {

  // TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
  throw std::runtime_error("OCSort track node is temporarily disabled due to CVEDIX SDK cereal/rapidxml issue");
  
  /* DISABLED CODE - cereal/rapidxml issue
  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderOtherNodes] Creating OCSort tracker node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;

    // Parse parameters with defaults
    cvedix_nodes::cvedix_track_for track_for = cvedix_nodes::cvedix_track_for::NORMAL;
    float det_thresh = 0.25f;
    int max_age = 30;
    int min_hits = 3;
    float iou_threshold = 0.3f;
    int delta_t = 3;
    std::string asso_func = "iou";
    float inertia = 0.2f;
    bool use_byte = false;

    // Parse track_for (optional)
    // Note: cvedix_track_for enum only has NORMAL and FACE
    if (params.count("track_for")) {
      std::string track_for_str = params.at("track_for");
      if (track_for_str == "FACE") {
        track_for = cvedix_nodes::cvedix_track_for::FACE;
      }
      // All other values default to NORMAL
    }

    // Parse det_thresh
    if (params.count("det_thresh")) {
      try {
        det_thresh = std::stof(params.at("det_thresh"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid det_thresh, using default: 0.25" << std::endl;
      }
    }

    // Parse max_age
    if (params.count("max_age")) {
      try {
        max_age = std::stoi(params.at("max_age"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid max_age, using default: 30" << std::endl;
      }
    }

    // Parse min_hits
    if (params.count("min_hits")) {
      try {
        min_hits = std::stoi(params.at("min_hits"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid min_hits, using default: 3" << std::endl;
      }
    }

    // Parse iou_threshold
    if (params.count("iou_threshold")) {
      try {
        iou_threshold = std::stof(params.at("iou_threshold"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid iou_threshold, using default: 0.3" << std::endl;
      }
    }

    // Parse delta_t
    if (params.count("delta_t")) {
      try {
        delta_t = std::stoi(params.at("delta_t"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid delta_t, using default: 3" << std::endl;
      }
    }

    // Parse asso_func
    if (params.count("asso_func")) {
      asso_func = params.at("asso_func");
    }

    // Parse inertia
    if (params.count("inertia")) {
      try {
        inertia = std::stof(params.at("inertia"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid inertia, using default: 0.2" << std::endl;
      }
    }

    // Parse use_byte
    if (params.count("use_byte")) {
      std::string use_byte_str = params.at("use_byte");
      use_byte = (use_byte_str == "true" || use_byte_str == "1" || use_byte_str == "yes");
    }

    std::cerr << "  det_thresh: " << det_thresh << std::endl;
    std::cerr << "  max_age: " << max_age << std::endl;
    std::cerr << "  min_hits: " << min_hits << std::endl;
    std::cerr << "  iou_threshold: " << iou_threshold << std::endl;
    std::cerr << "  delta_t: " << delta_t << std::endl;
    std::cerr << "  asso_func: " << asso_func << std::endl;
    std::cerr << "  inertia: " << inertia << std::endl;
    std::cerr << "  use_byte: " << (use_byte ? "true" : "false") << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_ocsort_track_node>(
        nodeName, track_for, det_thresh, max_age, min_hits, iou_threshold, 
        delta_t, asso_func, inertia, use_byte);

    std::cerr << "[PipelineBuilderOtherNodes] ✓ OCSort tracker node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderOtherNodes] Exception in createOCSortTrackNode: "
              << e.what() << std::endl;
    throw;
  }
  */
}

std::shared_ptr<cvedix_nodes::cvedix_node> PipelineBuilderOtherNodes::createFaceOSDNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params) {

  try {
    // Validate node name
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderOtherNodes] Creating face OSD v2 node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;

    auto node =
        std::make_shared<cvedix_nodes::cvedix_face_osd_node_v2>(nodeName);

    std::cerr << "[PipelineBuilderOtherNodes] ✓ Face OSD v2 node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderOtherNodes] Exception in createFaceOSDNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node> PipelineBuilderOtherNodes::createOSDv3Node(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    // Priority 1: Check additionalParams for FONT_PATH (highest priority -
    // allows runtime override)
    std::string fontPath = "";
    auto it = req.additionalParams.find("FONT_PATH");
    if (it != req.additionalParams.end() && !it->second.empty()) {
      fontPath = it->second;
      std::cerr << "[PipelineBuilderOtherNodes] Using FONT_PATH from additionalParams: "
                << fontPath << std::endl;
    }

    // Priority 2: Get font_path from params if not in additionalParams
    if (fontPath.empty() && params.count("font_path") &&
        !params.at("font_path").empty()) {
      fontPath = params.at("font_path");

      // Check if font file exists, try to resolve path
      fs::path fontFilePath(fontPath);

      // If relative path, try to resolve it
      if (!fontFilePath.is_absolute()) {
        // Try current directory first
        if (fs::exists(fontPath)) {
          fontPath = fs::absolute(fontPath).string();
        } else {
          // Try with CVEDIX_DATA_ROOT or CVEDIX_SDK_ROOT
          const char *dataRoot = std::getenv("CVEDIX_DATA_ROOT");
          if (dataRoot && strlen(dataRoot) > 0) {
            std::string resolvedPath = std::string(dataRoot);
            if (resolvedPath.back() != '/')
              resolvedPath += '/';
            resolvedPath += fontPath;
            if (fs::exists(resolvedPath)) {
              fontPath = resolvedPath;
            }
          }

          // Try CVEDIX_SDK_ROOT
          if (!fs::exists(fontPath)) {
            const char *sdkRoot = std::getenv("CVEDIX_SDK_ROOT");
            if (sdkRoot && strlen(sdkRoot) > 0) {
              std::string resolvedPath = std::string(sdkRoot);
              if (resolvedPath.back() != '/')
                resolvedPath += '/';
              resolvedPath += "cvedix_data/" + fontPath;
              if (fs::exists(resolvedPath)) {
                fontPath = resolvedPath;
              }
            }
          }
        }
      }

      // Check if font file exists after resolution
      if (!fs::exists(fontPath)) {
        std::cerr << "[PipelineBuilderOtherNodes] ⚠ WARNING: Font file not found: '"
                  << params.at("font_path") << "'" << std::endl;
        std::cerr << "[PipelineBuilderOtherNodes] ⚠ Resolved path: '" << fontPath << "'"
                  << std::endl;
        std::cerr
            << "[PipelineBuilderOtherNodes] ⚠ Trying default font from environment..."
            << std::endl;
        fontPath = ""; // Will try default font below
      }
    }

    // Priority 3: If no font_path in params/additionalParams or font file not
    // found, try default font
    if (fontPath.empty()) {
      // Try default font from /opt/omniapi/fonts/ first
      std::string defaultFontPath =
          "/opt/omniapi/fonts/NotoSansCJKsc-Medium.otf";
      if (fs::exists(defaultFontPath)) {
        fontPath = defaultFontPath;
        std::cerr << "[PipelineBuilderOtherNodes] Using default font from "
                     "/opt/omniapi/fonts/"
                  << std::endl;
      } else {
        // Fallback to environment variable resolution
        fontPath = EnvConfig::resolveDefaultFontPath();
      }
    }

    std::cerr << "[PipelineBuilderOtherNodes] Creating OSD v3 node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    if (!fontPath.empty()) {
      std::cerr << "  Font path: '" << fontPath << "'" << std::endl;
    } else {
      std::cerr << "  Font: Using default font" << std::endl;
    }

    // Try to create node with font path, if it fails, fallback to default font
    std::shared_ptr<cvedix_nodes::cvedix_node> node;
    try {
      node = std::make_shared<cvedix_nodes::cvedix_osd_node_v3>(nodeName,
                                                                fontPath);
      std::cerr << "[PipelineBuilderOtherNodes] ✓ OSD v3 node created successfully"
                << std::endl;
      return node;
    } catch (const cv::Exception &e) {
      // OpenCV exception (likely font loading failed)
      if (!fontPath.empty()) {
        std::cerr << "[PipelineBuilderOtherNodes] ⚠ WARNING: Failed to load font from '"
                  << fontPath << "': " << e.what() << std::endl;
        std::cerr << "[PipelineBuilderOtherNodes] ⚠ Falling back to default font (no "
                     "Chinese/Unicode support)"
                  << std::endl;
        // Try again with empty font path (default font)
        try {
          node =
              std::make_shared<cvedix_nodes::cvedix_osd_node_v3>(nodeName, "");
          std::cerr << "[PipelineBuilderOtherNodes] ✓ OSD v3 node created successfully "
                       "with default font"
                    << std::endl;
          return node;
        } catch (const std::exception &e2) {
          std::cerr << "[PipelineBuilderOtherNodes] ✗ ERROR: Failed to create OSD v3 "
                       "node even with default font: "
                    << e2.what() << std::endl;
          throw;
        }
      } else {
        // Already using default font, rethrow
        throw;
      }
    } catch (const std::exception &e) {
      // Other exceptions
      std::cerr << "[PipelineBuilderOtherNodes] Exception in createOSDv3Node: "
                << e.what() << std::endl;
      throw;
    }
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderOtherNodes] Exception in createOSDv3Node: " << e.what()
              << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderOtherNodes::createFrameFusionNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderOtherNodes] Creating frame fusion node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;

    // Parse calibration points
    // src_cali_points and des_cali_points should be JSON arrays of 4 points
    // Format: [{"x": 0, "y": 0}, {"x": 100, "y": 0}, {"x": 100, "y": 100}, {"x": 0, "y": 100}]
    std::vector<cvedix_objects::cvedix_point> src_points;
    std::vector<cvedix_objects::cvedix_point> des_points;

    // Get src_cali_points
    std::string src_cali_points_str = "";
    if (params.count("src_cali_points")) {
      src_cali_points_str = params.at("src_cali_points");
    } else {
      auto it = req.additionalParams.find("SRC_CALI_POINTS");
      if (it != req.additionalParams.end()) {
        src_cali_points_str = it->second;
      }
    }

    // Get des_cali_points
    std::string des_cali_points_str = "";
    if (params.count("des_cali_points")) {
      des_cali_points_str = params.at("des_cali_points");
    } else {
      auto it = req.additionalParams.find("DES_CALI_POINTS");
      if (it != req.additionalParams.end()) {
        des_cali_points_str = it->second;
      }
    }

    // Parse src_cali_points
    if (!src_cali_points_str.empty()) {
      Json::Reader reader;
      Json::Value src_points_json;
      if (reader.parse(src_cali_points_str, src_points_json) && src_points_json.isArray() && src_points_json.size() == 4) {
        for (Json::ArrayIndex i = 0; i < 4; ++i) {
          const Json::Value &point = src_points_json[i];
          if (point.isObject() && point.isMember("x") && point.isMember("y")) {
            cvedix_objects::cvedix_point p;
            p.x = point["x"].asInt();
            p.y = point["y"].asInt();
            src_points.push_back(p);
          }
        }
      }
    }

    // Parse des_cali_points
    if (!des_cali_points_str.empty()) {
      Json::Reader reader;
      Json::Value des_points_json;
      if (reader.parse(des_cali_points_str, des_points_json) && des_points_json.isArray() && des_points_json.size() == 4) {
        for (Json::ArrayIndex i = 0; i < 4; ++i) {
          const Json::Value &point = des_points_json[i];
          if (point.isObject() && point.isMember("x") && point.isMember("y")) {
            cvedix_objects::cvedix_point p;
            p.x = point["x"].asInt();
            p.y = point["y"].asInt();
            des_points.push_back(p);
          }
        }
      }
    }

    // Validate points
    if (src_points.size() != 4) {
      throw std::runtime_error("src_cali_points must contain exactly 4 points in format: [{\"x\": 0, \"y\": 0}, ...]");
    }
    if (des_points.size() != 4) {
      throw std::runtime_error("des_cali_points must contain exactly 4 points in format: [{\"x\": 0, \"y\": 0}, ...]");
    }

    // Parse channel indices (optional)
    int src_channel_index = 0;
    int des_channel_index = 1;
    if (params.count("src_channel_index")) {
      try {
        src_channel_index = std::stoi(params.at("src_channel_index"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid src_channel_index, using default: 0" << std::endl;
      }
    }
    if (params.count("des_channel_index")) {
      try {
        des_channel_index = std::stoi(params.at("des_channel_index"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid des_channel_index, using default: 1" << std::endl;
      }
    }

    std::cerr << "  Source channel: " << src_channel_index << std::endl;
    std::cerr << "  Destination channel: " << des_channel_index << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_frame_fusion_node>(
        nodeName, src_points, des_points, src_channel_index, des_channel_index);

    std::cerr << "[PipelineBuilderOtherNodes] ✓ Frame fusion node created successfully" << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderOtherNodes] Exception in createFrameFusionNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderOtherNodes::createRecordNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderOtherNodes] Creating record node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;

    // Parse required parameters
    std::string video_save_dir = "";
    std::string image_save_dir = "";

    // Get video_save_dir
    if (params.count("video_dir")) {
      video_save_dir = params.at("video_dir");
    } else if (params.count("video_save_dir")) {
      video_save_dir = params.at("video_save_dir");
    } else {
      auto it = req.additionalParams.find("VIDEO_DIR");
      if (it != req.additionalParams.end()) {
        video_save_dir = it->second;
      }
    }

    // Get image_save_dir
    if (params.count("image_dir")) {
      image_save_dir = params.at("image_dir");
    } else if (params.count("image_save_dir")) {
      image_save_dir = params.at("image_save_dir");
    } else {
      auto it = req.additionalParams.find("IMAGE_DIR");
      if (it != req.additionalParams.end()) {
        image_save_dir = it->second;
      }
    }

    // At least one directory must be provided
    if (video_save_dir.empty() && image_save_dir.empty()) {
      throw std::runtime_error("Either video_dir or image_dir must be provided");
    }

    // Use default directories if not provided
    if (video_save_dir.empty()) {
      video_save_dir = "./recordings/video";
    }
    if (image_save_dir.empty()) {
      image_save_dir = "./recordings/image";
    }

    // Parse optional parameters
    cvedix_objects::cvedix_size resolution_w_h = {};
    bool osd = false;
    int pre_record_video_duration = 5;
    int record_video_duration = 20;
    bool auto_sub_dir = true;
    int bitrate = 1024;

    // Parse resolution (format: "width,height" or "widthxheight")
    if (params.count("resolution")) {
      std::string resolution_str = params.at("resolution");
      size_t comma_pos = resolution_str.find(',');
      size_t x_pos = resolution_str.find('x');
      if (comma_pos != std::string::npos) {
        resolution_w_h.width = std::stoi(resolution_str.substr(0, comma_pos));
        resolution_w_h.height = std::stoi(resolution_str.substr(comma_pos + 1));
      } else if (x_pos != std::string::npos) {
        resolution_w_h.width = std::stoi(resolution_str.substr(0, x_pos));
        resolution_w_h.height = std::stoi(resolution_str.substr(x_pos + 1));
      }
    }

    // Parse osd
    if (params.count("osd")) {
      std::string osd_str = params.at("osd");
      osd = (osd_str == "true" || osd_str == "1" || osd_str == "yes");
    }

    // Parse pre_record_video_duration
    if (params.count("pre_record_video_duration")) {
      try {
        pre_record_video_duration = std::stoi(params.at("pre_record_video_duration"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid pre_record_video_duration, using default: 5" << std::endl;
      }
    }

    // Parse record_video_duration
    if (params.count("record_video_duration")) {
      try {
        record_video_duration = std::stoi(params.at("record_video_duration"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid record_video_duration, using default: 20" << std::endl;
      }
    }

    // Parse auto_sub_dir
    if (params.count("auto_sub_dir")) {
      std::string auto_sub_dir_str = params.at("auto_sub_dir");
      auto_sub_dir = (auto_sub_dir_str == "true" || auto_sub_dir_str == "1" || auto_sub_dir_str == "yes");
    }

    // Parse bitrate
    if (params.count("bitrate")) {
      try {
        bitrate = std::stoi(params.at("bitrate"));
      } catch (...) {
        std::cerr << "[PipelineBuilderOtherNodes] Invalid bitrate, using default: 1024" << std::endl;
      }
    }

    std::cerr << "  Video directory: '" << video_save_dir << "'" << std::endl;
    std::cerr << "  Image directory: '" << image_save_dir << "'" << std::endl;
    std::cerr << "  Resolution: " << resolution_w_h.width << "x" << resolution_w_h.height << std::endl;
    std::cerr << "  OSD: " << (osd ? "true" : "false") << std::endl;
    std::cerr << "  Pre-record duration: " << pre_record_video_duration << "s" << std::endl;
    std::cerr << "  Record duration: " << record_video_duration << "s" << std::endl;
    std::cerr << "  Auto sub-directory: " << (auto_sub_dir ? "true" : "false") << std::endl;
    std::cerr << "  Bitrate: " << bitrate << " kbps" << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_record_node>(
        nodeName, video_save_dir, image_save_dir, resolution_w_h, osd,
        pre_record_video_duration, record_video_duration, auto_sub_dir, bitrate);

    std::cerr << "[PipelineBuilderOtherNodes] ✓ Record node created successfully" << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderOtherNodes] Exception in createRecordNode: "
              << e.what() << std::endl;
    throw;
  }
}