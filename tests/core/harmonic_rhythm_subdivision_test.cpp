/**
 * @file harmonic_rhythm_subdivision_test.cpp
 * @brief Tests for harmonic rhythm subdivision (half-bar chord changes in B sections).
 */

#include "core/harmonic_rhythm.h"

#include <gtest/gtest.h>

#include "core/types.h"

namespace midisketch {
namespace {

// ============================================================================
// Subdivision field default and B section behavior
// ============================================================================

TEST(HarmonicRhythmSubdivisionTest, DefaultSubdivisionIsOne) {
  HarmonicRhythmInfo info{HarmonicDensity::Normal, false};
  EXPECT_EQ(info.subdivision, 1);
}

TEST(HarmonicRhythmSubdivisionTest, BSectionHasSubdivisionTwo) {
  // Non-ballad B section should have subdivision=2 for harmonic acceleration
  auto info = HarmonicRhythmInfo::forSection(SectionType::B, Mood::StraightPop);
  EXPECT_EQ(info.subdivision, 2);
}

TEST(HarmonicRhythmSubdivisionTest, BSectionBalladHasSubdivisionOne) {
  // Ballad B section should keep subdivision=1 (no acceleration)
  auto info = HarmonicRhythmInfo::forSection(SectionType::B, Mood::Ballad);
  EXPECT_EQ(info.subdivision, 1);
}

TEST(HarmonicRhythmSubdivisionTest, ChorusHasSubdivisionOne) {
  auto info = HarmonicRhythmInfo::forSection(SectionType::Chorus, Mood::StraightPop);
  EXPECT_EQ(info.subdivision, 1);
}

TEST(HarmonicRhythmSubdivisionTest, VerseHasSubdivisionOne) {
  auto info = HarmonicRhythmInfo::forSection(SectionType::A, Mood::StraightPop);
  EXPECT_EQ(info.subdivision, 1);
}

TEST(HarmonicRhythmSubdivisionTest, IntroHasSubdivisionOne) {
  auto info = HarmonicRhythmInfo::forSection(SectionType::Intro, Mood::StraightPop);
  EXPECT_EQ(info.subdivision, 1);
}

TEST(HarmonicRhythmSubdivisionTest, BridgeHasSubdivisionOne) {
  auto info = HarmonicRhythmInfo::forSection(SectionType::Bridge, Mood::StraightPop);
  EXPECT_EQ(info.subdivision, 1);
}

TEST(HarmonicRhythmSubdivisionTest, OutroHasSubdivisionOne) {
  auto info = HarmonicRhythmInfo::forSection(SectionType::Outro, Mood::StraightPop);
  EXPECT_EQ(info.subdivision, 1);
}

TEST(HarmonicRhythmSubdivisionTest, MixBreakHasSubdivisionOne) {
  auto info = HarmonicRhythmInfo::forSection(SectionType::MixBreak, Mood::StraightPop);
  EXPECT_EQ(info.subdivision, 1);
}

// ============================================================================
// Explicit harmonic_rhythm override from Section struct
// ============================================================================

TEST(HarmonicRhythmSubdivisionTest, ExplicitHalfBarSetsSubdivisionTwo) {
  Section section{};
  section.type = SectionType::A;  // A normally has subdivision=1
  section.harmonic_rhythm = 0.5f; // Explicit half-bar

  auto info = HarmonicRhythmInfo::forSection(section, Mood::StraightPop);
  EXPECT_EQ(info.subdivision, 2);
}

TEST(HarmonicRhythmSubdivisionTest, ExplicitOneBarKeepsSubdivisionOne) {
  Section section{};
  section.type = SectionType::B;  // B normally has subdivision=2
  section.harmonic_rhythm = 1.0f; // Explicit one bar

  auto info = HarmonicRhythmInfo::forSection(section, Mood::StraightPop);
  EXPECT_EQ(info.subdivision, 1);
}

TEST(HarmonicRhythmSubdivisionTest, ExplicitTwoBarKeepsSubdivisionOne) {
  Section section{};
  section.type = SectionType::B;
  section.harmonic_rhythm = 2.0f; // Explicit slow

  auto info = HarmonicRhythmInfo::forSection(section, Mood::StraightPop);
  EXPECT_EQ(info.subdivision, 1);
}

// ============================================================================
// getChordIndexForSubdividedBar helper
// ============================================================================

TEST(HarmonicRhythmSubdivisionTest, SubdividedBarFirstHalfIndex) {
  // Bar 0, first half -> chord index 0
  EXPECT_EQ(getChordIndexForSubdividedBar(0, 0, 4), 0);
  // Bar 1, first half -> chord index 2
  EXPECT_EQ(getChordIndexForSubdividedBar(1, 0, 4), 2);
  // Bar 2, first half -> chord index 0 (wraps around for 4-chord progression)
  EXPECT_EQ(getChordIndexForSubdividedBar(2, 0, 4), 0);
}

TEST(HarmonicRhythmSubdivisionTest, SubdividedBarSecondHalfIndex) {
  // Bar 0, second half -> chord index 1
  EXPECT_EQ(getChordIndexForSubdividedBar(0, 1, 4), 1);
  // Bar 1, second half -> chord index 3
  EXPECT_EQ(getChordIndexForSubdividedBar(1, 1, 4), 3);
  // Bar 2, second half -> chord index 1 (wraps for 4-chord progression)
  EXPECT_EQ(getChordIndexForSubdividedBar(2, 1, 4), 1);
}

TEST(HarmonicRhythmSubdivisionTest, SubdividedBarWrapsCorrectly) {
  // 8-bar B section with 4-chord progression
  // Each bar gets 2 chords, so 8 bars = 16 chord slots over 4-chord prog
  for (int bar = 0; bar < 8; ++bar) {
    int first_half = getChordIndexForSubdividedBar(bar, 0, 4);
    int second_half = getChordIndexForSubdividedBar(bar, 1, 4);
    EXPECT_GE(first_half, 0);
    EXPECT_LT(first_half, 4);
    EXPECT_GE(second_half, 0);
    EXPECT_LT(second_half, 4);
    // Second half should be different from first half (consecutive chords)
    // unless progression length is 1
    EXPECT_NE(first_half, second_half);
  }
}

TEST(HarmonicRhythmSubdivisionTest, SubdividedBarProgressionLengthOne) {
  // With progression length 1, all indices are 0
  EXPECT_EQ(getChordIndexForSubdividedBar(0, 0, 1), 0);
  EXPECT_EQ(getChordIndexForSubdividedBar(0, 1, 1), 0);
  EXPECT_EQ(getChordIndexForSubdividedBar(5, 1, 1), 0);
}

TEST(HarmonicRhythmSubdivisionTest, SubdividedBarZeroLength) {
  // Edge case: zero-length progression returns 0
  EXPECT_EQ(getChordIndexForSubdividedBar(0, 0, 0), 0);
  EXPECT_EQ(getChordIndexForSubdividedBar(3, 1, 0), 0);
}

// ============================================================================
// B section with various moods
// ============================================================================

TEST(HarmonicRhythmSubdivisionTest, BSectionIdolPopHasSubdivisionTwo) {
  auto info = HarmonicRhythmInfo::forSection(SectionType::B, Mood::IdolPop);
  EXPECT_EQ(info.subdivision, 2);
}

TEST(HarmonicRhythmSubdivisionTest, BSectionEnergeticDanceHasSubdivisionTwo) {
  auto info = HarmonicRhythmInfo::forSection(SectionType::B, Mood::EnergeticDance);
  EXPECT_EQ(info.subdivision, 2);
}

TEST(HarmonicRhythmSubdivisionTest, BSectionSentimentalHasSubdivisionOne) {
  // Sentimental is a ballad mood, should not get subdivision
  auto info = HarmonicRhythmInfo::forSection(SectionType::B, Mood::Sentimental);
  EXPECT_EQ(info.subdivision, 1);
}

}  // namespace
}  // namespace midisketch
