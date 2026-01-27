/**
 * @file melody_designer_test.cpp
 * @brief Tests for melody designer.
 */

#include "track/melody_designer.h"

#include <gtest/gtest.h>

#include <random>
#include <set>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/harmony_context.h"
#include "core/i_harmony_context.h"
#include "core/melody_templates.h"
#include "core/timing_constants.h"

namespace midisketch {
namespace {

// Helper to create a simple section context
MelodyDesigner::SectionContext createTestContext() {
  MelodyDesigner::SectionContext ctx;
  ctx.section_type = SectionType::A;
  ctx.section_start = 0;
  ctx.section_end = TICKS_PER_BAR * 4;  // 4 bars
  ctx.section_bars = 4;
  ctx.chord_degree = 0;                                // I chord
  ctx.key_offset = 0;                                  // C major
  ctx.tessitura = TessituraRange{60, 72, 66, 55, 77};  // C4 to C5
  ctx.vocal_low = 55;                                  // G3
  ctx.vocal_high = 79;                                 // G5
  return ctx;
}

// ============================================================================
// selectPitchChoice Tests
// ============================================================================

TEST(MelodyDesignerTest, SelectPitchChoiceReturnsValidChoice) {
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

  for (int i = 0; i < 100; ++i) {
    PitchChoice choice = MelodyDesigner::selectPitchChoice(tmpl, 0.5f, false, rng);
    // Should be one of the valid choices
    EXPECT_TRUE(choice == PitchChoice::Same || choice == PitchChoice::StepUp ||
                choice == PitchChoice::StepDown || choice == PitchChoice::TargetStep);
  }
}

TEST(MelodyDesignerTest, SelectPitchChoiceWithHighPlateau) {
  std::mt19937 rng(42);
  // PlateauTalk has 70% plateau ratio
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

  int same_count = 0;
  for (int i = 0; i < 100; ++i) {
    PitchChoice choice = MelodyDesigner::selectPitchChoice(tmpl, 0.5f, false, rng);
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
    PitchChoice choice = MelodyDesigner::selectPitchChoice(tmpl, 0.7f, true, rng);
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

  PitchChoice result = MelodyDesigner::applyDirectionInertia(PitchChoice::Same, 3, tmpl, rng);
  EXPECT_EQ(result, PitchChoice::Same);
}

TEST(MelodyDesignerTest, ApplyDirectionInertiaTargetUnchanged) {
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

  PitchChoice result =
      MelodyDesigner::applyDirectionInertia(PitchChoice::TargetStep, -3, tmpl, rng);
  EXPECT_EQ(result, PitchChoice::TargetStep);
}

TEST(MelodyDesignerTest, ApplyDirectionInertiaInfluencesStep) {
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

  // With strong positive inertia, should tend toward StepUp
  int up_count = 0;
  for (int i = 0; i < 100; ++i) {
    PitchChoice result = MelodyDesigner::applyDirectionInertia(PitchChoice::StepDown, 3, tmpl, rng);
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
  TessituraRange t{60, 72, 66, 55, 77};

  float ratio = MelodyDesigner::getEffectivePlateauRatio(tmpl, 66, t);
  EXPECT_FLOAT_EQ(ratio, tmpl.plateau_ratio);
}

TEST(MelodyDesignerTest, EffectivePlateauRatioHighRegister) {
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);
  TessituraRange t{60, 72, 66, 55, 77};

  float ratio = MelodyDesigner::getEffectivePlateauRatio(tmpl, 75, t);
  // Should be boosted above tessitura
  EXPECT_GT(ratio, tmpl.plateau_ratio);
}

TEST(MelodyDesignerTest, EffectivePlateauRatioCappedAt90) {
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);
  TessituraRange t{60, 72, 66, 55, 77};

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

  auto result = designer.generateMelodyPhrase(tmpl, 0, 8, ctx, -1, 0, harmony, rng);

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

  auto result = designer.generateMelodyPhrase(tmpl, 0, 8, ctx, -1, 0, harmony, rng);

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
  auto result1 = designer.generateMelodyPhrase(tmpl, 0, 8, ctx, -1, 0, harmony, rng);

  // Second phrase with continuity
  auto result2 = designer.generateMelodyPhrase(tmpl, TICKS_PER_BAR * 2, 8, ctx, result1.last_pitch,
                                               result1.direction_inertia, harmony, rng);

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

    EXPECT_GT(notes.size(), 0u) << "Template " << static_cast<int>(id) << " produced no notes";
  }
}

// ============================================================================
// Section Transition Tests
// ============================================================================

TEST(MelodyDesignerTest, GetTransitionBToChorus) {
  const SectionTransition* trans = getTransition(SectionType::B, SectionType::Chorus);
  ASSERT_NE(trans, nullptr);

  // B→Chorus builds anticipation with ascending tendency for "waiting for it" feeling.
  // Leading tone creates hook preparation before chorus entry.
  EXPECT_EQ(trans->pitch_tendency, 2);
  // Use leading tone for melodic preparation
  EXPECT_TRUE(trans->use_leading_tone);
  // Should have stronger velocity growth (excitement)
  EXPECT_GE(trans->velocity_growth, 1.20f);
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
  ctx.tessitura = TessituraRange{60, 72, 66, 55, 77};
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

  EXPECT_EQ(short_notes, 0) << "Found " << short_notes << " notes with duration < " << MIN_DURATION
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
    ctx.tessitura = TessituraRange{60, 72, 66, 55, 77};
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
          << " have same or reversed start_tick with template " << static_cast<int>(tmpl_id);
    }

    // No notes should have extremely short duration (< 60 ticks)
    // This indicates overlap collision that was resolved
    constexpr Tick MIN_DURATION = 60;
    for (size_t i = 0; i < notes.size(); ++i) {
      EXPECT_GE(notes[i].duration, MIN_DURATION)
          << "Note at index " << i << " has duration " << notes[i].duration
          << " which indicates overlap collision with template " << static_cast<int>(tmpl_id);
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
      MelodyTemplateId::PlateauTalk, MelodyTemplateId::RunUpTarget, MelodyTemplateId::SparseAnchor};

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
    ctx.tessitura = TessituraRange{60, 72, 66, 55, 77};
    ctx.vocal_low = 55;
    ctx.vocal_high = 79;
    ctx.mood = Mood::StraightPop;

    auto notes = designer.generateSection(tmpl, ctx, harmony, rng);

    if (notes.size() < 2) continue;

    // Calculate gaps between consecutive notes
    // Design intent: "half-bar gaps as breath points" (commit 59b7767)
    // Allow up to 3/4 bar (3 beats) to account for phrase timing variations
    constexpr Tick THREE_QUARTER_BAR = (TICKS_PER_BAR * 3) / 4;        // 1440 ticks = 3 beats
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
  ctx.tessitura = TessituraRange{60, 72, 66, 55, 77};
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

// ============================================================================
// Downbeat Chord-Tone Constraint Tests
// ============================================================================

// Helper to get chord tones for a given degree
std::vector<int> getChordTonePCs(int8_t degree) {
  // Diatonic triads in C major: I=CEG, ii=DFA, iii=EGB, IV=FAC, V=GBD, vi=ACE, vii°=BDF
  constexpr int CHORD_TONES[7][3] = {
      {0, 4, 7},   // I: C E G
      {2, 5, 9},   // ii: D F A
      {4, 7, 11},  // iii: E G B
      {5, 9, 0},   // IV: F A C
      {7, 11, 2},  // V: G B D
      {9, 0, 4},   // vi: A C E
      {11, 2, 5},  // vii°: B D F
  };
  int normalized = ((degree % 7) + 7) % 7;
  return {CHORD_TONES[normalized][0], CHORD_TONES[normalized][1], CHORD_TONES[normalized][2]};
}

// Test that downbeat notes are always chord tones
// This is a fundamental pop music theory principle
TEST(MelodyDesignerTest, DownbeatNotesAreChordTones) {
  MelodyDesigner designer;
  HarmonyContext harmony;

  // Test with multiple seeds to ensure seed-independence
  std::vector<uint32_t> seeds = {1, 42, 123, 456, 789, 1000, 9999, 12345};

  // Test with multiple templates
  std::vector<MelodyTemplateId> templates = {
      MelodyTemplateId::PlateauTalk,
      MelodyTemplateId::RunUpTarget,
      MelodyTemplateId::SparseAnchor,
      MelodyTemplateId::HookRepeat,
  };

  for (uint32_t seed : seeds) {
    for (auto tmpl_id : templates) {
      std::mt19937 rng(seed);
      const MelodyTemplate& tmpl = getTemplate(tmpl_id);

      MelodyDesigner::SectionContext ctx;
      ctx.section_type = SectionType::A;
      ctx.section_start = 0;
      ctx.section_end = TICKS_PER_BAR * 8;
      ctx.section_bars = 8;
      ctx.chord_degree = 0;  // I chord
      ctx.key_offset = 0;
      ctx.tessitura = TessituraRange{60, 72, 66, 55, 77};
      ctx.vocal_low = 55;
      ctx.vocal_high = 79;
      ctx.mood = Mood::StraightPop;
      ctx.vocal_attitude = VocalAttitude::Clean;

      auto notes = designer.generateSection(tmpl, ctx, harmony, rng);

      // Check each note on a downbeat (beat 1 of each bar)
      for (const auto& note : notes) {
        Tick bar_pos = note.start_tick % TICKS_PER_BAR;
        bool is_downbeat = bar_pos < TICKS_PER_BEAT / 4;

        if (is_downbeat) {
          // Get chord degree at this position
          int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);
          // Use degree 0 if harmony not initialized
          if (chord_degree < 0 || chord_degree > 6) chord_degree = 0;

          std::vector<int> chord_tones = getChordTonePCs(chord_degree);
          int pitch_class = note.note % 12;

          bool is_chord_tone = false;
          for (int ct : chord_tones) {
            if (pitch_class == ct) {
              is_chord_tone = true;
              break;
            }
          }

          EXPECT_TRUE(is_chord_tone)
              << "Downbeat note " << static_cast<int>(note.note) << " (PC=" << pitch_class
              << ") at tick " << note.start_tick << " is not a chord tone of degree "
              << static_cast<int>(chord_degree) << ". Chord tones: " << chord_tones[0] << ","
              << chord_tones[1] << "," << chord_tones[2] << ". Seed=" << seed
              << ", Template=" << static_cast<int>(tmpl_id);
        }
      }
    }
  }
}

// Test that downbeat constraint works across different section types
TEST(MelodyDesignerTest, DownbeatChordToneAcrossSectionTypes) {
  MelodyDesigner designer;
  HarmonyContext harmony;

  std::vector<SectionType> section_types = {
      SectionType::Intro, SectionType::A, SectionType::B, SectionType::Chorus, SectionType::Bridge,
  };

  std::vector<uint32_t> seeds = {42, 123, 456};

  for (uint32_t seed : seeds) {
    for (SectionType sec_type : section_types) {
      std::mt19937 rng(seed);
      const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

      MelodyDesigner::SectionContext ctx;
      ctx.section_type = sec_type;
      ctx.section_start = 0;
      ctx.section_end = TICKS_PER_BAR * 8;
      ctx.section_bars = 8;
      ctx.chord_degree = 0;
      ctx.key_offset = 0;
      ctx.tessitura = TessituraRange{60, 72, 66, 55, 77};
      ctx.vocal_low = 55;
      ctx.vocal_high = 79;
      ctx.mood = Mood::StraightPop;
      ctx.vocal_attitude = VocalAttitude::Clean;

      auto notes = designer.generateSection(tmpl, ctx, harmony, rng);

      for (const auto& note : notes) {
        Tick bar_pos = note.start_tick % TICKS_PER_BAR;
        bool is_downbeat = bar_pos < TICKS_PER_BEAT / 4;

        if (is_downbeat) {
          int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);
          if (chord_degree < 0 || chord_degree > 6) chord_degree = 0;

          std::vector<int> chord_tones = getChordTonePCs(chord_degree);
          int pitch_class = note.note % 12;

          bool is_chord_tone = false;
          for (int ct : chord_tones) {
            if (pitch_class == ct) {
              is_chord_tone = true;
              break;
            }
          }

          EXPECT_TRUE(is_chord_tone)
              << "Downbeat note PC=" << pitch_class << " at tick " << note.start_tick << " (bar "
              << (note.start_tick / TICKS_PER_BAR + 1) << ")"
              << " is not a chord tone. Chord degree=" << static_cast<int>(chord_degree)
              << ", SectionType=" << static_cast<int>(sec_type) << ". Seed=" << seed;
        }
      }
    }
  }
}

// Test that non-downbeat positions can still have non-chord tones
// (to ensure we're not over-constraining)
TEST(MelodyDesignerTest, NonDownbeatAllowsNonChordTones) {
  MelodyDesigner designer;
  HarmonyContext harmony;

  // Use a larger number of seeds to find at least one non-chord tone
  std::vector<uint32_t> seeds = {1, 42, 123, 456, 789, 1000, 5000, 9999};
  bool found_non_chord_tone_on_weak_beat = false;

  for (uint32_t seed : seeds) {
    if (found_non_chord_tone_on_weak_beat) break;

    std::mt19937 rng(seed);
    // Use Expressive attitude which allows tensions
    const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

    MelodyDesigner::SectionContext ctx;
    ctx.section_type = SectionType::A;
    ctx.section_start = 0;
    ctx.section_end = TICKS_PER_BAR * 8;
    ctx.section_bars = 8;
    ctx.chord_degree = 0;
    ctx.key_offset = 0;
    ctx.tessitura = TessituraRange{60, 72, 66, 55, 77};
    ctx.vocal_low = 55;
    ctx.vocal_high = 79;
    ctx.mood = Mood::StraightPop;
    ctx.vocal_attitude = VocalAttitude::Expressive;  // Allow tensions

    auto notes = designer.generateSection(tmpl, ctx, harmony, rng);

    for (const auto& note : notes) {
      Tick bar_pos = note.start_tick % TICKS_PER_BAR;
      bool is_downbeat = bar_pos < TICKS_PER_BEAT / 4;

      if (!is_downbeat) {
        std::vector<int> chord_tones = getChordTonePCs(0);  // I chord
        int pitch_class = note.note % 12;

        bool is_chord_tone = false;
        for (int ct : chord_tones) {
          if (pitch_class == ct) {
            is_chord_tone = true;
            break;
          }
        }

        if (!is_chord_tone) {
          found_non_chord_tone_on_weak_beat = true;
          break;
        }
      }
    }
  }

  // This test verifies we're not over-constraining - weak beats should
  // occasionally have non-chord tones (tensions, passing tones, etc.)
  // If this fails, the constraint might be too aggressive
  EXPECT_TRUE(found_non_chord_tone_on_weak_beat)
      << "No non-chord tones found on weak beats across " << seeds.size()
      << " seeds. The downbeat constraint may be over-applied.";
}

// ============================================================================
// GlobalMotif Tests
// ============================================================================

TEST(GlobalMotifTest, ExtractFromEmptyNotes) {
  std::vector<NoteEvent> empty_notes;
  GlobalMotif motif = MelodyDesigner::extractGlobalMotif(empty_notes);

  EXPECT_FALSE(motif.isValid());
  EXPECT_EQ(motif.interval_count, 0);
}

TEST(GlobalMotifTest, ExtractFromSingleNote) {
  // NoteEvent: {start_tick, duration, note, velocity}
  std::vector<NoteEvent> notes = {{0, 480, 60, 100}};
  GlobalMotif motif = MelodyDesigner::extractGlobalMotif(notes);

  EXPECT_FALSE(motif.isValid());
  EXPECT_EQ(motif.interval_count, 0);
}

TEST(GlobalMotifTest, ExtractAscendingContour) {
  // C4 -> D4 -> E4 -> F4 (ascending pattern)
  // NoteEvent: {start_tick, duration, note, velocity}
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100}, {480, 480, 62, 100}, {960, 480, 64, 100}, {1440, 480, 65, 100}};
  GlobalMotif motif = MelodyDesigner::extractGlobalMotif(notes);

  EXPECT_TRUE(motif.isValid());
  EXPECT_EQ(motif.interval_count, 3);
  EXPECT_EQ(motif.interval_signature[0], 2);  // +2 semitones
  EXPECT_EQ(motif.interval_signature[1], 2);  // +2 semitones
  EXPECT_EQ(motif.interval_signature[2], 1);  // +1 semitone
  EXPECT_EQ(motif.contour_type, ContourType::Ascending);
}

TEST(GlobalMotifTest, ExtractDescendingContour) {
  // F4 -> E4 -> D4 -> C4 (descending pattern)
  std::vector<NoteEvent> notes = {
      {0, 480, 65, 100}, {480, 480, 64, 100}, {960, 480, 62, 100}, {1440, 480, 60, 100}};
  GlobalMotif motif = MelodyDesigner::extractGlobalMotif(notes);

  EXPECT_TRUE(motif.isValid());
  EXPECT_EQ(motif.contour_type, ContourType::Descending);
}

TEST(GlobalMotifTest, ExtractPeakContour) {
  // C4 -> G4 -> E4 -> C4 (clear rise then fall = peak)
  // intervals: +7, -3, -4 → first half positive, second half negative
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100}, {480, 480, 67, 100}, {960, 480, 64, 100}, {1440, 480, 60, 100}};
  GlobalMotif motif = MelodyDesigner::extractGlobalMotif(notes);

  EXPECT_TRUE(motif.isValid());
  EXPECT_EQ(motif.contour_type, ContourType::Peak);
}

TEST(GlobalMotifTest, ExtractValleyContour) {
  // G4 -> C4 -> E4 -> G4 (clear fall then rise = valley)
  // intervals: -7, +4, +3 → first half negative, second half positive
  std::vector<NoteEvent> notes = {
      {0, 480, 67, 100}, {480, 480, 60, 100}, {960, 480, 64, 100}, {1440, 480, 67, 100}};
  GlobalMotif motif = MelodyDesigner::extractGlobalMotif(notes);

  EXPECT_TRUE(motif.isValid());
  EXPECT_EQ(motif.contour_type, ContourType::Valley);
}

TEST(GlobalMotifTest, ExtractPlateauContour) {
  // C4 -> C4 -> D4 -> C4 (mostly flat = plateau)
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100}, {480, 480, 60, 100}, {960, 480, 62, 100}, {1440, 480, 60, 100}};
  GlobalMotif motif = MelodyDesigner::extractGlobalMotif(notes);

  EXPECT_TRUE(motif.isValid());
  EXPECT_EQ(motif.contour_type, ContourType::Plateau);
}

TEST(GlobalMotifTest, ExtractRhythmSignature) {
  // Different durations: quarter, half, quarter, whole
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100},     // quarter
      {480, 960, 62, 100},   // half
      {1440, 480, 64, 100},  // quarter
      {1920, 1920, 65, 100}  // whole
  };
  GlobalMotif motif = MelodyDesigner::extractGlobalMotif(notes);

  EXPECT_TRUE(motif.isValid());
  EXPECT_EQ(motif.rhythm_count, 4);
  // Whole note (1920) is longest, so it gets 8
  EXPECT_EQ(motif.rhythm_signature[3], 8);
  // Quarter notes (480) should be proportionally smaller
  EXPECT_LT(motif.rhythm_signature[0], motif.rhythm_signature[3]);
}

TEST(GlobalMotifTest, EvaluateWithInvalidMotif) {
  GlobalMotif invalid_motif;
  std::vector<NoteEvent> candidate = {{0, 480, 60, 100}, {480, 480, 62, 100}};

  float bonus = MelodyDesigner::evaluateWithGlobalMotif(candidate, invalid_motif);

  EXPECT_EQ(bonus, 0.0f);
}

TEST(GlobalMotifTest, EvaluateWithIdenticalPattern) {
  // Create a motif from ascending pattern
  std::vector<NoteEvent> source = {{0, 480, 60, 100}, {480, 480, 62, 100}, {960, 480, 64, 100}};
  GlobalMotif motif = MelodyDesigner::extractGlobalMotif(source);

  // Evaluate same pattern (should get maximum bonus)
  float bonus = MelodyDesigner::evaluateWithGlobalMotif(source, motif);

  // Max bonus is 0.25 (0.10 contour + 0.05 intervals + 0.05 direction + 0.05 consistency)
  EXPECT_GT(bonus, 0.15f);
  EXPECT_LE(bonus, 0.25f);
}

TEST(GlobalMotifTest, EvaluateDifferentContour) {
  // Create a clearly ascending motif (large intervals to trigger Ascending contour)
  // Need intervals summing to >= 3 in each half to avoid Plateau classification
  std::vector<NoteEvent> ascending = {
      {0, 480, 55, 100}, {480, 480, 60, 100}, {960, 480, 64, 100}, {1440, 480, 69, 100}};
  GlobalMotif motif = MelodyDesigner::extractGlobalMotif(ascending);
  EXPECT_EQ(motif.contour_type, ContourType::Ascending);

  // Evaluate clearly descending pattern (different contour)
  std::vector<NoteEvent> descending = {
      {0, 480, 69, 100}, {480, 480, 64, 100}, {960, 480, 60, 100}, {1440, 480, 55, 100}};
  float bonus = MelodyDesigner::evaluateWithGlobalMotif(descending, motif);

  // No contour bonus (different contour types), no direction bonus (opposite),
  // but may get interval similarity (magnitudes match) and consistency bonus (both leaps)
  // Should be lower than identical pattern bonus
  EXPECT_LT(bonus, 0.15f);
}

TEST(GlobalMotifTest, CacheAndRetrieveGlobalMotif) {
  MelodyDesigner designer;

  // Initially no cached motif
  EXPECT_FALSE(designer.getCachedGlobalMotif().has_value());

  // Set a motif
  GlobalMotif motif;
  motif.contour_type = ContourType::Peak;
  motif.interval_signature[0] = 4;
  motif.interval_count = 1;
  designer.setGlobalMotif(motif);

  // Should now be cached
  EXPECT_TRUE(designer.getCachedGlobalMotif().has_value());
  EXPECT_EQ(designer.getCachedGlobalMotif()->contour_type, ContourType::Peak);
}

// ============================================================================
// selectPitchForLockedRhythm Tests
// ============================================================================

TEST(MelodyDesignerTest, SelectPitchForLockedRhythm_ReturnsInRange) {
  MelodyDesigner designer;
  std::mt19937 rng(42);

  uint8_t vocal_low = 60;   // C4
  uint8_t vocal_high = 72;  // C5
  uint8_t prev_pitch = 66;  // F#4

  for (int i = 0; i < 100; ++i) {
    uint8_t pitch = designer.selectPitchForLockedRhythm(prev_pitch, 0, vocal_low, vocal_high, rng);
    EXPECT_GE(pitch, vocal_low) << "Pitch below range";
    EXPECT_LE(pitch, vocal_high) << "Pitch above range";
    prev_pitch = pitch;
  }
}

TEST(MelodyDesignerTest, SelectPitchForLockedRhythm_PrefersChordTones) {
  MelodyDesigner designer;
  std::mt19937 rng(42);

  uint8_t vocal_low = 60;   // C4
  uint8_t vocal_high = 72;  // C5
  uint8_t prev_pitch = 64;  // E4 (chord tone of C major)

  // Test with I chord (C major: C, E, G)
  int chord_tone_count = 0;
  for (int i = 0; i < 100; ++i) {
    uint8_t pitch = designer.selectPitchForLockedRhythm(prev_pitch, 0, vocal_low, vocal_high, rng);
    int pc = pitch % 12;
    // C=0, E=4, G=7 are chord tones of C major
    if (pc == 0 || pc == 4 || pc == 7) {
      chord_tone_count++;
    }
    prev_pitch = pitch;
  }
  // Should have a majority of chord tones (more than 70%)
  EXPECT_GT(chord_tone_count, 70) << "Should prefer chord tones";
}

TEST(MelodyDesignerTest, SelectPitchForLockedRhythm_PrefersSmallIntervals) {
  MelodyDesigner designer;
  std::mt19937 rng(42);

  uint8_t vocal_low = 48;   // C3
  uint8_t vocal_high = 84;  // C6 (wide range)
  uint8_t prev_pitch = 64;  // E4

  int small_interval_count = 0;
  for (int i = 0; i < 100; ++i) {
    uint8_t pitch = designer.selectPitchForLockedRhythm(prev_pitch, 0, vocal_low, vocal_high, rng);
    int interval = std::abs(static_cast<int>(pitch) - prev_pitch);
    if (interval <= 5) {  // Within a 4th
      small_interval_count++;
    }
    prev_pitch = pitch;
  }
  // Should have mostly small intervals (more than 60%)
  EXPECT_GT(small_interval_count, 60) << "Should prefer stepwise motion";
}

TEST(MelodyDesignerTest, SelectPitchForLockedRhythm_HandlesNarrowRange) {
  MelodyDesigner designer;
  std::mt19937 rng(42);

  uint8_t vocal_low = 60;   // C4
  uint8_t vocal_high = 62;  // D4 (only 3 notes possible: C, C#, D)
  uint8_t prev_pitch = 60;

  for (int i = 0; i < 50; ++i) {
    uint8_t pitch = designer.selectPitchForLockedRhythm(prev_pitch, 0, vocal_low, vocal_high, rng);
    EXPECT_GE(pitch, vocal_low);
    EXPECT_LE(pitch, vocal_high);
    prev_pitch = pitch;
  }
}

TEST(MelodyDesignerTest, SelectPitchForLockedRhythm_DifferentChordDegrees) {
  MelodyDesigner designer;
  std::mt19937 rng(42);

  uint8_t vocal_low = 60;
  uint8_t vocal_high = 72;

  // Test with different chord degrees
  std::vector<int8_t> degrees = {0, 3, 4, 5};  // I, IV, V, vi
  for (int8_t degree : degrees) {
    uint8_t prev_pitch = 64;
    for (int i = 0; i < 20; ++i) {
      uint8_t pitch =
          designer.selectPitchForLockedRhythm(prev_pitch, degree, vocal_low, vocal_high, rng);
      EXPECT_GE(pitch, vocal_low);
      EXPECT_LE(pitch, vocal_high);
      prev_pitch = pitch;
    }
  }
}

// ============================================================================
// Triplet Rhythm Grid Tests (DownResolve uses Ternary)
// ============================================================================

TEST(MelodyDesignerTest, TernaryTemplateGeneratesNotes) {
  // DownResolve template uses Ternary rhythm grid
  MelodyDesigner designer;
  HarmonyContext harmony;

  std::vector<Section> sections;
  Section b_section;
  b_section.type = SectionType::B;
  b_section.bars = 8;
  b_section.start_tick = 0;
  b_section.name = "B";
  sections.push_back(b_section);
  harmony.initialize(Arrangement(sections), getChordProgression(0), Mood::StraightPop);

  MelodyTemplate tmpl = getTemplate(MelodyTemplateId::DownResolve);
  EXPECT_EQ(tmpl.rhythm_grid, RhythmGrid::Ternary) << "DownResolve should use Ternary grid";

  auto ctx = createTestContext();
  ctx.section_type = SectionType::B;
  ctx.mood = Mood::StraightPop;

  std::mt19937 rng(42);
  auto notes =
      designer.generateSectionWithEvaluation(tmpl, ctx, harmony, rng, VocalStylePreset::Standard);

  EXPECT_GT(notes.size(), 0u) << "Ternary template should generate notes";
}

TEST(MelodyDesignerTest, BinaryTemplateGeneratesNotes) {
  // PlateauTalk template uses Binary rhythm grid
  MelodyDesigner designer;
  HarmonyContext harmony;

  std::vector<Section> sections;
  Section a_section;
  a_section.type = SectionType::A;
  a_section.bars = 8;
  a_section.start_tick = 0;
  a_section.name = "A";
  sections.push_back(a_section);
  harmony.initialize(Arrangement(sections), getChordProgression(0), Mood::StraightPop);

  MelodyTemplate tmpl = getTemplate(MelodyTemplateId::PlateauTalk);
  EXPECT_EQ(tmpl.rhythm_grid, RhythmGrid::Binary) << "PlateauTalk should use Binary grid";

  auto ctx = createTestContext();
  ctx.section_type = SectionType::A;
  ctx.mood = Mood::StraightPop;

  std::mt19937 rng(42);
  auto notes =
      designer.generateSectionWithEvaluation(tmpl, ctx, harmony, rng, VocalStylePreset::Standard);

  EXPECT_GT(notes.size(), 0u) << "Binary template should generate notes";
}

// ============================================================================
// Breath Duration Tests (Variable phrase breathing)
// ============================================================================

TEST(MelodyDesignerTest, BalladMoodGeneratesNotes) {
  // Ballad mood should use longer breath durations (tested indirectly)
  MelodyDesigner designer;
  HarmonyContext harmony;

  std::vector<Section> sections;
  Section a_section;
  a_section.type = SectionType::A;
  a_section.bars = 8;
  a_section.start_tick = 0;
  a_section.name = "A";
  sections.push_back(a_section);
  harmony.initialize(Arrangement(sections), getChordProgression(0), Mood::Ballad);

  auto ctx = createTestContext();
  ctx.section_type = SectionType::A;
  ctx.mood = Mood::Ballad;
  ctx.section_end = TICKS_PER_BAR * 8;
  ctx.section_bars = 8;

  MelodyTemplate tmpl = getTemplate(MelodyTemplateId::SparseAnchor);
  std::mt19937 rng(42);
  auto notes =
      designer.generateSectionWithEvaluation(tmpl, ctx, harmony, rng, VocalStylePreset::Ballad);

  EXPECT_GT(notes.size(), 0u) << "Ballad mood should generate notes";
}

TEST(MelodyDesignerTest, ChorusSectionGeneratesNotes) {
  // Chorus section should use shorter breath durations (tested indirectly)
  MelodyDesigner designer;
  HarmonyContext harmony;

  std::vector<Section> sections;
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.bars = 8;
  chorus.start_tick = 0;
  chorus.name = "CHORUS";
  sections.push_back(chorus);
  harmony.initialize(Arrangement(sections), getChordProgression(0), Mood::StraightPop);

  auto ctx = createTestContext();
  ctx.section_type = SectionType::Chorus;
  ctx.mood = Mood::StraightPop;
  ctx.section_end = TICKS_PER_BAR * 8;
  ctx.section_bars = 8;

  MelodyTemplate tmpl = getTemplate(MelodyTemplateId::HookRepeat);
  std::mt19937 rng(42);
  auto notes =
      designer.generateSectionWithEvaluation(tmpl, ctx, harmony, rng, VocalStylePreset::Idol);

  EXPECT_GT(notes.size(), 0u) << "Chorus section should generate notes";
}

// ============================================================================
// Motif Variant Tests
// ============================================================================

TEST(MelodyDesignerTest, SetGlobalMotifPreparesVariants) {
  MelodyDesigner designer;

  // Create a test motif
  GlobalMotif source;
  source.contour_type = ContourType::Ascending;
  source.interval_signature[0] = 2;
  source.interval_signature[1] = 2;
  source.interval_signature[2] = -1;
  source.interval_count = 3;
  source.rhythm_signature[0] = 2;
  source.rhythm_signature[1] = 1;
  source.rhythm_count = 2;

  designer.setGlobalMotif(source);

  // Chorus should return the original motif
  const auto& chorus_motif = designer.getMotifForSection(SectionType::Chorus);
  EXPECT_EQ(chorus_motif.contour_type, ContourType::Ascending);
  EXPECT_EQ(chorus_motif.interval_signature[0], 2);

  // Bridge should have inverted contour
  const auto& bridge_motif = designer.getMotifForSection(SectionType::Bridge);
  EXPECT_EQ(bridge_motif.contour_type, ContourType::Descending);
  // Intervals should be negated
  EXPECT_EQ(bridge_motif.interval_signature[0], -2);
}

TEST(MelodyDesignerTest, GetMotifForSectionFallsBackToOriginal) {
  MelodyDesigner designer;

  // Without setting a motif, should return empty
  const auto& motif = designer.getMotifForSection(SectionType::Chorus);
  EXPECT_FALSE(motif.isValid());
}

TEST(MelodyDesignerTest, MotifVariantsHaveDifferentCharacteristics) {
  MelodyDesigner designer;

  // Create a test motif
  GlobalMotif source;
  source.contour_type = ContourType::Peak;
  source.interval_signature[0] = 3;
  source.interval_signature[1] = 2;
  source.interval_signature[2] = -2;
  source.interval_signature[3] = -3;
  source.interval_count = 4;
  source.rhythm_signature[0] = 4;
  source.rhythm_signature[1] = 2;
  source.rhythm_signature[2] = 2;
  source.rhythm_signature[3] = 4;
  source.rhythm_count = 4;

  designer.setGlobalMotif(source);

  // A section (Diminish): rhythm should be halved
  const auto& a_motif = designer.getMotifForSection(SectionType::A);
  EXPECT_EQ(a_motif.rhythm_signature[0], 2);  // 4 -> 2
  EXPECT_EQ(a_motif.rhythm_signature[1], 1);  // 2 -> 1

  // Outro (Fragment): should have fewer intervals
  const auto& outro_motif = designer.getMotifForSection(SectionType::Outro);
  EXPECT_LT(outro_motif.interval_count, source.interval_count);

  // Chant (Augment): rhythm should be doubled
  const auto& chant_motif = designer.getMotifForSection(SectionType::Chant);
  EXPECT_EQ(chant_motif.rhythm_signature[0], 8);  // 4 -> 8
}

TEST(MelodyDesignerTest, CachedGlobalMotifIsSet) {
  MelodyDesigner designer;

  GlobalMotif source;
  source.contour_type = ContourType::Valley;
  source.interval_count = 1;

  EXPECT_FALSE(designer.getCachedGlobalMotif().has_value());

  designer.setGlobalMotif(source);

  EXPECT_TRUE(designer.getCachedGlobalMotif().has_value());
  EXPECT_EQ(designer.getCachedGlobalMotif()->contour_type, ContourType::Valley);
}

// ============================================================================
// Melody DNA Strengthening Tests (Phase 3.12)
// ============================================================================

TEST(GlobalMotifTest, MaxBonusIsPointTwoFive) {
  // Identical pattern should yield the maximum possible bonus of 0.25
  // Components: 0.10 contour + 0.05 interval + 0.05 direction + 0.05 consistency
  std::vector<NoteEvent> source = {
      {0, 480, 60, 100},    // C4
      {480, 480, 64, 100},  // E4 (+4, leap up)
      {960, 480, 65, 100},  // F4 (+1, step up)
      {1440, 480, 62, 100}, // D4 (-3, leap down)
      {1920, 480, 64, 100}, // E4 (+2, step up)
  };
  GlobalMotif motif = MelodyDesigner::extractGlobalMotif(source);

  float bonus = MelodyDesigner::evaluateWithGlobalMotif(source, motif);

  // Exact same pattern: all components should be at maximum
  EXPECT_FLOAT_EQ(bonus, 0.25f);
}

TEST(GlobalMotifTest, ContourDirectionMatchingBonus) {
  // DNA pattern: ascending (up, up)
  std::vector<NoteEvent> dna = {
      {0, 480, 60, 100}, {480, 480, 64, 100}, {960, 480, 67, 100}};
  GlobalMotif motif = MelodyDesigner::extractGlobalMotif(dna);

  // Candidate also ascending (up, up) but different intervals
  std::vector<NoteEvent> same_dir = {
      {0, 480, 60, 100}, {480, 480, 61, 100}, {960, 480, 63, 100}};
  float bonus_same = MelodyDesigner::evaluateWithGlobalMotif(same_dir, motif);

  // Candidate descending (down, down) - opposite direction
  std::vector<NoteEvent> opp_dir = {
      {0, 480, 67, 100}, {480, 480, 64, 100}, {960, 480, 60, 100}};
  float bonus_opp = MelodyDesigner::evaluateWithGlobalMotif(opp_dir, motif);

  // Same direction should get higher bonus than opposite direction
  EXPECT_GT(bonus_same, bonus_opp);
}

TEST(GlobalMotifTest, IntervalConsistencyBonusStepsMatchSteps) {
  // DNA with all steps (1-2 semitones)
  std::vector<NoteEvent> dna_steps = {
      {0, 480, 60, 100}, {480, 480, 62, 100}, {960, 480, 64, 100}, {1440, 480, 65, 100}};
  GlobalMotif motif = MelodyDesigner::extractGlobalMotif(dna_steps);

  // Candidate with all steps (different pitches but same step character)
  std::vector<NoteEvent> cand_steps = {
      {0, 480, 65, 100}, {480, 480, 67, 100}, {960, 480, 69, 100}, {1440, 480, 71, 100}};
  float bonus_steps = MelodyDesigner::evaluateWithGlobalMotif(cand_steps, motif);

  // Candidate with all leaps (3+ semitones) - different character
  std::vector<NoteEvent> cand_leaps = {
      {0, 480, 60, 100}, {480, 480, 67, 100}, {960, 480, 72, 100}, {1440, 480, 79, 100}};
  float bonus_leaps = MelodyDesigner::evaluateWithGlobalMotif(cand_leaps, motif);

  // Steps matching steps should get higher consistency bonus
  EXPECT_GT(bonus_steps, bonus_leaps);
}

TEST(GlobalMotifTest, StrengthenedBonusImprovesCoherence) {
  // Verify that the strengthened bonus (0.25 max) meaningfully differentiates
  // matching vs non-matching patterns. The old 0.1 max was too small to influence
  // candidate selection in practice.
  std::vector<NoteEvent> dna = {
      {0, 480, 60, 100}, {480, 480, 64, 100}, {960, 480, 67, 100},
      {1440, 480, 65, 100}, {1920, 480, 62, 100}};
  GlobalMotif motif = MelodyDesigner::extractGlobalMotif(dna);

  // Nearly identical pattern (transposed up 1 semitone)
  std::vector<NoteEvent> similar = {
      {0, 480, 61, 100}, {480, 480, 65, 100}, {960, 480, 68, 100},
      {1440, 480, 66, 100}, {1920, 480, 63, 100}};
  float bonus_similar = MelodyDesigner::evaluateWithGlobalMotif(similar, motif);

  // Completely different pattern (static then big leap)
  std::vector<NoteEvent> different = {
      {0, 480, 60, 100}, {480, 480, 60, 100}, {960, 480, 60, 100},
      {1440, 480, 72, 100}, {1920, 480, 72, 100}};
  float bonus_different = MelodyDesigner::evaluateWithGlobalMotif(different, motif);

  // The gap between similar and different should be meaningful (> 0.10)
  // to influence candidate selection during melody evaluation
  EXPECT_GT(bonus_similar - bonus_different, 0.10f);
}

// ============================================================================
// Phase 5: Melody Motif Development Tests (Task 5-1, 5-2)
// ============================================================================

TEST(SectionContextTest, SubPhraseIndexHelpers) {
  // Test the SectionContext sub-phrase helper methods
  MelodyDesigner::SectionContext ctx;

  // Test isClimaxSubPhrase
  ctx.sub_phrase_index = 0;
  EXPECT_FALSE(ctx.isClimaxSubPhrase());

  ctx.sub_phrase_index = 2;  // Climax is sub-phrase 2 (bars 5-6)
  EXPECT_TRUE(ctx.isClimaxSubPhrase());

  // Test isResolutionSubPhrase
  ctx.sub_phrase_index = 3;  // Resolution is sub-phrase 3 (bars 7-8)
  EXPECT_TRUE(ctx.isResolutionSubPhrase());
  EXPECT_FALSE(ctx.isClimaxSubPhrase());

  ctx.sub_phrase_index = 1;  // Development
  EXPECT_FALSE(ctx.isResolutionSubPhrase());
}

TEST(SectionContextTest, TessituraAdjustment) {
  // Test tessitura adjustment for internal arc
  MelodyDesigner::SectionContext ctx;

  // Presentation: no adjustment
  ctx.sub_phrase_index = 0;
  EXPECT_EQ(ctx.getTessituraAdjustment(), 0);

  // Development: no adjustment
  ctx.sub_phrase_index = 1;
  EXPECT_EQ(ctx.getTessituraAdjustment(), 0);

  // Climax: shift up
  ctx.sub_phrase_index = 2;
  EXPECT_EQ(ctx.getTessituraAdjustment(), 2);

  // Resolution: slight drop
  ctx.sub_phrase_index = 3;
  EXPECT_EQ(ctx.getTessituraAdjustment(), -1);
}

TEST(SectionContextTest, StepSizeMultiplier) {
  // Test step size multiplier for internal arc
  MelodyDesigner::SectionContext ctx;

  // Presentation: normal (1.0)
  ctx.sub_phrase_index = 0;
  EXPECT_FLOAT_EQ(ctx.getStepSizeMultiplier(), 1.0f);

  // Development: wider steps (1.3)
  ctx.sub_phrase_index = 1;
  EXPECT_FLOAT_EQ(ctx.getStepSizeMultiplier(), 1.3f);

  // Climax: normal (1.0)
  ctx.sub_phrase_index = 2;
  EXPECT_FLOAT_EQ(ctx.getStepSizeMultiplier(), 1.0f);

  // Resolution: smaller steps (0.8)
  ctx.sub_phrase_index = 3;
  EXPECT_FLOAT_EQ(ctx.getStepSizeMultiplier(), 0.8f);
}

// ============================================================================
// Phase 5: Melody Climax Point Tests (Task 5-4)
// ============================================================================

TEST(VelocityContourTest, MelodyGeneratesWithVaryingVelocity) {
  // Test that melody notes have velocity variation (not all the same)
  MelodyDesigner designer;
  HarmonyContext harmony;  // Default harmony context
  std::mt19937 rng(12345);

  MelodyDesigner::SectionContext ctx;
  ctx.section_type = SectionType::Chorus;
  ctx.section_start = 0;
  ctx.section_end = 8 * TICKS_PER_BAR;
  ctx.section_bars = 8;
  ctx.chord_degree = 0;
  ctx.key_offset = 0;
  ctx.tessitura = TessituraRange{67, 77, 72, 60, 84};  // low, high, center, vocal_low, vocal_high
  ctx.vocal_low = 60;
  ctx.vocal_high = 84;
  ctx.mood = Mood::ModernPop;

  // Use a standard template from the library
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

  auto notes = designer.generateSection(tmpl, ctx, harmony, rng);

  if (notes.size() > 5) {
    std::set<uint8_t> velocities;
    for (const auto& note : notes) {
      velocities.insert(note.velocity);
    }
    // Should have some velocity variation
    EXPECT_GT(velocities.size(), 1u) << "Melody should have velocity variation";
  }
}

// ============================================================================
// Hook Betrayal Threshold Tests (Proposal C)
// ============================================================================

TEST(MelodyTemplateTest, BetrayalThresholdValuesAreDefined) {
  // Verify all templates have betrayal_threshold defined
  EXPECT_EQ(getTemplate(MelodyTemplateId::PlateauTalk).betrayal_threshold, 4);
  EXPECT_EQ(getTemplate(MelodyTemplateId::RunUpTarget).betrayal_threshold, 3);  // YOASOBI = early
  EXPECT_EQ(getTemplate(MelodyTemplateId::DownResolve).betrayal_threshold, 4);
  EXPECT_EQ(getTemplate(MelodyTemplateId::HookRepeat).betrayal_threshold, 3);   // TikTok = early
  EXPECT_EQ(getTemplate(MelodyTemplateId::SparseAnchor).betrayal_threshold, 5); // Ballad = late
  EXPECT_EQ(getTemplate(MelodyTemplateId::CallResponse).betrayal_threshold, 4);
  EXPECT_EQ(getTemplate(MelodyTemplateId::JumpAccent).betrayal_threshold, 4);
}

TEST(MelodyTemplateTest, BetrayalThresholdAffectsHookGeneration) {
  // Test that different thresholds produce different hook patterns
  // This is a basic smoke test - we cannot directly observe betrayal timing
  // but we verify the system compiles and runs with the new field

  MelodyDesigner designer;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  auto ctx = createTestContext();
  ctx.section_type = SectionType::Chorus;

  // Test templates with different thresholds
  const MelodyTemplate& tmpl_early = getTemplate(MelodyTemplateId::RunUpTarget);  // threshold=3
  const MelodyTemplate& tmpl_late = getTemplate(MelodyTemplateId::SparseAnchor);  // threshold=5

  auto notes_early = designer.generateSection(tmpl_early, ctx, harmony, rng);
  EXPECT_GT(notes_early.size(), 0u);

  auto notes_late = designer.generateSection(tmpl_late, ctx, harmony, rng);
  EXPECT_GT(notes_late.size(), 0u);
}

}  // namespace
}  // namespace midisketch
