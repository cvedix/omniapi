#include "core/onvif_camera_handlers/onvif_generic_handler.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include <algorithm>
#include <cctype>

ONVIFGenericHandler::ONVIFGenericHandler() {
  httpClient_.setTimeout(10);
  PLOG_DEBUG << "[ONVIFGenericHandler] Initialized with timeout: 10 seconds";
}

bool ONVIFGenericHandler::supports(const ONVIFCamera &camera) const {
  // Generic handler supports all cameras (used as fallback)
  (void)camera;  // Suppress unused parameter warning
  PLOG_DEBUG << "[ONVIFGenericHandler] supports() called - always returns true (fallback handler)";
  return true;
}

bool ONVIFGenericHandler::getDeviceInformation(ONVIFCamera &camera,
                                                 const std::string &username,
                                                 const std::string &password) {
  PLOG_INFO << "[ONVIFGenericHandler] ========================================";
  PLOG_INFO << "[ONVIFGenericHandler] getDeviceInformation() called";
  PLOG_INFO << "[ONVIFGenericHandler] Endpoint: " << camera.endpoint;
  PLOG_INFO << "[ONVIFGenericHandler] Username: " << (username.empty() ? "none" : username);

  if (camera.endpoint.empty()) {
    PLOG_WARNING << "[ONVIFGenericHandler] Endpoint is empty, cannot get device information";
    return false;
  }

  PLOG_INFO << "[ONVIFGenericHandler] Building GetDeviceInformation SOAP request";
  std::string request = ONVIFSoapBuilder::buildGetDeviceInformation();
  PLOG_DEBUG << "[ONVIFGenericHandler] SOAP request length: " << request.length();

  PLOG_INFO << "[ONVIFGenericHandler] Sending SOAP request to: " << camera.endpoint;
  std::string response = sendSoapRequest(camera.endpoint, request, username, password);

  if (response.empty()) {
    PLOG_WARNING << "[ONVIFGenericHandler] GetDeviceInformation failed - empty response";
    PLOG_WARNING << "[ONVIFGenericHandler] This might indicate:";
    PLOG_WARNING << "[ONVIFGenericHandler]   1. Camera requires authentication";
    PLOG_WARNING << "[ONVIFGenericHandler]   2. Network connectivity issue";
    PLOG_WARNING << "[ONVIFGenericHandler]   3. Camera doesn't support GetDeviceInformation";
    PLOG_INFO << "[ONVIFGenericHandler] ========================================";
    return false;
  }

  PLOG_INFO << "[ONVIFGenericHandler] Response received, length: " << response.length();
  PLOG_DEBUG << "[ONVIFGenericHandler] Parsing device information from response";
  bool success = ONVIFXmlParser::parseDeviceInformation(response, camera);
  
  if (success) {
    PLOG_INFO << "[ONVIFGenericHandler] ✓ Successfully parsed device information";
    PLOG_INFO << "[ONVIFGenericHandler] Manufacturer: " << (camera.manufacturer.empty() ? "unknown" : camera.manufacturer);
    PLOG_INFO << "[ONVIFGenericHandler] Model: " << (camera.model.empty() ? "unknown" : camera.model);
    PLOG_INFO << "[ONVIFGenericHandler] Serial: " << (camera.serialNumber.empty() ? "unknown" : camera.serialNumber);
  } else {
    PLOG_WARNING << "[ONVIFGenericHandler] Failed to parse device information from response";
    PLOG_DEBUG << "[ONVIFGenericHandler] Response preview (first 500 chars): " 
               << response.substr(0, std::min(500UL, response.length()));
  }
  
  PLOG_INFO << "[ONVIFGenericHandler] ========================================";
  return success;
}

std::vector<std::string> ONVIFGenericHandler::getProfiles(const ONVIFCamera &camera,
                                                            const std::string &username,
                                                            const std::string &password) {
  PLOG_INFO << "[ONVIFGenericHandler] ========================================";
  PLOG_INFO << "[ONVIFGenericHandler] getProfiles() called";
  PLOG_INFO << "[ONVIFGenericHandler] Camera IP: " << camera.ip;
  PLOG_INFO << "[ONVIFGenericHandler] Username: " << (username.empty() ? "none" : username);

  std::string mediaUrl = getMediaServiceUrl(camera);
  if (mediaUrl.empty()) {
    PLOG_WARNING << "[ONVIFGenericHandler] Media service URL is empty, cannot get profiles";
    PLOG_WARNING << "[ONVIFGenericHandler] Camera endpoint: " << camera.endpoint;
    return {};
  }

  PLOG_INFO << "[ONVIFGenericHandler] Media service URL: " << mediaUrl;
  PLOG_INFO << "[ONVIFGenericHandler] Building GetProfiles SOAP request";
  std::string request = ONVIFSoapBuilder::buildGetProfiles();
  PLOG_DEBUG << "[ONVIFGenericHandler] SOAP request length: " << request.length();

  PLOG_INFO << "[ONVIFGenericHandler] Sending GetProfiles request";
  std::string response = sendSoapRequest(mediaUrl, request, username, password);

  if (response.empty()) {
    PLOG_WARNING << "[ONVIFGenericHandler] GetProfiles failed - empty response";
    PLOG_WARNING << "[ONVIFGenericHandler] This might indicate:";
    PLOG_WARNING << "[ONVIFGenericHandler]   1. Authentication failure";
    PLOG_WARNING << "[ONVIFGenericHandler]   2. Camera doesn't support Media service";
    PLOG_WARNING << "[ONVIFGenericHandler]   3. Network connectivity issue";
    PLOG_INFO << "[ONVIFGenericHandler] ========================================";
    return {};
  }

  PLOG_INFO << "[ONVIFGenericHandler] Response received, length: " << response.length();
  PLOG_DEBUG << "[ONVIFGenericHandler] Parsing profiles from response";
  std::vector<std::string> profiles = ONVIFXmlParser::parseProfiles(response);
  
  if (profiles.empty()) {
    PLOG_WARNING << "[ONVIFGenericHandler] No profiles found in response";
    PLOG_DEBUG << "[ONVIFGenericHandler] Response preview (first 500 chars): " 
               << response.substr(0, std::min(500UL, response.length()));
  } else {
    PLOG_INFO << "[ONVIFGenericHandler] ✓ Successfully parsed " << profiles.size() << " profile(s)";
    for (size_t i = 0; i < profiles.size(); ++i) {
      PLOG_INFO << "[ONVIFGenericHandler] Profile[" << i << "]: " << profiles[i];
    }
  }
  
  PLOG_INFO << "[ONVIFGenericHandler] ========================================";
  return profiles;
}

std::string ONVIFGenericHandler::getStreamUri(const ONVIFCamera &camera,
                                                const std::string &profileToken,
                                                const std::string &username,
                                                const std::string &password) {
  std::string mediaUrl = getMediaServiceUrl(camera);
  if (mediaUrl.empty()) {
    return "";
  }

  std::string request = ONVIFSoapBuilder::buildGetStreamUri(profileToken);
  std::string response = sendSoapRequest(mediaUrl, request, username, password);

  if (response.empty()) {
    return "";
  }

  return ONVIFXmlParser::parseStreamUri(response);
}

bool ONVIFGenericHandler::getProfileConfiguration(const ONVIFCamera &camera,
                                                   const std::string &profileToken,
                                                   const std::string &username,
                                                   const std::string &password,
                                                   int &width, int &height, int &fps) {
  std::string mediaUrl = getMediaServiceUrl(camera);
  if (mediaUrl.empty()) {
    return false;
  }

  std::string request = ONVIFSoapBuilder::buildGetVideoEncoderConfiguration(profileToken);
  std::string response = sendSoapRequest(mediaUrl, request, username, password);

  if (response.empty()) {
    return false;
  }

  return ONVIFXmlParser::parseVideoEncoderConfiguration(response, width, height, fps);
}

std::string ONVIFGenericHandler::getMediaServiceUrl(const ONVIFCamera &camera) const {
  if (camera.endpoint.empty()) {
    return "";
  }

  std::string mediaUrl = camera.endpoint;
  const std::string deviceService = "device_service";
  const std::string mediaService = "media_service";
  size_t pos = mediaUrl.find(deviceService);
  if (pos != std::string::npos) {
    mediaUrl.replace(pos, deviceService.length(), mediaService);
  } else {
    pos = mediaUrl.find("Device");
    if (pos != std::string::npos) {
      mediaUrl.replace(pos, 6, "Media");
    } else {
      if (mediaUrl.back() != '/') {
        mediaUrl += "/";
      }
      mediaUrl += "Media";
    }
  }

  return mediaUrl;
}

std::string ONVIFGenericHandler::sendSoapRequest(const std::string &url,
                                                  const std::string &soapBody,
                                                  const std::string &username,
                                                  const std::string &password) {
  std::string response;
  if (httpClient_.sendSoapRequest(url, soapBody, username, password, response)) {
    return response;
  }
  return "";
}

