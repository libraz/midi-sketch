/**
 * @file leap_resolution_test.cpp
 * @brief Tests for context-dependent leap resolution probabilities.
 *
 * Verifies that applyLeapReversalRule uses section-type and phrase-position
 * dependent probabilities instead of a single hardcoded value.
 */

#include <gtest/gtest.h>

#include <random>

#include "track/melody/leap_resolution.h"

using namespace midisketch;
using namespace midisketch::melody;

namespace {

// Chord tones for C major triad: C(0), E(4), G(7)
const std::vector<int> kCMajChordTones = {0, 4, 7};

// Run multiple trials to estimate reversal probability.
// Returns fraction of times reversal was applied (0.0-1.0).
float measureReversalRate(int8_t section_type, float phrase_pos, int trials = 2000) {
  int reversal_count = 0;

  for (int i = 0; i < trials; ++i) {
    std::mt19937 rng(static_cast<uint32_t>(i * 7 + 13));

    // Setup: previous leap was +5 (ascending P4), new pitch continues up
    int current_pitch = 67;  // G4
    int prev_interval = 5;   // Previous leap was ascending 5 semitones
    int new_pitch = 69;      // Continuing upward (A4)

    int result = applyLeapReversalRule(new_pitch, current_pitch, prev_interval,
                                        kCMajChordTones, 48, 84,
                                        false, rng, section_type, phrase_pos);

    if (result != new_pitch) {
      reversal_count++;
    }
  }

  return static_cast<float>(reversal_count) / trials;
}

// ============================================================================
// Context-Dependent Reversal Probability
// ============================================================================

TEST(LeapResolutionTest, DefaultProbabilityIs80Percent) {
  // Unknown section type (-1) should use default 80% probability
  float rate = measureReversalRate(-1, -1.0f);
  EXPECT_NEAR(rate, 0.80f, 0.05f);
}

TEST(LeapResolutionTest, VersePhraseEndHigherProbability) {
  // A (Verse, section_type=1) at phrase end (>0.8) should have 95% probability
  float rate = measureReversalRate(1, 0.9f);
  EXPECT_GT(rate, 0.88f);  // Should be near 95%
}

TEST(LeapResolutionTest, VerseBaseProbability) {
  // A (Verse) at mid-phrase should have 85% probability
  float rate = measureReversalRate(1, 0.5f);
  EXPECT_NEAR(rate, 0.85f, 0.05f);
}

TEST(LeapResolutionTest, PreChorusPhraseEndLowerProbability) {
  // B (Pre-chorus, section_type=2) at phrase end should have 70%
  // to maintain forward momentum toward chorus
  float rate = measureReversalRate(2, 0.9f);
  EXPECT_NEAR(rate, 0.70f, 0.06f);
}

TEST(LeapResolutionTest, ChorusBaseProbabilityLower) {
  // Chorus (section_type=3) base probability should be 75%
  // to allow sustained peaks
  float rate = measureReversalRate(3, 0.5f);
  EXPECT_NEAR(rate, 0.75f, 0.05f);
}

TEST(LeapResolutionTest, BridgeHighResolutionProbability) {
  // Bridge (section_type=4) should favor resolution (90%)
  float rate = measureReversalRate(4, 0.5f);
  EXPECT_NEAR(rate, 0.90f, 0.05f);
}

TEST(LeapResolutionTest, PreferStepwiseOverridesSectionType) {
  // When prefer_stepwise=true, probability is always 100%
  // regardless of section type
  int reversal_count = 0;
  int trials = 100;

  for (int i = 0; i < trials; ++i) {
    std::mt19937 rng(static_cast<uint32_t>(i));
    int current_pitch = 67;
    int prev_interval = 5;
    int new_pitch = 69;

    int result = applyLeapReversalRule(new_pitch, current_pitch, prev_interval,
                                        kCMajChordTones, 48, 84,
                                        true, rng, 3, 0.5f);  // Chorus, mid-phrase

    if (result != new_pitch) {
      reversal_count++;
    }
  }

  // All should be reversed when prefer_stepwise=true
  EXPECT_EQ(reversal_count, trials);
}

TEST(LeapResolutionTest, SmallIntervalSkipsReversal) {
  // Intervals < kLeapReversalThreshold (4) should not trigger reversal
  std::mt19937 rng(42);
  int result = applyLeapReversalRule(65, 64, 3, kCMajChordTones, 48, 84,
                                      false, rng, 1, 0.5f);
  EXPECT_EQ(result, 65);  // No reversal for small intervals
}

}  // namespace
