/**
 * @file phrase_plan_test.cpp
 * @brief Unit tests for PhrasePlanner::buildPlan() and PhrasePlan structures.
 *
 * Tests phrase count, antecedent-consequent pairing, timing, arc stages,
 * contour assignment, mora density hints, and hold-burst detection.
 */

#include <gtest/gtest.h>

#include "core/timing_constants.h"
#include "track/vocal/phrase_planner.h"

namespace midisketch {

// ============================================================================
// Step 1: Phrase count and structure
// ============================================================================

TEST(PhrasePlanTest, EightBarSectionProduces4Phrases) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus,
      0,                   // section_start
      8 * TICKS_PER_BAR,   // section_end (8 bars)
      8,                   // section_bars
      Mood::StraightPop);

  EXPECT_EQ(plan.phrases.size(), 4u);
  EXPECT_EQ(plan.pair_count, 2u);
}

TEST(PhrasePlanTest, FourBarSectionProduces2Phrases) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A,
      0, 4 * TICKS_PER_BAR, 4,
      Mood::StraightPop);

  EXPECT_EQ(plan.phrases.size(), 2u);
  EXPECT_EQ(plan.pair_count, 1u);
}

TEST(PhrasePlanTest, TwoBarSectionProduces1IndependentPhrase) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Bridge,
      0, 2 * TICKS_PER_BAR, 2,
      Mood::StraightPop);

  EXPECT_EQ(plan.phrases.size(), 1u);
  EXPECT_EQ(plan.pair_count, 0u);
  EXPECT_EQ(plan.phrases[0].pair_role, PhrasePairRole::Independent);
}

TEST(PhrasePlanTest, SixBarSectionProduces3PhrasesWithIndependent) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A,
      0, 6 * TICKS_PER_BAR, 6,
      Mood::StraightPop);

  EXPECT_EQ(plan.phrases.size(), 3u);
  EXPECT_EQ(plan.pair_count, 1u);
  EXPECT_EQ(plan.phrases[0].pair_role, PhrasePairRole::Antecedent);
  EXPECT_EQ(plan.phrases[1].pair_role, PhrasePairRole::Consequent);
  EXPECT_EQ(plan.phrases[2].pair_role, PhrasePairRole::Independent);
}

// ============================================================================
// Antecedent-consequent pairing
// ============================================================================

TEST(PhrasePlanTest, AntecedentConsequentPairing) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus,
      0, 8 * TICKS_PER_BAR, 8,
      Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 4u);
  EXPECT_EQ(plan.phrases[0].pair_role, PhrasePairRole::Antecedent);
  EXPECT_EQ(plan.phrases[1].pair_role, PhrasePairRole::Consequent);
  EXPECT_EQ(plan.phrases[2].pair_role, PhrasePairRole::Antecedent);
  EXPECT_EQ(plan.phrases[3].pair_role, PhrasePairRole::Consequent);
  // First pair
  EXPECT_EQ(plan.phrases[0].pair_index, 0u);
  EXPECT_EQ(plan.phrases[1].pair_index, 0u);
  // Second pair
  EXPECT_EQ(plan.phrases[2].pair_index, 1u);
  EXPECT_EQ(plan.phrases[3].pair_index, 1u);
}

// ============================================================================
// Timing constraints
// ============================================================================

TEST(PhrasePlanTest, PhraseTimingWithinSectionBounds) {
  Tick start = 1000;
  Tick end = start + 8 * TICKS_PER_BAR;
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, start, end, 8,
      Mood::StraightPop);

  for (const auto& phrase : plan.phrases) {
    EXPECT_GE(phrase.start_tick, start);
    EXPECT_LE(phrase.end_tick, end);
    EXPECT_LT(phrase.start_tick, phrase.end_tick);
  }
}

TEST(PhrasePlanTest, NoOverlappingPhrases) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus,
      0, 8 * TICKS_PER_BAR, 8,
      Mood::StraightPop);

  for (size_t i = 1; i < plan.phrases.size(); ++i) {
    EXPECT_GE(plan.phrases[i].start_tick, plan.phrases[i - 1].end_tick)
        << "Phrase " << i << " overlaps with phrase " << (i - 1);
  }
}

// ============================================================================
// Arc stage assignment
// ============================================================================

TEST(PhrasePlanTest, ArcStageAssignment) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A,
      0, 8 * TICKS_PER_BAR, 8,
      Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 4u);
  EXPECT_EQ(plan.phrases[0].arc_stage, 0u);  // Presentation
  EXPECT_EQ(plan.phrases[1].arc_stage, 1u);  // Development
  EXPECT_EQ(plan.phrases[2].arc_stage, 2u);  // Climax
  EXPECT_EQ(plan.phrases[3].arc_stage, 3u);  // Resolution
}

// ============================================================================
// Contour assignment per section type
// ============================================================================

TEST(PhrasePlanTest, ChorusContourPattern) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus,
      0, 8 * TICKS_PER_BAR, 8,
      Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 4u);
  EXPECT_EQ(plan.phrases[0].contour, ContourType::Peak);
  EXPECT_EQ(plan.phrases[1].contour, ContourType::Valley);
  EXPECT_EQ(plan.phrases[2].contour, ContourType::Peak);
  EXPECT_EQ(plan.phrases[3].contour, ContourType::Descending);
}

TEST(PhrasePlanTest, VerseContourPattern) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A,
      0, 8 * TICKS_PER_BAR, 8,
      Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 4u);
  EXPECT_EQ(plan.phrases[0].contour, ContourType::Ascending);
  EXPECT_EQ(plan.phrases[1].contour, ContourType::Ascending);
  EXPECT_EQ(plan.phrases[2].contour, ContourType::Peak);
  EXPECT_EQ(plan.phrases[3].contour, ContourType::Descending);
}

// ============================================================================
// Hook positions
// ============================================================================

TEST(PhrasePlanTest, ChorusHookPositions) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus,
      0, 8 * TICKS_PER_BAR, 8,
      Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 4u);
  EXPECT_TRUE(plan.phrases[0].is_hook_position);
  EXPECT_FALSE(plan.phrases[1].is_hook_position);
  EXPECT_TRUE(plan.phrases[2].is_hook_position);
  EXPECT_FALSE(plan.phrases[3].is_hook_position);
}

// ============================================================================
// Mora density hints
// ============================================================================

TEST(PhrasePlanTest, MoraDensityHintsNonZero) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus,
      0, 8 * TICKS_PER_BAR, 8,
      Mood::StraightPop);

  for (const auto& phrase : plan.phrases) {
    EXPECT_GT(phrase.target_note_count, 0u);
  }
}

// ============================================================================
// Hold-burst detection
// ============================================================================

TEST(PhrasePlanTest, BSectionLastPhraseReducedDensity) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::B,
      0, 4 * TICKS_PER_BAR, 4,
      Mood::StraightPop);

  ASSERT_GE(plan.phrases.size(), 2u);
  // Last phrase should have reduced density (0.7x)
  EXPECT_LT(plan.phrases.back().density_modifier, 1.0f);
}

TEST(PhrasePlanTest, ChorusClimaxPhraseMarkedAsHoldBurst) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus,
      0, 8 * TICKS_PER_BAR, 8,
      Mood::StraightPop);

  // Find the climax phrase (arc_stage == 2)
  bool found_climax_burst = false;
  for (const auto& phrase : plan.phrases) {
    if (phrase.arc_stage == 2) {
      EXPECT_TRUE(phrase.is_hold_burst_entry);
      EXPECT_GT(phrase.density_modifier, 1.0f);
      found_climax_burst = true;
    }
  }
  EXPECT_TRUE(found_climax_burst);
}

// ============================================================================
// Phrase indices
// ============================================================================

TEST(PhrasePlanTest, PhraseIndicesSequential) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A,
      0, 8 * TICKS_PER_BAR, 8,
      Mood::StraightPop);

  for (size_t i = 0; i < plan.phrases.size(); ++i) {
    EXPECT_EQ(plan.phrases[i].phrase_index, static_cast<uint8_t>(i));
  }
}

// ============================================================================
// Different vocal styles
// ============================================================================

TEST(PhrasePlanTest, DifferentVocalStylesProduceValidPlans) {
  VocalStylePreset styles[] = {
    VocalStylePreset::Standard,
    VocalStylePreset::Vocaloid,
    VocalStylePreset::Idol,
    VocalStylePreset::Ballad,
    VocalStylePreset::Rock
  };

  for (auto style : styles) {
    PhrasePlan plan = PhrasePlanner::buildPlan(
        SectionType::Chorus,
        0, 8 * TICKS_PER_BAR, 8,
        Mood::StraightPop, style);

    EXPECT_FALSE(plan.phrases.empty())
        << "Style " << static_cast<int>(style) << " produced empty plan";
    for (const auto& phrase : plan.phrases) {
      EXPECT_LT(phrase.start_tick, phrase.end_tick)
          << "Style " << static_cast<int>(style)
          << " has invalid phrase timing";
    }
  }
}

}  // namespace midisketch
