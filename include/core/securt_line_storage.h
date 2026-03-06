#pragma once

#include "core/securt_line_types.h"
#include <json/json.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/**
 * @brief SecuRT Line Storage
 *
 * Thread-safe storage for lines per instance, grouped by line type.
 */
class SecuRTLineStorage {
public:
  /**
   * @brief Line pointer type (polymorphic)
   */
  struct LinePtr {
    LineType type;
    std::unique_ptr<CountingLine> countingLine;
    std::unique_ptr<CrossingLine> crossingLine;
    std::unique_ptr<TailgatingLine> tailgatingLine;

    LinePtr(CountingLine *line) : type(LineType::Counting), countingLine(line) {}
    LinePtr(CrossingLine *line) : type(LineType::Crossing), crossingLine(line) {}
    LinePtr(TailgatingLine *line) : type(LineType::Tailgating), tailgatingLine(line) {}

    /**
     * @brief Get line ID
     */
    std::string getId() const {
      switch (type) {
        case LineType::Counting:
          return countingLine ? countingLine->id : "";
        case LineType::Crossing:
          return crossingLine ? crossingLine->id : "";
        case LineType::Tailgating:
          return tailgatingLine ? tailgatingLine->id : "";
        default:
          return "";
      }
    }

    /**
     * @brief Convert to JSON
     */
    Json::Value toJson() const {
      switch (type) {
        case LineType::Counting:
          return countingLine ? countingLine->toJson() : Json::Value();
        case LineType::Crossing:
          return crossingLine ? crossingLine->toJson() : Json::Value();
        case LineType::Tailgating:
          return tailgatingLine ? tailgatingLine->toJson() : Json::Value();
        default:
          return Json::Value();
      }
    }
  };

  /**
   * @brief Add counting line
   */
  void addCountingLine(const std::string &instanceId,
                       std::unique_ptr<CountingLine> line);

  /**
   * @brief Add crossing line
   */
  void addCrossingLine(const std::string &instanceId,
                       std::unique_ptr<CrossingLine> line);

  /**
   * @brief Add tailgating line
   */
  void addTailgatingLine(const std::string &instanceId,
                         std::unique_ptr<TailgatingLine> line);

  /**
   * @brief Get line by ID
   */
  LinePtr *getLine(const std::string &instanceId, const std::string &lineId);

  /**
   * @brief Delete line by ID
   */
  bool deleteLine(const std::string &instanceId, const std::string &lineId);

  /**
   * @brief Delete all lines for instance
   */
  void deleteAllLines(const std::string &instanceId);

  /**
   * @brief Get all lines for instance, grouped by type
   */
  Json::Value getAllLines(const std::string &instanceId) const;

  /**
   * @brief Get counting lines for instance
   */
  std::vector<LinePtr *> getCountingLines(const std::string &instanceId) const;

  /**
   * @brief Get crossing lines for instance
   */
  std::vector<LinePtr *> getCrossingLines(const std::string &instanceId) const;

  /**
   * @brief Get tailgating lines for instance
   */
  std::vector<LinePtr *> getTailgatingLines(const std::string &instanceId) const;

  /**
   * @brief Check if line exists
   */
  bool hasLine(const std::string &instanceId, const std::string &lineId) const;

  /**
   * @brief Update line
   */
  bool updateLine(const std::string &instanceId, const std::string &lineId,
                  const Json::Value &json, LineType type);

private:
  // Storage: map<instanceId, map<lineId, LinePtr>>
  mutable std::mutex mutex_;
  std::map<std::string, std::map<std::string, std::unique_ptr<LinePtr>>> storage_;

  /**
   * @brief Find line by ID (internal, requires lock)
   */
  LinePtr *findLineUnlocked(const std::string &instanceId,
                            const std::string &lineId) const;
};

