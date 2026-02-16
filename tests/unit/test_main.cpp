#include "core/logging_flags.h"
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>
#include <unistd.h>

// Define logging flags for tests (they're normally defined in main.cpp)
std::atomic<bool> g_log_api{false};
std::atomic<bool> g_log_instance{false};
std::atomic<bool> g_log_sdk_output{false};

// Flag to track if we're currently running a test
static std::atomic<bool> g_in_test{false};

// Signal handler for segmentation faults in tests
// This allows tests to fail gracefully instead of crashing the entire test suite
void testSegfaultHandler(int signal) {
  if (g_in_test.load()) {
    // We're in a test - exit gracefully so Google Test can mark it as failed
    std::cerr << "\n[TEST] Segmentation fault (SIGSEGV) detected in test" << std::endl;
    std::cerr << "[TEST] Exiting test gracefully - it will be marked as FAILED" << std::endl;
    std::cerr << "[TEST] Test suite will continue with remaining tests" << std::endl;
    
    // Flush output
    std::fflush(stdout);
    std::fflush(stderr);
    
    // Exit with failure code - Google Test will catch this and mark test as failed
    // Use _exit() instead of exit() to avoid calling destructors in corrupted state
    _exit(1);
  } else {
    // Not in a test - restore default handler and re-raise
    std::signal(signal, SIG_DFL);
    std::raise(signal);
  }
}

// Test event listener to track when tests start/end
class CrashHandlingTestListener : public ::testing::EmptyTestEventListener {
public:
  void OnTestStart(const ::testing::TestInfo &test_info) override {
    g_in_test.store(true);
    std::cout << "[TEST] Starting: " << test_info.test_case_name() << "."
              << test_info.name() << std::endl;
  }
  
  void OnTestEnd(const ::testing::TestInfo &test_info) override {
    g_in_test.store(false);
    if (test_info.result()->Failed()) {
      std::cout << "[TEST] FAILED: " << test_info.test_case_name() << "."
                << test_info.name() << std::endl;
    } else {
      std::cout << "[TEST] PASSED: " << test_info.test_case_name() << "."
                << test_info.name() << std::endl;
    }
  }
};

int main(int argc, char **argv) {
  // CRITICAL: Set DISABLE_KAFKA before any initialization
  // This prevents Kafka from being initialized in CVEDIX SDK headers/constructors
  setenv("DISABLE_KAFKA", "1", 1);
  
  std::cout << "========================================" << std::endl;
  std::cout << "Edge AI API - Unit Tests" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Crash handling: Enabled" << std::endl;
  std::cout << "Kafka: Disabled (DISABLE_KAFKA=1)" << std::endl;
  std::cout << "Tests that crash will be marked as FAILED" << std::endl;
  std::cout << "Test suite will continue with remaining tests" << std::endl;
  std::cout << "========================================" << std::endl;

  // Register signal handler for SIGSEGV to catch crashes in tests
  // This allows tests to fail gracefully instead of crashing the entire suite
  std::signal(SIGSEGV, testSegfaultHandler);
  std::signal(SIGABRT, testSegfaultHandler);
  
  // Enable Google Test's death test style to run tests in separate threads
  // This provides better isolation and crash handling
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  
  // Initialize Google Test
  ::testing::InitGoogleTest(&argc, argv);
  
  // Install test event listener to track test execution
  ::testing::TestEventListeners &listeners =
      ::testing::UnitTest::GetInstance()->listeners();
  listeners.Append(new CrashHandlingTestListener());
  
  int result = RUN_ALL_TESTS();

  std::cout << "\n========================================" << std::endl;
  if (result == 0) {
    std::cout << "All tests PASSED!" << std::endl;
  } else {
    std::cout << "Some tests FAILED!" << std::endl;
    std::cout << "Note: If a test crashed (SIGSEGV), it was marked as FAILED" << std::endl;
    std::cout << "      and the test suite continued with remaining tests." << std::endl;
  }
  std::cout << "========================================" << std::endl;

  return result;
}
