/**
 * @file chord_utils_test.cpp
 * @brief Tests for chord utilities.
 */

#include "core/chord_utils.h"

#include <gtest/gtest.h>

#include <random>

#include "core/pitch_utils.h"

namespace midisketch {
namespace {

// ============================================================================
// Constants Tests
// ============================================================================

TEST(ChordUtilsTest, ScaleConstants) {
  // SCALE from pitch_utils.h defines major scale intervals
  EXPECT_EQ(SCALE[0], 0);   // C
  EXPECT_EQ(SCALE[1], 2);   // D
  EXPECT_EQ(SCALE[2], 4);   // E
  EXPECT_EQ(SCALE[3], 5);   // F
  EXPECT_EQ(SCALE[4], 7);   // G
  EXPECT_EQ(SCALE[5], 9);   // A
  EXPECT_EQ(SCALE[6], 11);  // B
}

// ============================================================================
// ChordTones Tests
// ============================================================================

TEST(ChordUtilsTest, GetChordTonesIMajor) {
  // I chord in C major = C major triad = C, E, G
  ChordTones ct = getChordTones(0);
  EXPECT_GE(ct.count, 3u);

  // Check root (C = 0) and third (E = 4) and fifth (G = 7)
  bool has_c = false, has_e = false, has_g = false;
  for (uint8_t i = 0; i < ct.count; ++i) {
    if (ct.pitch_classes[i] == 0) has_c = true;
    if (ct.pitch_classes[i] == 4) has_e = true;
    if (ct.pitch_classes[i] == 7) has_g = true;
  }
  EXPECT_TRUE(has_c);
  EXPECT_TRUE(has_e);
  EXPECT_TRUE(has_g);
}

TEST(ChordUtilsTest, GetChordTonesIVMajor) {
  // IV chord in C major = F major triad = F, A, C
  ChordTones ct = getChordTones(3);
  EXPECT_GE(ct.count, 3u);

  // Check root (F = 5) and third (A = 9) and fifth (C = 0)
  bool has_f = false, has_a = false, has_c = false;
  for (uint8_t i = 0; i < ct.count; ++i) {
    if (ct.pitch_classes[i] == 5) has_f = true;
    if (ct.pitch_classes[i] == 9) has_a = true;
    if (ct.pitch_classes[i] == 0) has_c = true;
  }
  EXPECT_TRUE(has_f);
  EXPECT_TRUE(has_a);
  EXPECT_TRUE(has_c);
}

TEST(ChordUtilsTest, GetChordTonesVMajor) {
  // V chord in C major = G major triad = G, B, D
  ChordTones ct = getChordTones(4);
  EXPECT_GE(ct.count, 3u);

  // Check root (G = 7) and third (B = 11) and fifth (D = 2)
  bool has_g = false, has_b = false, has_d = false;
  for (uint8_t i = 0; i < ct.count; ++i) {
    if (ct.pitch_classes[i] == 7) has_g = true;
    if (ct.pitch_classes[i] == 11) has_b = true;
    if (ct.pitch_classes[i] == 2) has_d = true;
  }
  EXPECT_TRUE(has_g);
  EXPECT_TRUE(has_b);
  EXPECT_TRUE(has_d);
}

TEST(ChordUtilsTest, GetChordTonesViMinor) {
  // vi chord in C major = A minor triad = A, C, E
  ChordTones ct = getChordTones(5);
  EXPECT_GE(ct.count, 3u);

  // Check root (A = 9) and third (C = 0) and fifth (E = 4)
  bool has_a = false, has_c = false, has_e = false;
  for (uint8_t i = 0; i < ct.count; ++i) {
    if (ct.pitch_classes[i] == 9) has_a = true;
    if (ct.pitch_classes[i] == 0) has_c = true;
    if (ct.pitch_classes[i] == 4) has_e = true;
  }
  EXPECT_TRUE(has_a);
  EXPECT_TRUE(has_c);
  EXPECT_TRUE(has_e);
}

TEST(ChordUtilsTest, GetChordTonesUnusedSlotsFilled) {
  ChordTones ct = getChordTones(0);
  // Unused slots should be -1
  for (uint8_t i = ct.count; i < 5; ++i) {
    EXPECT_EQ(ct.pitch_classes[i], -1);
  }
}

// ============================================================================
// getChordTonePitchClasses Tests
// ============================================================================

TEST(ChordUtilsTest, GetChordTonePitchClassesIMajor) {
  std::vector<int> pcs = getChordTonePitchClasses(0);
  EXPECT_GE(pcs.size(), 3u);

  // Should contain C=0, E=4, G=7
  EXPECT_NE(std::find(pcs.begin(), pcs.end(), 0), pcs.end());
  EXPECT_NE(std::find(pcs.begin(), pcs.end(), 4), pcs.end());
  EXPECT_NE(std::find(pcs.begin(), pcs.end(), 7), pcs.end());
}

TEST(ChordUtilsTest, GetChordTonePitchClassesNegativeDegree) {
  // Negative degree should be normalized
  std::vector<int> pcs = getChordTonePitchClasses(-1);  // Same as degree 6 (vii)
  EXPECT_GE(pcs.size(), 3u);
}

// ============================================================================
// nearestChordTonePitch Tests
// ============================================================================

TEST(ChordUtilsTest, NearestChordTonePitchExact) {
  // C4 (60) is already a chord tone of I chord
  int result = nearestChordTonePitch(60, 0);
  EXPECT_EQ(result, 60);
}

TEST(ChordUtilsTest, NearestChordTonePitchClose) {
  // C#4 (61) should snap to C4 (60) or E4 (64) for I chord
  int result = nearestChordTonePitch(61, 0);
  EXPECT_TRUE(result == 60 || result == 64);
}

TEST(ChordUtilsTest, NearestChordTonePitchOctave) {
  // C5 (72) is a chord tone of I chord
  int result = nearestChordTonePitch(72, 0);
  EXPECT_EQ(result, 72);
}

TEST(ChordUtilsTest, NearestChordTonePitchDifferentOctave) {
  // B4 (71) should snap to G4 (67) or C5 (72) for I chord
  int result = nearestChordTonePitch(71, 0);
  EXPECT_TRUE(result == 67 || result == 72);
}

// ============================================================================
// nearestChordToneWithinInterval Tests
// ============================================================================

TEST(ChordUtilsTest, NearestChordToneWithinIntervalBasic) {
  // Target C4 (60), prev E4 (64), max interval 5, I chord
  // E4 is also a chord tone (3rd of I), and staying on E4 is more singable
  // (dist_to_prev = 0 gets highest bonus for stepwise motion preference)
  int result = nearestChordToneWithinInterval(60, 64, 0, 5, 48, 84, nullptr);
  EXPECT_EQ(result, 64);  // Prefer staying on current chord tone for singability
}

TEST(ChordUtilsTest, NearestChordToneWithinIntervalConstrained) {
  // Target far away, prev E4 (64), max interval 2, I chord
  int result = nearestChordToneWithinInterval(80, 64, 0, 2, 48, 84, nullptr);
  // Should stay at E4 (64) since no chord tone is within 2 semitones
  // Actually E4 (64) is a chord tone of I chord
  EXPECT_EQ(result, 64);
}

TEST(ChordUtilsTest, NearestChordToneWithinIntervalNoPrev) {
  // No previous pitch, should just find nearest to target
  int result = nearestChordToneWithinInterval(61, -1, 0, 5, 48, 84, nullptr);
  // Should snap to nearest chord tone (C4=60 or E4=64)
  EXPECT_TRUE(result == 60 || result == 64);
}

TEST(ChordUtilsTest, NearestChordToneWithinIntervalWithTessitura) {
  TessituraRange t{60, 72, 66};

  // Target G4 (67), prev E4 (64), max interval 7, I chord
  // Both E4 and G4 are chord tones and within tessitura
  // E4 is preferred for singability (staying on prev pitch)
  int result = nearestChordToneWithinInterval(67, 64, 0, 7, 48, 84, &t);
  EXPECT_EQ(result, 64);  // Prefer staying on current chord tone
}

TEST(ChordUtilsTest, NearestChordToneWithinIntervalRespectsBounds) {
  // Target way below range
  int result = nearestChordToneWithinInterval(30, 60, 0, 12, 48, 84, nullptr);
  // Should be within range [48, 84]
  EXPECT_GE(result, 48);
  EXPECT_LE(result, 84);
}

TEST(ChordUtilsTest, NearestChordToneWithinIntervalDifferentChord) {
  // V chord = G, B, D
  // Target G4 (67), prev D4 (62), max interval 7
  // D4 is also a chord tone (5th of V), staying on D4 is more singable
  int result = nearestChordToneWithinInterval(67, 62, 4, 7, 48, 84, nullptr);
  EXPECT_EQ(result, 62);  // Prefer staying on current chord tone for singability
}

// ============================================================================
// stepwiseToTarget Tests
// ============================================================================

TEST(ChordUtilsTest, StepwiseToTargetLeadingToneResolution) {
  // Leading tone (B=71) ascending to tonic (C) should prefer half step
  // B4 (71) -> target C5 (72), ascending direction
  // Key = C (0), I chord (0)
  std::mt19937 rng(42);
  int result = stepwiseToTarget(71, 73, 0, 60, 84, 0, 0, &rng);
  // Should move by half step to C5 (72) for leading tone resolution
  EXPECT_EQ(result, 72);
}

TEST(ChordUtilsTest, StepwiseToTargetLeadingToneInDifferentKey) {
  // Leading tone in G major: F# (11 semitones from G)
  // F#4 (66) -> target G4 (67), ascending direction
  // Key = G (7)
  std::mt19937 rng(42);
  // Leading tone in G major is F# (pitch class 6, which is 11 semitones from G)
  // F#4 = 66, target G4 = 67
  int result = stepwiseToTarget(66, 68, 0, 60, 84, 7, 0, &rng);
  // Should move by half step to G4 (67)
  EXPECT_EQ(result, 67);
}

TEST(ChordUtilsTest, StepwiseToTargetNonLeadingTonePreferWholeStep) {
  // Non-leading tone should sometimes use whole step (probabilistic)
  // C4 (60) -> target E4 (64), ascending direction, key = C
  // Without leading tone resolution, whole step (D4=62) is preferred by default
  // but 30% of the time half step is chosen
  std::mt19937 rng(12345);
  int result = stepwiseToTarget(60, 64, 0, 48, 84, 0, 0, &rng);
  // Should be either D4 (62, whole step) or C#/Db (61, half step - but 61 is not in scale)
  // Since C# is not in C major scale, it should be D4 (62)
  EXPECT_EQ(result, 62);
}

TEST(ChordUtilsTest, StepwiseToTargetDescending) {
  // Descending motion: E4 (64) -> target C4 (60)
  std::mt19937 rng(42);
  int result = stepwiseToTarget(64, 60, 0, 48, 84, 0, 0, &rng);
  // Should move down by step (D4=62 whole step or Eb=63 not in scale)
  EXPECT_EQ(result, 62);
}

TEST(ChordUtilsTest, StepwiseToTargetDeterministic) {
  // Same seed should produce same results (deterministic behavior)
  std::mt19937 rng1(99999);
  std::mt19937 rng2(99999);

  int result1 = stepwiseToTarget(65, 70, 0, 48, 84, 0, 0, &rng1);
  int result2 = stepwiseToTarget(65, 70, 0, 48, 84, 0, 0, &rng2);

  EXPECT_EQ(result1, result2) << "Same seed should produce same result";
}

TEST(ChordUtilsTest, StepwiseToTargetProbabilisticHalfStep) {
  // Test that half step is chosen probabilistically (about 30% of the time)
  // Run multiple trials with different seeds and count half steps
  // Use E4 (64) -> F4 (65) is a valid half step in C major (E-F is a half step in scale)
  int half_step_count = 0;
  const int trials = 100;

  for (int seed = 0; seed < trials; ++seed) {
    std::mt19937 rng(seed);
    // E4 (64) -> target up: +1 = F (65, in scale), +2 = F# (66, NOT in scale)
    // So whole step from E should fail and fall back to half step
    int result = stepwiseToTarget(64, 70, 0, 48, 84, 0, 0, &rng);
    int step = result - 64;
    if (step == 1) {
      half_step_count++;
    }
    // Steps of 2 or 3 are also valid outcomes (not tracked)
  }

  // In C major from E, only half step (to F) is valid in scale
  // So we expect most results to be half step regardless of probability
  EXPECT_GT(half_step_count, 0) << "Half step should occur when whole step is not in scale";
}

TEST(ChordUtilsTest, StepwiseToTargetAvoidsAvoidNotes) {
  // Result should avoid notes that are minor 2nd or tritone from chord root
  // I chord root = C, avoid notes: C# (1 semitone) and F# (6 semitones)
  std::mt19937 rng(42);
  // Start from B3 (59), target up
  int result = stepwiseToTarget(59, 65, 0, 48, 84, 0, 0, &rng);
  int result_pc = result % 12;
  // Should not be C# (1) or F# (6)
  EXPECT_NE(result_pc, 1) << "Should avoid minor 2nd from root";
  EXPECT_NE(result_pc, 6) << "Should avoid tritone from root";
}

TEST(ChordUtilsTest, StepwiseToTargetRespectsRange) {
  // Result should stay within specified range
  std::mt19937 rng(42);
  int result = stepwiseToTarget(60, 70, 0, 55, 65, 0, 0, &rng);
  EXPECT_GE(result, 55);
  EXPECT_LE(result, 65);
}

}  // namespace
}  // namespace midisketch
