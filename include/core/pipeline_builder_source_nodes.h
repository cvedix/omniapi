#pragma once

#include "models/create_instance_request.h"
#include <memory>
#include <map>
#include <string>

// Forward declarations
namespace cvedix_nodes {
class cvedix_node;
}

/**
 * @brief Source Node Factory for PipelineBuilder
 * 
 * Handles creation of all source nodes (RTSP, RTMP, File, Image, App, UDP, FFmpeg)
 */
class PipelineBuilderSourceNodes {
public:
  /**
   * @brief Create RTSP source node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createRTSPSourceNode(const std::string &nodeName,
                       const std::map<std::string, std::string> &params,
                       const CreateInstanceRequest &req);

  /**
   * @brief Create file source node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createFileSourceNode(const std::string &nodeName,
                       const std::map<std::string, std::string> &params,
                       const CreateInstanceRequest &req);

  /**
   * @brief Create app source node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createAppSourceNode(const std::string &nodeName,
                      const std::map<std::string, std::string> &params,
                      const CreateInstanceRequest &req);

  /**
   * @brief Create image source node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createImageSourceNode(const std::string &nodeName,
                        const std::map<std::string, std::string> &params,
                        const CreateInstanceRequest &req);

  /**
   * @brief Create RTMP source node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createRTMPSourceNode(const std::string &nodeName,
                       const std::map<std::string, std::string> &params,
                       const CreateInstanceRequest &req);

  /**
   * @brief Create UDP source node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createUDPSourceNode(const std::string &nodeName,
                      const std::map<std::string, std::string> &params,
                      const CreateInstanceRequest &req);

  /**
   * @brief Create FFmpeg source node (for HLS, HTTP streams, etc.)
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createFFmpegSourceNode(const std::string &nodeName,
                         const std::map<std::string, std::string> &params,
                         const CreateInstanceRequest &req);

  /**
   * @brief Detect input type from URI/path
   * @param uri URI or file path
   * @return Input type: "rtsp", "rtmp", "hls", "http", "file"
   */
  static std::string detectInputType(const std::string &uri);
};

