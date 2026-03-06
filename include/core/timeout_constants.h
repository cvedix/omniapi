#pragma once

#include "core/env_config.h"
#include <chrono>

/**
 * @brief Timeout constants for various operations
 *
 * These timeouts are configurable via environment variables.
 * All values are in milliseconds.
 */
namespace TimeoutConstants {

// Registry mutex lock timeout (for read operations)
// Must be >= API wrapper timeout to allow registry to timeout first
inline int getRegistryMutexTimeoutMs() {
  return EnvConfig::getInt("REGISTRY_MUTEX_TIMEOUT_MS", 2000, 100, 30000);
}

// API wrapper timeout for getInstance() calls
// Should be >= registry timeout + buffer (e.g., registry + 500ms)
inline int getApiWrapperTimeoutMs() {
  int registryTimeout = getRegistryMutexTimeoutMs();
  int apiTimeout = EnvConfig::getInt("API_WRAPPER_TIMEOUT_MS",
                                     registryTimeout + 500, 500, 60000);
  // Ensure API timeout is always >= registry timeout
  return std::max(apiTimeout, registryTimeout + 100);
}

// IPC timeout for start/stop/update operations
inline int getIpcStartStopTimeoutMs() {
  return EnvConfig::getInt("IPC_START_STOP_TIMEOUT_MS", 10000, 1000, 60000);
}

// IPC timeout for get statistics/frame operations (API calls)
inline int getIpcApiTimeoutMs() {
  return EnvConfig::getInt("IPC_API_TIMEOUT_MS", 5000, 1000, 30000);
}

// IPC timeout for get status operations (quick checks)
inline int getIpcStatusTimeoutMs() {
  return EnvConfig::getInt("IPC_STATUS_TIMEOUT_MS", 3000, 500, 15000);
}

// Frame cache mutex timeout
inline int getFrameCacheMutexTimeoutMs() {
  return EnvConfig::getInt("FRAME_CACHE_MUTEX_TIMEOUT_MS", 1000, 100, 10000);
}

// Maximum age for a cached frame to be considered valid (in seconds)
// Frames older than this will be rejected as stale
inline int getMaxFrameAgeSeconds() {
  return EnvConfig::getInt("MAX_FRAME_AGE_SECONDS", 10, 1, 60);
}

// Worker state mutex timeout (for GET_STATISTICS/GET_STATUS operations)
// Should be very short since state reads should be quick
inline int getWorkerStateMutexTimeoutMs() {
  return EnvConfig::getInt("WORKER_STATE_MUTEX_TIMEOUT_MS", 100, 50, 1000);
}

// Shutdown timeout - total time before force exit
inline int getShutdownTimeoutMs() {
  return EnvConfig::getInt("SHUTDOWN_TIMEOUT_MS", 500, 100, 5000);
}

// RTSP stop timeout during normal operation
inline int getRtspStopTimeoutMs() {
  return EnvConfig::getInt("RTSP_STOP_TIMEOUT_MS", 200, 50, 2000);
}

// RTSP stop timeout during deletion/shutdown (shorter for faster exit)
inline int getRtspStopTimeoutDeletionMs() {
  return EnvConfig::getInt("RTSP_STOP_TIMEOUT_DELETION_MS", 100, 50, 1000);
}

// Destination node finalize timeout during normal operation
inline int getDestinationFinalizeTimeoutMs() {
  return EnvConfig::getInt("DESTINATION_FINALIZE_TIMEOUT_MS", 500, 100, 3000);
}

// Destination node finalize timeout during deletion/shutdown
inline int getDestinationFinalizeTimeoutDeletionMs() {
  return EnvConfig::getInt("DESTINATION_FINALIZE_TIMEOUT_DELETION_MS", 100, 50,
                           1000);
}

// RTMP destination node prepare timeout during normal operation
inline int getRtmpPrepareTimeoutMs() {
  return EnvConfig::getInt("RTMP_PREPARE_TIMEOUT_MS", 200, 50, 2000);
}

// RTMP destination node prepare timeout during deletion/shutdown
inline int getRtmpPrepareTimeoutDeletionMs() {
  return EnvConfig::getInt("RTMP_PREPARE_TIMEOUT_DELETION_MS", 50, 20, 500);
}

// RTMP source node stop timeout during reconnect
// Longer timeout to avoid using detach_recursively() which can affect destination
inline int getRtmpSourceStopTimeoutMs() {
  return EnvConfig::getInt("RTMP_SOURCE_STOP_TIMEOUT_MS", 2000, 500, 10000);
}

// RTMP source reconnect stabilization wait time (after stop, before restart)
// Allows GStreamer pipeline to fully release resources and destination to stabilize
inline int getRtmpSourceReconnectStabilizationMs() {
  return EnvConfig::getInt("RTMP_SOURCE_RECONNECT_STABILIZATION_MS", 3000, 1000, 10000);
}

// RTMP source reconnect initialization wait time (after restart)
// Allows GStreamer pipeline to initialize before processing frames.
// Higher value reduces "gst_sample_get_caps assertion / retrieveVideoFrame NULL" errors
// when the first pull after reconnect gets an invalid sample (see docs/RTMP_RECONNECT_GSTREAMER_NULL_SAMPLE.md).
inline int getRtmpSourceReconnectInitializationMs() {
  return EnvConfig::getInt("RTMP_SOURCE_RECONNECT_INITIALIZATION_MS", 10000, 500, 20000);
}

// ========== RTMP Destination Monitor (reduce false disconnect) ==========
inline int getRtmpDesInitialConnectionTimeoutSec() {
  return EnvConfig::getInt("RTMP_DES_INITIAL_CONNECTION_TIMEOUT_SEC", 45, 15, 120);
}
inline int getRtmpDesDisconnectionTimeoutSec() {
  return EnvConfig::getInt("RTMP_DES_DISCONNECTION_TIMEOUT_SEC", 25, 10, 90);
}
inline int getRtmpDesReconnectGracePeriodSec() {
  return EnvConfig::getInt("RTMP_DES_RECONNECT_GRACE_PERIOD_SEC", 45, 15, 120);
}
inline int getRtmpDesReconnectCooldownSec() {
  return EnvConfig::getInt("RTMP_DES_RECONNECT_COOLDOWN_SEC", 12, 5, 60);
}
// Seconds without activity before "early" reconnect (clear queue). Increase to reduce false disconnect.
inline int getRtmpDesEarlyDetectionThresholdSec() {
  return EnvConfig::getInt("RTMP_DES_EARLY_DETECTION_THRESHOLD_SEC", 20, 10, 120);
}

// Helper functions for std::chrono::milliseconds
inline std::chrono::milliseconds getRegistryMutexTimeout() {
  return std::chrono::milliseconds(getRegistryMutexTimeoutMs());
}

inline std::chrono::milliseconds getApiWrapperTimeout() {
  return std::chrono::milliseconds(getApiWrapperTimeoutMs());
}

inline std::chrono::milliseconds getIpcStartStopTimeout() {
  return std::chrono::milliseconds(getIpcStartStopTimeoutMs());
}

inline std::chrono::milliseconds getIpcApiTimeout() {
  return std::chrono::milliseconds(getIpcApiTimeoutMs());
}

inline std::chrono::milliseconds getIpcStatusTimeout() {
  return std::chrono::milliseconds(getIpcStatusTimeoutMs());
}

inline std::chrono::milliseconds getFrameCacheMutexTimeout() {
  return std::chrono::milliseconds(getFrameCacheMutexTimeoutMs());
}

inline std::chrono::milliseconds getWorkerStateMutexTimeout() {
  return std::chrono::milliseconds(getWorkerStateMutexTimeoutMs());
}

inline std::chrono::milliseconds getShutdownTimeout() {
  return std::chrono::milliseconds(getShutdownTimeoutMs());
}

inline std::chrono::milliseconds getRtspStopTimeout() {
  return std::chrono::milliseconds(getRtspStopTimeoutMs());
}

inline std::chrono::milliseconds getRtspStopTimeoutDeletion() {
  return std::chrono::milliseconds(getRtspStopTimeoutDeletionMs());
}

inline std::chrono::milliseconds getDestinationFinalizeTimeout() {
  return std::chrono::milliseconds(getDestinationFinalizeTimeoutMs());
}

inline std::chrono::milliseconds getDestinationFinalizeTimeoutDeletion() {
  return std::chrono::milliseconds(getDestinationFinalizeTimeoutDeletionMs());
}

inline std::chrono::milliseconds getRtmpPrepareTimeout() {
  return std::chrono::milliseconds(getRtmpPrepareTimeoutMs());
}

inline std::chrono::milliseconds getRtmpPrepareTimeoutDeletion() {
  return std::chrono::milliseconds(getRtmpPrepareTimeoutDeletionMs());
}

inline std::chrono::milliseconds getRtmpSourceStopTimeout() {
  return std::chrono::milliseconds(getRtmpSourceStopTimeoutMs());
}

inline std::chrono::milliseconds getRtmpSourceReconnectStabilization() {
  return std::chrono::milliseconds(getRtmpSourceReconnectStabilizationMs());
}

inline std::chrono::milliseconds getRtmpSourceReconnectInitialization() {
  return std::chrono::milliseconds(getRtmpSourceReconnectInitializationMs());
}

} // namespace TimeoutConstants
