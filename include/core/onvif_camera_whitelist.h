#pragma once

#include <map>
#include <set>
#include <string>

/**
 * @brief ONVIF Camera Whitelist
 *
 * Manages whitelist of supported camera manufacturers/models.
 * Support cameras gradually by adding to whitelist.
 */
class ONVIFCameraWhitelist {
public:
  /**
   * @brief Get singleton instance
   */
  static ONVIFCameraWhitelist &getInstance();

  /**
   * @brief Check if camera is whitelisted (supported)
   */
  bool isSupported(const std::string &manufacturer,
                   const std::string &model) const;

  /**
   * @brief Check if manufacturer is whitelisted
   */
  bool isManufacturerSupported(const std::string &manufacturer) const;

  /**
   * @brief Add manufacturer to whitelist
   */
  void addManufacturer(const std::string &manufacturer);

  /**
   * @brief Add specific camera model to whitelist
   */
  void addCameraModel(const std::string &manufacturer,
                      const std::string &model);

  /**
   * @brief Remove manufacturer from whitelist
   */
  void removeManufacturer(const std::string &manufacturer);

  /**
   * @brief Get all supported manufacturers
   */
  std::set<std::string> getSupportedManufacturers() const;

  /**
   * @brief Check if whitelist is empty (all cameras allowed)
   */
  bool isEmpty() const;

  /**
   * @brief Clear whitelist (allow all cameras)
   */
  void clear();

private:
  ONVIFCameraWhitelist();
  ~ONVIFCameraWhitelist() = default;
  ONVIFCameraWhitelist(const ONVIFCameraWhitelist &) = delete;
  ONVIFCameraWhitelist &operator=(const ONVIFCameraWhitelist &) = delete;

  // Set of supported manufacturers (empty = all supported)
  std::set<std::string> supportedManufacturers_;

  // Map of manufacturer -> set of models (empty set = all models supported)
  std::map<std::string, std::set<std::string>> supportedModels_;

  // If true, whitelist is disabled (all cameras allowed)
  bool allowAll_;
};

