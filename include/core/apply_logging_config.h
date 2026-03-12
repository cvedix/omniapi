#pragma once

#include "config/system_config.h"

/**
 * @brief Apply logging configuration at runtime (category flags).
 *
 * Updates global logging flags (api_enabled, instance_enabled, sdk_output_enabled).
 * Call after loading config or when user updates log config via API.
 * Note: log_level change may require server restart to take full effect.
 */
void applyLoggingConfig(const SystemConfig::LoggingConfig &config);
