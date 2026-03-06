#include "core/onvif_stream_manager.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/onvif_camera_registry.h"
#include "core/onvif_camera_handler_factory.h"
#include "core/onvif_credentials_manager.h"
#include "core/onvif_soap_builder.h"
#include "core/onvif_xml_parser.h"
#include "core/onvif_http_client.h"
#include <regex>
#include <sstream>

ONVIFStreamManager::ONVIFStreamManager() {}

ONVIFStreamManager::~ONVIFStreamManager() {}

std::vector<ONVIFStream> ONVIFStreamManager::getStreams(const std::string &cameraId) {
  std::vector<ONVIFStream> streams;

  PLOG_INFO << "[ONVIFStreamManager] ========================================";
  PLOG_INFO << "[ONVIFStreamManager] Getting streams for camera: " << cameraId;

  // Get camera from registry
  ONVIFCamera camera;
  auto &registry = ONVIFCameraRegistry::getInstance();
  if (!registry.getCamera(cameraId, camera)) {
    PLOG_WARNING << "[ONVIFStreamManager] Camera not found in registry: " << cameraId;
    PLOG_WARNING << "[ONVIFStreamManager] ========================================";
    return streams;
  }

  PLOG_INFO << "[ONVIFStreamManager] Camera found - IP: " << camera.ip
             << ", Endpoint: " << camera.endpoint
             << ", Manufacturer: " << (camera.manufacturer.empty() ? "unknown" : camera.manufacturer)
             << ", Model: " << (camera.model.empty() ? "unknown" : camera.model);

  // Get handler from factory
  auto &factory = ONVIFCameraHandlerFactory::getInstance();
  auto handler = factory.getHandler(camera);
  PLOG_INFO << "[ONVIFStreamManager] Using handler: " << handler->getName() << " for camera: " << cameraId;

  // Get credentials
  ONVIFCredentials credentials;
  auto &credManager = ONVIFCredentialsManager::getInstance();
  bool hasCreds = credManager.getCredentials(cameraId, credentials);
  
  std::string username, password;
  if (hasCreds) {
    username = credentials.username;
    password = credentials.password;
    PLOG_INFO << "[ONVIFStreamManager] Credentials found for camera: " << cameraId
              << " (username: " << username << ")";
  } else {
    PLOG_WARNING << "[ONVIFStreamManager] No credentials found for camera: " << cameraId;
    PLOG_WARNING << "[ONVIFStreamManager] Some cameras may require credentials to get streams";
  }

  // Get profiles using handler
  PLOG_INFO << "[ONVIFStreamManager] Getting profiles using handler: " << handler->getName();
  std::vector<std::string> profiles = handler->getProfiles(camera, username, password);
  
  if (profiles.empty()) {
    PLOG_WARNING << "[ONVIFStreamManager] No profiles found for camera: " << cameraId;
    PLOG_WARNING << "[ONVIFStreamManager] This might indicate:";
    PLOG_WARNING << "[ONVIFStreamManager]   1. Authentication failure";
    PLOG_WARNING << "[ONVIFStreamManager]   2. Camera doesn't support ONVIF Media service";
    PLOG_WARNING << "[ONVIFStreamManager]   3. Network connectivity issue";
    PLOG_WARNING << "[ONVIFStreamManager] ========================================";
    return streams;
  }

  PLOG_INFO << "[ONVIFStreamManager] Found " << profiles.size() << " profile(s)";

  // Get stream URI and configuration for each profile
  for (size_t i = 0; i < profiles.size(); ++i) {
    const std::string &profileToken = profiles[i];
    PLOG_INFO << "[ONVIFStreamManager] ----------------------------------------";
    PLOG_INFO << "[ONVIFStreamManager] Processing profile[" << i << "]: " << profileToken;
    
    ONVIFStream stream;
    stream.token = profileToken;

    // Get stream URI using handler
    PLOG_INFO << "[ONVIFStreamManager] Getting stream URI for profile: " << profileToken;
    std::string streamUri = handler->getStreamUri(camera, profileToken, username, password);
    if (streamUri.empty()) {
      PLOG_WARNING << "[ONVIFStreamManager] No stream URI for profile: " << profileToken << ", skipping";
      continue;
    }
    stream.uri = streamUri;
    PLOG_INFO << "[ONVIFStreamManager] Stream URI obtained: " << streamUri;

    // Get profile configuration (width, height, fps) using handler
    int width = 0, height = 0, fps = 0;
    PLOG_INFO << "[ONVIFStreamManager] Getting profile configuration for: " << profileToken;
    if (handler->getProfileConfiguration(camera, profileToken, username, password, width, height, fps)) {
      stream.width = width;
      stream.height = height;
      stream.fps = fps;
      PLOG_INFO << "[ONVIFStreamManager] Profile config - Width: " << width 
                << ", Height: " << height << ", FPS: " << fps;
    } else {
      PLOG_WARNING << "[ONVIFStreamManager] Could not get profile configuration for: " << profileToken;
      PLOG_WARNING << "[ONVIFStreamManager] Stream will be added without configuration";
    }

    streams.push_back(stream);
    PLOG_INFO << "[ONVIFStreamManager] Successfully added stream for profile: " << profileToken;
  }

  PLOG_INFO << "[ONVIFStreamManager] ========================================";
  PLOG_INFO << "[ONVIFStreamManager] Total streams collected: " << streams.size();
  return streams;
}

std::vector<std::string> ONVIFStreamManager::getProfiles(const std::string &cameraId) {
  // Get camera
  ONVIFCamera camera;
  auto &registry = ONVIFCameraRegistry::getInstance();
  if (!registry.getCamera(cameraId, camera)) {
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[ONVIFStreamManager] getProfiles: Camera not found: " << cameraId;
    }
    return {};
  }

  // Get credentials
  ONVIFCredentials credentials;
  auto &credManager = ONVIFCredentialsManager::getInstance();
  std::string username, password;
  if (credManager.getCredentials(cameraId, credentials)) {
    username = credentials.username;
    password = credentials.password;
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[ONVIFStreamManager] getProfiles: Using credentials for " << cameraId;
    }
  } else {
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[ONVIFStreamManager] getProfiles: No credentials for " << cameraId;
    }
  }

  // Get media service URL
  std::string mediaServiceUrl = getMediaServiceUrl(camera);
  if (mediaServiceUrl.empty()) {
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[ONVIFStreamManager] getProfiles: Media service URL empty";
    }
    return {};
  }

  if (isApiLoggingEnabled()) {
    PLOG_DEBUG << "[ONVIFStreamManager] getProfiles: Sending request to " << mediaServiceUrl;
  }

  // Create and send GetProfiles request using SOAP builder
  PLOG_INFO << "[ONVIFStreamManager] ========================================";
  PLOG_INFO << "[ONVIFStreamManager] getProfiles: Building GetProfiles SOAP request";
  std::string request = ONVIFSoapBuilder::buildGetProfiles();
  PLOG_INFO << "[ONVIFStreamManager] getProfiles: SOAP request built, length: " << request.length();
  
  PLOG_INFO << "[ONVIFStreamManager] getProfiles: Sending GetProfiles request to " 
            << mediaServiceUrl << " (username: " << (username.empty() ? "none" : username) << ")";
  
  std::string response = sendHttpPost(mediaServiceUrl, request, username, password);

  if (response.empty()) {
    PLOG_WARNING << "[ONVIFStreamManager] getProfiles: Empty response from " << mediaServiceUrl;
    PLOG_WARNING << "[ONVIFStreamManager] getProfiles: This might indicate authentication failure or network issue";
    return {};
  }

  PLOG_INFO << "[ONVIFStreamManager] getProfiles: Response received, length: " << response.length();
  
  // Log first 500 chars of response for debugging (if verbose logging enabled)
  if (isApiLoggingEnabled() && response.length() > 0) {
    std::string preview = response.substr(0, std::min(500UL, response.length()));
    PLOG_DEBUG << "[ONVIFStreamManager] getProfiles: Response preview: " << preview;
    if (response.length() > 500) {
      PLOG_DEBUG << "[ONVIFStreamManager] getProfiles: ... (truncated, total " << response.length() << " chars)";
    }
  }

  // Parse response using XML parser
  PLOG_INFO << "[ONVIFStreamManager] getProfiles: Parsing XML response for profiles";
  std::vector<std::string> profiles = ONVIFXmlParser::parseProfiles(response);
  
  if (profiles.empty()) {
    PLOG_WARNING << "[ONVIFStreamManager] getProfiles: No profiles found in response";
    PLOG_WARNING << "[ONVIFStreamManager] getProfiles: Response might have different XML format";
    // Log full response if no profiles found (for debugging)
    PLOG_WARNING << "[ONVIFStreamManager] getProfiles: Full response for debugging:";
    PLOG_WARNING << response;
  } else {
    PLOG_INFO << "[ONVIFStreamManager] getProfiles: Successfully parsed " << profiles.size() << " profile(s)";
    for (size_t i = 0; i < profiles.size(); ++i) {
      PLOG_INFO << "[ONVIFStreamManager] getProfiles: Profile[" << i << "]: " << profiles[i];
    }
  }
  
  PLOG_INFO << "[ONVIFStreamManager] ========================================";
  return profiles;
}

std::string ONVIFStreamManager::getStreamUri(const std::string &cameraId,
                                             const std::string &profileToken) {
  PLOG_INFO << "[ONVIFStreamManager] ========================================";
  PLOG_INFO << "[ONVIFStreamManager] getStreamUri: Getting stream URI for profile: " << profileToken;
  
  // Get camera
  ONVIFCamera camera;
  auto &registry = ONVIFCameraRegistry::getInstance();
  if (!registry.getCamera(cameraId, camera)) {
    PLOG_WARNING << "[ONVIFStreamManager] getStreamUri: Camera not found: " << cameraId;
    return "";
  }

  // Get credentials
  ONVIFCredentials credentials;
  auto &credManager = ONVIFCredentialsManager::getInstance();
  std::string username, password;
  if (credManager.getCredentials(cameraId, credentials)) {
    username = credentials.username;
    password = credentials.password;
    PLOG_INFO << "[ONVIFStreamManager] getStreamUri: Using credentials (username: " << username << ")";
  } else {
    PLOG_WARNING << "[ONVIFStreamManager] getStreamUri: No credentials found";
  }

  // Get media service URL
  std::string mediaServiceUrl = getMediaServiceUrl(camera);
  if (mediaServiceUrl.empty()) {
    PLOG_WARNING << "[ONVIFStreamManager] getStreamUri: Media service URL empty";
    return "";
  }

  // Create and send GetStreamUri request using SOAP builder
  PLOG_INFO << "[ONVIFStreamManager] getStreamUri: Building GetStreamUri SOAP request";
  std::string request = ONVIFSoapBuilder::buildGetStreamUri(profileToken);
  PLOG_INFO << "[ONVIFStreamManager] getStreamUri: Sending request to " << mediaServiceUrl;
  
  std::string response = sendHttpPost(mediaServiceUrl, request, username, password);

  if (response.empty()) {
    PLOG_WARNING << "[ONVIFStreamManager] getStreamUri: Empty response";
    return "";
  }

  // Parse response using XML parser
  PLOG_INFO << "[ONVIFStreamManager] getStreamUri: Parsing XML response for stream URI";
  std::string streamUri = ONVIFXmlParser::parseStreamUri(response);
  
  if (streamUri.empty()) {
    PLOG_WARNING << "[ONVIFStreamManager] getStreamUri: No stream URI found in response";
    PLOG_WARNING << "[ONVIFStreamManager] getStreamUri: Response: " << response;
  } else {
    PLOG_INFO << "[ONVIFStreamManager] getStreamUri: Found stream URI: " << streamUri;
  }
  
  PLOG_INFO << "[ONVIFStreamManager] ========================================";
  return streamUri;
}

bool ONVIFStreamManager::getProfileConfiguration(const std::string &cameraId,
                                                 const std::string &profileToken,
                                                 int &width, int &height, int &fps) {
  PLOG_DEBUG << "[ONVIFStreamManager] getProfileConfiguration: Getting config for profile: " << profileToken;
  
  // Get camera
  ONVIFCamera camera;
  auto &registry = ONVIFCameraRegistry::getInstance();
  if (!registry.getCamera(cameraId, camera)) {
    PLOG_WARNING << "[ONVIFStreamManager] getProfileConfiguration: Camera not found: " << cameraId;
    return false;
  }

  // Get credentials
  ONVIFCredentials credentials;
  auto &credManager = ONVIFCredentialsManager::getInstance();
  std::string username, password;
  if (credManager.getCredentials(cameraId, credentials)) {
    username = credentials.username;
    password = credentials.password;
  }

  // Get media service URL
  std::string mediaServiceUrl = getMediaServiceUrl(camera);
  if (mediaServiceUrl.empty()) {
    PLOG_WARNING << "[ONVIFStreamManager] getProfileConfiguration: Media service URL empty";
    return false;
  }

  // Try to get video encoder configuration using profile token as config token
  // (some cameras use profile token as config token)
  PLOG_DEBUG << "[ONVIFStreamManager] getProfileConfiguration: Building GetVideoEncoderConfiguration request";
  std::string request = ONVIFSoapBuilder::buildGetVideoEncoderConfiguration(profileToken);
  std::string response = sendHttpPost(mediaServiceUrl, request, username, password);

  if (response.empty()) {
    PLOG_WARNING << "[ONVIFStreamManager] getProfileConfiguration: Empty response";
    return false;
  }

  // Parse response using XML parser
  PLOG_DEBUG << "[ONVIFStreamManager] getProfileConfiguration: Parsing XML response";
  bool success = ONVIFXmlParser::parseVideoEncoderConfiguration(response, width, height, fps);
  
  if (success) {
    PLOG_INFO << "[ONVIFStreamManager] getProfileConfiguration: Parsed config - Width: " << width 
              << ", Height: " << height << ", FPS: " << fps;
  } else {
    PLOG_WARNING << "[ONVIFStreamManager] getProfileConfiguration: Failed to parse configuration";
  }
  
  return success;
}

// Removed - now using ONVIFSoapBuilder and ONVIFXmlParser

std::string ONVIFStreamManager::sendHttpPost(const std::string &url,
                                              const std::string &body,
                                              const std::string &username,
                                              const std::string &password) {
  ONVIFHttpClient client;
  std::string response;
  
  if (client.sendSoapRequest(url, body, username, password, response)) {
    return response;
  }
  
  if (isApiLoggingEnabled()) {
    PLOG_WARNING << "[ONVIFStreamManager] HTTP POST to " << url << " failed";
  }
  return "";
}

std::string ONVIFStreamManager::getMediaServiceUrl(const ONVIFCamera &camera) {
  // Extract media service URL from camera endpoint
  // Usually: http://IP/onvif/media_service or http://IP/onvif/Media
  if (camera.endpoint.empty()) {
    return "";
  }

  // Try to construct media service URL from device service URL
  std::string mediaUrl = camera.endpoint;
  // Replace device_service with media_service
  const std::string deviceService = "device_service";
  const std::string mediaService = "media_service";
  size_t pos = mediaUrl.find(deviceService);
  if (pos != std::string::npos) {
    mediaUrl.replace(pos, deviceService.length(), mediaService);
  } else {
    // Try replacing other common patterns
    pos = mediaUrl.find("Device");
    if (pos != std::string::npos) {
      mediaUrl.replace(pos, 6, "Media");
    } else {
      // Try adding /onvif/Media
      if (mediaUrl.back() != '/') {
        mediaUrl += "/";
      }
      mediaUrl += "Media";
    }
  }

  PLOG_INFO << "[ONVIFStreamManager] Constructed media service URL: " << mediaUrl 
            << " (from endpoint: " << camera.endpoint << ")";

  return mediaUrl;
}

