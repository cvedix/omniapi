#include "core/inference_session.h"
#include <gtest/gtest.h>
#include <opencv2/core.hpp>

namespace {

class InferenceSessionTest : public ::testing::Test {
protected:
  void SetUp() override { session_ = std::make_unique<core::InferenceSession>(); }
  void TearDown() override { session_.reset(); }

  std::unique_ptr<core::InferenceSession> session_;
};

// Not loaded by default
TEST_F(InferenceSessionTest, IsLoadedFalseByDefault) {
  EXPECT_FALSE(session_->isLoaded());
}

// load with empty detector path fails
TEST_F(InferenceSessionTest, LoadEmptyDetectorPathFails) {
  EXPECT_FALSE(session_->load("", ""));
  EXPECT_FALSE(session_->load("", "/some/recognizer.onnx"));
  EXPECT_FALSE(session_->isLoaded());
}

// unload when not loaded is safe (idempotent)
TEST_F(InferenceSessionTest, UnloadWhenNotLoadedIsSafe) {
  session_->unload();
  session_->unload();
  EXPECT_FALSE(session_->isLoaded());
}

// infer without load returns error
TEST_F(InferenceSessionTest, InferWhenNotLoadedReturnsError) {
  cv::Mat frame(100, 100, CV_8UC3);
  frame.setTo(cv::Scalar(128, 128, 128));

  core::InferenceInput input;
  input.frame = frame;
  input.model_id = "face";

  core::InferenceResult result = session_->infer(input);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error.empty());
  EXPECT_TRUE(result.data.isNull() || !result.data.isMember("faces"));
}

// infer with empty frame returns error
TEST_F(InferenceSessionTest, InferEmptyFrameReturnsError) {
  core::InferenceInput input;
  input.frame = cv::Mat();
  input.model_id = "face";

  core::InferenceResult result = session_->infer(input);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error.empty());
}

// infer with unsupported model_id returns error
TEST_F(InferenceSessionTest, InferUnsupportedModelIdReturnsError) {
  cv::Mat frame(100, 100, CV_8UC3);
  frame.setTo(cv::Scalar(128, 128, 128));

  core::InferenceInput input;
  input.frame = frame;
  input.model_id = "unknown_model";

  core::InferenceResult result = session_->infer(input);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error.empty());
  EXPECT_TRUE(result.error.find("Unsupported model_id") != std::string::npos);
}

// getDetectorPath / getRecognizerPath return empty when not loaded
TEST_F(InferenceSessionTest, GetPathsEmptyWhenNotLoaded) {
  EXPECT_TRUE(session_->getDetectorPath().empty());
  EXPECT_TRUE(session_->getRecognizerPath().empty());
}

// load with non-existent detector path fails (no crash)
TEST_F(InferenceSessionTest, LoadNonExistentDetectorFails) {
  bool ok = session_->load("/nonexistent/detector.onnx", "");
  EXPECT_FALSE(ok);
  EXPECT_FALSE(session_->isLoaded());
}

}  // namespace
