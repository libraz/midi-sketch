#include <gtest/gtest.h>
#include "core/chord_utils.h"
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
  int result = nearestChordToneWithinInterval(60, 64, 0, 5, 48, 84, nullptr);
  // C4 is within 5 semitones of E4, and is a chord tone
  EXPECT_EQ(result, 60);
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
  // G4 is a chord tone and within tessitura
  int result = nearestChordToneWithinInterval(67, 64, 0, 7, 48, 84, &t);
  EXPECT_EQ(result, 67);
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
  int result = nearestChordToneWithinInterval(67, 62, 4, 7, 48, 84, nullptr);
  // G4 (67) is a chord tone of V and within 7 semitones of D4 (62)
  EXPECT_EQ(result, 67);
}

}  // namespace
}  // namespace midisketch
