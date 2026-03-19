#pragma once

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @brief Helper functions to parse environment variables
 *
 * Provides utilities to read and parse environment variables
 * with default values and validation.
 */
namespace EnvConfig {

/**
 * @brief Get string environment variable
 * @param name Variable name
 * @param default_value Default value if not set
 * @return String value or default
 */
inline std::string getString(const char *name,
                             const std::string &default_value = "") {
  const char *value = std::getenv(name);
  return value ? std::string(value) : default_value;
}

/**
 * @brief Get integer environment variable
 * @param name Variable name
 * @param default_value Default value if not set
 * @param min_value Minimum allowed value (optional)
 * @param max_value Maximum allowed value (optional)
 * @return Integer value or default
 */
inline int getInt(const char *name, int default_value,
                  int min_value = INT32_MIN, int max_value = INT32_MAX) {
  const char *value = std::getenv(name);
  if (!value) {
    return default_value;
  }

  try {
    int int_value = std::stoi(value);
    if (int_value < min_value || int_value > max_value) {
      std::cerr << "Warning: " << name << "=" << value << " is out of range ["
                << min_value << ", " << max_value
                << "]. Using default: " << default_value << std::endl;
      return default_value;
    }
    return int_value;
  } catch (const std::exception &e) {
    std::cerr << "Warning: Invalid " << name << "='" << value
              << "': " << e.what() << ". Using default: " << default_value
              << std::endl;
    return default_value;
  }
}

/**
 * @brief Get unsigned integer environment variable
 * @param name Variable name
 * @param default_value Default value if not set
 * @param max_value Maximum allowed value (optional)
 * @return Unsigned integer value or default
 */
inline uint32_t getUInt32(const char *name, uint32_t default_value,
                          uint32_t max_value = UINT32_MAX) {
  const char *value = std::getenv(name);
  if (!value) {
    return default_value;
  }

  try {
    int int_value = std::stoi(value);
    if (int_value < 0) {
      std::cerr << "Warning: " << name << "=" << value
                << " must be non-negative. Using default: " << default_value
                << std::endl;
      return default_value;
    }
    uint32_t uint_value = static_cast<uint32_t>(int_value);
    if (uint_value > max_value) {
      std::cerr << "Warning: " << name << "=" << value << " exceeds maximum "
                << max_value << ". Using default: " << default_value
                << std::endl;
      return default_value;
    }
    return uint_value;
  } catch (const std::exception &e) {
    std::cerr << "Warning: Invalid " << name << "='" << value
              << "': " << e.what() << ". Using default: " << default_value
              << std::endl;
    return default_value;
  }
}

/**
 * @brief Get size_t environment variable (for sizes, limits)
 * @param name Variable name
 * @param default_value Default value if not set
 * @return size_t value or default
 */
inline size_t getSizeT(const char *name, size_t default_value) {
  const char *value = std::getenv(name);
  if (!value) {
    return default_value;
  }

  try {
    int int_value = std::stoi(value);
    if (int_value < 0) {
      std::cerr << "Warning: " << name << "=" << value
                << " must be non-negative. Using default: " << default_value
                << std::endl;
      return default_value;
    }
    return static_cast<size_t>(int_value);
  } catch (const std::exception &e) {
    std::cerr << "Warning: Invalid " << name << "='" << value
              << "': " << e.what() << ". Using default: " << default_value
              << std::endl;
    return default_value;
  }
}

/**
 * @brief Get double environment variable
 * @param name Variable name
 * @param default_value Default value if not set
 * @param min_value Minimum allowed value (optional)
 * @param max_value Maximum allowed value (optional)
 * @return Double value or default
 */
inline double getDouble(const char *name, double default_value,
                        double min_value = -1e10, double max_value = 1e10) {
  const char *value = std::getenv(name);
  if (!value) {
    return default_value;
  }

  try {
    double double_value = std::stod(value);
    if (double_value < min_value || double_value > max_value) {
      std::cerr << "Warning: " << name << "=" << value << " is out of range ["
                << min_value << ", " << max_value
                << "]. Using default: " << default_value << std::endl;
      return default_value;
    }
    return double_value;
  } catch (const std::exception &e) {
    std::cerr << "Warning: Invalid " << name << "='" << value
              << "': " << e.what() << ". Using default: " << default_value
              << std::endl;
    return default_value;
  }
}

/**
 * @brief Get boolean environment variable
 * @param name Variable name
 * @param default_value Default value if not set
 * @return Boolean value or default
 */
inline bool getBool(const char *name, bool default_value) {
  const char *value = std::getenv(name);
  if (!value) {
    return default_value;
  }

  std::string str_value = value;
  std::transform(str_value.begin(), str_value.end(), str_value.begin(),
                 ::tolower);

  if (str_value == "1" || str_value == "true" || str_value == "yes" ||
      str_value == "on") {
    return true;
  } else if (str_value == "0" || str_value == "false" || str_value == "no" ||
             str_value == "off") {
    return false;
  }

  std::cerr << "Warning: Invalid " << name << "='" << value
            << "'. Expected boolean (true/false, 1/0, yes/no, on/off). Using "
               "default: "
            << (default_value ? "true" : "false") << std::endl;
  return default_value;
}

/**
 * @brief Parse log level from string
 * Note: This function requires drogon headers to be included
 * @param level_str Log level string (TRACE, DEBUG, INFO, WARN, ERROR)
 * @param default_level Default log level
 * @return Log level enum value
 */
inline int parseLogLevelInt(const std::string &level_str, int default_level) {
  if (level_str.empty()) {
    return default_level;
  }

  std::string upper = level_str;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

  // Map to trantor::Logger constants (defined in drogon)
  if (upper == "TRACE")
    return 0; // kTrace
  if (upper == "DEBUG")
    return 1; // kDebug
  if (upper == "INFO")
    return 2; // kInfo
  if (upper == "WARN")
    return 3; // kWarn
  if (upper == "ERROR")
    return 4; // kError

  std::cerr << "Warning: Invalid LOG_LEVEL='" << level_str
            << "'. Using default: INFO" << std::endl;
  return default_level;
}

/**
 * @brief Resolve directory path with 3-tier fallback strategy
 *
 * This function implements the directory creation strategy from
 * DIRECTORY_CREATION_GUIDE.md:
 * 1. Try to create preferred_path (production path)
 * 2. If permission denied, fallback to user directory
 * (~/.local/share/edgeos-api/{subdir})
 * 3. If that fails, fallback to current directory (./{subdir})
 *
 * Never throws exceptions - always returns a path (even if creation failed)
 *
 * @param preferred_path Preferred directory path (e.g.,
 * "/opt/edgeos-api/instances")
 * @param subdir Subdirectory name for fallback (e.g., "instances")
 * @return Resolved directory path (may be different from preferred_path if
 * fallback was used)
 */
inline std::string resolveDirectory(const std::string &preferred_path,
                                    const std::string &subdir = "") {
  std::string final_path = preferred_path;

  // Try to create preferred directory
  if (!std::filesystem::exists(final_path)) {
    try {
      std::filesystem::create_directories(final_path);
      std::cerr << "[EnvConfig] ✓ Created directory: " << final_path
                << std::endl;
      return final_path;
    } catch (const std::filesystem::filesystem_error &e) {
      if (e.code() == std::errc::permission_denied) {
        std::cerr << "[EnvConfig] ⚠ Cannot create " << final_path
                  << " (permission denied)" << std::endl;

        // Fallback 1: User directory
        const char *home = std::getenv("HOME");
        if (home && !subdir.empty()) {
          std::string fallback =
              std::string(home) + "/.local/share/edgeos-api/" + subdir;
          try {
            std::filesystem::create_directories(fallback);
            std::cerr << "[EnvConfig] ✓ Using fallback: " << fallback
                      << std::endl;
            return fallback;
          } catch (...) {
            // Fallback 2: Current directory
            if (!subdir.empty()) {
              std::string last_resort = "./" + subdir;
              try {
                std::filesystem::create_directories(last_resort);
                std::cerr << "[EnvConfig] ✓ Using last resort: " << last_resort
                          << std::endl;
                return last_resort;
              } catch (...) {
                std::cerr << "[EnvConfig] ⚠⚠ Warning: Cannot create even last "
                             "resort directory: "
                          << last_resort << std::endl;
                return last_resort; // Return anyway
              }
            }
          }
        } else if (!subdir.empty()) {
          // No HOME, use current directory
          std::string last_resort = "./" + subdir;
          try {
            std::filesystem::create_directories(last_resort);
            std::cerr << "[EnvConfig] ✓ Using last resort: " << last_resort
                      << std::endl;
            return last_resort;
          } catch (...) {
            std::cerr << "[EnvConfig] ⚠⚠ Warning: Cannot create last resort "
                         "directory: "
                      << last_resort << std::endl;
            return last_resort; // Return anyway
          }
        }
      } else {
        std::cerr << "[EnvConfig] ⚠ Error creating " << final_path << ": "
                  << e.what() << std::endl;
      }
    } catch (const std::exception &e) {
      std::cerr << "[EnvConfig] ⚠ Exception creating " << final_path << ": "
                << e.what() << std::endl;
    }
  } else {
    // Directory already exists
    if (std::filesystem::is_directory(final_path)) {
      std::cerr << "[EnvConfig] ✓ Directory already exists: " << final_path
                << std::endl;
    } else {
      std::cerr << "[EnvConfig] ⚠ Path exists but is not a directory: "
                << final_path << std::endl;
    }
  }

  return final_path;
}

/**
 * @brief Get all possible directory paths for a given subdir (for loading data
 * from all tiers)
 *
 * Returns all possible paths in priority order:
 * 1. Production path: /opt/edgeos-api/{subdir}
 * 2. User directory: ~/.local/share/edgeos-api/{subdir}
 * 3. Current directory: ./{subdir}
 *
 * @param subdir Subdirectory name (e.g., "instances")
 * @return Vector of directory paths in priority order
 */
inline std::vector<std::string>
getAllPossibleDirectories(const std::string &subdir) {
  std::vector<std::string> paths;

  // Tier 1: Production path
  paths.push_back("/opt/edgeos-api/" + subdir);

  // Tier 2: User directory
  const char *home = std::getenv("HOME");
  if (home) {
    paths.push_back(std::string(home) + "/.local/share/edgeos-api/" + subdir);
  }

  // Tier 3: Current directory
  paths.push_back("./" + subdir);

  return paths;
}

/**
 * @brief Resolve data directory path intelligently with 3-tier fallback
 *
 * Priority:
 * 1. Environment variable (if set) - highest priority
 * 2. Use /opt/edgeos-api/{subdir} as default (production path)
 * 3. Fallback to ~/.local/share/edgeos-api/{subdir} (user directory)
 * 4. Last resort: ./{subdir} (current directory)
 *
 * Directory will be created automatically if it doesn't exist.
 * Follows XDG Base Directory Specification for user fallback.
 *
 * @param env_var_name Environment variable name (e.g., "SOLUTIONS_DIR")
 * @param subdir Subdirectory name under /opt/edgeos-api (e.g., "solutions")
 * @return Resolved directory path
 */
inline std::string resolveDataDir(const char *env_var_name,
                                  const std::string &subdir) {
  // Tier 1: Check environment variable first (highest priority)
  const char *env_value = std::getenv(env_var_name);
  if (env_value && strlen(env_value) > 0) {
    std::string path = std::string(env_value);
    // Ensure directory exists
    try {
      std::filesystem::create_directories(path);
      std::cerr << "[EnvConfig] ✓ Using directory from " << env_var_name << ": "
                << path << std::endl;
      return path;
    } catch (const std::filesystem::filesystem_error &e) {
      if (e.code() == std::errc::permission_denied) {
        std::cerr << "[EnvConfig] ⚠ Cannot create user-specified directory "
                  << path << " (permission denied), trying fallback..."
                  << std::endl;
      } else {
        std::cerr << "[EnvConfig] ⚠ Error creating user-specified directory "
                  << path << ": " << e.what() << ", trying fallback..."
                  << std::endl;
      }
      // Fall through to default path
    } catch (...) {
      std::cerr << "[EnvConfig] ⚠ Error with user-specified directory " << path
                << ", trying fallback..." << std::endl;
      // Fall through to default path
    }
  }

  // Tier 2: Use /opt/edgeos-api/{subdir} as default (production path)
  std::string default_path = "/opt/edgeos-api/" + subdir;

  // Try to create production directory
  try {
    if (!std::filesystem::exists(default_path)) {
      std::filesystem::create_directories(default_path);
      std::cerr << "[EnvConfig] ✓ Created production directory: "
                << default_path << std::endl;
    } else {
      std::cerr << "[EnvConfig] ✓ Production directory already exists: "
                << default_path << std::endl;
    }
    return default_path;
  } catch (const std::filesystem::filesystem_error &e) {
    if (e.code() == std::errc::permission_denied) {
      std::cerr << "[EnvConfig] ⚠ Cannot create " << default_path
                << " (permission denied), trying fallback..." << std::endl;
    } else {
      std::cerr << "[EnvConfig] ⚠ Error creating " << default_path << ": "
                << e.what() << ", trying fallback..." << std::endl;
    }
    // Fall through to fallback
  } catch (const std::exception &e) {
    std::cerr << "[EnvConfig] ⚠ Exception creating " << default_path << ": "
              << e.what() << ", trying fallback..." << std::endl;
    // Fall through to fallback
  }

  // Tier 3: Fallback to user directory (~/.local/share/edgeos-api/{subdir})
  // Follows XDG Base Directory Specification
  const char *home = std::getenv("HOME");
  if (home) {
    std::string fallback_path =
        std::string(home) + "/.local/share/edgeos-api/" + subdir;
    try {
      std::filesystem::create_directories(fallback_path);
      std::cerr << "[EnvConfig] ✓ Using fallback user directory: "
                << fallback_path << std::endl;
      std::cerr
          << "[EnvConfig] ℹ Note: To use production path, run: sudo mkdir -p "
          << default_path << " && sudo chown $USER:$USER " << default_path
          << std::endl;
      return fallback_path;
    } catch (const std::exception &e) {
      std::cerr << "[EnvConfig] ⚠ Cannot create fallback directory "
                << fallback_path << ": " << e.what() << ", using last resort..."
                << std::endl;
      // Fall through to last resort
    }
  } else {
    std::cerr << "[EnvConfig] ⚠ HOME environment variable not set, using last "
                 "resort..."
              << std::endl;
  }

  // Tier 4: Last resort - current directory
  std::string last_resort = "./" + subdir;
  try {
    std::filesystem::create_directories(last_resort);
    std::cerr << "[EnvConfig] ⚠ Using last resort directory: " << last_resort
              << std::endl;
    std::cerr
        << "[EnvConfig] ℹ Note: To use production path, run: sudo mkdir -p "
        << default_path << " && sudo chown $USER:$USER " << default_path
        << std::endl;
    return last_resort;
  } catch (...) {
    // Even last resort failed - return anyway, storage classes will handle
    std::cerr
        << "[EnvConfig] ⚠⚠ Warning: Cannot create even last resort directory: "
        << last_resort << std::endl;
    return last_resort;
  }
}

/**
 * @brief Resolve default font path for OSD nodes
 *
 * Priority:
 * 1. OSD_DEFAULT_FONT_PATH environment variable (if set) - highest priority
 * 2. DEFAULT_FONT_PATH environment variable (if set)
 * 3. CVEDIX_DATA_ROOT/font/NotoSansCJKsc-Medium.otf (if CVEDIX_DATA_ROOT is
 * set)
 * 4. CVEDIX_SDK_ROOT/cvedix_data/font/NotoSansCJKsc-Medium.otf (if
 * CVEDIX_SDK_ROOT is set)
 * 5. ./cvedix_data/font/NotoSansCJKsc-Medium.otf (relative to current
 * directory)
 * 6. Empty string (use default font)
 *
 * @return Resolved font path, or empty string if not found
 */
inline std::string resolveDefaultFontPath() {
  // Priority 1: OSD_DEFAULT_FONT_PATH environment variable
  const char *osdFontPath = std::getenv("OSD_DEFAULT_FONT_PATH");
  if (osdFontPath && strlen(osdFontPath) > 0) {
    std::string path = std::string(osdFontPath);
    try {
      if (std::filesystem::exists(path)) {
        std::cerr << "[EnvConfig] ✓ Using font from OSD_DEFAULT_FONT_PATH: "
                  << path << std::endl;
        return path;
      } else {
        std::cerr << "[EnvConfig] ⚠ OSD_DEFAULT_FONT_PATH points to "
                     "non-existent file: "
                  << path << std::endl;
      }
    } catch (const std::filesystem::filesystem_error &) {
      // Permission denied or other filesystem error - skip this path
    }
  }

  // Priority 2: DEFAULT_FONT_PATH environment variable
  const char *defaultFontPath = std::getenv("DEFAULT_FONT_PATH");
  if (defaultFontPath && strlen(defaultFontPath) > 0) {
    std::string path = std::string(defaultFontPath);
    try {
      if (std::filesystem::exists(path)) {
        std::cerr << "[EnvConfig] ✓ Using font from DEFAULT_FONT_PATH: " << path
                  << std::endl;
        return path;
      } else {
        std::cerr
            << "[EnvConfig] ⚠ DEFAULT_FONT_PATH points to non-existent file: "
            << path << std::endl;
      }
    } catch (const std::filesystem::filesystem_error &) {
      // Permission denied or other filesystem error - skip this path
    }
  }

  // Priority 3: CVEDIX_DATA_ROOT/font/NotoSansCJKsc-Medium.otf
  const char *dataRoot = std::getenv("CVEDIX_DATA_ROOT");
  if (dataRoot && strlen(dataRoot) > 0) {
    std::string path = std::string(dataRoot);
    if (path.back() != '/')
      path += '/';
    path += "font/NotoSansCJKsc-Medium.otf";
    try {
      if (std::filesystem::exists(path)) {
        std::cerr << "[EnvConfig] ✓ Using font from CVEDIX_DATA_ROOT: " << path
                  << std::endl;
        return path;
      }
    } catch (const std::filesystem::filesystem_error &) {
      // Permission denied or other filesystem error - skip this path
    }
  }

  // Priority 4: CVEDIX_SDK_ROOT/cvedix_data/font/NotoSansCJKsc-Medium.otf
  const char *sdkRoot = std::getenv("CVEDIX_SDK_ROOT");
  if (sdkRoot && strlen(sdkRoot) > 0) {
    std::string path = std::string(sdkRoot);
    if (path.back() != '/')
      path += '/';
    path += "cvedix_data/font/NotoSansCJKsc-Medium.otf";
    try {
      if (std::filesystem::exists(path)) {
        std::cerr << "[EnvConfig] ✓ Using font from CVEDIX_SDK_ROOT: " << path
                  << std::endl;
        return path;
      }
    } catch (const std::filesystem::filesystem_error &) {
      // Permission denied or other filesystem error - skip this path
    }
  }

  // Priority 5: /opt/edgeos-api/fonts/NotoSansCJKsc-Medium.otf (production
  // fonts directory)
  std::string productionFontPath =
      "/opt/edgeos-api/fonts/NotoSansCJKsc-Medium.otf";
  try {
    if (std::filesystem::exists(productionFontPath)) {
      std::cerr << "[EnvConfig] ✓ Using font from production fonts directory: "
                << productionFontPath << std::endl;
      return productionFontPath;
    }
  } catch (const std::filesystem::filesystem_error &) {
    // Permission denied or other filesystem error - skip this path
  }

  // Priority 6: Development fallback:
  // ./cvedix_data/font/NotoSansCJKsc-Medium.otf NOTE: This path will NOT exist
  // in production - all fonts should be in /opt/edgeos-api/fonts
  std::string relativePath = "./cvedix_data/font/NotoSansCJKsc-Medium.otf";
  try {
    if (std::filesystem::exists(relativePath)) {
      std::cerr << "[EnvConfig] ✓ Using font from development directory: "
                << relativePath << std::endl;
      return std::filesystem::absolute(relativePath).string();
    }
  } catch (const std::filesystem::filesystem_error &) {
    // Permission denied or other filesystem error - skip this path
  }

  // Priority 7: Empty string (use default font)
  std::cerr << "[EnvConfig] ℹ No default font found, OSD nodes will use system "
               "default font"
            << std::endl;
  return "";
}

/**
 * @brief Resolve config file path intelligently with 3-tier fallback
 *
 * Priority:
 * 1. CONFIG_FILE environment variable (if set) - highest priority
 * 2. Try paths in order:
 *    - ./config.json (current directory)
 *    - /opt/edgeos-api/config/config.json (production)
 *    - /etc/edgeos-api/config.json (system)
 *    - ~/.config/edgeos-api/config.json (user config - fallback)
 *    - ./config.json (last resort)
 *
 * Parent directories will be created automatically if needed.
 *
 * @return Resolved config file path
 */
inline std::string resolveConfigPath() {
  // Priority 1: Environment variable (highest priority)
  const char *env_config_file = std::getenv("CONFIG_FILE");
  if (env_config_file && strlen(env_config_file) > 0) {
    std::string path = std::string(env_config_file);
    // Create parent directory if needed
    try {
      std::filesystem::path filePath(path);
      if (filePath.has_parent_path()) {
        std::filesystem::create_directories(filePath.parent_path());
      }
      std::cerr << "[EnvConfig] Using config file from CONFIG_FILE: " << path
                << std::endl;
      return path;
    } catch (const std::filesystem::filesystem_error &e) {
      if (e.code() == std::errc::permission_denied) {
        std::cerr << "[EnvConfig] ⚠ Cannot create directory for " << path
                  << " (permission denied), trying fallback..." << std::endl;
      } else {
        std::cerr << "[EnvConfig] ⚠ Error with CONFIG_FILE path " << path
                  << ": " << e.what() << ", trying fallback..." << std::endl;
      }
    } catch (...) {
      std::cerr << "[EnvConfig] ⚠ Error with CONFIG_FILE path " << path
                << ", trying fallback..." << std::endl;
    }
  }

  // Priority 2: Try paths in order with fallback pattern
  // Tier 1: Current directory (always accessible)
  std::string current_dir_path = "./config.json";
  if (std::filesystem::exists(current_dir_path)) {
    std::cerr << "[EnvConfig] ✓ Found existing config file: "
              << current_dir_path << " (current directory)" << std::endl;
    return current_dir_path;
  }

  // Tier 2: Production path (/opt/edgeos-api/config/config.json)
  std::string production_path = "/opt/edgeos-api/config/config.json";
  if (std::filesystem::exists(production_path)) {
    std::cerr << "[EnvConfig] ✓ Found existing config file: " << production_path
              << " (production)" << std::endl;
    return production_path;
  }

  // Try to create production directory
  try {
    std::filesystem::path filePath(production_path);
    if (filePath.has_parent_path()) {
      std::filesystem::create_directories(filePath.parent_path());
    }
    std::cerr << "[EnvConfig] ✓ Created directory and will use: "
              << production_path << " (production)" << std::endl;
    return production_path;
  } catch (const std::filesystem::filesystem_error &e) {
    if (e.code() == std::errc::permission_denied) {
      std::cerr << "[EnvConfig] ⚠ Cannot create " << production_path
                << " (permission denied), trying fallback..." << std::endl;
    } else {
      std::cerr << "[EnvConfig] ⚠ Error creating " << production_path << ": "
                << e.what() << ", trying fallback..." << std::endl;
    }
  } catch (...) {
    std::cerr << "[EnvConfig] ⚠ Error with " << production_path
              << ", trying fallback..." << std::endl;
  }

  // Tier 3: System path (/etc/edgeos-api/config.json)
  std::string system_path = "/etc/edgeos-api/config.json";
  if (std::filesystem::exists(system_path)) {
    std::cerr << "[EnvConfig] ✓ Found existing config file: " << system_path
              << " (system)" << std::endl;
    return system_path;
  }

  // Try to create system directory
  try {
    std::filesystem::path filePath(system_path);
    if (filePath.has_parent_path()) {
      std::filesystem::create_directories(filePath.parent_path());
    }
    std::cerr << "[EnvConfig] ✓ Created directory and will use: " << system_path
              << " (system)" << std::endl;
    return system_path;
  } catch (const std::filesystem::filesystem_error &e) {
    if (e.code() == std::errc::permission_denied) {
      std::cerr << "[EnvConfig] ⚠ Cannot create " << system_path
                << " (permission denied), trying fallback..." << std::endl;
    } else {
      std::cerr << "[EnvConfig] ⚠ Error creating " << system_path << ": "
                << e.what() << ", trying fallback..." << std::endl;
    }
  } catch (...) {
    std::cerr << "[EnvConfig] ⚠ Error with " << system_path
              << ", trying fallback..." << std::endl;
  }

  // Fallback 1: User config directory (~/.config/edgeos-api/config.json)
  const char *home = std::getenv("HOME");
  if (home) {
    std::string user_config =
        std::string(home) + "/.config/edgeos-api/config.json";
    try {
      std::filesystem::path filePath(user_config);
      if (filePath.has_parent_path()) {
        std::filesystem::create_directories(filePath.parent_path());
      }
      std::cerr << "[EnvConfig] ✓ Using fallback user config: " << user_config
                << std::endl;
      return user_config;
    } catch (...) {
      std::cerr << "[EnvConfig] ⚠ Cannot create user config directory, using "
                   "last resort..."
                << std::endl;
    }
  }

  // Last resort: Current directory (always works)
  std::cerr
      << "[EnvConfig] ✓ Using last resort: ./config.json (current directory)"
      << std::endl;
  std::cerr << "[EnvConfig] ℹ Note: To use production path, run: sudo mkdir -p "
               "/opt/edgeos-api/config"
            << std::endl;
  return "./config.json";
}

/**
 * @brief Load KEY=VALUE from a .env file and setenv (for dev startup).
 * Skips empty lines and lines starting with #. Overwrites existing env.
 * @param path Full path to .env file
 * @return true if file was read (even if no valid lines), false if file missing/unreadable
 */
inline bool loadDotenv(const std::string &path) {
  std::ifstream f(path);
  if (!f.is_open()) {
    return false;
  }
  std::string line;
  int count = 0;
  while (std::getline(f, line)) {
    // Trim and skip empty / comment
    auto start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
      continue;
    }
    if (line[start] == '#') {
      continue;
    }
    auto eq = line.find('=', start);
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = line.substr(start, eq - start);
    std::string value = line.substr(eq + 1);
    auto key_end = key.find_last_not_of(" \t");
    if (key_end != std::string::npos) {
      key = key.substr(0, key_end + 1);
    }
    auto val_start = value.find_first_not_of(" \t\"'");
    if (val_start != std::string::npos) {
      value = value.substr(val_start);
      auto val_end = value.find_last_not_of(" \t\"'\r");
      if (val_end != std::string::npos) {
        value = value.substr(0, val_end + 1);
      }
    } else {
      value.clear();
    }
    if (!key.empty()) {
      setenv(key.c_str(), value.c_str(), 1);
      count++;
    }
  }
  if (count > 0) {
    std::cerr << "[EnvConfig] Loaded " << count << " variable(s) from " << path
              << std::endl;
  }
  return true;
}

/**
 * @brief When --dev: load .env (or .env.example) from project root.
 * Walks up from executable directory, tries .env then .env.example at each level.
 * setenv(..., 1) so values override existing env. Use when --dev flag is passed.
 * @param argv0 argv[0] from main (executable path)
 * @return true if a file was loaded
 */
inline bool loadDotenvFromProjectRootOrExample(const char *argv0) {
  if (!argv0 || !argv0[0]) {
    return false;
  }
  try {
    std::filesystem::path path(argv0);
    if (!path.is_absolute()) {
      path = std::filesystem::absolute(path);
    }
    path = path.parent_path();
    for (int up = 0; up < 5 && path.has_parent_path(); ++up) {
      std::string env_path = (path / ".env").string();
      if (loadDotenv(env_path)) {
        return true;
      }
      std::string example_path = (path / ".env.example").string();
      if (loadDotenv(example_path)) {
        return true;
      }
      path = path.parent_path();
    }
  } catch (...) {
  }
  return false;
}

/**
 * @brief When in dev (binary not under /opt/edgeos-api), try to load .env
 * so config/env is taken from project .env without needing to source it.
 * Set EDGEOS_LOAD_DOTENV=1 to force load; set EDGEOS_DOTENV_PATH to path to .env
 * @param argv0 argv[0] from main (executable path)
 */
inline void tryLoadDotenvForDev(const char *argv0) {
  const char *force = std::getenv("EDGEOS_LOAD_DOTENV");
  bool is_dev = true;
  if (argv0 && std::strstr(argv0, "/opt/edgeos-api") != nullptr) {
    is_dev = false;
  }
  if (force && (std::strcmp(force, "1") == 0 || std::strcmp(force, "true") == 0 ||
                std::strcmp(force, "yes") == 0)) {
    is_dev = true;
  }
  if (!is_dev) {
    return;
  }
  const char *explicit_path = std::getenv("EDGEOS_DOTENV_PATH");
  if (explicit_path && explicit_path[0]) {
    if (loadDotenv(explicit_path)) {
      return;
    }
  }
  try {
    std::string cwd = std::filesystem::current_path().string();
    std::string cwd_env = cwd + "/.env";
    if (loadDotenv(cwd_env)) {
      return;
    }
    if (argv0 && argv0[0]) {
      std::filesystem::path exe(argv0);
      if (!exe.is_absolute()) {
        exe = std::filesystem::absolute(exe);
      }
      std::filesystem::path root = exe.parent_path();
      for (int i = 0; i < 3 && root.has_parent_path(); ++i) {
        root = root.parent_path();
        std::string candidate = (root / ".env").string();
        if (loadDotenv(candidate)) {
          return;
        }
      }
    }
  } catch (...) {
  }
}

} // namespace EnvConfig
