#pragma once

#include "core/onvif_camera_handler.h"
#include "core/onvif_http_client.h"
#include "core/onvif_soap_builder.h"
#include "core/onvif_xml_parser.h"
#include <string>
#include <vector>

/**
 * @brief Tapo camera-specific ONVIF handler
 * 
 * Tapo cameras typically require:
 * - Digest authentication (instead of Basic)
 * - WS-Security headers in SOAP requests
 * - Special handling for authentication failures
 */
class ONVIFTapoHandler : public ONVIFCameraHandler {
public:
  ONVIFTapoHandler();
  ~ONVIFTapoHandler() override = default;

  std::string getName() const override { return "Tapo"; }

  bool supports(const ONVIFCamera &camera) const override;

  bool getDeviceInformation(ONVIFCamera &camera,
                             const std::string &username = "",
                             const std::string &password = "") override;

  std::vector<std::string> getProfiles(const ONVIFCamera &camera,
                                       const std::string &username,
                                       const std::string &password) override;

  std::string getStreamUri(const ONVIFCamera &camera,
                            const std::string &profileToken,
                            const std::string &username,
                            const std::string &password) override;

  bool getProfileConfiguration(const ONVIFCamera &camera,
                                const std::string &profileToken,
                                const std::string &username,
                                const std::string &password,
                                int &width, int &height, int &fps) override;

  std::string getMediaServiceUrl(const ONVIFCamera &camera) const override;

private:
  ONVIFHttpClient httpClient_;
  
  // Cache for successful authentication method (to avoid retrying all methods)
  // Format: "usePasswordDigest:useHttpAuth" (e.g., "true:false" for PasswordDigest without HTTP Basic Auth)
  mutable std::string cachedAuthMethod_;
  
  /**
   * @brief Build SOAP request with WS-Security header for Tapo
   */
  std::string buildSoapRequestWithSecurity(const std::string &action,
                                            const std::string &body,
                                            const std::string &username,
                                            const std::string &password);
  
  /**
   * @brief Build SOAP request with WS-Security header (with PasswordDigest option)
   * @param usePasswordDigest If true, use PasswordDigest instead of PasswordText
   */
  std::string buildSoapRequestWithSecurity(const std::string &action,
                                            const std::string &body,
                                            const std::string &username,
                                            const std::string &password,
                                            bool usePasswordDigest);
  
  /**
   * @brief Send SOAP request with automatic fallback through all authentication methods
   * @param url Service URL
   * @param action SOAP action
   * @param body SOAP body
   * @param username Username
   * @param password Password
   * @return Response string (empty if all methods failed)
   */
  std::string sendSoapRequestWithFallback(const std::string &url,
                                          const std::string &action,
                                          const std::string &body,
                                          const std::string &username,
                                          const std::string &password);
  
  /**
   * @brief Send SOAP request (may use Digest auth in future)
   * @deprecated Use sendSoapRequestWithFallback instead for better fallback support
   */
  std::string sendSoapRequest(const std::string &url,
                               const std::string &soapBody,
                               const std::string &username,
                               const std::string &password);
  
  /**
   * @brief Detect if camera is Tapo based on various indicators
   */
  bool isTapoCamera(const ONVIFCamera &camera) const;
};

