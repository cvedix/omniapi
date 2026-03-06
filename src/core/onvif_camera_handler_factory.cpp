#include "core/onvif_camera_handler_factory.h"
#include "core/onvif_camera_handlers/onvif_generic_handler.h"
#include "core/onvif_camera_handlers/onvif_tapo_handler.h"
#include "core/onvif_camera_registry.h"
#include "core/logger.h"
#include <algorithm>

ONVIFCameraHandlerFactory &ONVIFCameraHandlerFactory::getInstance() {
  static ONVIFCameraHandlerFactory instance;
  return instance;
}

ONVIFCameraHandlerFactory::ONVIFCameraHandlerFactory() {
  PLOG_INFO << "[ONVIFCameraHandlerFactory] ========================================";
  PLOG_INFO << "[ONVIFCameraHandlerFactory] Initializing camera handler factory";
  
  // Create default generic handler
  genericHandler_ = std::make_shared<ONVIFGenericHandler>();
  PLOG_INFO << "[ONVIFCameraHandlerFactory] Created generic handler (fallback)";
  
  // Register specific handlers (order matters - more specific first)
  handlers_.push_back(std::make_shared<ONVIFTapoHandler>());
  PLOG_INFO << "[ONVIFCameraHandlerFactory] Registered handler: Tapo";
  
  // Generic handler is used as fallback, don't add to handlers_ list
  PLOG_INFO << "[ONVIFCameraHandlerFactory] Total registered handlers: " << handlers_.size();
  PLOG_INFO << "[ONVIFCameraHandlerFactory] Factory initialization complete";
  PLOG_INFO << "[ONVIFCameraHandlerFactory] ========================================";
}

std::shared_ptr<ONVIFCameraHandler> ONVIFCameraHandlerFactory::getHandler(const ONVIFCamera &camera) {
  PLOG_DEBUG << "[ONVIFCameraHandlerFactory] Selecting handler for camera: " << camera.ip;
  PLOG_DEBUG << "[ONVIFCameraHandlerFactory] Camera info - Manufacturer: " 
             << (camera.manufacturer.empty() ? "unknown" : camera.manufacturer)
             << ", Model: " << (camera.model.empty() ? "unknown" : camera.model)
             << ", Endpoint: " << camera.endpoint;
  
  // Try each handler to see if it supports this camera
  for (size_t i = 0; i < handlers_.size(); ++i) {
    auto &handler = handlers_[i];
    PLOG_DEBUG << "[ONVIFCameraHandlerFactory] Checking handler[" << i << "]: " << handler->getName();
    if (handler->supports(camera)) {
      PLOG_INFO << "[ONVIFCameraHandlerFactory] ✓ Selected handler: " << handler->getName()
                << " for camera: " << camera.ip;
      return handler;
    } else {
      PLOG_DEBUG << "[ONVIFCameraHandlerFactory]   Handler[" << i << "] does not support this camera";
    }
  }
  
  // Fallback to generic handler
  PLOG_INFO << "[ONVIFCameraHandlerFactory] No specific handler found, using generic handler for camera: " << camera.ip;
  return genericHandler_;
}

std::shared_ptr<ONVIFCameraHandler> ONVIFCameraHandlerFactory::getHandler(const std::string &cameraId) {
  PLOG_DEBUG << "[ONVIFCameraHandlerFactory] Getting handler for camera ID: " << cameraId;
  
  auto &registry = ONVIFCameraRegistry::getInstance();
  ONVIFCamera camera;
  
  if (!registry.getCamera(cameraId, camera)) {
    PLOG_WARNING << "[ONVIFCameraHandlerFactory] Camera not found in registry: " << cameraId;
    PLOG_WARNING << "[ONVIFCameraHandlerFactory] Using generic handler as fallback";
    return genericHandler_;
  }
  
  PLOG_DEBUG << "[ONVIFCameraHandlerFactory] Camera found in registry, selecting handler";
  return getHandler(camera);
}

void ONVIFCameraHandlerFactory::registerHandler(std::shared_ptr<ONVIFCameraHandler> handler) {
  if (handler) {
    handlers_.push_back(handler);
    PLOG_INFO << "[ONVIFCameraHandlerFactory] ✓ Registered new handler: " << handler->getName();
    PLOG_INFO << "[ONVIFCameraHandlerFactory] Total handlers: " << handlers_.size();
  } else {
    PLOG_WARNING << "[ONVIFCameraHandlerFactory] Attempted to register null handler";
  }
}

