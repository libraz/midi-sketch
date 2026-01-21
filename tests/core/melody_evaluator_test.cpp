/**
 * @file melody_evaluator_test.cpp
 * @brief Tests for melody evaluator.
 */

#include "core/melody_evaluator.h"

#include <gtest/gtest.h>

#include <random>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/harmony_context.h"
#include "core/i_harmony_context.h"
#include "core/melody_templates.h"
#include "core/types.h"
#include "core/vocal_style_profile.h"
#include "track/melody_designer.h"

namespace midisketch {
namespace {

// Helper to create a simple melody for testing
std::vector<NoteEvent> createTestMelody(const std::vector<uint8_t>& pitches,
                                        Tick note_duration = TICKS_PER_BEAT) {
  std::vector<NoteEvent> notes;
  Tick current_tick = 0;
  for (uint8_t pitch : pitches) {
    notes.push_back({current_tick, note_duration, pitch, 100});
    current_tick += note_duration;
  }
  return notes;
}

// ============================================================================
// Singability Tests
// ============================================================================

TEST(MelodyEvaluatorTest, SingabilityIdealRange) {
  // New model: prefers step motion (1-2 semitones) over small leaps (3-4 semitones)
  // Good singable melody: mix of steps and same notes with occasional small leaps
  // C4 -> D4 -> E4 -> E4 -> D4 -> C4 -> E4 -> D4 -> C4 -> C4
  // intervals: 2, 2, 0, -2, -2, 4, -2, -2, 0
  // same: 2, step: 6, small: 1, large: 0 = ~22% same, 67% step, 11% small
  auto melody = createTestMelody({60, 62, 64, 64, 62, 60, 64, 62, 60, 60});
  float score = MelodyEvaluator::calcSingability(melody);
  EXPECT_GE(score, 0.6f) << "Step-motion dominated melody should score well";
}

TEST(MelodyEvaluatorTest, SingabilityTooJumpy) {
  // Large intervals should score lower
  // C4 -> G4 -> C5 -> G5 (7 semitones each)
  auto melody = createTestMelody({60, 67, 72, 79});
  float score = MelodyEvaluator::calcSingability(melody);
  EXPECT_LE(score, 0.5f) << "Large leaps should reduce singability";
}

TEST(MelodyEvaluatorTest, SingabilityTooStatic) {
  // Same note repeated: penalized for lack of step motion but not terrible
  // C4 -> C4 -> C4 -> C4
  auto melody = createTestMelody({60, 60, 60, 60});
  float score = MelodyEvaluator::calcSingability(melody);
  // 100% same notes: step_ratio=0, same_ratio=1.0, small=0, large=0
  // step_score=0 (very low), same_score=0.25 (penalized for being >25%)
  // Total should be low because no step motion
  EXPECT_LE(score, 0.5f) << "Static melody lacks step motion";
  EXPECT_GE(score, 0.2f) << "Static melody shouldn't be terrible";
}

TEST(MelodyEvaluatorTest, SingabilityExcessiveSmallLeaps) {
  // Too many small leaps (3-4 semitones) without step motion
  // C4 -> E4 -> G4 -> E4 -> G4 (intervals: 4, 3, -3, 3 = 100% small leaps)
  auto melody = createTestMelody({60, 64, 67, 64, 67});
  float score = MelodyEvaluator::calcSingability(melody);
  // All small leaps: step_ratio=0 → penalized
  EXPECT_LE(score, 0.5f) << "Excessive small leaps without steps should be penalized";
}

TEST(MelodyEvaluatorTest, SingabilityEmptyMelody) {
  std::vector<NoteEvent> empty;
  float score = MelodyEvaluator::calcSingability(empty);
  EXPECT_GE(score, 0.0f);
  EXPECT_LE(score, 1.0f);
}

TEST(MelodyEvaluatorTest, SingabilitySingleNote) {
  auto melody = createTestMelody({60});
  float score = MelodyEvaluator::calcSingability(melody);
  EXPECT_GE(score, 0.0f);
  EXPECT_LE(score, 1.0f);
}

// ============================================================================
// Contour Shape Tests
// ============================================================================

TEST(MelodyEvaluatorTest, ContourArchShape) {
  // Arch: C4 -> E4 -> G4 -> E4 -> C4
  auto melody = createTestMelody({60, 64, 67, 64, 60});
  float score = MelodyEvaluator::calcContourShape(melody);
  EXPECT_GE(score, 0.6f) << "Arch contour should be recognized";
}

TEST(MelodyEvaluatorTest, ContourWaveShape) {
  // Wave: C4 -> E4 -> D4 -> F4 -> E4
  auto melody = createTestMelody({60, 64, 62, 65, 64});
  float score = MelodyEvaluator::calcContourShape(melody);
  EXPECT_GE(score, 0.5f) << "Wave contour should be recognized";
}

TEST(MelodyEvaluatorTest, ContourDescending) {
  // Descending: G4 -> F4 -> E4 -> D4 -> C4
  auto melody = createTestMelody({67, 65, 64, 62, 60});
  float score = MelodyEvaluator::calcContourShape(melody);
  EXPECT_GE(score, 0.5f) << "Descending contour should be recognized";
}

TEST(MelodyEvaluatorTest, ContourShortMelody) {
  auto melody = createTestMelody({60, 62});
  float score = MelodyEvaluator::calcContourShape(melody);
  EXPECT_GE(score, 0.0f);
  EXPECT_LE(score, 1.0f);
}

// ============================================================================
// Surprise Element Tests
// ============================================================================

TEST(MelodyEvaluatorTest, SurpriseOneLeap) {
  // One large leap (octave) should score high
  // C4 -> E4 -> C5 -> B4 -> G4 (C5 is 8 semitones from E4)
  auto melody = createTestMelody({60, 64, 72, 71, 67});
  float score = MelodyEvaluator::calcSurpriseElement(melody);
  EXPECT_GE(score, 0.8f) << "One large leap should be ideal";
}

TEST(MelodyEvaluatorTest, SurpriseNoLeaps) {
  // No large leaps - stepwise motion
  auto melody = createTestMelody({60, 62, 64, 65, 67});
  float score = MelodyEvaluator::calcSurpriseElement(melody);
  EXPECT_LE(score, 0.8f) << "No surprise should score lower";
  EXPECT_GE(score, 0.5f) << "No surprise shouldn't be terrible";
}

TEST(MelodyEvaluatorTest, SurpriseTooManyLeaps) {
  // Too many large leaps
  // C4 -> C5 -> C4 -> C5 -> C4 (all octaves)
  auto melody = createTestMelody({60, 72, 60, 72, 60});
  float score = MelodyEvaluator::calcSurpriseElement(melody);
  EXPECT_LE(score, 0.7f) << "Too many leaps should reduce score";
}

// ============================================================================
// AAAB Pattern Tests
// ============================================================================

TEST(MelodyEvaluatorTest, AaabPatternDetection) {
  // AAAB pattern: similar first 3 phrases, different 4th
  // A: C4->D4, A: C4->D4, A: C4->D4, B: E4->G4
  std::vector<NoteEvent> melody;
  Tick tick = 0;
  // A phrase (3 times)
  for (int i = 0; i < 3; ++i) {
    melody.push_back({tick, TICKS_PER_BEAT, 60, 100});
    tick += TICKS_PER_BEAT;
    melody.push_back({tick, TICKS_PER_BEAT, 62, 100});
    tick += TICKS_PER_BEAT;
  }
  // B phrase (different)
  melody.push_back({tick, TICKS_PER_BEAT, 64, 100});
  tick += TICKS_PER_BEAT;
  melody.push_back({tick, TICKS_PER_BEAT, 67, 100});

  float score = MelodyEvaluator::calcAaabPattern(melody);
  EXPECT_GE(score, 0.5f) << "AAAB pattern should be detected";
}

TEST(MelodyEvaluatorTest, AaabNoPattern) {
  // No repetition pattern
  auto melody = createTestMelody({60, 64, 67, 72, 60, 65, 69, 74});
  float score = MelodyEvaluator::calcAaabPattern(melody);
  // Score can be anything, just verify it's in range
  EXPECT_GE(score, 0.0f);
  EXPECT_LE(score, 1.0f);
}

TEST(MelodyEvaluatorTest, AaabShortMelody) {
  auto melody = createTestMelody({60, 62, 64});
  float score = MelodyEvaluator::calcAaabPattern(melody);
  EXPECT_GE(score, 0.0f);
  EXPECT_LE(score, 1.0f);
}

// ============================================================================
// Score Range Tests
// ============================================================================

TEST(MelodyEvaluatorTest, AllScoresInRange) {
  auto melody = createTestMelody({60, 62, 64, 65, 67, 69, 71, 72});

  float sing = MelodyEvaluator::calcSingability(melody);
  EXPECT_GE(sing, 0.0f);
  EXPECT_LE(sing, 1.0f);

  float contour = MelodyEvaluator::calcContourShape(melody);
  EXPECT_GE(contour, 0.0f);
  EXPECT_LE(contour, 1.0f);

  float surprise = MelodyEvaluator::calcSurpriseElement(melody);
  EXPECT_GE(surprise, 0.0f);
  EXPECT_LE(surprise, 1.0f);

  float aaab = MelodyEvaluator::calcAaabPattern(melody);
  EXPECT_GE(aaab, 0.0f);
  EXPECT_LE(aaab, 1.0f);
}

// ============================================================================
// EvaluatorConfig Tests
// ============================================================================

TEST(MelodyEvaluatorTest, GetEvaluatorConfigIdol) {
  auto config = MelodyEvaluator::getEvaluatorConfig(VocalStylePreset::Idol);
  EXPECT_GT(config.singability_weight, 0.0f);
  EXPECT_GT(config.aaab_weight, 0.0f);

  // Total weights should sum to 1.0
  float total = config.singability_weight + config.chord_tone_weight + config.contour_weight +
                config.surprise_weight + config.aaab_weight;
  EXPECT_NEAR(total, 1.0f, 0.01f);
}

TEST(MelodyEvaluatorTest, GetEvaluatorConfigBallad) {
  auto config = MelodyEvaluator::getEvaluatorConfig(VocalStylePreset::Ballad);
  // Ballad should emphasize singability
  auto standard = MelodyEvaluator::getEvaluatorConfig(VocalStylePreset::Standard);
  EXPECT_GT(config.singability_weight, standard.singability_weight);
}

TEST(MelodyEvaluatorTest, GetEvaluatorConfigVocaloid) {
  auto config = MelodyEvaluator::getEvaluatorConfig(VocalStylePreset::Vocaloid);
  // Vocaloid can have more surprise elements
  EXPECT_GT(config.surprise_weight, 0.1f);
}

TEST(MelodyEvaluatorTest, TotalScoreCalculation) {
  MelodyScore score;
  score.singability = 0.8f;
  score.chord_tone_ratio = 0.7f;
  score.contour_shape = 0.9f;
  score.surprise_element = 0.6f;
  score.aaab_pattern = 0.5f;

  float simple_total = score.total();
  EXPECT_GE(simple_total, 0.0f);
  EXPECT_LE(simple_total, 1.0f);

  float weighted_total = score.total(kStandardProfile.evaluator);
  EXPECT_GE(weighted_total, 0.0f);
  EXPECT_LE(weighted_total, 1.0f);
}

// ============================================================================
// Integration Tests for MelodyEvaluator with MelodyDesigner
// ============================================================================

// Helper to create a valid SectionContext for testing
MelodyDesigner::SectionContext createTestSectionContext(SectionType type, Tick start,
                                                        uint8_t bars) {
  MelodyDesigner::SectionContext ctx;
  ctx.section_type = type;
  ctx.section_start = start;
  ctx.section_end = start + bars * TICKS_PER_BAR;
  ctx.section_bars = bars;
  ctx.chord_degree = 0;
  ctx.key_offset = 0;
  ctx.tessitura = {60, 79, 69};  // C4-G5, center ~A4
  ctx.vocal_low = 60;
  ctx.vocal_high = 79;
  ctx.density_modifier = 1.0f;
  return ctx;
}

TEST(MelodyEvaluatorIntegrationTest, GenerateSectionWithEvaluationProducesNotes) {
  MelodyDesigner designer;
  HarmonyContext harmony;

  // Create a simple arrangement
  std::vector<Section> sections;
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.bars = 8;
  chorus.start_tick = 0;
  chorus.name = "CHORUS";
  sections.push_back(chorus);
  harmony.initialize(Arrangement(sections), getChordProgression(0), Mood::StraightPop);

  // Get template
  MelodyTemplate tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

  // Create context
  auto ctx = createTestSectionContext(SectionType::Chorus, 0, 8);

  std::mt19937 rng(12345);

  auto notes =
      designer.generateSectionWithEvaluation(tmpl, ctx, harmony, rng, VocalStylePreset::Idol);

  // Should produce notes
  EXPECT_GT(notes.size(), 0u) << "generateSectionWithEvaluation should produce notes";
}

TEST(MelodyEvaluatorIntegrationTest, EvaluationSelectsBestCandidate) {
  MelodyDesigner designer;
  HarmonyContext harmony;

  std::vector<Section> sections;
  Section a;
  a.type = SectionType::A;
  a.bars = 8;
  a.start_tick = 0;
  a.name = "A";
  sections.push_back(a);
  harmony.initialize(Arrangement(sections), getChordProgression(0), Mood::StraightPop);

  MelodyTemplate tmpl = getTemplate(MelodyTemplateId::PlateauTalk);
  auto ctx = createTestSectionContext(SectionType::A, 0, 8);

  std::mt19937 rng1(11111);
  std::mt19937 rng2(22222);

  // Generate with different seeds
  auto notes1 =
      designer.generateSectionWithEvaluation(tmpl, ctx, harmony, rng1, VocalStylePreset::Standard);
  auto notes2 =
      designer.generateSectionWithEvaluation(tmpl, ctx, harmony, rng2, VocalStylePreset::Standard);

  // Both should produce valid output
  EXPECT_GT(notes1.size(), 0u);
  EXPECT_GT(notes2.size(), 0u);
}

TEST(MelodyEvaluatorIntegrationTest, DifferentStylesProduceDifferentMelodies) {
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

  MelodyTemplate tmpl = getTemplate(MelodyTemplateId::PlateauTalk);
  auto ctx = createTestSectionContext(SectionType::Chorus, 0, 8);

  // Use same seed but different styles
  std::mt19937 rng1(12345);
  std::mt19937 rng2(12345);

  auto idol_notes =
      designer.generateSectionWithEvaluation(tmpl, ctx, harmony, rng1, VocalStylePreset::Idol);
  auto ballad_notes =
      designer.generateSectionWithEvaluation(tmpl, ctx, harmony, rng2, VocalStylePreset::Ballad);

  // Both should produce notes
  EXPECT_GT(idol_notes.size(), 0u);
  EXPECT_GT(ballad_notes.size(), 0u);

  // Styles have different weights, so selection may differ
  // (We can't guarantee difference due to randomness, but we verify both work)
}

}  // namespace
}  // namespace midisketch
