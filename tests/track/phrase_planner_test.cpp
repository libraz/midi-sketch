/**
 * @file phrase_planner_test.cpp
 * @brief Tests for PhrasePlanner vocal phrase planning infrastructure.
 */

#include <gtest/gtest.h>

#include "core/timing_constants.h"
#include "core/types.h"
#include "track/vocal/phrase_cache.h"
#include "track/vocal/phrase_plan.h"
#include "track/vocal/phrase_planner.h"

namespace midisketch {
namespace {

// ============================================================================
// Helper constants
// ============================================================================

constexpr Tick kSectionStart = 0;
constexpr Tick k8BarEnd = 8 * TICKS_PER_BAR;     // 15360
constexpr Tick k4BarEnd = 4 * TICKS_PER_BAR;     // 7680
constexpr Tick k6BarEnd = 6 * TICKS_PER_BAR;     // 11520
constexpr Tick k2BarEnd = 2 * TICKS_PER_BAR;     // 3840

// ============================================================================
// Step 1: Phrase structure tests
// ============================================================================

TEST(PhrasePlannerTest, EightBarSectionProducesFourPhrases) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  EXPECT_EQ(plan.phrases.size(), 4u);
  EXPECT_EQ(plan.pair_count, 2u);
}

TEST(PhrasePlannerTest, FourBarSectionProducesTwoPhrases) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k4BarEnd, 4, Mood::StraightPop);

  EXPECT_EQ(plan.phrases.size(), 2u);
  EXPECT_EQ(plan.pair_count, 1u);
}

TEST(PhrasePlannerTest, SixBarSectionProducesThreePhrases) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k6BarEnd, 6, Mood::StraightPop);

  EXPECT_EQ(plan.phrases.size(), 3u);
  EXPECT_EQ(plan.pair_count, 1u);
}

TEST(PhrasePlannerTest, TwoBarSectionProducesOnePhrase) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k2BarEnd, 2, Mood::StraightPop);

  EXPECT_EQ(plan.phrases.size(), 1u);
  EXPECT_EQ(plan.pair_count, 0u);
}

TEST(PhrasePlannerTest, AntecedentConsequentRolesForEightBars) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 4u);

  // 8 bars: [Ant, Cons, Ant, Cons]
  EXPECT_EQ(plan.phrases[0].pair_role, PhrasePairRole::Antecedent);
  EXPECT_EQ(plan.phrases[1].pair_role, PhrasePairRole::Consequent);
  EXPECT_EQ(plan.phrases[2].pair_role, PhrasePairRole::Antecedent);
  EXPECT_EQ(plan.phrases[3].pair_role, PhrasePairRole::Consequent);

  // Pair indices
  EXPECT_EQ(plan.phrases[0].pair_index, 0u);
  EXPECT_EQ(plan.phrases[1].pair_index, 0u);
  EXPECT_EQ(plan.phrases[2].pair_index, 1u);
  EXPECT_EQ(plan.phrases[3].pair_index, 1u);
}

TEST(PhrasePlannerTest, SixBarThirdPhraseIsIndependent) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k6BarEnd, 6, Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 3u);

  // 6 bars: [Ant, Cons, Independent]
  EXPECT_EQ(plan.phrases[0].pair_role, PhrasePairRole::Antecedent);
  EXPECT_EQ(plan.phrases[1].pair_role, PhrasePairRole::Consequent);
  EXPECT_EQ(plan.phrases[2].pair_role, PhrasePairRole::Independent);
}

TEST(PhrasePlannerTest, TwoBarPhraseIsIndependent) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k2BarEnd, 2, Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 1u);
  EXPECT_EQ(plan.phrases[0].pair_role, PhrasePairRole::Independent);
}

TEST(PhrasePlannerTest, PhraseIndicesAreSequential) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  for (uint8_t idx = 0; idx < plan.phrases.size(); ++idx) {
    EXPECT_EQ(plan.phrases[idx].phrase_index, idx);
  }
}

// ============================================================================
// Step 2: Timing tests
// ============================================================================

TEST(PhrasePlannerTest, FirstPhraseStartsAtSectionStart) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  ASSERT_FALSE(plan.phrases.empty());
  EXPECT_EQ(plan.phrases[0].start_tick, kSectionStart);
  EXPECT_EQ(plan.phrases[0].breath_before, 0u);
}

TEST(PhrasePlannerTest, LastPhraseEndsAtSectionEnd) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  ASSERT_FALSE(plan.phrases.empty());
  EXPECT_EQ(plan.phrases.back().end_tick, k8BarEnd);
}

TEST(PhrasePlannerTest, PhraseTimingIsMonotonicallyIncreasing) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  for (size_t idx = 1; idx < plan.phrases.size(); ++idx) {
    EXPECT_GT(plan.phrases[idx].start_tick, plan.phrases[idx - 1].start_tick)
        << "Phrase " << idx << " start should be after phrase " << (idx - 1);
    EXPECT_GE(plan.phrases[idx].start_tick, plan.phrases[idx - 1].end_tick)
        << "Phrase " << idx << " should not overlap with phrase " << (idx - 1);
  }
}

TEST(PhrasePlannerTest, PhraseStartAlwaysBeforeEnd) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  for (size_t idx = 0; idx < plan.phrases.size(); ++idx) {
    EXPECT_LT(plan.phrases[idx].start_tick, plan.phrases[idx].end_tick)
        << "Phrase " << idx << " start should be before end";
  }
}

TEST(PhrasePlannerTest, BreathAfterMatchesNextBreathBefore) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  for (size_t idx = 0; idx + 1 < plan.phrases.size(); ++idx) {
    EXPECT_EQ(plan.phrases[idx].breath_after, plan.phrases[idx + 1].breath_before)
        << "Breath after phrase " << idx << " should match breath before phrase "
        << (idx + 1);
  }
  // Last phrase should have 0 breath after
  EXPECT_EQ(plan.phrases.back().breath_after, 0u);
}

TEST(PhrasePlannerTest, NonZeroSectionStartOffset) {
  constexpr Tick kOffset = 4 * TICKS_PER_BAR;
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus, kOffset, kOffset + k8BarEnd, 8, Mood::StraightPop);

  ASSERT_FALSE(plan.phrases.empty());
  EXPECT_EQ(plan.phrases[0].start_tick, kOffset);
  EXPECT_EQ(plan.phrases.back().end_tick, kOffset + k8BarEnd);
}

TEST(PhrasePlannerTest, PhraseBeatsArePositive) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  for (const auto& phrase : plan.phrases) {
    EXPECT_GT(phrase.beats, 0u) << "Phrase beats should be positive";
  }
}

// ============================================================================
// Step 3: Rhythm lock reconciliation tests
// ============================================================================

TEST(PhrasePlannerTest, RhythmLockNullPatternSkipsReconciliation) {
  // Building without rhythm pattern should not crash or change behavior
  PhrasePlan plan_without = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop,
      VocalStylePreset::Standard, nullptr);

  EXPECT_EQ(plan_without.phrases.size(), 4u);
  for (const auto& phrase : plan_without.phrases) {
    EXPECT_FALSE(phrase.soft_boundary);
  }
}

TEST(PhrasePlannerTest, RhythmLockWithGapsShiftsBoundaries) {
  // Create a rhythm pattern with a clear gap at beat 8 (bar 2 boundary)
  CachedRhythmPattern rhythm;
  rhythm.phrase_beats = 32;  // 8 bars
  rhythm.is_locked = true;

  // Notes from beat 0-7 with a big gap at beat 8
  for (int beat = 0; beat < 8; ++beat) {
    rhythm.onset_beats.push_back(static_cast<float>(beat));
    rhythm.durations.push_back(0.5f);
  }
  // Gap at beat 8 (no onset for 1 beat)
  // Notes from beat 9 onward
  for (int beat = 9; beat < 16; ++beat) {
    rhythm.onset_beats.push_back(static_cast<float>(beat));
    rhythm.durations.push_back(0.5f);
  }
  // Another gap at beat 16
  for (int beat = 17; beat < 24; ++beat) {
    rhythm.onset_beats.push_back(static_cast<float>(beat));
    rhythm.durations.push_back(0.5f);
  }
  // Gap at beat 24
  for (int beat = 25; beat < 32; ++beat) {
    rhythm.onset_beats.push_back(static_cast<float>(beat));
    rhythm.durations.push_back(0.5f);
  }

  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop,
      VocalStylePreset::Standard, &rhythm);

  ASSERT_EQ(plan.phrases.size(), 4u);
  // First phrase never has soft_boundary (it's the section start)
  EXPECT_FALSE(plan.phrases[0].soft_boundary);
  // At least some non-first boundaries should have been reconciled (not all soft)
  bool any_non_soft = false;
  for (size_t idx = 1; idx < plan.phrases.size(); ++idx) {
    if (!plan.phrases[idx].soft_boundary) {
      any_non_soft = true;
      break;
    }
  }
  EXPECT_TRUE(any_non_soft) << "At least one boundary should align with a rhythm gap";
}

TEST(PhrasePlannerTest, RhythmLockNoGapsMarksSoftBoundary) {
  // Create a dense rhythm pattern with no gaps
  CachedRhythmPattern rhythm;
  rhythm.phrase_beats = 32;
  rhythm.is_locked = true;

  // Continuous 16th notes with no gaps
  for (int idx = 0; idx < 128; ++idx) {
    rhythm.onset_beats.push_back(static_cast<float>(idx) * 0.25f);
    rhythm.durations.push_back(0.25f);
  }

  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop,
      VocalStylePreset::Standard, &rhythm);

  ASSERT_EQ(plan.phrases.size(), 4u);
  // All non-first phrases should be soft boundaries (no gaps found)
  for (size_t idx = 1; idx < plan.phrases.size(); ++idx) {
    EXPECT_TRUE(plan.phrases[idx].soft_boundary)
        << "Phrase " << idx << " should have soft boundary with no rhythm gaps";
  }
}

// ============================================================================
// Step 4: Arc and contour tests
// ============================================================================

TEST(PhrasePlannerTest, ArcStagesCoverAllFourStages) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 4u);

  // With 4 phrases, stages should be 0, 1, 2, 3
  EXPECT_EQ(plan.phrases[0].arc_stage, 0u);  // Presentation
  EXPECT_EQ(plan.phrases[1].arc_stage, 1u);  // Development
  EXPECT_EQ(plan.phrases[2].arc_stage, 2u);  // Climax
  EXPECT_EQ(plan.phrases[3].arc_stage, 3u);  // Resolution
}

TEST(PhrasePlannerTest, ArcStagesClampedForTwoPhrases) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k4BarEnd, 4, Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 2u);

  // With 2 phrases: stage 0 and stage 2
  EXPECT_EQ(plan.phrases[0].arc_stage, 0u);  // Presentation
  EXPECT_EQ(plan.phrases[1].arc_stage, 2u);  // Climax
}

TEST(PhrasePlannerTest, ChorusContourFollowsTable) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 4u);

  // Chorus: [Peak, Valley, Peak, Descending]
  EXPECT_EQ(plan.phrases[0].contour, ContourType::Peak);
  EXPECT_EQ(plan.phrases[1].contour, ContourType::Valley);
  EXPECT_EQ(plan.phrases[2].contour, ContourType::Peak);
  EXPECT_EQ(plan.phrases[3].contour, ContourType::Descending);
}

TEST(PhrasePlannerTest, VerseContourFollowsTable) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 4u);

  // A/Verse: [Ascending, Ascending, Peak, Descending]
  EXPECT_EQ(plan.phrases[0].contour, ContourType::Ascending);
  EXPECT_EQ(plan.phrases[1].contour, ContourType::Ascending);
  EXPECT_EQ(plan.phrases[2].contour, ContourType::Peak);
  EXPECT_EQ(plan.phrases[3].contour, ContourType::Descending);
}

TEST(PhrasePlannerTest, BSectionContourFollowsTable) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::B, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 4u);

  // B: [Ascending, Ascending, Peak, Ascending]
  EXPECT_EQ(plan.phrases[0].contour, ContourType::Ascending);
  EXPECT_EQ(plan.phrases[1].contour, ContourType::Ascending);
  EXPECT_EQ(plan.phrases[2].contour, ContourType::Peak);
  EXPECT_EQ(plan.phrases[3].contour, ContourType::Ascending);
}

TEST(PhrasePlannerTest, BridgeContourFollowsTable) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Bridge, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 4u);

  // Bridge: [Descending, Valley, Peak, Descending]
  EXPECT_EQ(plan.phrases[0].contour, ContourType::Descending);
  EXPECT_EQ(plan.phrases[1].contour, ContourType::Valley);
  EXPECT_EQ(plan.phrases[2].contour, ContourType::Peak);
  EXPECT_EQ(plan.phrases[3].contour, ContourType::Descending);
}

TEST(PhrasePlannerTest, ChorusHookPositionsCorrect) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 4u);

  // Hook positions: phrase 0 and phrase 2 (count > 3)
  EXPECT_TRUE(plan.phrases[0].is_hook_position);
  EXPECT_FALSE(plan.phrases[1].is_hook_position);
  EXPECT_TRUE(plan.phrases[2].is_hook_position);
  EXPECT_FALSE(plan.phrases[3].is_hook_position);
}

TEST(PhrasePlannerTest, VerseHasNoHookPositions) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  for (const auto& phrase : plan.phrases) {
    EXPECT_FALSE(phrase.is_hook_position);
  }
}

TEST(PhrasePlannerTest, ShortChorusHasOneHookPosition) {
  // 4-bar chorus has only 2 phrases, so only phrase 0 is hook
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus, kSectionStart, k4BarEnd, 4, Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 2u);
  EXPECT_TRUE(plan.phrases[0].is_hook_position);
  EXPECT_FALSE(plan.phrases[1].is_hook_position);
}

// ============================================================================
// Step 5: Mora density hints tests
// ============================================================================

TEST(PhrasePlannerTest, VerseMoraHigherThanChorus) {
  PhrasePlan verse_plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop);
  PhrasePlan chorus_plan = PhrasePlanner::buildPlan(
      SectionType::Chorus, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  // Compare first phrase (same arc stage = Presentation, modifier 1.0)
  ASSERT_FALSE(verse_plan.phrases.empty());
  ASSERT_FALSE(chorus_plan.phrases.empty());

  // Verse base 13 > Chorus base 9
  EXPECT_GT(verse_plan.phrases[0].target_note_count,
            chorus_plan.phrases[0].target_note_count);
}

TEST(PhrasePlannerTest, DevelopmentStageHasHigherDensity) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  ASSERT_GE(plan.phrases.size(), 3u);

  // Development (stage 1) has 1.15x modifier vs Presentation (stage 0) at 1.0x
  // So Development phrase should have >= Presentation notes
  // (may be equal due to rounding with different bases)
  EXPECT_GE(plan.phrases[1].density_modifier, plan.phrases[0].density_modifier);
}

TEST(PhrasePlannerTest, ResolutionStageHasLowerDensity) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 4u);

  // Resolution (stage 3) at 0.85x should be less than Presentation (stage 0) at 1.0x
  EXPECT_LT(plan.phrases[3].density_modifier, plan.phrases[0].density_modifier);
}

TEST(PhrasePlannerTest, TargetNoteCountIsPositive) {
  for (auto section : {SectionType::A, SectionType::B, SectionType::Chorus,
                       SectionType::Bridge, SectionType::Intro}) {
    PhrasePlan plan = PhrasePlanner::buildPlan(
        section, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

    for (const auto& phrase : plan.phrases) {
      EXPECT_GT(phrase.target_note_count, 0u)
          << "Section " << static_cast<int>(section)
          << " phrase " << static_cast<int>(phrase.phrase_index)
          << " should have positive target note count";
    }
  }
}

// ============================================================================
// Step 6: Hold-burst detection tests
// ============================================================================

TEST(PhrasePlannerTest, BSectionLastPhraseHasReducedDensity) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::B, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  ASSERT_FALSE(plan.phrases.empty());

  // Last phrase of B section should have density_modifier * 0.7
  const PlannedPhrase& last = plan.phrases.back();
  // The base arc stage modifier for Resolution is 0.85, then * 0.7 = 0.595
  EXPECT_LT(last.density_modifier, 0.7f);
}

TEST(PhrasePlannerTest, ChorusClimaxPhraseIsHoldBurstEntry) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  ASSERT_EQ(plan.phrases.size(), 4u);

  // Phase with arc_stage 2 (Climax) should be hold-burst entry
  bool found_hold_burst = false;
  for (const auto& phrase : plan.phrases) {
    if (phrase.arc_stage == 2) {
      EXPECT_TRUE(phrase.is_hold_burst_entry);
      // Density should be increased (1.3x)
      EXPECT_GT(phrase.density_modifier, 1.0f);
      found_hold_burst = true;
    }
  }
  EXPECT_TRUE(found_hold_burst) << "Should find at least one hold-burst entry in Chorus";
}

TEST(PhrasePlannerTest, VerseHasNoHoldBurstEntries) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

  for (const auto& phrase : plan.phrases) {
    EXPECT_FALSE(phrase.is_hold_burst_entry);
  }
}

// ============================================================================
// Section metadata tests
// ============================================================================

TEST(PhrasePlannerTest, SectionMetadataIsPreserved) {
  constexpr Tick kStart = 3840;
  constexpr Tick kEnd = 19200;

  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus, kStart, kEnd, 8, Mood::StraightPop);

  EXPECT_EQ(plan.section_type, SectionType::Chorus);
  EXPECT_EQ(plan.section_start, kStart);
  EXPECT_EQ(plan.section_end, kEnd);
  EXPECT_EQ(plan.section_bars, 8u);
}

// ============================================================================
// Different moods and vocal styles
// ============================================================================

TEST(PhrasePlannerTest, BalladMoodProducesValidPlan) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k8BarEnd, 8, Mood::Ballad,
      VocalStylePreset::Ballad);

  EXPECT_EQ(plan.phrases.size(), 4u);
  // Timing should still be valid
  for (const auto& phrase : plan.phrases) {
    EXPECT_LT(phrase.start_tick, phrase.end_tick);
    EXPECT_GE(phrase.start_tick, kSectionStart);
    EXPECT_LE(phrase.end_tick, k8BarEnd);
  }
}

TEST(PhrasePlannerTest, VocaloidStyleProducesValidPlan) {
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Chorus, kSectionStart, k8BarEnd, 8, Mood::Yoasobi,
      VocalStylePreset::Vocaloid);

  EXPECT_EQ(plan.phrases.size(), 4u);
  for (size_t idx = 1; idx < plan.phrases.size(); ++idx) {
    EXPECT_GT(plan.phrases[idx].start_tick, plan.phrases[idx - 1].start_tick);
  }
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(PhrasePlannerTest, LargeSectionBarCount) {
  // 16-bar section should produce 8 phrases
  constexpr Tick k16BarEnd = 16 * TICKS_PER_BAR;
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::A, kSectionStart, k16BarEnd, 16, Mood::StraightPop);

  EXPECT_EQ(plan.phrases.size(), 8u);
  EXPECT_EQ(plan.pair_count, 4u);

  // All timing should be valid
  for (size_t idx = 0; idx < plan.phrases.size(); ++idx) {
    EXPECT_LT(plan.phrases[idx].start_tick, plan.phrases[idx].end_tick);
    if (idx > 0) {
      EXPECT_GE(plan.phrases[idx].start_tick, plan.phrases[idx - 1].end_tick);
    }
  }
}

TEST(PhrasePlannerTest, SingleBarSection) {
  constexpr Tick k1BarEnd = TICKS_PER_BAR;
  PhrasePlan plan = PhrasePlanner::buildPlan(
      SectionType::Intro, kSectionStart, k1BarEnd, 1, Mood::StraightPop);

  // 1 bar should produce 1 independent phrase
  EXPECT_EQ(plan.phrases.size(), 1u);
  EXPECT_EQ(plan.pair_count, 0u);
  EXPECT_EQ(plan.phrases[0].pair_role, PhrasePairRole::Independent);
}

TEST(PhrasePlannerTest, AllSectionTypesProduceValidPlans) {
  for (auto section_type : {SectionType::Intro, SectionType::A, SectionType::B,
                            SectionType::Chorus, SectionType::Bridge,
                            SectionType::Interlude, SectionType::Outro,
                            SectionType::Drop}) {
    PhrasePlan plan = PhrasePlanner::buildPlan(
        section_type, kSectionStart, k8BarEnd, 8, Mood::StraightPop);

    EXPECT_FALSE(plan.phrases.empty())
        << "Section type " << static_cast<int>(section_type) << " should produce phrases";

    for (size_t idx = 0; idx < plan.phrases.size(); ++idx) {
      EXPECT_LT(plan.phrases[idx].start_tick, plan.phrases[idx].end_tick)
          << "Section type " << static_cast<int>(section_type)
          << " phrase " << idx << " has invalid timing";
    }
  }
}

TEST(PhrasePlannerTest, PhrasePlanDefaultValues) {
  // Verify default construction of PhrasePlan
  PhrasePlan plan;
  EXPECT_EQ(plan.section_type, SectionType::A);
  EXPECT_EQ(plan.section_start, 0u);
  EXPECT_EQ(plan.section_end, 0u);
  EXPECT_EQ(plan.section_bars, 8u);
  EXPECT_TRUE(plan.phrases.empty());
  EXPECT_EQ(plan.pair_count, 0u);
}

TEST(PhrasePlannerTest, PlannedPhraseDefaultValues) {
  // Verify default construction of PlannedPhrase
  PlannedPhrase phrase;
  EXPECT_EQ(phrase.start_tick, 0u);
  EXPECT_EQ(phrase.end_tick, 0u);
  EXPECT_EQ(phrase.beats, 8u);
  EXPECT_EQ(phrase.pair_role, PhrasePairRole::Independent);
  EXPECT_EQ(phrase.arc_stage, 0u);
  EXPECT_EQ(phrase.pair_index, 0u);
  EXPECT_EQ(phrase.phrase_index, 0u);
  EXPECT_EQ(phrase.breath_before, 0u);
  EXPECT_EQ(phrase.breath_after, 0u);
  EXPECT_EQ(phrase.target_note_count, 12u);
  EXPECT_FLOAT_EQ(phrase.density_modifier, 1.0f);
  EXPECT_EQ(phrase.contour, ContourType::Ascending);
  EXPECT_FALSE(phrase.is_hook_position);
  EXPECT_FALSE(phrase.is_hold_burst_entry);
  EXPECT_FALSE(phrase.soft_boundary);
}

}  // namespace
}  // namespace midisketch
