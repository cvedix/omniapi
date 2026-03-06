#pragma once

#include "core/area_manager.h"
#include "core/securt_line_manager.h"
#include <json/json.h>
#include <string>

/**
 * @brief SecuRT Pipeline Integration
 *
 * Converts SecuRT areas and lines to SDK format for pipeline integration.
 */
class SecuRTPipelineIntegration {
public:
  /**
   * @brief Convert SecuRT lines to CrossingLines JSON format for ba_crossline_node
   * @param lineManager Line manager instance
   * @param instanceId Instance ID
   * @return JSON string in CrossingLines format, empty if no lines
   */
  static std::string convertLinesToCrossingLinesFormat(
      const SecuRTLineManager *lineManager, const std::string &instanceId);

  /**
   * @brief Convert SecuRT areas to JSON format for pipeline
   * @param areaManager Area manager instance
   * @param instanceId Instance ID
   * @return JSON string with areas, empty if no areas
   */
  static std::string convertAreasToJsonFormat(
      const AreaManager *areaManager, const std::string &instanceId);

  /**
   * @brief Convert SecuRT areas to StopZones format for ba_stop node
   * @param areaManager Area manager instance
   * @param instanceId Instance ID
   * @return JSON string in StopZones format, empty if no areas
   */
  static std::string convertAreasToStopZonesFormat(
      const AreaManager *areaManager, const std::string &instanceId);

  /**
   * @brief Convert SecuRT loitering areas to LoiteringAreas format for ba_loitering node
   * @param areaManager Area manager instance
   * @param instanceId Instance ID
   * @return JSON string in LoiteringAreas format, empty if no loitering areas
   */
  static std::string convertAreasToLoiteringFormat(
      const AreaManager *areaManager, const std::string &instanceId);

  /**
   * @brief Get all lines as JSON array for API response
   * @param lineManager Line manager instance
   * @param instanceId Instance ID
   * @return JSON array of all lines
   */
  static Json::Value getAllLinesAsJson(
      const SecuRTLineManager *lineManager, const std::string &instanceId);

  /**
   * @brief Get all areas as JSON object grouped by type
   * @param areaManager Area manager instance
   * @param instanceId Instance ID
   * @return JSON object with areas grouped by type
   */
  static Json::Value getAllAreasAsJson(
      const AreaManager *areaManager, const std::string &instanceId);

private:
  /**
   * @brief Convert a single line to CrossingLines format
   */
  static Json::Value convertLineToCrossingLineFormat(const Json::Value &line);

  /**
   * @brief Convert coordinates from normalized (0.0-1.0) to pixel coordinates
   * @param coords Normalized coordinates
   * @param width Frame width
   * @param height Frame height
   * @return Pixel coordinates
   */
  static std::vector<std::pair<int, int>>
  convertNormalizedToPixel(const Json::Value &coords, int width = 1920,
                           int height = 1080);
};

