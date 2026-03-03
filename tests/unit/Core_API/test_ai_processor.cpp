#include "core/ai_processor.h"
#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <chrono>
#include <thread>

class AIProcessorTest : public ::testing::Test {
 protected:
  void TearDown() override {
    if (processor_ && processor_->isRunning()) processor_->stop(true);
  }
  std::unique_ptr<AIProcessor> processor_;
};

TEST_F(AIProcessorTest, StartWithEmptyConfig) {
  processor_ = std::make_unique<AIProcessor>();
  EXPECT_TRUE(processor_->start("", nullptr));
  EXPECT_TRUE(processor_->isRunning());
  processor_->stop(true);
  EXPECT_FALSE(processor_->isRunning());
}

TEST_F(AIProcessorTest, SubmitFrameWhenNotLoaded) {
  processor_ = std::make_unique<AIProcessor>();
  processor_->start("", nullptr);
  cv::Mat frame(100, 100, CV_8UC3);
  processor_->submitFrame(frame);
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  processor_->stop(true);
}

TEST_F(AIProcessorTest, GetMetrics) {
  processor_ = std::make_unique<AIProcessor>();
  processor_->start("", nullptr);
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  auto m = processor_->getMetrics();
  EXPECT_EQ(m.status, AIProcessor::Status::Running);
  processor_->stop(true);
  EXPECT_EQ(processor_->getMetrics().status, AIProcessor::Status::Stopped);
}

TEST_F(AIProcessorTest, DoubleStartRejected) {
  processor_ = std::make_unique<AIProcessor>();
  EXPECT_TRUE(processor_->start("", nullptr));
  EXPECT_FALSE(processor_->start("", nullptr));
  processor_->stop(true);
}
