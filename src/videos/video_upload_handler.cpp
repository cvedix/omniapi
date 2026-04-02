#include "videos/video_upload_handler.h"
#include "core/cors_helper.h"
#include "core/env_config.h"
#include "core/metrics_interceptor.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

std::string VideoUploadHandler::videos_dir_ = "./videos";

void VideoUploadHandler::setVideosDirectory(const std::string &dir) {
  videos_dir_ = dir;
}

std::string VideoUploadHandler::getVideosDirectory() const {
  return videos_dir_;
}

bool VideoUploadHandler::isValidVideoFile(const std::string &filename) const {
  // Allow common video file extensions
  std::string lower = filename;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  // Helper lambda to check suffix (C++17 compatible)
  auto hasSuffix = [](const std::string &str,
                      const std::string &suffix) -> bool {
    if (suffix.length() > str.length())
      return false;
    return str.compare(str.length() - suffix.length(), suffix.length(),
                       suffix) == 0;
  };

  return hasSuffix(lower, ".mp4") || hasSuffix(lower, ".avi") ||
         hasSuffix(lower, ".mkv") || hasSuffix(lower, ".mov") ||
         hasSuffix(lower, ".flv") || hasSuffix(lower, ".wmv") ||
         hasSuffix(lower, ".webm") || hasSuffix(lower, ".m4v") ||
         hasSuffix(lower, ".3gp") || hasSuffix(lower, ".ts") ||
         hasSuffix(lower, ".m3u8") || hasSuffix(lower, ".mpg") ||
         hasSuffix(lower, ".mpeg") || hasSuffix(lower, ".vob") ||
         hasSuffix(lower, ".asf") || hasSuffix(lower, ".rm") ||
         hasSuffix(lower, ".rmvb") || hasSuffix(lower, ".ogv") ||
         hasSuffix(lower, ".divx") || hasSuffix(lower, ".xvid") ||
         hasSuffix(lower, ".f4v") || hasSuffix(lower, ".mts") ||
         hasSuffix(lower, ".m2ts") || hasSuffix(lower, ".mxf") ||
         hasSuffix(lower, ".dv") || hasSuffix(lower, ".yuv") ||
         hasSuffix(lower, ".raw") || hasSuffix(lower, ".h264") ||
         hasSuffix(lower, ".h265") || hasSuffix(lower, ".hevc") ||
         hasSuffix(lower, ".vp8") || hasSuffix(lower, ".vp9") ||
         hasSuffix(lower, ".av1");
}

std::string
VideoUploadHandler::sanitizeFilename(const std::string &filename) const {
  std::string sanitized;
  for (char c : filename) {
    // Allow alphanumeric, dot, dash, underscore
    if (std::isalnum(c) || c == '.' || c == '-' || c == '_') {
      sanitized += c;
    }
  }
  // Remove any path traversal attempts
  if (sanitized.find("..") != std::string::npos) {
    sanitized.clear();
  }
  return sanitized;
}

std::string
VideoUploadHandler::sanitizeDirectoryPath(const std::string &dirPath) const {
  if (dirPath.empty()) {
    return "";
  }

  std::string sanitized;
  std::string currentSegment;

  for (char c : dirPath) {
    if (c == '/' || c == '\\') {
      // End of segment, validate and add
      if (!currentSegment.empty()) {
        // Sanitize segment
        std::string sanitizedSegment;
        for (char segChar : currentSegment) {
          // Allow alphanumeric, dash, underscore for directory names
          if (std::isalnum(segChar) || segChar == '-' || segChar == '_') {
            sanitizedSegment += segChar;
          }
        }
        if (!sanitizedSegment.empty() && sanitizedSegment != "." &&
            sanitizedSegment != "..") {
          if (!sanitized.empty()) {
            sanitized += "/";
          }
          sanitized += sanitizedSegment;
        }
        currentSegment.clear();
      }
    } else {
      currentSegment += c;
    }
  }

  // Handle last segment
  if (!currentSegment.empty()) {
    std::string sanitizedSegment;
    for (char segChar : currentSegment) {
      if (std::isalnum(segChar) || segChar == '-' || segChar == '_') {
        sanitizedSegment += segChar;
      }
    }
    if (!sanitizedSegment.empty() && sanitizedSegment != "." &&
        sanitizedSegment != "..") {
      if (!sanitized.empty()) {
        sanitized += "/";
      }
      sanitized += sanitizedSegment;
    }
  }

  // Remove any path traversal attempts
  if (sanitized.find("..") != std::string::npos) {
    sanitized.clear();
  }

  return sanitized;
}

std::string
VideoUploadHandler::getDirectoryPath(const HttpRequestPtr &req) const {
  // Try query parameter first
  std::string dirPath = req->getParameter("directory");

  // URL decode the directory path if it contains encoded characters
  // (Drogon usually auto-decodes, but we do it explicitly for safety)
  if (!dirPath.empty()) {
    std::string decoded;
    decoded.reserve(dirPath.length());
    for (size_t i = 0; i < dirPath.length(); ++i) {
      if (dirPath[i] == '%' && i + 2 < dirPath.length()) {
        // Try to decode hex value
        char hex[3] = {dirPath[i + 1], dirPath[i + 2], '\0'};
        char *end;
        unsigned long value = std::strtoul(hex, &end, 16);
        if (*end == '\0' && value <= 255) {
          decoded += static_cast<char>(value);
          i += 2; // Skip the hex digits
        } else {
          decoded += dirPath[i]; // Invalid encoding, keep as-is
        }
      } else {
        decoded += dirPath[i];
      }
    }
    dirPath = decoded;
  }

  // If not in query, try to get from multipart form data
  if (dirPath.empty()) {
    std::string contentType = req->getHeader("Content-Type");
    if (contentType.find("multipart/form-data") != std::string::npos) {
      // Parse multipart to find directory field
      std::string boundary;
      size_t boundaryPos = contentType.find("boundary=");
      if (boundaryPos != std::string::npos) {
        boundaryPos += 9; // length of "boundary="
        size_t endPos = contentType.find_first_of("; \r\n", boundaryPos);
        if (endPos != std::string::npos) {
          boundary = contentType.substr(boundaryPos, endPos - boundaryPos);
        } else {
          boundary = contentType.substr(boundaryPos);
        }
        // Remove quotes if present
        if (!boundary.empty() && boundary.front() == '"' &&
            boundary.back() == '"') {
          boundary = boundary.substr(1, boundary.length() - 2);
        }
      }

      if (!boundary.empty()) {
        auto body = req->getBody();
        if (!body.empty()) {
          std::string bodyStr(reinterpret_cast<const char *>(body.data()),
                              body.size());
          std::string boundaryMarker = "--" + boundary;

          // Look for directory field in multipart
          size_t searchPos = 0;
          size_t partStart = bodyStr.find(boundaryMarker, searchPos);
          while (partStart != std::string::npos) {
            partStart += boundaryMarker.length();
            if (partStart >= bodyStr.length())
              break;

            // Skip \r\n
            if (partStart < bodyStr.length() && bodyStr[partStart] == '\r')
              partStart++;
            if (partStart < bodyStr.length() && bodyStr[partStart] == '\n')
              partStart++;

            // Find Content-Disposition
            size_t contentDispositionPos =
                bodyStr.find("Content-Disposition:", partStart);
            if (contentDispositionPos != std::string::npos &&
                contentDispositionPos < partStart + 1024) {
              // Check if this is a directory field
              size_t namePos =
                  bodyStr.find("name=\"directory\"", contentDispositionPos);
              if (namePos == std::string::npos) {
                namePos = bodyStr.find("name=directory", contentDispositionPos);
              }

              if (namePos != std::string::npos &&
                  namePos < contentDispositionPos + 512) {
                // Find content start
                size_t contentStart =
                    bodyStr.find("\r\n\r\n", contentDispositionPos);
                if (contentStart != std::string::npos) {
                  contentStart += 4;
                } else {
                  contentStart = bodyStr.find("\n\n", contentDispositionPos);
                  if (contentStart != std::string::npos) {
                    contentStart += 2;
                  }
                }

                if (contentStart != std::string::npos) {
                  // Find content end (next boundary)
                  size_t nextBoundary =
                      bodyStr.find(boundaryMarker, contentStart);
                  size_t contentEnd = (nextBoundary != std::string::npos)
                                          ? nextBoundary
                                          : bodyStr.length();

                  // Remove trailing \r\n
                  while (contentEnd > contentStart &&
                         (bodyStr[contentEnd - 1] == '\r' ||
                          bodyStr[contentEnd - 1] == '\n')) {
                    contentEnd--;
                  }

                  if (contentEnd > contentStart) {
                    dirPath =
                        bodyStr.substr(contentStart, contentEnd - contentStart);
                    break; // Found directory, stop searching
                  }
                }
              }
            }

            // Move to next boundary
            partStart = bodyStr.find(boundaryMarker, partStart);
          }
        }
      }
    }
  }

  return sanitizeDirectoryPath(dirPath);
}

std::string
VideoUploadHandler::extractVideoName(const HttpRequestPtr &req) const {
  // Try getParameter first (standard way - Drogon auto-extracts from path
  // pattern)
  std::string videoName = req->getParameter("videoName");

  // Fallback: extract from path if getParameter doesn't work
  if (videoName.empty()) {
    // Try both path() and getPath() methods - path() includes query string
    std::string path = req->path();
    if (path.empty()) {
      path = req->getPath();
    }

    // Try /v1/securt/video/ pattern first
    size_t videoPos = path.find("/v1/securt/video/");
    if (videoPos != std::string::npos) {
      size_t start = videoPos + 15;       // length of "/v1/securt/video/"
      size_t end = path.find("?", start); // Stop at query string if present
      if (end == std::string::npos) {
        end = path.length();
      }
      videoName = path.substr(start, end - start);
    } else {
      // Fallback to old pattern /videos/ for backward compatibility
      size_t videosPos = path.find("/videos/");
      if (videosPos != std::string::npos) {
        size_t start = videosPos + 8;       // length of "/videos/"
        size_t end = path.find("?", start); // Stop at query string if present
        if (end == std::string::npos) {
          end = path.length();
        }
        videoName = path.substr(start, end - start);
      }
    }
  }

  // URL decode the video name if it contains encoded characters
  if (!videoName.empty()) {
    std::string decoded;
    decoded.reserve(videoName.length());
    for (size_t i = 0; i < videoName.length(); ++i) {
      if (videoName[i] == '%' && i + 2 < videoName.length()) {
        // Try to decode hex value
        char hex[3] = {videoName[i + 1], videoName[i + 2], '\0'};
        char *end;
        unsigned long value = std::strtoul(hex, &end, 16);
        if (*end == '\0' && value <= 255) {
          decoded += static_cast<char>(value);
          i += 2; // Skip the hex digits
        } else {
          decoded += videoName[i]; // Invalid encoding, keep as-is
        }
      } else {
        decoded += videoName[i];
      }
    }
    videoName = decoded;
  }

  return videoName;
}

void VideoUploadHandler::uploadVideo(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    // Check content type from header
    std::string contentType = req->getHeader("Content-Type");
    bool isMultipart =
        contentType.find("multipart/form-data") != std::string::npos;
    bool isOctetStream =
        contentType.find("application/octet-stream") != std::string::npos;
    bool isBinary = contentType.find("binary") != std::string::npos;

    if (!isMultipart && !isOctetStream && !isBinary) {
      callback(createErrorResponse(
          400, "Invalid content type",
          "Request must be multipart/form-data, application/octet-stream, or "
          "contain binary data"));
      return;
    }

    std::string originalFilename;
    std::filesystem::path filePath;
    std::string sanitizedFilename;

    // Handle multipart/form-data uploads (from curl -F or HTML form)
    if (isMultipart) {
      // Parse multipart form data to extract all files
      // Get boundary from Content-Type header
      std::string boundary;
      size_t boundaryPos = contentType.find("boundary=");
      if (boundaryPos != std::string::npos) {
        boundaryPos += 9; // length of "boundary="
        size_t endPos = contentType.find_first_of("; \r\n", boundaryPos);
        if (endPos != std::string::npos) {
          boundary = contentType.substr(boundaryPos, endPos - boundaryPos);
        } else {
          boundary = contentType.substr(boundaryPos);
        }
        // Remove quotes if present
        if (!boundary.empty() && boundary.front() == '"' &&
            boundary.back() == '"') {
          boundary = boundary.substr(1, boundary.length() - 2);
        }
      }

      if (boundary.empty()) {
        callback(createErrorResponse(
            400, "Invalid multipart request",
            "Could not find boundary in Content-Type header"));
        return;
      }

      // Get request body
      auto body = req->getBody();
      if (body.empty()) {
        callback(createErrorResponse(
            400, "No file data",
            "Request body is empty. Please include file data."));
        return;
      }

      // Parse multipart body to find all files
      std::string bodyStr(reinterpret_cast<const char *>(body.data()),
                          body.size());
      std::string boundaryMarker = "--" + boundary;
      std::string endBoundary = boundaryMarker + "--";

      // Get directory path from request
      std::string subDir = getDirectoryPath(req);

      // Ensure videos directory exists (with fallback if needed)
      std::string videosDir = getVideosDirectory();
      videosDir = EnvConfig::resolveDirectory(videosDir, "videos");
      std::filesystem::path videosPath(videosDir);

      // Add subdirectory if specified
      if (!subDir.empty()) {
        videosPath /= subDir;
      }

      // CRITICAL: Create directory if it doesn't exist
      try {
        std::filesystem::create_directories(videosPath);
      } catch (const std::filesystem::filesystem_error &e) {
        callback(createErrorResponse(500, "Directory creation failed",
                                     "Could not create videos directory: " +
                                         std::string(e.what())));
        return;
      }

      // Parse all multipart parts
      Json::Value uploadedFiles(Json::arrayValue);
      std::vector<std::string> errors;
      size_t searchPos = 0;
      int fileCount = 0;

      // Find first boundary
      size_t partStart = bodyStr.find(boundaryMarker, searchPos);
      if (partStart == std::string::npos) {
        callback(createErrorResponse(400, "Invalid multipart request",
                                     "Could not find multipart boundary"));
        return;
      }

      // Loop through all parts
      while (partStart != std::string::npos) {
        // Move to start of part content (after boundary)
        partStart += boundaryMarker.length();
        if (partStart >= bodyStr.length())
          break;

        // Skip \r\n after boundary
        if (partStart < bodyStr.length() && bodyStr[partStart] == '\r')
          partStart++;
        if (partStart < bodyStr.length() && bodyStr[partStart] == '\n')
          partStart++;

        // Check if this is the end boundary
        if (bodyStr.substr(partStart,
                           endBoundary.length() - boundaryMarker.length()) ==
            endBoundary.substr(boundaryMarker.length())) {
          break;
        }

        // Find Content-Disposition header
        size_t contentDispositionPos =
            bodyStr.find("Content-Disposition:", partStart);
        if (contentDispositionPos == std::string::npos ||
            contentDispositionPos >
                partStart + 1024) { // Reasonable header limit
          // Move to next boundary
          size_t nextBoundary = bodyStr.find(boundaryMarker, partStart);
          if (nextBoundary == std::string::npos)
            break;
          partStart = nextBoundary;
          continue;
        }

        // Look for filename= in Content-Disposition
        size_t filenamePos = bodyStr.find("filename=", contentDispositionPos);
        if (filenamePos == std::string::npos ||
            filenamePos > contentDispositionPos + 512) {
          // No filename, skip this part
          size_t nextBoundary = bodyStr.find(boundaryMarker, partStart);
          if (nextBoundary == std::string::npos)
            break;
          partStart = nextBoundary;
          continue;
        }

        // Extract filename
        filenamePos += 9; // length of "filename="
        // Skip whitespace and quotes
        while (filenamePos < bodyStr.length() &&
               (bodyStr[filenamePos] == ' ' || bodyStr[filenamePos] == '"')) {
          filenamePos++;
        }

        // Find end of filename
        size_t filenameEnd = filenamePos;
        while (filenameEnd < bodyStr.length() && bodyStr[filenameEnd] != '"' &&
               bodyStr[filenameEnd] != '\r' && bodyStr[filenameEnd] != '\n' &&
               bodyStr[filenameEnd] != ';') {
          filenameEnd++;
        }

        if (filenameEnd <= filenamePos) {
          // Invalid filename, skip
          size_t nextBoundary = bodyStr.find(boundaryMarker, partStart);
          if (nextBoundary == std::string::npos)
            break;
          partStart = nextBoundary;
          continue;
        }

        std::string partFilename =
            bodyStr.substr(filenamePos, filenameEnd - filenamePos);

        // Validate file extension
        if (!isValidVideoFile(partFilename)) {
          errors.push_back("File '" + partFilename + "' has invalid extension");
          size_t nextBoundary = bodyStr.find(boundaryMarker, partStart);
          if (nextBoundary == std::string::npos)
            break;
          partStart = nextBoundary;
          continue;
        }

        // Sanitize filename
        std::string sanitizedPartFilename = sanitizeFilename(partFilename);
        if (sanitizedPartFilename.empty()) {
          errors.push_back("File '" + partFilename +
                           "' contains invalid characters");
          size_t nextBoundary = bodyStr.find(boundaryMarker, partStart);
          if (nextBoundary == std::string::npos)
            break;
          partStart = nextBoundary;
          continue;
        }

        // Find the start of file content (after headers and blank line)
        size_t contentStart = bodyStr.find("\r\n\r\n", contentDispositionPos);
        if (contentStart != std::string::npos) {
          // Found \r\n\r\n, skip all 4 bytes
          contentStart += 4;
        } else {
          // Try \n\n
          contentStart = bodyStr.find("\n\n", contentDispositionPos);
          if (contentStart != std::string::npos) {
            // Found \n\n, skip both bytes
            contentStart += 2;
          } else {
            errors.push_back("Could not find content for file '" +
                             partFilename + "'");
            size_t nextBoundary = bodyStr.find(boundaryMarker, partStart);
            if (nextBoundary == std::string::npos)
              break;
            partStart = nextBoundary;
            continue;
          }
        }

        // Find the end of file content (before next boundary)
        size_t nextBoundary = bodyStr.find(boundaryMarker, contentStart);
        size_t contentEnd = (nextBoundary != std::string::npos)
                                ? nextBoundary
                                : bodyStr.length();

        // Back up to remove trailing \r\n before boundary
        while (contentEnd > contentStart && (bodyStr[contentEnd - 1] == '\r' ||
                                             bodyStr[contentEnd - 1] == '\n')) {
          contentEnd--;
        }

        if (contentEnd <= contentStart) {
          // Skip empty files (e.g., files= field with no value)
          // Only add warning if filename suggests it was intended to be a file
          if (!partFilename.empty() && partFilename != "files" &&
              partFilename != "file") {
            errors.push_back("File '" + partFilename + "' has no content");
          }
          partStart = nextBoundary;
          continue;
        }

        // Create full file path - if file exists, find unique name
        std::filesystem::path partFilePath = videosPath / sanitizedPartFilename;

        // If file already exists, find a unique name by adding number
        std::string finalFilename = sanitizedPartFilename;
        if (std::filesystem::exists(partFilePath)) {
          std::filesystem::path basePath = videosPath / sanitizedPartFilename;
          std::string baseName = basePath.stem().string();
          std::string extension = basePath.extension().string();

          int counter = 1;
          do {
            finalFilename =
                baseName + "_" + std::to_string(counter) + extension;
            partFilePath = videosPath / finalFilename;
            counter++;
          } while (std::filesystem::exists(partFilePath) && counter < 10000);

          if (counter >= 10000) {
            errors.push_back("Cannot find unique filename for '" +
                             partFilename + "'");
            partStart = nextBoundary;
            continue;
          }
        }

        // Save file content using atomic write pattern (write to temp file,
        // then rename) This prevents file corruption if another process reads
        // the file during upload
        std::filesystem::path tempFilePath = partFilePath;
        tempFilePath += ".tmp";

        try {
          // Write to temporary file first
          std::ofstream outFile(tempFilePath, std::ios::binary);
          if (!outFile.is_open()) {
            errors.push_back("Could not create temporary file for '" +
                             finalFilename + "'");
            partStart = nextBoundary;
            continue;
          }

          // Write file content
          size_t writeSize = contentEnd - contentStart;

          // CRITICAL: Validate bounds to prevent buffer overflow
          if (contentStart > body.size() || contentEnd > body.size() ||
              contentStart > contentEnd) {
            outFile.close();
            std::filesystem::remove(tempFilePath);
            errors.push_back("Invalid content boundaries for file '" +
                             partFilename + "'");
            partStart = nextBoundary;
            continue;
          }

          // Ensure writeSize doesn't exceed available data
          if (writeSize > body.size() - contentStart) {
            writeSize = body.size() - contentStart;
          }

          // Write file content
          outFile.write(reinterpret_cast<const char *>(body.data()) +
                            contentStart,
                        writeSize);

          // CRITICAL: Verify bytes written
          std::streampos bytesWritten = outFile.tellp();
          if (bytesWritten == -1 ||
              static_cast<size_t>(bytesWritten) != writeSize) {
            outFile.close();
            std::filesystem::remove(tempFilePath);
            errors.push_back(
                "Failed to write complete file '" + partFilename +
                "': expected " + std::to_string(writeSize) + " bytes, wrote " +
                std::to_string(bytesWritten == -1
                                   ? 0
                                   : static_cast<size_t>(bytesWritten)) +
                " bytes");
            partStart = nextBoundary;
            continue;
          }

          // CRITICAL: Check if write succeeded
          if (!outFile.good()) {
            outFile.close();
            std::filesystem::remove(tempFilePath);
            errors.push_back("Failed to write file '" + partFilename +
                             "': write operation failed");
            partStart = nextBoundary;
            continue;
          }

          // CRITICAL: Flush buffer to ensure data is written
          outFile.flush();
          if (!outFile.good()) {
            outFile.close();
            std::filesystem::remove(tempFilePath);
            errors.push_back("Failed to flush file '" + partFilename +
                             "': flush operation failed");
            partStart = nextBoundary;
            continue;
          }

          // CRITICAL: Sync file system to ensure data is on disk
          // This is especially important for MP4 files where moov atom must be
          // written
          if (outFile.rdbuf() && outFile.rdbuf()->pubsync() != 0) {
            outFile.close();
            std::filesystem::remove(tempFilePath);
            errors.push_back("Failed to sync file '" + partFilename +
                             "': sync operation failed");
            partStart = nextBoundary;
            continue;
          }

          outFile.close();

          // CRITICAL: Atomic rename - ensures file is only visible when
          // complete This prevents reading partial/corrupted files
          try {
            std::filesystem::rename(tempFilePath, partFilePath);
          } catch (const std::filesystem::filesystem_error &e) {
            // Clean up temp file if rename fails
            std::filesystem::remove(tempFilePath);
            errors.push_back("Error finalizing file '" + partFilename +
                             "': " + std::string(e.what()));
            partStart = nextBoundary;
            continue;
          }

          // Get file size
          auto fileSize = std::filesystem::file_size(partFilePath);

          // Add to uploaded files list
          Json::Value fileInfo;
          fileInfo["filename"] = finalFilename;
          fileInfo["originalFilename"] = partFilename;
          fileInfo["path"] = std::filesystem::canonical(partFilePath).string();
          fileInfo["size"] = static_cast<Json::Int64>(fileSize);
          fileInfo["url"] = "/v1/securt/video/" + finalFilename;
          uploadedFiles.append(fileInfo);

          fileCount++;
          std::cerr << "[VideoUploadHandler] Video file uploaded: "
                    << finalFilename << " (" << fileSize << " bytes)"
                    << std::endl;

        } catch (const std::exception &e) {
          errors.push_back("Error saving file '" + partFilename +
                           "': " + std::string(e.what()));
        }

        // Move to next boundary
        partStart = nextBoundary;
      }

      // Build response
      Json::Value response;
      if (fileCount > 0) {
        response["success"] = true;
        response["message"] =
            "Uploaded " + std::to_string(fileCount) + " video file(s)";
        response["count"] = fileCount;
        response["files"] = uploadedFiles;
        if (!errors.empty()) {
          Json::Value errorsJson(Json::arrayValue);
          for (const auto &err : errors) {
            errorsJson.append(err);
          }
          response["warnings"] = errorsJson;
        }

        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k201Created);
        CorsHelper::addAllowAllHeaders(resp);

        MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      } else {
        // No files uploaded successfully
        std::string errorMsg = "No files were uploaded successfully";
        if (!errors.empty()) {
          errorMsg += ". Errors: " + errors[0];
          for (size_t i = 1; i < errors.size() && i < 3; ++i) {
            errorMsg += "; " + errors[i];
          }
        }
        callback(createErrorResponse(400, "Upload failed", errorMsg));
      }
      return;

    } else {
      // Handle application/octet-stream or binary uploads
      // Get filename from Content-Disposition header or query parameter
      std::string contentDisposition = req->getHeader("Content-Disposition");

      // Try to extract filename from Content-Disposition header
      if (!contentDisposition.empty()) {
        size_t filenamePos = contentDisposition.find("filename=");
        if (filenamePos != std::string::npos) {
          filenamePos += 9; // length of "filename="
          if (contentDisposition[filenamePos] == '"') {
            filenamePos++; // skip opening quote
            size_t endPos = contentDisposition.find('"', filenamePos);
            if (endPos != std::string::npos) {
              originalFilename =
                  contentDisposition.substr(filenamePos, endPos - filenamePos);
            }
          } else {
            size_t endPos =
                contentDisposition.find_first_of("; \r\n", filenamePos);
            if (endPos != std::string::npos) {
              originalFilename =
                  contentDisposition.substr(filenamePos, endPos - filenamePos);
            } else {
              originalFilename = contentDisposition.substr(filenamePos);
            }
          }
        }
      }

      // If no filename from header, try query parameter
      if (originalFilename.empty()) {
        originalFilename = req->getParameter("filename");
      }

      // If still no filename, use default
      if (originalFilename.empty()) {
        originalFilename = "uploaded_video.mp4";
      }

      // Validate file extension
      if (!isValidVideoFile(originalFilename)) {
        callback(
            createErrorResponse(400, "Invalid file type",
                                "Only video files (.mp4, .avi, .mkv, .mov, "
                                ".flv, .wmv, .webm, etc.) are allowed"));
        return;
      }

      // Sanitize filename
      sanitizedFilename = sanitizeFilename(originalFilename);
      if (sanitizedFilename.empty()) {
        callback(createErrorResponse(400, "Invalid filename",
                                     "Filename contains invalid characters"));
        return;
      }

      // Get directory path from request
      std::string subDir = getDirectoryPath(req);

      // Ensure videos directory exists (with fallback if needed)
      std::string videosDir = getVideosDirectory();
      videosDir = EnvConfig::resolveDirectory(videosDir, "videos");
      std::filesystem::path videosPath(videosDir);

      // Add subdirectory if specified
      if (!subDir.empty()) {
        videosPath /= subDir;
      }

      // CRITICAL: Create directory if it doesn't exist
      try {
        std::filesystem::create_directories(videosPath);
      } catch (const std::filesystem::filesystem_error &e) {
        callback(createErrorResponse(500, "Directory creation failed",
                                     "Could not create videos directory: " +
                                         std::string(e.what())));
        return;
      }

      // Create full file path - if file exists, find unique name
      filePath = videosPath / sanitizedFilename;

      // If file already exists, find a unique name by adding number
      if (std::filesystem::exists(filePath)) {
        std::filesystem::path basePath = videosPath / sanitizedFilename;
        std::string baseName =
            basePath.stem().string(); // filename without extension
        std::string extension =
            basePath.extension().string(); // .onnx, .pt, etc.

        int counter = 1;
        std::string newFilename;
        do {
          newFilename = baseName + "_" + std::to_string(counter) + extension;
          filePath = videosPath / newFilename;
          counter++;
        } while (std::filesystem::exists(filePath) &&
                 counter < 10000); // Safety limit

        if (counter >= 10000) {
          callback(createErrorResponse(500, "Too many files",
                                       "Cannot find unique filename. Too many "
                                       "files with similar names exist."));
          return;
        }

        sanitizedFilename = newFilename;
        std::cerr
            << "[VideoUploadHandler] File with original name exists, using: "
            << sanitizedFilename << std::endl;
      }

      // Get request body
      auto body = req->getBody();
      if (body.empty()) {
        callback(createErrorResponse(
            400, "No file data",
            "Request body is empty. Please include file data."));
        return;
      }

      // Save file using atomic write pattern (write to temp file, then rename)
      // This prevents file corruption if another process reads the file during
      // upload
      std::filesystem::path tempFilePath = filePath;
      tempFilePath += ".tmp";

      std::ofstream outFile(tempFilePath, std::ios::binary);
      if (!outFile.is_open()) {
        callback(
            createErrorResponse(500, "File save failed",
                                "Could not create temporary file for upload"));
        return;
      }

      // Write file content from body
      size_t writeSize = body.size();

      // CRITICAL: Validate body is not empty
      if (writeSize == 0) {
        outFile.close();
        std::filesystem::remove(tempFilePath);
        callback(createErrorResponse(400, "Empty file",
                                     "Uploaded file has no content"));
        return;
      }

      // Write file content
      outFile.write(reinterpret_cast<const char *>(body.data()), writeSize);

      // CRITICAL: Verify bytes written
      std::streampos bytesWritten = outFile.tellp();
      if (bytesWritten == -1 ||
          static_cast<size_t>(bytesWritten) != writeSize) {
        outFile.close();
        std::filesystem::remove(tempFilePath);
        callback(createErrorResponse(
            500, "File save failed",
            "Failed to write complete file data: expected " +
                std::to_string(writeSize) + " bytes, wrote " +
                std::to_string(bytesWritten == -1
                                   ? 0
                                   : static_cast<size_t>(bytesWritten)) +
                " bytes"));
        return;
      }

      // CRITICAL: Check if write succeeded
      if (!outFile.good()) {
        outFile.close();
        std::filesystem::remove(tempFilePath);
        callback(createErrorResponse(
            500, "File save failed",
            "Failed to write file data: write operation failed"));
        return;
      }

      // CRITICAL: Flush buffer to ensure data is written
      outFile.flush();
      if (!outFile.good()) {
        outFile.close();
        std::filesystem::remove(tempFilePath);
        callback(createErrorResponse(
            500, "File save failed",
            "Failed to flush file data: flush operation failed"));
        return;
      }

      // CRITICAL: Sync file system to ensure data is on disk
      // This is especially important for MP4 files where moov atom must be
      // written
      if (outFile.rdbuf() && outFile.rdbuf()->pubsync() != 0) {
        outFile.close();
        std::filesystem::remove(tempFilePath);
        callback(createErrorResponse(
            500, "File save failed",
            "Failed to sync file data to disk: sync operation failed"));
        return;
      }

      outFile.close();

      // CRITICAL: Atomic rename - ensures file is only visible when complete
      // This prevents reading partial/corrupted files
      try {
        std::filesystem::rename(tempFilePath, filePath);
      } catch (const std::filesystem::filesystem_error &e) {
        // Clean up temp file if rename fails
        std::filesystem::remove(tempFilePath);
        callback(createErrorResponse(500, "File save failed",
                                     "Could not finalize uploaded file: " +
                                         std::string(e.what())));
        return;
      }
    }

    // Get file size with proper error handling
    Json::Int64 fileSize = 0;
    std::string canonicalPath;

    try {
      // ✅ Check if file exists first
      if (!std::filesystem::exists(filePath)) {
        callback(createErrorResponse(
            500, "File not found",
            "Uploaded file was not found after write operation"));
        return;
      }

      // ✅ Check if it's a regular file
      if (!std::filesystem::is_regular_file(filePath)) {
        callback(createErrorResponse(500, "Invalid file",
                                     "Uploaded path is not a regular file"));
        return;
      }

      // ✅ Get file size with exception handling
      try {
        fileSize =
            static_cast<Json::Int64>(std::filesystem::file_size(filePath));
      } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "[VideoUploadHandler] Error getting file size: "
                  << e.what() << std::endl;
        callback(createErrorResponse(500, "File size error",
                                     "Could not determine file size: " +
                                         std::string(e.what())));
        return;
      }

      // ✅ Get canonical path with exception handling
      try {
        canonicalPath = std::filesystem::canonical(filePath).string();
      } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "[VideoUploadHandler] Error getting canonical path: "
                  << e.what() << std::endl;
        // Use original path as fallback
        canonicalPath = filePath;
      }

    } catch (const std::exception &e) {
      std::cerr << "[VideoUploadHandler] Unexpected error: " << e.what()
                << std::endl;
      callback(createErrorResponse(
          500, "Internal error", "Unexpected error processing uploaded file"));
      return;
    }

    // Build response
    Json::Value response;
    response["success"] = true;
    response["message"] = "Video file uploaded successfully";
    response["filename"] = sanitizedFilename;
    response["originalFilename"] = originalFilename;
    response["path"] = canonicalPath;
    response["size"] = fileSize;
    response["url"] = "/v1/securt/video/" + sanitizedFilename;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    // Add CORS headers - ALLOW ALL
    CorsHelper::addAllowAllHeaders(resp);

    std::cerr << "[VideoUploadHandler] Video file uploaded: "
              << sanitizedFilename << " (" << fileSize << " bytes)"
              << std::endl;

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    std::cerr << "[VideoUploadHandler] Exception: " << e.what() << std::endl;
    auto errorResp =
        createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, errorResp, std::move(callback));
  } catch (...) {
    std::cerr << "[VideoUploadHandler] Unknown exception" << std::endl;
    auto errorResp = createErrorResponse(500, "Internal server error",
                                         "Unknown error occurred");
    MetricsInterceptor::callWithMetrics(req, errorResp, std::move(callback));
  }
}

void VideoUploadHandler::listVideos(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    // Get directory path from request
    std::string subDir = getDirectoryPath(req);

    std::string videosDir = getVideosDirectory();
    videosDir = EnvConfig::resolveDirectory(videosDir, "videos");
    std::filesystem::path videosPath(videosDir);

    // Add subdirectory if specified
    if (!subDir.empty()) {
      videosPath /= subDir;
    }

    Json::Value response;
    Json::Value videos(Json::arrayValue);

    if (!std::filesystem::exists(videosPath)) {
      // Return empty list if directory doesn't exist
      response["success"] = true;
      response["videos"] = videos;
      response["count"] = 0;

      auto resp = HttpResponse::newHttpJsonResponse(response);
      resp->setStatusCode(k200OK);
      CorsHelper::addAllowAllHeaders(resp);
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // List all video files
    // If no directory specified, recursively search all subdirectories
    // If directory specified, only search that directory (non-recursive)
    int count = 0;

    if (subDir.empty()) {
      // Recursive search: find all video files in all subdirectories
      try {
        for (const auto &entry :
             std::filesystem::recursive_directory_iterator(videosPath)) {
          try {
            if (entry.is_regular_file() &&
                isValidVideoFile(entry.path().filename().string())) {
              Json::Value video;
              video["filename"] = entry.path().filename().string();

              // Get relative path from base videos directory
              try {
                std::filesystem::path relativePath =
                    std::filesystem::relative(entry.path(), videosPath);
                video["relativePath"] = relativePath.string();
              } catch (const std::exception &e) {
                // If relative path fails, use filename only
                video["relativePath"] = entry.path().filename().string();
              }

              video["path"] = std::filesystem::canonical(entry.path()).string();
              video["size"] = static_cast<Json::Int64>(
                  std::filesystem::file_size(entry.path()));

              auto modTime = std::filesystem::last_write_time(entry.path());
              auto sctp = std::chrono::time_point_cast<
                  std::chrono::system_clock::duration>(
                  modTime - std::filesystem::file_time_type::clock::now() +
                  std::chrono::system_clock::now());
              auto timeT = std::chrono::system_clock::to_time_t(sctp);

              std::stringstream ss;
              ss << std::put_time(std::localtime(&timeT), "%Y-%m-%d %H:%M:%S");
              video["modified"] = ss.str();

              videos.append(video);
              count++;
            }
          } catch (const std::filesystem::filesystem_error &e) {
            // Skip files/directories that can't be accessed (permission denied,
            // etc.)
            std::cerr << "[VideoUploadHandler] Skipping entry due to error: "
                      << e.what() << std::endl;
            continue;
          }
        }
      } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "[VideoUploadHandler] Error during recursive search: "
                  << e.what() << std::endl;
        // Continue with empty list or partial results
      }
    } else {
      // Non-recursive: only search in specified directory
      for (const auto &entry :
           std::filesystem::directory_iterator(videosPath)) {
        if (entry.is_regular_file() &&
            isValidVideoFile(entry.path().filename().string())) {
          Json::Value video;
          video["filename"] = entry.path().filename().string();
          video["path"] = std::filesystem::canonical(entry.path()).string();
          video["size"] = static_cast<Json::Int64>(
              std::filesystem::file_size(entry.path()));

          auto modTime = std::filesystem::last_write_time(entry.path());
          auto sctp =
              std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                  modTime - std::filesystem::file_time_type::clock::now() +
                  std::chrono::system_clock::now());
          auto timeT = std::chrono::system_clock::to_time_t(sctp);

          std::stringstream ss;
          ss << std::put_time(std::localtime(&timeT), "%Y-%m-%d %H:%M:%S");
          video["modified"] = ss.str();

          videos.append(video);
          count++;
        }
      }
    }

    response["success"] = true;
    response["videos"] = videos;
    response["count"] = count;
    response["directory"] = std::filesystem::canonical(videosPath).string();

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    CorsHelper::addAllowAllHeaders(resp);

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    std::cerr << "[VideoUploadHandler] Exception: " << e.what() << std::endl;
    auto errorResp =
        createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, errorResp, std::move(callback));
  } catch (...) {
    std::cerr << "[VideoUploadHandler] Unknown exception" << std::endl;
    auto errorResp = createErrorResponse(500, "Internal server error",
                                         "Unknown error occurred");
    MetricsInterceptor::callWithMetrics(req, errorResp, std::move(callback));
  }
}

void VideoUploadHandler::renameVideo(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  try {
    // Get video name from path parameter
    std::string videoName = extractVideoName(req);
    if (videoName.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Video name is required"));
      return;
    }

    // Sanitize source filename
    std::string sanitizedSourceName = sanitizeFilename(videoName);
    if (sanitizedSourceName.empty() || sanitizedSourceName != videoName) {
      callback(createErrorResponse(400, "Invalid filename",
                                   "Invalid source video name"));
      return;
    }

    // Parse request body to get new name
    Json::Value requestJson;
    try {
      auto jsonPtr = req->getJsonObject();
      if (jsonPtr) {
        requestJson = *jsonPtr;
      } else {
        // Try to parse body as JSON
        auto body = req->getBody();
        if (!body.empty()) {
          Json::Reader reader;
          std::string bodyStr(reinterpret_cast<const char *>(body.data()),
                              body.size());
          if (!reader.parse(bodyStr, requestJson)) {
            callback(createErrorResponse(400, "Invalid JSON",
                                         "Request body must be valid JSON"));
            return;
          }
        } else {
          callback(createErrorResponse(400, "Missing parameter",
                                       "New name is required in request body"));
          return;
        }
      }
    } catch (const std::exception &e) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Could not parse request body: " +
                                       std::string(e.what())));
      return;
    }

    // Get new name from JSON
    if (!requestJson.isMember("newName") ||
        !requestJson["newName"].isString()) {
      callback(
          createErrorResponse(400, "Missing parameter",
                              "newName field is required in request body"));
      return;
    }

    std::string newName = requestJson["newName"].asString();
    if (newName.empty()) {
      callback(createErrorResponse(400, "Invalid parameter",
                                   "newName cannot be empty"));
      return;
    }

    // Validate new name file extension
    if (!isValidVideoFile(newName)) {
      callback(createErrorResponse(400, "Invalid file type",
                                   "New name must have a valid video file "
                                   "extension (.mp4, .avi, .mkv, .mov, etc.)"));
      return;
    }

    // Sanitize new filename
    std::string sanitizedNewName = sanitizeFilename(newName);
    if (sanitizedNewName.empty() || sanitizedNewName != newName) {
      callback(createErrorResponse(400, "Invalid filename",
                                   "New name contains invalid characters"));
      return;
    }

    // Check if source and destination are the same
    if (sanitizedSourceName == sanitizedNewName) {
      callback(createErrorResponse(400, "Invalid request",
                                   "New name is the same as current name"));
      return;
    }

    // Get directory path from request
    std::string subDir = getDirectoryPath(req);

    // Build file paths
    std::string videosDir = getVideosDirectory();
    videosDir = EnvConfig::resolveDirectory(videosDir, "videos");
    std::filesystem::path videosPath(videosDir);

    // Add subdirectory if specified
    if (!subDir.empty()) {
      videosPath /= subDir;
    }

    std::filesystem::path sourcePath = videosPath / sanitizedSourceName;
    std::filesystem::path destPath = videosPath / sanitizedNewName;

    // Check if source file exists
    if (!std::filesystem::exists(sourcePath)) {
      callback(
          createErrorResponse(404, "Not found", "Source video file not found"));
      return;
    }

    // Check if destination file already exists
    if (std::filesystem::exists(destPath)) {
      callback(createErrorResponse(
          409, "File exists",
          "A video file with the new name already exists. Please delete it "
          "first or use a different name."));
      return;
    }

    // Rename file
    try {
      std::filesystem::rename(sourcePath, destPath);
    } catch (const std::filesystem::filesystem_error &e) {
      callback(createErrorResponse(500, "Rename failed",
                                   "Could not rename video file: " +
                                       std::string(e.what())));
      return;
    }

    Json::Value response;
    response["success"] = true;
    response["message"] = "Video file renamed successfully";
    response["oldName"] = sanitizedSourceName;
    response["newName"] = sanitizedNewName;
    response["path"] = std::filesystem::canonical(destPath).string();

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    CorsHelper::addAllowAllHeaders(resp);

    std::cerr << "[VideoUploadHandler] Video file renamed: "
              << sanitizedSourceName << " -> " << sanitizedNewName << std::endl;

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    std::cerr << "[VideoUploadHandler] Exception: " << e.what() << std::endl;
    auto errorResp =
        createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, errorResp, std::move(callback));
  } catch (...) {
    std::cerr << "[VideoUploadHandler] Unknown exception" << std::endl;
    auto errorResp = createErrorResponse(500, "Internal server error",
                                         "Unknown error occurred");
    MetricsInterceptor::callWithMetrics(req, errorResp, std::move(callback));
  }
}

void VideoUploadHandler::deleteVideo(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  try {
    // Get video name from path parameter
    std::string videoName = extractVideoName(req);
    if (videoName.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Video name is required"));
      return;
    }

    // Sanitize filename
    std::string sanitizedFilename = sanitizeFilename(videoName);
    if (sanitizedFilename.empty() || sanitizedFilename != videoName) {
      callback(
          createErrorResponse(400, "Invalid filename", "Invalid video name"));
      return;
    }

    // Get directory path from request
    std::string subDir = getDirectoryPath(req);

    // Build file path
    std::string videosDir = getVideosDirectory();
    videosDir = EnvConfig::resolveDirectory(videosDir, "videos");
    std::filesystem::path videosPath(videosDir);

    // Add subdirectory if specified
    if (!subDir.empty()) {
      videosPath /= subDir;
    }

    std::filesystem::path filePath = videosPath / sanitizedFilename;

    // Check if file exists
    if (!std::filesystem::exists(filePath)) {
      callback(createErrorResponse(404, "Not found", "Video file not found"));
      return;
    }

    // Delete file
    if (!std::filesystem::remove(filePath)) {
      callback(createErrorResponse(500, "Delete failed",
                                   "Could not delete video file"));
      return;
    }

    Json::Value response;
    response["success"] = true;
    response["message"] = "Video file deleted successfully";
    response["filename"] = sanitizedFilename;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    CorsHelper::addAllowAllHeaders(resp);

    std::cerr << "[VideoUploadHandler] Video file deleted: "
              << sanitizedFilename << std::endl;

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    std::cerr << "[VideoUploadHandler] Exception: " << e.what() << std::endl;
    auto errorResp =
        createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, errorResp, std::move(callback));
  } catch (...) {
    std::cerr << "[VideoUploadHandler] Unknown exception" << std::endl;
    auto errorResp = createErrorResponse(500, "Internal server error",
                                         "Unknown error occurred");
    MetricsInterceptor::callWithMetrics(req, errorResp, std::move(callback));
  }
}

void VideoUploadHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  // Debug log to verify handler is called
  std::cerr << "[VideoUploadHandler] OPTIONS handler called!" << std::endl;
  std::cerr << "[VideoUploadHandler] Path: " << req->path() << std::endl;
  std::cerr << "[VideoUploadHandler] Origin: " << req->getHeader("Origin")
            << std::endl;

  // Use CORS helper to create "allow all" response
  auto resp = CorsHelper::createOptionsResponse();

  std::cerr
      << "[VideoUploadHandler] OPTIONS response sent with ALLOW ALL headers"
      << std::endl;

  // Record metrics and call callback
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

HttpResponsePtr
VideoUploadHandler::createErrorResponse(int statusCode,
                                        const std::string &error,
                                        const std::string &message) const {

  Json::Value errorJson;
  errorJson["success"] = false;
  errorJson["error"] = error;
  if (!message.empty()) {
    errorJson["message"] = message;
  }

  auto resp = HttpResponse::newHttpJsonResponse(errorJson);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));

  // Add CORS headers to error responses - ALLOW ALL
  CorsHelper::addAllowAllHeaders(resp);

  return resp;
}
