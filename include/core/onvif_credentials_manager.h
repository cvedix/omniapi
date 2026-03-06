#pragma once

#include <map>
#include <mutex>
#include <string>

/**
 * @brief Camera credentials structure
 */
struct ONVIFCredentials {
  std::string username;
  std::string password;
};

/**
 * @brief ONVIF Credentials Manager
 *
 * Manages credentials for ONVIF cameras with thread-safe access.
 */
class ONVIFCredentialsManager {
public:
  /**
   * @brief Get singleton instance
   */
  static ONVIFCredentialsManager &getInstance();

  /**
   * @brief Set credentials for a camera
   */
  void setCredentials(const std::string &cameraId,
                      const ONVIFCredentials &credentials);

  /**
   * @brief Get credentials for a camera
   */
  bool getCredentials(const std::string &cameraId,
                      ONVIFCredentials &credentials) const;

  /**
   * @brief Remove credentials for a camera
   */
  void removeCredentials(const std::string &cameraId);

  /**
   * @brief Clear all credentials
   */
  void clear();

  /**
   * @brief Check if credentials exist for a camera
   */
  bool hasCredentials(const std::string &cameraId) const;

private:
  ONVIFCredentialsManager() = default;
  ~ONVIFCredentialsManager() = default;
  ONVIFCredentialsManager(const ONVIFCredentialsManager &) = delete;
  ONVIFCredentialsManager &operator=(const ONVIFCredentialsManager &) = delete;

  mutable std::mutex mutex_;
  std::map<std::string, ONVIFCredentials> credentials_;
};

