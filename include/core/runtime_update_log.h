#pragma once

#include <string>

/**
 * Append one line to runtime_update.log (dev/debug for PATCH instance flow).
 * Path: EDGE_AI_RUNTIME_UPDATE_LOG_DIR or LOG_DIR or /tmp.
 * Used by API (SubprocessInstanceManager) and worker (WorkerHandler).
 */
void logRuntimeUpdate(const std::string &instance_id,
                      const std::string &message);
