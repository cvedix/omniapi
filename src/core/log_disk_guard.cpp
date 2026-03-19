#include "core/log_disk_guard.h"
#include <algorithm>
#include <iostream>
#include <sys/statvfs.h>

namespace {
constexpr auto kMinInterval = std::chrono::seconds(15);
}

std::mutex LogDiskGuard::mutex_;
std::string LogDiskGuard::path_;
int LogDiskGuard::suspend_pct_ = 95;
int LogDiskGuard::resume_pct_ = 90;
std::atomic<bool> LogDiskGuard::suspended_{false};
std::chrono::steady_clock::time_point LogDiskGuard::last_check_{};

void LogDiskGuard::configure(const std::string &path_to_check,
                             int suspend_percent, int resume_percent) {
  std::lock_guard<std::mutex> lock(mutex_);
  path_ = path_to_check;
  suspend_pct_ = std::clamp(suspend_percent, 50, 99);
  resume_pct_ = std::clamp(resume_percent, 40, suspend_pct_ - 1);
}

void LogDiskGuard::checkNow() {
  std::lock_guard<std::mutex> lock(mutex_);
  auto now = std::chrono::steady_clock::now();
  if (path_.empty()) {
    return;
  }
  if (now - last_check_ < kMinInterval) {
    return;
  }
  last_check_ = now;

  struct statvfs st {};
  if (statvfs(path_.c_str(), &st) != 0) {
    return;
  }
  uint64_t total = st.f_blocks * st.f_frsize;
  if (total == 0) {
    return;
  }
  uint64_t avail = st.f_bavail * st.f_frsize;
  double used_pct = (1.0 - static_cast<double>(avail) / total) * 100.0;

  if (used_pct >= static_cast<double>(suspend_pct_)) {
    if (!suspended_.exchange(true)) {
      std::cerr << "[LogDiskGuard] Disk usage " << static_cast<int>(used_pct)
                << "% >= suspend threshold " << suspend_pct_
                << "% — file logging suspended until space recovers."
                << std::endl;
    }
  } else if (used_pct <= static_cast<double>(resume_pct_)) {
    if (suspended_.exchange(false)) {
      std::cerr << "[LogDiskGuard] Disk usage " << static_cast<int>(used_pct)
                << "% <= resume threshold " << resume_pct_
                << "% — file logging resumed." << std::endl;
    }
  }
}

bool LogDiskGuard::isSuspended() { return suspended_.load(); }
