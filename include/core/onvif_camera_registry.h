#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

/**
 * @brief ONVIF Camera information structure
 */
struct ONVIFCamera {
  std::string ip;
  std::string uuid;
  std::string manufacturer;
  std::string model;
  std::string serialNumber;
  std::string endpoint; // ONVIF service endpoint URL
};

/**
 * @brief ONVIF Stream information structure
 */
struct ONVIFStream {
  std::string token; // Profile token
  int width;
  int height;
  int fps;
  std::string uri; // RTSP URI
};

/**
 * @brief ONVIF Camera Registry
 *
 * Manages discovered ONVIF cameras with thread-safe access.
 */
class ONVIFCameraRegistry {
public:
  /**
   * @brief Get singleton instance
   */
  static ONVIFCameraRegistry &getInstance();

  /**
   * @brief Add or update a camera
   */
  void addCamera(const std::string &cameraId, const ONVIFCamera &camera);

  /**
   * @brief Get camera by ID (UUID or IP)
   */
  bool getCamera(const std::string &cameraId, ONVIFCamera &camera) const;

  /**
   * @brief Get all cameras
   */
  std::vector<ONVIFCamera> getAllCameras() const;

  /**
   * @brief Remove a camera
   */
  void removeCamera(const std::string &cameraId);

  /**
   * @brief Clear all cameras
   */
  void clear();

  /**
   * @brief Check if camera exists
   */
  bool hasCamera(const std::string &cameraId) const;

private:
  ONVIFCameraRegistry() = default;
  ~ONVIFCameraRegistry() = default;
  ONVIFCameraRegistry(const ONVIFCameraRegistry &) = delete;
  ONVIFCameraRegistry &operator=(const ONVIFCameraRegistry &) = delete;

  mutable std::mutex mutex_;
  std::map<std::string, ONVIFCamera> cameras_; // Key: UUID or IP
};

