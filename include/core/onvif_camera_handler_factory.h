#pragma once

#include "core/onvif_camera_handler.h"
#include "core/onvif_camera_registry.h"
#include <memory>

/**
 * @brief Factory for creating camera-specific ONVIF handlers
 * 
 * Automatically selects the appropriate handler based on camera manufacturer/model
 */
class ONVIFCameraHandlerFactory {
public:
  /**
   * @brief Get singleton instance
   */
  static ONVIFCameraHandlerFactory &getInstance();

  /**
   * @brief Get handler for a specific camera
   * @param camera Camera information
   * @return Handler instance (never null, returns GenericHandler as fallback)
   */
  std::shared_ptr<ONVIFCameraHandler> getHandler(const ONVIFCamera &camera);

  /**
   * @brief Get handler by camera ID
   * @param cameraId Camera ID (IP or UUID)
   * @return Handler instance (never null)
   */
  std::shared_ptr<ONVIFCameraHandler> getHandler(const std::string &cameraId);

  /**
   * @brief Register a custom handler
   * @param handler Handler instance
   */
  void registerHandler(std::shared_ptr<ONVIFCameraHandler> handler);

private:
  ONVIFCameraHandlerFactory();
  ~ONVIFCameraHandlerFactory() = default;
  ONVIFCameraHandlerFactory(const ONVIFCameraHandlerFactory &) = delete;
  ONVIFCameraHandlerFactory &operator=(const ONVIFCameraHandlerFactory &) = delete;

  // List of registered handlers (order matters - first match wins)
  std::vector<std::shared_ptr<ONVIFCameraHandler>> handlers_;
  
  // Default generic handler (fallback)
  std::shared_ptr<ONVIFCameraHandler> genericHandler_;
};

