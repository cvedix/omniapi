#pragma once

#include "models/create_instance_request.h"
#include <memory>
#include <map>
#include <set>
#include <string>
#include <vector>

// Forward declarations
namespace cvedix_nodes {
class cvedix_node;
}

/**
 * @brief Destination Node Factory for PipelineBuilder
 * 
 * Handles creation of all destination nodes (File, RTMP, Screen, App)
 */
class PipelineBuilderDestinationNodes {
public:
  /**
   * @brief Create file destination node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createFileDestinationNode(const std::string &nodeName,
                            const std::map<std::string, std::string> &params,
                            const std::string &instanceId);

  /**
   * @brief Create RTMP destination with last-frame-fallback proxy.
   * Returns a proxy node (attach to OSD); rtmp_des is attached to proxy.
   * When outExtraNodes is non-null, the actual rtmp_des node is appended
   * so the caller can add it to the pipeline nodes list.
   * @param actualRtmpUrl Output: actual RTMP URL used
   * @param outExtraNodes Optional: append rtmp_des node here so caller can add to pipeline
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createRTMPDestinationNode(const std::string &nodeName,
                            const std::map<std::string, std::string> &params,
                            const CreateInstanceRequest &req,
                            const std::string &instanceId,
                            const std::set<std::string> &existingRTMPStreamKeys,
                            std::string &actualRtmpUrl,
                            std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> *outExtraNodes = nullptr);

  /**
   * @brief Create screen destination node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createScreenDestinationNode(const std::string &nodeName,
                              const std::map<std::string, std::string> &params);

  /**
   * @brief Create app destination node (for frame capture)
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createAppDesNode(const std::string &nodeName,
                   const std::map<std::string, std::string> &params);

  /**
   * @brief Create RTSP destination node (requires CVEDIX_WITH_GSTREAMER)
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createRTSPDestinationNode(const std::string &nodeName,
                            const std::map<std::string, std::string> &params,
                            const std::string &instanceId);

  /**
   * @brief Extract stream key from RTMP URL
   * @param rtmpUrl RTMP URL (e.g., rtmp://host:port/path/stream_key)
   * @return Stream key or empty string if invalid
   */
  static std::string extractRTMPStreamKey(const std::string &rtmpUrl);
};

