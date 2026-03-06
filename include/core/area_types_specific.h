#pragma once

#include "core/area_types.h"
#include <json/json.h>
#include <string>

/**
 * @brief Area event type for crossing areas
 */
enum class AreaEvent {
  Enter,
  Exit,
  Both
};

/**
 * @brief Convert AreaEvent to string
 */
inline std::string areaEventToString(AreaEvent event) {
  switch (event) {
  case AreaEvent::Enter:
    return "Enter";
  case AreaEvent::Exit:
    return "Exit";
  case AreaEvent::Both:
    return "Both";
  default:
    return "Both";
  }
}

/**
 * @brief Convert string to AreaEvent
 */
inline AreaEvent stringToAreaEvent(const std::string &str) {
  if (str == "Enter")
    return AreaEvent::Enter;
  if (str == "Exit")
    return AreaEvent::Exit;
  if (str == "Both")
    return AreaEvent::Both;
  return AreaEvent::Both; // Default
}

// ============================================================================
// Standard Areas
// ============================================================================

/**
 * @brief Crossing Area
 * Detects objects crossing into/out of area
 */
struct CrossingArea : public AreaBase {
  bool ignoreStationaryObjects = false;
  AreaEvent areaEvent = AreaEvent::Both;

  Json::Value toJson() const {
    Json::Value json = AreaBase::toJson();
    json["ignoreStationaryObjects"] = ignoreStationaryObjects;
    json["areaEvent"] = areaEventToString(areaEvent);
    return json;
  }

  static CrossingArea fromJson(const Json::Value &json) {
    CrossingArea area;
    // Parse base fields
    AreaBase base = AreaBase::fromJson(json);
    area.id = base.id;
    area.name = base.name;
    area.coordinates = base.coordinates;
    area.classes = base.classes;
    area.color = base.color;

    // Parse specific fields
    if (json.isMember("ignoreStationaryObjects") &&
        json["ignoreStationaryObjects"].isBool()) {
      area.ignoreStationaryObjects = json["ignoreStationaryObjects"].asBool();
    }
    if (json.isMember("areaEvent") && json["areaEvent"].isString()) {
      area.areaEvent = stringToAreaEvent(json["areaEvent"].asString());
    }

    return area;
  }
};

/**
 * @brief Crossing Area Write Schema
 */
struct CrossingAreaWrite : public AreaBaseWrite {
  bool ignoreStationaryObjects = false;
  AreaEvent areaEvent = AreaEvent::Both;

  static CrossingAreaWrite fromJson(const Json::Value &json) {
    CrossingAreaWrite write;
    // Parse base fields
    AreaBaseWrite base = AreaBaseWrite::fromJson(json);
    write.name = base.name;
    write.coordinates = base.coordinates;
    write.classes = base.classes;
    write.color = base.color;

    // Parse specific fields
    if (json.isMember("ignoreStationaryObjects") &&
        json["ignoreStationaryObjects"].isBool()) {
      write.ignoreStationaryObjects = json["ignoreStationaryObjects"].asBool();
    }
    if (json.isMember("areaEvent") && json["areaEvent"].isString()) {
      write.areaEvent = stringToAreaEvent(json["areaEvent"].asString());
    }

    return write;
  }
};

/**
 * @brief Intrusion Area
 * Detects intrusion into area
 */
struct IntrusionArea : public AreaBase {
  Json::Value toJson() const { return AreaBase::toJson(); }

  static IntrusionArea fromJson(const Json::Value &json) {
    IntrusionArea area;
    AreaBase base = AreaBase::fromJson(json);
    area.id = base.id;
    area.name = base.name;
    area.coordinates = base.coordinates;
    area.classes = base.classes;
    area.color = base.color;
    return area;
  }
};

/**
 * @brief Intrusion Area Write Schema
 */
struct IntrusionAreaWrite : public AreaBaseWrite {
  static IntrusionAreaWrite fromJson(const Json::Value &json) {
    IntrusionAreaWrite write;
    AreaBaseWrite base = AreaBaseWrite::fromJson(json);
    write.name = base.name;
    write.coordinates = base.coordinates;
    write.classes = base.classes;
    write.color = base.color;
    return write;
  }
};

/**
 * @brief Loitering Area
 * Detects loitering in area
 */
struct LoiteringArea : public AreaBase {
  int seconds = 5; // Duration threshold

  Json::Value toJson() const {
    Json::Value json = AreaBase::toJson();
    json["seconds"] = seconds;
    return json;
  }

  static LoiteringArea fromJson(const Json::Value &json) {
    LoiteringArea area;
    AreaBase base = AreaBase::fromJson(json);
    area.id = base.id;
    area.name = base.name;
    area.coordinates = base.coordinates;
    area.classes = base.classes;
    area.color = base.color;

    if (json.isMember("seconds") && json["seconds"].isInt()) {
      area.seconds = json["seconds"].asInt();
    }

    return area;
  }
};

/**
 * @brief Loitering Area Write Schema
 */
struct LoiteringAreaWrite : public AreaBaseWrite {
  int seconds = 5;

  static LoiteringAreaWrite fromJson(const Json::Value &json) {
    LoiteringAreaWrite write;
    AreaBaseWrite base = AreaBaseWrite::fromJson(json);
    write.name = base.name;
    write.coordinates = base.coordinates;
    write.classes = base.classes;
    write.color = base.color;

    if (json.isMember("seconds") && json["seconds"].isInt()) {
      write.seconds = json["seconds"].asInt();
    }

    return write;
  }
};

/**
 * @brief Crowding Area
 * Detects crowding (many objects)
 */
struct CrowdingArea : public AreaBase {
  int objectCount = 5; // Crowding threshold
  int seconds = 3;     // Duration before trigger

  Json::Value toJson() const {
    Json::Value json = AreaBase::toJson();
    json["objectCount"] = objectCount;
    json["seconds"] = seconds;
    return json;
  }

  static CrowdingArea fromJson(const Json::Value &json) {
    CrowdingArea area;
    AreaBase base = AreaBase::fromJson(json);
    area.id = base.id;
    area.name = base.name;
    area.coordinates = base.coordinates;
    area.classes = base.classes;
    area.color = base.color;

    if (json.isMember("objectCount") && json["objectCount"].isInt()) {
      area.objectCount = json["objectCount"].asInt();
    }
    if (json.isMember("seconds") && json["seconds"].isInt()) {
      area.seconds = json["seconds"].asInt();
    }

    return area;
  }
};

/**
 * @brief Crowding Area Write Schema
 */
struct CrowdingAreaWrite : public AreaBaseWrite {
  int objectCount = 5;
  int seconds = 3;

  static CrowdingAreaWrite fromJson(const Json::Value &json) {
    CrowdingAreaWrite write;
    AreaBaseWrite base = AreaBaseWrite::fromJson(json);
    write.name = base.name;
    write.coordinates = base.coordinates;
    write.classes = base.classes;
    write.color = base.color;

    if (json.isMember("objectCount") && json["objectCount"].isInt()) {
      write.objectCount = json["objectCount"].asInt();
    }
    if (json.isMember("seconds") && json["seconds"].isInt()) {
      write.seconds = json["seconds"].asInt();
    }

    return write;
  }
};

/**
 * @brief Occupancy Area
 * Detects occupancy
 */
struct OccupancyArea : public AreaBase {
  Json::Value toJson() const { return AreaBase::toJson(); }

  static OccupancyArea fromJson(const Json::Value &json) {
    OccupancyArea area;
    AreaBase base = AreaBase::fromJson(json);
    area.id = base.id;
    area.name = base.name;
    area.coordinates = base.coordinates;
    area.classes = base.classes;
    area.color = base.color;
    return area;
  }
};

/**
 * @brief Occupancy Area Write Schema
 */
struct OccupancyAreaWrite : public AreaBaseWrite {
  static OccupancyAreaWrite fromJson(const Json::Value &json) {
    OccupancyAreaWrite write;
    AreaBaseWrite base = AreaBaseWrite::fromJson(json);
    write.name = base.name;
    write.coordinates = base.coordinates;
    write.classes = base.classes;
    write.color = base.color;
    return write;
  }
};

/**
 * @brief Crowd Estimation Area
 * Estimates crowd count
 */
struct CrowdEstimationArea : public AreaBase {
  Json::Value toJson() const { return AreaBase::toJson(); }

  static CrowdEstimationArea fromJson(const Json::Value &json) {
    CrowdEstimationArea area;
    AreaBase base = AreaBase::fromJson(json);
    area.id = base.id;
    area.name = base.name;
    area.coordinates = base.coordinates;
    area.classes = base.classes;
    area.color = base.color;
    return area;
  }
};

/**
 * @brief Crowd Estimation Area Write Schema
 */
struct CrowdEstimationAreaWrite : public AreaBaseWrite {
  static CrowdEstimationAreaWrite fromJson(const Json::Value &json) {
    CrowdEstimationAreaWrite write;
    AreaBaseWrite base = AreaBaseWrite::fromJson(json);
    write.name = base.name;
    write.coordinates = base.coordinates;
    write.classes = base.classes;
    write.color = base.color;
    return write;
  }
};

/**
 * @brief Dwelling Area
 * Detects dwelling (staying long)
 */
struct DwellingArea : public AreaBase {
  int seconds = 10; // Duration threshold

  Json::Value toJson() const {
    Json::Value json = AreaBase::toJson();
    json["seconds"] = seconds;
    return json;
  }

  static DwellingArea fromJson(const Json::Value &json) {
    DwellingArea area;
    AreaBase base = AreaBase::fromJson(json);
    area.id = base.id;
    area.name = base.name;
    area.coordinates = base.coordinates;
    area.classes = base.classes;
    area.color = base.color;

    if (json.isMember("seconds") && json["seconds"].isInt()) {
      area.seconds = json["seconds"].asInt();
    }

    return area;
  }
};

/**
 * @brief Dwelling Area Write Schema
 */
struct DwellingAreaWrite : public AreaBaseWrite {
  int seconds = 10;

  static DwellingAreaWrite fromJson(const Json::Value &json) {
    DwellingAreaWrite write;
    AreaBaseWrite base = AreaBaseWrite::fromJson(json);
    write.name = base.name;
    write.coordinates = base.coordinates;
    write.classes = base.classes;
    write.color = base.color;

    if (json.isMember("seconds") && json["seconds"].isInt()) {
      write.seconds = json["seconds"].asInt();
    }

    return write;
  }
};

/**
 * @brief Armed Person Area
 * Detects armed person
 */
struct ArmedPersonArea : public AreaBase {
  Json::Value toJson() const { return AreaBase::toJson(); }

  static ArmedPersonArea fromJson(const Json::Value &json) {
    ArmedPersonArea area;
    AreaBase base = AreaBase::fromJson(json);
    area.id = base.id;
    area.name = base.name;
    area.coordinates = base.coordinates;
    area.classes = base.classes;
    area.color = base.color;
    return area;
  }
};

/**
 * @brief Armed Person Area Write Schema
 */
struct ArmedPersonAreaWrite : public AreaBaseWrite {
  static ArmedPersonAreaWrite fromJson(const Json::Value &json) {
    ArmedPersonAreaWrite write;
    AreaBaseWrite base = AreaBaseWrite::fromJson(json);
    write.name = base.name;
    write.coordinates = base.coordinates;
    write.classes = base.classes;
    write.color = base.color;
    return write;
  }
};

/**
 * @brief Object Left Area
 * Detects object left
 */
struct ObjectLeftArea : public AreaBase {
  int seconds = 5; // Duration threshold

  Json::Value toJson() const {
    Json::Value json = AreaBase::toJson();
    json["seconds"] = seconds;
    return json;
  }

  static ObjectLeftArea fromJson(const Json::Value &json) {
    ObjectLeftArea area;
    AreaBase base = AreaBase::fromJson(json);
    area.id = base.id;
    area.name = base.name;
    area.coordinates = base.coordinates;
    area.classes = base.classes;
    area.color = base.color;

    if (json.isMember("seconds") && json["seconds"].isInt()) {
      area.seconds = json["seconds"].asInt();
    }

    return area;
  }
};

/**
 * @brief Object Left Area Write Schema
 */
struct ObjectLeftAreaWrite : public AreaBaseWrite {
  int seconds = 5;

  static ObjectLeftAreaWrite fromJson(const Json::Value &json) {
    ObjectLeftAreaWrite write;
    AreaBaseWrite base = AreaBaseWrite::fromJson(json);
    write.name = base.name;
    write.coordinates = base.coordinates;
    write.classes = base.classes;
    write.color = base.color;

    if (json.isMember("seconds") && json["seconds"].isInt()) {
      write.seconds = json["seconds"].asInt();
    }

    return write;
  }
};

/**
 * @brief Object Removed Area
 * Detects object removed
 */
struct ObjectRemovedArea : public AreaBase {
  int seconds = 5; // Duration threshold

  Json::Value toJson() const {
    Json::Value json = AreaBase::toJson();
    json["seconds"] = seconds;
    return json;
  }

  static ObjectRemovedArea fromJson(const Json::Value &json) {
    ObjectRemovedArea area;
    AreaBase base = AreaBase::fromJson(json);
    area.id = base.id;
    area.name = base.name;
    area.coordinates = base.coordinates;
    area.classes = base.classes;
    area.color = base.color;

    if (json.isMember("seconds") && json["seconds"].isInt()) {
      area.seconds = json["seconds"].asInt();
    }

    return area;
  }
};

/**
 * @brief Object Removed Area Write Schema
 */
struct ObjectRemovedAreaWrite : public AreaBaseWrite {
  int seconds = 5;

  static ObjectRemovedAreaWrite fromJson(const Json::Value &json) {
    ObjectRemovedAreaWrite write;
    AreaBaseWrite base = AreaBaseWrite::fromJson(json);
    write.name = base.name;
    write.coordinates = base.coordinates;
    write.classes = base.classes;
    write.color = base.color;

    if (json.isMember("seconds") && json["seconds"].isInt()) {
      write.seconds = json["seconds"].asInt();
    }

    return write;
  }
};

/**
 * @brief Fallen Person Area
 * Detects fallen person
 */
struct FallenPersonArea : public AreaBase {
  Json::Value toJson() const { return AreaBase::toJson(); }

  static FallenPersonArea fromJson(const Json::Value &json) {
    FallenPersonArea area;
    AreaBase base = AreaBase::fromJson(json);
    area.id = base.id;
    area.name = base.name;
    area.coordinates = base.coordinates;
    area.classes = base.classes;
    area.color = base.color;
    return area;
  }
};

/**
 * @brief Fallen Person Area Write Schema
 */
struct FallenPersonAreaWrite : public AreaBaseWrite {
  static FallenPersonAreaWrite fromJson(const Json::Value &json) {
    FallenPersonAreaWrite write;
    AreaBaseWrite base = AreaBaseWrite::fromJson(json);
    write.name = base.name;
    write.coordinates = base.coordinates;
    write.classes = base.classes;
    write.color = base.color;
    return write;
  }
};

// ============================================================================
// Experimental Areas
// ============================================================================

/**
 * @brief Vehicle Guard Area (Experimental)
 * Vehicle guard detection
 */
struct VehicleGuardArea : public AreaBase {
  Json::Value toJson() const { return AreaBase::toJson(); }

  static VehicleGuardArea fromJson(const Json::Value &json) {
    VehicleGuardArea area;
    AreaBase base = AreaBase::fromJson(json);
    area.id = base.id;
    area.name = base.name;
    area.coordinates = base.coordinates;
    area.classes = base.classes;
    area.color = base.color;
    return area;
  }
};

/**
 * @brief Vehicle Guard Area Write Schema
 */
struct VehicleGuardAreaWrite : public AreaBaseWrite {
  static VehicleGuardAreaWrite fromJson(const Json::Value &json) {
    VehicleGuardAreaWrite write;
    AreaBaseWrite base = AreaBaseWrite::fromJson(json);
    write.name = base.name;
    write.coordinates = base.coordinates;
    write.classes = base.classes;
    write.color = base.color;
    return write;
  }
};

/**
 * @brief Face Covered Area (Experimental)
 * Face covered detection
 */
struct FaceCoveredArea : public AreaBase {
  Json::Value toJson() const { return AreaBase::toJson(); }

  static FaceCoveredArea fromJson(const Json::Value &json) {
    FaceCoveredArea area;
    AreaBase base = AreaBase::fromJson(json);
    area.id = base.id;
    area.name = base.name;
    area.coordinates = base.coordinates;
    area.classes = base.classes;
    area.color = base.color;
    return area;
  }
};

/**
 * @brief Face Covered Area Write Schema
 */
struct FaceCoveredAreaWrite : public AreaBaseWrite {
  static FaceCoveredAreaWrite fromJson(const Json::Value &json) {
    FaceCoveredAreaWrite write;
    AreaBaseWrite base = AreaBaseWrite::fromJson(json);
    write.name = base.name;
    write.coordinates = base.coordinates;
    write.classes = base.classes;
    write.color = base.color;
    return write;
  }
};

/**
 * @brief Object Enter/Exit Area
 * Detects objects entering/exiting area (for BA Area Enter/Exit solution)
 */
struct ObjectEnterExitArea : public AreaBase {
  bool alertOnEnter = true;
  bool alertOnExit = true;

  Json::Value toJson() const {
    Json::Value json = AreaBase::toJson();
    json["alertOnEnter"] = alertOnEnter;
    json["alertOnExit"] = alertOnExit;
    return json;
  }

  static ObjectEnterExitArea fromJson(const Json::Value &json) {
    ObjectEnterExitArea area;
    AreaBase base = AreaBase::fromJson(json);
    area.id = base.id;
    area.name = base.name;
    area.coordinates = base.coordinates;
    area.classes = base.classes;
    area.color = base.color;

    if (json.isMember("alertOnEnter") && json["alertOnEnter"].isBool()) {
      area.alertOnEnter = json["alertOnEnter"].asBool();
    }
    if (json.isMember("alertOnExit") && json["alertOnExit"].isBool()) {
      area.alertOnExit = json["alertOnExit"].asBool();
    }

    return area;
  }
};

/**
 * @brief Object Enter/Exit Area Write Schema
 */
struct ObjectEnterExitAreaWrite : public AreaBaseWrite {
  bool alertOnEnter = true;
  bool alertOnExit = true;

  static ObjectEnterExitAreaWrite fromJson(const Json::Value &json) {
    ObjectEnterExitAreaWrite write;
    AreaBaseWrite base = AreaBaseWrite::fromJson(json);
    write.name = base.name;
    write.coordinates = base.coordinates;
    write.classes = base.classes;
    write.color = base.color;

    if (json.isMember("alertOnEnter") && json["alertOnEnter"].isBool()) {
      write.alertOnEnter = json["alertOnEnter"].asBool();
    }
    if (json.isMember("alertOnExit") && json["alertOnExit"].isBool()) {
      write.alertOnExit = json["alertOnExit"].asBool();
    }

    return write;
  }
};

