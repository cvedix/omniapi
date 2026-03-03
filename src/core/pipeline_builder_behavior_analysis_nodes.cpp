#include "core/pipeline_builder_behavior_analysis_nodes.h"
#include "core/pipeline_builder_model_resolver.h"
#include "core/area_manager.h"
#include "core/securt_line_manager.h"
#include <iostream>
#include <stdexcept>
#include <climits>
#include <opencv2/core.hpp>
#include <json/reader.h>
#include <json/value.h>
#include <cvedix/nodes/ba/cvedix_ba_line_crossline_node.h>
#include <cvedix/nodes/ba/cvedix_ba_area_jam_node.h>
#include <cvedix/nodes/ba/cvedix_ba_stop_node.h>
#include <cvedix/nodes/ba/cvedix_ba_area_loitering_node.h>
#include <cvedix/nodes/ba/cvedix_ba_area_enter_exit_node.h>
#include <cvedix/nodes/ba/cvedix_ba_line_counting_node.h>
#include <cvedix/nodes/osd/cvedix_ba_line_crossline_osd_node.h>
#include <cvedix/nodes/ba/cvedix_ba_area_crowding_node.h>
#include <cvedix/nodes/osd/cvedix_ba_area_crowding_osd_node.h>
#include <cvedix/nodes/osd/cvedix_ba_area_jam_osd_node.h>
#include <cvedix/nodes/osd/cvedix_ba_stop_osd_node.h>
#include <cvedix/nodes/osd/cvedix_ba_area_enter_exit_osd_node.h>
#include <cvedix/objects/shapes/cvedix_rect.h>
#include <cvedix/objects/shapes/cvedix_point.h>


std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBehaviorAnalysisNodes::createBACrosslineNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    // Use crossline_config to support names, colors, and directions
    std::map<int, std::vector<cvedix_nodes::crossline_config>> lineConfigs;
    // Also prepare lines map for constructor (multi-line per channel support)
    std::map<int, std::vector<cvedix_objects::cvedix_line>> linesMultiChannel;
    std::map<int, cvedix_objects::cvedix_line> lines; // For backward compatibility fallback
    bool linesParsed = false;
    bool useConfigs = false; // Track if we're using new config API

    // Priority 1: Check CrossingLines from API (stored in additionalParams)
    auto crossingLinesIt = req.additionalParams.find("CrossingLines");
    if (crossingLinesIt != req.additionalParams.end() &&
        !crossingLinesIt->second.empty()) {
      try {
        // Parse JSON string to JSON array
        Json::Reader reader;
        Json::Value parsedLines;
        if (reader.parse(crossingLinesIt->second, parsedLines) &&
            parsedLines.isArray()) {
          // Group lines by channel (use channel from line config if available, otherwise use index)
          std::map<int, std::vector<cvedix_nodes::crossline_config>> configsByChannel;
          
          // Iterate through lines array
          for (Json::ArrayIndex i = 0; i < parsedLines.size(); ++i) {
            const Json::Value &lineObj = parsedLines[i];

            // Check if line has coordinates
            if (!lineObj.isMember("coordinates") ||
                !lineObj["coordinates"].isArray()) {
              std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Line at index " << i
                        << " missing or invalid 'coordinates' field, skipping"
                        << std::endl;
              continue;
            }

            const Json::Value &coordinates = lineObj["coordinates"];
            if (coordinates.size() < 2) {
              std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Line at index " << i
                        << " has less than 2 coordinates, skipping"
                        << std::endl;
              continue;
            }

            // Get first and last coordinates
            const Json::Value &startCoord = coordinates[0];
            const Json::Value &endCoord = coordinates[coordinates.size() - 1];

            if (!startCoord.isMember("x") || !startCoord.isMember("y") ||
                !endCoord.isMember("x") || !endCoord.isMember("y")) {
              std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Line at index " << i
                        << " has invalid coordinate format, skipping"
                        << std::endl;
              continue;
            }

            if (!startCoord["x"].isNumeric() || !startCoord["y"].isNumeric() ||
                !endCoord["x"].isNumeric() || !endCoord["y"].isNumeric()) {
              std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Line at index " << i
                        << " has non-numeric coordinates, skipping"
                        << std::endl;
              continue;
            }

            // Convert to cvedix_line
            int start_x = startCoord["x"].asInt();
            int start_y = startCoord["y"].asInt();
            int end_x = endCoord["x"].asInt();
            int end_y = endCoord["y"].asInt();

            cvedix_objects::cvedix_point start(start_x, start_y);
            cvedix_objects::cvedix_point end(end_x, end_y);
            cvedix_objects::cvedix_line line(start, end);

            // Parse line name
            std::string line_name = "";
            if (lineObj.isMember("name") && lineObj["name"].isString() &&
                !lineObj["name"].asString().empty()) {
              line_name = lineObj["name"].asString();
            } else {
              line_name = "Line " + std::to_string(i);
            }

            // Parse color (RGBA format: [R, G, B, A])
            cv::Scalar line_color = cv::Scalar(0, 255, 0); // Default green
            if (lineObj.isMember("color") && lineObj["color"].isArray() &&
                lineObj["color"].size() >= 3) {
              int r = lineObj["color"][0].asInt();
              int g = lineObj["color"][1].asInt();
              int b = lineObj["color"][2].asInt();
              // OpenCV uses BGR format
              line_color = cv::Scalar(b, g, r);
            }

            // Parse direction
            cvedix_objects::cvedix_ba_direct_type direction =
                cvedix_objects::cvedix_ba_direct_type::BOTH;
            if (lineObj.isMember("direction") && lineObj["direction"].isString()) {
              std::string dir = lineObj["direction"].asString();
              if (dir == "Up" || dir == "IN") {
                direction = cvedix_objects::cvedix_ba_direct_type::IN;
              } else if (dir == "Down" || dir == "OUT") {
                direction = cvedix_objects::cvedix_ba_direct_type::OUT;
              } else {
                direction = cvedix_objects::cvedix_ba_direct_type::BOTH;
              }
            }

            // Determine channel: use channel from config if available, otherwise use channel 0
            // All lines go to channel 0 by default to support multiple lines per channel
            // This allows all lines to be displayed on the same video stream (typically channel 0)
            int channel = 0; // Default: all lines on channel 0 for multi-line support
            if (lineObj.isMember("channel") && lineObj["channel"].isNumeric()) {
              channel = lineObj["channel"].asInt();
            }

            // Create crossline_config with name, color, and direction
            cvedix_nodes::crossline_config config(line, line_color, line_name, direction);
            configsByChannel[channel].push_back(config);

            // Also prepare lines for multi-channel constructor (vector of lines per channel)
            linesMultiChannel[channel].push_back(line);

            // Also keep backward compatible format (for fallback if config API fails)
            // Note: If multiple lines share same channel, only last one is kept in this map
            // But we use configsByChannel and linesMultiChannel for the actual node creation
            lines[channel] = line;
            linesParsed = true;
            useConfigs = true;
          }

          if (linesParsed && useConfigs) {
            lineConfigs = configsByChannel;
            int totalLinesParsed = 0;
            for (const auto &pair : configsByChannel) {
              totalLinesParsed += static_cast<int>(pair.second.size());
            }
            std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ Parsed " << totalLinesParsed
                      << " line(s) from CrossingLines API (supports unlimited lines per channel)" << std::endl;
            for (const auto &channelPair : lineConfigs) {
              std::cerr << "  Channel " << channelPair.first << ": " 
                        << channelPair.second.size() << " line(s)" << std::endl;
              for (size_t j = 0; j < channelPair.second.size(); ++j) {
                const auto &config = channelPair.second[j];
                std::cerr << "    Line " << j << ": name='" << config.name 
                          << "', color=(" << config.color[0] << "," << config.color[1] 
                          << "," << config.color[2] << ")" << std::endl;
              }
            }
          }
        } else {
          std::cerr
              << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Failed to parse CrossingLines "
                 "JSON or not an array, falling back to solution config"
              << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr
            << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Exception parsing CrossingLines "
               "JSON: "
            << e.what() << ", falling back to solution config" << std::endl;
      }
    }

    // Priority 2: Fallback to solution config parameters if no lines from API
    if (!linesParsed) {
      // Check if we have line parameters from solution config
      // Also check if values are not placeholders (e.g., ${CROSSLINE_START_X})
      bool hasValidParams =
          params.count("line_channel") && params.count("line_start_x") &&
          params.count("line_start_y") && params.count("line_end_x") &&
          params.count("line_end_y");

      // Check if values are actual numbers (not placeholders)
      if (hasValidParams) {
        bool allValid = true;
        for (const auto &key :
             {"line_start_x", "line_start_y", "line_end_x", "line_end_y"}) {
          if (params.at(key).find("${") != std::string::npos) {
            allValid = false;
            break;
          }
        }

        if (allValid) {
          try {
            int channel = std::stoi(params.at("line_channel"));
            int start_x = std::stoi(params.at("line_start_x"));
            int start_y = std::stoi(params.at("line_start_y"));
            int end_x = std::stoi(params.at("line_end_x"));
            int end_y = std::stoi(params.at("line_end_y"));

            cvedix_objects::cvedix_point start(start_x, start_y);
            cvedix_objects::cvedix_point end(end_x, end_y);
            lines[channel] = cvedix_objects::cvedix_line(start, end);
            std::cerr
                << "[PipelineBuilderBehaviorAnalysisNodes] Using line configuration from solution "
                   "config (channel "
                << channel << ": (" << start_x << "," << start_y << ") -> ("
                << end_x << "," << end_y << "))" << std::endl;
          } catch (const std::exception &e) {
            std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Warning: Failed to parse line "
                         "parameters from solution config: "
                      << e.what() << std::endl;
            hasValidParams = false;
          }
        } else {
          std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Line parameters contain unresolved "
                       "placeholders"
                    << std::endl;
          hasValidParams = false;
        }
      }

      if (!hasValidParams) {
        std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] No valid line configuration found. "
                     "BA crossline node will be created without lines. "
                     "Lines can be added later via API."
                  << std::endl;
      }
    }

    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Creating BA crossline node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;

    std::shared_ptr<cvedix_nodes::cvedix_ba_line_crossline_node> node;

    // Use new config API if we have configs with names/colors
    if (useConfigs && !lineConfigs.empty() && !linesMultiChannel.empty()) {
      // Create node with empty lines first, then add lines with full configs (names, colors, directions)
      // This ensures names are stored in ba_crossline_node (for events) and OSD node
      std::map<int, std::vector<cvedix_objects::cvedix_line>> emptyLines;
      node = std::make_shared<cvedix_nodes::cvedix_ba_line_crossline_node>(
          nodeName, emptyLines, true, false);
      
      std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]   Created node, adding lines with configs" << std::endl;
      
      // Add lines with full configs (name, color, direction) using add_line API
      // This ensures names are stored in ba_crossline_node's all_configs
      int totalLinesAdded = 0;
      for (const auto &channelPair : lineConfigs) {
        int channel = channelPair.first;
        std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]   Adding " 
                  << channelPair.second.size() << " line(s) to channel " << channel << std::endl;
        
        for (const auto &config : channelPair.second) {
          int lineIndex = node->add_line(channel, config);
          totalLinesAdded++;
          std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]     ✓ Added line index " << lineIndex 
                    << " to channel " << channel << " with name='" << config.name 
                    << "', color=(" << config.color[0] << "," << config.color[1] 
                    << "," << config.color[2] << "), direction=" 
                    << (config.direction == cvedix_objects::cvedix_ba_direct_type::IN ? "IN" :
                        config.direction == cvedix_objects::cvedix_ba_direct_type::OUT ? "OUT" : "BOTH")
                    << std::endl;
        }
      }
      std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]   ✓ Total " << totalLinesAdded 
                << " line(s) added with names, colors, and directions" << std::endl;
    } else {
      // Fallback to backward compatible API
      std::cerr << "  Lines configured for " << lines.size() << " channel(s)"
                << std::endl;
      node = std::make_shared<cvedix_nodes::cvedix_ba_line_crossline_node>(
          nodeName, lines);
      std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]   Using backward compatible API"
                << std::endl;
    }

    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ BA crossline node created successfully"
              << std::endl;
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]   Lines will be passed to OSD node via "
                 "pipeline metadata"
              << std::endl;
    std::cerr
        << "[PipelineBuilderBehaviorAnalysisNodes]   OSD node will draw these lines on video frames"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Exception in createBACrosslineNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node> PipelineBuilderBehaviorAnalysisNodes::createBAJamNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::map<int, std::vector<cvedix_objects::cvedix_point>> jams;
    bool jamsParsed = false;

    // Priority 1: Check Jams from API (stored in additionalParams)
    // Support both "Jams" and "JamZones" for backward compatibility
    auto jamZoneIt = req.additionalParams.find("JamZones");
    if (jamZoneIt == req.additionalParams.end() || jamZoneIt->second.empty()) {
      jamZoneIt = req.additionalParams.find("Jams");
    }
    if (jamZoneIt != req.additionalParams.end() && !jamZoneIt->second.empty()) {
      try {
        // Parse JSON string to JSON array
        Json::Reader reader;
        Json::Value parsedJams;
        if (reader.parse(jamZoneIt->second, parsedJams) &&
            parsedJams.isArray()) {
          // Iterate through jams array
          for (Json::ArrayIndex i = 0; i < parsedJams.size(); ++i) {
            const Json::Value &jamObj = parsedJams[i];
            // Check if jam has coordinates
            if (!jamObj.isMember("roi") || !jamObj["roi"].isArray()) {
              std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Jam at index " << i
                        << " missing or invalid 'coordinates' field, skipping"
                        << std::endl;
              continue;
            }

            const Json::Value &roi = jamObj["roi"];
            if (roi.size() < 3) {
              std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Jam at index " << i
                        << " has less than 3 coordinates, skipping"
                        << std::endl;
              continue;
            }

            for (const auto &pt : roi) {

              if (!pt.isMember("x") || !pt.isMember("y")) {
                std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Point at index " << i
                          << " has invalid coordinate format, skipping"
                          << std::endl;
                continue;
              }

              if (!pt["x"].isNumeric() || !pt["y"].isNumeric()) {
                std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Point at index " << i
                          << " must be number, skipping" << std::endl;
                continue;
              }
            }
            std::vector<cvedix_objects::cvedix_point> roiPoints;
            bool ok = true;
            for (const auto &coord : jamObj["roi"]) {
              if (!coord.isObject() || !coord.isMember("x") ||
                  !coord.isMember("y") || !coord["x"].isNumeric() ||
                  !coord["y"].isNumeric()) {
                ok = false;
                break;
              }
              cvedix_objects::cvedix_point p;
              p.x = static_cast<int>(coord["x"].asDouble());
              p.y = static_cast<int>(coord["y"].asDouble());
              roiPoints.push_back(p);
            }
            if (!ok || roiPoints.empty()) {
              std::cerr << "[API] parseJamsFromJson: Invalid ROI at index " << i
                        << " - skipping";
              continue;
            }
            // Use array index as channel (0, 1, 2, ...)
            int channel = static_cast<int>(i);
            jams[channel] = std::move(roiPoints);
            jamsParsed = true;
          }

          if (jamsParsed) {
            std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ Parsed " << jams.size()
                      << " jam(s) from Jams API" << std::endl;
          }
        } else {
          std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Failed to parse Jams "
                       "JSON or not an array, falling back to solution config"
                    << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Exception parsing Jams "
                     "JSON: "
                  << e.what() << ", falling back to solution config"
                  << std::endl;
      }
    }

    // Priority 2: Fallback to solution config parameters if no jams from API
    if (!jamsParsed) {
      // Check if we have jam parameters from solution config
      // Also check if values are not placeholders (e.g., ${CROSSLINE_START_X})
      bool hasValidParams =
          params.count("line_channel") && params.count("line_start_x") &&
          params.count("line_start_y") && params.count("line_end_x") &&
          params.count("line_end_y");

      // Check if values are actual numbers (not placeholders)
      if (hasValidParams) {
        bool allValid = true;
        for (const auto &key :
             {"line_start_x", "line_start_y", "line_end_x", "line_end_y"}) {
          if (params.at(key).find("${") != std::string::npos) {
            allValid = false;
            break;
          }
        }

        if (allValid) {
          try {
            int channel = std::stoi(params.at("jam_channel"));
            int start_x = std::stoi(params.at("line_start_x"));
            int start_y = std::stoi(params.at("line_start_y"));
            int end_x = std::stoi(params.at("line_end_x"));
            int end_y = std::stoi(params.at("line_end_y"));

            cvedix_objects::cvedix_point start(start_x, start_y);
            cvedix_objects::cvedix_point end(end_x, end_y);
            jams[channel] = {start, end};
            std::cerr
                << "[PipelineBuilderBehaviorAnalysisNodes] Using jam configuration from solution "
                   "config (channel "
                << channel << ": (" << start_x << "," << start_y << ") -> ("
                << end_x << "," << end_y << "))" << std::endl;
          } catch (const std::exception &e) {
            std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Warning: Failed to parse jam "
                         "parameters from solution config: "
                      << e.what() << std::endl;
            hasValidParams = false;
          }
        } else {
          std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Jam parameters contain unresolved "
                       "placeholders"
                    << std::endl;
          hasValidParams = false;
        }
      }

      if (!hasValidParams) {
        std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] No valid jam configuration found. "
                     "BA jam node will be created without jams. "
                     "Jams can be added later via API."
                  << std::endl;
      }
    }

    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Creating BA jam node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Jams configured for " << jams.size() << " channel(s)"
              << std::endl;

    auto node =
        std::make_shared<cvedix_nodes::cvedix_ba_area_jam_node>(nodeName, jams);
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ BA jam node created successfully"
              << std::endl;
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]   Jams will be passed to OSD node via "
                 "pipeline metadata"
              << std::endl;
    std::cerr
        << "[PipelineBuilderBehaviorAnalysisNodes]   OSD node will draw these jams on video frames"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Exception in createBAJamNode: " << e.what()
              << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node> PipelineBuilderBehaviorAnalysisNodes::createBAStopNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::map<int, std::vector<cvedix_objects::cvedix_point>> stops;
    bool stopsParsed = false;

    // Priority 1: Check StopZones from API (stored in additionalParams)
    // Support both "Stops" and "StopZones" for backward compatibility
    auto stopZoneIt = req.additionalParams.find("StopZones");
    if (stopZoneIt == req.additionalParams.end() || stopZoneIt->second.empty()) {
      stopZoneIt = req.additionalParams.find("Stops");
    }
    if (stopZoneIt != req.additionalParams.end() && !stopZoneIt->second.empty()) {
      try {
        // Parse JSON string to JSON array
        Json::Reader reader;
        Json::Value parsedStops;
        if (reader.parse(stopZoneIt->second, parsedStops) &&
            parsedStops.isArray()) {
          // Iterate through stops array
          for (Json::ArrayIndex i = 0; i < parsedStops.size(); ++i) {
            const Json::Value &stopObj = parsedStops[i];
            // Check if stop has roi
            if (!stopObj.isMember("roi") || !stopObj["roi"].isArray()) {
              std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Stop at index " << i
                        << " missing or invalid 'roi' field, skipping"
                        << std::endl;
              continue;
            }

            const Json::Value &roi = stopObj["roi"];
            if (roi.size() < 3) {
              std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Stop at index " << i
                        << " has less than 3 coordinates, skipping"
                        << std::endl;
              continue;
            }

            std::vector<cvedix_objects::cvedix_point> roiPoints;
            bool ok = true;
            for (const auto &coord : stopObj["roi"]) {
              if (!coord.isObject() || !coord.isMember("x") ||
                  !coord.isMember("y") || !coord["x"].isNumeric() ||
                  !coord["y"].isNumeric()) {
                ok = false;
                break;
              }
              cvedix_objects::cvedix_point p;
              p.x = static_cast<int>(coord["x"].asDouble());
              p.y = static_cast<int>(coord["y"].asDouble());
              roiPoints.push_back(p);
            }
            if (!ok || roiPoints.empty()) {
              std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Invalid ROI at index " << i
                        << " - skipping" << std::endl;
              continue;
            }
            // Use array index as channel (0, 1, 2, ...)
            // Or use channel from stopObj if provided
            int channel = static_cast<int>(i);
            if (stopObj.isMember("channel") && stopObj["channel"].isNumeric()) {
              channel = stopObj["channel"].asInt();
            }
            stops[channel] = std::move(roiPoints);
            stopsParsed = true;
          }

          if (stopsParsed) {
            std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ Parsed " << stops.size()
                      << " stop zone(s) from StopZones API" << std::endl;
          }
        } else {
          std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Failed to parse StopZones "
                       "JSON or not an array, falling back to solution config"
                    << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Exception parsing StopZones "
                     "JSON: "
                  << e.what() << ", falling back to solution config"
                  << std::endl;
      }
    }

    // Priority 2: Fallback to solution config parameters if no stops from API
    if (!stopsParsed) {
      // Check if we have stop parameters from solution config
      // For ba_stop, we typically use StopZones JSON in params
      if (params.count("StopZones") && !params.at("StopZones").empty()) {
        try {
          Json::Reader reader;
          Json::Value parsedStops;
          if (reader.parse(params.at("StopZones"), parsedStops) &&
              parsedStops.isArray()) {
            for (Json::ArrayIndex i = 0; i < parsedStops.size(); ++i) {
              const Json::Value &stopObj = parsedStops[i];
              if (!stopObj.isMember("roi") || !stopObj["roi"].isArray()) {
                continue;
              }
              std::vector<cvedix_objects::cvedix_point> roiPoints;
              for (const auto &coord : stopObj["roi"]) {
                if (coord.isObject() && coord.isMember("x") &&
                    coord.isMember("y") && coord["x"].isNumeric() &&
                    coord["y"].isNumeric()) {
                  cvedix_objects::cvedix_point p;
                  p.x = static_cast<int>(coord["x"].asDouble());
                  p.y = static_cast<int>(coord["y"].asDouble());
                  roiPoints.push_back(p);
                }
              }
              if (!roiPoints.empty()) {
                int channel = static_cast<int>(i);
                if (stopObj.isMember("channel") && stopObj["channel"].isNumeric()) {
                  channel = stopObj["channel"].asInt();
                }
                stops[channel] = std::move(roiPoints);
                stopsParsed = true;
              }
            }
          }
        } catch (const std::exception &e) {
          std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Warning: Failed to parse StopZones "
                       "from solution config: "
                    << e.what() << std::endl;
        }
      }

      if (!stopsParsed) {
        std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] No valid stop zone configuration found. "
                     "BA stop node will be created without stop zones. "
                     "Stop zones can be added later via API."
                  << std::endl;
      }
    }

    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Creating BA stop node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Stop zones configured for " << stops.size() << " channel(s)"
              << std::endl;

    auto node =
        std::make_shared<cvedix_nodes::cvedix_ba_stop_node>(nodeName, stops);
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ BA stop node created successfully"
              << std::endl;
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]   Stop zones will be passed to OSD node via "
                 "pipeline metadata"
              << std::endl;
    std::cerr
        << "[PipelineBuilderBehaviorAnalysisNodes]   OSD node will draw these stop zones on video frames"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Exception in createBAStopNode: " << e.what()
              << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBehaviorAnalysisNodes::createBACrosslineOSDNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Creating BA crossline OSD node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Note: OSD node will automatically get lines from "
                 "ba_crossline_node via pipeline metadata"
              << std::endl;

    auto node =
        std::make_shared<cvedix_nodes::cvedix_ba_line_crossline_osd_node>(nodeName);

    // Parse CrossingLines config to set line names, colors, and directions for OSD
    auto crossingLinesIt = req.additionalParams.find("CrossingLines");
    if (crossingLinesIt != req.additionalParams.end() &&
        !crossingLinesIt->second.empty()) {
      try {
        Json::Reader reader;
        Json::Value parsedLines;
        if (reader.parse(crossingLinesIt->second, parsedLines) &&
            parsedLines.isArray()) {
          // Group lines by channel
          std::map<int, std::vector<cvedix_nodes::line_display_config>> configsByChannel;
          
          for (Json::ArrayIndex i = 0; i < parsedLines.size(); ++i) {
            const Json::Value &lineObj = parsedLines[i];

            // Check if line has coordinates
            if (!lineObj.isMember("coordinates") ||
                !lineObj["coordinates"].isArray() ||
                lineObj["coordinates"].size() < 2) {
              continue;
            }

            const Json::Value &coordinates = lineObj["coordinates"];
            const Json::Value &startCoord = coordinates[0];
            const Json::Value &endCoord = coordinates[coordinates.size() - 1];

            if (!startCoord.isMember("x") || !startCoord.isMember("y") ||
                !endCoord.isMember("x") || !endCoord.isMember("y") ||
                !startCoord["x"].isNumeric() || !startCoord["y"].isNumeric() ||
                !endCoord["x"].isNumeric() || !endCoord["y"].isNumeric()) {
              continue;
            }

            // Convert to cvedix_line
            int start_x = startCoord["x"].asInt();
            int start_y = startCoord["y"].asInt();
            int end_x = endCoord["x"].asInt();
            int end_y = endCoord["y"].asInt();

            cvedix_objects::cvedix_point start(start_x, start_y);
            cvedix_objects::cvedix_point end(end_x, end_y);
            cvedix_objects::cvedix_line line(start, end);

            // Parse line name
            std::string line_name = "";
            if (lineObj.isMember("name") && lineObj["name"].isString() &&
                !lineObj["name"].asString().empty()) {
              line_name = lineObj["name"].asString();
            } else {
              line_name = "Line " + std::to_string(i);
            }

            // Parse color (RGBA format: [R, G, B, A])
            cv::Scalar line_color = cv::Scalar(0, 255, 0); // Default green
            if (lineObj.isMember("color") && lineObj["color"].isArray() &&
                lineObj["color"].size() >= 3) {
              int r = lineObj["color"][0].asInt();
              int g = lineObj["color"][1].asInt();
              int b = lineObj["color"][2].asInt();
              // OpenCV uses BGR format
              line_color = cv::Scalar(b, g, r);
            }

            // Parse direction
            cvedix_objects::cvedix_ba_direct_type direction =
                cvedix_objects::cvedix_ba_direct_type::BOTH;
            if (lineObj.isMember("direction") && lineObj["direction"].isString()) {
              std::string dir = lineObj["direction"].asString();
              if (dir == "Up" || dir == "IN") {
                direction = cvedix_objects::cvedix_ba_direct_type::IN;
              } else if (dir == "Down" || dir == "OUT") {
                direction = cvedix_objects::cvedix_ba_direct_type::OUT;
              } else {
                direction = cvedix_objects::cvedix_ba_direct_type::BOTH;
              }
            }

            // Determine channel: use channel from config if available, otherwise use channel 0
            // All lines go to channel 0 by default to support multiple lines per channel
            // This allows all lines to be displayed on the same video stream (typically channel 0)
            int channel = 0; // Default: all lines on channel 0 for multi-line support
            if (lineObj.isMember("channel") && lineObj["channel"].isNumeric()) {
              channel = lineObj["channel"].asInt();
            }

            // Create line_display_config with name, color, and direction
            cvedix_nodes::line_display_config displayConfig(line, line_color, line_name, direction);
            configsByChannel[channel].push_back(displayConfig);
          }

          // Set line configs for each channel
          int totalConfigsSet = 0;
          for (const auto &channelPair : configsByChannel) {
            int channel = channelPair.first;
            node->set_line_configs(channel, channelPair.second);
            totalConfigsSet += channelPair.second.size();
            std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]   ✓ Set " 
                      << channelPair.second.size() 
                      << " line config(s) for channel " << channel << " with names:" << std::endl;
            for (size_t j = 0; j < channelPair.second.size(); ++j) {
              const auto &config = channelPair.second[j];
              std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]     Line " << j << ": name='" 
                        << config.name << "', color=(" << config.color[0] << "," << config.color[1] 
                        << "," << config.color[2] << ")" << std::endl;
            }
          }
          std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]   ✓ Total " << totalConfigsSet 
                    << " line config(s) set for OSD node" << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Failed to parse "
                     "CrossingLines for OSD node: " << e.what() << std::endl;
      }
    }

    std::cerr
        << "[PipelineBuilderBehaviorAnalysisNodes] ✓ BA crossline OSD node created successfully"
        << std::endl;
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]   OSD node will draw lines on video frames "
                 "from ba_crossline_node"
              << std::endl;
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]   NOTE: OSD node displays line labels "
                 "based on channel/index. Custom line names from CrossingLines are used in "
                 "MQTT events but may not appear in video overlay if SDK doesn't support it."
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Exception in createBACrosslineOSDNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node> PipelineBuilderBehaviorAnalysisNodes::createBAJamOSDNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params) {

  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Creating BA jam OSD node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Note: OSD node will automatically get lines from "
                 "ba_jam_node via pipeline metadata"
              << std::endl;

    auto node =
        std::make_shared<cvedix_nodes::cvedix_ba_area_jam_osd_node>(nodeName);
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ BA jam OSD node created successfully"
              << std::endl;
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]   OSD node will draw lines on video frames "
                 "from ba_jam_node"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Exception in createBAJamOSDNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node> PipelineBuilderBehaviorAnalysisNodes::createBAStopOSDNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params) {

  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Creating BA stop OSD node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Note: OSD node will automatically get stop zones from "
                 "ba_stop_node via pipeline metadata"
              << std::endl;

    auto node =
        std::make_shared<cvedix_nodes::cvedix_ba_stop_osd_node>(nodeName);
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ BA stop OSD node created successfully"
              << std::endl;
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]   OSD node will draw stop zones on video frames "
                 "from ba_stop_node"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Exception in createBAStopOSDNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node> PipelineBuilderBehaviorAnalysisNodes::createBALoiteringNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::map<int, cvedix_objects::cvedix_rect> regions;
    std::map<int, double> alarm_seconds;
    bool regionsParsed = false;

    // Priority 1: Check LoiteringAreas from params (substituted from ${LOITERING_AREAS_JSON})
    std::string loiteringAreasJson;
    if (params.count("LoiteringAreas") && !params.at("LoiteringAreas").empty()) {
      loiteringAreasJson = params.at("LoiteringAreas");
      std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Found LoiteringAreas in params (substituted from placeholder)"
                << std::endl;
    }
    // Priority 2: Check LoiteringAreas from additionalParams (direct API call)
    else {
      auto loiteringAreasIt = req.additionalParams.find("LoiteringAreas");
      if (loiteringAreasIt != req.additionalParams.end() && !loiteringAreasIt->second.empty()) {
        loiteringAreasJson = loiteringAreasIt->second;
        std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Found LoiteringAreas in additionalParams (direct API call)"
                  << std::endl;
      }
    }

    // Parse LoiteringAreas JSON if found
    if (!loiteringAreasJson.empty()) {
      try {
        // Parse JSON string to JSON array
        Json::Reader reader;
        Json::Value parsedAreas;
        if (reader.parse(loiteringAreasJson, parsedAreas) && parsedAreas.isArray()) {
          // Iterate through loitering areas array
          for (Json::ArrayIndex i = 0; i < parsedAreas.size(); ++i) {
            const Json::Value &areaObj = parsedAreas[i];
            
            // Check if area has coordinates
            if (!areaObj.isMember("coordinates") || !areaObj["coordinates"].isArray() ||
                areaObj["coordinates"].size() < 3) {
              std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Loitering area at index " << i
                        << " missing or invalid 'coordinates' field, skipping"
                        << std::endl;
              continue;
            }

            // Get alarm seconds (default to 5 if not specified)
            double seconds = 5.0;
            if (areaObj.isMember("seconds") && areaObj["seconds"].isNumeric()) {
              seconds = areaObj["seconds"].asDouble();
            }

            // Convert polygon coordinates to bounding rectangle
            const Json::Value &coords = areaObj["coordinates"];
            int min_x = INT_MAX, min_y = INT_MAX;
            int max_x = INT_MIN, max_y = INT_MIN;

            for (const auto &coord : coords) {
              if (!coord.isObject() || !coord.isMember("x") || !coord.isMember("y") ||
                  !coord["x"].isNumeric() || !coord["y"].isNumeric()) {
                continue;
              }
              
              double x = coord["x"].asDouble();
              double y = coord["y"].asDouble();
              
              // Handle normalized coordinates (0.0-1.0)
              if (x <= 1.0 && y <= 1.0 && x >= 0.0 && y >= 0.0) {
                // Normalized - convert to pixels (assume 1920x1080 default)
                x = x * 1920;
                y = y * 1080;
              }
              
              int px = static_cast<int>(x);
              int py = static_cast<int>(y);
              
              min_x = std::min(min_x, px);
              min_y = std::min(min_y, py);
              max_x = std::max(max_x, px);
              max_y = std::max(max_y, py);
            }

            if (min_x != INT_MAX && min_y != INT_MAX && max_x != INT_MIN && max_y != INT_MIN) {
              // Create rectangle from bounding box
              int width = max_x - min_x;
              int height = max_y - min_y;
              
              // Determine channel (default to 0)
              int channel = 0;
              if (areaObj.isMember("channel") && areaObj["channel"].isNumeric()) {
                channel = areaObj["channel"].asInt();
              } else {
                // Use index as channel if not specified
                channel = static_cast<int>(i);
              }
              
              cvedix_objects::cvedix_rect rect(min_x, min_y, width, height);
              regions[channel] = rect;
              alarm_seconds[channel] = seconds;
              regionsParsed = true;
            }
          }

          if (regionsParsed) {
            std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ Parsed " << regions.size()
                      << " loitering area(s) from LoiteringAreas API" << std::endl;
          }
        } else {
          std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Failed to parse LoiteringAreas "
                       "JSON or not an array, falling back to solution config"
                    << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Exception parsing LoiteringAreas "
                     "JSON: "
                  << e.what() << ", falling back to solution config"
                  << std::endl;
      }
    }

    // Priority 3: Fallback to solution config parameters if no areas from API/params
    if (!regionsParsed) {
      // Check if we have loitering parameters from solution config
      bool hasValidParams = params.count("loitering_channel") && 
                           params.count("loitering_x") &&
                           params.count("loitering_y") &&
                           params.count("loitering_width") &&
                           params.count("loitering_height");
      
      // Also check for alarm_seconds parameter (can be from ${ALARM_SECONDS} or direct)
      std::string alarmSecondsStr;
      if (params.count("alarm_seconds") && !params.at("alarm_seconds").empty()) {
        alarmSecondsStr = params.at("alarm_seconds");
      } else {
        auto alarmSecondsIt = req.additionalParams.find("ALARM_SECONDS");
        if (alarmSecondsIt != req.additionalParams.end() && !alarmSecondsIt->second.empty()) {
          alarmSecondsStr = alarmSecondsIt->second;
        }
      }

      if (hasValidParams) {
        try {
          int channel = std::stoi(params.at("loitering_channel"));
          int x = std::stoi(params.at("loitering_x"));
          int y = std::stoi(params.at("loitering_y"));
          int width = std::stoi(params.at("loitering_width"));
          int height = std::stoi(params.at("loitering_height"));
          double seconds = 5.0;
          
          // Try to get alarm_seconds from multiple sources
          if (!alarmSecondsStr.empty()) {
            try {
              seconds = std::stod(alarmSecondsStr);
            } catch (const std::exception &e) {
              std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Warning: Failed to parse alarm_seconds: "
                        << e.what() << ", using default 5.0" << std::endl;
            }
          } else if (params.count("loitering_seconds")) {
            seconds = std::stod(params.at("loitering_seconds"));
          }

          cvedix_objects::cvedix_rect rect(x, y, width, height);
          regions[channel] = rect;
          alarm_seconds[channel] = seconds;
          std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Using loitering configuration from solution "
                       "config (channel "
                    << channel << ": rect(" << x << "," << y << "," << width << "," << height 
                    << "), seconds=" << seconds << ")" << std::endl;
        } catch (const std::exception &e) {
          std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Warning: Failed to parse loitering "
                       "parameters from solution config: "
                    << e.what() << std::endl;
          hasValidParams = false;
        }
      }

      if (!hasValidParams) {
        std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] No valid loitering configuration found. "
                     "BA loitering node will be created without regions. "
                     "Regions can be added later via API."
                  << std::endl;
      }
    }

    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Creating BA loitering node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Regions configured for " << regions.size() << " channel(s)"
              << std::endl;

    // SDK expects map<int, vector<cvedix_point>> (polygon per channel). Convert rects to polygons.
    std::map<int, std::vector<cvedix_objects::cvedix_point>> rois;
    for (const auto& kv : regions) {
      const cvedix_objects::cvedix_rect& r = kv.second;
      rois[kv.first] = {
        cvedix_objects::cvedix_point(r.x, r.y),
        cvedix_objects::cvedix_point(r.x + r.width, r.y),
        cvedix_objects::cvedix_point(r.x + r.width, r.y + r.height),
        cvedix_objects::cvedix_point(r.x, r.y + r.height)
      };
    }
    // Create loitering node: (nodeName, rois, fps, draw_roi, draw_alarm_region)
    auto node = std::make_shared<cvedix_nodes::cvedix_ba_area_loitering_node>(
        nodeName, rois, 30, false, false);
    
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ BA loitering node created successfully"
              << std::endl;
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]   Regions will be passed to OSD node via "
                 "pipeline metadata"
              << std::endl;
    std::cerr
        << "[PipelineBuilderBehaviorAnalysisNodes]   OSD node will draw these regions on video frames"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Exception in createBALoiteringNode: " << e.what()
              << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node> PipelineBuilderBehaviorAnalysisNodes::createBALoiteringOSDNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params) {

  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Creating BA loitering OSD node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Note: OSD node will automatically get regions from "
                 "ba_loitering_node via pipeline metadata"
              << std::endl;

    // Use ba_stop_osd_node for loitering visualization (they share the same OSD node)
    auto node =
        std::make_shared<cvedix_nodes::cvedix_ba_stop_osd_node>(nodeName);
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ BA loitering OSD node created successfully"
              << std::endl;
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes]   OSD node will draw regions on video frames "
                 "from ba_loitering_node"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Exception in createBALoiteringOSDNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node> PipelineBuilderBehaviorAnalysisNodes::createBAAreaEnterExitNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {
  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::map<int, std::vector<cvedix_objects::cvedix_rect>> areas;
    std::map<int, std::vector<cvedix_nodes::area_alert_config>> configs;

    // Parse Areas from additionalParams
    auto areasIt = req.additionalParams.find("Areas");
    bool areasParsed = false;
    if (areasIt != req.additionalParams.end() && !areasIt->second.empty()) {
      try {
        Json::Reader reader;
        Json::Value areasJson;
        if (reader.parse(areasIt->second, areasJson) && areasJson.isArray()) {
          for (const auto &areaObj : areasJson) {
            if (!areaObj.isMember("channel") || !areaObj.isMember("roi") ||
                !areaObj["roi"].isArray() || areaObj["roi"].size() < 3) {
              continue;
            }
            int channel = areaObj["channel"].asInt();
            const Json::Value &roi = areaObj["roi"];
            
            // Convert polygon to bounding rect (simplified - use first 3 points)
            if (roi.size() >= 3) {
              int min_x = INT_MAX, min_y = INT_MAX;
              int max_x = INT_MIN, max_y = INT_MIN;
              for (const auto &point : roi) {
                if (point.isMember("x") && point.isMember("y")) {
                  int x = point["x"].asInt();
                  int y = point["y"].asInt();
                  min_x = std::min(min_x, x);
                  min_y = std::min(min_y, y);
                  max_x = std::max(max_x, x);
                  max_y = std::max(max_y, y);
                }
              }
              if (min_x < max_x && min_y < max_y) {
                cvedix_objects::cvedix_rect rect(min_x, min_y, max_x - min_x, max_y - min_y);
                areas[channel].push_back(rect);
              }
            }
          }
          areasParsed = !areas.empty();
        }
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Failed to parse Areas JSON: "
                  << e.what() << std::endl;
      }
    }

    // Parse AreaConfigs if available
    auto configsIt = req.additionalParams.find("AreaConfigs");
    if (configsIt != req.additionalParams.end() && !configsIt->second.empty()) {
      try {
        Json::Reader reader;
        Json::Value configsJson;
        if (reader.parse(configsIt->second, configsJson) && configsJson.isArray()) {
          for (const auto &configObj : configsJson) {
            if (!configObj.isMember("channel") || !configObj.isMember("index")) {
              continue;
            }
            int channel = configObj["channel"].asInt();
            int index = configObj["index"].asInt();
            
            bool alert_enter = configObj.get("alert_enter", true).asBool();
            bool alert_exit = configObj.get("alert_exit", true).asBool();
            std::string name = configObj.get("name", "").asString();
            
            cv::Scalar color(0, 220, 0); // Default green
            if (configObj.isMember("color") && configObj["color"].isArray() &&
                configObj["color"].size() >= 3) {
              int r = configObj["color"][0].asInt();
              int g = configObj["color"][1].asInt();
              int b = configObj["color"][2].asInt();
              color = cv::Scalar(b, g, r); // BGR format
            }
            
            if (configs[channel].size() <= static_cast<size_t>(index)) {
              configs[channel].resize(index + 1);
            }
            configs[channel][index] = cvedix_nodes::area_alert_config(
                alert_enter, alert_exit, name, color);
          }
        }
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Failed to parse AreaConfigs JSON: "
                  << e.what() << std::endl;
      }
    }

    if (!areasParsed) {
      std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] No valid area configuration found. "
                   "BA area enter/exit node will be created without areas. "
                   "Areas can be added later via API."
                << std::endl;
    }

    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Creating BA area enter/exit node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Areas configured for " << areas.size() << " channel(s)"
              << std::endl;

    // SDK expects map<int, vector<vector<cvedix_point>>> (polygons per channel). Convert rects to polygons.
    std::map<int, std::vector<std::vector<cvedix_objects::cvedix_point>>> areas_poly;
    for (const auto& kv : areas) {
      for (const auto& r : kv.second) {
        areas_poly[kv.first].push_back({
          cvedix_objects::cvedix_point(r.x, r.y),
          cvedix_objects::cvedix_point(r.x + r.width, r.y),
          cvedix_objects::cvedix_point(r.x + r.width, r.y + r.height),
          cvedix_objects::cvedix_point(r.x, r.y + r.height)
        });
      }
    }
    // Create node with areas (polygons) and configs (image_recording=false, video_recording=false)
    auto node = std::make_shared<cvedix_nodes::cvedix_ba_area_enter_exit_node>(
        nodeName, areas_poly, configs, false, false);
    
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ BA area enter/exit node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Exception in createBAAreaEnterExitNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node> PipelineBuilderBehaviorAnalysisNodes::createBALineCountingNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {
  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::map<int, std::vector<cvedix_nodes::cvedix_ba_line_couting_setting>> lineSettings;

    // Parse LineSettings from additionalParams
    auto lineSettingsIt = req.additionalParams.find("LineSettings");
    bool lineSettingsParsed = false;
    if (lineSettingsIt != req.additionalParams.end() && !lineSettingsIt->second.empty()) {
      try {
        Json::Reader reader;
        Json::Value lineSettingsJson;
        if (reader.parse(lineSettingsIt->second, lineSettingsJson) && lineSettingsJson.isObject()) {
          for (const auto &channelStr : lineSettingsJson.getMemberNames()) {
            int channel = std::stoi(channelStr);
            const Json::Value &linesArray = lineSettingsJson[channelStr];
            
            if (linesArray.isArray()) {
              for (const auto &lineObj : linesArray) {
                cvedix_nodes::cvedix_ba_line_couting_setting setting;
                
                // Parse setting name
                if (lineObj.isMember("setting_name") && lineObj["setting_name"].isString()) {
                  setting.setting_name = lineObj["setting_name"].asString();
                }
                
                // Parse line coordinates
                if (lineObj.isMember("line") && lineObj["line"].isObject()) {
                  const Json::Value &line = lineObj["line"];
                  if (line.isMember("start") && line.isMember("end")) {
                    int start_x = line["start"]["x"].asInt();
                    int start_y = line["start"]["y"].asInt();
                    int end_x = line["end"]["x"].asInt();
                    int end_y = line["end"]["y"].asInt();
                    setting.line = cvedix_objects::cvedix_line(
                        cvedix_objects::cvedix_point(start_x, start_y),
                        cvedix_objects::cvedix_point(end_x, end_y));
                  }
                }
                
                // Parse direction
                if (lineObj.isMember("direction") && lineObj["direction"].isString()) {
                  std::string dir = lineObj["direction"].asString();
                  if (dir == "IN" || dir == "Up") {
                    setting.direction = cvedix_objects::cvedix_ba_direct_type::IN;
                  } else if (dir == "OUT" || dir == "Down") {
                    setting.direction = cvedix_objects::cvedix_ba_direct_type::OUT;
                  } else {
                    setting.direction = cvedix_objects::cvedix_ba_direct_type::BOTH;
                  }
                } else {
                  setting.direction = cvedix_objects::cvedix_ba_direct_type::BOTH;
                }
                
                lineSettings[channel].push_back(setting);
              }
            }
          }
          lineSettingsParsed = !lineSettings.empty();
        }
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: Failed to parse LineSettings JSON: "
                  << e.what() << std::endl;
      }
    }

    if (!lineSettingsParsed) {
      std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] No valid line settings found. "
                   "BA line counting node will be created without lines. "
                   "Lines can be added later via API."
                << std::endl;
    }

    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Creating BA line counting node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Line settings configured for " << lineSettings.size() << " channel(s)"
              << std::endl;

    // Create node with line settings
    auto node = std::make_shared<cvedix_nodes::cvedix_ba_line_counting_node>(
        nodeName, lineSettings);
    
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ BA line counting node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Exception in createBALineCountingNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node> PipelineBuilderBehaviorAnalysisNodes::createBAAreaEnterExitOSDNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params) {
  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Creating BA area enter/exit OSD node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Note: OSD node will automatically get areas from "
                 "ba_area_enter_exit_node via pipeline metadata"
              << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_ba_area_enter_exit_osd_node>(nodeName);
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ BA area enter/exit OSD node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Exception in createBAAreaEnterExitOSDNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBehaviorAnalysisNodes::createBACrowdingNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    std::map<int, std::vector<cvedix_objects::cvedix_point>> rois;
    std::map<int, cvedix_nodes::crowding_config> configs;
    bool parsed = false;

    auto it = req.additionalParams.find("CrowdingZones");
    if (it != req.additionalParams.end() && !it->second.empty()) {
      try {
        Json::Reader reader;
        Json::Value arr;
        if (reader.parse(it->second, arr) && arr.isArray()) {
          for (Json::ArrayIndex i = 0; i < arr.size(); ++i) {
            const Json::Value &obj = arr[i];
            if (!obj.isMember("coordinates") || !obj["coordinates"].isArray() ||
                obj["coordinates"].size() < 3) {
              continue;
            }
            std::vector<cvedix_objects::cvedix_point> pts;
            for (const auto &c : obj["coordinates"]) {
              if (c.isMember("x") && c.isMember("y") && c["x"].isNumeric() && c["y"].isNumeric()) {
                pts.push_back(cvedix_objects::cvedix_point(
                    static_cast<int>(c["x"].asDouble()),
                    static_cast<int>(c["y"].asDouble())));
              }
            }
            if (pts.empty()) continue;
            int ch = obj.isMember("channel") && obj["channel"].isNumeric()
                         ? obj["channel"].asInt()
                         : static_cast<int>(i);
            rois[ch] = std::move(pts);
            int threshold = obj.isMember("threshold") && obj["threshold"].isNumeric()
                                ? obj["threshold"].asInt()
                                : 3;
            double alarmSec = obj.isMember("alarm_seconds") && obj["alarm_seconds"].isNumeric()
                                 ? obj["alarm_seconds"].asDouble()
                                 : 2.0;
            std::string name = obj.isMember("name") && obj["name"].isString()
                                   ? obj["name"].asString()
                                   : "Zone";
            configs[ch] = cvedix_nodes::crowding_config(threshold, alarmSec, name);
            parsed = true;
          }
        }
      } catch (const std::exception &e) {
        std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] WARNING: parse CrowdingZones: "
                  << e.what() << std::endl;
      }
    }

    if (!parsed || rois.empty()) {
      std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] No valid CrowdingZones; creating node with empty ROIs"
                << std::endl;
    }

    int checkInterval = 30;
    auto ciIt = req.additionalParams.find("CROWDING_CHECK_INTERVAL");
    if (ciIt != req.additionalParams.end() && !ciIt->second.empty()) {
      try {
        checkInterval = std::stoi(ciIt->second);
      } catch (...) {}
    }

    auto node = std::make_shared<cvedix_nodes::cvedix_ba_area_crowding_node>(
        nodeName, rois, configs, checkInterval, false, false);
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ BA crowding node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Exception in createBACrowdingNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderBehaviorAnalysisNodes::createBACrowdingOSDNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params) {

  try {
    auto node = std::make_shared<cvedix_nodes::cvedix_ba_area_crowding_osd_node>(nodeName);
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] ✓ BA crowding OSD node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderBehaviorAnalysisNodes] Exception in createBACrowdingOSDNode: "
              << e.what() << std::endl;
    throw;
  }
}