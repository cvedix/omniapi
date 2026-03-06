#pragma once

#include <string>
#include <vector>

/**
 * @brief ONVIF SOAP Request Builder
 *
 * Builds ONVIF SOAP requests manually without gSOAP dependency.
 */
class ONVIFSoapBuilder {
public:
  /**
   * @brief Build GetDeviceInformation SOAP request
   */
  static std::string buildGetDeviceInformation();

  /**
   * @brief Build GetCapabilities SOAP request
   */
  static std::string buildGetCapabilities();

  /**
   * @brief Build GetProfiles SOAP request
   */
  static std::string buildGetProfiles();

  /**
   * @brief Build GetStreamUri SOAP request
   */
  static std::string buildGetStreamUri(const std::string &profileToken,
                                       const std::string &streamType = "RTP-Unicast",
                                       const std::string &protocol = "RTSP");

  /**
   * @brief Build GetVideoEncoderConfiguration SOAP request
   */
  static std::string buildGetVideoEncoderConfiguration(
      const std::string &configurationToken);

  /**
   * @brief Build GetVideoEncoderConfigurations SOAP request
   */
  static std::string buildGetVideoEncoderConfigurations();

  /**
   * @brief Build GetSnapshotUri SOAP request
   */
  static std::string buildGetSnapshotUri(const std::string &profileToken);

  /**
   * @brief Build PTZ GetStatus SOAP request
   */
  static std::string buildPTZGetStatus(const std::string &profileToken);

  /**
   * @brief Build PTZ AbsoluteMove SOAP request
   */
  static std::string buildPTZAbsoluteMove(const std::string &profileToken,
                                          double pan, double tilt, double zoom);

  /**
   * @brief Build GetServiceCapabilities SOAP request (for Media service)
   */
  static std::string buildGetServiceCapabilities();

private:
  /**
   * @brief Build SOAP envelope header
   */
  static std::string buildSoapHeader(const std::string &action,
                                      const std::string &to);

  /**
   * @brief Build SOAP envelope footer
   */
  static std::string buildSoapFooter();

  /**
   * @brief Build complete SOAP envelope
   */
  static std::string buildSoapEnvelope(const std::string &body,
                                       const std::string &action,
                                       const std::string &to);
};

