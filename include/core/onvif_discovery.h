#pragma once

#include "core/onvif_camera_registry.h"
#include <string>
#include <vector>

/**
 * @brief ONVIF Discovery
 *
 * Implements WS-Discovery protocol to discover ONVIF cameras on the network.
 */
class ONVIFDiscovery {
public:
  /**
   * @brief Constructor
   */
  ONVIFDiscovery();

  /**
   * @brief Destructor
   */
  ~ONVIFDiscovery();

  /**
   * @brief Discover ONVIF cameras on the network
   *
   * @param timeoutSeconds Timeout in seconds for discovery
   * @return Vector of discovered cameras
   */
  std::vector<ONVIFCamera> discoverCameras(int timeoutSeconds = 5);

  /**
   * @brief Discover cameras asynchronously (non-blocking)
   *
   * @param timeoutSeconds Timeout in seconds
   */
  void discoverCamerasAsync(int timeoutSeconds = 5);

  /**
   * @brief Check if discovery is in progress
   */
  bool isDiscovering() const;

private:
  /**
   * @brief Send WS-Discovery Probe message
   */
  bool sendProbe();

  /**
   * @brief Receive ProbeMatch responses
   */
  std::vector<ONVIFCamera> receiveProbeMatches(int timeoutSeconds);

  /**
   * @brief Parse ProbeMatch response
   */
  ONVIFCamera parseProbeMatch(const std::string &xmlResponse);

  /**
   * @brief Get device information from camera
   */
  bool getDeviceInformation(ONVIFCamera &camera);

  /**
   * @brief Extract IP address from XAddrs
   */
  std::string extractIPFromXAddrs(const std::string &xaddrs);

  /**
   * @brief Extract UUID from ProbeMatch
   */
  std::string extractUUID(const std::string &xmlResponse);

  /**
   * @brief Extract XAddrs from ProbeMatch
   */
  std::string extractXAddrs(const std::string &xmlResponse);

  /**
   * @brief Create WS-Discovery Probe SOAP message
   */
  std::string createProbeMessage();

  // SOAP building and XML parsing now handled by ONVIFSoapBuilder and ONVIFXmlParser

  /**
   * @brief Send HTTP POST request (for ONVIF SOAP)
   */
  std::string sendHttpPost(const std::string &url, const std::string &body,
                           const std::string &username = "",
                           const std::string &password = "");

  bool discovering_;
  int socketFd_;
};

