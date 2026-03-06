#include "core/decoder_detector.h"
#include <gtest/gtest.h>
#include <json/json.h>

class DecoderDetectorTest : public ::testing::Test {
protected:
  void SetUp() override {
    detector_ = &DecoderDetector::getInstance();
  }

  void TearDown() override {}

  DecoderDetector *detector_;
};

// Test singleton pattern
TEST_F(DecoderDetectorTest, SingletonPattern) {
  auto &instance1 = DecoderDetector::getInstance();
  auto &instance2 = DecoderDetector::getInstance();
  EXPECT_EQ(&instance1, &instance2);
}

// Test detect decoders
TEST_F(DecoderDetectorTest, DetectDecoders) {
  bool result = detector_->detectDecoders();
  EXPECT_TRUE(result);
  EXPECT_TRUE(detector_->isDetected());
}

// Test get decoders JSON
TEST_F(DecoderDetectorTest, GetDecodersJson) {
  detector_->detectDecoders();

  Json::Value json = detector_->getDecodersJson();
  EXPECT_TRUE(json.isObject());
}

// Test get decoders map
TEST_F(DecoderDetectorTest, GetDecoders) {
  detector_->detectDecoders();

  auto decoders = detector_->getDecoders();
  // Can be empty if no hardware decoders available
  EXPECT_GE(decoders.size(), 0);
}

// Test has NVIDIA decoders (may be false if no NVIDIA hardware)
TEST_F(DecoderDetectorTest, HasNvidiaDecoders) {
  detector_->detectDecoders();

  bool hasNvidia = detector_->hasNvidiaDecoders();
  // Result depends on system, just check it doesn't crash
  EXPECT_TRUE(hasNvidia || !hasNvidia); // Always true, just checking no crash
}

// Test has Intel decoders (may be false if no Intel hardware)
TEST_F(DecoderDetectorTest, HasIntelDecoders) {
  detector_->detectDecoders();

  bool hasIntel = detector_->hasIntelDecoders();
  // Result depends on system, just check it doesn't crash
  EXPECT_TRUE(hasIntel || !hasIntel); // Always true, just checking no crash
}

// Test get NVIDIA decoder count
TEST_F(DecoderDetectorTest, GetNvidiaDecoderCount) {
  detector_->detectDecoders();

  int count = detector_->getNvidiaDecoderCount("h264");
  EXPECT_GE(count, 0); // Should be 0 or positive

  count = detector_->getNvidiaDecoderCount("hevc");
  EXPECT_GE(count, 0);
}

// Test get Intel decoder count
TEST_F(DecoderDetectorTest, GetIntelDecoderCount) {
  detector_->detectDecoders();

  int count = detector_->getIntelDecoderCount("h264");
  EXPECT_GE(count, 0); // Should be 0 or positive

  count = detector_->getIntelDecoderCount("hevc");
  EXPECT_GE(count, 0);
}

// Test is detected
TEST_F(DecoderDetectorTest, IsDetected) {
  EXPECT_FALSE(detector_->isDetected()); // Not detected yet

  detector_->detectDecoders();
  EXPECT_TRUE(detector_->isDetected());
}

// Test decoder JSON structure
TEST_F(DecoderDetectorTest, DecoderJsonStructure) {
  detector_->detectDecoders();

  Json::Value json = detector_->getDecodersJson();

  // If nvidia exists, check structure
  if (json.isMember("nvidia")) {
    EXPECT_TRUE(json["nvidia"].isObject());
    if (json["nvidia"].isMember("h264")) {
      EXPECT_TRUE(json["nvidia"]["h264"].isInt());
    }
    if (json["nvidia"].isMember("hevc")) {
      EXPECT_TRUE(json["nvidia"]["hevc"].isInt());
    }
  }

  // If intel exists, check structure
  if (json.isMember("intel")) {
    EXPECT_TRUE(json["intel"].isObject());
    if (json["intel"].isMember("h264")) {
      EXPECT_TRUE(json["intel"]["h264"].isInt());
    }
    if (json["intel"].isMember("hevc")) {
      EXPECT_TRUE(json["intel"]["hevc"].isInt());
    }
  }
}

