#include "core/pipeline_builder_model_resolver.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <vector>

namespace fs = std::filesystem;

std::string PipelineBuilderModelResolver::resolveModelPath(
    const std::string &relativePath) {
  // Helper function to resolve model file paths
  // Priority:
  // 1. CVEDIX_DATA_ROOT environment variable
  // 2. CVEDIX_SDK_ROOT environment variable + /cvedix_data
  // 3. Production path: /opt/omniapi/models (where user-uploaded models are
  // stored)
  // 4. System-wide installation paths (/usr/share/cvedix/cvedix_data/)
  // 5. SDK source locations (relative paths)
  // 6. Development fallback: ./cvedix_data/ (will NOT exist in production)

  // 1. Check CVEDIX_DATA_ROOT
  const char *dataRoot = std::getenv("CVEDIX_DATA_ROOT");
  if (dataRoot && strlen(dataRoot) > 0) {
    std::string path = std::string(dataRoot);
    if (path.back() != '/')
      path += '/';
    path += relativePath;
    if (fs::exists(path)) {
      std::cerr << "[PipelineBuilder] Using CVEDIX_DATA_ROOT: " << path
                << std::endl;
      return path;
    }
  }

  // 2. Check CVEDIX_SDK_ROOT
  const char *sdkRoot = std::getenv("CVEDIX_SDK_ROOT");
  if (sdkRoot && strlen(sdkRoot) > 0) {
    std::string path = std::string(sdkRoot);
    if (path.back() != '/')
      path += '/';
    path += "cvedix_data/" + relativePath;
    if (fs::exists(path)) {
      std::cerr << "[PipelineBuilder] Using CVEDIX_SDK_ROOT: " << path
                << std::endl;
      return path;
    }
  }

  // 3. Try production installation path (/opt/omniapi/models)
  // This is where user-uploaded models are stored
  // Check if relativePath starts with "models/" or is a direct model file
  if (relativePath.find("models/") == 0 || relativePath.find("models\\") == 0) {
    // Extract the model path after "models/"
    std::string modelPath =
        relativePath.substr(relativePath.find_first_of("/\\") + 1);
    std::string optPath = "/opt/omniapi/models/" + modelPath;
    if (fs::exists(optPath)) {
      std::cerr << "[PipelineBuilder] Using production path: " << optPath
                << std::endl;
      return optPath;
    }
    // Also try direct path if relativePath is just a filename
    std::string optPathDirect = "/opt/omniapi/models/" + relativePath;
    if (fs::exists(optPathDirect)) {
      std::cerr << "[PipelineBuilder] Using production path: " << optPathDirect
                << std::endl;
      return optPathDirect;
    }
  } else {
    // Try direct model file in /opt/omniapi/models
    std::string optPath = "/opt/omniapi/models/" + relativePath;
    if (fs::exists(optPath)) {
      std::cerr << "[PipelineBuilder] Using production path: " << optPath
                << std::endl;
      return optPath;
    }
  }

  // 4. Try system-wide installation paths (when SDK is installed to /usr)
  // Note: /usr/share/ is preferred (FHS standard for data files)
  // /usr/include/ is for header files, but we support it as fallback
  std::vector<std::string> systemPaths = {
      "/usr/share/cvedix/cvedix_data/" +
          relativePath, // Preferred (FHS standard)
      "/usr/local/share/cvedix/cvedix_data/" + relativePath, // Local install
      "/usr/include/cvedix/cvedix_data/" +
          relativePath, // Fallback (not recommended)
      "/usr/local/include/cvedix/cvedix_data/" +
          relativePath, // Local install fallback
  };

  for (const auto &path : systemPaths) {
    if (fs::exists(path)) {
      std::cerr << "[PipelineBuilder] Found in system-wide location: " << path
                << std::endl;
      return path;
    }

    // If exact file not found, try to find alternative yunet files in the same
    // directory This handles cases where file is named like
    // "face_detection_yunet_2023mar.onnx" instead of "yunet.onnx"
    if (relativePath.find("yunet.onnx") != std::string::npos) {
      fs::path dirPath = fs::path(path).parent_path();
      if (fs::exists(dirPath) && fs::is_directory(dirPath)) {
        // Look for alternative yunet files (prefer newer versions)
        std::vector<std::string> alternatives = {
            "face_detection_yunet_2023mar.onnx", // Newer version (preferred)
            "face_detection_yunet_2022mar.onnx", // Older version
            "yunet_2023mar.onnx",
            "yunet_2022mar.onnx",
        };

        for (const auto &alt : alternatives) {
          fs::path altPath = dirPath / alt;
          if (fs::exists(altPath)) {
            std::cerr << "[PipelineBuilder] Found alternative yunet model: "
                      << altPath.string() << std::endl;
            return altPath.string();
          }
        }
      }
    }
  }

  // 5. Try common SDK source locations (relative paths only, no hardcoded
  // absolute paths)
  std::vector<std::string> commonPaths = {
      "../edge_ai_sdk/cvedix_data/" + relativePath,
      "../../edge_ai_sdk/cvedix_data/" + relativePath,
      "../../../edge_ai_sdk/cvedix_data/" + relativePath,
  };

  for (const auto &path : commonPaths) {
    if (fs::exists(path)) {
      std::cerr << "[PipelineBuilder] Found in SDK directory: "
                << fs::absolute(path).string() << std::endl;
      return path;
    }
  }

  // 6. Development fallback: relative to current working directory
  // (./cvedix_data/) NOTE: This path will NOT exist in production - all data
  // should be in /opt/omniapi
  std::string relativePathFull = "./cvedix_data/" + relativePath;
  if (fs::exists(relativePathFull)) {
    std::cerr << "[PipelineBuilder] Using development relative path: "
              << fs::absolute(relativePathFull).string() << std::endl;
    return relativePathFull;
  }

  // Return default relative path (will show warning later if not found)
  std::cerr << "[PipelineBuilder] ⚠ Using default path (may not exist): "
               "./cvedix_data/"
            << relativePath << std::endl;
  std::cerr << "[PipelineBuilder] ℹ NOTE: In production, use "
               "/opt/omniapi/models/ or set CVEDIX_DATA_ROOT"
            << std::endl;
  return relativePathFull;
}

std::string PipelineBuilderModelResolver::resolveModelByName(
    const std::string &modelName, const std::string &category) {
  // Resolve model file by name (e.g., "yunet_2023mar", "yunet_2022mar",
  // "yolov8n_face") Supports various naming patterns and extensions

  // List of possible file extensions to try
  std::vector<std::string> extensions = {".onnx", ".rknn", ".weights", ".pt",
                                         ".pth",  ".pb",   ".tflite"};

  // List of possible file name patterns to try
  std::vector<std::string> patterns;

  // Direct match
  patterns.push_back(modelName);

  // Add common prefixes/suffixes
  if (modelName.find("yunet") != std::string::npos ||
      modelName.find("face") != std::string::npos) {
    patterns.push_back("face_detection_" + modelName);
    patterns.push_back(modelName + "_face_detection");
    if (modelName.find("yunet") == std::string::npos) {
      patterns.push_back("face_detection_yunet_" + modelName);
    }
  }

  // Try different search locations
  std::vector<std::string> searchDirs;

  // 1. Check CVEDIX_DATA_ROOT
  const char *dataRoot = std::getenv("CVEDIX_DATA_ROOT");
  if (dataRoot && strlen(dataRoot) > 0) {
    std::string dir = std::string(dataRoot);
    if (dir.back() != '/')
      dir += '/';
    dir += "models/" + category;
    searchDirs.push_back(dir);
  }

  // 2. Check CVEDIX_SDK_ROOT
  const char *sdkRoot = std::getenv("CVEDIX_SDK_ROOT");
  if (sdkRoot && strlen(sdkRoot) > 0) {
    std::string dir = std::string(sdkRoot);
    if (dir.back() != '/')
      dir += '/';
    dir += "cvedix_data/models/" + category;
    searchDirs.push_back(dir);
  }

  // 3. Production installation path (/opt/omniapi/models)
  // This is where user-uploaded models are stored
  searchDirs.push_back("/opt/omniapi/models/" + category);
  searchDirs.push_back(
      "/opt/omniapi/models"); // Also check root models directory

  // 4. System-wide locations
  searchDirs.push_back("/usr/share/cvedix/cvedix_data/models/" + category);
  searchDirs.push_back("/usr/local/share/cvedix/cvedix_data/models/" +
                       category);
  searchDirs.push_back("/usr/include/cvedix/cvedix_data/models/" + category);
  searchDirs.push_back("/usr/local/include/cvedix/cvedix_data/models/" +
                       category);

  // 5. SDK source locations (relative paths only, no hardcoded absolute paths)
  searchDirs.push_back("../edge_ai_sdk/cvedix_data/models/" + category);
  searchDirs.push_back("../../edge_ai_sdk/cvedix_data/models/" + category);
  searchDirs.push_back("../../../edge_ai_sdk/cvedix_data/models/" + category);

  // 6. Development fallback: relative to current working directory
  // (./cvedix_data/) NOTE: This path will NOT exist in production - all data
  // should be in /opt/omniapi
  searchDirs.push_back("./cvedix_data/models/" + category);
  searchDirs.push_back("./models"); // Also check API models directory

  // Search for model file
  for (const auto &dir : searchDirs) {
    fs::path dirPath(dir);
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
      continue;
    }

    // Try each pattern with each extension
    for (const auto &pattern : patterns) {
      for (const auto &ext : extensions) {
        fs::path filePath = dirPath / (pattern + ext);
        if (fs::exists(filePath)) {
          std::cerr << "[PipelineBuilder] Found model by name '" << modelName
                    << "' (pattern: " << pattern << ext
                    << ") at: " << fs::canonical(filePath).string()
                    << std::endl;
          return fs::canonical(filePath).string();
        }

        // Also try case-insensitive search (list directory)
        try {
          for (const auto &entry : fs::directory_iterator(dirPath)) {
            if (fs::is_regular_file(entry.path())) {
              std::string filename = entry.path().filename().string();
              std::string filenameLower = filename;
              std::transform(filenameLower.begin(), filenameLower.end(),
                             filenameLower.begin(), ::tolower);

              std::string patternLower = pattern + ext;
              std::transform(patternLower.begin(), patternLower.end(),
                             patternLower.begin(), ::tolower);

              if (filenameLower == patternLower ||
                  filenameLower.find(patternLower) != std::string::npos) {
                std::cerr << "[PipelineBuilder] Found model by name '"
                          << modelName << "' (matched: " << filename
                          << ") at: " << fs::canonical(entry.path()).string()
                          << std::endl;
                return fs::canonical(entry.path()).string();
              }
            }
          }
        } catch (const std::exception &e) {
          // Ignore directory iteration errors
        }
      }
    }
  }

  return ""; // Not found
}

std::vector<std::string>
PipelineBuilderModelResolver::listAvailableModels(const std::string &category) {
  std::vector<std::string> models;
  std::vector<std::string> extensions = {".onnx", ".rknn", ".weights", ".pt",
                                         ".pth",  ".pb",   ".tflite"};

  // List of search directories (same as resolveModelByName)
  std::vector<std::string> searchDirs;

  const char *dataRoot = std::getenv("CVEDIX_DATA_ROOT");
  if (dataRoot && strlen(dataRoot) > 0) {
    std::string dir = std::string(dataRoot);
    if (dir.back() != '/')
      dir += '/';
    if (category.empty()) {
      searchDirs.push_back(dir + "models");
    } else {
      searchDirs.push_back(dir + "models/" + category);
    }
  }

  const char *sdkRoot = std::getenv("CVEDIX_SDK_ROOT");
  if (sdkRoot && strlen(sdkRoot) > 0) {
    std::string dir = std::string(sdkRoot);
    if (dir.back() != '/')
      dir += '/';
    if (category.empty()) {
      searchDirs.push_back(dir + "cvedix_data/models");
    } else {
      searchDirs.push_back(dir + "cvedix_data/models/" + category);
    }
  }

  // Production installation path (/opt/omniapi/models)
  // This is where user-uploaded models are stored
  if (category.empty()) {
    searchDirs.push_back("/opt/omniapi/models");
  } else {
    searchDirs.push_back("/opt/omniapi/models/" + category);
    searchDirs.push_back(
        "/opt/omniapi/models"); // Also check root models directory
  }

  if (category.empty()) {
    searchDirs.push_back("/usr/share/cvedix/cvedix_data/models");
    searchDirs.push_back("/usr/local/share/cvedix/cvedix_data/models");
  } else {
    searchDirs.push_back("/usr/share/cvedix/cvedix_data/models/" + category);
    searchDirs.push_back("/usr/local/share/cvedix/cvedix_data/models/" +
                         category);
  }

  // Development fallback: relative to current working directory
  // (./cvedix_data/) NOTE: This path will NOT exist in production - all data
  // should be in /opt/omniapi
  searchDirs.push_back("./cvedix_data/models/" +
                       (category.empty() ? "" : category));
  searchDirs.push_back("./models");

  // Collect all model files
  std::set<std::string> uniqueModels; // Use set to avoid duplicates

  for (const auto &dir : searchDirs) {
    fs::path dirPath(dir);
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
      continue;
    }

    try {
      for (const auto &entry : fs::directory_iterator(dirPath)) {
        if (fs::is_regular_file(entry.path())) {
          std::string filename = entry.path().filename().string();
          std::string ext = entry.path().extension().string();

          // Check if it's a model file
          bool isModelFile = false;
          for (const auto &modelExt : extensions) {
            if (ext == modelExt ||
                filename.find(modelExt) != std::string::npos) {
              isModelFile = true;
              break;
            }
          }

          if (isModelFile) {
            uniqueModels.insert(fs::canonical(entry.path()).string());
          }
        }
      }
    } catch (const std::exception &e) {
      // Ignore directory iteration errors
    }
  }

  // Convert set to vector
  models.assign(uniqueModels.begin(), uniqueModels.end());
  return models;
}

double PipelineBuilderModelResolver::mapDetectionSensitivity(
    const std::string &sensitivity) {
  if (sensitivity == "Low") {
    return 0.5;
  } else if (sensitivity == "Medium") {
    return 0.7;
  } else if (sensitivity == "High") {
    return 0.9;
  }
  return 0.7; // Default to Medium
}

