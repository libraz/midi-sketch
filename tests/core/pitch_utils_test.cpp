/**
 * @file pitch_utils_test.cpp
 * @brief Tests for pitch utilities.
 */

#include "core/pitch_utils.h"

#include <gtest/gtest.h>

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
  TessituraRange t{60, 72, 66, 55, 77};

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
  // Dynamic passaggio calculation: 55%-75% of vocal range
  // For vocal_low=50, vocal_high=80: range=30, passaggio=50+16=66 to 50+22=72
  // Passaggio center = (66+72)/2 = 69, half_width = 3
  // Create tessitura that excludes the passaggio zone
  TessituraRange t{74, 80, 77, 50, 80};  // Tessitura above passaggio (66-72)

  // Pitch 69 (center): gradient=0 -> score = 0.35
  float score_center = getComfortScore(69, t, 50, 80);
  EXPECT_FLOAT_EQ(score_center, 0.35f);  // Center of passaggio = minimum comfort

  // Pitch 66 (boundary): dist=3, gradient=1.0 -> score = 0.45
  float score_boundary = getComfortScore(66, t, 50, 80);
  EXPECT_FLOAT_EQ(score_boundary, 0.45f);  // Boundary = higher comfort (climax potential)

  // Pitch 72 (boundary): dist=3, gradient=1.0 -> score = 0.45
  float score_boundary_high = getComfortScore(72, t, 50, 80);
  EXPECT_FLOAT_EQ(score_boundary_high, 0.45f);  // Symmetric at both boundaries
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
  EXPECT_FALSE(isDissonantInterval(0, 4));  // Major 3rd
  EXPECT_FALSE(isDissonantInterval(0, 3));  // Minor 3rd
  EXPECT_FALSE(isDissonantInterval(0, 7));  // Perfect 5th
  EXPECT_FALSE(isDissonantInterval(0, 5));  // Perfect 4th
  EXPECT_FALSE(isDissonantInterval(0, 0));  // Unison
  EXPECT_FALSE(isDissonantInterval(0, 2));  // Major 2nd (not severely dissonant)
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
  EXPECT_FALSE(isDissonantIntervalWithContext(0, 6, 4));   // C-F# on V
  EXPECT_FALSE(isDissonantIntervalWithContext(5, 11, 4));  // F-B on V
}

TEST(PitchUtilsTest, IsDissonantWithContextTritoneOnNonDominant) {
  // Tritone is dissonant on non-dominant chords
  EXPECT_TRUE(isDissonantIntervalWithContext(0, 6, 0));   // C-F# on I
  EXPECT_TRUE(isDissonantIntervalWithContext(0, 6, 3));   // C-F# on IV
  EXPECT_TRUE(isDissonantIntervalWithContext(5, 11, 5));  // F-B on vi
}

// ============================================================================
// Actual Interval Dissonance Tests (isDissonantActualInterval)
// ============================================================================

TEST(PitchUtilsTest, IsDissonantActualInterval_BasicIntervals) {
  // Basic dissonant intervals (within one octave)
  EXPECT_TRUE(isDissonantActualInterval(1, 0));   // Minor 2nd
  EXPECT_TRUE(isDissonantActualInterval(2, 0));   // Major 2nd
  EXPECT_TRUE(isDissonantActualInterval(11, 0));  // Major 7th
  EXPECT_TRUE(isDissonantActualInterval(6, 0));   // Tritone on I chord
}

TEST(PitchUtilsTest, IsDissonantActualInterval_ConsonantIntervals) {
  // Consonant intervals should NOT be flagged
  EXPECT_FALSE(isDissonantActualInterval(3, 0));   // Minor 3rd
  EXPECT_FALSE(isDissonantActualInterval(4, 0));   // Major 3rd
  EXPECT_FALSE(isDissonantActualInterval(5, 0));   // Perfect 4th
  EXPECT_FALSE(isDissonantActualInterval(7, 0));   // Perfect 5th
  EXPECT_FALSE(isDissonantActualInterval(8, 0));   // Minor 6th
  EXPECT_FALSE(isDissonantActualInterval(9, 0));   // Major 6th
  EXPECT_FALSE(isDissonantActualInterval(10, 0));  // Minor 7th (acceptable in pop)
  EXPECT_FALSE(isDissonantActualInterval(12, 0));  // Octave
}

TEST(PitchUtilsTest, IsDissonantActualInterval_CompoundMinor2nd) {
  // Minor 9th (13 semitones) is the perceptual limit for minor 2nd dissonance
  EXPECT_TRUE(isDissonantActualInterval(13, 0));   // Minor 9th (1 + 12)
  EXPECT_FALSE(isDissonantActualInterval(25, 0));  // Minor 2nd + 2 octaves: too far to clash
}

TEST(PitchUtilsTest, IsDissonantActualInterval_CompoundMajor7th) {
  // Major 7th is dissonant at any distance under 3 octaves.
  // Compound M7 (e.g. bass C2 vs motif B4 = 35 semitones) creates audible beating.
  EXPECT_TRUE(isDissonantActualInterval(11, 0));   // Major 7th: dissonant
  EXPECT_TRUE(isDissonantActualInterval(23, 0));   // Major 7th + octave: dissonant
  EXPECT_TRUE(isDissonantActualInterval(35, 0));   // Major 7th + 2 oct: dissonant (bass vs upper)
  EXPECT_FALSE(isDissonantActualInterval(47, 0));  // Major 7th + 3 oct: allowed (wide separation)
}

TEST(PitchUtilsTest, IsDissonantActualInterval_CompoundTritone) {
  // Compound tritone - context-dependent at any octave
  // Example: F3(53) vs B4(71) = 18 semitones
  EXPECT_TRUE(isDissonantActualInterval(18, 0));   // Tritone + octave on I chord
  EXPECT_TRUE(isDissonantActualInterval(18, 3));   // Tritone + octave on IV chord
  EXPECT_FALSE(isDissonantActualInterval(18, 4));  // Tritone + octave on V chord (allowed)
  EXPECT_FALSE(isDissonantActualInterval(18, 6));  // Tritone + octave on vii chord (allowed)
  EXPECT_TRUE(isDissonantActualInterval(30, 0));   // Tritone + 2 octaves on I chord
}

TEST(PitchUtilsTest, IsDissonantActualInterval_Major9thIsConsonant) {
  // Major 9th (14 semitones) is a common chord extension - NOT dissonant
  // This is critical: add9 chords use this interval
  EXPECT_FALSE(isDissonantActualInterval(14, 0));  // Major 9th
  EXPECT_FALSE(isDissonantActualInterval(14, 3));  // Major 9th on IV
  EXPECT_FALSE(isDissonantActualInterval(14, 5));  // Major 9th on vi
}

TEST(PitchUtilsTest, IsDissonantActualInterval_VeryWideIntervalsAllowed) {
  // Intervals >= 36 semitones (3 octaves) are allowed
  // Perceptual harshness is sufficiently reduced at this distance
  EXPECT_FALSE(isDissonantActualInterval(36, 0));  // 3 octaves (would be pc=0)
  EXPECT_FALSE(isDissonantActualInterval(37, 0));  // 3 octaves + minor 2nd
  EXPECT_FALSE(isDissonantActualInterval(47, 0));  // 3 octaves + major 7th
  EXPECT_FALSE(isDissonantActualInterval(42, 0));  // 3 octaves + tritone
}

TEST(PitchUtilsTest, IsDissonantActualInterval_RealWorldBassVocalClash) {
  // Real-world test cases from dissonance analysis
  // Bass F3 (53) vs Vocal B4 (71) = 18 semitones (compound tritone)
  // On I chord (C): should be dissonant
  EXPECT_TRUE(isDissonantActualInterval(18, 0));

  // Bass F3 (53) vs Vocal E5 (76) = 23 semitones (compound major 7th)
  // Within 2 octaves: still dissonant (bass defines harmony)
  EXPECT_TRUE(isDissonantActualInterval(23, 0));
  EXPECT_TRUE(isDissonantActualInterval(23, 4));  // Even on V chord

  // Bass G3 (55) vs Vocal B4 (71) = 16 semitones (major 10th)
  // Should NOT be dissonant
  EXPECT_FALSE(isDissonantActualInterval(16, 0));
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

// ============================================================================
// Melodic Interval Constants Tests
// ============================================================================

TEST(PitchUtilsTest, MaxMelodicIntervalValue) {
  // kMaxMelodicInterval should be 9 (Major 6th)
  // This is the maximum singable leap for pop melodies
  EXPECT_EQ(kMaxMelodicInterval, 9);
}

TEST(PitchUtilsTest, MaxMelodicIntervalIsLessThanOctave) {
  // The maximum melodic interval should be less than an octave (12 semitones)
  // to ensure all melodies are singable
  EXPECT_LT(kMaxMelodicInterval, 12);
}

TEST(PitchUtilsTest, MaxMelodicIntervalIsAtLeastPerfectFifth) {
  // The maximum melodic interval should be at least a perfect 5th (7 semitones)
  // to allow expressive melodic leaps
  EXPECT_GE(kMaxMelodicInterval, 7);
}

// ============================================================================
// getMaxMelodicIntervalForSection Tests
// ============================================================================

TEST(PitchUtilsTest, MaxIntervalForSection_Chorus) {
  EXPECT_EQ(getMaxMelodicIntervalForSection(SectionType::Chorus), 12);
}

TEST(PitchUtilsTest, MaxIntervalForSection_Bridge) {
  EXPECT_EQ(getMaxMelodicIntervalForSection(SectionType::Bridge), 14);
}

TEST(PitchUtilsTest, MaxIntervalForSection_PreChorus) {
  EXPECT_EQ(getMaxMelodicIntervalForSection(SectionType::B), 10);
}

TEST(PitchUtilsTest, MaxIntervalForSection_Verse) {
  EXPECT_EQ(getMaxMelodicIntervalForSection(SectionType::A), kMaxMelodicInterval);
}

TEST(PitchUtilsTest, MaxIntervalForSection_MixBreak) {
  EXPECT_EQ(getMaxMelodicIntervalForSection(SectionType::MixBreak), 12);
}

TEST(PitchUtilsTest, MaxIntervalForSection_Drop) {
  EXPECT_EQ(getMaxMelodicIntervalForSection(SectionType::Drop), 12);
}

TEST(PitchUtilsTest, MaxIntervalForSection_Intro) {
  EXPECT_EQ(getMaxMelodicIntervalForSection(SectionType::Intro), kMaxMelodicInterval);
}

// ============================================================================
// clampPitch / clampBass Tests
// ============================================================================

TEST(PitchUtilsTest, ClampPitch_WithinRange) {
  EXPECT_EQ(clampPitch(60, 48, 84), 60);
}

TEST(PitchUtilsTest, ClampPitch_BelowRange) {
  EXPECT_EQ(clampPitch(30, 48, 84), 48);
}

TEST(PitchUtilsTest, ClampPitch_AboveRange) {
  EXPECT_EQ(clampPitch(100, 48, 84), 84);
}

TEST(PitchUtilsTest, ClampBass) {
  EXPECT_EQ(clampBass(20), BASS_LOW);   // Below
  EXPECT_EQ(clampBass(40), 40);         // Within
  EXPECT_EQ(clampBass(70), BASS_HIGH);  // Above
}

// ============================================================================
// Multi-Scale Support Tests
// ============================================================================

TEST(PitchUtilsTest, GetScaleIntervals_Major) {
  const int* s = getScaleIntervals(ScaleType::Major);
  EXPECT_EQ(s[0], 0);
  EXPECT_EQ(s[3], 5);
  EXPECT_EQ(s[6], 11);
}

TEST(PitchUtilsTest, GetScaleIntervals_NaturalMinor) {
  const int* s = getScaleIntervals(ScaleType::NaturalMinor);
  EXPECT_EQ(s[2], 3);  // Minor 3rd
  EXPECT_EQ(s[5], 8);  // Minor 6th
  EXPECT_EQ(s[6], 10); // Minor 7th
}

TEST(PitchUtilsTest, GetScaleIntervals_HarmonicMinor) {
  const int* s = getScaleIntervals(ScaleType::HarmonicMinor);
  EXPECT_EQ(s[2], 3);  // Minor 3rd
  EXPECT_EQ(s[6], 11); // Major 7th (raised)
}

TEST(PitchUtilsTest, GetScaleIntervals_Dorian) {
  const int* s = getScaleIntervals(ScaleType::Dorian);
  EXPECT_EQ(s[2], 3);  // Minor 3rd
  EXPECT_EQ(s[5], 9);  // Major 6th (raised)
}

TEST(PitchUtilsTest, GetScaleIntervals_Mixolydian) {
  const int* s = getScaleIntervals(ScaleType::Mixolydian);
  EXPECT_EQ(s[3], 5);  // Perfect 4th
  EXPECT_EQ(s[6], 10); // Minor 7th (lowered)
}

// ============================================================================
// degreeToPitch Tests
// ============================================================================

TEST(PitchUtilsTest, DegreeToPitch_BasicMajor) {
  // Degree 0 in C major at C4 (60) => C4 (60)
  EXPECT_EQ(degreeToPitch(0, 60, 0), 60);
  // Degree 2 (E) => 60 + 4 = 64
  EXPECT_EQ(degreeToPitch(2, 60, 0), 64);
  // Degree 4 (G) => 60 + 7 = 67
  EXPECT_EQ(degreeToPitch(4, 60, 0), 67);
}

TEST(PitchUtilsTest, DegreeToPitch_OctaveWrap) {
  // Degree 7 = next octave's root
  EXPECT_EQ(degreeToPitch(7, 60, 0), 72);  // C5
}

TEST(PitchUtilsTest, DegreeToPitch_NegativeDegree) {
  // Degree -1 wraps to scale degree 6 (B) in the octave below:
  // d = 6, oct_adjust = -1, result = 60 + (-12) + 11 = 59 (B3)
  EXPECT_EQ(degreeToPitch(-1, 60, 0), 59);
}

TEST(PitchUtilsTest, DegreeToPitch_WithKeyOffset) {
  // Degree 0 in G major (key_offset=7): C4 base + 0 + 7 = 67 (G4)
  EXPECT_EQ(degreeToPitch(0, 60, 7), 67);
}

TEST(PitchUtilsTest, DegreeToPitch_MinorScale) {
  // Degree 2 in natural minor = minor 3rd (3 semitones)
  EXPECT_EQ(degreeToPitch(2, 60, 0, ScaleType::NaturalMinor), 63);
}

// ============================================================================
// pitchToNoteName Tests
// ============================================================================

TEST(PitchUtilsTest, PitchToNoteName_MiddleC) {
  EXPECT_EQ(pitchToNoteName(60), "C4");
}

TEST(PitchUtilsTest, PitchToNoteName_A4) {
  EXPECT_EQ(pitchToNoteName(69), "A4");
}

TEST(PitchUtilsTest, PitchToNoteName_Sharp) {
  EXPECT_EQ(pitchToNoteName(61), "C#4");
}

TEST(PitchUtilsTest, PitchToNoteName_Low) {
  EXPECT_EQ(pitchToNoteName(36), "C2");
}

// ============================================================================
// ChordFunction Tests
// ============================================================================

TEST(PitchUtilsTest, GetChordFunction_Tonic) {
  EXPECT_EQ(getChordFunction(0), ChordFunction::Tonic);   // I
  EXPECT_EQ(getChordFunction(2), ChordFunction::Tonic);   // iii
  EXPECT_EQ(getChordFunction(5), ChordFunction::Tonic);   // vi
}

TEST(PitchUtilsTest, GetChordFunction_Dominant) {
  EXPECT_EQ(getChordFunction(4), ChordFunction::Dominant); // V
  EXPECT_EQ(getChordFunction(6), ChordFunction::Dominant); // vii
}

TEST(PitchUtilsTest, GetChordFunction_Subdominant) {
  EXPECT_EQ(getChordFunction(1), ChordFunction::Subdominant);  // ii
  EXPECT_EQ(getChordFunction(3), ChordFunction::Subdominant);  // IV
  EXPECT_EQ(getChordFunction(10), ChordFunction::Subdominant); // bVII
}

// ============================================================================
// Passaggio Dynamic Tests
// ============================================================================

TEST(PitchUtilsTest, CalculateDynamicPassaggio_StandardRange) {
  // Vocal range: 50-80 (30 semitones)
  // Passaggio: 55% to 75% of range = 50 + 16.5 to 50 + 22.5 => 66-72
  PassaggioRange p = calculateDynamicPassaggio(50, 80);
  EXPECT_GE(p.lower, 64);
  EXPECT_LE(p.upper, 74);
  EXPECT_LT(p.lower, p.upper);
}

TEST(PitchUtilsTest, CalculateDynamicPassaggio_NarrowRange) {
  PassaggioRange p = calculateDynamicPassaggio(60, 72);
  EXPECT_GE(p.lower, 60);
  EXPECT_LE(p.upper, 72);
}

TEST(PitchUtilsTest, IsInPassaggioRange_InRange) {
  // For range 50-80, passaggio ~66-72
  EXPECT_TRUE(isInPassaggioRange(68, 50, 80));
}

TEST(PitchUtilsTest, IsInPassaggioRange_OutOfRange) {
  EXPECT_FALSE(isInPassaggioRange(55, 50, 80));
  EXPECT_FALSE(isInPassaggioRange(78, 50, 80));
}

TEST(PitchUtilsTest, PassaggioRange_Contains) {
  PassaggioRange p{64, 71};
  EXPECT_TRUE(p.contains(64));
  EXPECT_TRUE(p.contains(68));
  EXPECT_TRUE(p.contains(71));
  EXPECT_FALSE(p.contains(63));
  EXPECT_FALSE(p.contains(72));
}

TEST(PitchUtilsTest, PassaggioRange_Center) {
  PassaggioRange p{64, 72};
  EXPECT_EQ(p.center(), 68);
}

TEST(PitchUtilsTest, PassaggioRange_Width) {
  PassaggioRange p{64, 71};
  EXPECT_EQ(p.width(), 7);
}

// ============================================================================
// Avoid Note Tests
// ============================================================================

TEST(PitchUtilsTest, IsAvoidNoteSimple_P4OnMajor) {
  // F (pitch 65) over C major root (60). Interval = 5 (P4). Avoided.
  EXPECT_TRUE(isAvoidNoteSimple(65, 60, false));
}

TEST(PitchUtilsTest, IsAvoidNoteSimple_Minor6OnMinor) {
  // Ab (pitch 68) over C minor root (60). Interval = 8 (m6). Avoided.
  EXPECT_TRUE(isAvoidNoteSimple(68, 60, true));
}

TEST(PitchUtilsTest, IsAvoidNoteSimple_TritoneAlways) {
  // F# (pitch 66) over C root (60). Interval = 6 (tritone). Avoided.
  EXPECT_TRUE(isAvoidNoteSimple(66, 60, false));
  EXPECT_TRUE(isAvoidNoteSimple(66, 60, true));
}

TEST(PitchUtilsTest, IsAvoidNoteSimple_ChordToneNotAvoided) {
  // E (pitch 64) over C major root (60). Interval = 4 (M3). Not avoided.
  EXPECT_FALSE(isAvoidNoteSimple(64, 60, false));
}

TEST(PitchUtilsTest, IsAvoidNoteWithContext_TritoneOnDominant) {
  // Tritone is REQUIRED on V chord, not avoided.
  // F# (66) over G root (67, but we use pitch class). Actually, over C root:
  // degree=4 (V chord). F# over C root = tritone (6). Should NOT be avoided.
  EXPECT_FALSE(isAvoidNoteWithContext(66, 60, false, 4));
}

TEST(PitchUtilsTest, IsAvoidNoteWithContext_TritoneOnTonic) {
  // F# over C root on I chord (degree 0). Should be avoided.
  EXPECT_TRUE(isAvoidNoteWithContext(66, 60, false, 0));
}

// ============================================================================
// transposePitch Tests
// ============================================================================

TEST(PitchUtilsTest, TransposePitch_NoTranspose) {
  EXPECT_EQ(transposePitch(60, Key::C), 60);
}

TEST(PitchUtilsTest, TransposePitch_UpHalfStep) {
  // Key::Db = 1 semitone
  EXPECT_EQ(transposePitch(60, static_cast<Key>(1)), 61);
}

TEST(PitchUtilsTest, TransposePitch_ClampsToMax) {
  EXPECT_EQ(transposePitch(127, static_cast<Key>(5)), 127);
}

TEST(PitchUtilsTest, TransposePitch_ClampsToMin) {
  // Transpose down would go negative (only if key had negative value)
  // Key is uint8_t so always positive; test boundary with max pitch
  EXPECT_EQ(transposePitch(0, Key::C), 0);
}

// ============================================================================
// getPitchClass Tests
// ============================================================================

TEST(PitchUtilsTest, GetPitchClass_MiddleC) {
  // C4 = MIDI 60, pitch class = 0 (C)
  EXPECT_EQ(getPitchClass(60), 0);
}

TEST(PitchUtilsTest, GetPitchClass_AllPitchClasses) {
  // Verify all 12 pitch classes in octave 4
  EXPECT_EQ(getPitchClass(60), 0);   // C
  EXPECT_EQ(getPitchClass(61), 1);   // C#
  EXPECT_EQ(getPitchClass(62), 2);   // D
  EXPECT_EQ(getPitchClass(63), 3);   // D#
  EXPECT_EQ(getPitchClass(64), 4);   // E
  EXPECT_EQ(getPitchClass(65), 5);   // F
  EXPECT_EQ(getPitchClass(66), 6);   // F#
  EXPECT_EQ(getPitchClass(67), 7);   // G
  EXPECT_EQ(getPitchClass(68), 8);   // G#
  EXPECT_EQ(getPitchClass(69), 9);   // A
  EXPECT_EQ(getPitchClass(70), 10);  // A#
  EXPECT_EQ(getPitchClass(71), 11);  // B
}

TEST(PitchUtilsTest, GetPitchClass_OctaveInvariant) {
  // Same pitch class across different octaves
  EXPECT_EQ(getPitchClass(0), 0);    // C-1
  EXPECT_EQ(getPitchClass(12), 0);   // C0
  EXPECT_EQ(getPitchClass(24), 0);   // C1
  EXPECT_EQ(getPitchClass(60), 0);   // C4
  EXPECT_EQ(getPitchClass(72), 0);   // C5
  EXPECT_EQ(getPitchClass(84), 0);   // C6
  EXPECT_EQ(getPitchClass(120), 0);  // C9
}

TEST(PitchUtilsTest, GetPitchClass_BoundaryValues) {
  // MIDI note 0 (lowest)
  EXPECT_EQ(getPitchClass(0), 0);
  // MIDI note 127 (highest) = G9, pitch class 7
  EXPECT_EQ(getPitchClass(127), 7);
}

TEST(PitchUtilsTest, GetPitchClass_ConsistentWithNoteNames) {
  // Verify getPitchClass result indexes into NOTE_NAMES correctly
  for (uint8_t pitch = 0; pitch < 128; ++pitch) {
    int pc = getPitchClass(pitch);
    EXPECT_GE(pc, 0);
    EXPECT_LE(pc, 11);
    // Verify it matches the NOTE_NAMES array indexing used by pitchToNoteName
    std::string name = pitchToNoteName(pitch);
    std::string expected_prefix = NOTE_NAMES[pc];
    EXPECT_EQ(name.substr(0, expected_prefix.size()), expected_prefix)
        << "Mismatch for MIDI pitch " << static_cast<int>(pitch);
  }
}

// ============================================================================
// Unified Dissonance Check Tests (isDissonantSemitoneInterval)
// ============================================================================

// --- Default options (standard Pop theory rules) ---

TEST(UnifiedDissonanceTest, Minor2ndAlwaysDissonant) {
  // Minor 2nd (1 semitone) is always dissonant
  EXPECT_TRUE(isDissonantSemitoneInterval(1));
}

TEST(UnifiedDissonanceTest, Minor9thDissonant) {
  // Minor 9th (13 semitones = compound minor 2nd) is dissonant
  EXPECT_TRUE(isDissonantSemitoneInterval(13));
}

TEST(UnifiedDissonanceTest, CompoundMinor2ndBeyond13NotDissonant) {
  // 25 semitones (m2 + 2 octaves) is too far for perceptual harshness
  EXPECT_FALSE(isDissonantSemitoneInterval(25));
}

TEST(UnifiedDissonanceTest, Major2ndCloseRangeDissonant) {
  // Major 2nd (2 semitones) is dissonant in default (close voicing)
  EXPECT_TRUE(isDissonantSemitoneInterval(2));
}

TEST(UnifiedDissonanceTest, Major9thNotDissonant) {
  // Major 9th (14 semitones) is a common chord extension, NOT dissonant
  EXPECT_FALSE(isDissonantSemitoneInterval(14));
}

TEST(UnifiedDissonanceTest, Major7thDissonant) {
  // Major 7th (11 semitones) is dissonant
  EXPECT_TRUE(isDissonantSemitoneInterval(11));
}

TEST(UnifiedDissonanceTest, CompoundMajor7thDissonant) {
  // Compound M7 at various octaves
  EXPECT_TRUE(isDissonantSemitoneInterval(23));   // M7 + octave
  EXPECT_TRUE(isDissonantSemitoneInterval(35));   // M7 + 2 octaves
}

TEST(UnifiedDissonanceTest, TritoneDissonantByDefault) {
  // Tritone (6 semitones) is dissonant with default options (chord_degree=-1)
  EXPECT_TRUE(isDissonantSemitoneInterval(6));
  EXPECT_TRUE(isDissonantSemitoneInterval(18));   // Compound tritone
}

TEST(UnifiedDissonanceTest, ConsonantIntervalsNotDissonant) {
  // All consonant intervals should not be flagged
  EXPECT_FALSE(isDissonantSemitoneInterval(0));    // Unison
  EXPECT_FALSE(isDissonantSemitoneInterval(3));    // Minor 3rd
  EXPECT_FALSE(isDissonantSemitoneInterval(4));    // Major 3rd
  EXPECT_FALSE(isDissonantSemitoneInterval(5));    // Perfect 4th
  EXPECT_FALSE(isDissonantSemitoneInterval(7));    // Perfect 5th
  EXPECT_FALSE(isDissonantSemitoneInterval(8));    // Minor 6th
  EXPECT_FALSE(isDissonantSemitoneInterval(9));    // Major 6th
  EXPECT_FALSE(isDissonantSemitoneInterval(10));   // Minor 7th
  EXPECT_FALSE(isDissonantSemitoneInterval(12));   // Octave
}

TEST(UnifiedDissonanceTest, WideIntervalCutoff) {
  // Intervals >= 36 semitones (3 octaves) are not dissonant
  EXPECT_FALSE(isDissonantSemitoneInterval(36));   // 3 octaves
  EXPECT_FALSE(isDissonantSemitoneInterval(37));   // 3 octaves + m2
  EXPECT_FALSE(isDissonantSemitoneInterval(42));   // 3 octaves + tritone
  EXPECT_FALSE(isDissonantSemitoneInterval(47));   // 3 octaves + M7
}

TEST(UnifiedDissonanceTest, NegativeIntervalNotDissonant) {
  // Negative intervals should not crash and return false
  EXPECT_FALSE(isDissonantSemitoneInterval(-1));
  EXPECT_FALSE(isDissonantSemitoneInterval(-12));
}

// --- Tritone chord context ---

TEST(UnifiedDissonanceTest, TritoneAllowedOnDominant) {
  DissonanceCheckOptions opts;
  opts.check_tritone = true;
  opts.chord_degree = 4;  // V chord
  EXPECT_FALSE(isDissonantSemitoneInterval(6, opts));
  EXPECT_FALSE(isDissonantSemitoneInterval(18, opts));   // Compound
}

TEST(UnifiedDissonanceTest, TritoneAllowedOnDiminished) {
  DissonanceCheckOptions opts;
  opts.check_tritone = true;
  opts.chord_degree = 6;  // vii chord
  EXPECT_FALSE(isDissonantSemitoneInterval(6, opts));
}

TEST(UnifiedDissonanceTest, TritoneDissonantOnTonic) {
  DissonanceCheckOptions opts;
  opts.check_tritone = true;
  opts.chord_degree = 0;  // I chord
  EXPECT_TRUE(isDissonantSemitoneInterval(6, opts));
}

TEST(UnifiedDissonanceTest, TritoneDissonantOnSubdominant) {
  DissonanceCheckOptions opts;
  opts.check_tritone = true;
  opts.chord_degree = 3;  // IV chord
  EXPECT_TRUE(isDissonantSemitoneInterval(6, opts));
}

TEST(UnifiedDissonanceTest, TritoneAlwaysDissonantWithNegativeDegree) {
  // chord_degree = -1 means no context: treat tritone as always dissonant
  DissonanceCheckOptions opts;
  opts.check_tritone = true;
  opts.chord_degree = -1;
  EXPECT_TRUE(isDissonantSemitoneInterval(6, opts));
}

// --- Major 2nd options ---

TEST(UnifiedDissonanceTest, Major2ndSkippedWhenDisabled) {
  DissonanceCheckOptions opts;
  opts.check_major_2nd = false;
  EXPECT_FALSE(isDissonantSemitoneInterval(2, opts));
}

TEST(UnifiedDissonanceTest, Major2ndWithWiderThreshold) {
  DissonanceCheckOptions opts;
  opts.major_2nd_max_distance = Interval::TWO_OCTAVES;  // 24
  // 2 semitones: dissonant (< 24)
  EXPECT_TRUE(isDissonantSemitoneInterval(2, opts));
  // 14 semitones: major 9th, NOT m2 (pc=2 but actual >= 12, well below 24)
  // Actually 14 % 12 = 2, and 14 < 24, so it should be dissonant with this threshold
  EXPECT_TRUE(isDissonantSemitoneInterval(14, opts));
  // 26 semitones: pc=2, but actual >= 24, so NOT dissonant
  EXPECT_FALSE(isDissonantSemitoneInterval(26, opts));
}

// --- Tritone disabled ---

TEST(UnifiedDissonanceTest, TritoneNotCheckedWhenDisabled) {
  DissonanceCheckOptions opts;
  opts.check_tritone = false;
  EXPECT_FALSE(isDissonantSemitoneInterval(6, opts));
  EXPECT_FALSE(isDissonantSemitoneInterval(18, opts));
  EXPECT_FALSE(isDissonantSemitoneInterval(30, opts));
}

// --- Wide interval cutoff disabled ---

TEST(UnifiedDissonanceTest, WideIntervalCutoffDisabled) {
  DissonanceCheckOptions opts;
  opts.apply_wide_interval_cutoff = false;
  // M7 at 3+ octaves should be dissonant when cutoff is off
  EXPECT_TRUE(isDissonantSemitoneInterval(47, opts));  // M7 + 3 octaves
  // Tritone at 3+ octaves should be dissonant when cutoff is off
  EXPECT_TRUE(isDissonantSemitoneInterval(42, opts));  // Tritone + 3 octaves
}

// --- Factory presets ---

TEST(UnifiedDissonanceTest, StandardPreset) {
  auto opts = DissonanceCheckOptions::standard();
  // Same as default
  EXPECT_TRUE(isDissonantSemitoneInterval(1, opts));    // m2
  EXPECT_TRUE(isDissonantSemitoneInterval(2, opts));    // M2
  EXPECT_TRUE(isDissonantSemitoneInterval(6, opts));    // tritone (no context)
  EXPECT_TRUE(isDissonantSemitoneInterval(11, opts));   // M7
  EXPECT_TRUE(isDissonantSemitoneInterval(13, opts));   // m9
  EXPECT_FALSE(isDissonantSemitoneInterval(7, opts));   // P5
}

TEST(UnifiedDissonanceTest, MinimalClashPreset) {
  auto opts = DissonanceCheckOptions::minimalClash();
  // Only m2/m9 and M7 - no tritone, no M2
  EXPECT_TRUE(isDissonantSemitoneInterval(1, opts));    // m2
  EXPECT_TRUE(isDissonantSemitoneInterval(13, opts));   // m9
  EXPECT_TRUE(isDissonantSemitoneInterval(11, opts));   // M7
  EXPECT_FALSE(isDissonantSemitoneInterval(2, opts));   // M2 skipped
  EXPECT_FALSE(isDissonantSemitoneInterval(6, opts));   // tritone skipped
}

TEST(UnifiedDissonanceTest, CloseVoicingPreset) {
  auto opts = DissonanceCheckOptions::closeVoicing();
  // m2, M7, close M2 - no tritone
  EXPECT_TRUE(isDissonantSemitoneInterval(1, opts));    // m2
  EXPECT_TRUE(isDissonantSemitoneInterval(2, opts));    // M2 close
  EXPECT_TRUE(isDissonantSemitoneInterval(11, opts));   // M7
  EXPECT_FALSE(isDissonantSemitoneInterval(6, opts));   // tritone skipped
  EXPECT_FALSE(isDissonantSemitoneInterval(14, opts));  // M9 not close
}

TEST(UnifiedDissonanceTest, FullWithTritonePreset) {
  auto opts = DissonanceCheckOptions::fullWithTritone();
  // All intervals including tritone (always dissonant, no chord context)
  EXPECT_TRUE(isDissonantSemitoneInterval(1, opts));    // m2
  EXPECT_TRUE(isDissonantSemitoneInterval(2, opts));    // M2
  EXPECT_TRUE(isDissonantSemitoneInterval(6, opts));    // tritone
  EXPECT_TRUE(isDissonantSemitoneInterval(11, opts));   // M7
}

TEST(UnifiedDissonanceTest, VocalClashPreset) {
  auto opts = DissonanceCheckOptions::vocalClash();
  // m2, M7, M2 within 2 octaves - no tritone
  EXPECT_TRUE(isDissonantSemitoneInterval(1, opts));    // m2
  EXPECT_TRUE(isDissonantSemitoneInterval(2, opts));    // M2 (< 24)
  EXPECT_TRUE(isDissonantSemitoneInterval(14, opts));   // M9 (pc=2, < 24)
  EXPECT_TRUE(isDissonantSemitoneInterval(11, opts));   // M7
  EXPECT_FALSE(isDissonantSemitoneInterval(6, opts));   // tritone skipped
  EXPECT_FALSE(isDissonantSemitoneInterval(26, opts));  // M2 compound (>= 24)
}

// --- isDissonantPitchPair convenience function ---

TEST(UnifiedDissonanceTest, PitchPairMinor2nd) {
  // C4 (60) and C#4 (61) = 1 semitone
  EXPECT_TRUE(isDissonantPitchPair(60, 61));
  // B3 (59) and C4 (60) = 1 semitone
  EXPECT_TRUE(isDissonantPitchPair(59, 60));
}

TEST(UnifiedDissonanceTest, PitchPairConsonant) {
  // C4 (60) and E4 (64) = 4 semitones (major 3rd)
  EXPECT_FALSE(isDissonantPitchPair(60, 64));
  // C4 (60) and G4 (67) = 7 semitones (perfect 5th)
  EXPECT_FALSE(isDissonantPitchPair(60, 67));
}

TEST(UnifiedDissonanceTest, PitchPairSymmetric) {
  // Order should not matter
  EXPECT_TRUE(isDissonantPitchPair(60, 61));
  EXPECT_TRUE(isDissonantPitchPair(61, 60));
  EXPECT_FALSE(isDissonantPitchPair(60, 67));
  EXPECT_FALSE(isDissonantPitchPair(67, 60));
}

TEST(UnifiedDissonanceTest, PitchPairWithOptions) {
  auto opts = DissonanceCheckOptions::minimalClash();
  // C4 (60) and D4 (62) = 2 semitones (M2) - skipped in minimalClash
  EXPECT_FALSE(isDissonantPitchPair(60, 62, opts));
  // C4 (60) and C#4 (61) = 1 semitone (m2) - still dissonant
  EXPECT_TRUE(isDissonantPitchPair(60, 61, opts));
}

TEST(UnifiedDissonanceTest, PitchPairUnison) {
  // Same pitch = unison = not dissonant
  EXPECT_FALSE(isDissonantPitchPair(60, 60));
  EXPECT_FALSE(isDissonantPitchPair(72, 72));
}

}  // namespace
}  // namespace midisketch
