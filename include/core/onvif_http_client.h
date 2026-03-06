#pragma once

#include "core/onvif_credentials_manager.h"
#include <drogon/HttpClient.h>
#include <functional>
#include <memory>
#include <string>

/**
 * @brief ONVIF HTTP Client Wrapper
 *
 * Wraps Drogon HttpClient for ONVIF SOAP calls with authentication support.
 */
class ONVIFHttpClient {
public:
  /**
   * @brief Constructor
   */
  ONVIFHttpClient();

  /**
   * @brief Destructor
   */
  ~ONVIFHttpClient();

  /**
   * @brief Send SOAP request synchronously
   *
   * @param url Service URL
   * @param soapBody SOAP request body
   * @param username Username for authentication
   * @param password Password for authentication
   * @param response Output response body
   * @param useHttpAuth If true, add HTTP Basic Auth header (default: true)
   *                    Set to false when using WS-Security only
   * @return true if successful
   */
  bool sendSoapRequest(const std::string &url, const std::string &soapBody,
                       const std::string &username, const std::string &password,
                       std::string &response, bool useHttpAuth = true);

  /**
   * @brief Send SOAP request asynchronously
   *
   * @param url Service URL
   * @param soapBody SOAP request body
   * @param username Username for authentication
   * @param password Password for authentication
   * @param callback Callback function
   */
  void sendSoapRequestAsync(
      const std::string &url, const std::string &soapBody,
      const std::string &username, const std::string &password,
      std::function<void(bool success, const std::string &response)> callback);

  /**
   * @brief Set timeout for requests (in seconds)
   */
  void setTimeout(int seconds);

private:
  /**
   * @brief Internal method to send SOAP request with specific auth method
   * @param digestAuthHeader If provided, use Digest auth instead of Basic
   * @param wwwAuthHeader Output: WWW-Authenticate header from response (if 401)
   */
  bool sendSoapRequestWithAuth(const std::string &url,
                                const std::string &soapBody,
                                const std::string &username,
                                const std::string &password,
                                std::string &response,
                                bool useHttpAuth,
                                const std::string &digestAuthHeader,
                                std::string &wwwAuthHeader);

  /**
   * @brief Create HTTP request with SOAP body and authentication
   * @param useHttpAuth If true, add HTTP Basic Auth header (default: true)
   *                    Set to false when using WS-Security only
   * @param digestAuthHeader If provided, use Digest auth instead of Basic
   */
  drogon::HttpRequestPtr createRequest(const std::string &soapBody,
                                       const std::string &username,
                                       const std::string &password,
                                       bool useHttpAuth = true,
                                       const std::string &digestAuthHeader = "");

  /**
   * @brief Extract service URL from camera endpoint
   */
  std::string extractServiceUrl(const std::string &baseUrl,
                                 const std::string &serviceType);

  int timeoutSeconds_;
};

