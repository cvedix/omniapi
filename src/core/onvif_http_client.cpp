#include "core/onvif_http_client.h"
#include "core/onvif_digest_auth.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/utils/Utilities.h>
#include <chrono>
#include <future>
#include <mutex>
#include <sstream>

ONVIFHttpClient::ONVIFHttpClient() : timeoutSeconds_(10) {}

ONVIFHttpClient::~ONVIFHttpClient() {}

bool ONVIFHttpClient::sendSoapRequest(const std::string &url,
                                       const std::string &soapBody,
                                       const std::string &username,
                                       const std::string &password,
                                       std::string &response,
                                       bool useHttpAuth) {
  // Extract URI path for Digest authentication
  std::string uriPath = url;
  size_t pathStart = uriPath.find("://");
  if (pathStart != std::string::npos) {
    pathStart = uriPath.find("/", pathStart + 3);
    if (pathStart != std::string::npos) {
      uriPath = uriPath.substr(pathStart);
    } else {
      uriPath = "/";
    }
  }
  
  // Try Basic Auth first (or WS-Security only)
  std::string wwwAuthHeader;
  bool success = sendSoapRequestWithAuth(url, soapBody, username, password, response, useHttpAuth, "", wwwAuthHeader);
  
  // If failed with 401/400 and WWW-Authenticate header present, try Digest Auth
  if (!success && !wwwAuthHeader.empty() && !username.empty() && !password.empty()) {
    std::string realm, nonce, qop, algorithm;
    if (ONVIFDigestAuth::parseWWWAuthenticate(wwwAuthHeader, realm, nonce, qop, algorithm)) {
      PLOG_INFO << "[ONVIFHttpClient] Retrying with Digest Authentication";
      std::string digestAuth = ONVIFDigestAuth::buildAuthorizationHeader(
          username, password, realm, nonce, "POST", uriPath, qop, "00000001", "", algorithm);
      success = sendSoapRequestWithAuth(url, soapBody, username, password, response, false, digestAuth, wwwAuthHeader);
    }
  }
  
  return success;
}

bool ONVIFHttpClient::sendSoapRequestWithAuth(const std::string &url,
                                               const std::string &soapBody,
                                               const std::string &username,
                                               const std::string &password,
                                               std::string &response,
                                               bool useHttpAuth,
                                               const std::string &digestAuthHeader,
                                               std::string &wwwAuthHeader) {
  // Create HTTP client
  auto client = drogon::HttpClient::newHttpClient(url);
  // Note: Drogon HttpClient timeout is handled via future.wait_for() below

  // Log request details
  PLOG_INFO << "[ONVIFHttpClient] ========================================";
  PLOG_INFO << "[ONVIFHttpClient] Sending SOAP request";
  PLOG_INFO << "[ONVIFHttpClient] URL: " << url;
  PLOG_INFO << "[ONVIFHttpClient] Username: " << (username.empty() ? "none" : username);
  PLOG_INFO << "[ONVIFHttpClient] Request body length: " << soapBody.length();
  PLOG_INFO << "[ONVIFHttpClient] Use HTTP Basic Auth: " << (useHttpAuth ? "yes" : "no (WS-Security only)");
  if (!digestAuthHeader.empty()) {
    PLOG_INFO << "[ONVIFHttpClient] Using Digest Authentication";
  }
  
  // Log SOAP request body (first 1000 chars)
  if (soapBody.length() > 0) {
    std::string preview = soapBody.substr(0, std::min(1000UL, soapBody.length()));
    PLOG_INFO << "[ONVIFHttpClient] SOAP Request (preview):";
    PLOG_INFO << preview;
    if (soapBody.length() > 1000) {
      PLOG_INFO << "[ONVIFHttpClient] ... (truncated, total " << soapBody.length() << " chars)";
    }
  }

  // Create request
  auto req = createRequest(soapBody, username, password, useHttpAuth, digestAuthHeader);
  
  // Log request headers
  PLOG_DEBUG << "[ONVIFHttpClient] Request headers:";
  PLOG_DEBUG << "[ONVIFHttpClient]   Content-Type: " << req->getHeader("Content-Type");
  PLOG_DEBUG << "[ONVIFHttpClient]   Content-Length: " << req->getHeader("Content-Length");
  if (!username.empty()) {
    PLOG_DEBUG << "[ONVIFHttpClient]   Authorization: Basic [REDACTED]";
  }

  // Send request synchronously using promise/future
  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();
  std::string responseBody;
  std::string urlCopy = url; // Copy for lambda capture
  int timeoutCopy = timeoutSeconds_; // Copy for lambda capture
  std::string &wwwAuthRef = wwwAuthHeader; // Reference for lambda capture

  client->sendRequest(
      req,
      [&promise, &responseBody, &wwwAuthRef, urlCopy, timeoutCopy](drogon::ReqResult result,
                                  const drogon::HttpResponsePtr &resp) {
        if (result == drogon::ReqResult::Ok && resp) {
          int statusCode = resp->statusCode();
          PLOG_INFO << "[ONVIFHttpClient] Response from " << urlCopy 
                    << " - Status: " << statusCode;
          
          // Log response headers for debugging (especially WWW-Authenticate for Tapo cameras)
          if (statusCode == drogon::k401Unauthorized || statusCode == drogon::k400BadRequest) {
            // Log all headers first (at INFO level for auth failures)
            PLOG_INFO << "[ONVIFHttpClient] Response headers:";
            auto headers = resp->getHeaders();
            for (const auto &[key, value] : headers) {
              PLOG_INFO << "[ONVIFHttpClient]   " << key << ": " << value;
            }
            
            // Check WWW-Authenticate header specifically
            auto wwwAuth = resp->getHeader("WWW-Authenticate");
            if (wwwAuth.empty()) {
              wwwAuth = resp->getHeader("www-authenticate"); // Try lowercase
            }
            
            if (!wwwAuth.empty()) {
              PLOG_INFO << "[ONVIFHttpClient] Response header WWW-Authenticate: " << wwwAuth;
              // Store WWW-Authenticate header for potential Digest auth retry
              wwwAuthRef = wwwAuth;
              if (wwwAuth.find("Digest") != std::string::npos || wwwAuth.find("digest") != std::string::npos) {
                PLOG_WARNING << "[ONVIFHttpClient] ⚠ Camera requires DIGEST authentication (e.g., Tapo cameras)";
                PLOG_INFO << "[ONVIFHttpClient] Will retry with Digest authentication";
              }
            } else {
              PLOG_INFO << "[ONVIFHttpClient] No WWW-Authenticate header found in response";
              PLOG_WARNING << "[ONVIFHttpClient] Camera may require WS-Security headers in SOAP request";
            }
          }
          
          auto bodyView = resp->body();
          std::string bodyStr = std::string(bodyView);
          
          if (statusCode == drogon::k200OK ||
              statusCode == drogon::k500InternalServerError) {
            // ONVIF sometimes returns 500 for SOAP faults, but body contains
            // valid XML
            responseBody = bodyStr;
            PLOG_INFO << "[ONVIFHttpClient] Response body length: " << responseBody.length();
            if (responseBody.empty()) {
              PLOG_WARNING << "[ONVIFHttpClient] Response body is empty despite status " << statusCode;
            }
            promise.set_value(true);
          } else if (statusCode == drogon::k400BadRequest) {
            // Some cameras return 400 with SOAP fault in body - try to parse it
            PLOG_WARNING << "[ONVIFHttpClient] HTTP 400 Bad Request received";
            PLOG_WARNING << "[ONVIFHttpClient] Full error response body:";
            PLOG_WARNING << bodyStr;
            
            // Check if it's a SOAP fault and parse it
            if (bodyStr.find("SOAP-ENV:Fault") != std::string::npos || 
                bodyStr.find("soap:Fault") != std::string::npos ||
                bodyStr.find("Fault") != std::string::npos) {
              PLOG_WARNING << "[ONVIFHttpClient] Response contains SOAP fault - parsing details";
              
              // Parse SOAP fault details
              std::string faultCode, faultSubcode, faultReason;
              
              // Try to extract fault code (SOAP 1.2 format: <SOAP-ENV:Value>)
              size_t valueStart = bodyStr.find("<SOAP-ENV:Value>");
              if (valueStart != std::string::npos) {
                valueStart += 16; // length of "<SOAP-ENV:Value>"
                size_t valueEnd = bodyStr.find("</SOAP-ENV:Value>", valueStart);
                if (valueEnd != std::string::npos) {
                  faultCode = bodyStr.substr(valueStart, valueEnd - valueStart);
                }
              }
              
              // Try to extract subcode (ter:NotAuthorized, etc.)
              size_t subcodeStart = bodyStr.find("<SOAP-ENV:Subcode>");
              if (subcodeStart != std::string::npos) {
                size_t subValueStart = bodyStr.find("<SOAP-ENV:Value>", subcodeStart);
                if (subValueStart != std::string::npos) {
                  subValueStart += 16;
                  size_t subValueEnd = bodyStr.find("</SOAP-ENV:Value>", subValueStart);
                  if (subValueEnd != std::string::npos) {
                    faultSubcode = bodyStr.substr(subValueStart, subValueEnd - subValueStart);
                  }
                }
              }
              
              // Try to extract fault reason (SOAP 1.2 format: <SOAP-ENV:Text>)
              size_t textStart = bodyStr.find("<SOAP-ENV:Text");
              if (textStart != std::string::npos) {
                textStart = bodyStr.find(">", textStart);
                if (textStart != std::string::npos) {
                  textStart += 1;
                  size_t textEnd = bodyStr.find("</SOAP-ENV:Text>", textStart);
                  if (textEnd != std::string::npos) {
                    faultReason = bodyStr.substr(textStart, textEnd - textStart);
                  }
                }
              }
              
              // Log parsed fault details
              PLOG_ERROR << "[ONVIFHttpClient] ========== SOAP FAULT DETAILS ==========";
              if (!faultCode.empty()) {
                PLOG_ERROR << "[ONVIFHttpClient] Fault Code: " << faultCode;
              }
              if (!faultSubcode.empty()) {
                PLOG_ERROR << "[ONVIFHttpClient] Fault Subcode: " << faultSubcode;
                
                // Provide specific guidance based on fault subcode
                if (faultSubcode.find("NotAuthorized") != std::string::npos) {
                  PLOG_ERROR << "[ONVIFHttpClient] ERROR: Not Authorized - Authentication failed!";
                  PLOG_ERROR << "[ONVIFHttpClient] Possible causes:";
                  PLOG_ERROR << "[ONVIFHttpClient]   1. Wrong username or password";
                  PLOG_ERROR << "[ONVIFHttpClient]   2. Camera requires Digest authentication (not Basic)";
                  PLOG_ERROR << "[ONVIFHttpClient]      ⚠ NOTE: Tapo cameras typically require Digest auth";
                  PLOG_ERROR << "[ONVIFHttpClient]   3. User account doesn't have required permissions";
                  PLOG_ERROR << "[ONVIFHttpClient]   4. Camera requires WS-Security headers in SOAP request";
                  PLOG_ERROR << "[ONVIFHttpClient]      ⚠ NOTE: Some Tapo cameras require WS-Security";
                  PLOG_ERROR << "[ONVIFHttpClient] SOLUTION:";
                  PLOG_ERROR << "[ONVIFHttpClient]   - Verify username/password are correct";
                  PLOG_ERROR << "[ONVIFHttpClient]   - For Tapo cameras: Digest auth or WS-Security may be required";
                  PLOG_ERROR << "[ONVIFHttpClient]   - Check if camera supports Basic auth or requires Digest";
                  PLOG_ERROR << "[ONVIFHttpClient]   - Check user permissions on camera";
                  PLOG_ERROR << "[ONVIFHttpClient]   - Check camera ONVIF settings (may need to enable ONVIF)";
                }
              }
              if (!faultReason.empty()) {
                PLOG_ERROR << "[ONVIFHttpClient] Fault Reason: " << faultReason;
              }
              PLOG_ERROR << "[ONVIFHttpClient] ========================================";
            }
            
            // Don't set responseBody for 400 - let caller handle it
            promise.set_value(false);
          } else {
            // Log detailed error information for other status codes
            PLOG_WARNING << "[ONVIFHttpClient] HTTP error status: " << statusCode;
            if (bodyView.length() > 0) {
              PLOG_WARNING << "[ONVIFHttpClient] Full error response body:";
              PLOG_WARNING << bodyStr;
            }
            
            // Check for authentication required (401)
            if (statusCode == drogon::k401Unauthorized) {
              PLOG_ERROR << "[ONVIFHttpClient] ========== AUTHENTICATION ERROR ==========";
              PLOG_ERROR << "[ONVIFHttpClient] HTTP 401 Unauthorized - Authentication failed";
              PLOG_ERROR << "[ONVIFHttpClient] URL: " << urlCopy;
              
              // Check for WWW-Authenticate header (indicates Digest auth required)
              auto wwwAuth = resp->getHeader("WWW-Authenticate");
              if (!wwwAuth.empty()) {
                PLOG_ERROR << "[ONVIFHttpClient] WWW-Authenticate header: " << wwwAuth;
                if (wwwAuth.find("Digest") != std::string::npos) {
                  PLOG_ERROR << "[ONVIFHttpClient] ⚠ Camera requires DIGEST authentication (not Basic)";
                  PLOG_ERROR << "[ONVIFHttpClient] Current implementation only supports Basic auth";
                  PLOG_ERROR << "[ONVIFHttpClient] SOLUTION: Digest authentication needs to be implemented";
                }
              } else {
                PLOG_WARNING << "[ONVIFHttpClient] No WWW-Authenticate header found";
              }
              
              PLOG_ERROR << "[ONVIFHttpClient] Possible causes:";
              PLOG_ERROR << "[ONVIFHttpClient]   1. Wrong username or password";
              PLOG_ERROR << "[ONVIFHttpClient]   2. Camera requires Digest authentication (e.g., Tapo cameras)";
              PLOG_ERROR << "[ONVIFHttpClient]   3. Camera requires WS-Security headers in SOAP request";
              PLOG_ERROR << "[ONVIFHttpClient]   4. User account doesn't have required permissions";
              PLOG_ERROR << "[ONVIFHttpClient] ========================================";
            }
            
            promise.set_value(false);
          }
        } else {
          // Log request failure details
          PLOG_ERROR << "[ONVIFHttpClient] ========== REQUEST FAILURE ==========";
          PLOG_ERROR << "[ONVIFHttpClient] Request failed - Result code: " 
                      << static_cast<int>(result);
          
          if (result == drogon::ReqResult::BadServerAddress) {
            PLOG_ERROR << "[ONVIFHttpClient] ERROR: Bad server address";
            PLOG_ERROR << "[ONVIFHttpClient] URL: " << urlCopy;
            PLOG_ERROR << "[ONVIFHttpClient] Possible causes:";
            PLOG_ERROR << "[ONVIFHttpClient]   1. Invalid URL format";
            PLOG_ERROR << "[ONVIFHttpClient]   2. DNS resolution failed";
            PLOG_ERROR << "[ONVIFHttpClient]   3. Camera is offline";
          } else if (result == drogon::ReqResult::NetworkFailure) {
            PLOG_ERROR << "[ONVIFHttpClient] ERROR: Network failure";
            PLOG_ERROR << "[ONVIFHttpClient] URL: " << urlCopy;
            PLOG_ERROR << "[ONVIFHttpClient] Possible causes:";
            PLOG_ERROR << "[ONVIFHttpClient]   1. Camera is not reachable (network issue)";
            PLOG_ERROR << "[ONVIFHttpClient]   2. Firewall blocking connection";
            PLOG_ERROR << "[ONVIFHttpClient]   3. Camera service is down";
            PLOG_ERROR << "[ONVIFHttpClient]   4. Camera rejected connection (authentication issue)";
            PLOG_ERROR << "[ONVIFHttpClient] SOLUTION:";
            PLOG_ERROR << "[ONVIFHttpClient]   - Check if camera is online: ping " << urlCopy;
            PLOG_ERROR << "[ONVIFHttpClient]   - Check network connectivity";
            PLOG_ERROR << "[ONVIFHttpClient]   - Verify camera ONVIF service is enabled";
            PLOG_ERROR << "[ONVIFHttpClient]   - Check firewall rules";
          } else if (result == drogon::ReqResult::Timeout) {
            PLOG_ERROR << "[ONVIFHttpClient] ERROR: Request timeout";
            PLOG_ERROR << "[ONVIFHttpClient] URL: " << urlCopy;
            PLOG_ERROR << "[ONVIFHttpClient] Timeout: " << timeoutCopy << " seconds";
            PLOG_ERROR << "[ONVIFHttpClient] Possible causes:";
            PLOG_ERROR << "[ONVIFHttpClient]   1. Camera is slow to respond";
            PLOG_ERROR << "[ONVIFHttpClient]   2. Network latency is high";
            PLOG_ERROR << "[ONVIFHttpClient]   3. Camera is processing request but taking too long";
            PLOG_ERROR << "[ONVIFHttpClient] SOLUTION:";
            PLOG_ERROR << "[ONVIFHttpClient]   - Increase timeout if needed";
            PLOG_ERROR << "[ONVIFHttpClient]   - Check network latency";
            PLOG_ERROR << "[ONVIFHttpClient]   - Verify camera is responsive";
          } else {
            PLOG_ERROR << "[ONVIFHttpClient] Unknown error code: " << static_cast<int>(result);
          }
          
          PLOG_ERROR << "[ONVIFHttpClient] ========================================";
          promise.set_value(false);
        }
      });

  // Wait for response (with timeout)
  auto startTime = std::chrono::steady_clock::now();
  if (future.wait_for(std::chrono::seconds(timeoutSeconds_ + 2)) ==
      std::future_status::timeout) {
    auto elapsed = std::chrono::steady_clock::now() - startTime;
    auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    PLOG_ERROR << "[ONVIFHttpClient] ========== TIMEOUT ERROR ==========";
    PLOG_ERROR << "[ONVIFHttpClient] Request timed out after " << elapsedSeconds << " seconds";
    PLOG_ERROR << "[ONVIFHttpClient] URL: " << url;
    PLOG_ERROR << "[ONVIFHttpClient] Configured timeout: " << timeoutSeconds_ << " seconds";
    PLOG_ERROR << "[ONVIFHttpClient] Possible causes:";
    PLOG_ERROR << "[ONVIFHttpClient]   1. Camera is not responding";
    PLOG_ERROR << "[ONVIFHttpClient]   2. Network latency is too high";
    PLOG_ERROR << "[ONVIFHttpClient]   3. Camera is processing but taking too long";
    PLOG_ERROR << "[ONVIFHttpClient]   4. Authentication issue causing camera to hang";
    PLOG_ERROR << "[ONVIFHttpClient] SOLUTION:";
    PLOG_ERROR << "[ONVIFHttpClient]   - Check camera status and connectivity";
    PLOG_ERROR << "[ONVIFHttpClient]   - Verify network connection";
    PLOG_ERROR << "[ONVIFHttpClient]   - Consider increasing timeout";
    PLOG_ERROR << "[ONVIFHttpClient] ========================================";
    return false;
  }

  bool success = future.get();
  if (success) {
    response = responseBody;
    PLOG_INFO << "[ONVIFHttpClient] Request successful";
    PLOG_INFO << "[ONVIFHttpClient] Response length: " << response.length();
    
    // Log response preview
    if (response.length() > 0) {
      std::string preview = response.substr(0, std::min(1000UL, response.length()));
      PLOG_INFO << "[ONVIFHttpClient] SOAP Response (preview):";
      PLOG_INFO << preview;
      if (response.length() > 1000) {
        PLOG_INFO << "[ONVIFHttpClient] ... (truncated, total " << response.length() << " chars)";
      }
    }
  } else {
    PLOG_WARNING << "[ONVIFHttpClient] Request failed";
  }
  
  PLOG_INFO << "[ONVIFHttpClient] ========================================";

  return success;
}

void ONVIFHttpClient::sendSoapRequestAsync(
    const std::string &url, const std::string &soapBody,
    const std::string &username, const std::string &password,
    std::function<void(bool success, const std::string &response)> callback) {
  // Create HTTP client
  auto client = drogon::HttpClient::newHttpClient(url);
  // Note: Drogon HttpClient timeout is handled by the framework

  // Create request
  auto req = createRequest(soapBody, username, password);

  // Send request asynchronously
  client->sendRequest(req, [callback](drogon::ReqResult result,
                                        const drogon::HttpResponsePtr &resp) {
    if (result == drogon::ReqResult::Ok && resp) {
      if (resp->statusCode() == drogon::k200OK ||
          resp->statusCode() == drogon::k500InternalServerError) {
        // Convert string_view to string
        std::string body(resp->body());
        callback(true, body);
      } else {
        callback(false, "");
      }
    } else {
      callback(false, "");
    }
  });
}

void ONVIFHttpClient::setTimeout(int seconds) { timeoutSeconds_ = seconds; }

drogon::HttpRequestPtr ONVIFHttpClient::createRequest(
    const std::string &soapBody, const std::string &username,
    const std::string &password, bool useHttpAuth,
    const std::string &digestAuthHeader) {
  auto req = drogon::HttpRequest::newHttpRequest();
  req->setMethod(drogon::Post);
  req->setBody(soapBody);
  req->addHeader("Content-Type", "application/soap+xml; charset=utf-8");
  req->addHeader("Content-Length", std::to_string(soapBody.length()));

  // Add authentication header
  if (!digestAuthHeader.empty()) {
    // Use Digest Authentication
    req->addHeader("Authorization", digestAuthHeader);
    PLOG_DEBUG << "[ONVIFHttpClient] Added HTTP Digest Auth header";
  } else if (useHttpAuth && !username.empty() && !password.empty()) {
    // Basic authentication (ONVIF uses Digest, but Basic can work for some)
    std::string auth = username + ":" + password;
    std::string encoded = drogon::utils::base64Encode(
        reinterpret_cast<const unsigned char *>(auth.c_str()), auth.length());
    req->addHeader("Authorization", "Basic " + encoded);
    PLOG_DEBUG << "[ONVIFHttpClient] Added HTTP Basic Auth header";
  } else if (!useHttpAuth) {
    PLOG_DEBUG << "[ONVIFHttpClient] HTTP Basic Auth disabled (using WS-Security only)";
  }

  return req;
}

std::string ONVIFHttpClient::extractServiceUrl(const std::string &baseUrl,
                                                  const std::string &serviceType) {
  // Try to construct service URL from base URL
  // Common patterns:
  // http://ip/onvif/device_service -> http://ip/onvif/media_service
  // http://ip/onvif/Device -> http://ip/onvif/Media

  std::string serviceUrl = baseUrl;

  if (serviceType == "media") {
    // Replace device_service with media_service
    const std::string deviceService = "device_service";
    const std::string mediaService = "media_service";
    size_t pos = serviceUrl.find(deviceService);
    if (pos != std::string::npos) {
      serviceUrl.replace(pos, deviceService.length(), mediaService);
    } else {
      pos = serviceUrl.find("Device");
      if (pos != std::string::npos) {
        serviceUrl.replace(pos, 6, "Media");
      } else if (serviceUrl.back() != '/') {
        serviceUrl += "/Media";
      }
    }
  } else if (serviceType == "ptz") {
    size_t pos = serviceUrl.find("device_service");
    if (pos != std::string::npos) {
      serviceUrl.replace(pos, 13, "ptz_service");
    } else {
      pos = serviceUrl.find("Device");
      if (pos != std::string::npos) {
        serviceUrl.replace(pos, 6, "PTZ");
      } else if (serviceUrl.back() != '/') {
        serviceUrl += "/PTZ";
      }
    }
  }

  return serviceUrl;
}

