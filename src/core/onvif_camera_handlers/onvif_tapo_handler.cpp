#include "core/onvif_camera_handlers/onvif_tapo_handler.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/onvif_digest_auth.h"
#include <drogon/utils/Utilities.h>
#include <openssl/sha.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <chrono>
#include <random>
#include <ctime>

ONVIFTapoHandler::ONVIFTapoHandler() : cachedAuthMethod_("") {
  httpClient_.setTimeout(15); // Tapo cameras may need longer timeout
  PLOG_DEBUG << "[ONVIFTapoHandler] Initialized with timeout: 15 seconds (Tapo cameras may need longer)";
}

bool ONVIFTapoHandler::supports(const ONVIFCamera &camera) const {
  PLOG_DEBUG << "[ONVIFTapoHandler] supports() called - checking if camera is Tapo";
  bool isTapo = isTapoCamera(camera);
  PLOG_DEBUG << "[ONVIFTapoHandler] Camera " << camera.ip << " is " << (isTapo ? "Tapo" : "not Tapo");
  return isTapo;
}

bool ONVIFTapoHandler::isTapoCamera(const ONVIFCamera &camera) const {
  PLOG_DEBUG << "[ONVIFTapoHandler] Checking if camera is Tapo - Manufacturer: " 
             << (camera.manufacturer.empty() ? "unknown" : camera.manufacturer)
             << ", Model: " << (camera.model.empty() ? "unknown" : camera.model)
             << ", Endpoint: " << camera.endpoint;
  
  // Check manufacturer name
  std::string manufacturer = normalizeManufacturer(camera.manufacturer);
  if (manufacturer.find("tapo") != std::string::npos ||
      manufacturer.find("tp-link") != std::string::npos) {
    PLOG_DEBUG << "[ONVIFTapoHandler] ✓ Detected Tapo by manufacturer name";
    return true;
  }

  // Check model name
  std::string model = camera.model;
  std::transform(model.begin(), model.end(), model.begin(), ::tolower);
  if (model.find("tapo") != std::string::npos ||
      model.find("c200") != std::string::npos ||
      model.find("c310") != std::string::npos ||
      model.find("c320") != std::string::npos ||
      model.find("c325") != std::string::npos) {
    PLOG_DEBUG << "[ONVIFTapoHandler] ✓ Detected Tapo by model name";
    return true;
  }

  // Check endpoint pattern - Tapo cameras typically use port 2020 for ONVIF
  // This is a strong indicator for Tapo cameras (based on TP-Link documentation)
  if (camera.endpoint.find(":2020") != std::string::npos) {
    PLOG_DEBUG << "[ONVIFTapoHandler] ✓ Detected Tapo by port 2020 (Tapo cameras typically use this port for ONVIF)";
    PLOG_INFO << "[ONVIFTapoHandler] Camera uses port 2020 - treating as Tapo camera";
    return true;
  }

  PLOG_DEBUG << "[ONVIFTapoHandler] Camera does not appear to be Tapo";
  return false;
}

bool ONVIFTapoHandler::getDeviceInformation(ONVIFCamera &camera,
                                             const std::string &username,
                                             const std::string &password) {
  PLOG_INFO << "[ONVIFTapoHandler] ========================================";
  PLOG_INFO << "[ONVIFTapoHandler] getDeviceInformation() called (Tapo-specific)";
  PLOG_INFO << "[ONVIFTapoHandler] Endpoint: " << camera.endpoint;
  PLOG_INFO << "[ONVIFTapoHandler] Username: " << (username.empty() ? "none" : username);

  if (camera.endpoint.empty()) {
    PLOG_WARNING << "[ONVIFTapoHandler] Endpoint is empty, cannot get device information";
    return false;
  }

  PLOG_INFO << "[ONVIFTapoHandler] ⚠ Note: Tapo cameras typically require authentication";
  
  // Build SOAP request with WS-Security header for Tapo cameras
  if (!username.empty() && !password.empty()) {
    PLOG_INFO << "[ONVIFTapoHandler] Building GetDeviceInformation SOAP request with WS-Security header";
    // Use sendSoapRequestWithFallback to automatically try all authentication methods
    std::string response = sendSoapRequestWithFallback(
        camera.endpoint,
        "http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation",
        "    <tds:GetDeviceInformation/>\n",
        username, password);
    
    if (!response.empty()) {
      PLOG_INFO << "[ONVIFTapoHandler] Response received, length: " << response.length();
      PLOG_DEBUG << "[ONVIFTapoHandler] Parsing device information from response";
      bool success = ONVIFXmlParser::parseDeviceInformation(response, camera);
      
      if (success) {
        PLOG_INFO << "[ONVIFTapoHandler] ✓ Successfully parsed device information";
        PLOG_INFO << "[ONVIFTapoHandler] Manufacturer: " << (camera.manufacturer.empty() ? "unknown" : camera.manufacturer);
        PLOG_INFO << "[ONVIFTapoHandler] Model: " << (camera.model.empty() ? "unknown" : camera.model);
        PLOG_INFO << "[ONVIFTapoHandler] Serial: " << (camera.serialNumber.empty() ? "unknown" : camera.serialNumber);
        PLOG_INFO << "[ONVIFTapoHandler] ========================================";
        return true;
      } else {
        PLOG_WARNING << "[ONVIFTapoHandler] Failed to parse device information from response";
        PLOG_DEBUG << "[ONVIFTapoHandler] Response preview (first 500 chars): " 
                   << response.substr(0, std::min(500UL, response.length()));
      }
    }
  } else {
    // Try without WS-Security if no credentials provided
    PLOG_INFO << "[ONVIFTapoHandler] Building GetDeviceInformation SOAP request (no credentials)";
    std::string request = ONVIFSoapBuilder::buildGetDeviceInformation();
    PLOG_DEBUG << "[ONVIFTapoHandler] SOAP request length: " << request.length();
    PLOG_INFO << "[ONVIFTapoHandler] Sending SOAP request to: " << camera.endpoint;
    std::string response = sendSoapRequest(camera.endpoint, request, username, password);
    
    if (!response.empty()) {
      PLOG_INFO << "[ONVIFTapoHandler] Response received, length: " << response.length();
      PLOG_DEBUG << "[ONVIFTapoHandler] Parsing device information from response";
      bool success = ONVIFXmlParser::parseDeviceInformation(response, camera);
      
      if (success) {
        PLOG_INFO << "[ONVIFTapoHandler] ✓ Successfully parsed device information";
        PLOG_INFO << "[ONVIFTapoHandler] Manufacturer: " << (camera.manufacturer.empty() ? "unknown" : camera.manufacturer);
        PLOG_INFO << "[ONVIFTapoHandler] Model: " << (camera.model.empty() ? "unknown" : camera.model);
        PLOG_INFO << "[ONVIFTapoHandler] Serial: " << (camera.serialNumber.empty() ? "unknown" : camera.serialNumber);
        PLOG_INFO << "[ONVIFTapoHandler] ========================================";
        return true;
      }
    }
  }

  // If we get here, request failed
  PLOG_WARNING << "[ONVIFTapoHandler] GetDeviceInformation failed - empty response";
  PLOG_WARNING << "[ONVIFTapoHandler] Tapo cameras typically require authentication";
  PLOG_WARNING << "[ONVIFTapoHandler] This might indicate:";
  PLOG_WARNING << "[ONVIFTapoHandler]   1. Missing or incorrect credentials";
  PLOG_WARNING << "[ONVIFTapoHandler]   2. Camera requires Digest authentication";
  PLOG_WARNING << "[ONVIFTapoHandler]   3. Camera requires WS-Security headers";
  PLOG_INFO << "[ONVIFTapoHandler] ========================================";
  return false;
}

std::vector<std::string> ONVIFTapoHandler::getProfiles(const ONVIFCamera &camera,
                                                         const std::string &username,
                                                         const std::string &password) {
  PLOG_INFO << "[ONVIFTapoHandler] ========================================";
  PLOG_INFO << "[ONVIFTapoHandler] getProfiles() called (Tapo-specific)";
  PLOG_INFO << "[ONVIFTapoHandler] Camera IP: " << camera.ip;
  PLOG_INFO << "[ONVIFTapoHandler] Username: " << (username.empty() ? "none" : username);

  if (username.empty() || password.empty()) {
    PLOG_WARNING << "[ONVIFTapoHandler] ⚠ Credentials required for Tapo cameras";
    PLOG_WARNING << "[ONVIFTapoHandler] Cannot get profiles without credentials";
    PLOG_INFO << "[ONVIFTapoHandler] ========================================";
    return {};
  }

  std::string mediaUrl = getMediaServiceUrl(camera);
  if (mediaUrl.empty()) {
    PLOG_WARNING << "[ONVIFTapoHandler] Media service URL is empty, cannot get profiles";
    PLOG_WARNING << "[ONVIFTapoHandler] Camera endpoint: " << camera.endpoint;
    return {};
  }

  PLOG_INFO << "[ONVIFTapoHandler] Media service URL: " << mediaUrl;
  PLOG_INFO << "[ONVIFTapoHandler] ✓ Using WS-Security for Tapo camera";
  PLOG_INFO << "[ONVIFTapoHandler] Building GetProfiles SOAP request with WS-Security header";

  // Use sendSoapRequestWithFallback to automatically try all authentication methods
  std::string response = sendSoapRequestWithFallback(
      mediaUrl,
      "http://www.onvif.org/ver10/media/wsdl/GetProfiles",
      "    <trt:GetProfiles/>\n",
      username, password);

  if (response.empty()) {
    PLOG_WARNING << "[ONVIFTapoHandler] GetProfiles failed - empty response";
    PLOG_WARNING << "[ONVIFTapoHandler] Tapo cameras may require:";
    PLOG_WARNING << "[ONVIFTapoHandler]   1. Digest authentication (not Basic)";
    PLOG_WARNING << "[ONVIFTapoHandler]   2. WS-Security headers (already tried)";
    PLOG_WARNING << "[ONVIFTapoHandler]   3. Correct credentials";
    PLOG_INFO << "[ONVIFTapoHandler] ========================================";
    return {};
  }

  PLOG_INFO << "[ONVIFTapoHandler] Response received, length: " << response.length();
  PLOG_DEBUG << "[ONVIFTapoHandler] Parsing profiles from response";
  std::vector<std::string> profiles = ONVIFXmlParser::parseProfiles(response);
  
  if (profiles.empty()) {
    PLOG_WARNING << "[ONVIFTapoHandler] No profiles found in response";
    PLOG_DEBUG << "[ONVIFTapoHandler] Response preview (first 500 chars): " 
               << response.substr(0, std::min(500UL, response.length()));
  } else {
    PLOG_INFO << "[ONVIFTapoHandler] ✓ Successfully parsed " << profiles.size() << " profile(s)";
    for (size_t i = 0; i < profiles.size(); ++i) {
      PLOG_INFO << "[ONVIFTapoHandler] Profile[" << i << "]: " << profiles[i];
    }
  }
  
  PLOG_INFO << "[ONVIFTapoHandler] ========================================";
  return profiles;
}

std::string ONVIFTapoHandler::getStreamUri(const ONVIFCamera &camera,
                                             const std::string &profileToken,
                                             const std::string &username,
                                             const std::string &password) {
  if (username.empty() || password.empty()) {
    PLOG_WARNING << "[ONVIFTapoHandler] getStreamUri: Credentials required";
    return "";
  }

  std::string mediaUrl = getMediaServiceUrl(camera);
  if (mediaUrl.empty()) {
    return "";
  }

  // Build GetStreamUri request body
  std::ostringstream body;
  body << "    <trt:GetStreamUri>\n"
       << "      <trt:StreamSetup>\n"
       << "        <tt:Stream>RTP-Unicast</tt:Stream>\n"
       << "        <tt:Transport>\n"
       << "          <tt:Protocol>RTSP</tt:Protocol>\n"
       << "        </tt:Transport>\n"
       << "      </trt:StreamSetup>\n"
       << "      <trt:ProfileToken>" << profileToken << "</trt:ProfileToken>\n"
       << "    </trt:GetStreamUri>\n";

  // Use sendSoapRequestWithFallback to automatically try all authentication methods
  // This will also cache the successful method for future requests
  std::string response = sendSoapRequestWithFallback(
      mediaUrl,
      "http://www.onvif.org/ver10/media/wsdl/GetStreamUri",
      body.str(),
      username, password);
  
  if (response.empty()) {
    PLOG_WARNING << "[ONVIFTapoHandler] getStreamUri: All authentication methods failed";
    return "";
  }
  
  return ONVIFXmlParser::parseStreamUri(response);
}

bool ONVIFTapoHandler::getProfileConfiguration(const ONVIFCamera &camera,
                                                const std::string &profileToken,
                                                const std::string &username,
                                                const std::string &password,
                                                int &width, int &height, int &fps) {
  if (username.empty() || password.empty()) {
    return false;
  }

  std::string mediaUrl = getMediaServiceUrl(camera);
  if (mediaUrl.empty()) {
    return false;
  }

  std::ostringstream body;
  body << "    <trt:GetVideoEncoderConfiguration>\n"
       << "      <trt:ConfigurationToken>" << profileToken << "</trt:ConfigurationToken>\n"
       << "    </trt:GetVideoEncoderConfiguration>\n";

  // Use sendSoapRequestWithFallback to automatically try all authentication methods
  // This will also cache the successful method for future requests
  std::string response = sendSoapRequestWithFallback(
      mediaUrl,
      "http://www.onvif.org/ver10/media/wsdl/GetVideoEncoderConfiguration",
      body.str(),
      username, password);
  
  if (response.empty()) {
    PLOG_WARNING << "[ONVIFTapoHandler] getProfileConfiguration: All authentication methods failed";
    return false;
  }
  
  if (!ONVIFXmlParser::parseVideoEncoderConfiguration(response, width, height, fps)) {
    PLOG_WARNING << "[ONVIFTapoHandler] getProfileConfiguration: Failed to parse video encoder configuration";
    return false;
  }
  
  return true;
}

std::string ONVIFTapoHandler::getMediaServiceUrl(const ONVIFCamera &camera) const {
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

std::string ONVIFTapoHandler::buildSoapRequestWithSecurity(
    const std::string &action,
    const std::string &body,
    const std::string &username,
    const std::string &password) {
  return buildSoapRequestWithSecurity(action, body, username, password, false);
}

std::string ONVIFTapoHandler::buildSoapRequestWithSecurity(
    const std::string &action,
    const std::string &body,
    const std::string &username,
    const std::string &password,
    bool usePasswordDigest) {
  std::ostringstream oss;
  
  // Generate nonce and timestamp for PasswordDigest
  std::string nonce;
  std::string created;
  unsigned char nonceBytes[16] = {0}; // Initialize for use in PasswordDigest calculation
  
  if (usePasswordDigest && !username.empty() && !password.empty()) {
    // Generate random nonce (16 bytes)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (int i = 0; i < 16; ++i) {
      nonceBytes[i] = static_cast<unsigned char>(dis(gen));
    }
    
    // Base64 encode nonce for WS-Security header
    nonce = drogon::utils::base64Encode(nonceBytes, 16);
    
    // Generate timestamp (ISO 8601 format)
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    gmtime_r(&time_t, &tm_buf);
    char timeStr[32];
    std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    created = timeStr;
  }
  
  oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\"\n"
      << "            xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\"\n"
      << "            xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\"\n"
      << "            xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\"\n"
      << "            xmlns:tt=\"http://www.onvif.org/ver10/schema\"\n"
      << "            xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\"\n"
      << "            xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\">\n"
      << "  <s:Header>\n"
      << "    <wsa:Action xmlns:wsa=\"http://www.w3.org/2005/08/addressing\">" << action << "</wsa:Action>\n";
  
  // Add WS-Security header if credentials provided
  if (!username.empty() && !password.empty()) {
    oss << "    <wsse:Security>\n"
        << "      <wsse:UsernameToken";
    
    if (usePasswordDigest && !created.empty()) {
      oss << " wsu:Id=\"UsernameToken-1\"";
    }
    oss << ">\n"
        << "        <wsse:Username>" << username << "</wsse:Username>\n";
    
    if (usePasswordDigest) {
      // Calculate PasswordDigest = Base64(SHA1(Nonce + Created + Password))
      std::string passwordDigest;
      if (!nonce.empty() && !created.empty()) {
        // Nonce is already base64 encoded, but we need raw bytes for SHA1
        // For WS-Security PasswordDigest: SHA1(nonce_bytes + created + password)
        // We'll use the nonce bytes directly (before base64 encoding)
        // Since we generated nonceBytes above, we can use them directly
        std::string nonceStr(reinterpret_cast<const char*>(nonceBytes), 16);
        
        // Calculate SHA1(nonce_bytes + created + password)
        std::string digestInput = nonceStr + created + password;
        unsigned char digest[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(digestInput.c_str()), digestInput.length(), digest);
        
        // Base64 encode
        passwordDigest = drogon::utils::base64Encode(digest, SHA_DIGEST_LENGTH);
      }
      
      oss << "        <wsse:Password Type=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">"
          << passwordDigest << "</wsse:Password>\n"
          << "        <wsse:Nonce EncodingType=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary\">"
          << nonce << "</wsse:Nonce>\n"
          << "        <wsu:Created>" << created << "</wsu:Created>\n";
    } else {
      oss << "        <wsse:Password Type=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordText\">"
          << password << "</wsse:Password>\n";
    }
    
    oss << "      </wsse:UsernameToken>\n"
        << "    </wsse:Security>\n";
  }
  
  oss << "  </s:Header>\n"
      << "  <s:Body>\n"
      << body
      << "  </s:Body>\n"
      << "</s:Envelope>\n";
  
  return oss.str();
}

std::string ONVIFTapoHandler::sendSoapRequestWithFallback(const std::string &url,
                                                           const std::string &action,
                                                           const std::string &body,
                                                           const std::string &username,
                                                           const std::string &password) {
  // Try cached method first (if available)
  if (!cachedAuthMethod_.empty()) {
    bool usePasswordDigest = (cachedAuthMethod_.find("true") != std::string::npos);
    bool useHttpAuth = (cachedAuthMethod_.find(":true") != std::string::npos);
    
    PLOG_DEBUG << "[ONVIFTapoHandler] Trying cached auth method: PasswordDigest=" 
               << usePasswordDigest << ", HTTP Basic Auth=" << useHttpAuth;
    
    std::string request = buildSoapRequestWithSecurity(action, body, username, password, usePasswordDigest);
    std::string response;
    if (httpClient_.sendSoapRequest(url, request, username, password, response, useHttpAuth)) {
      PLOG_DEBUG << "[ONVIFTapoHandler] ✓ Cached auth method succeeded";
      return response;
    }
    // Cache invalid, clear it
    PLOG_WARNING << "[ONVIFTapoHandler] Cached auth method failed, clearing cache";
    cachedAuthMethod_.clear();
  }
  
  // Try all authentication methods in order
  // Method 1: WS-Security PasswordText (no HTTP Basic Auth)
  PLOG_INFO << "[ONVIFTapoHandler] Trying Method 1: WS-Security PasswordText (no HTTP Basic Auth)";
  std::string request = buildSoapRequestWithSecurity(action, body, username, password, false);
  std::string response;
  if (httpClient_.sendSoapRequest(url, request, username, password, response, false)) {
    cachedAuthMethod_ = "false:false";
    PLOG_INFO << "[ONVIFTapoHandler] ✓ Method 1 succeeded! Caching for future requests";
    return response;
  }
  
  // Method 2: WS-Security PasswordDigest (no HTTP Basic Auth) - Most common for Tapo
  PLOG_WARNING << "[ONVIFTapoHandler] Method 1 failed, trying Method 2: WS-Security PasswordDigest (no HTTP Basic Auth)";
  request = buildSoapRequestWithSecurity(action, body, username, password, true);
  if (httpClient_.sendSoapRequest(url, request, username, password, response, false)) {
    cachedAuthMethod_ = "true:false";
    PLOG_INFO << "[ONVIFTapoHandler] ✓ Method 2 succeeded! Caching for future requests";
    return response;
  }
  
  // Method 3: WS-Security PasswordText + HTTP Basic Auth
  PLOG_WARNING << "[ONVIFTapoHandler] Method 2 failed, trying Method 3: WS-Security PasswordText + HTTP Basic Auth";
  request = buildSoapRequestWithSecurity(action, body, username, password, false);
  if (httpClient_.sendSoapRequest(url, request, username, password, response, true)) {
    cachedAuthMethod_ = "false:true";
    PLOG_INFO << "[ONVIFTapoHandler] ✓ Method 3 succeeded! Caching for future requests";
    return response;
  }
  
  // Method 4: WS-Security PasswordDigest + HTTP Basic Auth
  PLOG_WARNING << "[ONVIFTapoHandler] Method 3 failed, trying Method 4: WS-Security PasswordDigest + HTTP Basic Auth";
  request = buildSoapRequestWithSecurity(action, body, username, password, true);
  if (httpClient_.sendSoapRequest(url, request, username, password, response, true)) {
    cachedAuthMethod_ = "true:true";
    PLOG_INFO << "[ONVIFTapoHandler] ✓ Method 4 succeeded! Caching for future requests";
    return response;
  }
  
  PLOG_WARNING << "[ONVIFTapoHandler] All authentication methods failed";
  return "";
}

std::string ONVIFTapoHandler::sendSoapRequest(const std::string &url,
                                                const std::string &soapBody,
                                                const std::string &username,
                                                const std::string &password) {
  // Tapo cameras typically require WS-Security in SOAP header
  // When WS-Security is present, HTTP Basic Auth may not be needed (or may cause conflicts)
  // Try without HTTP Basic Auth first (WS-Security only)
  std::string response;
  PLOG_INFO << "[ONVIFTapoHandler] Sending SOAP request with WS-Security (no HTTP Basic Auth)";
  if (httpClient_.sendSoapRequest(url, soapBody, username, password, response, false)) {
    return response;
  }
  
  // If failed, try with both WS-Security and HTTP Basic Auth (some cameras may need both)
  PLOG_WARNING << "[ONVIFTapoHandler] Request failed with WS-Security only, trying with HTTP Basic Auth too";
  if (httpClient_.sendSoapRequest(url, soapBody, username, password, response, true)) {
    return response;
  }
  
  PLOG_WARNING << "[ONVIFTapoHandler] Request failed - Tapo may require Digest authentication or different credentials";
  return "";
}

