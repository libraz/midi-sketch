/**
 * @file chord_utils_test.cpp
 * @brief Tests for chord utilities.
 */

#include "core/chord_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <set>
#include <vector>

#include "core/chord.h"
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
  TessituraRange t{60, 72, 66, 55, 77};

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

// ============================================================================
// findNearestChordToneInRange Tests
// ============================================================================

TEST(FindNearestChordToneInRangeTest, NearestToNonChordTone) {
  // C#4 (61) is not a chord tone of I (C, E, G)
  // Nearest chord tones: C4 (60, dist=1) or E4 (64, dist=3)
  int result = findNearestChordToneInRange(61, 0, 48, 84);
  EXPECT_EQ(result, 60);  // C4 is closest
}

TEST(FindNearestChordToneInRangeTest, ExactChordTone) {
  // C4 (60) is already a chord tone of I chord
  int result = findNearestChordToneInRange(60, 0, 48, 84);
  EXPECT_EQ(result, 60);
}

TEST(FindNearestChordToneInRangeTest, RangeForcesHigherTone) {
  // C4 (60) is a chord tone but range_low=64 excludes it
  // Lowest chord tone in range: E4 (64)
  int result = findNearestChordToneInRange(60, 0, 64, 84);
  EXPECT_EQ(result, 64);
}

TEST(FindNearestChordToneInRangeTest, VChordRootIsChordTone) {
  // G4 (67) is root of V chord (G, B, D)
  int result = findNearestChordToneInRange(67, 4, 48, 84);
  EXPECT_EQ(result, 67);
}

TEST(FindNearestChordToneInRangeTest, NearestVChordTone) {
  // G#4 (68) is not a chord tone of V (G=7, B=11, D=2)
  // Nearest: G4 (67, dist=1) or B4 (71, dist=3)
  int result = findNearestChordToneInRange(68, 4, 48, 84);
  EXPECT_EQ(result, 67);  // G4 is closest
}

TEST(FindNearestChordToneInRangeTest, ResultWithinRange) {
  // Verify result is always within [range_low, range_high]
  for (int degree = 0; degree < 7; ++degree) {
    for (int pitch = 48; pitch <= 84; ++pitch) {
      int result = findNearestChordToneInRange(pitch, static_cast<int8_t>(degree), 48, 84);
      EXPECT_GE(result, 48) << "degree=" << degree << " pitch=" << pitch;
      EXPECT_LE(result, 84) << "degree=" << degree << " pitch=" << pitch;
    }
  }
}

// ============================================================================
// Borrowed-chord root correctness (regression tests)
// ============================================================================
//
// Confirmed degree -> root semitone (C major) and chord-tone pitch-class sets
// via degreeToSemitone() and buildChord() in chord.cpp:
//
//   Degree  Name    root_pc  Quality  Intervals  ChordTones (PCs)
//   0       I       0  (C)   Major    0,4,7      {0,4,7}
//   1       ii      2  (D)   Minor    0,3,7      {2,5,9}
//   2       iii     4  (E)   Minor    0,3,7      {4,7,11}
//   3       IV      5  (F)   Major    0,4,7      {5,9,0}
//   4       V       7  (G)   Major    0,4,7      {7,11,2}
//   5       vi      9  (A)   Minor    0,3,7      {9,0,4}
//   6       vii     11 (B)   Dim      0,3,6      {11,2,5}
//   8       bVI     8  (Ab)  Major    0,4,7      {8,0,3}
//   10      bVII    10 (Bb)  Major    0,4,7      {10,2,5}
//   11      bIII    3  (Eb)  Major    0,4,7      {3,7,10}
//   12      iv      5  (F)   Minor    0,3,7      {5,8,0}
//   13      bII     1  (Db)  Major    0,4,7      {1,5,8}
//   14      #IVdim  6  (F#)  Dim      0,3,6      {6,9,0}

namespace {

// All chord degrees the codebase supports.
const std::vector<int8_t> kAllDegrees = {0, 1, 2, 3, 4, 5, 6, 8, 10, 11, 12, 13, 14};

std::set<int> toSet(const std::vector<int>& v) { return std::set<int>(v.begin(), v.end()); }

}  // namespace

TEST(ChordUtilsBorrowedTest, GetChordTonePitchClassesAllDiatonic) {
  // Diatonic degrees 0-6: verify unchanged, correct pitch-class sets.
  EXPECT_EQ(toSet(getChordTonePitchClasses(0)), (std::set<int>{0, 4, 7}));   // I  : C E G
  EXPECT_EQ(toSet(getChordTonePitchClasses(1)), (std::set<int>{2, 5, 9}));   // ii : D F A
  EXPECT_EQ(toSet(getChordTonePitchClasses(2)), (std::set<int>{4, 7, 11}));  // iii: E G B
  EXPECT_EQ(toSet(getChordTonePitchClasses(3)), (std::set<int>{5, 9, 0}));   // IV : F A C
  EXPECT_EQ(toSet(getChordTonePitchClasses(4)), (std::set<int>{7, 11, 2}));  // V  : G B D
  EXPECT_EQ(toSet(getChordTonePitchClasses(5)), (std::set<int>{9, 0, 4}));   // vi : A C E
  EXPECT_EQ(toSet(getChordTonePitchClasses(6)), (std::set<int>{11, 2, 5}));  // vii: B D F
}

TEST(ChordUtilsBorrowedTest, GetChordTonePitchClassesAllBorrowed) {
  // Borrowed degrees must use the correct chromatic root, not %7 collapse.
  EXPECT_EQ(toSet(getChordTonePitchClasses(8)), (std::set<int>{8, 0, 3}));    // bVI  : Ab C Eb
  EXPECT_EQ(toSet(getChordTonePitchClasses(10)), (std::set<int>{10, 2, 5}));  // bVII : Bb D F
  EXPECT_EQ(toSet(getChordTonePitchClasses(11)), (std::set<int>{3, 7, 10}));  // bIII : Eb G Bb
  EXPECT_EQ(toSet(getChordTonePitchClasses(12)), (std::set<int>{5, 8, 0}));   // iv   : F Ab C
  EXPECT_EQ(toSet(getChordTonePitchClasses(13)), (std::set<int>{1, 5, 8}));   // bII  : Db F Ab
  EXPECT_EQ(toSet(getChordTonePitchClasses(14)), (std::set<int>{6, 9, 0}));   // #IVdim: F# A C
}

TEST(ChordUtilsBorrowedTest, GetChordTonePitchClassesMatchesGetChordTones) {
  // getChordTonePitchClasses(d) must equal pitch classes of getChordTones(d)
  // for ALL supported degrees (root computation must be consistent).
  for (int8_t d : kAllDegrees) {
    ChordTones ct = getChordTones(d);
    std::vector<int> from_struct;
    for (uint8_t i = 0; i < ct.count; ++i) {
      from_struct.push_back(ct.pitch_classes[i]);
    }
    EXPECT_EQ(toSet(getChordTonePitchClasses(d)), toSet(from_struct))
        << "Mismatch for degree " << static_cast<int>(d);
  }
}

TEST(ChordUtilsBorrowedTest, GetAvailableTensionRootCorrectness) {
  // bIII root = Eb (3). 9th above Eb = F (5). Tensions must be rooted on Eb,
  // not on the wrong %7-collapsed root.
  auto tens_biii = getAvailableTensionPitchClasses(11);
  ASSERT_FALSE(tens_biii.empty());
  EXPECT_NE(std::find(tens_biii.begin(), tens_biii.end(), 5), tens_biii.end())
      << "bIII (Eb major) should expose 9th = F (5)";

  // bVII root = Bb (10). 9th above Bb = C (0).
  auto tens_bvii = getAvailableTensionPitchClasses(10);
  ASSERT_FALSE(tens_bvii.empty());
  EXPECT_NE(std::find(tens_bvii.begin(), tens_bvii.end(), 0), tens_bvii.end())
      << "bVII (Bb major) should expose 9th = C (0)";

  // bVI root = Ab (8). 9th above Ab = Bb (10).
  auto tens_bvi = getAvailableTensionPitchClasses(8);
  ASSERT_FALSE(tens_bvi.empty());
  EXPECT_NE(std::find(tens_bvi.begin(), tens_bvi.end(), 10), tens_bvi.end())
      << "bVI (Ab major) should expose 9th = Bb (10)";
}

TEST(ChordUtilsBorrowedTest, GuideToneViiSeventhIsADiatonic) {
  // vii (degree 6): guide tones are 3rd (D=2) and 7th. The diatonic 7th above
  // B in C major is A (9), giving Bm7b5 (half-diminished). Must NOT be Bb (10).
  auto guides = getGuideTonePitchClasses(6);
  ASSERT_EQ(guides.size(), 2u);
  EXPECT_NE(std::find(guides.begin(), guides.end(), 9), guides.end())
      << "vii guide 7th should be A (9), diatonic half-diminished";
  EXPECT_EQ(std::find(guides.begin(), guides.end(), 10), guides.end())
      << "vii guide 7th must NOT be Bb (10), which is non-diatonic";
  // 3rd of B dim = D (2)
  EXPECT_NE(std::find(guides.begin(), guides.end(), 2), guides.end());
}

TEST(ChordUtilsBorrowedTest, GuideToneDiatonicSevenths) {
  // I: 3rd=E(4), maj7=B(11)
  EXPECT_EQ(toSet(getGuideTonePitchClasses(0)), (std::set<int>{4, 11}));
  // IV: 3rd=A(9), maj7=E(4)
  EXPECT_EQ(toSet(getGuideTonePitchClasses(3)), (std::set<int>{9, 4}));
  // V: 3rd=B(11), min7=F(5)  -> G7
  EXPECT_EQ(toSet(getGuideTonePitchClasses(4)), (std::set<int>{11, 5}));
  // ii: 3rd=F(5), min7=C(0)  -> Dm7
  EXPECT_EQ(toSet(getGuideTonePitchClasses(1)), (std::set<int>{5, 0}));
}

TEST(ChordUtilsBorrowedTest, GuideToneBorrowedRootCorrectness) {
  // bIII (Eb major): 3rd = G (7). Root must be Eb, so 3rd is G not something else.
  auto g_biii = getGuideTonePitchClasses(11);
  ASSERT_EQ(g_biii.size(), 2u);
  EXPECT_NE(std::find(g_biii.begin(), g_biii.end(), 7), g_biii.end()) << "bIII 3rd should be G (7)";
}

}  // namespace
}  // namespace midisketch
