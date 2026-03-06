#pragma once

#include "core/onvif_camera_registry.h"
#include "core/onvif_credentials_manager.h"
#include <string>
#include <vector>

/**
 * @brief ONVIF Stream Manager
 *
 * Manages ONVIF streams for cameras.
 */
class ONVIFStreamManager {
public:
  /**
   * @brief Constructor
   */
  ONVIFStreamManager();

  /**
   * @brief Destructor
   */
  ~ONVIFStreamManager();

  /**
   * @brief Get streams for a camera
   *
   * @param cameraId Camera ID (UUID or IP)
   * @return Vector of streams
   */
  std::vector<ONVIFStream> getStreams(const std::string &cameraId);

private:
  /**
   * @brief Get media profiles from camera
   */
  std::vector<std::string> getProfiles(const std::string &cameraId);

  /**
   * @brief Get stream URI for a profile
   */
  std::string getStreamUri(const std::string &cameraId,
                           const std::string &profileToken);

  /**
   * @brief Get profile configuration
   */
  bool getProfileConfiguration(const std::string &cameraId,
                                const std::string &profileToken,
                                int &width, int &height, int &fps);

  // SOAP building and XML parsing now handled by ONVIFSoapBuilder and ONVIFXmlParser

  /**
   * @brief Send HTTP POST request (for ONVIF SOAP)
   */
  std::string sendHttpPost(const std::string &url, const std::string &body,
                           const std::string &username = "",
                           const std::string &password = "");

  /**
   * @brief Get media service URL from camera
   */
  std::string getMediaServiceUrl(const ONVIFCamera &camera);
};

