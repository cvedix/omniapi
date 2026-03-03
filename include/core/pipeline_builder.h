#pragma once

#include "instances/instance_info.h"
#include "models/create_instance_request.h"
#include "models/solution_config.h"
#include "core/pipeline_builder_model_resolver.h"
#include "core/pipeline_builder_request_utils.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Forward declarations
namespace cvedix_nodes {
class cvedix_node;
}
class AreaManager;
class SecuRTLineManager;

/**
 * @brief Pipeline Builder
 *
 * Builds CVEDIX SDK pipelines from solution configurations and instance
 * requests.
 */
class PipelineBuilder {
public:
  /**
   * @brief Set Area Manager for SecuRT integration
   * @param manager Area manager instance
   */
  static void setAreaManager(AreaManager *manager);

  /**
   * @brief Set Line Manager for SecuRT integration
   * @param manager Line manager instance
   */
  static void setLineManager(SecuRTLineManager *manager);
  /**
   * @brief Build pipeline from solution config and request
   * @param solution Solution configuration
   * @param req Create instance request
   * @param instanceId Instance ID for node naming
   * @return Vector of pipeline nodes (connected in order)
   */
  std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>>
  buildPipeline(const SolutionConfig &solution,
                const CreateInstanceRequest &req,
                const std::string &instanceId,
                const std::set<std::string> &existingRTMPStreamKeys = {});


private:
  // Static pointers for SecuRT integration
  static AreaManager *area_manager_;
  static SecuRTLineManager *line_manager_;
  
  // Map to store actual RTMP URLs (may be modified for conflict resolution)
  static std::map<std::string, std::string> actual_rtmp_urls_;
  
public:
  /**
   * @brief Get actual RTMP URL for an instance (may have been modified for conflict resolution)
   * @param instanceId Instance ID
   * @return Actual RTMP URL or empty string if not found
   */
  static std::string getActualRTMPUrl(const std::string &instanceId);
  
  /**
   * @brief Clear actual RTMP URL for an instance
   * @param instanceId Instance ID
   */
  static void clearActualRTMPUrl(const std::string &instanceId);

private:
  
  /**
   * @brief Create a node from node configuration
   * @param nodeConfig Node configuration
   * @param req Create instance request
   * @param instanceId Instance ID
   * @return Created node
   */
  std::shared_ptr<cvedix_nodes::cvedix_node>
  createNode(const SolutionConfig::NodeConfig &nodeConfig,
             const CreateInstanceRequest &req, const std::string &instanceId,
             const std::set<std::string> &existingRTMPStreamKeys = {},
             std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> *outExtraNodes = nullptr);

  // Helper methods for buildPipeline()
  /**
   * @brief Load SecuRT areas and lines into request
   * @param solution Solution configuration
   * @param req Create instance request (mutable reference)
   * @param instanceId Instance ID
   */
  void loadSecuRTData(const SolutionConfig &solution,
                      CreateInstanceRequest &req,
                      const std::string &instanceId);

  /**
   * @brief Handle multiple source nodes from FILE_PATHS or RTSP_URLS
   * @param req Create instance request
   * @param instanceId Instance ID
   * @param nodes Output vector for created nodes
   * @param nodeTypes Output vector for node types
   * @return Tuple of (hasMultipleSources, hasCustomSourceNodes, multipleSourceType, multipleSourceNodes)
   */
  std::tuple<bool, bool, std::string, std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>>>
  handleMultipleSources(const CreateInstanceRequest &req,
                        const std::string &instanceId,
                        std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes,
                        std::vector<std::string> &nodeTypes);

  /**
   * @brief Auto-inject optional nodes (app_des, file_des, rtmp_des, screen_des, MQTT brokers)
   * @param req Create instance request
   * @param instanceId Instance ID
   * @param existingRTMPStreamKeys Existing RTMP stream keys
   * @param nodes Output vector for created nodes
   * @param nodeTypes Output vector for node types
   */
  void autoInjectOptionalNodes(const CreateInstanceRequest &req,
                               const std::string &instanceId,
                               const std::set<std::string> &existingRTMPStreamKeys,
                               std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes,
                               std::vector<std::string> &nodeTypes);

  /**
   * @brief Connect node to appropriate previous node(s)
   * @param node Node to connect
   * @param nodeConfig Node configuration
   * @param nodes All created nodes so far
   * @param nodeTypes Node types so far
   * @param hasMultipleSources Whether multiple sources exist
   * @param multipleSourceNodes Multiple source nodes if applicable
   * @param multipleSourceType Type of multiple sources
   * @param hasOSDNode Whether pipeline has OSD node
   */
  void connectNode(std::shared_ptr<cvedix_nodes::cvedix_node> node,
                   const SolutionConfig::NodeConfig &nodeConfig,
                   std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &nodes,
                   const std::vector<std::string> &nodeTypes,
                   bool hasMultipleSources,
                   const std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> &multipleSourceNodes,
                   const std::string &multipleSourceType,
                   bool hasOSDNode);

  // Helper methods for createNode()
  /**
   * @brief Substitute placeholders in node name
   * @param nodeName Node name template
   * @param instanceId Instance ID
   * @return Substituted node name
   */
  std::string substituteNodeName(const std::string &nodeName,
                                 const std::string &instanceId);

  /**
   * @brief Build parameter map with all substitutions
   * @param nodeConfig Node configuration
   * @param req Create instance request
   * @param instanceId Instance ID
   * @return Parameter map with substituted values
   */
  std::map<std::string, std::string>
  buildParameterMap(const SolutionConfig::NodeConfig &nodeConfig,
                    const CreateInstanceRequest &req,
                    const std::string &instanceId);

  /**
   * @brief Auto-detect source type from file_src configuration
   * @param nodeConfig Node configuration
   * @param req Create instance request
   * @param nodeName Node name (may be modified)
   * @param params Parameters (may be modified)
   * @return Actual node type (may differ from nodeConfig.nodeType)
   */
  std::string detectSourceType(const SolutionConfig::NodeConfig &nodeConfig,
                                const CreateInstanceRequest &req,
                                std::string &nodeName,
                                std::map<std::string, std::string> &params);

  // Note: Source node methods have been moved to PipelineBuilderSourceNodes
  // Note: Destination node methods have been moved to PipelineBuilderDestinationNodes
  // Note: Detector node methods have been moved to PipelineBuilderDetectorNodes
  // Note: Broker node methods have been moved to PipelineBuilderBrokerNodes
  // Note: Behavior Analysis node methods have been moved to PipelineBuilderBehaviorAnalysisNodes
  // Note: OSD and Tracking node methods have been moved to PipelineBuilderOtherNodes

  // Note: Utility methods have been moved to separate utility classes:
  // - PipelineBuilderModelResolver: resolveModelPath, resolveModelByName, 
  //   listAvailableModels, mapDetectionSensitivity
  // - PipelineBuilderRequestUtils: getFilePath, getRTMPUrl, getRTSPUrl
};
