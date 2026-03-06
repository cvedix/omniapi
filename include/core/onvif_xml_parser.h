#pragma once

#include "core/onvif_camera_registry.h"
#include <string>
#include <vector>

/**
 * @brief ONVIF XML Response Parser
 *
 * Parses ONVIF SOAP XML responses using regex and string manipulation.
 */
class ONVIFXmlParser {
public:
  /**
   * @brief Parse GetDeviceInformation response
   */
  static bool parseDeviceInformation(const std::string &xmlResponse,
                                      ONVIFCamera &camera);

  /**
   * @brief Parse GetCapabilities response to extract service URLs
   */
  static bool parseCapabilities(const std::string &xmlResponse,
                                 std::string &deviceServiceUrl,
                                 std::string &mediaServiceUrl,
                                 std::string &ptzServiceUrl);

  /**
   * @brief Parse GetProfiles response to extract profile tokens
   */
  static std::vector<std::string> parseProfiles(const std::string &xmlResponse);

  /**
   * @brief Parse GetStreamUri response
   */
  static std::string parseStreamUri(const std::string &xmlResponse);

  /**
   * @brief Parse GetVideoEncoderConfiguration response
   */
  static bool parseVideoEncoderConfiguration(const std::string &xmlResponse,
                                              int &width, int &height, int &fps);

  /**
   * @brief Parse GetSnapshotUri response
   */
  static std::string parseSnapshotUri(const std::string &xmlResponse);

  /**
   * @brief Extract value from XML element (namespace-aware)
   */
  static std::string extractElementValue(const std::string &xml,
                                         const std::string &elementName,
                                         const std::string &namespacePrefix = "");

  /**
   * @brief Extract attribute value from XML element
   */
  static std::string extractAttributeValue(const std::string &xml,
                                            const std::string &elementName,
                                            const std::string &attributeName);

private:
  /**
   * @brief Remove XML namespaces for easier parsing
   */
  static std::string removeNamespaces(const std::string &xml);
};

