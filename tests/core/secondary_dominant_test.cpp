/**
 * @file secondary_dominant_test.cpp
 * @brief Tests for secondary dominant detection and generation.
 */

#include "core/chord.h"

#include <gtest/gtest.h>

namespace midisketch {
namespace {

// ============================================================================
// getSecondaryDominantDegree Tests
// ============================================================================

TEST(SecondaryDominantTest, VofII) {
  // V/ii = VI (A7 in C major, targeting Dm)
  EXPECT_EQ(getSecondaryDominantDegree(1), 5);  // Target ii (degree 1) -> VI (degree 5)
}

TEST(SecondaryDominantTest, VofVI) {
  // V/vi = III (E7 in C major, targeting Am)
  EXPECT_EQ(getSecondaryDominantDegree(5), 2);  // Target vi (degree 5) -> III (degree 2)
}

TEST(SecondaryDominantTest, VofIV) {
  // V/IV = I (C7 in C major, targeting F)
  EXPECT_EQ(getSecondaryDominantDegree(3), 0);  // Target IV (degree 3) -> I (degree 0)
}

TEST(SecondaryDominantTest, VofV) {
  // V/V = II (D7 in C major, targeting G)
  EXPECT_EQ(getSecondaryDominantDegree(4), 1);  // Target V (degree 4) -> II (degree 1)
}

TEST(SecondaryDominantTest, VofIII) {
  // V/iii = VII (B7 in C major, targeting Em)
  EXPECT_EQ(getSecondaryDominantDegree(2), 6);  // Target iii (degree 2) -> VII (degree 6)
}

TEST(SecondaryDominantTest, VofVII_Invalid) {
  // V/vii is rarely used (would be #IV)
  EXPECT_EQ(getSecondaryDominantDegree(6), -1);  // Invalid
}

TEST(SecondaryDominantTest, VofI) {
  // V/I = V (just regular dominant)
  EXPECT_EQ(getSecondaryDominantDegree(0), 4);  // Target I (degree 0) -> V (degree 4)
}

// ============================================================================
// checkSecondaryDominant Tests
// ============================================================================

TEST(SecondaryDominantTest, LowTensionNoInsertion) {
  // Low tension should not insert secondary dominant
  auto info = checkSecondaryDominant(0, 1, 0.3f);  // I -> ii
  EXPECT_FALSE(info.should_insert);
}

TEST(SecondaryDominantTest, HighTensionToII) {
  // High tension going to ii should suggest V/ii
  auto info = checkSecondaryDominant(0, 1, 0.7f);  // I -> ii
  EXPECT_TRUE(info.should_insert);
  EXPECT_EQ(info.dominant_degree, 5);  // VI (A in C)
  EXPECT_EQ(info.extension, ChordExtension::Dom7);
  EXPECT_EQ(info.target_degree, 1);
}

TEST(SecondaryDominantTest, HighTensionToVI) {
  // High tension going to vi should suggest V/vi
  auto info = checkSecondaryDominant(0, 5, 0.8f);  // I -> vi
  EXPECT_TRUE(info.should_insert);
  EXPECT_EQ(info.dominant_degree, 2);  // III (E in C)
  EXPECT_EQ(info.extension, ChordExtension::Dom7);
  EXPECT_EQ(info.target_degree, 5);
}

TEST(SecondaryDominantTest, HighTensionToIV) {
  // High tension going to IV should suggest V/IV (but not if already on I)
  // V/IV = I chord, so from vi -> IV we get C7 before F
  auto info = checkSecondaryDominant(5, 3, 0.6f);  // vi -> IV
  EXPECT_TRUE(info.should_insert);
  EXPECT_EQ(info.dominant_degree, 0);  // I (C7 in C)
  EXPECT_EQ(info.extension, ChordExtension::Dom7);
}

TEST(SecondaryDominantTest, HighTensionToV) {
  // High tension going to V should suggest V/V
  auto info = checkSecondaryDominant(0, 4, 0.7f);  // I -> V
  EXPECT_TRUE(info.should_insert);
  EXPECT_EQ(info.dominant_degree, 1);  // II (D in C)
  EXPECT_EQ(info.extension, ChordExtension::Dom7);
}

TEST(SecondaryDominantTest, BadTargetNoInsertion) {
  // iii is not a good target for secondary dominant
  auto info = checkSecondaryDominant(0, 2, 0.8f);  // I -> iii
  EXPECT_FALSE(info.should_insert);
}

TEST(SecondaryDominantTest, AlreadyOnDominantNoInsertion) {
  // If current chord IS the secondary dominant, don't insert
  auto info = checkSecondaryDominant(5, 1, 0.8f);  // VI -> ii (VI is already V/ii)
  EXPECT_FALSE(info.should_insert);
}

TEST(SecondaryDominantTest, ModerateTensionThreshold) {
  // Exactly at threshold (0.5) should not insert
  auto info = checkSecondaryDominant(0, 1, 0.5f);
  EXPECT_FALSE(info.should_insert);

  // Just above threshold should insert
  info = checkSecondaryDominant(0, 1, 0.51f);
  EXPECT_TRUE(info.should_insert);
}

}  // namespace
}  // namespace midisketch
