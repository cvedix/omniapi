#pragma once

// This file contains forward declarations for custom MQTT broker nodes
// The actual class definitions are in pipeline_builder.cpp
// TODO: Move full class definitions to this header file

#ifdef CVEDIX_WITH_MQTT
#include <cvedix/nodes/broker/cvedix_json_enhanced_console_broker_node.h>
#include <functional>
#include <map>
#include <mutex>
#include <string>

// Forward declarations
class cvedix_json_crossline_mqtt_broker_node;
class cvedix_json_jam_mqtt_broker_node;
class cvedix_json_stop_mqtt_broker_node;

#endif // CVEDIX_WITH_MQTT

