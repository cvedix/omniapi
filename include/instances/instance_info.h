#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Forward declaration
namespace cvedix_nodes {
class cvedix_node;
}

/**
 * @brief Instance information structure matching the API response format
 */
struct InstanceInfo {
  std::string instanceId;
  std::string displayName;
  std::string group;
  std::string solutionId;
  std::string solutionName;
  bool persistent = false;
  bool loaded = false;
  bool running = false;
  double fps = 0.0; // Current/actual processing FPS
  int configuredFps = 5; // Configured target FPS (default: 5 FPS)
  std::string version;
  int frameRateLimit = 0;
  bool metadataMode = false;
  bool statisticsMode = false;
  bool diagnosticsMode = false;
  bool debugMode = false;
  bool readOnly = false;
  bool autoStart = false;
  bool autoRestart = false;
  bool systemInstance = false;

  // Retry limit configuration
  int maxRetryCount =
      10; // Maximum number of retry attempts before stopping instance
  int retryCount =
      0; // Current retry count (reset when instance starts successfully)
  bool retryLimitReached =
      false; // Flag to indicate retry limit has been reached
  std::chrono::steady_clock::time_point startTime; // Time when instance started
  std::chrono::steady_clock::time_point
      lastActivityTime; // Time of last successful activity (frames/data)
  bool hasReceivedData =
      false; // Flag to indicate if instance has received any data/frames
  int inputPixelLimit = 0;
  int inputOrientation = 0;
  std::string detectorMode = "SmartDetection";
  std::string detectionSensitivity = "Low";
  std::string movementSensitivity = "Low";
  std::string sensorModality = "RGB";

  // Detector configuration (detailed)
  std::string
      detectorModelFile; // Model file name (e.g., "pva_det_full_frame_512")
  double animalConfidenceThreshold = 0.0;
  double personConfidenceThreshold = 0.0;
  double vehicleConfidenceThreshold = 0.0;
  double faceConfidenceThreshold = 0.0;
  double licensePlateConfidenceThreshold = 0.0;
  double confThreshold = 0.0; // General confidence threshold

  // DetectorThermal configuration
  std::string detectorThermalModelFile; // Thermal model file name

  // Performance mode
  std::string performanceMode =
      "Balanced"; // "Balanced", "Performance", "Saved"

  // SolutionManager settings
  int recommendedFrameRate = 0; // Recommended frame rate

  struct Originator {
    std::string address;
  } originator;

  // Streaming URLs (for RTMP/RTSP solutions)
  std::string rtmpUrl; // RTMP URL used for streaming
  std::string rtspUrl; // RTSP URL for viewing (if server supports conversion)

  // Source file path (for file source solutions)
  std::string filePath; // File path for file source node

  // Additional parameters (MODEL_PATH, SFACE_MODEL_PATH, RESIZE_RATIO, etc.)
  std::map<std::string, std::string> additionalParams;

  // Internal: Reference to pipeline nodes (not serialized)
  std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> pipeline_nodes;
};
