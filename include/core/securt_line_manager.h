#pragma once

#include "core/securt_line_storage.h"
#include "core/securt_line_types.h"
#include "core/uuid_generator.h"
#include <json/json.h>
#include <string>

/**
 * @brief SecuRT Line Manager
 *
 * Manages line CRUD operations and validation.
 */
class SecuRTLineManager {
public:
  /**
   * @brief Constructor
   */
  SecuRTLineManager();

  /**
   * @brief Create counting line
   * @param instanceId Instance ID
   * @param json LineWrite JSON
   * @param lineId Optional line ID (if empty, will be generated)
   * @return Line ID if successful, empty string otherwise
   */
  std::string createCountingLine(const std::string &instanceId,
                                 const Json::Value &json,
                                 const std::string &lineId = "");

  /**
   * @brief Create crossing line
   * @param instanceId Instance ID
   * @param json LineWrite JSON
   * @param lineId Optional line ID (if empty, will be generated)
   * @return Line ID if successful, empty string otherwise
   */
  std::string createCrossingLine(const std::string &instanceId,
                                  const Json::Value &json,
                                  const std::string &lineId = "");

  /**
   * @brief Create tailgating line
   * @param instanceId Instance ID
   * @param json LineWrite JSON
   * @param lineId Optional line ID (if empty, will be generated)
   * @return Line ID if successful, empty string otherwise
   */
  std::string createTailgatingLine(const std::string &instanceId,
                                    const Json::Value &json,
                                    const std::string &lineId = "");

  /**
   * @brief Update line
   * @param instanceId Instance ID
   * @param lineId Line ID
   * @param json LineWrite JSON
   * @param type Line type
   * @return true if successful
   */
  bool updateLine(const std::string &instanceId, const std::string &lineId,
                  const Json::Value &json, LineType type);

  /**
   * @brief Delete line
   * @param instanceId Instance ID
   * @param lineId Line ID
   * @return true if successful
   */
  bool deleteLine(const std::string &instanceId, const std::string &lineId);

  /**
   * @brief Delete all lines for instance
   * @param instanceId Instance ID
   */
  void deleteAllLines(const std::string &instanceId);

  /**
   * @brief Get all lines for instance
   * @param instanceId Instance ID
   * @return JSON with all lines grouped by type
   */
  Json::Value getAllLines(const std::string &instanceId) const;

  /**
   * @brief Get counting lines for instance
   * @param instanceId Instance ID
   * @return JSON array of counting lines
   */
  Json::Value getCountingLines(const std::string &instanceId) const;

  /**
   * @brief Get crossing lines for instance
   * @param instanceId Instance ID
   * @return JSON array of crossing lines
   */
  Json::Value getCrossingLines(const std::string &instanceId) const;

  /**
   * @brief Get tailgating lines for instance
   * @param instanceId Instance ID
   * @return JSON array of tailgating lines
   */
  Json::Value getTailgatingLines(const std::string &instanceId) const;

  /**
   * @brief Check if line exists
   * @param instanceId Instance ID
   * @param lineId Line ID
   * @return true if exists
   */
  bool hasLine(const std::string &instanceId, const std::string &lineId) const;

  /**
   * @brief Validate line data
   * @param json LineWrite JSON
   * @param type Line type
   * @param error Error message output
   * @return true if valid
   */
  bool validateLine(const Json::Value &json, LineType type,
                    std::string &error) const;

private:
  SecuRTLineStorage storage_;

  /**
   * @brief Validate coordinates (exactly 2 points)
   */
  bool validateCoordinates(const Json::Value &coordinates,
                           std::string &error) const;

  /**
   * @brief Validate classes array
   */
  bool validateClasses(const Json::Value &classes, std::string &error) const;

  /**
   * @brief Validate direction
   */
  bool validateDirection(const Json::Value &direction,
                         std::string &error) const;

  /**
   * @brief Validate color
   */
  bool validateColor(const Json::Value &color, std::string &error) const;

  /**
   * @brief Validate tailgating-specific fields
   */
  bool validateTailgatingFields(const Json::Value &json,
                                 std::string &error) const;
};

