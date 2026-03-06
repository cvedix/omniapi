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
 * @brief Broker Node Factory for PipelineBuilder
 * 
 * Handles creation of all broker nodes (MQTT, Kafka, XML, Socket, etc.)
 */
class PipelineBuilderBrokerNodes {
public:
  // ========== JSON Broker Nodes ==========

  /**
   * @brief Create JSON console broker node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createJSONConsoleBrokerNode(const std::string &nodeName,
                              const std::map<std::string, std::string> &params,
                              const CreateInstanceRequest &req);

  /**
   * @brief Create JSON enhanced console broker node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createJSONEnhancedConsoleBrokerNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);

  /**
   * @brief Create JSON MQTT broker node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createJSONMQTTBrokerNode(const std::string &nodeName,
                           const std::map<std::string, std::string> &params,
                           const CreateInstanceRequest &req);

  /**
   * @brief Create JSON Crossline MQTT broker node (custom formatting for ba_crossline)
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node> createJSONCrosslineMQTTBrokerNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req, const std::string &instanceId);

  /**
   * @brief Create JSON Jam MQTT broker node (custom formatting for ba_jam)
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node> createJSONJamMQTTBrokerNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req, const std::string &instanceId);

  /**
   * @brief Create JSON Stop MQTT broker node (custom formatting for ba_stop)
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node> createJSONStopMQTTBrokerNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req, const std::string &instanceId);

  /**
   * @brief Create JSON Kafka broker node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createJSONKafkaBrokerNode(const std::string &nodeName,
                            const std::map<std::string, std::string> &params,
                            const CreateInstanceRequest &req);

  // ========== XML Broker Nodes ==========

  /**
   * @brief Create XML file broker node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createXMLFileBrokerNode(const std::string &nodeName,
                          const std::map<std::string, std::string> &params,
                          const CreateInstanceRequest &req);

  /**
   * @brief Create XML socket broker node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createXMLSocketBrokerNode(const std::string &nodeName,
                            const std::map<std::string, std::string> &params,
                            const CreateInstanceRequest &req);

  // ========== Socket Broker Nodes ==========

  /**
   * @brief Create message broker node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createMessageBrokerNode(const std::string &nodeName,
                          const std::map<std::string, std::string> &params,
                          const CreateInstanceRequest &req);

  /**
   * @brief Create BA socket broker node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createBASocketBrokerNode(const std::string &nodeName,
                           const std::map<std::string, std::string> &params,
                           const CreateInstanceRequest &req);

  /**
   * @brief Create embeddings socket broker node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node> createEmbeddingsSocketBrokerNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);

  /**
   * @brief Create embeddings properties socket broker node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createEmbeddingsPropertiesSocketBrokerNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);

  /**
   * @brief Create plate socket broker node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createPlateSocketBrokerNode(const std::string &nodeName,
                              const std::map<std::string, std::string> &params,
                              const CreateInstanceRequest &req);

  /**
   * @brief Create expression socket broker node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node> createExpressionSocketBrokerNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);

  // ========== SSE Broker Nodes ==========

  /**
   * @brief Create SSE broker node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createSSEBrokerNode(const std::string &nodeName,
                      const std::map<std::string, std::string> &params,
                      const CreateInstanceRequest &req);
};

