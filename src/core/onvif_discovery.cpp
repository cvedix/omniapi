#include "core/onvif_discovery.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/onvif_http_client.h"
#include "core/onvif_soap_builder.h"
#include "core/onvif_xml_parser.h"
#include "core/onvif_camera_whitelist.h"
#include "core/onvif_camera_handler_factory.h"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <errno.h>
#include <map>
#include <netinet/in.h>
#include <regex>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

// WS-Discovery multicast address and port
#define WS_DISCOVERY_MULTICAST_ADDR "239.255.255.250"
#define WS_DISCOVERY_PORT 3702

ONVIFDiscovery::ONVIFDiscovery() : discovering_(false), socketFd_(-1) {}

ONVIFDiscovery::~ONVIFDiscovery() {
  if (socketFd_ >= 0) {
    close(socketFd_);
  }
}

std::vector<ONVIFCamera> ONVIFDiscovery::discoverCameras(int timeoutSeconds) {
  discovering_ = true;
  std::vector<ONVIFCamera> cameras;

  try {
    // Send Probe message
    if (!sendProbe()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[ONVIFDiscovery] Failed to send Probe message";
      }
      discovering_ = false;
      return cameras;
    }

    // Receive ProbeMatch responses
    cameras = receiveProbeMatches(timeoutSeconds);

    // Get device information for each camera
    for (auto &camera : cameras) {
      getDeviceInformation(camera);
    }

    // Register cameras (check whitelist)
    auto &registry = ONVIFCameraRegistry::getInstance();
    auto &whitelist = ONVIFCameraWhitelist::getInstance();
    
    for (const auto &camera : cameras) {
      // Check whitelist if enabled
      if (!whitelist.isEmpty()) {
        if (!camera.manufacturer.empty() &&
            !whitelist.isManufacturerSupported(camera.manufacturer)) {
          if (isApiLoggingEnabled()) {
            PLOG_DEBUG << "[ONVIFDiscovery] Skipping non-whitelisted camera: "
                       << camera.manufacturer << " " << camera.model;
          }
          continue;
        }
      }
      
      std::string cameraId = camera.uuid.empty() ? camera.ip : camera.uuid;
      registry.addCamera(cameraId, camera);
    }

  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[ONVIFDiscovery] Error during discovery: " << e.what();
    }
  }

  discovering_ = false;
  return cameras;
}

void ONVIFDiscovery::discoverCamerasAsync(int timeoutSeconds) {
  // For now, just call synchronously
  // In the future, this could spawn a thread
  discoverCameras(timeoutSeconds);
}

bool ONVIFDiscovery::isDiscovering() const { return discovering_; }

bool ONVIFDiscovery::sendProbe() {
  // Create UDP socket
  socketFd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (socketFd_ < 0) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[ONVIFDiscovery] Failed to create socket";
    }
    return false;
  }

  // Enable broadcast
  int broadcast = 1;
  if (setsockopt(socketFd_, SOL_SOCKET, SO_BROADCAST, &broadcast,
                 sizeof(broadcast)) < 0) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[ONVIFDiscovery] Failed to set socket broadcast option";
    }
    close(socketFd_);
    socketFd_ = -1;
    return false;
  }

  // Set up multicast address
  struct sockaddr_in multicastAddr;
  memset(&multicastAddr, 0, sizeof(multicastAddr));
  multicastAddr.sin_family = AF_INET;
  multicastAddr.sin_port = htons(WS_DISCOVERY_PORT);
  if (inet_pton(AF_INET, WS_DISCOVERY_MULTICAST_ADDR,
                &multicastAddr.sin_addr) <= 0) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[ONVIFDiscovery] Invalid multicast address";
    }
    close(socketFd_);
    socketFd_ = -1;
    return false;
  }

  // Create and send Probe message
  std::string probeMsg = createProbeMessage();
  ssize_t sent = sendto(socketFd_, probeMsg.c_str(), probeMsg.length(), 0,
                        (struct sockaddr *)&multicastAddr,
                        sizeof(multicastAddr));

  if (sent < 0) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[ONVIFDiscovery] Failed to send Probe message";
    }
    close(socketFd_);
    socketFd_ = -1;
    return false;
  }

  if (isApiLoggingEnabled()) {
    PLOG_DEBUG << "[ONVIFDiscovery] Sent Probe message";
  }

  return true;
}

std::vector<ONVIFCamera> ONVIFDiscovery::receiveProbeMatches(int timeoutSeconds) {
  std::vector<ONVIFCamera> cameras;

  if (socketFd_ < 0) {
    return cameras;
  }

  // Set receive timeout
  struct timeval tv;
  tv.tv_sec = timeoutSeconds;
  tv.tv_usec = 0;
  if (setsockopt(socketFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[ONVIFDiscovery] Failed to set receive timeout";
    }
  }

  char buffer[4096];
  std::map<std::string, ONVIFCamera> uniqueCameras; // Use UUID or IP as key

  // Receive responses
  auto startTime = std::chrono::steady_clock::now();
  while (true) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - startTime);
    if (elapsed.count() >= timeoutSeconds) {
      break;
    }

    struct sockaddr_in fromAddr;
    socklen_t fromLen = sizeof(fromAddr);
    ssize_t received = recvfrom(socketFd_, buffer, sizeof(buffer) - 1, 0,
                                 (struct sockaddr *)&fromAddr, &fromLen);

    if (received < 0) {
      // Timeout or error
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      break;
    }

    buffer[received] = '\0';
    std::string response(buffer, received);

    // Parse ProbeMatch
    ONVIFCamera camera = parseProbeMatch(response);
    if (!camera.ip.empty() || !camera.uuid.empty()) {
      std::string key = camera.uuid.empty() ? camera.ip : camera.uuid;
      uniqueCameras[key] = camera;
    }
  }

  // Convert map to vector
  for (const auto &[key, camera] : uniqueCameras) {
    cameras.push_back(camera);
  }

  return cameras;
}

ONVIFCamera ONVIFDiscovery::parseProbeMatch(const std::string &xmlResponse) {
  ONVIFCamera camera;

  // Extract UUID
  camera.uuid = extractUUID(xmlResponse);

  // Extract XAddrs (service endpoint)
  std::string xaddrs = extractXAddrs(xmlResponse);
  camera.endpoint = xaddrs;

  // Extract IP from XAddrs
  camera.ip = extractIPFromXAddrs(xaddrs);

  return camera;
}

std::string ONVIFDiscovery::extractIPFromXAddrs(const std::string &xaddrs) {
  // XAddrs format: http://192.168.1.100/onvif/device_service
  std::regex ipRegex(R"(http://([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+))");
  std::smatch match;
  if (std::regex_search(xaddrs, match, ipRegex)) {
    return match[1].str();
  }
  return "";
}

std::string ONVIFDiscovery::extractUUID(const std::string &xmlResponse) {
  // Look for wsa:EndpointReference or similar
  std::regex uuidRegex(R"(urn:uuid:([a-fA-F0-9\-]{36}))");
  std::smatch match;
  if (std::regex_search(xmlResponse, match, uuidRegex)) {
    return match[1].str();
  }
  return "";
}

std::string ONVIFDiscovery::extractXAddrs(const std::string &xmlResponse) {
  // Look for XAddrs element
  std::regex xaddrsRegex(R"(<[^>]*:XAddrs[^>]*>([^<]+)</[^>]*:XAddrs>)");
  std::smatch match;
  if (std::regex_search(xmlResponse, match, xaddrsRegex)) {
    return match[1].str();
  }
  return "";
}

std::string ONVIFDiscovery::createProbeMessage() {
  // WS-Discovery Probe message
  std::ostringstream oss;
  oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
         "xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
         "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\">\n"
      << "  <s:Header>\n"
      << "    <a:Action "
         "s:mustUnderstand=\"1\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</a:Action>\n"
      << "    <a:MessageID>uuid:" << std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count()
      << "</a:MessageID>\n"
      << "    <a:To "
         "s:mustUnderstand=\"1\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</a:To>\n"
      << "  </s:Header>\n"
      << "  <s:Body>\n"
      << "    <d:Probe>\n"
      << "      <d:Types>dn:NetworkVideoTransmitter</d:Types>\n"
      << "    </d:Probe>\n"
      << "  </s:Body>\n"
      << "</s:Envelope>";

  return oss.str();
}

bool ONVIFDiscovery::getDeviceInformation(ONVIFCamera &camera) {
  if (camera.endpoint.empty()) {
    PLOG_WARNING << "[ONVIFDiscovery] getDeviceInformation: Camera endpoint is empty";
    return false;
  }

  PLOG_INFO << "[ONVIFDiscovery] ========================================";
  PLOG_INFO << "[ONVIFDiscovery] Getting device information from: " << camera.endpoint;

  // Check whitelist
  auto &whitelist = ONVIFCameraWhitelist::getInstance();
  if (!whitelist.isEmpty() && !camera.manufacturer.empty()) {
    if (!whitelist.isManufacturerSupported(camera.manufacturer)) {
      PLOG_DEBUG << "[ONVIFDiscovery] Camera manufacturer not whitelisted: "
                 << camera.manufacturer;
      PLOG_DEBUG << "[ONVIFDiscovery] Still trying to get info, but camera may be unsupported";
    }
  }

  // Get handler from factory
  auto &factory = ONVIFCameraHandlerFactory::getInstance();
  auto handler = factory.getHandler(camera);
  PLOG_INFO << "[ONVIFDiscovery] Using handler: " << handler->getName() 
            << " for camera: " << camera.ip;

  // Try to get device information (no auth first, some cameras require auth)
  PLOG_INFO << "[ONVIFDiscovery] Attempting GetDeviceInformation without authentication";
  bool success = handler->getDeviceInformation(camera, "", "");

  if (!success) {
    PLOG_WARNING << "[ONVIFDiscovery] GetDeviceInformation failed without authentication";
    PLOG_INFO << "[ONVIFDiscovery] This might mean camera requires authentication for GetDeviceInformation";
    PLOG_INFO << "[ONVIFDiscovery] Camera will be registered with IP only, device info can be retrieved later";
    PLOG_INFO << "[ONVIFDiscovery] ========================================";
    // Don't return false - allow camera to be registered with just IP
    return false;
  }

  PLOG_INFO << "[ONVIFDiscovery] Successfully retrieved device information";
  PLOG_INFO << "[ONVIFDiscovery] Manufacturer: " << (camera.manufacturer.empty() ? "unknown" : camera.manufacturer)
            << ", Model: " << (camera.model.empty() ? "unknown" : camera.model)
            << ", Serial: " << (camera.serialNumber.empty() ? "unknown" : camera.serialNumber);
  PLOG_INFO << "[ONVIFDiscovery] ========================================";
  
  return success;
}

// Removed - now using ONVIFSoapBuilder and ONVIFXmlParser

std::string ONVIFDiscovery::sendHttpPost(const std::string &url,
                                         const std::string &body,
                                         const std::string &username,
                                         const std::string &password) {
  ONVIFHttpClient client;
  std::string response;
  
  if (client.sendSoapRequest(url, body, username, password, response)) {
    return response;
  }
  
  if (isApiLoggingEnabled()) {
    PLOG_WARNING << "[ONVIFDiscovery] HTTP POST to " << url << " failed";
  }
  return "";
}

