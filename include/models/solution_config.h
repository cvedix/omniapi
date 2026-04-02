#pragma once

#include <map>
#include <string>
#include <vector>

/**
 * @brief Solution configuration structure
 * Defines how to build a pipeline for a specific solution type
 */
struct SolutionConfig {
  std::string solutionId;
  std::string solutionName;
  std::string solutionType; // "face_detection", "object_detection", etc.
  std::string category;     // "security", "its", "armed", "firefighting", "custom"
  std::string feature;      // "crossline", "intrusion", "loitering", "jam", etc.
  bool isDefault = false;   // If true, solution cannot be deleted

  /**
   * @brief Node configuration in pipeline
   */
  struct NodeConfig {
    std::string nodeType; // "rtsp_src", "yunet_face_detector", "file_des", etc.
    std::string nodeName; // Template with {instanceId} placeholder
    std::map<std::string, std::string> parameters; // Node parameters
  };

  std::vector<NodeConfig> pipeline;

  // Default configurations
  std::map<std::string, std::string> defaults;

  /**
   * @brief Get node name with instanceId substituted
   */
  std::string getNodeName(const std::string &templateName,
                          const std::string &instanceId) const;

  /**
   * @brief Get parameter value with variable substitution
   */
  std::string
  getParameter(const std::string &key,
               const std::map<std::string, std::string> &requestParams,
               const std::string &instanceId) const;
};
