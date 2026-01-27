/**
 * @file hook_utils_test.cpp
 * @brief Tests for hook utilities including skeleton patterns and betrayals.
 */

#include "core/hook_utils.h"

#include <gtest/gtest.h>

#include <random>

namespace midisketch {
namespace {

// ============================================================================
// Skeleton Pattern Tests
// ============================================================================

TEST(HookUtilsTest, GetSkeletonPatternRepeat) {
  auto pattern = getSkeletonPattern(HookSkeleton::Repeat);
  EXPECT_EQ(pattern.length, 3);
  // All intervals should be 0 (same pitch)
  for (size_t i = 0; i < pattern.length; ++i) {
    EXPECT_EQ(pattern.intervals[i], 0);
  }
}

TEST(HookUtilsTest, GetSkeletonPatternAscending) {
  auto pattern = getSkeletonPattern(HookSkeleton::Ascending);
  EXPECT_EQ(pattern.length, 3);
  EXPECT_EQ(pattern.intervals[0], 0);  // Start
  EXPECT_EQ(pattern.intervals[1], 1);  // +1 step
  EXPECT_EQ(pattern.intervals[2], 2);  // +2 steps
}

TEST(HookUtilsTest, GetSkeletonPatternOstinato) {
  // Ostinato: 6-note same-pitch repetition (Ice Cream style)
  auto pattern = getSkeletonPattern(HookSkeleton::Ostinato);
  EXPECT_EQ(pattern.length, 6) << "Ostinato should have 6 notes";

  // All intervals should be 0 (same pitch throughout)
  for (size_t i = 0; i < pattern.length; ++i) {
    EXPECT_EQ(pattern.intervals[i], 0) << "Ostinato interval[" << i << "] should be 0";
  }
}

TEST(HookUtilsTest, GetSkeletonPatternStutterRepeat) {
  auto pattern = getSkeletonPattern(HookSkeleton::StutterRepeat);
  EXPECT_EQ(pattern.length, 5);
  EXPECT_EQ(pattern.intervals[0], 0);     // Note
  EXPECT_EQ(pattern.intervals[1], 0);     // Note
  EXPECT_EQ(pattern.intervals[2], -128);  // Rest marker
  EXPECT_EQ(pattern.intervals[3], 0);     // Note
  EXPECT_EQ(pattern.intervals[4], 0);     // Note
}

TEST(HookUtilsTest, GetSkeletonPatternRhythmRepeat) {
  auto pattern = getSkeletonPattern(HookSkeleton::RhythmRepeat);
  EXPECT_EQ(pattern.length, 5);
  // Check rest markers
  EXPECT_EQ(pattern.intervals[1], -128);  // Rest
  EXPECT_EQ(pattern.intervals[3], -128);  // Rest
}

// ============================================================================
// Skeleton Weights Tests
// ============================================================================

TEST(HookUtilsTest, ChorusSkeletonWeightsHasOstinato) {
  EXPECT_GT(kChorusSkeletonWeights.ostinato, 0.0f)
      << "Chorus weights should have positive ostinato weight";
  EXPECT_GE(kChorusSkeletonWeights.ostinato, 1.5f)
      << "Ostinato should be heavily weighted for chorus (Ice Cream style)";
}

TEST(HookUtilsTest, DefaultSkeletonWeightsHasOstinato) {
  EXPECT_GT(kDefaultSkeletonWeights.ostinato, 0.0f)
      << "Default weights should have positive ostinato weight";
  EXPECT_LT(kDefaultSkeletonWeights.ostinato, kChorusSkeletonWeights.ostinato)
      << "Ostinato should be less weighted outside chorus";
}

// ============================================================================
// Hook Intensity Tests
// ============================================================================

TEST(HookUtilsTest, ApplyHookIntensityMaximumBoostsOstinato) {
  SkeletonWeights result = applyHookIntensityToWeights(kChorusSkeletonWeights, HookIntensity::Maximum);

  // Maximum intensity should heavily boost ostinato
  float base_ostinato = kChorusSkeletonWeights.ostinato;
  EXPECT_GT(result.ostinato, base_ostinato * 2.0f)
      << "Maximum intensity should significantly boost ostinato";
}

TEST(HookUtilsTest, ApplyHookIntensityOffSuppressesOstinato) {
  SkeletonWeights result = applyHookIntensityToWeights(kChorusSkeletonWeights, HookIntensity::Off);

  // Off intensity should suppress ostinato
  float base_ostinato = kChorusSkeletonWeights.ostinato;
  EXPECT_LT(result.ostinato, base_ostinato)
      << "Off intensity should suppress ostinato";
}

TEST(HookUtilsTest, ApplyHookIntensityNormalBoostsOstinato) {
  SkeletonWeights result = applyHookIntensityToWeights(kChorusSkeletonWeights, HookIntensity::Normal);

  // Normal intensity should moderately boost ostinato
  float base_ostinato = kChorusSkeletonWeights.ostinato;
  EXPECT_GT(result.ostinato, base_ostinato)
      << "Normal intensity should boost ostinato";
}

// ============================================================================
// Skeleton Selection Tests
// ============================================================================

TEST(HookUtilsTest, SelectHookSkeletonCanReturnOstinato) {
  // With Maximum intensity, ostinato should be highly likely
  // Run multiple times to check if ostinato can be selected
  std::mt19937 rng(42);  // Fixed seed for reproducibility

  bool found_ostinato = false;
  for (int i = 0; i < 100; ++i) {
    HookSkeleton skeleton = selectHookSkeleton(SectionType::Chorus, rng, HookIntensity::Maximum);
    if (skeleton == HookSkeleton::Ostinato) {
      found_ostinato = true;
      break;
    }
  }

  EXPECT_TRUE(found_ostinato)
      << "Ostinato should be selectable with Maximum intensity in Chorus";
}

TEST(HookUtilsTest, SelectHookSkeletonReturnsValidPattern) {
  std::mt19937 rng(12345);

  for (int i = 0; i < 50; ++i) {
    HookSkeleton skeleton = selectHookSkeleton(SectionType::Chorus, rng, HookIntensity::Normal);
    auto pattern = getSkeletonPattern(skeleton);

    // Pattern should have valid length (1-6 notes)
    EXPECT_GE(pattern.length, 1u) << "Pattern length should be at least 1";
    EXPECT_LE(pattern.length, 6u) << "Pattern length should be at most 6";
  }
}

// ============================================================================
// Scale Degree Conversion Tests
// ============================================================================

TEST(HookUtilsTest, ScaleDegreesToSemitones) {
  // Major scale: C-D-E-F-G-A-B-C = 0-2-4-5-7-9-11-12
  EXPECT_EQ(scaleDegreesToSemitones(0), 0);   // Unison
  EXPECT_EQ(scaleDegreesToSemitones(1), 2);   // Major 2nd
  EXPECT_EQ(scaleDegreesToSemitones(2), 4);   // Major 3rd
  EXPECT_EQ(scaleDegreesToSemitones(3), 5);   // Perfect 4th
  EXPECT_EQ(scaleDegreesToSemitones(4), 7);   // Perfect 5th
  EXPECT_EQ(scaleDegreesToSemitones(5), 9);   // Major 6th
  EXPECT_EQ(scaleDegreesToSemitones(6), 11);  // Major 7th
  EXPECT_EQ(scaleDegreesToSemitones(7), 12);  // Octave
}

TEST(HookUtilsTest, ScaleDegreesToSemitonesNegative) {
  // Negative degrees should mirror positive
  EXPECT_EQ(scaleDegreesToSemitones(-1), -2);   // Down major 2nd
  EXPECT_EQ(scaleDegreesToSemitones(-2), -4);   // Down major 3rd
  EXPECT_EQ(scaleDegreesToSemitones(-3), -5);   // Down perfect 4th
}

// ============================================================================
// Expand Skeleton to Pitches Tests
// ============================================================================

TEST(HookUtilsTest, ExpandOstinatoProducesSamePitch) {
  int base_pitch = 60;  // C4
  uint8_t vocal_low = 55;
  uint8_t vocal_high = 75;

  auto pitches = expandSkeletonToPitches(HookSkeleton::Ostinato, base_pitch, vocal_low, vocal_high);

  EXPECT_EQ(pitches.size(), 6u) << "Ostinato should expand to 6 pitches";

  // All pitches should be the same (base pitch)
  for (const auto& pitch : pitches) {
    EXPECT_EQ(pitch, base_pitch) << "Ostinato pitches should all be the base pitch";
  }
}

TEST(HookUtilsTest, ExpandRepeatPatternProducesSamePitch) {
  int base_pitch = 65;  // F4
  uint8_t vocal_low = 55;
  uint8_t vocal_high = 75;

  auto pitches = expandSkeletonToPitches(HookSkeleton::Repeat, base_pitch, vocal_low, vocal_high);

  EXPECT_EQ(pitches.size(), 3u);
  for (const auto& pitch : pitches) {
    EXPECT_EQ(pitch, base_pitch);
  }
}

// ============================================================================
// Betrayal Tests
// ============================================================================

TEST(HookUtilsTest, SelectBetrayalFirstIsNone) {
  std::mt19937 rng(42);
  HookBetrayal betrayal = selectBetrayal(0, rng);
  EXPECT_EQ(betrayal, HookBetrayal::None) << "First occurrence should have no betrayal";
}

TEST(HookUtilsTest, SelectBetrayalSubsequentNotNone) {
  std::mt19937 rng(42);

  // Check multiple subsequent occurrences
  bool all_none = true;
  for (int i = 1; i <= 10; ++i) {
    HookBetrayal betrayal = selectBetrayal(i, rng);
    if (betrayal != HookBetrayal::None) {
      all_none = false;
      break;
    }
  }

  EXPECT_FALSE(all_none) << "Subsequent occurrences should sometimes have betrayal";
}

}  // namespace
}  // namespace midisketch
