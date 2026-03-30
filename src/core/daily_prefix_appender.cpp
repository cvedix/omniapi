#include "core/daily_prefix_appender.h"
#include "core/log_disk_guard.h"
#include <cstring>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

DailyPrefixAppender::DailyPrefixAppender(std::string category_dir, Mode mode,
                                         size_t max_file_bytes)
    : dir_(std::move(category_dir)), mode_(mode),
      max_bytes_(std::max(size_t(1024 * 1024), max_file_bytes)) {
  while (!dir_.empty() && dir_.back() == '/') {
    dir_.pop_back();
  }
}

bool DailyPrefixAppender::matchesMode(const char *msg) const {
  if (!msg) {
    msg = "";
  }
  const bool has_api = std::strstr(msg, "[API]") != nullptr;
  const bool has_inst = std::strstr(msg, "[Instance]") != nullptr;
  const bool has_sdk = std::strstr(msg, "[SDKOutput]") != nullptr;
  switch (mode_) {
  case Mode::ApiLines:
    return has_api;
  case Mode::SdkLines:
    return has_sdk;
  case Mode::GeneralLines:
    return !has_api && !has_inst && !has_sdk;
  }
  return false;
}

std::string DailyPrefixAppender::buildPath(const std::string &date,
                                           int part) const {
  if (part <= 0) {
    return dir_ + "/" + date + ".log";
  }
  return dir_ + "/" + date + "." + std::to_string(part) + ".log";
}

void DailyPrefixAppender::write(const plog::Record &record) {
  LogDiskGuard::checkNow();
  if (LogDiskGuard::isSuspended()) {
    return;
  }
  if (!matchesMode(record.getMessage())) {
    return;
  }

  std::string line = plog::TxtFormatter::format(record);
  if (line.empty() || line.back() != '\n') {
    line.push_back('\n');
  }

  std::lock_guard<std::mutex> lock(mu_);

  std::time_t tt = std::time(nullptr);
  std::tm tm {};
  localtime_r(&tt, &tm);
  char date_buf[16];
  std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm);
  std::string today(date_buf);

  if (open_date_ != today) {
    if (out_.is_open()) {
      out_.close();
    }
    open_date_ = today;
    part_index_ = 0;
    bytes_in_part_ = 0;
  }

  std::error_code ec;
  fs::create_directories(dir_, ec);

  for (int guard = 0; guard < 10000; ++guard) {
    if (!out_.is_open()) {
      std::string path = buildPath(open_date_, part_index_);
      out_.open(path, std::ios::app | std::ios::out);
      if (!out_.is_open()) {
        return;
      }
      bytes_in_part_ = 0;
      if (fs::exists(path, ec)) {
        try {
          bytes_in_part_ = static_cast<size_t>(fs::file_size(path));
        } catch (...) {
          bytes_in_part_ = 0;
        }
      }
    }

    if (bytes_in_part_ >= max_bytes_) {
      out_.close();
      part_index_++;
      bytes_in_part_ = 0;
      continue;
    }

    if (bytes_in_part_ + line.size() > max_bytes_ && bytes_in_part_ > 0) {
      out_.close();
      part_index_++;
      bytes_in_part_ = 0;
      continue;
    }

    out_ << line;
    out_.flush();
    bytes_in_part_ += line.size();
    return;
  }
}
