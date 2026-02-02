/**
 * @file velocity_helper_test.cpp
 * @brief Tests for velocity helper utilities and rng_util wrappers.
 */

#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "core/rng_util.h"
#include "core/velocity_helper.h"

using namespace midisketch;

// ============================================================================
// vel::clamp(int) Tests
// ============================================================================

TEST(VelocityHelperTest, ClampInt_WithinRange) {
  EXPECT_EQ(vel::clamp(64), 64);
  EXPECT_EQ(vel::clamp(1), 1);
  EXPECT_EQ(vel::clamp(127), 127);
}

TEST(VelocityHelperTest, ClampInt_BelowMin) {
  EXPECT_EQ(vel::clamp(0), 1);
  EXPECT_EQ(vel::clamp(-50), 1);
}

TEST(VelocityHelperTest, ClampInt_AboveMax) {
  EXPECT_EQ(vel::clamp(128), 127);
  EXPECT_EQ(vel::clamp(255), 127);
}

// ============================================================================
// vel::clamp(float) Tests
// ============================================================================

TEST(VelocityHelperTest, ClampFloat_WithinRange) {
  EXPECT_EQ(vel::clamp(64.5f), 64);
  EXPECT_EQ(vel::clamp(100.9f), 100);
}

TEST(VelocityHelperTest, ClampFloat_BelowMin) {
  EXPECT_EQ(vel::clamp(0.5f), 1);
  EXPECT_EQ(vel::clamp(-10.0f), 1);
}

TEST(VelocityHelperTest, ClampFloat_AboveMax) {
  EXPECT_EQ(vel::clamp(200.0f), 127);
}

// ============================================================================
// vel::clamp(int, min, max) Tests
// ============================================================================

TEST(VelocityHelperTest, ClampIntRange_Custom) {
  EXPECT_EQ(vel::clamp(50, 40, 100), 50);
  EXPECT_EQ(vel::clamp(30, 40, 100), 40);
  EXPECT_EQ(vel::clamp(110, 40, 100), 100);
}

// ============================================================================
// vel::clamp(float, min, max) Tests
// ============================================================================

TEST(VelocityHelperTest, ClampFloatRange_Custom) {
  EXPECT_EQ(vel::clamp(50.0f, 40.0f, 100.0f), 50);
  EXPECT_EQ(vel::clamp(30.0f, 40.0f, 100.0f), 40);
  EXPECT_EQ(vel::clamp(110.0f, 40.0f, 100.0f), 100);
}

// ============================================================================
// vel::scale() Tests
// ============================================================================

TEST(VelocityHelperTest, Scale_Normal) {
  EXPECT_EQ(vel::scale(100, 0.8f), 80);
  EXPECT_EQ(vel::scale(100, 1.0f), 100);
  EXPECT_EQ(vel::scale(100, 0.5f), 50);
}

TEST(VelocityHelperTest, Scale_ClampsToMin) {
  EXPECT_EQ(vel::scale(10, 0.05f), 1);  // 10 * 0.05 = 0.5 -> clamped to 1
}

TEST(VelocityHelperTest, Scale_ClampsToMax) {
  EXPECT_EQ(vel::scale(127, 1.5f), 127);  // 127 * 1.5 = 190 -> clamped to 127
}

// ============================================================================
// vel::withDelta() Tests
// ============================================================================

TEST(VelocityHelperTest, WithDelta_Positive) {
  EXPECT_EQ(vel::withDelta(80, 10), 90);
}

TEST(VelocityHelperTest, WithDelta_Negative) {
  EXPECT_EQ(vel::withDelta(80, -10), 70);
}

TEST(VelocityHelperTest, WithDelta_ClampsToMin) {
  EXPECT_EQ(vel::withDelta(5, -10), 1);  // 5 - 10 = -5, clamped to 1
}

TEST(VelocityHelperTest, WithDelta_ClampsToMax) {
  EXPECT_EQ(vel::withDelta(120, 20), 127);  // 120 + 20 = 140, clamped to 127
}

// ============================================================================
// rng_util::rollProbability() Tests
// ============================================================================

TEST(RngUtilTest, RollProbability_AlwaysTrue) {
  std::mt19937 rng(42);
  int count = 0;
  for (int i = 0; i < 100; ++i) {
    if (rng_util::rollProbability(rng, 1.0f)) ++count;
  }
  EXPECT_EQ(count, 100);
}

TEST(RngUtilTest, RollProbability_AlwaysFalse) {
  std::mt19937 rng(42);
  int count = 0;
  for (int i = 0; i < 100; ++i) {
    if (rng_util::rollProbability(rng, 0.0f)) ++count;
  }
  EXPECT_EQ(count, 0);
}

TEST(RngUtilTest, RollProbability_Approximately50Percent) {
  std::mt19937 rng(42);
  int count = 0;
  constexpr int N = 10000;
  for (int i = 0; i < N; ++i) {
    if (rng_util::rollProbability(rng, 0.5f)) ++count;
  }
  // Should be around 5000 with reasonable tolerance
  EXPECT_GT(count, 4500);
  EXPECT_LT(count, 5500);
}

// ============================================================================
// rng_util::rollRange() Tests
// ============================================================================

TEST(RngUtilTest, RollRange_WithinBounds) {
  std::mt19937 rng(42);
  for (int i = 0; i < 100; ++i) {
    int val = rng_util::rollRange(rng, 10, 20);
    EXPECT_GE(val, 10);
    EXPECT_LE(val, 20);
  }
}

TEST(RngUtilTest, RollRange_SingleValue) {
  std::mt19937 rng(42);
  EXPECT_EQ(rng_util::rollRange(rng, 5, 5), 5);
}

// ============================================================================
// rng_util::rollFloat() Tests
// ============================================================================

TEST(RngUtilTest, RollFloat_WithinBounds) {
  std::mt19937 rng(42);
  for (int i = 0; i < 100; ++i) {
    float val = rng_util::rollFloat(rng, 0.0f, 1.0f);
    EXPECT_GE(val, 0.0f);
    EXPECT_LE(val, 1.0f);
  }
}

// ============================================================================
// rng_util::selectRandom() Tests
// ============================================================================

TEST(RngUtilTest, SelectRandom_FromVector) {
  std::mt19937 rng(42);
  std::vector<int> items = {10, 20, 30, 40, 50};

  for (int i = 0; i < 50; ++i) {
    const auto& val = rng_util::selectRandom(rng, items);
    EXPECT_TRUE(val == 10 || val == 20 || val == 30 || val == 40 || val == 50);
  }
}

TEST(RngUtilTest, SelectRandom_MutableVector) {
  std::mt19937 rng(42);
  std::vector<int> items = {1, 2, 3};

  auto& val = rng_util::selectRandom(rng, items);
  val = 99;  // Should modify the original vector
  bool found = false;
  for (int v : items) {
    if (v == 99) found = true;
  }
  EXPECT_TRUE(found);
}

// ============================================================================
// rng_util::selectRandomIndex() Tests
// ============================================================================

TEST(RngUtilTest, SelectRandomIndex_WithinBounds) {
  std::mt19937 rng(42);
  std::vector<int> items = {10, 20, 30};

  for (int i = 0; i < 50; ++i) {
    size_t idx = rng_util::selectRandomIndex(rng, items);
    EXPECT_LT(idx, items.size());
  }
}
