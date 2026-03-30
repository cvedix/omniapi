#pragma once

#include <json/json.h>
#include <mutex>
#include <string>
#include <unordered_map>

/**
 * @brief Per-instance logging enabled flag (runtime cache).
 * Persisted via instance config path "logging.enabled"; cache is updated by API and when instance config is loaded.
 */
namespace InstanceLoggingConfig {
/** @brief Set whether logging to instance-specific log dir is enabled for the given instance. */
void setEnabled(const std::string &instance_id, bool enabled);

/** @brief Get cached value; returns false if not set. */
bool isEnabled(const std::string &instance_id);

/** @brief Remove from cache (e.g. when instance is deleted). */
void remove(const std::string &instance_id);

/** @brief Load from instance config JSON (e.g. when instance is loaded); pass config["logging"] or empty. */
void loadFromConfig(const std::string &instance_id, const Json::Value &logging_section);
} // namespace InstanceLoggingConfig
