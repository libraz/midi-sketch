#include <gtest/gtest.h>
#include "core/harmony_context.h"
#include "core/melody_templates.h"
#include "track/melody_designer.h"
#include <random>

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
  ctx.tessitura = {60, 72, 66};  // C4 to C5
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
    EXPECT_GE(note.startTick, ctx.section_start);
    EXPECT_LE(note.startTick + note.duration, ctx.section_end + TICKS_PER_BEAT);
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

}  // namespace
}  // namespace midisketch
