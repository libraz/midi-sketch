/**
 * @file constraint_pipeline_test.cpp
 * @brief Tests for melody constraint pipeline functions.
 *
 * Tests calculateGateRatio, applyGateRatio, clampToChordBoundary,
 * clampToPhraseBoundary, findChordToneInDirection, applyAllDurationConstraints.
 */

#include <gtest/gtest.h>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/harmony_context.h"
#include "core/timing_constants.h"
#include "track/melody/constraint_pipeline.h"

using namespace midisketch;
using namespace midisketch::melody;

// ============================================================================
// calculateGateRatio Tests
// ============================================================================

TEST(ConstraintPipelineTest, GateRatio_PhraseEnd) {
  GateContext ctx;
  ctx.is_phrase_end = true;
  ctx.interval_from_prev = 0;
  // Phrase end: no gate shortening (PhrasePlanner handles breath gaps)
  EXPECT_FLOAT_EQ(calculateGateRatio(ctx), 1.0f);
}

TEST(ConstraintPipelineTest, GateRatio_PhraseStart) {
  GateContext ctx;
  ctx.is_phrase_start = true;
  ctx.interval_from_prev = 5;
  EXPECT_FLOAT_EQ(calculateGateRatio(ctx), 1.0f);
}

TEST(ConstraintPipelineTest, GateRatio_LongNote) {
  GateContext ctx;
  ctx.note_duration = TICK_QUARTER;  // 480 ticks
  ctx.interval_from_prev = 7;
  EXPECT_FLOAT_EQ(calculateGateRatio(ctx), 1.0f);
}

TEST(ConstraintPipelineTest, GateRatio_SamePitch) {
  GateContext ctx;
  ctx.note_duration = TICK_EIGHTH;  // 240 ticks (not >= quarter)
  ctx.interval_from_prev = 0;
  EXPECT_FLOAT_EQ(calculateGateRatio(ctx), 1.0f);
}

TEST(ConstraintPipelineTest, GateRatio_StepMotion) {
  GateContext ctx;
  ctx.note_duration = TICK_EIGHTH;
  ctx.interval_from_prev = 2;  // Whole step
  // Step motion is now full legato
  EXPECT_FLOAT_EQ(calculateGateRatio(ctx), 1.0f);
}

TEST(ConstraintPipelineTest, GateRatio_Skip) {
  GateContext ctx;
  ctx.note_duration = TICK_EIGHTH;
  ctx.interval_from_prev = 4;  // Major 3rd
  // Skip is now near-legato
  EXPECT_FLOAT_EQ(calculateGateRatio(ctx), 0.98f);
}

TEST(ConstraintPipelineTest, GateRatio_Leap) {
  GateContext ctx;
  ctx.note_duration = TICK_EIGHTH;
  ctx.interval_from_prev = 7;  // Perfect 5th
  // Leap now has slight articulation (was 0.92)
  EXPECT_FLOAT_EQ(calculateGateRatio(ctx), 0.95f);
}

TEST(ConstraintPipelineTest, GateRatio_NegativeInterval) {
  // Negative intervals are handled via abs()
  GateContext ctx;
  ctx.note_duration = TICK_EIGHTH;
  ctx.interval_from_prev = -3;  // Minor 3rd descending
  // Skip (3-5st) is now near-legato
  EXPECT_FLOAT_EQ(calculateGateRatio(ctx), 0.98f);
}

TEST(ConstraintPipelineTest, GateRatio_PhraseEndTakesPriority) {
  // Phrase end should override other considerations
  GateContext ctx;
  ctx.is_phrase_end = true;
  ctx.is_phrase_start = true;  // Contradictory, but phrase_end is checked first
  ctx.interval_from_prev = 0;
  // Phrase end takes priority: no gate shortening
  EXPECT_FLOAT_EQ(calculateGateRatio(ctx), 1.0f);
}

// ============================================================================
// applyGateRatio Tests
// ============================================================================

TEST(ConstraintPipelineTest, ApplyGateRatio_ShortensNote) {
  GateContext ctx;
  ctx.note_duration = TICK_EIGHTH;
  ctx.interval_from_prev = 7;  // Leap => 0.95f
  Tick result = applyGateRatio(TICK_QUARTER, ctx);  // 480 * 0.95 = 456
  EXPECT_EQ(result, 456u);
}

TEST(ConstraintPipelineTest, ApplyGateRatio_RespectsMinDuration) {
  GateContext ctx;
  ctx.is_phrase_end = true;  // 1.0f (no gate)
  // Very short note: 60 * 1.0 = 60, but min_duration = 120
  Tick result = applyGateRatio(60, ctx, TICK_SIXTEENTH);
  EXPECT_EQ(result, TICK_SIXTEENTH);
}

TEST(ConstraintPipelineTest, ApplyGateRatio_DefaultMinIsSixteenth) {
  GateContext ctx;
  ctx.is_phrase_end = true;
  // 100 * 1.0 = 100, which is < TICK_SIXTEENTH (120)
  Tick result = applyGateRatio(100, ctx);
  EXPECT_EQ(result, TICK_SIXTEENTH);
}

// ============================================================================
// clampToPhraseBoundary Tests
// ============================================================================

TEST(ConstraintPipelineTest, ClampToPhrase_NoteWithinBoundary) {
  // Note: start=0, duration=480, phrase_end=960 => no clamp needed
  Tick result = clampToPhraseBoundary(0, 480, 960);
  EXPECT_EQ(result, 480u);
}

TEST(ConstraintPipelineTest, ClampToPhrase_NoteExceedsBoundary) {
  // Note: start=480, duration=960, phrase_end=960
  // note_end = 1440 > 960, so clamp: 960 - 480 = 480
  Tick result = clampToPhraseBoundary(480, 960, 960);
  EXPECT_EQ(result, 480u);
}

TEST(ConstraintPipelineTest, ClampToPhrase_NoteExactlyAtBoundary) {
  // Note: start=0, duration=960, phrase_end=960
  // note_end = 960 = phrase_end, so no clamp (<=)
  Tick result = clampToPhraseBoundary(0, 960, 960);
  EXPECT_EQ(result, 960u);
}

TEST(ConstraintPipelineTest, ClampToPhrase_TooCloseForMinDuration) {
  // Note: start=950, duration=480, phrase_end=960
  // new_duration = 960 - 950 = 10, which < TICK_SIXTEENTH (120)
  // Keep original
  Tick result = clampToPhraseBoundary(950, 480, 960);
  EXPECT_EQ(result, 480u);
}

TEST(ConstraintPipelineTest, ClampToPhrase_PhraseEndBeforeStart) {
  // Edge case: phrase_end <= note_start
  Tick result = clampToPhraseBoundary(960, 480, 480);
  EXPECT_EQ(result, 480u);  // Unchanged
}

// ============================================================================
// clampToChordBoundary Tests (with real HarmonyContext)
// ============================================================================

class ChordBoundaryClampTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Section chorus;
    chorus.type = SectionType::Chorus;
    chorus.start_tick = 0;
    chorus.bars = 8;
    chorus.name = "Chorus";
    arrangement_ = Arrangement({chorus});
    progression_ = getChordProgression(0);  // I-V-vi-IV
    harmony_.initialize(arrangement_, progression_, Mood::StraightPop);
  }

  Arrangement arrangement_;
  ChordProgression progression_;
  HarmonyContext harmony_;
};

TEST_F(ChordBoundaryClampTest, NoBoundary_ReturnsOriginal) {
  // Short note within bar 1 (I chord) - no boundary crossing
  Tick result = clampToChordBoundary(0, 480, harmony_, 65);  // F4
  EXPECT_EQ(result, 480u);
}

TEST_F(ChordBoundaryClampTest, PitchZero_ReturnsOriginal) {
  Tick result = clampToChordBoundary(0, 1920, harmony_, 0);
  EXPECT_EQ(result, 1920u);
}

TEST_F(ChordBoundaryClampTest, ChordToneInNextChord_NoClip) {
  // G4 (67) is chord tone in both I and V
  // Note: start=960, duration=1920, crosses boundary at 1920
  Tick result = clampToChordBoundary(960, 1920, harmony_, 67);
  EXPECT_EQ(result, 1920u);  // G is chord tone in V, no clip
}

TEST_F(ChordBoundaryClampTest, NonChordTone_Clips) {
  // F4 (65) is NonChordTone in V(G-B-D)
  // Note: start=960, duration=1920, crosses boundary at 1920
  Tick result = clampToChordBoundary(960, 1920, harmony_, 65);
  EXPECT_LT(result, 1920u);  // Should be clipped
}

// ============================================================================
// findChordToneInDirection Tests
// ============================================================================

TEST(ConstraintPipelineTest, FindChordTone_Ascending) {
  // From C4 (60), chord I (degree 0 = C-E-G), ascending
  int result = findChordToneInDirection(60, 0, 1, 48, 84);
  EXPECT_GT(result, 60);
  // Should be E4 (64) or G4 (67) - nearest ascending chord tone
  int pc = result % 12;
  EXPECT_TRUE(pc == 0 || pc == 4 || pc == 7);
}

TEST(ConstraintPipelineTest, FindChordTone_Descending) {
  // From G4 (67), chord I (degree 0 = C-E-G), descending
  int result = findChordToneInDirection(67, 0, -1, 48, 84);
  EXPECT_LT(result, 67);
  int pc = result % 12;
  EXPECT_TRUE(pc == 0 || pc == 4 || pc == 7);
}

TEST(ConstraintPipelineTest, FindChordTone_NearestWhenNoDirection) {
  // Direction 0: nearest chord tone (could be up or down)
  int result = findChordToneInDirection(61, 0, 0, 48, 84);
  int pc = result % 12;
  EXPECT_TRUE(pc == 0 || pc == 4 || pc == 7);
  // C#4 (61) -> nearest should be C4 (60) (dist=1) rather than E4 (64) (dist=3)
  EXPECT_EQ(result, 60);
}

TEST(ConstraintPipelineTest, FindChordTone_RespectsMaxInterval) {
  // From C4 (60), ascending, max_interval = 3 => can only reach up to 63
  // Chord tones: C(0), E(4), G(7). E4 = 64 is 4 semitones away (> 3).
  int result = findChordToneInDirection(60, 0, 1, 48, 84, 3);
  // No chord tone within 3 semitones ascending from 60 (next is E4=64, 4 away)
  EXPECT_EQ(result, 60);  // Falls back to current pitch
}

TEST(ConstraintPipelineTest, FindChordTone_RespectsVocalRange) {
  // From C6 (84), ascending, but vocal_high = 84
  int result = findChordToneInDirection(84, 0, 1, 48, 84);
  EXPECT_EQ(result, 84);  // Can't go higher
}

TEST(ConstraintPipelineTest, FindChordTone_DifferentChord) {
  // Chord V (degree 4) = G-B-D: pitch classes 7, 11, 2
  int result = findChordToneInDirection(65, 4, 1, 48, 84);
  EXPECT_GT(result, 65);
  int pc = result % 12;
  EXPECT_TRUE(pc == 7 || pc == 11 || pc == 2);
}

// ============================================================================
// applyAllDurationConstraints Tests
// ============================================================================

TEST_F(ChordBoundaryClampTest, AllConstraints_CombinesCorrectly) {
  GateContext ctx;
  ctx.note_duration = TICK_EIGHTH;
  ctx.interval_from_prev = 2;  // Step motion => 1.0f gate ratio (full legato)

  // Note within bar, not crossing chord boundary, within phrase
  Tick result = applyAllDurationConstraints(0, TICK_QUARTER, harmony_, TICK_WHOLE * 4, ctx, 60);

  // 480 * 1.0 = 480, no chord boundary clip, within phrase
  EXPECT_EQ(result, 480u);
}

TEST_F(ChordBoundaryClampTest, AllConstraints_PhraseClampTakesPriority) {
  GateContext ctx;
  ctx.note_duration = TICK_QUARTER;
  ctx.interval_from_prev = 0;  // Same pitch => 1.0 gate ratio

  // Note near phrase end: start=900, duration=480, phrase_end=960
  // After gate: 480 (no change). Phrase clamp: 960-900=60 < TICK_SIXTEENTH, keep original.
  // Actually 60 < 120, so keep 480.
  Tick result = applyAllDurationConstraints(900, TICK_QUARTER, harmony_, 960, ctx, 60);
  // clampToPhraseBoundary: 900+480=1380 > 960, new_dur=60 < 120 => keep 480
  EXPECT_EQ(result, 480u);
}
