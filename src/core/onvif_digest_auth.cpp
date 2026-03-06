#include "core/onvif_digest_auth.h"
#include "core/logger.h"
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <sstream>
#include <iomanip>
#include <random>
#include <regex>

bool ONVIFDigestAuth::parseWWWAuthenticate(const std::string &wwwAuthHeader,
                                            std::string &realm,
                                            std::string &nonce,
                                            std::string &qop,
                                            std::string &algorithm) {
  realm.clear();
  nonce.clear();
  qop.clear();
  algorithm = "MD5"; // Default

  if (wwwAuthHeader.empty()) {
    return false;
  }

  // Check if it's Digest authentication
  if (wwwAuthHeader.find("Digest") == std::string::npos &&
      wwwAuthHeader.find("digest") == std::string::npos) {
    return false;
  }

  // Extract parameters using regex
  // Use delimiter for raw string to avoid issues with quotes inside
  std::regex realmRegex(R"delim(realm="([^"]+)")delim", std::regex::icase);
  std::regex nonceRegex(R"delim(nonce="([^"]+)")delim", std::regex::icase);
  std::regex qopRegex(R"delim(qop="([^"]+)")delim", std::regex::icase);
  std::regex algorithmRegex(R"delim(algorithm=([^,\s]+))delim", std::regex::icase);

  std::smatch match;
  if (std::regex_search(wwwAuthHeader, match, realmRegex)) {
    realm = match[1].str();
  }
  if (std::regex_search(wwwAuthHeader, match, nonceRegex)) {
    nonce = match[1].str();
  }
  if (std::regex_search(wwwAuthHeader, match, qopRegex)) {
    qop = match[1].str();
  }
  if (std::regex_search(wwwAuthHeader, match, algorithmRegex)) {
    algorithm = match[1].str();
  }

  PLOG_DEBUG << "[ONVIFDigestAuth] Parsed Digest parameters:";
  PLOG_DEBUG << "[ONVIFDigestAuth]   realm: " << realm;
  PLOG_DEBUG << "[ONVIFDigestAuth]   nonce: " << nonce;
  PLOG_DEBUG << "[ONVIFDigestAuth]   qop: " << qop;
  PLOG_DEBUG << "[ONVIFDigestAuth]   algorithm: " << algorithm;

  return !realm.empty() && !nonce.empty();
}

std::string ONVIFDigestAuth::md5(const std::string &input) {
  unsigned char digest[MD5_DIGEST_LENGTH];
  // Use EVP API instead of deprecated MD5() for OpenSSL 3.0 compatibility
  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
  if (!mdctx) {
    // Fallback: return empty string if EVP fails
    return "";
  }
  
  if (EVP_DigestInit_ex(mdctx, EVP_md5(), nullptr) != 1 ||
      EVP_DigestUpdate(mdctx, input.c_str(), input.length()) != 1 ||
      EVP_DigestFinal_ex(mdctx, digest, nullptr) != 1) {
    EVP_MD_CTX_free(mdctx);
    return "";
  }
  EVP_MD_CTX_free(mdctx);

  std::ostringstream oss;
  for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
  }
  return oss.str();
}

std::string ONVIFDigestAuth::generateCNonce() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);

  std::ostringstream oss;
  for (int i = 0; i < 16; ++i) {
    oss << std::hex << dis(gen);
  }
  return oss.str();
}

std::string ONVIFDigestAuth::extractParam(const std::string &header, const std::string &param) {
  // Build regex pattern manually to avoid raw string issues
  std::string pattern = param + "=\"([^\"]+)\"";
  std::regex regex(pattern, std::regex::icase);
  std::smatch match;
  if (std::regex_search(header, match, regex)) {
    return match[1].str();
  }
  return "";
}

std::string ONVIFDigestAuth::calculateDigestResponse(const std::string &username,
                                                      const std::string &password,
                                                      const std::string &realm,
                                                      const std::string &nonce,
                                                      const std::string &method,
                                                      const std::string &uri,
                                                      const std::string &qop,
                                                      const std::string &nc,
                                                      const std::string &cnonce,
                                                       const std::string &algorithm) {
  // Note: algorithm parameter is for future use (currently only MD5 supported)
  (void)algorithm; // Suppress unused parameter warning
  
  // Calculate HA1 = MD5(username:realm:password)
  std::string ha1Input = username + ":" + realm + ":" + password;
  std::string ha1 = md5(ha1Input);

  // Calculate HA2 = MD5(method:uri)
  std::string ha2Input = method + ":" + uri;
  std::string ha2 = md5(ha2Input);

  // Calculate response
  std::string responseInput;
  if (!qop.empty()) {
    // With qop: MD5(HA1:nonce:nc:cnonce:qop:HA2)
    responseInput = ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" + qop + ":" + ha2;
  } else {
    // Without qop: MD5(HA1:nonce:HA2)
    responseInput = ha1 + ":" + nonce + ":" + ha2;
  }

  std::string response = md5(responseInput);

  PLOG_DEBUG << "[ONVIFDigestAuth] Calculated digest response:";
  PLOG_DEBUG << "[ONVIFDigestAuth]   HA1: " << ha1;
  PLOG_DEBUG << "[ONVIFDigestAuth]   HA2: " << ha2;
  PLOG_DEBUG << "[ONVIFDigestAuth]   response: " << response;

  return response;
}

std::string ONVIFDigestAuth::buildAuthorizationHeader(const std::string &username,
                                                       const std::string &password,
                                                       const std::string &realm,
                                                       const std::string &nonce,
                                                       const std::string &method,
                                                       const std::string &uri,
                                                       const std::string &qop,
                                                       const std::string &nc,
                                                       const std::string &cnonce,
                                                       const std::string &algorithm) {
  std::string cnonceValue = cnonce.empty() ? generateCNonce() : cnonce;
  std::string ncValue = nc.empty() ? "00000001" : nc;

  std::string response = calculateDigestResponse(username, password, realm, nonce,
                                                  method, uri, qop, ncValue, cnonceValue, algorithm);

  std::ostringstream authHeader;
  authHeader << "Digest username=\"" << username << "\", "
             << "realm=\"" << realm << "\", "
             << "nonce=\"" << nonce << "\", "
             << "uri=\"" << uri << "\", "
             << "response=\"" << response << "\"";

  if (!qop.empty()) {
    authHeader << ", qop=" << qop
               << ", nc=" << ncValue
               << ", cnonce=\"" << cnonceValue << "\"";
  }

  if (!algorithm.empty() && algorithm != "MD5") {
    authHeader << ", algorithm=" << algorithm;
  }

  return authHeader.str();
}

