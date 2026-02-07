/**
 * @file mora_rhythm_test.cpp
 * @brief Tests for mora-timed rhythm generation and mode resolution.
 */

#include "track/melody/rhythm_generator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <set>

#include "core/melody_types.h"

namespace midisketch {
namespace melody {
namespace {

// ============================================================================
// resolveMoraMode tests
// ============================================================================

TEST(ResolveMoraModeTest, ExplicitStandardReturnsStandard) {
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::Standard, VocalStylePreset::Standard),
            MoraRhythmMode::Standard);
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::Standard, VocalStylePreset::Rock),
            MoraRhythmMode::Standard);
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::Standard, VocalStylePreset::Idol),
            MoraRhythmMode::Standard);
}

TEST(ResolveMoraModeTest, ExplicitMoraTimedReturnsMoraTimed) {
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::MoraTimed, VocalStylePreset::Rock),
            MoraRhythmMode::MoraTimed);
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::MoraTimed, VocalStylePreset::UltraVocaloid),
            MoraRhythmMode::MoraTimed);
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::MoraTimed, VocalStylePreset::PowerfulShout),
            MoraRhythmMode::MoraTimed);
}

TEST(ResolveMoraModeTest, AutoResolvesToStandardForStressTimedStyles) {
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::Auto, VocalStylePreset::Rock),
            MoraRhythmMode::Standard);
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::Auto, VocalStylePreset::CityPop),
            MoraRhythmMode::Standard);
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::Auto, VocalStylePreset::UltraVocaloid),
            MoraRhythmMode::Standard);
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::Auto, VocalStylePreset::PowerfulShout),
            MoraRhythmMode::Standard);
}

TEST(ResolveMoraModeTest, AutoResolvesToMoraTimedForJPopStyles) {
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::Auto, VocalStylePreset::Standard),
            MoraRhythmMode::MoraTimed);
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::Auto, VocalStylePreset::Idol),
            MoraRhythmMode::MoraTimed);
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::Auto, VocalStylePreset::Anime),
            MoraRhythmMode::MoraTimed);
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::Auto, VocalStylePreset::Vocaloid),
            MoraRhythmMode::MoraTimed);
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::Auto, VocalStylePreset::KPop),
            MoraRhythmMode::MoraTimed);
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::Auto, VocalStylePreset::BrightKira),
            MoraRhythmMode::MoraTimed);
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::Auto, VocalStylePreset::CuteAffected),
            MoraRhythmMode::MoraTimed);
}

TEST(ResolveMoraModeTest, AutoWithBallad) {
  // Ballad is not explicitly in either list; falls through to MoraTimed (default)
  EXPECT_EQ(resolveMoraMode(MoraRhythmMode::Auto, VocalStylePreset::Ballad),
            MoraRhythmMode::MoraTimed);
}

// ============================================================================
// generateMoraTimedRhythm - edge cases
// ============================================================================

TEST(MoraTimedRhythmTest, ZeroPhraseBeatsReturnsEmpty) {
  std::mt19937 rng(42);
  auto result = generateMoraTimedRhythm(0, 8, 1.0f, rng);
  EXPECT_TRUE(result.empty());
}

TEST(MoraTimedRhythmTest, ZeroTargetCountReturnsEmpty) {
  std::mt19937 rng(42);
  auto result = generateMoraTimedRhythm(4, 0, 1.0f, rng);
  EXPECT_TRUE(result.empty());
}

TEST(MoraTimedRhythmTest, BothZeroReturnsEmpty) {
  std::mt19937 rng(42);
  auto result = generateMoraTimedRhythm(0, 0, 1.0f, rng);
  EXPECT_TRUE(result.empty());
}

// ============================================================================
// generateMoraTimedRhythm - basic properties
// ============================================================================

TEST(MoraTimedRhythmTest, ProducesNonEmptyForValidInput) {
  std::mt19937 rng(42);
  auto result = generateMoraTimedRhythm(4, 8, 1.0f, rng);
  EXPECT_FALSE(result.empty());
}

TEST(MoraTimedRhythmTest, MinimumTwoNotesWithLowDensity) {
  // Even with very low density modifier, at least 2 notes are generated
  std::mt19937 rng(42);
  auto result = generateMoraTimedRhythm(4, 2, 0.1f, rng);
  EXPECT_GE(result.size(), 2u);
}

TEST(MoraTimedRhythmTest, AllNotesWithinPhraseBounds) {
  std::mt19937 rng(42);
  uint8_t phrase_beats = 8;
  auto result = generateMoraTimedRhythm(phrase_beats, 12, 1.0f, rng);

  float end_beat = static_cast<float>(phrase_beats);
  for (const auto& note : result) {
    EXPECT_GE(note.beat, 0.0f) << "Note starts before phrase";
    EXPECT_LT(note.beat, end_beat) << "Note starts after phrase end";
  }
}

TEST(MoraTimedRhythmTest, NotesInChronologicalOrder) {
  std::mt19937 rng(42);
  auto result = generateMoraTimedRhythm(4, 8, 1.0f, rng);

  for (size_t idx = 1; idx < result.size(); ++idx) {
    EXPECT_GE(result[idx].beat, result[idx - 1].beat)
        << "Note at index " << idx << " is before previous note";
  }
}

TEST(MoraTimedRhythmTest, AllDurationsPositive) {
  std::mt19937 rng(42);
  auto result = generateMoraTimedRhythm(4, 8, 1.0f, rng);

  for (size_t idx = 0; idx < result.size(); ++idx) {
    EXPECT_GT(result[idx].eighths, 0.0f)
        << "Note at index " << idx << " has non-positive duration";
  }
}

// ============================================================================
// generateMoraTimedRhythm - word group structure
// ============================================================================

TEST(MoraTimedRhythmTest, HasWordGroupAccents) {
  // At least some notes should have strong=true (first mora of word groups)
  std::mt19937 rng(42);
  auto result = generateMoraTimedRhythm(4, 8, 1.0f, rng);

  int strong_count = 0;
  for (const auto& note : result) {
    if (note.strong) strong_count++;
  }
  EXPECT_GE(strong_count, 1) << "No word group accents found";
}

TEST(MoraTimedRhythmTest, FirstNoteIsAccented) {
  // The first note should always be the start of a word group
  std::mt19937 rng(42);
  auto result = generateMoraTimedRhythm(4, 8, 1.0f, rng);

  ASSERT_FALSE(result.empty());
  EXPECT_TRUE(result[0].strong) << "First note should be accented (first mora of first group)";
}

// ============================================================================
// generateMoraTimedRhythm - phrase-ending extension
// ============================================================================

TEST(MoraTimedRhythmTest, LastNoteHasExtendedDuration) {
  // The last note gets 1.5x-2x extension. Use a longer phrase with fewer
  // notes so there is room for the extension to be visible.
  // Test across multiple seeds: at least some should show extension.
  int extended_count = 0;
  constexpr int kTrials = 20;

  for (int seed = 0; seed < kTrials; ++seed) {
    std::mt19937 rng(static_cast<uint32_t>(seed));
    auto result = generateMoraTimedRhythm(8, 6, 1.0f, rng);
    if (result.size() < 3) continue;

    // Compute median duration of non-last notes
    std::vector<float> durations;
    for (size_t idx = 0; idx + 1 < result.size(); ++idx) {
      durations.push_back(result[idx].eighths);
    }
    std::sort(durations.begin(), durations.end());
    float median = durations[durations.size() / 2];

    if (result.back().eighths > median) {
      extended_count++;
    }
  }

  // At least 25% of trials should show last note longer than median
  EXPECT_GE(extended_count, kTrials / 4)
      << "Phrase-ending extension should make last note longer in most cases";
}

// ============================================================================
// generateMoraTimedRhythm - density modifier
// ============================================================================

TEST(MoraTimedRhythmTest, HigherDensityProducesMoreNotes) {
  std::mt19937 rng_low(42);
  std::mt19937 rng_high(42);

  auto low_density = generateMoraTimedRhythm(4, 8, 0.5f, rng_low);
  auto high_density = generateMoraTimedRhythm(4, 8, 2.0f, rng_high);

  // Higher density should produce at least as many notes
  EXPECT_GE(high_density.size(), low_density.size());
}

// ============================================================================
// generateMoraTimedRhythm - melisma avoidance
// ============================================================================

TEST(MoraTimedRhythmTest, NoThreeConsecutiveVeryShortNotes) {
  // Verify the melisma avoidance post-processing works:
  // No 3+ consecutive notes with duration < 0.5 eighths (16th note)
  for (uint32_t seed = 0; seed < 50; ++seed) {
    std::mt19937 rng(seed);
    auto result = generateMoraTimedRhythm(4, 16, 1.0f, rng);

    int consecutive_short = 0;
    for (const auto& note : result) {
      if (note.eighths < 0.5f) {
        consecutive_short++;
        EXPECT_LT(consecutive_short, 3)
            << "Found 3+ consecutive very short notes with seed=" << seed;
      } else {
        consecutive_short = 0;
      }
    }
  }
}

// ============================================================================
// generateMoraTimedRhythm - seed reproducibility
// ============================================================================

TEST(MoraTimedRhythmTest, SameSeedProducesSameOutput) {
  std::mt19937 rng1(123);
  std::mt19937 rng2(123);

  auto result1 = generateMoraTimedRhythm(4, 8, 1.0f, rng1);
  auto result2 = generateMoraTimedRhythm(4, 8, 1.0f, rng2);

  ASSERT_EQ(result1.size(), result2.size());
  for (size_t idx = 0; idx < result1.size(); ++idx) {
    EXPECT_FLOAT_EQ(result1[idx].beat, result2[idx].beat);
    EXPECT_FLOAT_EQ(result1[idx].eighths, result2[idx].eighths);
    EXPECT_EQ(result1[idx].strong, result2[idx].strong);
  }
}

TEST(MoraTimedRhythmTest, DifferentSeedsProduceDifferentOutput) {
  std::mt19937 rng1(42);
  std::mt19937 rng2(999);

  auto result1 = generateMoraTimedRhythm(4, 8, 1.0f, rng1);
  auto result2 = generateMoraTimedRhythm(4, 8, 1.0f, rng2);

  // At least note count or positions should differ
  bool differ = (result1.size() != result2.size());
  if (!differ) {
    for (size_t idx = 0; idx < result1.size(); ++idx) {
      if (std::abs(result1[idx].beat - result2[idx].beat) > 0.01f ||
          std::abs(result1[idx].eighths - result2[idx].eighths) > 0.01f) {
        differ = true;
        break;
      }
    }
  }
  EXPECT_TRUE(differ) << "Different seeds should produce different rhythm patterns";
}

// ============================================================================
// generateMoraTimedRhythm - various phrase lengths
// ============================================================================

TEST(MoraTimedRhythmTest, WorksWithShortPhrase) {
  std::mt19937 rng(42);
  auto result = generateMoraTimedRhythm(2, 4, 1.0f, rng);
  EXPECT_GE(result.size(), 2u);
  for (const auto& note : result) {
    EXPECT_LT(note.beat, 2.0f);
  }
}

TEST(MoraTimedRhythmTest, WorksWithLongPhrase) {
  std::mt19937 rng(42);
  auto result = generateMoraTimedRhythm(16, 24, 1.0f, rng);
  EXPECT_GE(result.size(), 4u);
  for (const auto& note : result) {
    EXPECT_LT(note.beat, 16.0f);
  }
}

TEST(MoraTimedRhythmTest, SingleBeatPhrase) {
  std::mt19937 rng(42);
  auto result = generateMoraTimedRhythm(1, 2, 1.0f, rng);
  EXPECT_GE(result.size(), 1u);
}

// ============================================================================
// generateMoraTimedRhythm - stress test across many seeds
// ============================================================================

TEST(MoraTimedRhythmTest, StressTestNoCrash) {
  // Run with 100 seeds to verify no crashes or assertion failures
  for (uint32_t seed = 0; seed < 100; ++seed) {
    std::mt19937 rng(seed);
    auto result = generateMoraTimedRhythm(4, 8, 1.0f, rng);
    EXPECT_FALSE(result.empty()) << "Empty result for seed=" << seed;

    // Verify basic invariants
    for (size_t idx = 0; idx < result.size(); ++idx) {
      EXPECT_GE(result[idx].beat, 0.0f);
      EXPECT_GT(result[idx].eighths, 0.0f);
    }
  }
}

}  // namespace
}  // namespace melody
}  // namespace midisketch
