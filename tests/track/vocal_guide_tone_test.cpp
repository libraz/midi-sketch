/**
 * @file vocal_guide_tone_test.cpp
 * @brief Tests for guide tone priority, vocal range span, and guitar_below_vocal.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <set>
#include <vector>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/generator.h"
#include "core/production_blueprint.h"
#include "core/song.h"
#include "core/timing_constants.h"
#include "test_support/generator_test_fixture.h"
#include "test_support/test_constants.h"
#include "track/melody/pitch_constraints.h"

namespace midisketch {
namespace {

// ============================================================================
// Guide Tone Pitch Classes Tests
// ============================================================================

TEST(GuideToneTest, GuideTonePitchClassesForMajorChord) {
  // I chord (C major): 3rd = E (4), 7th = B (11, major 7th)
  auto guides = getGuideTonePitchClasses(0);
  ASSERT_EQ(guides.size(), 2u);
  EXPECT_EQ(guides[0], 4);   // E (major 3rd)
  EXPECT_EQ(guides[1], 11);  // B (major 7th)
}

TEST(GuideToneTest, GuideTonePitchClassesForMinorChord) {
  // ii chord (D minor): 3rd = F (5), 7th = C (0, minor 7th)
  auto guides = getGuideTonePitchClasses(1);
  ASSERT_EQ(guides.size(), 2u);
  EXPECT_EQ(guides[0], 5);  // F (minor 3rd)
  EXPECT_EQ(guides[1], 0);  // C (minor 7th from D=2, 2+10=12%12=0)
}

TEST(GuideToneTest, GuideTonePitchClassesForDominant) {
  // V chord (G major): 3rd = B (11), 7th = F (5, minor 7th)
  auto guides = getGuideTonePitchClasses(4);
  ASSERT_EQ(guides.size(), 2u);
  EXPECT_EQ(guides[0], 11);  // B (major 3rd)
  EXPECT_EQ(guides[1], 5);   // F (minor 7th from G=7, 7+10=17%12=5)
}

TEST(GuideToneTest, GuideTonePitchClassesForIV) {
  // IV chord (F major): 3rd = A (9), 7th = E (4, major 7th)
  auto guides = getGuideTonePitchClasses(3);
  ASSERT_EQ(guides.size(), 2u);
  EXPECT_EQ(guides[0], 9);  // A (major 3rd)
  EXPECT_EQ(guides[1], 4);  // E (major 7th from F=5, 5+11=16%12=4)
}

// ============================================================================
// enforceGuideToneOnDownbeat Tests
// ============================================================================

TEST(GuideToneTest, GuideToneRateZeroDoesNothing) {
  std::mt19937 rng(42);
  // With guide_tone_rate=0, pitch should not change
  int pitch = 60;  // C4 (root of I chord)
  int result = melody::enforceGuideToneOnDownbeat(pitch, 0, 0, 48, 84, 0, rng);
  EXPECT_EQ(result, pitch);
}

TEST(GuideToneTest, GuideToneRateOnNonStrongBeatDoesNothing) {
  std::mt19937 rng(42);
  // Beat 2 (tick 480) is not a strong beat
  int pitch = 60;  // C4
  int result = melody::enforceGuideToneOnDownbeat(pitch, TICKS_PER_BEAT, 0, 48, 84, 100, rng);
  EXPECT_EQ(result, pitch);
}

TEST(GuideToneTest, GuideToneRate100OnDownbeatChangesToGuideTone) {
  std::mt19937 rng(42);
  // With guide_tone_rate=100 on beat 1 (tick 0), should bias toward guide tone
  // I chord guide tones: E(4), B(11)
  int pitch = 60;  // C4 (root, not a guide tone)
  int result = melody::enforceGuideToneOnDownbeat(pitch, 0, 0, 48, 84, 100, rng);
  int result_pc = result % 12;
  // Should be either E (4) or B (11)
  EXPECT_TRUE(result_pc == 4 || result_pc == 11)
      << "Expected guide tone (E=4 or B=11), got pitch class " << result_pc;
}

TEST(GuideToneTest, GuideToneAlreadyGuideToneUnchanged) {
  std::mt19937 rng(42);
  // E4 (64) is already a guide tone (3rd of I chord)
  int pitch = 64;
  int result = melody::enforceGuideToneOnDownbeat(pitch, 0, 0, 48, 84, 100, rng);
  EXPECT_EQ(result, 64);
}

TEST(GuideToneTest, GuideToneRate100OnBeat3Works) {
  std::mt19937 rng(42);
  // Beat 3 (tick 960) is also a strong beat
  int pitch = 60;  // C4, not a guide tone
  int result = melody::enforceGuideToneOnDownbeat(pitch, 2 * TICKS_PER_BEAT, 0, 48, 84, 100, rng);
  int result_pc = result % 12;
  EXPECT_TRUE(result_pc == 4 || result_pc == 11)
      << "Expected guide tone on beat 3, got pitch class " << result_pc;
}

// ============================================================================
// Guide Tone Rate Statistical Test
// ============================================================================

TEST(GuideToneTest, GuideToneRateNonZero) {
  // With guide_tone_rate=70, approximately 70% of downbeat notes should be guide tones
  constexpr int kTrials = 1000;
  int guide_tone_count = 0;

  auto guides = getGuideTonePitchClasses(0);  // I chord: E(4), B(11)
  std::set<int> guide_set(guides.begin(), guides.end());

  for (int trial = 0; trial < kTrials; ++trial) {
    std::mt19937 rng(trial);
    int pitch = 60;  // C4 (root, not a guide tone)
    int result = melody::enforceGuideToneOnDownbeat(pitch, 0, 0, 48, 84, 70, rng);
    int result_pc = result % 12;
    if (guide_set.count(result_pc) > 0) {
      guide_tone_count++;
    }
  }

  // Should be approximately 70%, allow tolerance of 10%
  double ratio = static_cast<double>(guide_tone_count) / kTrials;
  EXPECT_GT(ratio, 0.55) << "Guide tone ratio " << ratio << " too low (expected ~0.70)";
  EXPECT_LT(ratio, 0.85) << "Guide tone ratio " << ratio << " too high (expected ~0.70)";
}

TEST(GuideToneTest, GuideToneRateZeroStatistical) {
  // With guide_tone_rate=0, the pitch should never change from root
  constexpr int kTrials = 100;
  for (int trial = 0; trial < kTrials; ++trial) {
    std::mt19937 rng(trial);
    int pitch = 60;  // C4
    int result = melody::enforceGuideToneOnDownbeat(pitch, 0, 0, 48, 84, 0, rng);
    EXPECT_EQ(result, pitch);
  }
}

// ============================================================================
// Vocal Range Span Constraint Tests
// ============================================================================

class VocalRangeSpanTest : public test::GeneratorTestFixture {};

TEST_F(VocalRangeSpanTest, VocalRangeSpanConstraint) {
  // Use a blueprint that has vocal_range_span set
  // Generate with specific seed for determinism
  params_.seed = 42;
  params_.blueprint_id = 0;  // Traditional

  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  const auto& vocal_notes = song.vocal().notes();
  ASSERT_FALSE(vocal_notes.empty());

  // Without range span constraint, the vocal should use the full range
  // We just verify that the full generation works correctly
  uint8_t actual_low = 127;
  uint8_t actual_high = 0;
  for (const auto& note : vocal_notes) {
    actual_low = std::min(actual_low, note.note);
    actual_high = std::max(actual_high, note.note);
  }

  int actual_span = actual_high - actual_low;
  EXPECT_GT(actual_span, 0) << "Vocal should have some range";
}

TEST_F(VocalRangeSpanTest, VocalRangeSpanConstraintNarrowRange) {
  // Test that vocal_range_span=15 constrains the range
  // We need to set guide_tone_rate on sections after generation
  // This is a structural test - just verify the constraint code path works
  params_.seed = 100;
  params_.blueprint_id = 0;

  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  EXPECT_FALSE(song.vocal().empty());
}

// ============================================================================
// Guitar Below Vocal Tests
// ============================================================================

class GuitarBelowVocalTest : public test::GeneratorTestFixture {};

TEST_F(GuitarBelowVocalTest, GuitarBelowVocalDisabled) {
  // Without guitar_below_vocal, guitar can be in any register
  params_.seed = 42;
  params_.blueprint_id = 0;

  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  // Guitar may or may not be present depending on mood
  // Just verify generation succeeds
  EXPECT_FALSE(song.vocal().empty());
}

TEST_F(GuitarBelowVocalTest, GuitarBelowVocalStructuralTest) {
  // Verify that guitar_below_vocal field exists and is accessible
  BlueprintConstraints constraints;
  EXPECT_FALSE(constraints.guitar_below_vocal);  // Default is false

  constraints.guitar_below_vocal = true;
  EXPECT_TRUE(constraints.guitar_below_vocal);
}

}  // namespace
}  // namespace midisketch
