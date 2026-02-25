#include "models/create_instance_request.h"
#include <algorithm>
#include <regex>

bool CreateInstanceRequest::validate() const {
  validation_error_.clear();

  // Validate name (optional, but if provided must match pattern: ^[A-Za-z0-9 -_]+$)
  // If empty, will use default name during instance creation
  if (!name.empty()) {
    std::regex name_pattern("^[A-Za-z0-9 -_]+$");
    if (!std::regex_match(name, name_pattern)) {
      validation_error_ = "name must match pattern: ^[A-Za-z0-9 -_]+$";
      return false;
    }
  }

  // Validate group (optional, but if provided must match pattern)
  if (!group.empty()) {
    std::regex group_pattern("^[A-Za-z0-9 -_]+$");
    if (!std::regex_match(group, group_pattern)) {
      validation_error_ = "group must match pattern: ^[A-Za-z0-9 -_]+$";
      return false;
    }
  }

  // Validate detectionSensitivity
  if (detectionSensitivity != "Low" && detectionSensitivity != "Medium" &&
      detectionSensitivity != "High") {
    validation_error_ = "detectionSensitivity must be Low, Medium, or High";
    return false;
  }

  // Validate movementSensitivity
  if (movementSensitivity != "Low" && movementSensitivity != "Medium" &&
      movementSensitivity != "High") {
    validation_error_ = "movementSensitivity must be Low, Medium, or High";
    return false;
  }

  // Validate sensorModality
  if (sensorModality != "RGB" && sensorModality != "Thermal") {
    validation_error_ = "sensorModality must be RGB or Thermal";
    return false;
  }

  // Validate detectorMode
  if (detectorMode != "SmartDetection" && detectorMode != "Detection") {
    validation_error_ = "detectorMode must be 'SmartDetection' or 'Detection'";
    return false;
  }

  // Validate inputOrientation
  if (inputOrientation < 0 || inputOrientation > 3) {
    validation_error_ = "inputOrientation must be between 0 and 3";
    return false;
  }

  // Validate frameRateLimit
  if (frameRateLimit < 0) {
    validation_error_ = "frameRateLimit must be >= 0";
    return false;
  }

  // Validate fps (must be positive integer if set)
  if (fps < 0) {
    validation_error_ = "fps must be >= 0";
    return false;
  }

  return true;
}

std::string CreateInstanceRequest::getValidationError() const {
  return validation_error_;
}
