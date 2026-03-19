#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

class LogDiskGuard {
public:
  static void configure(const std::string &path_to_check, int suspend_percent,
                        int resume_percent);
  static void checkNow();
  static bool isSuspended();

private:
  static std::mutex mutex_;
  static std::string path_;
  static int suspend_pct_;
  static int resume_pct_;
  static std::atomic<bool> suspended_;
  static std::chrono::steady_clock::time_point last_check_;
};
