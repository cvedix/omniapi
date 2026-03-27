#include "instances/instance_storage.h"
#include "core/env_config.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <json/json.h>
#include <sstream>
#include <vector>

InstanceStorage::InstanceStorage(const std::string &storage_dir)
    : storage_dir_(storage_dir) {
  ensureStorageDir();
}

void InstanceStorage::ensureStorageDir() {
  // Extract subdir name from storage_dir_ for fallback
  std::filesystem::path path(storage_dir_);
  std::string subdir = path.filename().string();
  if (subdir.empty()) {
    subdir = "instances"; // Default fallback subdir
  }

  // Use resolveDirectory with 3-tier fallback strategy
  std::string resolved_dir = EnvConfig::resolveDirectory(storage_dir_, subdir);

  // Update storage_dir_ if fallback was used
  if (resolved_dir != storage_dir_) {
    std::cerr << "[InstanceStorage] ⚠ Storage directory changed from "
              << storage_dir_ << " to " << resolved_dir << " (fallback)"
              << std::endl;
    storage_dir_ = resolved_dir;
  }
}

std::string InstanceStorage::getInstancesFilePath() const {
  return storage_dir_ + "/instances.json";
}

Json::Value InstanceStorage::loadInstancesFile() const {
  Json::Value root(Json::objectValue);

  // First, try to load from the configured storage_dir_ (highest priority for
  // tests)
  std::string primaryFilepath = storage_dir_ + "/instances.json";
  if (std::filesystem::exists(primaryFilepath)) {
    try {
      std::ifstream file(primaryFilepath);
      if (file.is_open()) {
        Json::CharReaderBuilder builder;
        std::string errors;
        if (Json::parseFromStream(builder, file, &root, &errors)) {
          std::cerr << "[InstanceStorage] Loaded data from primary: "
                    << storage_dir_ << std::endl;
          return root; // Return immediately if found in primary storage
        }
      }
    } catch (const std::exception &e) {
      // Continue to fallback tiers
    }
  }

  // Fallback: Extract subdir for checking all tiers
  std::filesystem::path path(storage_dir_);
  std::string subdir = path.filename().string();
  if (subdir.empty()) {
    subdir = "instances";
  }

  // Get all possible directories in priority order
  std::vector<std::string> allDirs =
      EnvConfig::getAllPossibleDirectories(subdir);

  // Try to load from all tiers, merge data (later tiers override earlier ones)
  for (const auto &dir : allDirs) {
    // Skip if this is the same as storage_dir_ (already checked above)
    if (dir == storage_dir_) {
      continue;
    }

    std::string filepath = dir + "/instances.json";
    if (!std::filesystem::exists(filepath)) {
      continue; // Skip if file doesn't exist
    }

    try {
      std::ifstream file(filepath);
      if (!file.is_open()) {
        continue;
      }

      Json::CharReaderBuilder builder;
      std::string errors;
      Json::Value tierData(Json::objectValue);
      if (Json::parseFromStream(builder, file, &tierData, &errors)) {
        // Merge data: later tiers override earlier ones
        for (const auto &key : tierData.getMemberNames()) {
          root[key] = tierData[key];
        }
        std::cerr << "[InstanceStorage] Loaded data from tier: " << dir
                  << std::endl;
      }
    } catch (const std::exception &e) {
      // Continue to next tier
      continue;
    }
  }

  return root;
}

bool InstanceStorage::saveInstancesFile(const Json::Value &instances) {
  try {
    // Ensure directory exists - try to create if it doesn't exist
    ensureStorageDir();

    // Double-check: if directory still doesn't exist, try one more time
    if (!std::filesystem::exists(storage_dir_)) {
      std::cerr << "[InstanceStorage] Directory still doesn't exist, retrying "
                   "creation: "
                << storage_dir_ << std::endl;
      try {
        std::filesystem::create_directories(storage_dir_);
        if (std::filesystem::exists(storage_dir_)) {
          std::cerr
              << "[InstanceStorage] Successfully created directory on retry: "
              << storage_dir_ << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "[InstanceStorage] Failed to create directory on retry: "
                  << e.what() << std::endl;
      }
    }

    std::string filepath = getInstancesFilePath();
    std::cerr << "[InstanceStorage] Saving instances to: " << filepath
              << std::endl;

    // Create parent directory of file if it doesn't exist (should be same as
    // storage_dir_, but be safe)
    std::filesystem::path file_path_obj(filepath);
    std::filesystem::path parent_dir = file_path_obj.parent_path();
    if (!parent_dir.empty() && !std::filesystem::exists(parent_dir)) {
      try {
        // Use resolveDirectory for parent directory (with fallback if needed)
        std::string parentDirStr = parent_dir.string();
        std::string subdir = parent_dir.filename().string();
        if (subdir.empty()) {
          subdir = "instances"; // Default fallback subdir
        }
        parentDirStr = EnvConfig::resolveDirectory(parentDirStr, subdir);
        parent_dir = std::filesystem::path(parentDirStr);
        std::filesystem::create_directories(parent_dir);
        std::cerr << "[InstanceStorage] Created parent directory for file: "
                  << parent_dir << std::endl;
      } catch (const std::exception &e) {
        std::cerr
            << "[InstanceStorage] Warning: Could not create parent directory: "
            << e.what() << std::endl;
      }
    }

    // Try to open file for writing with explicit flags
    // Use truncate mode to overwrite existing file
    std::ofstream file(filepath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
      std::cerr << "[InstanceStorage] Error: Failed to open file for writing: "
                << filepath << " (" << std::strerror(errno) << ")" << std::endl;

      // Check if parent directory exists and is writable
      if (std::filesystem::exists(parent_dir)) {
        std::cerr << "[InstanceStorage] Parent directory exists: " << parent_dir
                  << std::endl;
        // Check permissions
        auto perms = std::filesystem::status(parent_dir).permissions();
        std::cerr << "[InstanceStorage] Parent directory permissions: "
                  << std::oct << static_cast<int>(perms) << std::dec
                  << std::endl;
        
        // Check if file exists and its permissions
        if (std::filesystem::exists(filepath)) {
          auto file_perms = std::filesystem::status(filepath).permissions();
          std::cerr << "[InstanceStorage] File exists with permissions: "
                    << std::oct << static_cast<int>(file_perms) << std::dec
                    << std::endl;
        }
      } else {
        std::cerr << "[InstanceStorage] Parent directory does not exist: "
                  << parent_dir << std::endl;
      }

      // Fallback: /opt/... may exist but not writable. Try user dir then ./instances
      std::vector<std::string> tiers =
          EnvConfig::getAllPossibleDirectories("instances");
      for (const auto &dir : tiers) {
        if (dir == storage_dir_) {
          continue; // already failed above
        }
        try {
          std::filesystem::create_directories(dir);
        } catch (...) {
          continue;
        }
        std::string fallbackPath = dir + "/instances.json";
        std::ofstream fallbackFile(fallbackPath);
        if (fallbackFile.is_open()) {
          Json::StreamWriterBuilder builder;
          builder["indentation"] = "    ";
          std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
          writer->write(instances, &fallbackFile);
          fallbackFile.close();
          if (std::filesystem::exists(fallbackPath)) {
            std::cerr << "[InstanceStorage] ✓ Saved instances to fallback path (no write access to primary): "
                      << fallbackPath << std::endl;
            std::cerr << "[InstanceStorage]   Fix permissions on " << storage_dir_
                      << " or set writable storage; subsequent saves use fallback until restart."
                      << std::endl;
            storage_dir_ = dir; // so getInstancesFilePath() and next save use same dir
            return true;
          }
        }
      }

      return false;
    }

    // Write JSON to file
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "    "; // 4 spaces for indentation
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(instances, &file);
    file.close();

    // Verify file was written
    if (std::filesystem::exists(filepath)) {
      auto file_size = std::filesystem::file_size(filepath);
      std::cerr << "[InstanceStorage] Successfully saved instances file (size: "
                << file_size << " bytes)" << std::endl;
    } else {
      std::cerr << "[InstanceStorage] Warning: File was written but doesn't "
                   "exist after close"
                << std::endl;
    }

    return true;
  } catch (const std::filesystem::filesystem_error &e) {
    std::cerr << "[InstanceStorage] Filesystem exception in saveInstancesFile: "
              << e.what() << std::endl;
    std::cerr << "[InstanceStorage] Error code: " << e.code().value() << " ("
              << e.code().message() << ")" << std::endl;
    return false;
  } catch (const std::exception &e) {
    std::cerr << "[InstanceStorage] Exception in saveInstancesFile: "
              << e.what() << std::endl;
    return false;
  } catch (...) {
    std::cerr << "[InstanceStorage] Unknown exception in saveInstancesFile"
              << std::endl;
    return false;
  }
}

bool InstanceStorage::validateInstanceInfo(const InstanceInfo &info,
                                           std::string &error) const {
  if (info.instanceId.empty()) {
    error = "InstanceId cannot be empty";
    return false;
  }

  // Validate instanceId format (should be UUID-like)
  if (info.instanceId.length() < 10) {
    error = "InstanceId format appears invalid (too short)";
    return false;
  }

  // Validate displayName if provided
  if (!info.displayName.empty() && info.displayName.length() > 255) {
    error = "DisplayName too long (max 255 characters)";
    return false;
  }

  // Validate frameRateLimit
  if (info.frameRateLimit < 0 || info.frameRateLimit > 1000) {
    error = "frameRateLimit must be between 0 and 1000";
    return false;
  }

  // Validate inputOrientation
  if (info.inputOrientation < 0 || info.inputOrientation > 3) {
    error = "inputOrientation must be between 0 and 3";
    return false;
  }

  // Validate inputPixelLimit
  if (info.inputPixelLimit < 0) {
    error = "inputPixelLimit cannot be negative";
    return false;
  }

  return true;
}

bool InstanceStorage::validateConfigJson(const Json::Value &config,
                                         std::string &error) const {
  if (!config.isObject()) {
    error = "Config must be a JSON object";
    return false;
  }

  // InstanceId is required
  if (!config.isMember("InstanceId") || !config["InstanceId"].isString()) {
    error = "Config must contain 'InstanceId' as a string";
    return false;
  }

  std::string instanceId = config["InstanceId"].asString();
  if (instanceId.empty()) {
    error = "InstanceId cannot be empty";
    return false;
  }

  // Validate nested structures if present
  if (config.isMember("Input") && !config["Input"].isObject()) {
    error = "Input must be a JSON object";
    return false;
  }

  if (config.isMember("SolutionManager") &&
      !config["SolutionManager"].isObject()) {
    error = "SolutionManager must be a JSON object";
    return false;
  }

  if (config.isMember("Detector") && !config["Detector"].isObject()) {
    error = "Detector must be a JSON object";
    return false;
  }

  if (config.isMember("Logging") && !config["Logging"].isObject()) {
    error = "Logging must be a JSON object";
    return false;
  }

  return true;
}

bool InstanceStorage::mergeConfigs(
    Json::Value &existingConfig, const Json::Value &newConfig,
    const std::vector<std::string> &preserveKeys) const {
  if (!existingConfig.isObject() || !newConfig.isObject()) {
    return false;
  }

  // List of keys that should be completely replaced (not merged)
  std::vector<std::string> replaceKeys = {
      "InstanceId",  "DisplayName", "Solution",       "SolutionName",
      "Group",       "ReadOnly",    "SystemInstance", "AutoStart",
      "AutoRestart", "logging",     "loaded",         "running",
      "fps",         "version"};

  // List of keys that should be merged (nested objects)
  std::vector<std::string> mergeKeys = {
      "Input",           "SolutionManager",  "Detector",
      "DetectorRegions", "DetectorThermal",  "Movement",
      "OriginatorInfo",  "AdditionalParams", "Output",
      "PerformanceMode", "Tripwire",         "Zone"};

  // Replace simple fields
  for (const auto &key : replaceKeys) {
    if (newConfig.isMember(key)) {
      existingConfig[key] = newConfig[key];
    }
  }

  // Merge nested objects
  for (const auto &key : mergeKeys) {
    if (newConfig.isMember(key) && newConfig[key].isObject()) {
      if (!existingConfig.isMember(key)) {
        existingConfig[key] = Json::Value(Json::objectValue);
      }

      // Special handling for AdditionalParams: flatten nested input/output
      // structure
      if (key == "AdditionalParams") {
        Json::Value flattenedParams(Json::objectValue);

        // Check if newParams has nested input/output structure
        const Json::Value &newParams = newConfig[key];
        bool hasNestedStructure =
            newParams.isMember("input") || newParams.isMember("output");

        if (hasNestedStructure) {
          // If nested structure, start with existing params (to preserve keys
          // not in the update)
          flattenedParams = existingConfig[key];
          if (!flattenedParams.isObject()) {
            flattenedParams = Json::Value(Json::objectValue);
          }

          // Flatten input section - only replace keys that appear in the
          // request
          if (newParams.isMember("input") && newParams["input"].isObject()) {
            for (const auto &inputKey : newParams["input"].getMemberNames()) {
              if (newParams["input"][inputKey].isString()) {
                flattenedParams[inputKey] = newParams["input"][inputKey];
              }
            }
          }

          // Flatten output section - only replace keys that appear in the
          // request
          if (newParams.isMember("output") && newParams["output"].isObject()) {
            for (const auto &outputKey : newParams["output"].getMemberNames()) {
              if (newParams["output"][outputKey].isString()) {
                flattenedParams[outputKey] = newParams["output"][outputKey];
              }
            }
          }

          // Also merge flat keys (backward compatibility) - only replace keys
          // in request
          for (const auto &flatKey : newParams.getMemberNames()) {
            if (flatKey != "input" && flatKey != "output" &&
                newParams[flatKey].isString()) {
              flattenedParams[flatKey] = newParams[flatKey];
            }
          }
        } else {
          // If flat structure, merge with existing params (preserve keys not in
          // update) Start with existing params to preserve keys like
          // WEIGHTS_PATH, CONFIG_PATH, etc.
          flattenedParams = existingConfig[key];
          if (!flattenedParams.isObject()) {
            flattenedParams = Json::Value(Json::objectValue);
          }

          // Only update keys that appear in the new params (merge, don't
          // replace)
          std::cerr << "[InstanceStorage] Merging flat AdditionalParams: "
                       "preserving existing keys, updating "
                    << newParams.size() << " keys from update" << std::endl;
          for (const auto &flatKey : newParams.getMemberNames()) {
            if (newParams[flatKey].isString()) {
              std::cerr << "[InstanceStorage] Updating key: " << flatKey
                        << " = " << newParams[flatKey].asString() << std::endl;
              flattenedParams[flatKey] = newParams[flatKey];
            }
          }
        }

        // Update AdditionalParams with merged/flattened version
        std::cerr << "[InstanceStorage] Final AdditionalParams has "
                  << flattenedParams.size() << " keys" << std::endl;
        existingConfig[key] = flattenedParams;
      } else {
        // For other nested objects, do deep merge as before
        for (const auto &nestedKey : newConfig[key].getMemberNames()) {
          existingConfig[key][nestedKey] = newConfig[key][nestedKey];
        }
      }
    }
  }

  // Preserve special keys (TensorRT, Zone, Tripwire, etc.) from existing config
  for (const auto &preserveKey : preserveKeys) {
    if (existingConfig.isMember(preserveKey) &&
        !newConfig.isMember(preserveKey)) {
      // Keep existing value
      continue;
    }
  }

  // Also preserve any UUID-like keys (TensorRT model IDs, Zone IDs, etc.)
  // These are typically UUIDs that map to complex config objects
  for (const auto &key : existingConfig.getMemberNames()) {
    // Check if key looks like a UUID (contains dashes and is long enough)
    if (key.length() >= 36 && key.find('-') != std::string::npos) {
      // This is likely a TensorRT model ID or similar - preserve it
      if (!newConfig.isMember(key)) {
        // Keep existing UUID-keyed configs
        continue;
      }
    }

    // Preserve special non-instance keys
    std::vector<std::string> specialKeys = {"AnimalTracker",
                                            "AutoRestart",
                                            "AutoStart",
                                            "Detector",
                                            "DetectorRegions",
                                            "DetectorThermal",
                                            "Global",
                                            "LicensePlateTracker",
                                            "ObjectAttributeExtraction",
                                            "ObjectMovementClassifier",
                                            "PersonTracker",
                                            "Tripwire",
                                            "VehicleTracker",
                                            "Zone"};

    for (const auto &specialKey : specialKeys) {
      if (key == specialKey && existingConfig.isMember(key) &&
          !newConfig.isMember(key)) {
        // Preserve special key from existing config
        continue;
      }
    }
  }

  return true;
}

Json::Value
InstanceStorage::instanceInfoToConfigJson(const InstanceInfo &info,
                                          std::string *error) const {
  Json::Value config(Json::objectValue);

  // Validate input
  std::string validationError;
  if (!validateInstanceInfo(info, validationError)) {
    if (error) {
      *error = "Validation failed: " + validationError;
    }
    return Json::Value(Json::objectValue); // Return empty object on error
  }

  // Store InstanceId
  config["InstanceId"] = info.instanceId;

  // Store DisplayName
  if (!info.displayName.empty()) {
    config["DisplayName"] = info.displayName;
  }

  // Store Solution
  if (!info.solutionId.empty()) {
    config["Solution"] = info.solutionId;
  }

  // Store SolutionName (if available)
  if (!info.solutionName.empty()) {
    config["SolutionName"] = info.solutionName;
  }

  // Store Group (if available)
  if (!info.group.empty()) {
    config["Group"] = info.group;
  }

  // Store ReadOnly
  config["ReadOnly"] = info.readOnly;

  // Store SystemInstance
  config["SystemInstance"] = info.systemInstance;

  // Store AutoStart
  config["AutoStart"] = info.autoStart;

  // Store AutoRestart
  config["AutoRestart"] = info.autoRestart;

  // Store per-instance logging (API path: logging.enabled)
  config["logging"] = Json::objectValue;
  config["logging"]["enabled"] = info.instanceLoggingEnabled;

  // Store Input configuration (always include)
  Json::Value input(Json::objectValue);

  // Input media_format
  Json::Value mediaFormat(Json::objectValue);
  mediaFormat["color_format"] = 0;
  mediaFormat["default_format"] = true;
  mediaFormat["height"] = 0;
  mediaFormat["is_software"] = false;
  mediaFormat["name"] = "Same as Source";
  input["media_format"] = mediaFormat;

  // Input media_type and uri
  if (!info.rtspUrl.empty()) {
    input["media_type"] = "IP Camera";

    // Check if user wants to use urisourcebin format (for
    // compatibility/auto-detect decoder)
    bool useUrisourcebin = false;
    auto urisourcebinIt = info.additionalParams.find("USE_URISOURCEBIN");
    if (urisourcebinIt != info.additionalParams.end()) {
      useUrisourcebin =
          (urisourcebinIt->second == "true" || urisourcebinIt->second == "1");
    }

    // Check if decoder is set to "decodebin" (auto-detect)
    std::string decoderName = "avdec_h264"; // Default decoder
    auto decoderIt = info.additionalParams.find("GST_DECODER_NAME");
    if (decoderIt != info.additionalParams.end() &&
        !decoderIt->second.empty()) {
      decoderName = decoderIt->second;
      // If decodebin is specified, use urisourcebin format
      if (decoderName == "decodebin") {
        useUrisourcebin = true;
      }
    }

    if (useUrisourcebin) {
      // Format: gstreamer:///urisourcebin uri=... ! decodebin ! videoconvert !
      // video/x-raw, format=NV12 ! appsink drop=true name=cvdsink This format
      // uses decodebin for auto-detection (may help with decoder compatibility
      // issues)
      input["uri"] = "gstreamer:///urisourcebin uri=" + info.rtspUrl +
                     " ! decodebin ! videoconvert ! video/x-raw, format=NV12 ! "
                     "appsink drop=true name=cvdsink";
    } else {
      // Format: rtspsrc location=... [protocols=...] !
      // application/x-rtp,media=video ! rtph264depay ! h264parse ! decoder !
      // videoconvert ! video/x-raw,format=NV12 ! appsink drop=true name=cvdsink
      // This format matches SDK template structure (with h264parse before
      // decoder) Get transport protocol from additionalParams if specified
      std::string protocolsParam = "";
      auto rtspTransportIt = info.additionalParams.find("RTSP_TRANSPORT");
      if (rtspTransportIt != info.additionalParams.end() &&
          !rtspTransportIt->second.empty()) {
        std::string transport = rtspTransportIt->second;
        std::transform(transport.begin(), transport.end(), transport.begin(),
                       ::tolower);
        if (transport == "tcp" || transport == "udp") {
          protocolsParam = " protocols=" + transport;
        }
      }
      // If no transport specified, don't add protocols parameter - let
      // GStreamer use default
      input["uri"] =
          "rtspsrc location=" + info.rtspUrl + protocolsParam +
          " ! application/x-rtp,media=video ! rtph264depay ! h264parse ! " +
          decoderName +
          " ! videoconvert ! video/x-raw,format=NV12 ! appsink drop=true "
          "name=cvdsink";
    }
  } else if (!info.filePath.empty()) {
    input["media_type"] = "File";
    input["uri"] = info.filePath;
  } else {
    input["media_type"] = "IP Camera";
    input["uri"] = "";
  }

  config["Input"] = input;

  // Store Output configuration (always include)
  Json::Value output(Json::objectValue);
  output["JSONExport"]["enabled"] = info.metadataMode;
  output["NXWitness"]["enabled"] = false;

  // Output handlers (RTSP output if available)
  Json::Value handlers(Json::objectValue);
  if (!info.rtspUrl.empty() || !info.rtmpUrl.empty()) {
    Json::Value rtspHandler(Json::objectValue);
    Json::Value handlerConfig(Json::objectValue);
    handlerConfig["debug"] = info.debugMode ? "4" : "0";
    // Use configuredFps (from API /api/v1/instances/{id}/fps) for output FPS
    // This ensures output stream matches processing FPS
    handlerConfig["fps"] = info.configuredFps > 0 ? info.configuredFps : 5;
    handlerConfig["pipeline"] =
        "( appsrc name=cvedia-rt ! videoconvert ! videoscale ! x264enc ! "
        "video/x-h264,profile=high ! rtph264pay name=pay0 pt=96 )";
    rtspHandler["config"] = handlerConfig;
    rtspHandler["enabled"] = info.running;
    rtspHandler["sink"] = "output-image";

    std::string outputUrl = info.rtspUrl;
    if (outputUrl.empty() && !info.rtmpUrl.empty()) {
      outputUrl = "rtsp://0.0.0.0:8554/stream1";
    }
    if (outputUrl.empty()) {
      outputUrl = "rtsp://0.0.0.0:8554/stream1";
    }
    rtspHandler["uri"] = outputUrl;
    handlers["rtsp:--0.0.0.0:8554-stream1"] = rtspHandler;
  }
  output["handlers"] = handlers;
  output["render_preset"] = "Default";
  config["Output"] = output;

  // Store OriginatorInfo
  if (!info.originator.address.empty()) {
    Json::Value originator(Json::objectValue);
    originator["address"] = info.originator.address;
    config["OriginatorInfo"] = originator;
  }

  // Store SolutionManager settings
  Json::Value solutionManager(Json::objectValue);
  // Use configuredFps (from API /api/v1/instances/{id}/fps) for frame_rate_limit
  // This ensures SDK processing matches configured FPS
  // Fallback to frameRateLimit if configuredFps not set (backward compatibility)
  solutionManager["frame_rate_limit"] = 
      info.configuredFps > 0 ? info.configuredFps : info.frameRateLimit;
  solutionManager["send_metadata"] = info.metadataMode;
  solutionManager["run_statistics"] = info.statisticsMode;
  solutionManager["send_diagnostics"] = info.diagnosticsMode;
  solutionManager["enable_debug"] = info.debugMode;
  if (info.inputPixelLimit > 0) {
    solutionManager["input_pixel_limit"] = info.inputPixelLimit;
  }
  if (info.recommendedFrameRate > 0) {
    solutionManager["recommended_frame_rate"] = info.recommendedFrameRate;
  }
  config["SolutionManager"] = solutionManager;

  // Store Detector settings (always include, with defaults if needed)
  Json::Value detector(Json::objectValue);
  detector["animal_confidence_threshold"] = info.animalConfidenceThreshold > 0.0
                                                ? info.animalConfidenceThreshold
                                                : 0.3;
  detector["conf_threshold"] =
      info.confThreshold > 0.0 ? info.confThreshold : 0.2;
  detector["current_preset"] =
      info.detectorMode.empty() ? "FullRegionInference" : info.detectorMode;
  detector["current_sensitivity_preset"] =
      info.detectionSensitivity.empty() ? "High" : info.detectionSensitivity;
  detector["face_confidence_threshold"] =
      info.faceConfidenceThreshold > 0.0 ? info.faceConfidenceThreshold : 0.1;
  detector["license_plate_confidence_threshold"] =
      info.licensePlateConfidenceThreshold > 0.0
          ? info.licensePlateConfidenceThreshold
          : 0.1;
  detector["model_file"] = info.detectorModelFile.empty()
                               ? "pva_det_full_frame_512"
                               : info.detectorModelFile;
  detector["person_confidence_threshold"] = info.personConfidenceThreshold > 0.0
                                                ? info.personConfidenceThreshold
                                                : 0.3;
  detector["vehicle_confidence_threshold"] =
      info.vehicleConfidenceThreshold > 0.0 ? info.vehicleConfidenceThreshold
                                            : 0.3;

  // Preset values
  Json::Value presetValues(Json::objectValue);
  Json::Value mosaicInference(Json::objectValue);
  mosaicInference["Detector/model_file"] = "pva_det_mosaic_320";
  presetValues["MosaicInference"] = mosaicInference;
  detector["preset_values"] = presetValues;

  config["Detector"] = detector;

  // DetectorRegions (always include, empty by default)
  config["DetectorRegions"] = Json::Value(Json::objectValue);

  // Store DetectorThermal settings (always include)
  Json::Value detectorThermal(Json::objectValue);
  detectorThermal["model_file"] = info.detectorThermalModelFile.empty()
                                      ? "pva_det_mosaic_320"
                                      : info.detectorThermalModelFile;
  config["DetectorThermal"] = detectorThermal;

  // Store PerformanceMode (always include)
  Json::Value performanceMode(Json::objectValue);
  performanceMode["current_preset"] =
      info.performanceMode.empty() ? "Balanced" : info.performanceMode;
  config["PerformanceMode"] = performanceMode;

  // Store Tripwire (always include, empty by default)
  Json::Value tripwire(Json::objectValue);
  tripwire["Tripwires"] = Json::Value(Json::objectValue);
  config["Tripwire"] = tripwire;

  // Store Zone (always include, empty by default)
  Json::Value zone(Json::objectValue);
  zone["Zones"] = Json::Value(Json::objectValue);
  config["Zone"] = zone;

  // Store additional parameters as nested config
  if (!info.additionalParams.empty()) {
    std::cerr << "[InstanceStorage] Converting " << info.additionalParams.size()
              << " additionalParams to config JSON" << std::endl;
    for (const auto &pair : info.additionalParams) {
      // Skip internal flags
      if (pair.first == "__REPLACE_INPUT_OUTPUT_PARAMS__") {
        continue;
      }

      // Store model paths and other configs
      if (pair.first == "MODEL_PATH" || pair.first == "SFACE_MODEL_PATH") {
        // These might be stored in ObjectAttributeExtraction or Detector
        // For now, store in a generic AdditionalParams section
        if (!config.isMember("AdditionalParams")) {
          config["AdditionalParams"] = Json::Value(Json::objectValue);
        }
        std::cerr << "[InstanceStorage] Adding to config: " << pair.first
                  << " = " << pair.second << std::endl;
        config["AdditionalParams"][pair.first] = pair.second;
      } else {
        // Store other params
        if (!config.isMember("AdditionalParams")) {
          config["AdditionalParams"] = Json::Value(Json::objectValue);
        }
        std::cerr << "[InstanceStorage] Adding to config: " << pair.first
                  << " = " << pair.second << std::endl;
        config["AdditionalParams"][pair.first] = pair.second;
      }
    }
  }

  // Store runtime info (not persisted, but included for completeness)
  config["loaded"] = info.loaded;
  config["running"] = info.running;
  config["fps"] = info.fps;
  config["version"] = info.version;

  // Validate output config
  std::string configError;
  if (!validateConfigJson(config, configError)) {
    if (error) {
      *error = "Generated config validation failed: " + configError;
    }
    return Json::Value(Json::objectValue);
  }

  return config;
}

std::optional<InstanceInfo>
InstanceStorage::configJsonToInstanceInfo(const Json::Value &config,
                                          std::string *error) const {
  try {
    // Validate input config
    std::string validationError;
    if (!validateConfigJson(config, validationError)) {
      if (error) {
        *error = "Config validation failed: " + validationError;
      }
      return std::nullopt;
    }

    InstanceInfo info;

    // Extract InstanceId
    if (config.isMember("InstanceId") && config["InstanceId"].isString()) {
      info.instanceId = config["InstanceId"].asString();
    } else {
      if (error) {
        *error = "InstanceId is required but missing or invalid";
      }
      return std::nullopt; // InstanceId is required
    }

    // Extract DisplayName
    if (config.isMember("DisplayName") && config["DisplayName"].isString()) {
      info.displayName = config["DisplayName"].asString();
    }

    // Extract Solution
    if (config.isMember("Solution") && config["Solution"].isString()) {
      info.solutionId = config["Solution"].asString();
    }

    // Extract SolutionName
    if (config.isMember("SolutionName") && config["SolutionName"].isString()) {
      info.solutionName = config["SolutionName"].asString();
    }

    // Extract Group
    if (config.isMember("Group") && config["Group"].isString()) {
      info.group = config["Group"].asString();
    }

    // Extract ReadOnly
    if (config.isMember("ReadOnly") && config["ReadOnly"].isBool()) {
      info.readOnly = config["ReadOnly"].asBool();
    }

    // Extract SystemInstance
    if (config.isMember("SystemInstance") &&
        config["SystemInstance"].isBool()) {
      info.systemInstance = config["SystemInstance"].asBool();
    }

    // Extract AutoStart
    if (config.isMember("AutoStart") && config["AutoStart"].isBool()) {
      info.autoStart = config["AutoStart"].asBool();
    }

    // Extract AutoRestart
    if (config.isMember("AutoRestart") && config["AutoRestart"].isBool()) {
      info.autoRestart = config["AutoRestart"].asBool();
    }

    // Extract per-instance logging (API path: logging.enabled)
    if (config.isMember("logging") && config["logging"].isObject() &&
        config["logging"].isMember("enabled") &&
        config["logging"]["enabled"].isBool()) {
      info.instanceLoggingEnabled = config["logging"]["enabled"].asBool();
    }

    // Extract Input configuration
    if (config.isMember("Input") && config["Input"].isObject()) {
      const Json::Value &input = config["Input"];

      // Extract URI
      if (input.isMember("uri") && input["uri"].isString()) {
        std::string uri = input["uri"].asString();
        // Parse RTSP URL from GStreamer URI - support both old format (uri=)
        // and new format (location=)
        size_t rtspPos = uri.find("location=");
        if (rtspPos != std::string::npos) {
          // New format: rtspsrc location=...
          size_t start = rtspPos + 9;
          size_t end = uri.find(" ", start);
          if (end == std::string::npos) {
            end = uri.find(" !", start);
          }
          if (end == std::string::npos) {
            end = uri.length();
          }
          info.rtspUrl = uri.substr(start, end - start);
        } else {
          // Old format: gstreamer:///urisourcebin uri=...
          rtspPos = uri.find("uri=");
          if (rtspPos != std::string::npos) {
            size_t start = rtspPos + 4;
            size_t end = uri.find(" !", start);
            if (end == std::string::npos) {
              end = uri.length();
            }
            info.rtspUrl = uri.substr(start, end - start);
          } else if (uri.find("://") == std::string::npos) {
            // Direct file path (no protocol)
            info.filePath = uri;
          } else {
            // URL with protocol
            info.filePath = uri;
          }
        }
      }

      // Extract media_type
      if (input.isMember("media_type") && input["media_type"].isString()) {
        std::string mediaType = input["media_type"].asString();
        if (mediaType == "File" && info.filePath.empty() &&
            input.isMember("uri")) {
          info.filePath = input["uri"].asString();
        }
      }
    }

    // Extract RTMP URL from Output section if available
    if (config.isMember("Output") && config["Output"].isObject()) {
      const Json::Value &output = config["Output"];
      if (output.isMember("rtmpUrl") && output["rtmpUrl"].isString()) {
        info.rtmpUrl = output["rtmpUrl"].asString();
      }
    }

    // Extract OriginatorInfo
    if (config.isMember("OriginatorInfo") &&
        config["OriginatorInfo"].isObject()) {
      const Json::Value &originator = config["OriginatorInfo"];
      if (originator.isMember("address") && originator["address"].isString()) {
        info.originator.address = originator["address"].asString();
      }
    }

    // Extract SolutionManager settings
    if (config.isMember("SolutionManager") &&
        config["SolutionManager"].isObject()) {
      const Json::Value &sm = config["SolutionManager"];
      if (sm.isMember("frame_rate_limit") && sm["frame_rate_limit"].isInt()) {
        info.frameRateLimit = sm["frame_rate_limit"].asInt();
      }
      if (sm.isMember("send_metadata") && sm["send_metadata"].isBool()) {
        info.metadataMode = sm["send_metadata"].asBool();
      }
      if (sm.isMember("run_statistics") && sm["run_statistics"].isBool()) {
        info.statisticsMode = sm["run_statistics"].asBool();
      }
      if (sm.isMember("send_diagnostics") && sm["send_diagnostics"].isBool()) {
        info.diagnosticsMode = sm["send_diagnostics"].asBool();
      }
      if (sm.isMember("enable_debug") && sm["enable_debug"].isBool()) {
        info.debugMode = sm["enable_debug"].asBool();
      }
      if (sm.isMember("input_pixel_limit") && sm["input_pixel_limit"].isInt()) {
        info.inputPixelLimit = sm["input_pixel_limit"].asInt();
      }
      if (sm.isMember("recommended_frame_rate") &&
          sm["recommended_frame_rate"].isInt()) {
        info.recommendedFrameRate = sm["recommended_frame_rate"].asInt();
      }
    }

    // Extract Detector settings
    if (config.isMember("Detector") && config["Detector"].isObject()) {
      const Json::Value &detector = config["Detector"];
      if (detector.isMember("current_preset") &&
          detector["current_preset"].isString()) {
        info.detectorMode = detector["current_preset"].asString();
      }
      if (detector.isMember("current_sensitivity_preset") &&
          detector["current_sensitivity_preset"].isString()) {
        info.detectionSensitivity =
            detector["current_sensitivity_preset"].asString();
      }
      if (detector.isMember("model_file") &&
          detector["model_file"].isString()) {
        info.detectorModelFile = detector["model_file"].asString();
      }
      if (detector.isMember("animal_confidence_threshold") &&
          detector["animal_confidence_threshold"].isNumeric()) {
        info.animalConfidenceThreshold =
            detector["animal_confidence_threshold"].asDouble();
      }
      if (detector.isMember("person_confidence_threshold") &&
          detector["person_confidence_threshold"].isNumeric()) {
        info.personConfidenceThreshold =
            detector["person_confidence_threshold"].asDouble();
      }
      if (detector.isMember("vehicle_confidence_threshold") &&
          detector["vehicle_confidence_threshold"].isNumeric()) {
        info.vehicleConfidenceThreshold =
            detector["vehicle_confidence_threshold"].asDouble();
      }
      if (detector.isMember("face_confidence_threshold") &&
          detector["face_confidence_threshold"].isNumeric()) {
        info.faceConfidenceThreshold =
            detector["face_confidence_threshold"].asDouble();
      }
      if (detector.isMember("license_plate_confidence_threshold") &&
          detector["license_plate_confidence_threshold"].isNumeric()) {
        info.licensePlateConfidenceThreshold =
            detector["license_plate_confidence_threshold"].asDouble();
      }
      if (detector.isMember("conf_threshold") &&
          detector["conf_threshold"].isNumeric()) {
        info.confThreshold = detector["conf_threshold"].asDouble();
      }
    }

    // Extract DetectorThermal settings
    if (config.isMember("DetectorThermal") &&
        config["DetectorThermal"].isObject()) {
      const Json::Value &detectorThermal = config["DetectorThermal"];
      if (detectorThermal.isMember("model_file") &&
          detectorThermal["model_file"].isString()) {
        info.detectorThermalModelFile =
            detectorThermal["model_file"].asString();
      }
    }

    // Extract PerformanceMode
    if (config.isMember("PerformanceMode") &&
        config["PerformanceMode"].isObject()) {
      const Json::Value &performanceMode = config["PerformanceMode"];
      if (performanceMode.isMember("current_preset") &&
          performanceMode["current_preset"].isString()) {
        info.performanceMode = performanceMode["current_preset"].asString();
      }
    }

    // Extract Movement settings
    if (config.isMember("Movement") && config["Movement"].isObject()) {
      const Json::Value &movement = config["Movement"];
      if (movement.isMember("current_sensitivity_preset") &&
          movement["current_sensitivity_preset"].isString()) {
        info.movementSensitivity =
            movement["current_sensitivity_preset"].asString();
      }
    }

    // Extract AdditionalParams
    if (config.isMember("AdditionalParams") &&
        config["AdditionalParams"].isObject()) {
      const Json::Value &additionalParams = config["AdditionalParams"];
      for (const auto &key : additionalParams.getMemberNames()) {
        if (additionalParams[key].isString()) {
          info.additionalParams[key] = additionalParams[key].asString();

          // Extract RTSP_URL from additionalParams if not already set
          if (key == "RTSP_URL" && info.rtspUrl.empty()) {
            info.rtspUrl = additionalParams[key].asString();
          }

          // Extract FILE_PATH from additionalParams if not already set
          if (key == "FILE_PATH" && info.filePath.empty()) {
            info.filePath = additionalParams[key].asString();
          }

          // Extract RTMP output URL: prefer RTMP_DES_URL (output), else RTMP_URL if still unset
          if (key == "RTMP_DES_URL") {
            info.rtmpUrl = additionalParams[key].asString();
          } else if (key == "RTMP_URL" && info.rtmpUrl.empty()) {
            info.rtmpUrl = additionalParams[key].asString();
          }
        }
      }
    }

    // Extract runtime info
    if (config.isMember("loaded") && config["loaded"].isBool()) {
      info.loaded = config["loaded"].asBool();
    }
    if (config.isMember("running") && config["running"].isBool()) {
      info.running = config["running"].asBool();
    }
    if (config.isMember("fps") && config["fps"].isNumeric()) {
      info.fps = config["fps"].asDouble();
    }
    if (config.isMember("version") && config["version"].isString()) {
      info.version = config["version"].asString();
    }

    // Set defaults
    info.persistent = true; // All instances in instances.json are persistent
    info.loaded = true;
    info.running = false; // Will be set when started

    // Validate output InstanceInfo
    std::string infoError;
    if (!validateInstanceInfo(info, infoError)) {
      if (error) {
        *error = "Converted InstanceInfo validation failed: " + infoError;
      }
      return std::nullopt;
    }

    return info;
  } catch (const std::exception &e) {
    if (error) {
      *error = "Exception during conversion: " + std::string(e.what());
    }
    return std::nullopt;
  } catch (...) {
    if (error) {
      *error = "Unknown exception during conversion";
    }
    return std::nullopt;
  }
}

bool InstanceStorage::saveInstance(const std::string &instanceId,
                                   const InstanceInfo &info) {
  std::cerr << "[InstanceStorage] saveInstance called for instance: "
            << instanceId << std::endl;
  std::cerr << "[InstanceStorage] Storage directory: " << storage_dir_
            << std::endl;

  try {
    // Validate instanceId matches
    if (info.instanceId != instanceId) {
      std::cerr << "[InstanceStorage] Error: InstanceId mismatch. Expected: "
                << instanceId << ", Got: " << info.instanceId << std::endl;
      return false;
    }

    // Validate InstanceInfo
    std::string validationError;
    if (!validateInstanceInfo(info, validationError)) {
      std::cerr << "[InstanceStorage] Validation error: " << validationError
                << std::endl;
      return false;
    }

    // Load existing instances file
    Json::Value instances = loadInstancesFile();

    // Convert InstanceInfo to config JSON format
    std::string conversionError;
    Json::Value config = instanceInfoToConfigJson(info, &conversionError);
    if (config.isNull() || config.empty()) {
      std::cerr << "[InstanceStorage] Conversion error: " << conversionError
                << std::endl;
      std::cerr << "[InstanceStorage] InstanceId: " << instanceId << std::endl;
      return false;
    }

    std::cerr << "[InstanceStorage] Successfully converted InstanceInfo to "
                 "JSON for instance: "
              << instanceId << std::endl;

    // If instance already exists, merge with existing config to preserve
    // TensorRT and other nested configs
    if (instances.isMember(instanceId) && instances[instanceId].isObject()) {
      Json::Value existingConfig = instances[instanceId];

      // List of keys to preserve (TensorRT model IDs, Zone IDs, etc.)
      std::vector<std::string> preserveKeys;

      // Collect UUID-like keys (TensorRT model IDs)
      for (const auto &key : existingConfig.getMemberNames()) {
        if (key.length() >= 36 && key.find('-') != std::string::npos) {
          preserveKeys.push_back(key);
        }
      }

      // Add special keys to preserve
      std::vector<std::string> specialKeys = {"AnimalTracker",
                                              "DetectorRegions",
                                              "DetectorThermal",
                                              "Global",
                                              "LicensePlateTracker",
                                              "ObjectAttributeExtraction",
                                              "ObjectMovementClassifier",
                                              "PersonTracker",
                                              "Tripwire",
                                              "VehicleTracker",
                                              "Zone"};
      preserveKeys.insert(preserveKeys.end(), specialKeys.begin(),
                          specialKeys.end());

      // Merge configs
      if (!mergeConfigs(existingConfig, config, preserveKeys)) {
        std::cerr << "[InstanceStorage] Merge failed for instance "
                  << instanceId << std::endl;
        return false;
      }

      instances[instanceId] = existingConfig;
    } else {
      // New instance, just store the config
      instances[instanceId] = config;
    }

    // Save updated instances file
    if (!saveInstancesFile(instances)) {
      std::cerr << "[InstanceStorage] Failed to save instances file"
                << std::endl;
      return false;
    }

    std::cerr << "[InstanceStorage] ✓ Successfully saved instance: "
              << instanceId << std::endl;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[InstanceStorage] Exception in saveInstance: " << e.what()
              << std::endl;
    return false;
  } catch (...) {
    std::cerr << "[InstanceStorage] Unknown exception in saveInstance"
              << std::endl;
    return false;
  }
}

std::optional<InstanceInfo>
InstanceStorage::loadInstance(const std::string &instanceId) {
  try {
    // Validate instanceId
    if (instanceId.empty()) {
      std::cerr << "[InstanceStorage] Error: Empty instanceId provided"
                << std::endl;
      return std::nullopt;
    }

    // Load instances file
    Json::Value instances = loadInstancesFile();

    // Check if instance exists
    if (!instances.isMember(instanceId) || !instances[instanceId].isObject()) {
      std::cerr << "[InstanceStorage] Instance " << instanceId
                << " not found in instances.json" << std::endl;
      return std::nullopt;
    }

    // Convert config JSON to InstanceInfo
    std::string conversionError;
    auto info =
        configJsonToInstanceInfo(instances[instanceId], &conversionError);
    if (!info.has_value()) {
      std::cerr << "[InstanceStorage] Conversion error for instance "
                << instanceId << ": " << conversionError << std::endl;
      return std::nullopt;
    }

    // Verify instanceId matches
    if (info->instanceId != instanceId) {
      std::cerr << "[InstanceStorage] Warning: InstanceId mismatch. Expected: "
                << instanceId << ", Got: " << info->instanceId << std::endl;
      // Fix it
      info->instanceId = instanceId;
    }

    return info;
  } catch (const std::exception &e) {
    std::cerr << "[InstanceStorage] Exception in loadInstance: " << e.what()
              << std::endl;
    return std::nullopt;
  } catch (...) {
    std::cerr << "[InstanceStorage] Unknown exception in loadInstance"
              << std::endl;
    return std::nullopt;
  }
}

std::vector<std::string> InstanceStorage::loadAllInstances() {
  std::vector<std::string> loaded;

  try {
    // Load instances file
    Json::Value instances = loadInstancesFile();

    // Iterate through all keys in instances object
    for (const auto &key : instances.getMemberNames()) {
      // Skip special keys that are not instance IDs (like "AutoRestart",
      // "AnimalTracker", etc.) Instance IDs are UUIDs, so we check if the key
      // looks like a UUID or if it has an "InstanceId" field
      const Json::Value &value = instances[key];

      if (value.isObject()) {
        // Check if this is an instance config (has InstanceId field) or if key
        // is a UUID-like string
        bool isInstance = false;

        // Check if it has InstanceId field
        if (value.isMember("InstanceId") && value["InstanceId"].isString()) {
          isInstance = true;
        } else {
          // Check if key looks like a UUID (contains dashes and is long enough)
          // UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
          if (key.length() >= 36 && key.find('-') != std::string::npos) {
            isInstance = true;
          }
        }

        if (isInstance) {
          // Try to load to validate it's a valid instance
          auto info = loadInstance(key);
          if (info.has_value()) {
            loaded.push_back(key);
          }
        }
      }
    }
  } catch (const std::exception &e) {
    // Ignore errors
  }

  return loaded;
}

bool InstanceStorage::deleteInstance(const std::string &instanceId) {
  try {
    bool success = true;
    std::vector<std::string> failedTiers;

    // Delete from primary storage directory first
    Json::Value instances = loadInstancesFile();
    if (instances.isMember(instanceId)) {
      instances.removeMember(instanceId);
      if (!saveInstancesFile(instances)) {
        std::cerr << "[InstanceStorage] Failed to delete instance "
                  << instanceId << " from primary storage: " << storage_dir_
                  << std::endl;
        success = false;
        failedTiers.push_back(storage_dir_);
      } else {
        std::cerr << "[InstanceStorage] Deleted instance " << instanceId
                  << " from primary storage: " << storage_dir_ << std::endl;
      }
    }

    // Also delete from all fallback tiers to prevent instances from reappearing
    // on server restart. This ensures complete deletion across all storage
    // locations.
    std::filesystem::path path(storage_dir_);
    std::string subdir = path.filename().string();
    if (subdir.empty()) {
      subdir = "instances";
    }

    std::vector<std::string> allDirs =
        EnvConfig::getAllPossibleDirectories(subdir);

    for (const auto &dir : allDirs) {
      // Skip primary storage directory (already handled above)
      if (dir == storage_dir_) {
        continue;
      }

      std::string filepath = dir + "/instances.json";
      if (!std::filesystem::exists(filepath)) {
        continue; // Skip if file doesn't exist
      }

      try {
        // Load instances from this tier
        std::ifstream file(filepath);
        if (!file.is_open()) {
          continue;
        }

        Json::CharReaderBuilder builder;
        std::string errors;
        Json::Value tierInstances(Json::objectValue);
        if (!Json::parseFromStream(builder, file, &tierInstances, &errors)) {
          continue; // Skip if parsing failed
        }
        file.close();

        // Check if instance exists in this tier
        if (!tierInstances.isMember(instanceId)) {
          continue; // Instance not in this tier, skip
        }

        // Remove instance from this tier
        tierInstances.removeMember(instanceId);

        // Save updated file
        std::ofstream outFile(filepath);
        if (outFile.is_open()) {
          Json::StreamWriterBuilder writerBuilder;
          writerBuilder["indentation"] = "    ";
          std::unique_ptr<Json::StreamWriter> writer(
              writerBuilder.newStreamWriter());
          writer->write(tierInstances, &outFile);
          outFile.close();

          // Verify deletion by reloading the file
          std::ifstream verifyFile(filepath);
          if (verifyFile.is_open()) {
            Json::Value verifyInstances(Json::objectValue);
            std::string verifyErrors;
            if (Json::parseFromStream(builder, verifyFile, &verifyInstances,
                                      &verifyErrors)) {
              if (verifyInstances.isMember(instanceId)) {
                std::cerr << "[InstanceStorage] ERROR: Instance " << instanceId
                          << " still exists in tier after deletion: " << dir
                          << std::endl;
                success = false;
                failedTiers.push_back(dir);
              } else {
                std::cerr
                    << "[InstanceStorage] ✓ Verified deletion of instance "
                    << instanceId << " from tier: " << dir << std::endl;
              }
            }
            verifyFile.close();
          }
        } else {
          std::cerr << "[InstanceStorage] Warning: Could not open file for "
                       "writing in tier: "
                    << dir << std::endl;
          success = false;
          failedTiers.push_back(dir);
        }
      } catch (const std::exception &e) {
        std::cerr << "[InstanceStorage] Exception deleting from tier " << dir
                  << ": " << e.what() << std::endl;
        success = false;
        failedTiers.push_back(dir);
        // Continue with other tiers
      }
    }

    // Final verification: Check if instance still exists in any tier
    if (instanceExists(instanceId)) {
      std::cerr << "[InstanceStorage] ERROR: Instance " << instanceId
                << " still exists after deletion attempt!" << std::endl;
      std::cerr << "[InstanceStorage] Failed tiers: ";
      for (const auto &tier : failedTiers) {
        std::cerr << tier << " ";
      }
      std::cerr << std::endl;
      return false;
    }

    if (!success && !failedTiers.empty()) {
      std::cerr << "[InstanceStorage] Warning: Some tiers failed to delete, "
                   "but instance "
                << "was removed from primary storage. Failed tiers: ";
      for (const auto &tier : failedTiers) {
        std::cerr << tier << " ";
      }
      std::cerr << std::endl;
    }

    return success;
  } catch (const std::exception &e) {
    std::cerr << "[InstanceStorage] Exception in deleteInstance: " << e.what()
              << std::endl;
    return false;
  }
}

bool InstanceStorage::instanceExists(const std::string &instanceId) const {
  try {
    // Load instances file
    Json::Value instances = loadInstancesFile();

    // Check if instance exists
    return instances.isMember(instanceId) && instances[instanceId].isObject();
  } catch (const std::exception &e) {
    return false;
  }
}
