#pragma once

#include "models/create_instance_request.h"
#include <string>

/**
 * @brief Pipeline Builder Request Utilities
 *
 * Utility class for extracting values from CreateInstanceRequest.
 * Extracted from PipelineBuilder for better code organization.
 */
class PipelineBuilderRequestUtils {
public:
  /**
   * @brief Get file path from request (for file source)
   */
  static std::string getFilePath(const CreateInstanceRequest &req);

  /**
   * @brief Get RTMP URL from request
   */
  static std::string getRTMPUrl(const CreateInstanceRequest &req);

  /**
   * @brief Get RTSP URL from request
   */
  static std::string getRTSPUrl(const CreateInstanceRequest &req);
};

