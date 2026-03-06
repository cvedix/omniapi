#include "core/pipeline_builder_broker_nodes.h"
#include "core/pipeline_builder_model_resolver.h"
#include <cvedix/utils/mqtt_client/cvedix_mqtt_client.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <json/reader.h>
#include <json/value.h>

// Include forward declarations for custom broker nodes
// Note: Full class definitions are in pipeline_builder.cpp
// These classes are defined in pipeline_builder.cpp and will be available at link time
// TODO: Move full class definitions to a separate header file for better organization
#ifdef CVEDIX_WITH_MQTT
// Forward declarations - full definitions are in pipeline_builder.cpp
// The linker will resolve these when both files are linked together
class cvedix_json_crossline_mqtt_broker_node;
class cvedix_json_jam_mqtt_broker_node;
class cvedix_json_stop_mqtt_broker_node;
#endif

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
// SSE broker node (doesn't require cereal)
#include <cvedix/nodes/broker/cvedix_sse_broker_node.h>
#ifdef CVEDIX_WITH_KAFKA
#include <cvedix/nodes/broker/cvedix_json_kafka_broker_node.h>
#endif

namespace fs = std::filesystem;



std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createJSONConsoleBrokerNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    // Parse broke_for enum
    std::string brokeForStr =
        params.count("broke_for") ? params.at("broke_for") : "NORMAL";
    cvedix_nodes::cvedix_broke_for brokeFor =
        cvedix_nodes::cvedix_broke_for::NORMAL;

    if (brokeForStr == "FACE") {
      brokeFor = cvedix_nodes::cvedix_broke_for::FACE;
    } else if (brokeForStr == "TEXT") {
      brokeFor = cvedix_nodes::cvedix_broke_for::TEXT;
    } else if (brokeForStr == "POSE") {
      brokeFor = cvedix_nodes::cvedix_broke_for::POSE;
    } else if (brokeForStr == "POSE") {
      brokeFor = cvedix_nodes::cvedix_broke_for::POSE;
    } else if (brokeForStr == "NORMAL") {
      brokeFor = cvedix_nodes::cvedix_broke_for::NORMAL;
    }

    int warnThreshold =
        params.count("broking_cache_warn_threshold")
            ? std::stoi(params.at("broking_cache_warn_threshold"))
            : 50;
    int ignoreThreshold =
        params.count("broking_cache_ignore_threshold")
            ? std::stoi(params.at("broking_cache_ignore_threshold"))
            : 200;

    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderBrokerNodes] Creating JSON console broker node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Broke for: " << brokeForStr << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_json_console_broker_node>(
        nodeName, brokeFor, warnThreshold, ignoreThreshold);

    std::cerr
        << "[PipelineBuilderBrokerNodes] ✓ JSON console broker node created successfully"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBrokerNodes] Exception in createJSONConsoleBrokerNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createJSONEnhancedConsoleBrokerNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string brokeForStr =
        params.count("broke_for") ? params.at("broke_for") : "NORMAL";
    cvedix_nodes::cvedix_broke_for brokeFor =
        cvedix_nodes::cvedix_broke_for::NORMAL;

    if (brokeForStr == "FACE") {
      brokeFor = cvedix_nodes::cvedix_broke_for::FACE;
    } else if (brokeForStr == "TEXT") {
      brokeFor = cvedix_nodes::cvedix_broke_for::TEXT;
    } else if (brokeForStr == "POSE") {
      brokeFor = cvedix_nodes::cvedix_broke_for::POSE;
    }

    int warnThreshold =
        params.count("broking_cache_warn_threshold")
            ? std::stoi(params.at("broking_cache_warn_threshold"))
            : 50;
    int ignoreThreshold =
        params.count("broking_cache_ignore_threshold")
            ? std::stoi(params.at("broking_cache_ignore_threshold"))
            : 200;
    bool encodeFullFrame = params.count("encode_full_frame")
                               ? (params.at("encode_full_frame") == "true" ||
                                  params.at("encode_full_frame") == "1")
                               : false;

    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderBrokerNodes] Creating JSON enhanced console broker node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Broke for: " << brokeForStr << std::endl;
    std::cerr << "  Encode full frame: " << (encodeFullFrame ? "true" : "false")
              << std::endl;

    auto node = std::make_shared<
        cvedix_nodes::cvedix_json_enhanced_console_broker_node>(
        nodeName, brokeFor, warnThreshold, ignoreThreshold, encodeFullFrame);

    std::cerr << "[PipelineBuilderBrokerNodes] ✓ JSON enhanced console broker node "
                 "created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBrokerNodes] Exception in "
                 "createJSONEnhancedConsoleBrokerNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createJSONMQTTBrokerNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    // Parse broke_for enum
    std::string brokeForStr =
        params.count("broke_for") ? params.at("broke_for") : "NORMAL";
    cvedix_nodes::cvedix_broke_for brokeFor =
        cvedix_nodes::cvedix_broke_for::NORMAL;

    if (brokeForStr == "FACE") {
      brokeFor = cvedix_nodes::cvedix_broke_for::FACE;
    } else if (brokeForStr == "TEXT") {
      brokeFor = cvedix_nodes::cvedix_broke_for::TEXT;
    } else if (brokeForStr == "POSE") {
      brokeFor = cvedix_nodes::cvedix_broke_for::POSE;
    } else if (brokeForStr == "NORMAL") {
      brokeFor = cvedix_nodes::cvedix_broke_for::NORMAL;
    }

    // Get MQTT configuration from additionalParams
    std::string mqtt_broker = "localhost";
    int mqtt_port = 1883;
    std::string mqtt_topic = "events";
    std::string mqtt_username = "";
    std::string mqtt_password = "";

    auto brokerIt = req.additionalParams.find("MQTT_BROKER_URL");
    if (brokerIt != req.additionalParams.end() && !brokerIt->second.empty()) {
      mqtt_broker = brokerIt->second;
    }

    auto portIt = req.additionalParams.find("MQTT_PORT");
    if (portIt != req.additionalParams.end() && !portIt->second.empty()) {
      try {
        mqtt_port = std::stoi(portIt->second);
      } catch (...) {
        std::cerr << "[PipelineBuilderBrokerNodes] Warning: Invalid MQTT_PORT, using "
                     "default 1883"
                  << std::endl;
      }
    }

    auto topicIt = req.additionalParams.find("MQTT_TOPIC");
    if (topicIt != req.additionalParams.end() && !topicIt->second.empty()) {
      mqtt_topic = topicIt->second;
    }

    auto usernameIt = req.additionalParams.find("MQTT_USERNAME");
    if (usernameIt != req.additionalParams.end()) {
      mqtt_username = usernameIt->second;
    }

    auto passwordIt = req.additionalParams.find("MQTT_PASSWORD");
    if (passwordIt != req.additionalParams.end()) {
      mqtt_password = passwordIt->second;
    }

    // Get thresholds
    int warnThreshold =
        params.count("broking_cache_warn_threshold")
            ? std::stoi(params.at("broking_cache_warn_threshold"))
            : 100;
    int ignoreThreshold =
        params.count("broking_cache_ignore_threshold")
            ? std::stoi(params.at("broking_cache_ignore_threshold"))
            : 500;
    // encode_full_frame parameter is not supported in this version
    // bool encodeFullFrame = params.count("encode_full_frame") ?
    // (params.at("encode_full_frame") == "true" ||
    // params.at("encode_full_frame") == "1") : false;

    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderBrokerNodes] Creating JSON MQTT broker node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Broker: " << mqtt_broker << ":" << mqtt_port << std::endl;
    std::cerr << "  Topic: " << mqtt_topic << std::endl;
    std::cerr << "  Broke for: " << brokeForStr << std::endl;

    // Create MQTT client using SDK (like in sample code)
    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] Initializing MQTT client for "
              << mqtt_broker << ":" << mqtt_port << std::endl;

    std::string client_id =
        "edgeos_api_" + nodeName + "_" + std::to_string(std::time(nullptr));
    auto mqtt_client = std::make_unique<cvedix_utils::cvedix_mqtt_client>(
        mqtt_broker, mqtt_port, client_id, 60);
    mqtt_client->set_auto_reconnect(true, 5000);

    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] Connecting to " << mqtt_broker << ":"
              << mqtt_port << "..." << std::endl;
    mqtt_client->connect(mqtt_username, mqtt_password);

    // Create MQTT publish function using SDK (like in sample code)
    static std::mutex mqtt_publish_mutex;
    auto mqtt_client_ptr = std::shared_ptr<cvedix_utils::cvedix_mqtt_client>(
        mqtt_client.release());

    auto mqtt_publish_func = [mqtt_client_ptr,
                              mqtt_topic](const std::string &json_message) {
      std::lock_guard<std::mutex> lock(mqtt_publish_mutex);
      if (mqtt_client_ptr && mqtt_client_ptr->is_ready()) {
        mqtt_client_ptr->publish(mqtt_topic, json_message, 1, false);
      }
    };

    // OLD CODE REMOVED - NonBlockingMQTTPublisher struct removed, using SDK
    // directly

    // Create MQTT broker node
    // Note: CVEDIX SDK only has cvedix_json_mqtt_broker_node (not enhanced
    // version) encodeFullFrame is not supported in this version
    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] Creating broker node with callback "
                 "function..."
              << std::endl;
    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] Broke for: " << brokeForStr
              << ", Warn threshold: " << warnThreshold
              << ", Ignore threshold: " << ignoreThreshold << std::endl;
    auto node = std::make_shared<cvedix_nodes::cvedix_json_mqtt_broker_node>(
        nodeName, brokeFor, warnThreshold, ignoreThreshold,
        nullptr, // json_transformer (nullptr = use original JSON)
        mqtt_publish_func);

    std::cerr
        << "[PipelineBuilderBrokerNodes] ✓ JSON MQTT broker node created successfully"
        << std::endl;
    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] Node will publish to topic: '"
              << mqtt_topic << "'" << std::endl;
    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] NOTE: Callback will be called when "
                 "json_mqtt_broker_node receives data"
              << std::endl;
    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] NOTE: If no messages appear, check:"
              << std::endl;
    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT]   1. Upstream node (ba_crossline) "
                 "is outputting data"
              << std::endl;
    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT]   2. broke_for parameter matches "
                 "data type (NORMAL for detection/BA events)"
              << std::endl;
    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT]   3. Pipeline is running and "
                 "processing frames"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBrokerNodes] Exception in createJSONMQTTBrokerNode: "
              << e.what() << std::endl;
    throw;
  }
}

// TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createJSONCrosslineMQTTBrokerNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req, const std::string &instanceId) {
  
  // TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
  throw std::runtime_error("JSON Crossline MQTT broker node is temporarily disabled due to CVEDIX SDK cereal/rapidxml issue");
  
  /* DISABLED CODE - cereal/rapidxml issue
  (void)params; // Parameters may be used in future implementations

  try {
    // instanceId is the actual UUID of the instance
    // req.name is the display name of the instance
    std::string instance_id = instanceId;
    std::string instance_name = req.name.empty() ? instanceId : req.name;

    // Get zone configuration from additionalParams (with defaults)
    std::string zone_id = "default_zone";
    std::string zone_name = "CrosslineZone";

    auto zoneIdIt = req.additionalParams.find("ZONE_ID");
    if (zoneIdIt != req.additionalParams.end() && !zoneIdIt->second.empty()) {
      zone_id = zoneIdIt->second;
    }

    auto zoneNameIt = req.additionalParams.find("ZONE_NAME");
    if (zoneNameIt != req.additionalParams.end() &&
        !zoneNameIt->second.empty()) {
      zone_name = zoneNameIt->second;
    }

    // Get MQTT configuration from additionalParams
    std::string mqtt_broker = "";
    int mqtt_port = 1883;
    std::string mqtt_topic = "events";
    std::string mqtt_username = "";
    std::string mqtt_password = "";

    auto brokerIt = req.additionalParams.find("MQTT_BROKER_URL");
    if (brokerIt != req.additionalParams.end() && !brokerIt->second.empty()) {
      mqtt_broker = brokerIt->second;
      // Trim whitespace
      mqtt_broker.erase(0, mqtt_broker.find_first_not_of(" \t\n\r"));
      mqtt_broker.erase(mqtt_broker.find_last_not_of(" \t\n\r") + 1);
    }

    auto portIt = req.additionalParams.find("MQTT_PORT");
    if (portIt != req.additionalParams.end() && !portIt->second.empty()) {
      try {
        mqtt_port = std::stoi(portIt->second);
      } catch (...) {
        std::cerr << "[PipelineBuilderBrokerNodes] Warning: Invalid MQTT_PORT, using "
                     "default 1883"
                  << std::endl;
      }
    }

    auto topicIt = req.additionalParams.find("MQTT_TOPIC");
    if (topicIt != req.additionalParams.end() && !topicIt->second.empty()) {
      mqtt_topic = topicIt->second;
    }

    auto usernameIt = req.additionalParams.find("MQTT_USERNAME");
    if (usernameIt != req.additionalParams.end()) {
      mqtt_username = usernameIt->second;
    }

    auto passwordIt = req.additionalParams.find("MQTT_PASSWORD");
    if (passwordIt != req.additionalParams.end()) {
      mqtt_password = passwordIt->second;
    }

    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    // If MQTT broker URL is empty, skip this node (optional MQTT output)
    if (mqtt_broker.empty()) {
      std::cerr << "[PipelineBuilderBrokerNodes] MQTT broker URL is empty, skipping "
                   "crossline MQTT broker node: "
                << nodeName << std::endl;
      std::cerr << "[PipelineBuilderBrokerNodes] NOTE: To enable MQTT output, provide "
                   "'MQTT_BROKER_URL' in request additionalParams"
                << std::endl;
      return nullptr;
    }

    std::cerr << "[PipelineBuilderBrokerNodes] Creating JSON Crossline MQTT broker node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Instance ID (UUID): '" << instance_id << "'" << std::endl;
    std::cerr << "  Instance Name: '" << instance_name << "'" << std::endl;
    std::cerr << "  Zone ID: '" << zone_id << "'" << std::endl;
    std::cerr << "  Zone Name: '" << zone_name << "'" << std::endl;
    std::cerr << "  Broker: " << mqtt_broker << ":" << mqtt_port << std::endl;
    std::cerr << "  Topic: " << mqtt_topic << std::endl;

    // Create MQTT client using SDK (like in sample code)
    // Use instance_id (UUID) for client ID: edgeos_api_{instance_id}
    std::string client_id = "edgeos_api_" + instance_id;
    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] Client ID: '" << client_id << "'"
              << std::endl;
    auto mqtt_client = std::make_unique<cvedix_utils::cvedix_mqtt_client>(
        mqtt_broker, mqtt_port, client_id, 60);
    mqtt_client->set_auto_reconnect(true, 5000);

    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] Connecting to " << mqtt_broker << ":"
              << mqtt_port << "..." << std::endl;
    bool connected = mqtt_client->connect(mqtt_username, mqtt_password);
    if (connected) {
      std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] ✓ Connected successfully"
                << std::endl;
    } else {
      std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] ⚠ Connection failed or pending "
                   "(will retry with auto-reconnect)"
                << std::endl;
      std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] NOTE: Auto-reconnect is enabled, "
                   "connection will be retried automatically"
                << std::endl;
    }

    // Create MQTT publish function using SDK (like in sample code)
    static std::mutex mqtt_publish_mutex;
    auto mqtt_client_ptr = std::shared_ptr<cvedix_utils::cvedix_mqtt_client>(
        mqtt_client.release());

    auto mqtt_publish_func = [mqtt_client_ptr,
                              mqtt_topic](const std::string &json_message) {
      std::lock_guard<std::mutex> lock(mqtt_publish_mutex);
      if (!mqtt_client_ptr) {
        std::cerr << "[MQTT] ⚠ Cannot publish: MQTT client is null"
                  << std::endl;
        return;
      }
      if (!mqtt_client_ptr->is_ready()) {
        std::cerr << "[MQTT] ⚠ Cannot publish: MQTT client not ready (not "
                     "connected yet)"
                  << std::endl;
        return;
      }
      mqtt_client_ptr->publish(mqtt_topic, json_message, 1, false);
      std::cerr << "[MQTT] ✓ Published crossline event to topic: " << mqtt_topic
                << std::endl;
    };

    // Get CrossingLines config to pass to broker node
    std::string crossing_lines_json = "";
    auto crossingLinesIt = req.additionalParams.find("CrossingLines");
    if (crossingLinesIt != req.additionalParams.end() &&
        !crossingLinesIt->second.empty()) {
      crossing_lines_json = crossingLinesIt->second;
    }

    // Create crossline MQTT broker node
    // Note: cvedix_json_crossline_mqtt_broker_node is defined in pipeline_builder.cpp
    // TODO: Move class definition to a separate header file so it can be included here
    // For now, return nullptr as a workaround - the proper solution is to move the class to a header file
    std::cerr << "[PipelineBuilderBrokerNodes] ⚠ Custom MQTT broker nodes require full class definitions. "
                 "Returning nullptr. TODO: Move class definitions to a header file."
              << std::endl;
    return nullptr;
    
    // auto node = std::make_shared<cvedix_json_crossline_mqtt_broker_node>(
    //     nodeName, mqtt_publish_func, instance_id, instance_name, zone_id,
    //     zone_name, crossing_lines_json);

    // std::cerr << "[PipelineBuilderBrokerNodes] ✓ JSON Crossline MQTT broker node created "
    //              "successfully"
    //           << std::endl;
    // std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] Node will publish crossline events "
    //              "to topic: '"
    //           << mqtt_topic << "'" << std::endl;
    // return node;
  } catch (const std::exception &e) {
    std::cerr
        << "[PipelineBuilderBrokerNodes] Exception in createJSONCrosslineMQTTBrokerNode: "
        << e.what() << std::endl;
    throw;
  }
}

// TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createJSONJamMQTTBrokerNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req, const std::string &instanceId) {
  
  // TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
  throw std::runtime_error("JSON Jam MQTT broker node is temporarily disabled due to CVEDIX SDK cereal/rapidxml issue");
  
  /* DISABLED CODE - cereal/rapidxml issue
  (void)params; // Parameters may be used in future implementations

  try {
    // instanceId is the actual UUID of the instance
    // req.name is the display name of the instance
    std::string instance_id = instanceId;
    std::string instance_name = req.name.empty() ? instanceId : req.name;

    // Get zone configuration from additionalParams (with defaults)
    std::string zone_id = "default_zone";
    std::string zone_name = "JamZone";

    auto zoneIdIt = req.additionalParams.find("ZONE_ID");
    if (zoneIdIt != req.additionalParams.end() && !zoneIdIt->second.empty()) {
      zone_id = zoneIdIt->second;
    }

    auto zoneNameIt = req.additionalParams.find("ZONE_NAME");
    if (zoneNameIt != req.additionalParams.end() &&
        !zoneNameIt->second.empty()) {
      zone_name = zoneNameIt->second;
    }

    // Get MQTT configuration from additionalParams
    std::string mqtt_broker = "";
    int mqtt_port = 1883;
    std::string mqtt_topic = "events";
    std::string mqtt_username = "";
    std::string mqtt_password = "";

    auto brokerIt = req.additionalParams.find("MQTT_BROKER_URL");
    if (brokerIt != req.additionalParams.end() && !brokerIt->second.empty()) {
      mqtt_broker = brokerIt->second;
      // Trim whitespace
      mqtt_broker.erase(0, mqtt_broker.find_first_not_of(" \t\n\r"));
      mqtt_broker.erase(mqtt_broker.find_last_not_of(" \t\n\r") + 1);
    }

    auto portIt = req.additionalParams.find("MQTT_PORT");
    if (portIt != req.additionalParams.end() && !portIt->second.empty()) {
      try {
        mqtt_port = std::stoi(portIt->second);
      } catch (...) {
        std::cerr << "[PipelineBuilderBrokerNodes] Warning: Invalid MQTT_PORT, using "
                     "default 1883"
                  << std::endl;
      }
    }

    auto topicIt = req.additionalParams.find("MQTT_TOPIC");
    if (topicIt != req.additionalParams.end() && !topicIt->second.empty()) {
      mqtt_topic = topicIt->second;
    }

    auto usernameIt = req.additionalParams.find("MQTT_USERNAME");
    if (usernameIt != req.additionalParams.end()) {
      mqtt_username = usernameIt->second;
    }

    auto passwordIt = req.additionalParams.find("MQTT_PASSWORD");
    if (passwordIt != req.additionalParams.end()) {
      mqtt_password = passwordIt->second;
    }

    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    // If MQTT broker URL is empty, skip this node (optional MQTT output)
    if (mqtt_broker.empty()) {
      std::cerr << "[PipelineBuilderBrokerNodes] MQTT broker URL is empty, skipping "
                   "jam MQTT broker node: "
                << nodeName << std::endl;
      std::cerr << "[PipelineBuilderBrokerNodes] NOTE: To enable MQTT output, provide "
                   "'MQTT_BROKER_URL' in request additionalParams"
                << std::endl;
      return nullptr;
    }

    std::cerr << "[PipelineBuilderBrokerNodes] Creating JSON Jam MQTT broker node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Instance ID (UUID): '" << instance_id << "'" << std::endl;
    std::cerr << "  Instance Name: '" << instance_name << "'" << std::endl;
    std::cerr << "  Zone ID: '" << zone_id << "'" << std::endl;
    std::cerr << "  Zone Name: '" << zone_name << "'" << std::endl;
    std::cerr << "  Broker: " << mqtt_broker << ":" << mqtt_port << std::endl;
    std::cerr << "  Topic: " << mqtt_topic << std::endl;

    // Create MQTT client using SDK (like in sample code)
    // Use instance_id (UUID) for client ID: edgeos_api_{instance_id}
    std::string client_id = "edgeos_api_" + instance_id;
    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] Client ID: '" << client_id << "'"
              << std::endl;
    auto mqtt_client = std::make_unique<cvedix_utils::cvedix_mqtt_client>(
        mqtt_broker, mqtt_port, client_id, 60);
    mqtt_client->set_auto_reconnect(true, 5000);

    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] Connecting to " << mqtt_broker << ":"
              << mqtt_port << "..." << std::endl;
    bool connected = mqtt_client->connect(mqtt_username, mqtt_password);
    if (connected) {
      std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] ✓ Connected successfully"
                << std::endl;
    } else {
      std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] ⚠ Connection failed or pending "
                   "(will retry with auto-reconnect)"
                << std::endl;
      std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] NOTE: Auto-reconnect is enabled, "
                   "connection will be retried automatically"
                << std::endl;
    }

    // Create MQTT publish function using SDK (like in sample code)
    static std::mutex mqtt_publish_mutex;
    auto mqtt_client_ptr = std::shared_ptr<cvedix_utils::cvedix_mqtt_client>(
        mqtt_client.release());

    auto mqtt_publish_func = [mqtt_client_ptr,
                              mqtt_topic](const std::string &json_message) {
      std::lock_guard<std::mutex> lock(mqtt_publish_mutex);
      if (!mqtt_client_ptr) {
        std::cerr << "[MQTT] ⚠ Cannot publish: MQTT client is null"
                  << std::endl;
        return;
      }
      if (!mqtt_client_ptr->is_ready()) {
        std::cerr << "[MQTT] ⚠ Cannot publish: MQTT client not ready (not "
                     "connected yet)"
                  << std::endl;
        return;
      }
      mqtt_client_ptr->publish(mqtt_topic, json_message, 1, false);
      std::cerr << "[MQTT] ✓ Published jam event to topic: " << mqtt_topic
                << std::endl;
    };

    // Get CrossingLines config to pass to broker node
    std::string crossing_lines_json = "";
    auto crossingLinesIt = req.additionalParams.find("CrossingLines");
    if (crossingLinesIt != req.additionalParams.end() &&
        !crossingLinesIt->second.empty()) {
      crossing_lines_json = crossingLinesIt->second;
    }

    // Create jam MQTT broker node
    // Note: cvedix_json_jam_mqtt_broker_node is defined in pipeline_builder.cpp
    // TODO: Move class definition to a separate header file so it can be included here
    // For now, return nullptr as a workaround - the proper solution is to move the class to a header file
    std::cerr << "[PipelineBuilderBrokerNodes] ⚠ Custom MQTT broker nodes require full class definitions. "
                 "Returning nullptr. TODO: Move class definitions to a header file."
              << std::endl;
    return nullptr;
    
    // auto node = std::make_shared<cvedix_json_jam_mqtt_broker_node>(
    //     nodeName, mqtt_publish_func, instance_id, instance_name, zone_id,
    //     zone_name, crossing_lines_json);

    // std::cerr << "[PipelineBuilderBrokerNodes] ✓ JSON Jam MQTT broker node created "
    //              "successfully"
    //           << std::endl;
    // std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] Node will publish jam events "
    //              "to topic: '"
    //           << mqtt_topic << "'" << std::endl;
    // return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBrokerNodes] Exception in createJSONJamMQTTBrokerNode: "
              << e.what() << std::endl;
    throw;
  }
  */
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createJSONStopMQTTBrokerNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req, const std::string &instanceId) {
  (void)params; // Parameters may be used in future implementations

  try {
    // instanceId is the actual UUID of the instance
    // req.name is the display name of the instance
    std::string instance_id = instanceId;
    std::string instance_name = req.name.empty() ? instanceId : req.name;

    // Get zone configuration from additionalParams (with defaults)
    std::string zone_id = "default_zone";
    std::string zone_name = "StopZone";

    auto zoneIdIt = req.additionalParams.find("ZONE_ID");
    if (zoneIdIt != req.additionalParams.end() && !zoneIdIt->second.empty()) {
      zone_id = zoneIdIt->second;
    }

    auto zoneNameIt = req.additionalParams.find("ZONE_NAME");
    if (zoneNameIt != req.additionalParams.end() &&
        !zoneNameIt->second.empty()) {
      zone_name = zoneNameIt->second;
    }

    // Get MQTT configuration from additionalParams
    std::string mqtt_broker = "";
    int mqtt_port = 1883;
    std::string mqtt_topic = "events";
    std::string mqtt_username = "";
    std::string mqtt_password = "";

    auto brokerIt = req.additionalParams.find("MQTT_BROKER_URL");
    if (brokerIt != req.additionalParams.end() && !brokerIt->second.empty()) {
      mqtt_broker = brokerIt->second;
      // Trim whitespace
      mqtt_broker.erase(0, mqtt_broker.find_first_not_of(" \t\n\r"));
      mqtt_broker.erase(mqtt_broker.find_last_not_of(" \t\n\r") + 1);
    }

    auto portIt = req.additionalParams.find("MQTT_PORT");
    if (portIt != req.additionalParams.end() && !portIt->second.empty()) {
      try {
        mqtt_port = std::stoi(portIt->second);
      } catch (...) {
        std::cerr << "[PipelineBuilderBrokerNodes] Warning: Invalid MQTT_PORT, using "
                     "default 1883"
                  << std::endl;
      }
    }

    auto topicIt = req.additionalParams.find("MQTT_TOPIC");
    if (topicIt != req.additionalParams.end() && !topicIt->second.empty()) {
      mqtt_topic = topicIt->second;
    }

    auto usernameIt = req.additionalParams.find("MQTT_USERNAME");
    if (usernameIt != req.additionalParams.end()) {
      mqtt_username = usernameIt->second;
    }

    auto passwordIt = req.additionalParams.find("MQTT_PASSWORD");
    if (passwordIt != req.additionalParams.end()) {
      mqtt_password = passwordIt->second;
    }

    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    // If MQTT broker URL is empty, skip this node (optional MQTT output)
    if (mqtt_broker.empty()) {
      std::cerr << "[PipelineBuilderBrokerNodes] MQTT broker URL is empty, skipping "
                   "stop MQTT broker node: "
                << nodeName << std::endl;
      std::cerr << "[PipelineBuilderBrokerNodes] NOTE: To enable MQTT output, provide "
                   "'MQTT_BROKER_URL' in request additionalParams"
                << std::endl;
      return nullptr;
    }

    std::cerr << "[PipelineBuilderBrokerNodes] Creating JSON Stop MQTT broker node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Instance ID (UUID): '" << instance_id << "'" << std::endl;
    std::cerr << "  Instance Name: '" << instance_name << "'" << std::endl;
    std::cerr << "  Zone ID: '" << zone_id << "'" << std::endl;
    std::cerr << "  Zone Name: '" << zone_name << "'" << std::endl;
    std::cerr << "  Broker: " << mqtt_broker << ":" << mqtt_port << std::endl;
    std::cerr << "  Topic: " << mqtt_topic << std::endl;

    // Create MQTT client using SDK (like in sample code)
    // Use instance_id (UUID) for client ID: edgeos_api_{instance_id}
    std::string client_id = "edgeos_api_" + instance_id;
    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] Client ID: '" << client_id << "'"
              << std::endl;
    auto mqtt_client = std::make_unique<cvedix_utils::cvedix_mqtt_client>(
        mqtt_broker, mqtt_port, client_id, 60);
    mqtt_client->set_auto_reconnect(true, 5000);

    std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] Connecting to " << mqtt_broker << ":"
              << mqtt_port << "..." << std::endl;
    bool connected = mqtt_client->connect(mqtt_username, mqtt_password);
    if (connected) {
      std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] ✓ Connected successfully"
                << std::endl;
    } else {
      std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] ⚠ Connection failed or pending "
                   "(will retry with auto-reconnect)"
                << std::endl;
      std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] NOTE: Auto-reconnect is enabled, "
                   "connection will be retried automatically"
                << std::endl;
    }

    // Create MQTT publish function using SDK (like in sample code)
    static std::mutex mqtt_publish_mutex;
    auto mqtt_client_ptr = std::shared_ptr<cvedix_utils::cvedix_mqtt_client>(
        mqtt_client.release());

    auto mqtt_publish_func = [mqtt_client_ptr,
                              mqtt_topic](const std::string &json_message) {
      std::lock_guard<std::mutex> lock(mqtt_publish_mutex);
      if (!mqtt_client_ptr) {
        std::cerr << "[MQTT] ⚠ Cannot publish: MQTT client is null"
                  << std::endl;
        return;
      }
      if (!mqtt_client_ptr->is_ready()) {
        std::cerr << "[MQTT] ⚠ Cannot publish: MQTT client not ready (not "
                     "connected yet)"
                  << std::endl;
        return;
      }
      mqtt_client_ptr->publish(mqtt_topic, json_message, 1, false);
      std::cerr << "[MQTT] ✓ Published stop event to topic: " << mqtt_topic
                << std::endl;
    };

    // Get CrossingLines config to pass to broker node
    std::string crossing_lines_json = "";
    auto crossingLinesIt = req.additionalParams.find("CrossingLines");
    if (crossingLinesIt != req.additionalParams.end() &&
        !crossingLinesIt->second.empty()) {
      crossing_lines_json = crossingLinesIt->second;
    }

    // Create stop MQTT broker node
    // Note: cvedix_json_stop_mqtt_broker_node is defined in pipeline_builder.cpp
    // TODO: Move class definition to a separate header file so it can be included here
    // For now, return nullptr as a workaround - the proper solution is to move the class to a header file
    std::cerr << "[PipelineBuilderBrokerNodes] ⚠ Custom MQTT broker nodes require full class definitions. "
                 "Returning nullptr. TODO: Move class definitions to a header file."
              << std::endl;
    return nullptr;
    
    // auto node = std::make_shared<cvedix_json_stop_mqtt_broker_node>(
    //     nodeName, mqtt_publish_func, instance_id, instance_name, zone_id,
    //     zone_name, crossing_lines_json);

    // std::cerr << "[PipelineBuilderBrokerNodes] ✓ JSON Stop MQTT broker node created "
    //              "successfully"
    //           << std::endl;
    // std::cerr << "[PipelineBuilderBrokerNodes] [MQTT] Node will publish stop events "
    //              "to topic: '"
    //           << mqtt_topic << "'" << std::endl;
    // return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBrokerNodes] Exception in createJSONStopMQTTBrokerNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createJSONKafkaBrokerNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string kafkaServers = params.count("kafka_servers")
                                   ? params.at("kafka_servers")
                                   : "127.0.0.1:9092";
    std::string topicName = params.count("topic_name") ? params.at("topic_name")
                                                       : "videopipe_topic";
    std::string brokeForStr =
        params.count("broke_for") ? params.at("broke_for") : "NORMAL";
    cvedix_nodes::cvedix_broke_for brokeFor =
        cvedix_nodes::cvedix_broke_for::NORMAL;

    // Get from additionalParams if not in params
    if (params.count("kafka_servers") == 0) {
      auto it = req.additionalParams.find("KAFKA_SERVERS");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        kafkaServers = it->second;
      }
    }
    if (params.count("topic_name") == 0) {
      auto it = req.additionalParams.find("KAFKA_TOPIC");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        topicName = it->second;
      }
    }

    if (brokeForStr == "FACE") {
      brokeFor = cvedix_nodes::cvedix_broke_for::FACE;
    } else if (brokeForStr == "TEXT") {
      brokeFor = cvedix_nodes::cvedix_broke_for::TEXT;
    } else if (brokeForStr == "POSE") {
      brokeFor = cvedix_nodes::cvedix_broke_for::POSE;
    }

    int warnThreshold =
        params.count("broking_cache_warn_threshold")
            ? std::stoi(params.at("broking_cache_warn_threshold"))
            : 50;
    int ignoreThreshold =
        params.count("broking_cache_ignore_threshold")
            ? std::stoi(params.at("broking_cache_ignore_threshold"))
            : 200;

    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    // Check if Kafka is disabled via environment variable (for tests)
    const char *disable_kafka = std::getenv("DISABLE_KAFKA");
    if (disable_kafka && (std::string(disable_kafka) == "1" || std::string(disable_kafka) == "true")) {
      std::cerr << "[PipelineBuilderBrokerNodes] Kafka is disabled via DISABLE_KAFKA environment variable"
                << std::endl;
      std::cerr << "[PipelineBuilderBrokerNodes] Skipping JSON Kafka broker node creation"
                << std::endl;
      return nullptr;
    }

    std::cerr << "[PipelineBuilderBrokerNodes] Creating JSON Kafka broker node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Kafka servers: '" << kafkaServers << "'" << std::endl;
    std::cerr << "  Topic name: '" << topicName << "'" << std::endl;
    std::cerr << "  Broke for: " << brokeForStr << std::endl;

#ifdef CVEDIX_WITH_KAFKA
    auto node = std::make_shared<cvedix_nodes::cvedix_json_kafka_broker_node>(
        nodeName, kafkaServers, topicName, brokeFor, warnThreshold,
        ignoreThreshold);

    std::cerr << "[PipelineBuilderBrokerNodes] ✓ JSON Kafka broker node created successfully"
        << std::endl;
    return node;
#else
    std::cerr << "[PipelineBuilderBrokerNodes] JSON Kafka broker node requires "
                 "CVEDIX_WITH_KAFKA to be enabled"
              << std::endl;
    return nullptr;
#endif
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBrokerNodes] Exception in createJSONKafkaBrokerNode: "
              << e.what() << std::endl;
    throw;
  }
}

// TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createXMLFileBrokerNode( const std::string& nodeName, const
std::map<std::string, std::string>& params, const CreateInstanceRequest& req) {

  // TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
  throw std::runtime_error("XML file broker node is temporarily disabled due to CVEDIX SDK cereal/rapidxml issue");
  
  /* DISABLED CODE - cereal/rapidxml issue
    try {
        std::string brokeForStr = params.count("broke_for") ? params.at("broke_for") : "NORMAL"; 
        cvedix_nodes::cvedix_broke_for brokeFor = cvedix_nodes::cvedix_broke_for::NORMAL;

        if (brokeForStr == "FACE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::FACE;
        } else if (brokeForStr == "TEXT") {
            brokeFor = cvedix_nodes::cvedix_broke_for::TEXT;
        } else if (brokeForStr == "POSE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::POSE;
        }

        std::string filePath = params.count("file_path") ? params.at("file_path") : ""; 
        if (filePath.empty()) { 
            auto it = req.additionalParams.find("XML_FILE_PATH"); 
            if (it != req.additionalParams.end() && !it->second.empty()) { 
                filePath = it->second; 
            } else {
                // Default path
                filePath = "./output/broker_output.xml";
            }
        }

        int warnThreshold = params.count("broking_cache_warn_threshold") ?
std::stoi(params.at("broking_cache_warn_threshold")) : 50; 
        int ignoreThreshold = params.count("broking_cache_ignore_threshold") ?
std::stoi(params.at("broking_cache_ignore_threshold")) : 200;

        if (nodeName.empty()) {
            throw std::invalid_argument("Node name cannot be empty");
        }

        std::cerr << "[PipelineBuilderBrokerNodes] Creating XML file broker node:" << std::endl;
        std::cerr << "  Name: '" << nodeName << "'" << std::endl;
        std::cerr << "  File path: '" << filePath << "'" << std::endl;
        std::cerr << "  Broke for: " << brokeForStr << std::endl;

        auto node = std::make_shared<cvedix_nodes::cvedix_xml_file_broker_node>(
            nodeName,
            brokeFor,
            filePath,
            warnThreshold,
            ignoreThreshold
        );

        std::cerr << "[PipelineBuilderBrokerNodes] ✓ XML file broker node created successfully" << std::endl;
        return node;
    } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBrokerNodes] Exception in createXMLFileBrokerNode: "
<< e.what() << std::endl; 
        throw;
    }
    */
}

// TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createXMLSocketBrokerNode( const std::string& nodeName, const
std::map<std::string, std::string>& params, const CreateInstanceRequest& req) {

  // TEMPORARILY DISABLED: cereal/rapidxml macro conflict issue in CVEDIX SDK
  throw std::runtime_error("XML socket broker node is temporarily disabled due to CVEDIX SDK cereal/rapidxml issue");
  
  /* DISABLED CODE - cereal/rapidxml issue
    try {
        std::string desIp = params.count("des_ip") ? params.at("des_ip") : "";
        int desPort = params.count("des_port") ?
std::stoi(params.at("des_port")) : 0; std::string brokeForStr =
params.count("broke_for") ? params.at("broke_for") : "NORMAL";
        cvedix_nodes::cvedix_broke_for brokeFor =
cvedix_nodes::cvedix_broke_for::NORMAL;

        // Get from additionalParams if not in params
        if (desIp.empty()) {
            auto it = req.additionalParams.find("XML_SOCKET_IP");
            if (it != req.additionalParams.end() && !it->second.empty()) {
                desIp = it->second;
            } else {
                // Default fake data
                desIp = "127.0.0.1";
            }
        }
        if (desPort == 0) {
            auto it = req.additionalParams.find("XML_SOCKET_PORT");
            if (it != req.additionalParams.end() && !it->second.empty()) {
                try {
                    desPort = std::stoi(it->second);
                } catch (...) {
                    desPort = 8080; // Default fake data
                }
            } else {
                desPort = 8080; // Default fake data
            }
        }

        if (brokeForStr == "FACE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::FACE;
        } else if (brokeForStr == "TEXT") {
            brokeFor = cvedix_nodes::cvedix_broke_for::TEXT;
        } else if (brokeForStr == "POSE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::POSE;
        }

        int warnThreshold = params.count("broking_cache_warn_threshold") ?
std::stoi(params.at("broking_cache_warn_threshold")) : 50; int ignoreThreshold =
params.count("broking_cache_ignore_threshold") ?
std::stoi(params.at("broking_cache_ignore_threshold")) : 200;

        if (nodeName.empty()) {
            throw std::invalid_argument("Node name cannot be empty");
        }

        std::cerr << "[PipelineBuilderBrokerNodes] Creating XML socket broker node:" << std::endl;
        std::cerr << "  Name: '" << nodeName << "'" << std::endl;
        std::cerr << "  Destination IP: '" << desIp << "'" << std::endl;
        std::cerr << "  Destination port: " << desPort << std::endl;
        std::cerr << "  Broke for: " << brokeForStr << std::endl;

        auto node = std::make_shared<cvedix_nodes::cvedix_xml_socket_broker_node>(
            nodeName, desIp, desPort, brokeFor, warnThreshold, ignoreThreshold
        );

        std::cerr << "[PipelineBuilderBrokerNodes] ✓ XML socket broker node created successfully" << std::endl;
        return node;
    } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBrokerNodes] Exception in createXMLSocketBrokerNode: " << e.what() << std::endl;
        throw;
    }
    */
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createMessageBrokerNode( const std::string& nodeName, const
std::map<std::string, std::string>& params, const CreateInstanceRequest& req) {

    try {
        std::string brokeForStr = params.count("broke_for") ?
params.at("broke_for") : "NORMAL"; cvedix_nodes::cvedix_broke_for brokeFor =
cvedix_nodes::cvedix_broke_for::NORMAL;

        if (brokeForStr == "FACE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::FACE;
        } else if (brokeForStr == "TEXT") {
            brokeFor = cvedix_nodes::cvedix_broke_for::TEXT;
        } else if (brokeForStr == "POSE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::POSE;
        }

        int warnThreshold = params.count("broking_cache_warn_threshold") ?
std::stoi(params.at("broking_cache_warn_threshold")) : 50; int ignoreThreshold =
params.count("broking_cache_ignore_threshold") ?
std::stoi(params.at("broking_cache_ignore_threshold")) : 200;

        if (nodeName.empty()) {
            throw std::invalid_argument("Node name cannot be empty");
        }

        std::cerr << "[PipelineBuilderBrokerNodes] Creating message broker node:" << std::endl;
        std::cerr << "  Name: '" << nodeName << "'" << std::endl;
        std::cerr << "  Broke for: " << brokeForStr << std::endl;

        // Note: cvedix_msg_broker_node is an abstract class and cannot be instantiated
        throw std::runtime_error("Message broker node is an abstract class and cannot be instantiated");
        
        // auto node = std::make_shared<cvedix_nodes::cvedix_msg_broker_node>(
        //     nodeName,
        //     brokeFor,
        //     warnThreshold,
        //     ignoreThreshold
        // );

        // std::cerr << "[PipelineBuilderBrokerNodes] ✓ Message broker node created successfully" << std::endl;
        // return node;
    } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBrokerNodes] Exception in createMessageBrokerNode: " << e.what() << std::endl;
        throw;
    }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createBASocketBrokerNode( const std::string& nodeName, const
std::map<std::string, std::string>& params, const CreateInstanceRequest& req) {

    try {
        std::string desIp = params.count("des_ip") ? params.at("des_ip") : "";
        int desPort = params.count("des_port") ?
std::stoi(params.at("des_port")) : 0; std::string brokeForStr =
params.count("broke_for") ? params.at("broke_for") : "NORMAL";
        cvedix_nodes::cvedix_broke_for brokeFor =
cvedix_nodes::cvedix_broke_for::NORMAL;

        // Get from additionalParams if not in params
        if (desIp.empty()) {
            auto it = req.additionalParams.find("BA_SOCKET_IP");
            if (it != req.additionalParams.end() && !it->second.empty()) {
                desIp = it->second;
            } else {
                desIp = "127.0.0.1"; // Default fake data
            }
        }
        if (desPort == 0) {
            auto it = req.additionalParams.find("BA_SOCKET_PORT");
            if (it != req.additionalParams.end() && !it->second.empty()) {
                try {
                    desPort = std::stoi(it->second);
                } catch (...) {
                    desPort = 8080; // Default fake data
                }
            } else {
                desPort = 8080; // Default fake data
            }
        }

        if (brokeForStr == "FACE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::FACE;
        } else if (brokeForStr == "TEXT") {
            brokeFor = cvedix_nodes::cvedix_broke_for::TEXT;
        } else if (brokeForStr == "POSE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::POSE;
        }

        int warnThreshold = params.count("broking_cache_warn_threshold") ?
std::stoi(params.at("broking_cache_warn_threshold")) : 50; int ignoreThreshold =
params.count("broking_cache_ignore_threshold") ?
std::stoi(params.at("broking_cache_ignore_threshold")) : 200;

        if (nodeName.empty()) {
            throw std::invalid_argument("Node name cannot be empty");
        }

        std::cerr << "[PipelineBuilderBrokerNodes] Creating BA socket broker node:" << std::endl;
        std::cerr << "  Name: '" << nodeName << "'" << std::endl;
        std::cerr << "  Destination IP: '" << desIp << "'" << std::endl;
        std::cerr << "  Destination port: " << desPort << std::endl;
        std::cerr << "  Broke for: " << brokeForStr << std::endl;

        auto node = std::make_shared<cvedix_nodes::cvedix_ba_socket_broker_node>(
            nodeName, desIp, desPort, brokeFor, warnThreshold, ignoreThreshold
        );

        std::cerr << "[PipelineBuilderBrokerNodes] ✓ BA socket broker node created successfully" << std::endl;
        return node;
    } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBrokerNodes] Exception in createBASocketBrokerNode: " << e.what() << std::endl;
        throw;
    }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createEmbeddingsSocketBrokerNode( const std::string& nodeName,
    const std::map<std::string, std::string>& params,
    const CreateInstanceRequest& req) {

    try {
        std::string desIp = params.count("des_ip") ? params.at("des_ip") : "";
        int desPort = params.count("des_port") ?
std::stoi(params.at("des_port")) : 0; std::string croppedDir =
params.count("cropped_dir") ? params.at("cropped_dir") : "cropped_images"; int
minCropWidth = params.count("min_crop_width") ?
std::stoi(params.at("min_crop_width")) : 50; int minCropHeight =
params.count("min_crop_height") ? std::stoi(params.at("min_crop_height")) : 50;
        std::string brokeForStr = params.count("broke_for") ?
params.at("broke_for") : "NORMAL"; cvedix_nodes::cvedix_broke_for brokeFor =
cvedix_nodes::cvedix_broke_for::NORMAL; bool onlyForTracked =
params.count("only_for_tracked") ? (params.at("only_for_tracked") == "true" ||
params.at("only_for_tracked") == "1") : false;

        // Get from additionalParams if not in params
        if (desIp.empty()) {
            auto it = req.additionalParams.find("EMBEDDINGS_SOCKET_IP");
            if (it != req.additionalParams.end() && !it->second.empty()) {
                desIp = it->second;
            } else {
                desIp = "127.0.0.1"; // Default fake data
            }
        }
        if (desPort == 0) {
            auto it = req.additionalParams.find("EMBEDDINGS_SOCKET_PORT");
            if (it != req.additionalParams.end() && !it->second.empty()) {
                try {
                    desPort = std::stoi(it->second);
                } catch (...) {
                    desPort = 8080; // Default fake data
                }
            } else {
                desPort = 8080; // Default fake data
            }
        }

        if (brokeForStr == "FACE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::FACE;
        } else if (brokeForStr == "TEXT") {
            brokeFor = cvedix_nodes::cvedix_broke_for::TEXT;
        } else if (brokeForStr == "POSE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::POSE;
        }

        int warnThreshold = params.count("broking_cache_warn_threshold") ?
std::stoi(params.at("broking_cache_warn_threshold")) : 50; int ignoreThreshold =
params.count("broking_cache_ignore_threshold") ?
std::stoi(params.at("broking_cache_ignore_threshold")) : 200;

        if (nodeName.empty()) {
            throw std::invalid_argument("Node name cannot be empty");
        }

        std::cerr << "[PipelineBuilderBrokerNodes] Creating embeddings socket broker node:" << std::endl;
        std::cerr << "  Name: '" << nodeName << "'" << std::endl;
        std::cerr << "  Destination IP: '" << desIp << "'" << std::endl;
        std::cerr << "  Destination port: " << desPort << std::endl;
        std::cerr << "  Cropped dir: '" << croppedDir << "'" << std::endl;

        auto node = std::make_shared<cvedix_nodes::cvedix_embeddings_socket_broker_node>(
            nodeName, desIp, desPort, croppedDir, minCropWidth, minCropHeight,
            brokeFor, onlyForTracked, warnThreshold, ignoreThreshold
        );

        std::cerr << "[PipelineBuilderBrokerNodes] ✓ Embeddings socket broker node created successfully" << std::endl;
        return node;
    } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBrokerNodes] Exception in createEmbeddingsSocketBrokerNode: " << e.what() << std::endl;
        throw;
    }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createEmbeddingsPropertiesSocketBrokerNode( const std::string&
nodeName, const std::map<std::string, std::string>& params, const
CreateInstanceRequest& req) {

    try {
        std::string desIp = params.count("des_ip") ? params.at("des_ip") : "";
        int desPort = params.count("des_port") ?
std::stoi(params.at("des_port")) : 0; std::string croppedDir =
params.count("cropped_dir") ? params.at("cropped_dir") : "cropped_images"; int
minCropWidth = params.count("min_crop_width") ?
std::stoi(params.at("min_crop_width")) : 50; int minCropHeight =
params.count("min_crop_height") ? std::stoi(params.at("min_crop_height")) : 50;
        std::string brokeForStr = params.count("broke_for") ?
params.at("broke_for") : "NORMAL"; cvedix_nodes::cvedix_broke_for brokeFor =
cvedix_nodes::cvedix_broke_for::NORMAL; bool onlyForTracked =
params.count("only_for_tracked") ? (params.at("only_for_tracked") == "true" ||
params.at("only_for_tracked") == "1") : false;

        // Get from additionalParams if not in params
        if (desIp.empty()) {
            auto it =
req.additionalParams.find("EMBEDDINGS_PROPERTIES_SOCKET_IP"); if (it !=
req.additionalParams.end() && !it->second.empty()) { desIp = it->second; } else
{ desIp = "127.0.0.1"; // Default fake data
            }
        }
        if (desPort == 0) {
            auto it =
req.additionalParams.find("EMBEDDINGS_PROPERTIES_SOCKET_PORT"); if (it !=
req.additionalParams.end() && !it->second.empty()) { try { desPort =
std::stoi(it->second); } catch (...) { desPort = 8080; // Default fake data
                }
            } else {
                desPort = 8080; // Default fake data
            }
        }

        if (brokeForStr == "FACE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::FACE;
        } else if (brokeForStr == "TEXT") {
            brokeFor = cvedix_nodes::cvedix_broke_for::TEXT;
        } else if (brokeForStr == "POSE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::POSE;
        }

        int warnThreshold = params.count("broking_cache_warn_threshold") ?
std::stoi(params.at("broking_cache_warn_threshold")) : 50; int ignoreThreshold =
params.count("broking_cache_ignore_threshold") ?
std::stoi(params.at("broking_cache_ignore_threshold")) : 200;

        if (nodeName.empty()) {
            throw std::invalid_argument("Node name cannot be empty");
        }

        std::cerr << "[PipelineBuilderBrokerNodes] Creating embeddings properties socket broker node:" << std::endl;
        std::cerr << "  Name: '" << nodeName << "'" << std::endl;
        std::cerr << "  Destination IP: '" << desIp << "'" << std::endl;
        std::cerr << "  Destination port: " << desPort << std::endl;

        auto node = std::make_shared<cvedix_nodes::cvedix_embeddings_properties_socket_broker_node>(
            nodeName, desIp, desPort, croppedDir, minCropWidth, minCropHeight,
            brokeFor, onlyForTracked, warnThreshold, ignoreThreshold
        );

        std::cerr << "[PipelineBuilderBrokerNodes] ✓ Embeddings properties socket broker node created successfully" << std::endl;
        return node;
    } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBrokerNodes] Exception in createEmbeddingsPropertiesSocketBrokerNode: " << e.what() << std::endl;
        throw;
    }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createPlateSocketBrokerNode( const std::string& nodeName, const
std::map<std::string, std::string>& params, const CreateInstanceRequest& req) {

    try {
        std::string desIp = params.count("des_ip") ? params.at("des_ip") : "";
        int desPort = params.count("des_port") ?
std::stoi(params.at("des_port")) : 0; std::string platesDir =
params.count("plates_dir") ? params.at("plates_dir") : "plate_images"; int
minCropWidth = params.count("min_crop_width") ?
std::stoi(params.at("min_crop_width")) : 100; int minCropHeight =
params.count("min_crop_height") ? std::stoi(params.at("min_crop_height")) : 0;
        std::string brokeForStr = params.count("broke_for") ?
params.at("broke_for") : "NORMAL"; cvedix_nodes::cvedix_broke_for brokeFor =
cvedix_nodes::cvedix_broke_for::NORMAL; bool onlyForTracked =
params.count("only_for_tracked") ? (params.at("only_for_tracked") == "true" ||
params.at("only_for_tracked") == "1") : true;

        // Get from additionalParams if not in params
        if (desIp.empty()) {
            auto it = req.additionalParams.find("PLATE_SOCKET_IP");
            if (it != req.additionalParams.end() && !it->second.empty()) {
                desIp = it->second;
            } else {
                desIp = "127.0.0.1"; // Default fake data
            }
        }
        if (desPort == 0) {
            auto it = req.additionalParams.find("PLATE_SOCKET_PORT");
            if (it != req.additionalParams.end() && !it->second.empty()) {
                try {
                    desPort = std::stoi(it->second);
                } catch (...) {
                    desPort = 8080; // Default fake data
                }
            } else {
                desPort = 8080; // Default fake data
            }
        }

        if (brokeForStr == "FACE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::FACE;
        } else if (brokeForStr == "TEXT") {
            brokeFor = cvedix_nodes::cvedix_broke_for::TEXT;
        } else if (brokeForStr == "POSE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::POSE;
        }

        int warnThreshold = params.count("broking_cache_warn_threshold") ?
std::stoi(params.at("broking_cache_warn_threshold")) : 50; int ignoreThreshold =
params.count("broking_cache_ignore_threshold") ?
std::stoi(params.at("broking_cache_ignore_threshold")) : 200;

        if (nodeName.empty()) {
            throw std::invalid_argument("Node name cannot be empty");
        }

        std::cerr << "[PipelineBuilderBrokerNodes] Creating plate socket broker node:" << std::endl;
        std::cerr << "  Name: '" << nodeName << "'" << std::endl;
        std::cerr << "  Destination IP: '" << desIp << "'" << std::endl;
        std::cerr << "  Destination port: " << desPort << std::endl;
        std::cerr << "  Plates dir: '" << platesDir << "'" << std::endl;

        auto node = std::make_shared<cvedix_nodes::cvedix_plate_socket_broker_node>(
            nodeName, desIp, desPort, platesDir, minCropWidth, minCropHeight,
            brokeFor, onlyForTracked, warnThreshold, ignoreThreshold
        );

        std::cerr << "[PipelineBuilderBrokerNodes] ✓ Plate socket broker node created successfully" << std::endl;
        return node;
    } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBrokerNodes] Exception in createPlateSocketBrokerNode: " << e.what() << std::endl;
        throw;
    }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createExpressionSocketBrokerNode( const std::string& nodeName,
    const std::map<std::string, std::string>& params,
    const CreateInstanceRequest& req) {

    try {
        std::string desIp = params.count("des_ip") ? params.at("des_ip") : "";
        int desPort = params.count("des_port") ? std::stoi(params.at("des_port")) : 0;
        std::string screenshotDir = params.count("screenshot_dir") ? params.at("screenshot_dir") : "screenshot_images";
        std::string brokeForStr = params.count("broke_for") ? params.at("broke_for") : "TEXT";
        cvedix_nodes::cvedix_broke_for brokeFor = cvedix_nodes::cvedix_broke_for::TEXT;

        // Get from additionalParams if not in params
        if (desIp.empty()) {
            auto it = req.additionalParams.find("EXPR_SOCKET_IP");
            if (it != req.additionalParams.end() && !it->second.empty()) {
                desIp = it->second;
            } else {
                desIp = "127.0.0.1"; // Default fake data
            }
        }
        if (desPort == 0) {
            auto it = req.additionalParams.find("EXPR_SOCKET_PORT");
            if (it != req.additionalParams.end() && !it->second.empty()) {
                try {
                    desPort = std::stoi(it->second);
                } catch (...) {
                    desPort = 8080; // Default fake data
                }
            } else {
                desPort = 8080; // Default fake data
            }
        }

        if (brokeForStr == "FACE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::FACE;
        } else if (brokeForStr == "TEXT") {
            brokeFor = cvedix_nodes::cvedix_broke_for::TEXT;
        } else if (brokeForStr == "POSE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::POSE;
        } else if (brokeForStr == "POSE") {
            brokeFor = cvedix_nodes::cvedix_broke_for::POSE;
        } else if (brokeForStr == "NORMAL") {
            brokeFor = cvedix_nodes::cvedix_broke_for::NORMAL;
        }

        int warnThreshold = params.count("broking_cache_warn_threshold") ?
std::stoi(params.at("broking_cache_warn_threshold")) : 50; int ignoreThreshold =
params.count("broking_cache_ignore_threshold") ?
std::stoi(params.at("broking_cache_ignore_threshold")) : 200;

        if (nodeName.empty()) {
            throw std::invalid_argument("Node name cannot be empty");
        }

        std::cerr << "[PipelineBuilderBrokerNodes] Creating expression socket broker node:" << std::endl;
        std::cerr << "  Name: '" << nodeName << "'" << std::endl;
        std::cerr << "  Destination IP: '" << desIp << "'" << std::endl;
        std::cerr << "  Destination port: " << desPort << std::endl;
        std::cerr << "  Screenshot dir: '" << screenshotDir << "'" << std::endl;

        auto node = std::make_shared<cvedix_nodes::cvedix_expr_socket_broker_node>(
            nodeName, desIp, desPort, screenshotDir, brokeFor, warnThreshold, ignoreThreshold);

        std::cerr << "[PipelineBuilderBrokerNodes] ✓ Expression socket broker node created successfully" << std::endl;
        return node;
    } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBrokerNodes] Exception in createExpressionSocketBrokerNode: " << e.what() << std::endl;
        throw;
    }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBrokerNodes::createSSEBrokerNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderBrokerNodes] Creating SSE broker node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;

    // Parse parameters with defaults
    cvedix_nodes::cvedix_broke_for broke_for = cvedix_nodes::cvedix_broke_for::NORMAL;
    int broking_cache_warn_threshold = 50;
    int broking_cache_ignore_threshold = 200;
    int port = 8090;
    std::string endpoint = "/events";

    // Parse broke_for (optional)
    if (params.count("broke_for")) {
      std::string broke_for_str = params.at("broke_for");
      if (broke_for_str == "FACE") {
        broke_for = cvedix_nodes::cvedix_broke_for::FACE;
      } else if (broke_for_str == "TEXT") {
        broke_for = cvedix_nodes::cvedix_broke_for::TEXT;
      } else if (broke_for_str == "POSE") {
        broke_for = cvedix_nodes::cvedix_broke_for::POSE;
      }
      // VEHICLE, PERSON and other values keep NORMAL (generic object detection)
      // Default to NORMAL
    }

    // Parse broking_cache_warn_threshold
    if (params.count("broking_cache_warn_threshold")) {
      try {
        broking_cache_warn_threshold = std::stoi(params.at("broking_cache_warn_threshold"));
      } catch (...) {
        std::cerr << "[PipelineBuilderBrokerNodes] Invalid broking_cache_warn_threshold, using default: 50" << std::endl;
      }
    }

    // Parse broking_cache_ignore_threshold
    if (params.count("broking_cache_ignore_threshold")) {
      try {
        broking_cache_ignore_threshold = std::stoi(params.at("broking_cache_ignore_threshold"));
      } catch (...) {
        std::cerr << "[PipelineBuilderBrokerNodes] Invalid broking_cache_ignore_threshold, using default: 200" << std::endl;
      }
    }

    // Parse port
    if (params.count("port")) {
      try {
        port = std::stoi(params.at("port"));
      } catch (...) {
        std::cerr << "[PipelineBuilderBrokerNodes] Invalid port, using default: 8090" << std::endl;
      }
    }

    // Parse endpoint
    if (params.count("endpoint")) {
      endpoint = params.at("endpoint");
    }

    std::cerr << "  Port: " << port << std::endl;
    std::cerr << "  Endpoint: '" << endpoint << "'" << std::endl;
    std::cerr << "  Broke for: " << static_cast<int>(broke_for) << std::endl;
    std::cerr << "  Cache warn threshold: " << broking_cache_warn_threshold << std::endl;
    std::cerr << "  Cache ignore threshold: " << broking_cache_ignore_threshold << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_sse_broker_node>(
        nodeName, broke_for, broking_cache_warn_threshold,
        broking_cache_ignore_threshold, port, endpoint);

    std::cerr << "[PipelineBuilderBrokerNodes] ✓ SSE broker node created successfully" << std::endl;
    std::cerr << "[PipelineBuilderBrokerNodes] SSE server will be available at: http://0.0.0.0:" << port << endpoint << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBrokerNodes] Exception in createSSEBrokerNode: "
              << e.what() << std::endl;
    throw;
  }
}