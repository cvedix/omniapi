#pragma once

#include <string>

/**
 * @brief HTTP Digest Authentication helper for ONVIF
 * 
 * Implements RFC 2617 Digest Authentication for ONVIF cameras
 */
class ONVIFDigestAuth {
public:
  /**
   * @brief Parse WWW-Authenticate header and extract digest parameters
   * @param wwwAuthHeader WWW-Authenticate header value
   * @param realm Output realm
   * @param nonce Output nonce
   * @param qop Output qop (quality of protection)
   * @param algorithm Output algorithm
   * @return true if parsed successfully
   */
  static bool parseWWWAuthenticate(const std::string &wwwAuthHeader,
                                   std::string &realm,
                                   std::string &nonce,
                                   std::string &qop,
                                   std::string &algorithm);

  /**
   * @brief Calculate digest response for HTTP Digest Authentication
   * @param username Username
   * @param password Password
   * @param realm Realm from WWW-Authenticate
   * @param nonce Nonce from WWW-Authenticate
   * @param method HTTP method (e.g., "POST")
   * @param uri Request URI
   * @param qop Quality of protection (optional)
   * @param nc Nonce count (optional, for qop)
   * @param cnonce Client nonce (optional, for qop)
   * @param algorithm Algorithm (default: "MD5")
   * @return Digest response string
   */
  static std::string calculateDigestResponse(const std::string &username,
                                             const std::string &password,
                                             const std::string &realm,
                                             const std::string &nonce,
                                             const std::string &method,
                                             const std::string &uri,
                                             const std::string &qop = "",
                                             const std::string &nc = "",
                                             const std::string &cnonce = "",
                                             const std::string &algorithm = "MD5");

  /**
   * @brief Build Authorization header for Digest Authentication
   * @param username Username
   * @param password Password
   * @param realm Realm
   * @param nonce Nonce
   * @param method HTTP method
   * @param uri Request URI
   * @param qop Quality of protection
   * @param nc Nonce count
   * @param cnonce Client nonce
   * @param algorithm Algorithm
   * @return Authorization header value
   */
  static std::string buildAuthorizationHeader(const std::string &username,
                                               const std::string &password,
                                               const std::string &realm,
                                               const std::string &nonce,
                                               const std::string &method,
                                               const std::string &uri,
                                               const std::string &qop = "",
                                               const std::string &nc = "",
                                               const std::string &cnonce = "",
                                               const std::string &algorithm = "MD5");

private:
  /**
   * @brief Calculate MD5 hash
   */
  static std::string md5(const std::string &input);

  /**
   * @brief Generate random nonce for client
   */
  static std::string generateCNonce();

  /**
   * @brief Extract value from digest header parameter
   */
  static std::string extractParam(const std::string &header, const std::string &param);
};

