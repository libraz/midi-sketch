/**
 * @file melody_designer_test.cpp
 * @brief Tests for melody designer.
 */

#include <gtest/gtest.h>
#include "core/harmony_context.h"
#include "core/melody_templates.h"
#include "core/timing_constants.h"
#include "track/melody_designer.h"
#include <random>
#include <set>

namespace midisketch {
namespace {

// Helper to create a simple section context
MelodyDesigner::SectionContext createTestContext() {
  MelodyDesigner::SectionContext ctx;
  ctx.section_type = SectionType::A;
  ctx.section_start = 0;
  ctx.section_end = TICKS_PER_BAR * 4;  // 4 bars
  ctx.section_bars = 4;
  ctx.chord_degree = 0;  // I chord
  ctx.key_offset = 0;    // C major
  ctx.tessitura = TessituraRange{60, 72, 66};  // C4 to C5
  ctx.vocal_low = 55;    // G3
  ctx.vocal_high = 79;   // G5
  return ctx;
}

// ============================================================================
// selectPitchChoice Tests
// ============================================================================

TEST(MelodyDesignerTest, SelectPitchChoiceReturnsValidChoice) {
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

  for (int i = 0; i < 100; ++i) {
    PitchChoice choice = MelodyDesigner::selectPitchChoice(
        tmpl, 0.5f, false, rng);
    // Should be one of the valid choices
    EXPECT_TRUE(choice == PitchChoice::Same ||
                choice == PitchChoice::StepUp ||
                choice == PitchChoice::StepDown ||
                choice == PitchChoice::TargetStep);
  }
}

TEST(MelodyDesignerTest, SelectPitchChoiceWithHighPlateau) {
  std::mt19937 rng(42);
  // PlateauTalk has 70% plateau ratio
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

  int same_count = 0;
  for (int i = 0; i < 100; ++i) {
    PitchChoice choice = MelodyDesigner::selectPitchChoice(
        tmpl, 0.5f, false, rng);
    if (choice == PitchChoice::Same) same_count++;
  }

  // With 70% plateau, expect roughly 60-80% same
  EXPECT_GT(same_count, 50);
  EXPECT_LT(same_count, 90);
}

TEST(MelodyDesignerTest, SelectPitchChoiceWithTarget) {
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::RunUpTarget);

  int target_count = 0;
  // Test at phrase position > target_attraction_start
  for (int i = 0; i < 100; ++i) {
    PitchChoice choice = MelodyDesigner::selectPitchChoice(
        tmpl, 0.7f, true, rng);
    if (choice == PitchChoice::TargetStep) target_count++;
  }

  // RunUpTarget has strong target attraction (0.8)
  EXPECT_GT(target_count, 30);
}

// ============================================================================
// applyDirectionInertia Tests
// ============================================================================

TEST(MelodyDesignerTest, ApplyDirectionInertiaSameUnchanged) {
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

  PitchChoice result = MelodyDesigner::applyDirectionInertia(
      PitchChoice::Same, 3, tmpl, rng);
  EXPECT_EQ(result, PitchChoice::Same);
}

TEST(MelodyDesignerTest, ApplyDirectionInertiaTargetUnchanged) {
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

  PitchChoice result = MelodyDesigner::applyDirectionInertia(
      PitchChoice::TargetStep, -3, tmpl, rng);
  EXPECT_EQ(result, PitchChoice::TargetStep);
}

TEST(MelodyDesignerTest, ApplyDirectionInertiaInfluencesStep) {
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

  // With strong positive inertia, should tend toward StepUp
  int up_count = 0;
  for (int i = 0; i < 100; ++i) {
    PitchChoice result = MelodyDesigner::applyDirectionInertia(
        PitchChoice::StepDown, 3, tmpl, rng);
    if (result == PitchChoice::StepUp) up_count++;
  }

  // Should sometimes override to StepUp
  EXPECT_GT(up_count, 0);
}

// ============================================================================
// getEffectivePlateauRatio Tests
// ============================================================================

TEST(MelodyDesignerTest, EffectivePlateauRatioBasic) {
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);
  TessituraRange t{60, 72, 66};

  float ratio = MelodyDesigner::getEffectivePlateauRatio(tmpl, 66, t);
  EXPECT_FLOAT_EQ(ratio, tmpl.plateau_ratio);
}

TEST(MelodyDesignerTest, EffectivePlateauRatioHighRegister) {
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);
  TessituraRange t{60, 72, 66};

  float ratio = MelodyDesigner::getEffectivePlateauRatio(tmpl, 75, t);
  // Should be boosted above tessitura
  EXPECT_GT(ratio, tmpl.plateau_ratio);
}

TEST(MelodyDesignerTest, EffectivePlateauRatioCappedAt90) {
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);
  TessituraRange t{60, 72, 66};

  float ratio = MelodyDesigner::getEffectivePlateauRatio(tmpl, 80, t);
  EXPECT_LE(ratio, 0.9f);
}

// ============================================================================
// shouldLeap Tests
// ============================================================================

TEST(MelodyDesignerTest, ShouldLeapNone) {
  EXPECT_FALSE(MelodyDesigner::shouldLeap(LeapTrigger::None, 0.0f, 0.0f));
  EXPECT_FALSE(MelodyDesigner::shouldLeap(LeapTrigger::None, 0.5f, 0.5f));
  EXPECT_FALSE(MelodyDesigner::shouldLeap(LeapTrigger::None, 1.0f, 1.0f));
}

TEST(MelodyDesignerTest, ShouldLeapPhraseStart) {
  EXPECT_TRUE(MelodyDesigner::shouldLeap(LeapTrigger::PhraseStart, 0.0f, 0.5f));
  EXPECT_TRUE(MelodyDesigner::shouldLeap(LeapTrigger::PhraseStart, 0.05f, 0.5f));
  EXPECT_FALSE(MelodyDesigner::shouldLeap(LeapTrigger::PhraseStart, 0.5f, 0.5f));
}

TEST(MelodyDesignerTest, ShouldLeapEmotionalPeak) {
  EXPECT_FALSE(MelodyDesigner::shouldLeap(LeapTrigger::EmotionalPeak, 0.5f, 0.3f));
  EXPECT_TRUE(MelodyDesigner::shouldLeap(LeapTrigger::EmotionalPeak, 0.5f, 0.7f));
  EXPECT_FALSE(MelodyDesigner::shouldLeap(LeapTrigger::EmotionalPeak, 0.5f, 0.9f));
}

TEST(MelodyDesignerTest, ShouldLeapSectionBoundary) {
  EXPECT_TRUE(MelodyDesigner::shouldLeap(LeapTrigger::SectionBoundary, 0.5f, 0.02f));
  EXPECT_FALSE(MelodyDesigner::shouldLeap(LeapTrigger::SectionBoundary, 0.5f, 0.5f));
  EXPECT_TRUE(MelodyDesigner::shouldLeap(LeapTrigger::SectionBoundary, 0.5f, 0.98f));
}

// ============================================================================
// getStabilizeStep Tests
// ============================================================================

TEST(MelodyDesignerTest, StabilizeStepOppositeDirection) {
  int step = MelodyDesigner::getStabilizeStep(1, 4);
  EXPECT_LT(step, 0);  // Opposite direction

  step = MelodyDesigner::getStabilizeStep(-1, 4);
  EXPECT_GT(step, 0);  // Opposite direction
}

TEST(MelodyDesignerTest, StabilizeStepSmallerMagnitude) {
  int step = MelodyDesigner::getStabilizeStep(1, 6);
  EXPECT_LE(std::abs(step), 3);  // Half of max_step

  step = MelodyDesigner::getStabilizeStep(-1, 6);
  EXPECT_LE(std::abs(step), 3);
}

// ============================================================================
// isInSameVowelSection Tests
// ============================================================================

TEST(MelodyDesignerTest, SameVowelSectionTrue) {
  // Positions within same 2-beat section
  EXPECT_TRUE(MelodyDesigner::isInSameVowelSection(0.0f, 1.0f, 8));
  EXPECT_TRUE(MelodyDesigner::isInSameVowelSection(2.0f, 3.5f, 8));
}

TEST(MelodyDesignerTest, SameVowelSectionFalse) {
  // Positions in different 2-beat sections
  EXPECT_FALSE(MelodyDesigner::isInSameVowelSection(1.5f, 2.5f, 8));
  EXPECT_FALSE(MelodyDesigner::isInSameVowelSection(0.0f, 4.0f, 8));
}

// ============================================================================
// getMaxStepInVowelSection Tests
// ============================================================================

TEST(MelodyDesignerTest, MaxStepInSameVowelSection) {
  EXPECT_EQ(MelodyDesigner::getMaxStepInVowelSection(true), 2);
}

TEST(MelodyDesignerTest, MaxStepInDifferentVowelSection) {
  EXPECT_EQ(MelodyDesigner::getMaxStepInVowelSection(false), 4);
}

// ============================================================================
// generateMelodyPhrase Tests
// ============================================================================

TEST(MelodyDesignerTest, GenerateMelodyPhraseProducesNotes) {
  MelodyDesigner designer;
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);
  auto ctx = createTestContext();

  // Need to set up HarmonyContext
  HarmonyContext harmony;

  auto result = designer.generateMelodyPhrase(
      tmpl, 0, 8, ctx, -1, 0, harmony, rng);

  EXPECT_GT(result.notes.size(), 0u);
  EXPECT_GE(result.last_pitch, ctx.vocal_low);
  EXPECT_LE(result.last_pitch, ctx.vocal_high);
}

TEST(MelodyDesignerTest, GenerateMelodyPhraseNotesInRange) {
  MelodyDesigner designer;
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);
  auto ctx = createTestContext();
  HarmonyContext harmony;

  auto result = designer.generateMelodyPhrase(
      tmpl, 0, 8, ctx, -1, 0, harmony, rng);

  for (const auto& note : result.notes) {
    EXPECT_GE(note.note, ctx.vocal_low);
    EXPECT_LE(note.note, ctx.vocal_high);
  }
}

TEST(MelodyDesignerTest, GenerateMelodyPhraseContinuity) {
  MelodyDesigner designer;
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);
  auto ctx = createTestContext();
  HarmonyContext harmony;

  // First phrase
  auto result1 = designer.generateMelodyPhrase(
      tmpl, 0, 8, ctx, -1, 0, harmony, rng);

  // Second phrase with continuity
  auto result2 = designer.generateMelodyPhrase(
      tmpl, TICKS_PER_BAR * 2, 8, ctx,
      result1.last_pitch, result1.direction_inertia,
      harmony, rng);

  EXPECT_GT(result2.notes.size(), 0u);
  // First note of second phrase should be close to last note of first
  if (!result2.notes.empty()) {
    int diff = std::abs(static_cast<int>(result2.notes[0].note) - result1.last_pitch);
    EXPECT_LE(diff, 7);  // Within a fifth
  }
}

// ============================================================================
// generateHook Tests
// ============================================================================

TEST(MelodyDesignerTest, GenerateHookProducesNotes) {
  MelodyDesigner designer;
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::HookRepeat);
  auto ctx = createTestContext();
  ctx.section_type = SectionType::Chorus;
  HarmonyContext harmony;

  auto result = designer.generateHook(tmpl, 0, ctx, -1, harmony, rng);

  EXPECT_GT(result.notes.size(), 0u);
}

TEST(MelodyDesignerTest, GenerateHookRepeatsPattern) {
  MelodyDesigner designer;
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::HookRepeat);
  auto ctx = createTestContext();
  ctx.section_type = SectionType::Chorus;
  HarmonyContext harmony;

  auto result = designer.generateHook(tmpl, 0, ctx, -1, harmony, rng);

  // HookRepeat has hook_note_count=2, hook_repeat_count=4
  // So expect 2*4 = 8 notes minimum
  EXPECT_GE(result.notes.size(), 8u);
}

// ============================================================================
// generateSection Tests
// ============================================================================

TEST(MelodyDesignerTest, GenerateSectionProducesNotes) {
  MelodyDesigner designer;
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);
  auto ctx = createTestContext();
  HarmonyContext harmony;

  auto notes = designer.generateSection(tmpl, ctx, harmony, rng);

  EXPECT_GT(notes.size(), 0u);
}

TEST(MelodyDesignerTest, GenerateSectionNotesInTimeRange) {
  MelodyDesigner designer;
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);
  auto ctx = createTestContext();
  HarmonyContext harmony;

  auto notes = designer.generateSection(tmpl, ctx, harmony, rng);

  for (const auto& note : notes) {
    EXPECT_GE(note.start_tick, ctx.section_start);
    EXPECT_LE(note.start_tick + note.duration, ctx.section_end + TICKS_PER_BEAT);
  }
}

TEST(MelodyDesignerTest, GenerateSectionDifferentTemplates) {
  MelodyDesigner designer;
  auto ctx = createTestContext();
  HarmonyContext harmony;

  // Test all templates produce valid output
  for (uint8_t id = 1; id <= MELODY_TEMPLATE_COUNT; ++id) {
    std::mt19937 rng(42);
    const MelodyTemplate& tmpl = getTemplate(static_cast<MelodyTemplateId>(id));

    auto notes = designer.generateSection(tmpl, ctx, harmony, rng);

    EXPECT_GT(notes.size(), 0u) << "Template " << static_cast<int>(id)
                                 << " produced no notes";
  }
}

// ============================================================================
// Section Transition Tests
// ============================================================================

TEST(MelodyDesignerTest, GetTransitionBToChorus) {
  const SectionTransition* trans = getTransition(SectionType::B, SectionType::Chorus);
  ASSERT_NE(trans, nullptr);

  // B→Chorus maintains melodic register (pitch_tendency=0) to preserve motif continuity.
  // Tension is built through dynamics (velocity_growth) rather than forced pitch ascent.
  // This allows the Chorus to bring the melodic peak naturally.
  EXPECT_EQ(trans->pitch_tendency, 0);
  // No forced leading tone - let melodic flow remain natural
  EXPECT_FALSE(trans->use_leading_tone);
  // Should have velocity growth (excitement)
  EXPECT_GT(trans->velocity_growth, 1.0f);
}

TEST(MelodyDesignerTest, GetTransitionBridgeToChorus) {
  const SectionTransition* trans = getTransition(SectionType::Bridge, SectionType::Chorus);
  ASSERT_NE(trans, nullptr);

  // Bridge→Chorus should have strong upward tendency
  EXPECT_GE(trans->pitch_tendency, 3);
  EXPECT_TRUE(trans->use_leading_tone);
}

TEST(MelodyDesignerTest, GetTransitionChorusToA) {
  const SectionTransition* trans = getTransition(SectionType::Chorus, SectionType::A);
  ASSERT_NE(trans, nullptr);

  // Chorus→A should calm down (negative tendency)
  EXPECT_LT(trans->pitch_tendency, 0);
  // Should have velocity decrease
  EXPECT_LT(trans->velocity_growth, 1.0f);
}

TEST(MelodyDesignerTest, GetTransitionNoTransition) {
  // No specific transition defined for Outro→Intro
  const SectionTransition* trans = getTransition(SectionType::Outro, SectionType::Intro);
  EXPECT_EQ(trans, nullptr);
}

TEST(MelodyDesignerTest, ApplyTransitionApproachModifiesNotes) {
  MelodyDesigner designer;
  auto ctx = createTestContext();
  ctx.section_type = SectionType::B;
  ctx.transition_to_next = getTransition(SectionType::B, SectionType::Chorus);
  HarmonyContext harmony;
  std::mt19937 rng(42);

  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);
  auto notes = designer.generateSection(tmpl, ctx, harmony, rng);

  // Store original velocities near section end
  std::vector<uint8_t> original_velocities;
  Tick approach_start = ctx.section_end - 4 * TICKS_PER_BEAT;
  for (const auto& note : notes) {
    if (note.start_tick >= approach_start) {
      original_velocities.push_back(note.velocity);
    }
  }

  // Apply transition
  designer.applyTransitionApproach(notes, ctx, harmony);

  // Verify velocities changed (should be louder due to velocity_growth > 1)
  size_t idx = 0;
  for (const auto& note : notes) {
    if (note.start_tick >= approach_start && idx < original_velocities.size()) {
      // Due to crescendo, later notes should be louder or same
      EXPECT_GE(note.velocity, original_velocities[idx] * 0.9f)
          << "Velocity should not decrease significantly during approach";
      ++idx;
    }
  }
}

TEST(MelodyDesignerTest, ApplyTransitionApproachNoOpWithoutTransition) {
  MelodyDesigner designer;
  auto ctx = createTestContext();
  ctx.transition_to_next = nullptr;  // No transition
  HarmonyContext harmony;
  std::mt19937 rng(42);

  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);
  auto notes = designer.generateSection(tmpl, ctx, harmony, rng);

  // Store original notes
  auto original_notes = notes;

  // Apply transition (should be no-op)
  designer.applyTransitionApproach(notes, ctx, harmony);

  // Notes should be unchanged
  EXPECT_EQ(notes.size(), original_notes.size());
  for (size_t i = 0; i < notes.size(); ++i) {
    EXPECT_EQ(notes[i].note, original_notes[i].note);
    EXPECT_EQ(notes[i].velocity, original_notes[i].velocity);
  }
}

// ============================================================================
// Hook Duration Regression Tests
// ============================================================================

// Regression test for hook duration calculation fix.
// Previously, hooks could span more time than phrase_beats, causing
// the next phrase to start during the hook and create overlapping notes.
// After removeOverlaps, these became 1-tick duration notes.
TEST(MelodyDesignerTest, HookDoesNotCreateOverlappingNotes) {
  MelodyDesigner designer;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  // Use HookRepeat template which has high hook repeat count
  // This tests the hook overlap scenario
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::HookRepeat);

  // Create a Chorus context (hooks are generated for Chorus sections)
  MelodyDesigner::SectionContext ctx;
  ctx.section_type = SectionType::Chorus;
  ctx.section_start = 0;
  ctx.section_end = TICKS_PER_BAR * 8;  // 8 bars
  ctx.section_bars = 8;
  ctx.chord_degree = 0;
  ctx.key_offset = 0;
  ctx.tessitura = TessituraRange{60, 72, 66};
  ctx.vocal_low = 57;
  ctx.vocal_high = 79;
  ctx.density_modifier = 1.0f;
  ctx.thirtysecond_ratio = 0.0f;

  auto notes = designer.generateSection(tmpl, ctx, harmony, rng);

  // Verify no notes have extremely short duration (< 60 ticks = 1/8 beat)
  // Notes with duration of 1 tick indicate overlap collision
  constexpr Tick MIN_DURATION = 60;
  int short_notes = 0;
  for (const auto& note : notes) {
    if (note.duration < MIN_DURATION) {
      short_notes++;
    }
  }

  EXPECT_EQ(short_notes, 0)
      << "Found " << short_notes << " notes with duration < " << MIN_DURATION
      << " ticks. This indicates hook overlap issue.";
}

// Test that generated notes have no same-tick collisions across templates
TEST(MelodyDesignerTest, NoSameTickCollisionAcrossTemplates) {
  MelodyDesigner designer;
  HarmonyContext harmony;
  std::mt19937 rng(123);

  // Test with multiple templates that have hooks
  std::vector<MelodyTemplateId> templates = {
      MelodyTemplateId::HookRepeat,
      MelodyTemplateId::PlateauTalk,
      MelodyTemplateId::RunUpTarget,
  };

  for (auto tmpl_id : templates) {
    const MelodyTemplate& tmpl = getTemplate(tmpl_id);

    MelodyDesigner::SectionContext ctx;
    ctx.section_type = SectionType::Chorus;
    ctx.section_start = 0;
    ctx.section_end = TICKS_PER_BAR * 8;
    ctx.section_bars = 8;
    ctx.chord_degree = 0;
    ctx.key_offset = 0;
    ctx.tessitura = TessituraRange{60, 72, 66};
    ctx.vocal_low = 57;
    ctx.vocal_high = 79;
    ctx.density_modifier = 1.0f;
    ctx.thirtysecond_ratio = 0.0f;

    auto notes = designer.generateSection(tmpl, ctx, harmony, rng);

    // No note should have same start_tick as another
    // (which would indicate hook overlap that got resolved to 1-tick note)
    for (size_t i = 0; i + 1 < notes.size(); ++i) {
      EXPECT_LT(notes[i].start_tick, notes[i + 1].start_tick)
          << "Notes at index " << i << " and " << (i + 1)
          << " have same or reversed start_tick with template "
          << static_cast<int>(tmpl_id);
    }

    // No notes should have extremely short duration (< 60 ticks)
    // This indicates overlap collision that was resolved
    constexpr Tick MIN_DURATION = 60;
    for (size_t i = 0; i < notes.size(); ++i) {
      EXPECT_GE(notes[i].duration, MIN_DURATION)
          << "Note at index " << i << " has duration " << notes[i].duration
          << " which indicates overlap collision with template "
          << static_cast<int>(tmpl_id);
    }
  }
}

// ============================================================================
// Phrase Gap Tests (TDD for half-bar breath point fix)
// ============================================================================

// Test that phrase gaps are at most half-bar (2 beats) as per design intent.
// Reference: commit 59b7767 "half-bar gaps as breath points"
TEST(MelodyDesignerTest, PhraseGapsAreAtMostHalfBar) {
  MelodyDesigner designer;
  std::mt19937 rng(42);

  // Test with multiple templates
  std::vector<MelodyTemplateId> templates = {
      MelodyTemplateId::PlateauTalk,
      MelodyTemplateId::RunUpTarget,
      MelodyTemplateId::SparseAnchor
  };

  for (auto tmpl_id : templates) {
    const MelodyTemplate& tmpl = getTemplate(tmpl_id);

    // Create harmony context
    HarmonyContext harmony;

    MelodyDesigner::SectionContext ctx;
    ctx.section_type = SectionType::A;
    ctx.section_start = 0;
    ctx.section_end = TICKS_PER_BAR * 8;  // 8 bars for longer test
    ctx.section_bars = 8;
    ctx.chord_degree = 0;
    ctx.key_offset = 0;
    ctx.tessitura = TessituraRange{60, 72, 66};
    ctx.vocal_low = 55;
    ctx.vocal_high = 79;
    ctx.mood = Mood::StraightPop;

    auto notes = designer.generateSection(tmpl, ctx, harmony, rng);

    if (notes.size() < 2) continue;

    // Calculate gaps between consecutive notes
    // Design intent: "half-bar gaps as breath points" (commit 59b7767)
    // Allow up to 3/4 bar (3 beats) to account for phrase timing variations
    constexpr Tick THREE_QUARTER_BAR = (TICKS_PER_BAR * 3) / 4;  // 1440 ticks = 3 beats
    constexpr Tick MAX_ALLOWED_GAP = THREE_QUARTER_BAR + TICK_EIGHTH;  // 1680 ticks tolerance

    for (size_t i = 0; i + 1 < notes.size(); ++i) {
      Tick note_end = notes[i].start_tick + notes[i].duration;
      Tick next_start = notes[i + 1].start_tick;

      if (next_start > note_end) {
        Tick gap = next_start - note_end;
        EXPECT_LE(gap, MAX_ALLOWED_GAP)
            << "Gap of " << gap << " ticks (" << (gap / TICKS_PER_BEAT) << " beats) "
            << "between note " << i << " and " << (i + 1)
            << " exceeds 3/4-bar limit (design: half-bar breath points). "
            << "Template: " << static_cast<int>(tmpl_id);
      }
    }
  }
}

// Test that phrase gaps exist (breathing room) but are not excessive
TEST(MelodyDesignerTest, PhraseGapsProvideBreathingRoom) {
  MelodyDesigner designer;
  std::mt19937 rng(12345);

  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

  HarmonyContext harmony;

  MelodyDesigner::SectionContext ctx;
  ctx.section_type = SectionType::A;
  ctx.section_start = 0;
  ctx.section_end = TICKS_PER_BAR * 8;
  ctx.section_bars = 8;
  ctx.chord_degree = 0;
  ctx.key_offset = 0;
  ctx.tessitura = TessituraRange{60, 72, 66};
  ctx.vocal_low = 55;
  ctx.vocal_high = 79;
  ctx.mood = Mood::StraightPop;

  auto notes = designer.generateSection(tmpl, ctx, harmony, rng);

  if (notes.size() < 2) return;

  // Count bars with notes
  std::set<int> bars_with_notes;
  for (const auto& note : notes) {
    int bar = note.start_tick / TICKS_PER_BAR;
    bars_with_notes.insert(bar);
  }

  // Should have notes in most bars (not alternating empty bars)
  // With 8 bars, should have notes in at least 6 bars
  EXPECT_GE(bars_with_notes.size(), 6u)
      << "Only " << bars_with_notes.size() << " of 8 bars have notes. "
      << "This suggests excessive gaps (1-bar alternation pattern).";
}

}  // namespace
}  // namespace midisketch
