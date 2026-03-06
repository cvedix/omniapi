#pragma once

#include "core/onvif_camera_registry.h"
#include <algorithm>
#include <string>
#include <vector>

/**
 * @brief Base interface for camera-specific ONVIF handlers
 * 
 * Each camera manufacturer may have different requirements:
 * - Authentication method (Basic, Digest, WS-Security)
 * - SOAP request format
 * - Service URLs
 * - Error handling
 */
class ONVIFCameraHandler {
public:
  virtual ~ONVIFCameraHandler() = default;

  /**
   * @brief Get handler name (for logging/debugging)
   */
  virtual std::string getName() const = 0;

  /**
   * @brief Check if this handler supports the given camera
   * @param camera Camera information
   * @return true if this handler should be used for this camera
   */
  virtual bool supports(const ONVIFCamera &camera) const = 0;

  /**
   * @brief Get device information from camera
   * @param camera Camera to query
   * @param username Username for authentication
   * @param password Password for authentication
   * @return true if successful
   */
  virtual bool getDeviceInformation(ONVIFCamera &camera,
                                     const std::string &username = "",
                                     const std::string &password = "") = 0;

  /**
   * @brief Get media profiles from camera
   * @param camera Camera to query
   * @param username Username for authentication
   * @param password Password for authentication
   * @return Vector of profile tokens
   */
  virtual std::vector<std::string> getProfiles(const ONVIFCamera &camera,
                                                const std::string &username,
                                                const std::string &password) = 0;

  /**
   * @brief Get stream URI for a profile
   * @param camera Camera to query
   * @param profileToken Profile token
   * @param username Username for authentication
   * @param password Password for authentication
   * @return Stream URI (RTSP URL)
   */
  virtual std::string getStreamUri(const ONVIFCamera &camera,
                                   const std::string &profileToken,
                                   const std::string &username,
                                   const std::string &password) = 0;

  /**
   * @brief Get profile configuration (width, height, fps)
   * @param camera Camera to query
   * @param profileToken Profile token
   * @param username Username for authentication
   * @param password Password for authentication
   * @param width Output width
   * @param height Output height
   * @param fps Output fps
   * @return true if successful
   */
  virtual bool getProfileConfiguration(const ONVIFCamera &camera,
                                       const std::string &profileToken,
                                       const std::string &username,
                                       const std::string &password,
                                       int &width, int &height, int &fps) = 0;

  /**
   * @brief Get media service URL from camera endpoint
   * @param camera Camera information
   * @return Media service URL
   */
  virtual std::string getMediaServiceUrl(const ONVIFCamera &camera) const = 0;

protected:
  /**
   * @brief Normalize manufacturer name for comparison
   */
  static std::string normalizeManufacturer(const std::string &manufacturer) {
    std::string normalized = manufacturer;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    return normalized;
  }
};

