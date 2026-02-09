/**
 * @file basic_types_test.cpp
 * @brief Tests for tick/bar/beat conversion utilities in basic_types.h.
 */

#include "core/basic_types.h"

#include <gtest/gtest.h>

namespace midisketch {
namespace {

// ============================================================================
// tickToBar Tests
// ============================================================================

TEST(BasicTypesTest, TickToBar_Zero) { EXPECT_EQ(tickToBar(0), 0u); }

TEST(BasicTypesTest, TickToBar_LastTickOfFirstBar) { EXPECT_EQ(tickToBar(1919), 0u); }

TEST(BasicTypesTest, TickToBar_FirstTickOfSecondBar) { EXPECT_EQ(tickToBar(1920), 1u); }

TEST(BasicTypesTest, TickToBar_ThirdBar) { EXPECT_EQ(tickToBar(3840), 2u); }

// ============================================================================
// tickToBeat Tests
// ============================================================================

TEST(BasicTypesTest, TickToBeat_Zero) { EXPECT_EQ(tickToBeat(0), 0u); }

TEST(BasicTypesTest, TickToBeat_LastTickOfFirstBeat) { EXPECT_EQ(tickToBeat(479), 0u); }

TEST(BasicTypesTest, TickToBeat_SecondBeat) { EXPECT_EQ(tickToBeat(480), 1u); }

TEST(BasicTypesTest, TickToBeat_ThirdBeat) { EXPECT_EQ(tickToBeat(960), 2u); }

// ============================================================================
// positionInBar Tests
// ============================================================================

TEST(BasicTypesTest, PositionInBar_Zero) { EXPECT_EQ(positionInBar(0), 0u); }

TEST(BasicTypesTest, PositionInBar_BarBoundary) { EXPECT_EQ(positionInBar(1920), 0u); }

TEST(BasicTypesTest, PositionInBar_OneAfterBoundary) { EXPECT_EQ(positionInBar(1921), 1u); }

TEST(BasicTypesTest, PositionInBar_EndOfFirstBeat) { EXPECT_EQ(positionInBar(2399), 479u); }

// ============================================================================
// beatInBar Tests
// ============================================================================

TEST(BasicTypesTest, BeatInBar_Beat0) { EXPECT_EQ(beatInBar(0), 0); }

TEST(BasicTypesTest, BeatInBar_Beat1) { EXPECT_EQ(beatInBar(480), 1); }

TEST(BasicTypesTest, BeatInBar_Beat2) { EXPECT_EQ(beatInBar(960), 2); }

TEST(BasicTypesTest, BeatInBar_Beat3) { EXPECT_EQ(beatInBar(1440), 3); }

TEST(BasicTypesTest, BeatInBar_WrapsAtBarBoundary) { EXPECT_EQ(beatInBar(1920), 0); }

// ============================================================================
// barToTick Tests
// ============================================================================

TEST(BasicTypesTest, BarToTick_Zero) { EXPECT_EQ(barToTick(0), 0u); }

TEST(BasicTypesTest, BarToTick_FirstBar) { EXPECT_EQ(barToTick(1), 1920u); }

TEST(BasicTypesTest, BarToTick_SecondBar) { EXPECT_EQ(barToTick(2), 3840u); }

// ============================================================================
// Round-trip Tests
// ============================================================================

TEST(BasicTypesTest, RoundTrip_BarToTickToBar) {
  // barToTick(tickToBar(x)) should return the start of the bar containing x
  EXPECT_EQ(barToTick(tickToBar(0)), 0u);
  EXPECT_EQ(barToTick(tickToBar(1919)), 0u);
  EXPECT_EQ(barToTick(tickToBar(1920)), 1920u);
  EXPECT_EQ(barToTick(tickToBar(2000)), 1920u);
  EXPECT_EQ(barToTick(tickToBar(3840)), 3840u);
}

}  // namespace
}  // namespace midisketch
