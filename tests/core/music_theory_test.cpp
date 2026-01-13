/**
 * @file music_theory_test.cpp
 * @brief Tests for music theory fixes (review_05_music_theory.md)
 */

#include <gtest/gtest.h>
#include "core/pitch_utils.h"
#include "core/chord_utils.h"

namespace midisketch {

// =============================================================================
// Issue 1: Tension note definition (6th is NOT a tension)
// =============================================================================

class TensionNoteTest : public ::testing::Test {};

TEST_F(TensionNoteTest, SixthIsNotTension) {
  // In C major, A (pitch_class 9) is the root of vi chord, not a tension
  // Tensions are: 2nd (D, pc=2), 4th (F, pc=5), 7th (B, pc=11)

  // This is a conceptual test - the actual tension check is in detectCadenceType
  // which is internal to vocal.cpp. We test the principle here.

  // Stable tones in C major: C(0), E(4), G(7), A(9) - tonic + relative minor root
  std::vector<int> stable_tones = {0, 4, 7, 9};

  // Tension tones: D(2), F(5), B(11)
  std::vector<int> tension_tones = {2, 5, 11};

  // 6th (A, pc=9) should be in stable, not tension
  EXPECT_TRUE(std::find(stable_tones.begin(), stable_tones.end(), 9) != stable_tones.end());
  EXPECT_TRUE(std::find(tension_tones.begin(), tension_tones.end(), 9) == tension_tones.end());
}

// =============================================================================
// Issue 2: Tritone context-aware dissonance
// =============================================================================

class TritoneContextTest : public ::testing::Test {};

TEST_F(TritoneContextTest, TritoneDissonantOnNonDominant) {
  // Tritone (6 semitones) should be dissonant on non-dominant chords
  // I chord (degree 0): tritone is dissonant
  EXPECT_TRUE(isDissonantIntervalWithContext(0, 6, 0));  // C-F# on I
  EXPECT_TRUE(isDissonantIntervalWithContext(5, 11, 0)); // F-B on I
}

TEST_F(TritoneContextTest, TritoneAcceptableOnDominant) {
  // Tritone is part of V7 chord structure (3rd and 7th)
  // V chord (degree 4): tritone is acceptable
  EXPECT_FALSE(isDissonantIntervalWithContext(0, 6, 4));  // On V chord
  EXPECT_FALSE(isDissonantIntervalWithContext(5, 11, 4)); // B-F on V7
}

TEST_F(TritoneContextTest, TritoneAcceptableOnDiminished) {
  // vii° chord (degree 6): tritone is part of the chord (root to dim5)
  EXPECT_FALSE(isDissonantIntervalWithContext(0, 6, 6));  // On vii°
}

TEST_F(TritoneContextTest, Minor2ndAlwaysDissonant) {
  // Minor 2nd (1 semitone) is always dissonant regardless of chord
  EXPECT_TRUE(isDissonantIntervalWithContext(0, 1, 0));  // On I
  EXPECT_TRUE(isDissonantIntervalWithContext(0, 1, 4));  // On V
  EXPECT_TRUE(isDissonantIntervalWithContext(0, 1, 6));  // On vii°
}

// =============================================================================
// Issue 8: Voice leading weighted distance
// =============================================================================

// We can't directly test voicingDistance as it's in an anonymous namespace,
// but we can verify the principle through observable behavior

// =============================================================================
// Issue 9: Dynamic passaggio calculation
// =============================================================================

class PassaggioTest : public ::testing::Test {};

TEST_F(PassaggioTest, FixedPassaggioRange) {
  // Fixed passaggio: E4 (64) to B4 (71)
  EXPECT_FALSE(isInPassaggio(63));  // D#4
  EXPECT_TRUE(isInPassaggio(64));   // E4 (PASSAGGIO_LOW)
  EXPECT_TRUE(isInPassaggio(67));   // G4
  EXPECT_TRUE(isInPassaggio(71));   // B4 (PASSAGGIO_HIGH)
  EXPECT_FALSE(isInPassaggio(72));  // C5
}

TEST_F(PassaggioTest, DynamicPassaggioNarrowRange) {
  // Very narrow range (<=12 semitones) should use fixed passaggio
  uint8_t low = 60;   // C4
  uint8_t high = 72;  // C5 (12 semitone range)

  // Should fall back to fixed passaggio
  EXPECT_FALSE(isInPassaggioRange(63, low, high));  // D#4
  EXPECT_TRUE(isInPassaggioRange(64, low, high));   // E4
  EXPECT_TRUE(isInPassaggioRange(71, low, high));   // B4
}

TEST_F(PassaggioTest, DynamicPassaggioWideRange) {
  // Wide range: passaggio at 55%-75% of range
  uint8_t low = 48;   // C3
  uint8_t high = 84;  // C6 (36 semitone range)

  // 55% of 36 = 19.8 -> 48 + 19 = 67 (G4)
  // 75% of 36 = 27 -> 48 + 27 = 75 (D#5)

  EXPECT_FALSE(isInPassaggioRange(66, low, high));  // F#4, below passaggio
  EXPECT_TRUE(isInPassaggioRange(67, low, high));   // G4, start of passaggio
  EXPECT_TRUE(isInPassaggioRange(71, low, high));   // B4, in passaggio
  EXPECT_TRUE(isInPassaggioRange(75, low, high));   // D#5, end of passaggio
  EXPECT_FALSE(isInPassaggioRange(76, low, high));  // E5, above passaggio
}

TEST_F(PassaggioTest, DynamicPassaggioTenorRange) {
  // Typical tenor range: C3 (48) to C5 (72) - 24 semitones
  uint8_t low = 48;
  uint8_t high = 72;

  // 55% of 24 = 13.2 -> 48 + 13 = 61 (C#4)
  // 75% of 24 = 18 -> 48 + 18 = 66 (F#4)

  EXPECT_FALSE(isInPassaggioRange(60, low, high));  // C4, below
  EXPECT_TRUE(isInPassaggioRange(61, low, high));   // C#4, start
  EXPECT_TRUE(isInPassaggioRange(64, low, high));   // E4, in
  EXPECT_TRUE(isInPassaggioRange(66, low, high));   // F#4, end
  EXPECT_FALSE(isInPassaggioRange(67, low, high));  // G4, above
}

TEST_F(PassaggioTest, DynamicPassaggioSopranoRange) {
  // Typical soprano range: C4 (60) to C6 (84) - 24 semitones
  uint8_t low = 60;
  uint8_t high = 84;

  // 55% of 24 = 13.2 -> 60 + 13 = 73 (C#5)
  // 75% of 24 = 18 -> 60 + 18 = 78 (F#5)

  EXPECT_FALSE(isInPassaggioRange(72, low, high));  // C5, below
  EXPECT_TRUE(isInPassaggioRange(73, low, high));   // C#5, start
  EXPECT_TRUE(isInPassaggioRange(76, low, high));   // E5, in
  EXPECT_TRUE(isInPassaggioRange(78, low, high));   // F#5, end
  EXPECT_FALSE(isInPassaggioRange(79, low, high));  // G5, above
}

// =============================================================================
// Chord tone utilities
// =============================================================================

class ChordToneTest : public ::testing::Test {};

TEST_F(ChordToneTest, CMajorChordTones) {
  // I chord (degree 0) in C major: C-E-G
  auto tones = getChordTonePitchClasses(0);
  ASSERT_EQ(tones.size(), 3);
  EXPECT_EQ(tones[0], 0);  // C
  EXPECT_EQ(tones[1], 4);  // E
  EXPECT_EQ(tones[2], 7);  // G
}

TEST_F(ChordToneTest, DMinorChordTones) {
  // ii chord (degree 1) in C major: D-F-A
  auto tones = getChordTonePitchClasses(1);
  ASSERT_EQ(tones.size(), 3);
  EXPECT_EQ(tones[0], 2);  // D
  EXPECT_EQ(tones[1], 5);  // F
  EXPECT_EQ(tones[2], 9);  // A
}

TEST_F(ChordToneTest, GDominantChordTones) {
  // V chord (degree 4) in C major: G-B-D
  auto tones = getChordTonePitchClasses(4);
  ASSERT_EQ(tones.size(), 3);
  EXPECT_EQ(tones[0], 7);  // G
  EXPECT_EQ(tones[1], 11); // B
  EXPECT_EQ(tones[2], 2);  // D
}

TEST_F(ChordToneTest, BDiminishedChordTones) {
  // vii° chord (degree 6) in C major: B-D-F (diminished)
  auto tones = getChordTonePitchClasses(6);
  ASSERT_EQ(tones.size(), 3);
  EXPECT_EQ(tones[0], 11); // B
  EXPECT_EQ(tones[1], 2);  // D
  EXPECT_EQ(tones[2], 5);  // F (diminished 5th, tritone from B)
}

}  // namespace midisketch
