#pragma once

#include <map>
#include <string>

/**
 * @brief Request structure for creating a new instance
 * Matches the API requirement from requirement_create_instance.txt
 */
struct CreateInstanceRequest {
  std::string name;     // Required, Pattern: ^[A-Za-z0-9 -_]+$
  std::string group;    // Optional, Pattern: ^[A-Za-z0-9 -_]+$
  std::string solution; // Optional, must match existing solution ID

  // Configuration flags
  bool persistent = false;
  int frameRateLimit = 0;
  bool metadataMode = false;
  bool statisticsMode = false;
  bool diagnosticsMode = false;
  bool debugMode = false;

  // Detection settings
  std::string detectorMode = "SmartDetection";
  std::string detectionSensitivity =
      "Low"; // "Low", "Medium", "High", "Normal", "Slow"
  std::string movementSensitivity = "Low"; // "Low", "Medium", "High"
  std::string sensorModality = "RGB";      // "RGB", "Thermal"

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

  // FPS configuration (target frame processing rate)
  int fps = 0; // Target FPS (0 means use default: 5 FPS)

  // Auto management
  bool autoStart = false;
  bool autoRestart = false;
  bool blockingReadaheadQueue = false;

  // Input settings
  int inputOrientation = 0; // 0-3
  int inputPixelLimit = 0;

  // Additional parameters (for solution-specific configs)
  // e.g., RTSP URL for face detection solution
  std::map<std::string, std::string> additionalParams;

  /**
   * @brief Validate request parameters
   * @return true if valid, false otherwise
   */
  bool validate() const;

  /**
   * @brief Get error message if validation fails
   */
  std::string getValidationError() const;

private:
  mutable std::string validation_error_;
};
