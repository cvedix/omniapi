#include "core/onvif_xml_parser.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include <algorithm>
#include <regex>
#include <sstream>

bool ONVIFXmlParser::parseDeviceInformation(const std::string &xmlResponse,
                                             ONVIFCamera &camera) {
  // Try multiple namespace variations
  // First try with tds: namespace (Device service)
  camera.manufacturer = extractElementValue(xmlResponse, "Manufacturer", "tds");
  if (camera.manufacturer.empty()) {
    camera.manufacturer = extractElementValue(xmlResponse, "Manufacturer", "tt");
  }
  if (camera.manufacturer.empty()) {
    camera.manufacturer = extractElementValue(xmlResponse, "Manufacturer");
  }
  
  // Extract model
  camera.model = extractElementValue(xmlResponse, "Model", "tds");
  if (camera.model.empty()) {
    camera.model = extractElementValue(xmlResponse, "Model", "tt");
  }
  if (camera.model.empty()) {
    camera.model = extractElementValue(xmlResponse, "Model");
  }
  
  // Extract serial number
  camera.serialNumber = extractElementValue(xmlResponse, "SerialNumber", "tds");
  if (camera.serialNumber.empty()) {
    camera.serialNumber = extractElementValue(xmlResponse, "SerialNumber", "tt");
  }
  if (camera.serialNumber.empty()) {
    camera.serialNumber = extractElementValue(xmlResponse, "SerialNumber");
  }
  
  // Extract firmware version (optional)
  std::string firmware = extractElementValue(xmlResponse, "FirmwareVersion");
  
  // Also try to extract from GetDeviceInformationResponse wrapper
  // Some cameras wrap the response differently
  if (camera.manufacturer.empty() && camera.model.empty()) {
    // Try to find in GetDeviceInformationResponse element
    // Find the start and end positions manually (since dotall is not available in C++11)
    std::regex startRegex(
        R"(<[^>]*:GetDeviceInformationResponse[^>]*>)",
        std::regex::icase);
    std::regex endRegex(
        R"(</[^>]*:GetDeviceInformationResponse>)",
        std::regex::icase);
    std::smatch startMatch, endMatch;
    
    if (std::regex_search(xmlResponse, startMatch, startRegex)) {
      size_t startPos = startMatch.position() + startMatch.length();
      std::string remaining = xmlResponse.substr(startPos);
      
      if (std::regex_search(remaining, endMatch, endRegex)) {
        size_t endPos = endMatch.position();
        std::string responseBody = remaining.substr(0, endPos);
        camera.manufacturer = extractElementValue(responseBody, "Manufacturer");
        camera.model = extractElementValue(responseBody, "Model");
        camera.serialNumber = extractElementValue(responseBody, "SerialNumber");
      }
    }
  }
  
  return !camera.manufacturer.empty() || !camera.model.empty();
}

bool ONVIFXmlParser::parseCapabilities(const std::string &xmlResponse,
                                        std::string &deviceServiceUrl,
                                        std::string &mediaServiceUrl,
                                        std::string &ptzServiceUrl) {
  // Look for Device service XAddr
  deviceServiceUrl = extractElementValue(xmlResponse, "XAddr", "Device");
  if (deviceServiceUrl.empty()) {
    // Try alternative patterns
    std::regex deviceRegex(
        R"(<[^>]*:Device[^>]*>.*?<[^>]*:XAddr[^>]*>([^<]+)</[^>]*:XAddr>)",
        std::regex::icase);
    std::smatch match;
    if (std::regex_search(xmlResponse, match, deviceRegex)) {
      deviceServiceUrl = match[1].str();
    }
  }

  // Look for Media service XAddr
  mediaServiceUrl = extractElementValue(xmlResponse, "XAddr", "Media");
  if (mediaServiceUrl.empty()) {
    std::regex mediaRegex(
        R"(<[^>]*:Media[^>]*>.*?<[^>]*:XAddr[^>]*>([^<]+)</[^>]*:XAddr>)",
        std::regex::icase);
    std::smatch match;
    if (std::regex_search(xmlResponse, match, mediaRegex)) {
      mediaServiceUrl = match[1].str();
    }
  }

  // Look for PTZ service XAddr
  ptzServiceUrl = extractElementValue(xmlResponse, "XAddr", "PTZ");
  if (ptzServiceUrl.empty()) {
    std::regex ptzRegex(
        R"(<[^>]*:PTZ[^>]*>.*?<[^>]*:XAddr[^>]*>([^<]+)</[^>]*:XAddr>)",
        std::regex::icase);
    std::smatch match;
    if (std::regex_search(xmlResponse, match, ptzRegex)) {
      ptzServiceUrl = match[1].str();
    }
  }

  return !deviceServiceUrl.empty() || !mediaServiceUrl.empty();
}

std::vector<std::string> ONVIFXmlParser::parseProfiles(const std::string &xmlResponse) {
  std::vector<std::string> profiles;
  
  PLOG_DEBUG << "[ONVIFXmlParser] parseProfiles: Starting to parse XML response";
  PLOG_DEBUG << "[ONVIFXmlParser] parseProfiles: XML length: " << xmlResponse.length();

  // Look for Profile elements with token attribute
  std::regex profileRegex(
      R"(<[^>]*:Profile[^>]*token\s*=\s*["']([^"']+)["'][^>]*>)",
      std::regex::icase);
  std::sregex_iterator iter(xmlResponse.begin(), xmlResponse.end(), profileRegex);
  std::sregex_iterator end;

  int count = 0;
  for (; iter != end; ++iter) {
    std::string token = iter->str(1);
    profiles.push_back(token);
    PLOG_DEBUG << "[ONVIFXmlParser] parseProfiles: Found profile token (attribute): " << token;
    count++;
  }
  
  PLOG_DEBUG << "[ONVIFXmlParser] parseProfiles: Found " << count << " profile(s) with token attribute";

  // If no profiles found with token attribute, try to find ProfileToken elements
  if (profiles.empty()) {
    PLOG_DEBUG << "[ONVIFXmlParser] parseProfiles: No profiles with token attribute, trying ProfileToken elements";
    std::regex tokenRegex(
        R"(<[^>]*:ProfileToken[^>]*>([^<]+)</[^>]*:ProfileToken>)",
        std::regex::icase);
    std::sregex_iterator tokenIter(xmlResponse.begin(), xmlResponse.end(), tokenRegex);
    std::sregex_iterator tokenEnd;

    count = 0;
    for (; tokenIter != tokenEnd; ++tokenIter) {
      std::string token = tokenIter->str(1);
      // Trim whitespace
      token.erase(0, token.find_first_not_of(" \t\n\r"));
      token.erase(token.find_last_not_of(" \t\n\r") + 1);
      if (!token.empty()) {
        profiles.push_back(token);
        PLOG_DEBUG << "[ONVIFXmlParser] parseProfiles: Found profile token (element): " << token;
        count++;
      }
    }
    PLOG_DEBUG << "[ONVIFXmlParser] parseProfiles: Found " << count << " profile token element(s)";
  }

  if (profiles.empty()) {
    PLOG_WARNING << "[ONVIFXmlParser] parseProfiles: No profiles found in XML response";
    PLOG_WARNING << "[ONVIFXmlParser] parseProfiles: XML preview: " 
                 << xmlResponse.substr(0, std::min(500UL, xmlResponse.length()));
  } else {
    PLOG_INFO << "[ONVIFXmlParser] parseProfiles: Successfully parsed " << profiles.size() << " profile(s)";
  }

  return profiles;
}

std::string ONVIFXmlParser::parseStreamUri(const std::string &xmlResponse) {
  PLOG_DEBUG << "[ONVIFXmlParser] parseStreamUri: Parsing stream URI from XML";
  std::string uri = extractElementValue(xmlResponse, "Uri");
  if (uri.empty()) {
    PLOG_WARNING << "[ONVIFXmlParser] parseStreamUri: No URI found in XML response";
    PLOG_WARNING << "[ONVIFXmlParser] parseStreamUri: XML preview: " 
                 << xmlResponse.substr(0, std::min(500UL, xmlResponse.length()));
  } else {
    PLOG_INFO << "[ONVIFXmlParser] parseStreamUri: Found stream URI: " << uri;
  }
  return uri;
}

bool ONVIFXmlParser::parseVideoEncoderConfiguration(
    const std::string &xmlResponse, int &width, int &height, int &fps) {
  // Extract width
  std::string widthStr = extractElementValue(xmlResponse, "Width");
  if (!widthStr.empty()) {
    try {
      width = std::stoi(widthStr);
    } catch (...) {
      width = 0;
    }
  }

  // Extract height
  std::string heightStr = extractElementValue(xmlResponse, "Height");
  if (!heightStr.empty()) {
    try {
      height = std::stoi(heightStr);
    } catch (...) {
      height = 0;
    }
  }

  // Extract frame rate
  std::string fpsStr = extractElementValue(xmlResponse, "FrameRateLimit");
  if (fpsStr.empty()) {
    fpsStr = extractElementValue(xmlResponse, "FrameRate");
  }
  if (!fpsStr.empty()) {
    try {
      fps = std::stoi(fpsStr);
    } catch (...) {
      fps = 0;
    }
  }

  return width > 0 && height > 0;
}

std::string ONVIFXmlParser::parseSnapshotUri(const std::string &xmlResponse) {
  return extractElementValue(xmlResponse, "Uri");
}

std::string ONVIFXmlParser::extractElementValue(const std::string &xml,
                                                  const std::string &elementName,
                                                  const std::string &namespacePrefix) {
  // Try with namespace prefix first
  if (!namespacePrefix.empty()) {
    std::string prefixedName = namespacePrefix + ":" + elementName;
    std::ostringstream pattern1;
    pattern1 << R"(<[^>]*:)" << elementName << R"([^>]*>([^<]+)</[^>]*:)" << elementName << R"(>)";
    std::regex regex1(pattern1.str(), std::regex::icase);
    std::smatch match;
    if (std::regex_search(xml, match, regex1)) {
      std::string value = match[1].str();
      value.erase(0, value.find_first_not_of(" \t\n\r"));
      value.erase(value.find_last_not_of(" \t\n\r") + 1);
      return value;
    }
  }

  // Try without namespace (any namespace)
  std::ostringstream pattern;
  pattern << R"(<[^>]*:)" << elementName << R"([^>]*>([^<]+)</[^>]*:)" << elementName << R"(>)";
  std::regex regex2(pattern.str(), std::regex::icase);
  std::smatch match;
  if (std::regex_search(xml, match, regex2)) {
    std::string value = match[1].str();
    // Trim whitespace
    value.erase(0, value.find_first_not_of(" \t\n\r"));
    value.erase(value.find_last_not_of(" \t\n\r") + 1);
    return value;
  }

  // Try with tt: namespace (common in ONVIF)
  std::ostringstream pattern2;
  pattern2 << R"(<tt:)" << elementName << R"([^>]*>([^<]+)</tt:)" << elementName << R"(>)";
  std::regex regex3(pattern2.str(), std::regex::icase);
  if (std::regex_search(xml, match, regex3)) {
    std::string value = match[1].str();
    value.erase(0, value.find_first_not_of(" \t\n\r"));
    value.erase(value.find_last_not_of(" \t\n\r") + 1);
    return value;
  }

  return "";
}

std::string ONVIFXmlParser::extractAttributeValue(const std::string &xml,
                                                   const std::string &elementName,
                                                   const std::string &attributeName) {
  std::ostringstream pattern;
  pattern << R"(<[^>]*:)" << elementName << R"([^>]*)" << attributeName
          << R"(=["']([^"']+)["'][^>]*>)";
  std::regex regex(pattern.str(), std::regex::icase);
  std::smatch match;
  if (std::regex_search(xml, match, regex)) {
    return match[1].str();
  }
  return "";
}

std::string ONVIFXmlParser::removeNamespaces(const std::string &xml) {
  // Simple namespace removal - replace namespace:element with element
  std::string result = xml;
  std::regex nsRegex(R"(([a-zA-Z0-9]+):([a-zA-Z0-9]+))");
  result = std::regex_replace(result, nsRegex, "$2");
  return result;
}


