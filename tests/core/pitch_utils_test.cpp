/**
 * @file pitch_utils_test.cpp
 * @brief Tests for pitch utilities.
 */

#include <gtest/gtest.h>
#include "core/pitch_utils.h"

namespace midisketch {
namespace {

// ============================================================================
// TessituraRange Tests
// ============================================================================

TEST(PitchUtilsTest, CalculateTessituraBasic) {
  // Standard vocal range: C4 (60) to C5 (72) = 12 semitones
  TessituraRange t = calculateTessitura(60, 72);

  // Margin = 12 / 5 = 2 (but min is 3)
  // So tessitura = 60+3=63 to 72-3=69
  EXPECT_EQ(t.low, 63);
  EXPECT_EQ(t.high, 69);
  EXPECT_EQ(t.center, 66);
}

TEST(PitchUtilsTest, CalculateTessituraWideRange) {
  // Wide range: C3 (48) to C6 (84) = 36 semitones
  TessituraRange t = calculateTessitura(48, 84);

  // Margin = 36 / 5 = 7
  // So tessitura = 48+7=55 to 84-7=77
  EXPECT_EQ(t.low, 55);
  EXPECT_EQ(t.high, 77);
  EXPECT_EQ(t.center, 66);
}

TEST(PitchUtilsTest, CalculateTessituraNarrowRange) {
  // Narrow range: E4 (64) to G4 (67) = 3 semitones
  // After applying margin, if low >= high, fallback to original range
  TessituraRange t = calculateTessitura(64, 67);

  // Margin = 3 / 5 = 0 (but min is 3)
  // 64+3=67 >= 67-3=64, so fallback
  EXPECT_EQ(t.low, 64);
  EXPECT_EQ(t.high, 67);
  EXPECT_EQ(t.center, 65);
}

TEST(PitchUtilsTest, IsInTessitura) {
  TessituraRange t{60, 72, 66};

  EXPECT_TRUE(isInTessitura(60, t));   // Low boundary
  EXPECT_TRUE(isInTessitura(66, t));   // Center
  EXPECT_TRUE(isInTessitura(72, t));   // High boundary
  EXPECT_FALSE(isInTessitura(59, t));  // Below
  EXPECT_FALSE(isInTessitura(73, t));  // Above
}

TEST(PitchUtilsTest, GetComfortScoreCenter) {
  TessituraRange t = calculateTessitura(55, 75);
  float score = getComfortScore(t.center, t, 55, 75);
  EXPECT_FLOAT_EQ(score, 1.0f);  // Perfect score at center
}

TEST(PitchUtilsTest, GetComfortScoreInTessitura) {
  TessituraRange t = calculateTessitura(55, 75);
  float score = getComfortScore(t.low, t, 55, 75);
  EXPECT_GE(score, 0.8f);  // High score for tessitura range
  EXPECT_LE(score, 1.0f);
}

TEST(PitchUtilsTest, GetComfortScorePassaggio) {
  // Create a tessitura that excludes the passaggio zone
  // Passaggio is 64-71, so use tessitura above it
  TessituraRange t{72, 82, 77};  // Tessitura above passaggio
  // Pitch 68 (G#4) is in passaggio but outside tessitura
  float score = getComfortScore(68, t, 60, 85);
  EXPECT_FLOAT_EQ(score, 0.4f);  // Reduced score for passaggio
}

TEST(PitchUtilsTest, GetComfortScoreExtreme) {
  TessituraRange t = calculateTessitura(50, 80);
  float score_low = getComfortScore(50, t, 50, 80);   // Extreme low
  float score_high = getComfortScore(80, t, 50, 80);  // Extreme high

  EXPECT_GE(score_low, 0.3f);
  EXPECT_LE(score_low, 0.6f);
  EXPECT_GE(score_high, 0.3f);
  EXPECT_LE(score_high, 0.6f);
}

// ============================================================================
// Passaggio Tests
// ============================================================================

TEST(PitchUtilsTest, IsInPassaggio) {
  EXPECT_FALSE(isInPassaggio(63));  // E4 - 1 = D#4, below
  EXPECT_TRUE(isInPassaggio(64));   // E4 (PASSAGGIO_LOW)
  EXPECT_TRUE(isInPassaggio(68));   // G#4, middle of passaggio
  EXPECT_TRUE(isInPassaggio(71));   // B4 (PASSAGGIO_HIGH)
  EXPECT_FALSE(isInPassaggio(72));  // C5, above
}

TEST(PitchUtilsTest, PassaggioConstants) {
  EXPECT_EQ(PASSAGGIO_LOW, 64);   // E4
  EXPECT_EQ(PASSAGGIO_HIGH, 71);  // B4
}

// ============================================================================
// Interval Constraint Tests
// ============================================================================

TEST(PitchUtilsTest, ConstrainIntervalWithinLimit) {
  // Target within interval limit
  int result = constrainInterval(65, 60, 7, 48, 84);
  EXPECT_EQ(result, 65);  // 5 semitones, within 7 limit
}

TEST(PitchUtilsTest, ConstrainIntervalExceedsLimit) {
  // Target exceeds interval limit (going up)
  int result = constrainInterval(72, 60, 5, 48, 84);
  // Should constrain to prev + max_interval = 60 + 5 = 65
  EXPECT_EQ(result, 65);
}

TEST(PitchUtilsTest, ConstrainIntervalExceedsLimitDown) {
  // Target exceeds interval limit (going down)
  int result = constrainInterval(50, 60, 5, 48, 84);
  // Should constrain to prev - max_interval = 60 - 5 = 55
  EXPECT_EQ(result, 55);
}

TEST(PitchUtilsTest, ConstrainIntervalNoPrevious) {
  // No previous pitch
  int result = constrainInterval(65, -1, 7, 48, 84);
  EXPECT_EQ(result, 65);  // Just return target clamped to range
}

TEST(PitchUtilsTest, ConstrainIntervalAtRangeBoundary) {
  // Target exceeds both interval and range (going up)
  int result = constrainInterval(90, 80, 5, 48, 84);
  // prev + max = 85, but clamp to 84
  EXPECT_EQ(result, 84);
}

TEST(PitchUtilsTest, ConstrainIntervalStaysAtPrevWhenCantMove) {
  // At top of range, want to go up
  int result = constrainInterval(90, 84, 5, 48, 84);
  // Can't go up from 84, stay at 84
  EXPECT_EQ(result, 84);
}

// ============================================================================
// Dissonant Interval Tests
// ============================================================================

TEST(PitchUtilsTest, IsDissonantIntervalMinor2nd) {
  EXPECT_TRUE(isDissonantInterval(0, 1));   // C and C#
  EXPECT_TRUE(isDissonantInterval(4, 5));   // E and F
  EXPECT_TRUE(isDissonantInterval(11, 0));  // B and C (wrapped)
}

TEST(PitchUtilsTest, IsDissonantIntervalTritone) {
  EXPECT_TRUE(isDissonantInterval(0, 6));   // C and F#
  EXPECT_TRUE(isDissonantInterval(5, 11));  // F and B
}

TEST(PitchUtilsTest, IsNotDissonantConsonant) {
  EXPECT_FALSE(isDissonantInterval(0, 4));   // Major 3rd
  EXPECT_FALSE(isDissonantInterval(0, 3));   // Minor 3rd
  EXPECT_FALSE(isDissonantInterval(0, 7));   // Perfect 5th
  EXPECT_FALSE(isDissonantInterval(0, 5));   // Perfect 4th
  EXPECT_FALSE(isDissonantInterval(0, 0));   // Unison
  EXPECT_FALSE(isDissonantInterval(0, 2));   // Major 2nd (not severely dissonant)
}

// ============================================================================
// Context-Aware Dissonance Tests
// ============================================================================

TEST(PitchUtilsTest, IsDissonantWithContextMinor2ndAlwaysDissonant) {
  // Minor 2nd is always dissonant regardless of chord context
  EXPECT_TRUE(isDissonantIntervalWithContext(0, 1, 0));  // On I chord
  EXPECT_TRUE(isDissonantIntervalWithContext(0, 1, 4));  // On V chord
  EXPECT_TRUE(isDissonantIntervalWithContext(4, 5, 4));  // E-F on V chord
}

TEST(PitchUtilsTest, IsDissonantWithContextTritoneOnDominant) {
  // Tritone is acceptable on dominant (V) chord (degree 4)
  EXPECT_FALSE(isDissonantIntervalWithContext(0, 6, 4));  // C-F# on V
  EXPECT_FALSE(isDissonantIntervalWithContext(5, 11, 4)); // F-B on V
}

TEST(PitchUtilsTest, IsDissonantWithContextTritoneOnNonDominant) {
  // Tritone is dissonant on non-dominant chords
  EXPECT_TRUE(isDissonantIntervalWithContext(0, 6, 0));   // C-F# on I
  EXPECT_TRUE(isDissonantIntervalWithContext(0, 6, 3));   // C-F# on IV
  EXPECT_TRUE(isDissonantIntervalWithContext(5, 11, 5));  // F-B on vi
}

// ============================================================================
// Scale Snap Tests
// ============================================================================

TEST(PitchUtilsTest, SnapToNearestScaleToneInScale) {
  // C4 (60) is already in C major scale
  int result = snapToNearestScaleTone(60, 0);
  EXPECT_EQ(result, 60);

  // D4 (62) is already in C major scale
  result = snapToNearestScaleTone(62, 0);
  EXPECT_EQ(result, 62);

  // E4 (64) is already in C major scale
  result = snapToNearestScaleTone(64, 0);
  EXPECT_EQ(result, 64);
}

TEST(PitchUtilsTest, SnapToNearestScaleToneOutOfScale) {
  // C#4 (61) -> should snap to C (60) or D (62)
  int result = snapToNearestScaleTone(61, 0);
  EXPECT_TRUE(result == 60 || result == 62);

  // F#4 (66) -> should snap to F (65) or G (67)
  result = snapToNearestScaleTone(66, 0);
  EXPECT_TRUE(result == 65 || result == 67);
}

TEST(PitchUtilsTest, SnapToNearestScaleToneWithKeyOffset) {
  // G major (key_offset = 7): G A B C D E F#
  // G4 (67) is the tonic of G major
  int result = snapToNearestScaleTone(67, 7);
  EXPECT_EQ(result, 67);

  // F#4 (66) is in G major scale
  result = snapToNearestScaleTone(66, 7);
  EXPECT_EQ(result, 66);
}

TEST(PitchUtilsTest, ScaleConstants) {
  // C major scale intervals
  EXPECT_EQ(SCALE[0], 0);   // C
  EXPECT_EQ(SCALE[1], 2);   // D
  EXPECT_EQ(SCALE[2], 4);   // E
  EXPECT_EQ(SCALE[3], 5);   // F
  EXPECT_EQ(SCALE[4], 7);   // G
  EXPECT_EQ(SCALE[5], 9);   // A
  EXPECT_EQ(SCALE[6], 11);  // B
}

}  // namespace
}  // namespace midisketch
