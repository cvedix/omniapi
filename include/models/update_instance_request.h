#pragma once

#include <map>
#include <optional>
#include <string>

/**
 * @brief Request structure for updating an existing instance
 * Similar to CreateInstanceRequest but without solution (cannot change solution
 * after creation)
 */
struct UpdateInstanceRequest {
  std::string name;  // Optional, Pattern: ^[A-Za-z0-9 -_]+$
  std::string group; // Optional, Pattern: ^[A-Za-z0-9 -_]+$

  // Configuration flags (optional - only update if provided)
  std::optional<bool> persistent;
  int frameRateLimit = -1; // Use -1 to indicate not set
  int configuredFps = -1; // Use -1 to indicate not set (default: 5 FPS)
  std::optional<bool> metadataMode;
  std::optional<bool> statisticsMode;
  std::optional<bool> diagnosticsMode;
  std::optional<bool> debugMode;

  // Detection settings (optional)
  std::string detectorMode;         // Empty string means not set
  std::string detectionSensitivity; // Empty string means not set
  std::string movementSensitivity;  // Empty string means not set
  std::string sensorModality;       // Empty string means not set

  // Auto management (optional)
  std::optional<bool> autoStart;
  std::optional<bool> autoRestart;

  // Input settings (optional)
  int inputOrientation = -1; // Use -1 to indicate not set
  int inputPixelLimit = -1;  // Use -1 to indicate not set

  // Additional parameters (for solution-specific configs)
  // e.g., RTSP URL, MODEL_PATH, FILE_PATH, RTMP_URL
  std::map<std::string, std::string>
      additionalParams; // Only update keys that are provided

  /**
   * @brief Validate request parameters
   * @return true if valid, false otherwise
   */
  bool validate() const;

  /**
   * @brief Get error message if validation fails
   */
  std::string getValidationError() const;

  /**
   * @brief Check if request has any fields to update
   */
  bool hasUpdates() const;

private:
  mutable std::string validation_error_;
};
