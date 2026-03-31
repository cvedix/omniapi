#include "core/cvedix_validator.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

bool CVEDIXValidator::validateModelFile(const std::string &modelPath) {
  if (modelPath.empty()) {
    throw std::runtime_error("Model path cannot be empty");
  }

  fs::path modelFilePath(modelPath);

  // Check if file exists
  if (!fs::exists(modelFilePath)) {
    std::ostringstream oss;
    oss << "Model file not found: " << modelPath << "\n";
    oss << "Absolute path: " << fs::absolute(modelFilePath).string() << "\n";
    oss << "\nSOLUTION:\n";
    oss << "  1. Ensure model file exists at the specified path\n";
    oss << "  2. Check CVEDIX_DATA_ROOT environment variable\n";
    oss << "  3. Upload model via API: POST /v1/core/model/upload\n";
    throw std::runtime_error(oss.str());
  }

  // Check if it's a regular file (not directory)
  if (!fs::is_regular_file(modelFilePath)) {
    throw std::runtime_error("Model path is not a regular file: " + modelPath);
  }

  // Check file permissions
  if (!isFileReadable(modelFilePath)) {
    throw std::runtime_error(getPermissionErrorMessage(modelPath));
  }

  // Check parent directory is traversable
  fs::path parentDir = modelFilePath.parent_path();
  if (!parentDir.empty() && !isDirectoryTraversable(parentDir)) {
    std::ostringstream oss;
    oss << "Cannot access parent directory: " << parentDir.string() << "\n";
    oss << getPermissionErrorMessage(modelPath);
    throw std::runtime_error(oss.str());
  }

  return true;
}

bool CVEDIXValidator::isFileReadable(const fs::path &filePath) {
  if (!fs::exists(filePath)) {
    return false;
  }

  // Try to open file for reading
  std::ifstream testFile(filePath, std::ios::binary);
  if (!testFile.good()) {
    return false;
  }
  testFile.close();

  // Check POSIX read permission
  if (access(filePath.c_str(), R_OK) != 0) {
    return false;
  }

  return true;
}

bool CVEDIXValidator::isDirectoryTraversable(const fs::path &dirPath) {
  if (!fs::exists(dirPath)) {
    return false;
  }

  if (!fs::is_directory(dirPath)) {
    return false;
  }

  // Check POSIX execute permission (required for directory traversal)
  if (access(dirPath.c_str(), X_OK) != 0) {
    return false;
  }

  return true;
}

bool CVEDIXValidator::validateSDKDependencies() {
  bool allOk = true;

  if (!checkTinyExprLibrary()) {
    allOk = false;
  }

  // Note: Cereal and cpp-base64 are only needed at compile time
  // We check them here for completeness but they're not critical at runtime
  // if SDK was already compiled

  return allOk;
}

std::string CVEDIXValidator::getPermissionErrorMessage(const std::string &filePath) {
  std::ostringstream oss;
  oss << "Permission denied accessing file: " << filePath << "\n";
  oss << "\nSOLUTION:\n";
  oss << "  1. Check file permissions:\n";
  oss << "     ls -la " << filePath << "\n";
  oss << "  2. File should be readable (644 or 664)\n";
  oss << "  3. Directory should be traversable (755 or 775)\n";
  oss << "  4. Fix permissions and symlinks:\n";
  oss << "     sudo systemctl restart omniapi\n";
  oss << "  5. Restart service:\n";
  oss << "     sudo systemctl restart omniapi\n";
  return oss.str();
}

std::string CVEDIXValidator::getDependencyErrorMessage() {
  std::ostringstream oss;
  oss << "CVEDIX SDK dependencies not available\n";
  oss << "\nSOLUTION:\n";
  oss << "  1. Fix symlinks:\n";
  oss << "     sudo /opt/omniapi/scripts/dev_setup.sh --skip-deps --skip-build\n";
  oss << "  2. Update library cache:\n";
  oss << "     sudo ldconfig\n";
  oss << "  3. Restart service:\n";
  oss << "     sudo systemctl restart omniapi\n";
  return oss.str();
}

void CVEDIXValidator::preCheckBeforeNodeCreation(const std::string &modelPath) {
  // Validate model file first (most common issue)
  validateModelFile(modelPath);

  // Validate SDK dependencies (less common but important)
  if (!validateSDKDependencies()) {
    std::cerr << "[CVEDIXValidator] Warning: Some SDK dependencies may be "
                 "missing\n";
    std::cerr << "[CVEDIXValidator] " << getDependencyErrorMessage()
              << std::endl;
    // Don't throw here - dependencies might still work if SDK was pre-compiled
    // But log warning for user awareness
  }
}

bool CVEDIXValidator::checkTinyExprLibrary() {
  // Check in common locations
  const char *paths[] = {
      "/usr/lib/libtinyexpr.so",
      "/opt/cvedix/lib/libtinyexpr.so",
      "/usr/lib/x86_64-linux-gnu/libtinyexpr.so",
  };

  for (const char *path : paths) {
    if (fs::exists(path) && fs::is_regular_file(path)) {
      return true;
    }
  }

  return false;
}

bool CVEDIXValidator::checkCerealHeaders() {
  // Check in common locations
  const char *paths[] = {
      "/usr/include/cereal",
      "/usr/local/include/cereal",
      "/opt/cvedix/include/cvedix/third_party/cereal",
  };

  for (const char *path : paths) {
    if (fs::exists(path) && fs::is_directory(path)) {
      // Check for main header
      fs::path headerPath = fs::path(path) / "cereal.hpp";
      if (fs::exists(headerPath)) {
        return true;
      }
    }
  }

  return false;
}

bool CVEDIXValidator::checkCppBase64Headers() {
  // Check in common locations
  const char *paths[] = {
      "/usr/include/base64.h",
      "/usr/local/include/base64.h",
      "/opt/cvedix/include/cvedix/third_party/cpp_base64/base64.h",
  };

  for (const char *path : paths) {
    if (fs::exists(path) && fs::is_regular_file(path)) {
      return true;
    }
  }

  return false;
}

