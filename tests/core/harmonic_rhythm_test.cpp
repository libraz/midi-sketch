/**
 * @file harmonic_rhythm_test.cpp
 * @brief Tests for harmonic rhythm density conversion and section-based lookup.
 */

#include "core/harmonic_rhythm.h"

#include <gtest/gtest.h>

#include "core/types.h"

namespace midisketch {
namespace {

// ============================================================================
// C3: harmonicRhythmToDensity - float to enum conversion
// ============================================================================

TEST(HarmonicRhythmTest, DenseAt0_5) {
  EXPECT_EQ(harmonicRhythmToDensity(0.5f), HarmonicDensity::Dense);
}

TEST(HarmonicRhythmTest, NormalAt1_0) {
  EXPECT_EQ(harmonicRhythmToDensity(1.0f), HarmonicDensity::Normal);
}

TEST(HarmonicRhythmTest, SlowAt2_0) {
  EXPECT_EQ(harmonicRhythmToDensity(2.0f), HarmonicDensity::Slow);
}

TEST(HarmonicRhythmTest, DenseAtBelowThreshold) {
  // Values <= 0.5 should map to Dense
  EXPECT_EQ(harmonicRhythmToDensity(0.3f), HarmonicDensity::Dense);
}

TEST(HarmonicRhythmTest, NormalBetweenThresholds) {
  // Values between 0.5 and 2.0 should map to Normal
  EXPECT_EQ(harmonicRhythmToDensity(1.5f), HarmonicDensity::Normal);
}

TEST(HarmonicRhythmTest, SlowAtAboveThreshold) {
  // Values >= 2.0 should map to Slow
  EXPECT_EQ(harmonicRhythmToDensity(3.0f), HarmonicDensity::Slow);
}

// ============================================================================
// C4: HarmonicRhythmInfo::forSection - explicit vs fallback
// ============================================================================

TEST(HarmonicRhythmInfoTest, ExplicitHarmonicRhythmUsesDense) {
  // Section with harmonic_rhythm=0.5 should use Dense regardless of type
  Section section{};
  section.type = SectionType::Intro;  // Intro normally maps to Slow
  section.harmonic_rhythm = 0.5f;

  auto info = HarmonicRhythmInfo::forSection(section, Mood::StraightPop);
  EXPECT_EQ(info.density, HarmonicDensity::Dense);
}

TEST(HarmonicRhythmInfoTest, ExplicitDenseEnablesDoubleAtPhraseEnd) {
  Section section{};
  section.type = SectionType::A;
  section.harmonic_rhythm = 0.5f;

  auto info = HarmonicRhythmInfo::forSection(section, Mood::StraightPop);
  EXPECT_EQ(info.density, HarmonicDensity::Dense);
  EXPECT_TRUE(info.double_at_phrase_end);
}

TEST(HarmonicRhythmInfoTest, ExplicitNonDenseDisablesDoubleAtPhraseEnd) {
  Section section{};
  section.type = SectionType::Chorus;
  section.harmonic_rhythm = 1.0f;

  auto info = HarmonicRhythmInfo::forSection(section, Mood::StraightPop);
  EXPECT_EQ(info.density, HarmonicDensity::Normal);
  EXPECT_FALSE(info.double_at_phrase_end);
}

TEST(HarmonicRhythmInfoTest, FallbackIntroIsSlow) {
  // Section with harmonic_rhythm=0 (not set) falls back to type-based
  Section section{};
  section.type = SectionType::Intro;
  section.harmonic_rhythm = 0.0f;

  auto info = HarmonicRhythmInfo::forSection(section, Mood::StraightPop);
  EXPECT_EQ(info.density, HarmonicDensity::Slow);
}

TEST(HarmonicRhythmInfoTest, FallbackAIsNormal) {
  Section section{};
  section.type = SectionType::A;
  section.harmonic_rhythm = 0.0f;

  auto info = HarmonicRhythmInfo::forSection(section, Mood::StraightPop);
  EXPECT_EQ(info.density, HarmonicDensity::Normal);
}

TEST(HarmonicRhythmInfoTest, FallbackChorusNonBalladIsDense) {
  Section section{};
  section.type = SectionType::Chorus;
  section.harmonic_rhythm = 0.0f;

  auto info = HarmonicRhythmInfo::forSection(section, Mood::StraightPop);
  EXPECT_EQ(info.density, HarmonicDensity::Dense);
}

TEST(HarmonicRhythmInfoTest, FallbackChorusBalladIsNormal) {
  Section section{};
  section.type = SectionType::Chorus;
  section.harmonic_rhythm = 0.0f;

  auto info = HarmonicRhythmInfo::forSection(section, Mood::Ballad);
  EXPECT_EQ(info.density, HarmonicDensity::Normal);
}

}  // namespace
}  // namespace midisketch
