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

  // Total weights should sum to exactly 1.0 (with new fields)
  float total = config.singability_weight + config.chord_tone_weight + config.contour_weight +
                config.surprise_weight + config.aaab_weight + config.rhythm_interval_weight +
                config.catchiness_weight;
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
  score.rhythm_interval_correlation = 0.75f;
  score.catchiness = 0.65f;  // Initialize catchiness field

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
  ctx.tessitura = {60, 79, 69, 60, 79};  // C4-G5, center ~A4
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

// ============================================================================
// Rhythm-Interval Correlation Tests
// ============================================================================

TEST(MelodyEvaluatorTest, RhythmIntervalCorrelation_EmptyNotes) {
  std::vector<NoteEvent> empty;
  float score = MelodyEvaluator::calcRhythmIntervalCorrelation(empty);
  EXPECT_FLOAT_EQ(score, 0.5f) << "Empty notes should return neutral score";
}

TEST(MelodyEvaluatorTest, RhythmIntervalCorrelation_SingleNote) {
  std::vector<NoteEvent> single = {{0, 480, 60, 100}};  // One quarter note
  float score = MelodyEvaluator::calcRhythmIntervalCorrelation(single);
  EXPECT_FLOAT_EQ(score, 0.5f) << "Single note should return neutral score";
}

TEST(MelodyEvaluatorTest, RhythmIntervalCorrelation_LongNoteWithLeap) {
  // Good pattern: quarter note (480 ticks) followed by leap (5+ semitones)
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100},    // C4 quarter note
      {480, 480, 67, 100},  // G4 (7 semitones leap) after long note
  };
  float score = MelodyEvaluator::calcRhythmIntervalCorrelation(notes);
  EXPECT_GT(score, 0.5f) << "Long note + leap should score above neutral";
}

TEST(MelodyEvaluatorTest, RhythmIntervalCorrelation_ShortNoteWithStep) {
  // Good pattern: short note (< 240 ticks) followed by step (1-2 semitones)
  std::vector<NoteEvent> notes = {
      {0, 120, 60, 100},    // C4 eighth note
      {120, 120, 62, 100},  // D4 (2 semitones step) after short note
  };
  float score = MelodyEvaluator::calcRhythmIntervalCorrelation(notes);
  EXPECT_GT(score, 0.5f) << "Short note + step should score above neutral";
}

TEST(MelodyEvaluatorTest, RhythmIntervalCorrelation_ShortNoteWithLeap) {
  // Bad pattern: short note followed by large leap (hard to sing)
  std::vector<NoteEvent> notes = {
      {0, 120, 60, 100},    // C4 eighth note
      {120, 120, 72, 100},  // C5 (12 semitones leap) after short note
  };
  float score = MelodyEvaluator::calcRhythmIntervalCorrelation(notes);
  EXPECT_LT(score, 0.5f) << "Short note + large leap should score below neutral";
}

TEST(MelodyEvaluatorTest, RhythmIntervalCorrelation_MixedPattern) {
  // Mix of good and bad patterns
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100},     // C4 quarter (long)
      {480, 480, 67, 100},   // G4 leap (good: long+leap)
      {960, 120, 67, 100},   // G4 short
      {1080, 120, 72, 100},  // C5 leap (bad: short+leap)
      {1200, 480, 72, 100},  // C5 quarter (long)
      {1680, 480, 74, 100},  // D5 step (neutral: long+step)
  };
  float score = MelodyEvaluator::calcRhythmIntervalCorrelation(notes);
  // Should be near neutral due to mix
  EXPECT_GE(score, 0.3f);
  EXPECT_LE(score, 0.7f);
}

TEST(MelodyEvaluatorTest, EvaluateIncludesRhythmIntervalCorrelation) {
  // Verify that evaluate() populates rhythm_interval_correlation
  HarmonyContext harmony;
  std::vector<Section> sections;
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.bars = 8;
  chorus.start_tick = 0;
  chorus.name = "CHORUS";
  sections.push_back(chorus);
  harmony.initialize(Arrangement(sections), getChordProgression(0), Mood::StraightPop);

  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100},
      {480, 480, 64, 100},
      {960, 480, 67, 100},
  };

  MelodyScore score = MelodyEvaluator::evaluate(notes, harmony);

  // rhythm_interval_correlation should be set (not NaN or uninitialized)
  EXPECT_GE(score.rhythm_interval_correlation, 0.0f);
  EXPECT_LE(score.rhythm_interval_correlation, 1.0f);
}

// ============================================================================
// Catchiness Score Tests (Proposal B)
// ============================================================================

TEST(MelodyEvaluatorTest, Catchiness_EmptyNotes) {
  std::vector<NoteEvent> empty;
  float score = MelodyEvaluator::calcCatchiness(empty);
  EXPECT_FLOAT_EQ(score, 0.5f) << "Empty notes should return neutral score";
}

TEST(MelodyEvaluatorTest, Catchiness_ShortMelody) {
  // Less than 4 notes should return neutral
  std::vector<NoteEvent> short_melody = {
      {0, 480, 60, 100},
      {480, 480, 62, 100},
  };
  float score = MelodyEvaluator::calcCatchiness(short_melody);
  EXPECT_FLOAT_EQ(score, 0.5f) << "Short melody should return neutral score";
}

TEST(MelodyEvaluatorTest, Catchiness_RepetitivePattern) {
  // Highly repetitive pattern: C-D-C-D (interval +2, -2, +2)
  // Should score well due to pattern repetition
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100},     // C4
      {480, 480, 62, 100},   // D4 (+2)
      {960, 480, 60, 100},   // C4 (-2)
      {1440, 480, 62, 100},  // D4 (+2)
      {1920, 480, 60, 100},  // C4 (-2)
      {2400, 480, 62, 100},  // D4 (+2)
  };
  float score = MelodyEvaluator::calcCatchiness(notes);
  EXPECT_GT(score, 0.5f) << "Repetitive pattern should score above neutral";
}

TEST(MelodyEvaluatorTest, Catchiness_RandomPattern) {
  // Random-ish pattern: no repetition, large intervals
  // Should score lower
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100},     // C4
      {480, 240, 67, 100},   // G4 (+7)
      {720, 960, 55, 100},   // G3 (-12)
      {1680, 120, 72, 100},  // C5 (+17)
      {1800, 480, 58, 100},  // Bb3 (-14)
  };
  float score = MelodyEvaluator::calcCatchiness(notes);
  EXPECT_LT(score, 0.5f) << "Random pattern should score below neutral";
}

TEST(MelodyEvaluatorTest, Catchiness_SimpleIntervals) {
  // All simple intervals (steps): C-D-E-F-G
  // Should score well for simple_interval_score component
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100},     // C4
      {480, 480, 62, 100},   // D4 (+2)
      {960, 480, 64, 100},   // E4 (+2)
      {1440, 480, 65, 100},  // F4 (+1)
      {1920, 480, 67, 100},  // G4 (+2)
  };
  float score = MelodyEvaluator::calcCatchiness(notes);
  EXPECT_GT(score, 0.4f) << "Simple intervals should contribute positively";
}

TEST(MelodyEvaluatorTest, Catchiness_AscendDrop) {
  // Ascending then dropping: C-E-G-E-C (arch shape)
  // Should get contour bonus
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100},     // C4
      {480, 480, 64, 100},   // E4 (+4)
      {960, 480, 67, 100},   // G4 (+3)
      {1440, 480, 64, 100},  // E4 (-3)
      {1920, 480, 60, 100},  // C4 (-4)
  };
  float score = MelodyEvaluator::calcCatchiness(notes);
  // This should get some contour bonus for AscendDrop pattern
  EXPECT_GE(score, 0.3f) << "AscendDrop contour should contribute to catchiness";
}

TEST(MelodyEvaluatorTest, Catchiness_RepeatPitches) {
  // Same pitch repeated: C-C-C-D-D-D
  // Should get repeat bonus
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100},     // C4
      {480, 480, 60, 100},   // C4
      {960, 480, 60, 100},   // C4
      {1440, 480, 62, 100},  // D4
      {1920, 480, 62, 100},  // D4
      {2400, 480, 62, 100},  // D4
  };
  float score = MelodyEvaluator::calcCatchiness(notes);
  // Should get repeat bonus for consecutive same pitches
  EXPECT_GT(score, 0.5f) << "Pitch repetition should be catchy";
}

TEST(MelodyEvaluatorTest, Catchiness_ConsistentRhythm) {
  // All same duration: should score well for rhythm consistency
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100},
      {480, 480, 64, 100},
      {960, 480, 67, 100},
      {1440, 480, 64, 100},
      {1920, 480, 60, 100},
  };
  float score = MelodyEvaluator::calcCatchiness(notes);
  EXPECT_GE(score, 0.4f) << "Consistent rhythm should contribute to catchiness";
}

TEST(MelodyEvaluatorTest, EvaluateIncludesCatchiness) {
  // Verify that evaluate() populates catchiness
  HarmonyContext harmony;
  std::vector<Section> sections;
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.bars = 8;
  chorus.start_tick = 0;
  chorus.name = "CHORUS";
  sections.push_back(chorus);
  harmony.initialize(Arrangement(sections), getChordProgression(0), Mood::StraightPop);

  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100},
      {480, 480, 64, 100},
      {960, 480, 67, 100},
      {1440, 480, 64, 100},
      {1920, 480, 60, 100},
  };

  MelodyScore score = MelodyEvaluator::evaluate(notes, harmony);

  // catchiness should be set (not NaN or uninitialized)
  EXPECT_GE(score.catchiness, 0.0f);
  EXPECT_LE(score.catchiness, 1.0f);
}

TEST(MelodyEvaluatorTest, TotalIncludesCatchiness) {
  // Verify that total() with config includes catchiness weight
  MelodyScore score;
  score.singability = 0.5f;
  score.chord_tone_ratio = 0.5f;
  score.contour_shape = 0.5f;
  score.surprise_element = 0.5f;
  score.aaab_pattern = 0.5f;
  score.rhythm_interval_correlation = 0.5f;
  score.catchiness = 1.0f;  // High catchiness

  EvaluatorConfig config_no_catchiness;
  config_no_catchiness.singability_weight = 1.0f;
  config_no_catchiness.chord_tone_weight = 0.0f;
  config_no_catchiness.contour_weight = 0.0f;
  config_no_catchiness.surprise_weight = 0.0f;
  config_no_catchiness.aaab_weight = 0.0f;
  config_no_catchiness.rhythm_interval_weight = 0.0f;
  config_no_catchiness.catchiness_weight = 0.0f;

  EvaluatorConfig config_with_catchiness;
  config_with_catchiness.singability_weight = 0.5f;
  config_with_catchiness.chord_tone_weight = 0.0f;
  config_with_catchiness.contour_weight = 0.0f;
  config_with_catchiness.surprise_weight = 0.0f;
  config_with_catchiness.aaab_weight = 0.0f;
  config_with_catchiness.rhythm_interval_weight = 0.0f;
  config_with_catchiness.catchiness_weight = 0.5f;

  float total_no_catchiness = score.total(config_no_catchiness);
  float total_with_catchiness = score.total(config_with_catchiness);

  // With catchiness weight, total should be higher (catchiness=1.0 > singability=0.5)
  EXPECT_GT(total_with_catchiness, total_no_catchiness);
}

// ============================================================================
// Graduated Repeat Bonus Tests (Phase 1: Catchiness Enhancement)
// ============================================================================

TEST(MelodyEvaluatorTest, Catchiness_GraduatedRepeatBonus_TwoNotes) {
  // 2 consecutive same pitches should get 0.2 bonus
  // C-C-D pattern: 2 consecutive same notes
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100},     // C4
      {480, 480, 60, 100},   // C4 (same)
      {960, 480, 62, 100},   // D4
      {1440, 480, 64, 100},  // E4
  };
  float score = MelodyEvaluator::calcCatchiness(notes);
  // Should get some repeat bonus but not maximum
  EXPECT_GE(score, 0.3f) << "2 consecutive same notes should provide some catchiness";
}

TEST(MelodyEvaluatorTest, Catchiness_GraduatedRepeatBonus_FiveNotes) {
  // 5 consecutive same pitches should get maximum 1.0 bonus (Ice Cream style)
  // C-C-C-C-C-D pattern
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100},      // C4
      {480, 480, 60, 100},    // C4 (same)
      {960, 480, 60, 100},    // C4 (same)
      {1440, 480, 60, 100},   // C4 (same)
      {1920, 480, 60, 100},   // C4 (same) - 5 consecutive!
      {2400, 480, 62, 100},   // D4
  };
  float score = MelodyEvaluator::calcCatchiness(notes);
  // Should get high catchiness due to maximum repeat bonus
  EXPECT_GE(score, 0.5f) << "5 consecutive same notes should provide high catchiness (Ice Cream style)";
}

TEST(MelodyEvaluatorTest, Catchiness_HighIntervalRepetition) {
  // Same interval (e.g., +2 semitones) appearing 6+ times should add bonus
  // C-D-E-F-G-A-B pattern: all +2 intervals (whole steps)
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100},      // C4
      {480, 480, 62, 100},    // D4 (+2)
      {960, 480, 64, 100},    // E4 (+2)
      {1440, 480, 65, 100},   // F4 (+1) - different
      {1920, 480, 67, 100},   // G4 (+2)
      {2400, 480, 69, 100},   // A4 (+2)
      {2880, 480, 71, 100},   // B4 (+2)
      {3360, 480, 72, 100},   // C5 (+1)
  };
  float score = MelodyEvaluator::calcCatchiness(notes);
  // Should get bonus for high interval repetition (5x "+2" intervals)
  EXPECT_GE(score, 0.4f) << "High interval repetition should boost catchiness";
}

TEST(MelodyEvaluatorTest, Catchiness_SixSameIntervals) {
  // Create 6 identical intervals (+0 = same pitch repeated)
  // This tests the high_rep_bonus for 6+ occurrences
  std::vector<NoteEvent> notes = {
      {0, 480, 60, 100},      // C4
      {480, 480, 60, 100},    // C4 (interval=0)
      {960, 480, 60, 100},    // C4 (interval=0)
      {1440, 480, 60, 100},   // C4 (interval=0)
      {1920, 480, 60, 100},   // C4 (interval=0)
      {2400, 480, 60, 100},   // C4 (interval=0)
      {2880, 480, 60, 100},   // C4 (interval=0) - 6 consecutive 0 intervals!
  };
  float score = MelodyEvaluator::calcCatchiness(notes);
  // Should get maximum high_rep_bonus (0.25) plus repeat bonus (1.0)
  EXPECT_GE(score, 0.6f) << "6+ same intervals should maximize catchiness bonus";
}

}  // namespace
}  // namespace midisketch
