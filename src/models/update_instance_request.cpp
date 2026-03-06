#include "models/update_instance_request.h"
#include <regex>

bool UpdateInstanceRequest::validate() const {
  validation_error_.clear();

  // Validate name pattern if provided
  if (!name.empty()) {
    std::regex namePattern("^[A-Za-z0-9 -_]+$");
    if (!std::regex_match(name, namePattern)) {
      validation_error_ =
          "Invalid name format. Name must match pattern: ^[A-Za-z0-9 -_]+$";
      return false;
    }
  }

  // Validate group pattern if provided
  if (!group.empty()) {
    std::regex groupPattern("^[A-Za-z0-9 -_]+$");
    if (!std::regex_match(group, groupPattern)) {
      validation_error_ =
          "Invalid group format. Group must match pattern: ^[A-Za-z0-9 -_]+$";
      return false;
    }
  }

  // Validate frameRateLimit if set
  if (frameRateLimit != -1 && frameRateLimit < 0) {
    validation_error_ = "frameRateLimit must be >= 0";
    return false;
  }

  // Validate configuredFps if set (must be positive)
  if (configuredFps != -1 && configuredFps <= 0) {
    validation_error_ = "configuredFps must be > 0";
    return false;
  }

  // Validate detectorMode if provided
  if (!detectorMode.empty() && detectorMode != "SmartDetection" && detectorMode != "Detection") {
    validation_error_ = "detectorMode must be 'SmartDetection' or 'Detection'";
    return false;
  }

  // Validate detectionSensitivity if provided
  if (!detectionSensitivity.empty()) {
    if (detectionSensitivity != "Low" && detectionSensitivity != "Medium" &&
        detectionSensitivity != "High") {
      validation_error_ =
          "detectionSensitivity must be 'Low', 'Medium', or 'High'";
      return false;
    }
  }

  // Validate movementSensitivity if provided
  if (!movementSensitivity.empty()) {
    if (movementSensitivity != "Low" && movementSensitivity != "Medium" &&
        movementSensitivity != "High") {
      validation_error_ =
          "movementSensitivity must be 'Low', 'Medium', or 'High'";
      return false;
    }
  }

  // Validate sensorModality if provided
  if (!sensorModality.empty()) {
    if (sensorModality != "RGB" && sensorModality != "Thermal") {
      validation_error_ = "sensorModality must be 'RGB' or 'Thermal'";
      return false;
    }
  }

  // Validate inputOrientation if set
  if (inputOrientation != -1 &&
      (inputOrientation < 0 || inputOrientation > 3)) {
    validation_error_ = "inputOrientation must be between 0 and 3";
    return false;
  }

  // Validate inputPixelLimit if set
  if (inputPixelLimit != -1 && inputPixelLimit < 0) {
    validation_error_ = "inputPixelLimit must be >= 0";
    return false;
  }

  return true;
}

std::string UpdateInstanceRequest::getValidationError() const {
  return validation_error_;
}

bool UpdateInstanceRequest::hasUpdates() const {
  return !name.empty() || !group.empty() || persistent.has_value() ||
         frameRateLimit != -1 || configuredFps != -1 || metadataMode.has_value() ||
         statisticsMode.has_value() || diagnosticsMode.has_value() ||
         debugMode.has_value() || !detectorMode.empty() ||
         !detectionSensitivity.empty() || !movementSensitivity.empty() ||
         !sensorModality.empty() || autoStart.has_value() ||
         autoRestart.has_value() || inputOrientation != -1 ||
         inputPixelLimit != -1 || !additionalParams.empty();
}
