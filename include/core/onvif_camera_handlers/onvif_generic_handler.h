#pragma once

#include "core/onvif_camera_handler.h"
#include "core/onvif_http_client.h"
#include "core/onvif_soap_builder.h"
#include "core/onvif_xml_parser.h"
#include <string>
#include <vector>

/**
 * @brief Generic ONVIF handler (default implementation)
 * 
 * Uses Basic authentication and standard ONVIF SOAP format.
 * Works with most standard ONVIF cameras.
 */
class ONVIFGenericHandler : public ONVIFCameraHandler {
public:
  ONVIFGenericHandler();
  ~ONVIFGenericHandler() override = default;

  std::string getName() const override { return "Generic"; }

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
  
  std::string sendSoapRequest(const std::string &url,
                               const std::string &soapBody,
                               const std::string &username,
                               const std::string &password);
};

