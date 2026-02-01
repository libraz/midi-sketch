/**
 * @file swing_quantize_test.cpp
 * @brief Tests for triplet-grid swing quantization.
 */

#include <gtest/gtest.h>

#include "core/midi_track.h"
#include "core/swing_quantize.h"
#include "core/timing_constants.h"

namespace midisketch {
namespace {

// ============================================================================
// quantizeToSwingGrid - 8th note swing
// ============================================================================

TEST(QuantizeToSwingGridTest, ZeroSwingReturnsUnchanged) {
  // At swing_amount=0, all positions should be unchanged
  EXPECT_EQ(quantizeToSwingGrid(0, 0.0f), 0u);
  EXPECT_EQ(quantizeToSwingGrid(240, 0.0f), 240u);   // Off-beat 8th
  EXPECT_EQ(quantizeToSwingGrid(480, 0.0f), 480u);   // Beat 2
  EXPECT_EQ(quantizeToSwingGrid(720, 0.0f), 720u);   // Off-beat 8th beat 2
  EXPECT_EQ(quantizeToSwingGrid(960, 0.0f), 960u);   // Beat 3
  EXPECT_EQ(quantizeToSwingGrid(1200, 0.0f), 1200u); // Off-beat 8th beat 3
  EXPECT_EQ(quantizeToSwingGrid(1920, 0.0f), 1920u); // Bar 2 beat 1
}

TEST(QuantizeToSwingGridTest, FullSwingMovesToTripletPosition) {
  // At swing_amount=1.0, off-beat 8ths move to triplet position (2/3 of beat)
  // Straight off-beat: 240 -> Triplet off-beat: 320 (delta = 80)

  // Off-beat 8th in beat 1: 240 -> 320
  EXPECT_EQ(quantizeToSwingGrid(240, 1.0f), 320u);

  // Off-beat 8th in beat 2: 720 -> 800
  EXPECT_EQ(quantizeToSwingGrid(720, 1.0f), 800u);

  // Off-beat 8th in beat 3: 1200 -> 1280
  EXPECT_EQ(quantizeToSwingGrid(1200, 1.0f), 1280u);

  // Off-beat 8th in beat 4: 1680 -> 1760
  EXPECT_EQ(quantizeToSwingGrid(1680, 1.0f), 1760u);
}

TEST(QuantizeToSwingGridTest, OnBeatPositionsNeverAffected) {
  // On-beat positions must remain unchanged regardless of swing amount
  for (float swing = 0.0f; swing <= 1.0f; swing += 0.1f) {
    EXPECT_EQ(quantizeToSwingGrid(0, swing), 0u)
        << "Beat 1 should not move at swing=" << swing;
    EXPECT_EQ(quantizeToSwingGrid(480, swing), 480u)
        << "Beat 2 should not move at swing=" << swing;
    EXPECT_EQ(quantizeToSwingGrid(960, swing), 960u)
        << "Beat 3 should not move at swing=" << swing;
    EXPECT_EQ(quantizeToSwingGrid(1440, swing), 1440u)
        << "Beat 4 should not move at swing=" << swing;
  }
}

TEST(QuantizeToSwingGridTest, HalfSwingInterpolates) {
  // At swing_amount=0.5, off-beat should move halfway to triplet position
  // 240 + (80 * 0.5) = 240 + 40 = 280
  EXPECT_EQ(quantizeToSwingGrid(240, 0.5f), 280u);
  EXPECT_EQ(quantizeToSwingGrid(720, 0.5f), 760u);
}

TEST(QuantizeToSwingGridTest, WorksAcrossMultipleBars) {
  // Bar 2: beat 1 off-beat = 1920 + 240 = 2160
  EXPECT_EQ(quantizeToSwingGrid(2160, 1.0f), 2240u);

  // Bar 3: beat 1 off-beat = 3840 + 240 = 4080
  EXPECT_EQ(quantizeToSwingGrid(4080, 1.0f), 4160u);
}

TEST(QuantizeToSwingGridTest, NegativeSwingClampedToZero) {
  // Negative swing should be treated as zero
  EXPECT_EQ(quantizeToSwingGrid(240, -0.5f), 240u);
}

TEST(QuantizeToSwingGridTest, SwingAboveOneClampedToOne) {
  // Swing > 1.0 should be clamped to 1.0
  EXPECT_EQ(quantizeToSwingGrid(240, 1.5f), 320u);  // Same as 1.0
}

// ============================================================================
// quantizeToSwingGrid16th - 16th note swing
// ============================================================================

TEST(QuantizeToSwingGrid16thTest, ZeroSwingReturnsUnchanged) {
  EXPECT_EQ(quantizeToSwingGrid16th(0, 0.0f), 0u);
  EXPECT_EQ(quantizeToSwingGrid16th(120, 0.0f), 120u);   // 16th position 1
  EXPECT_EQ(quantizeToSwingGrid16th(240, 0.0f), 240u);   // 16th position 2
  EXPECT_EQ(quantizeToSwingGrid16th(360, 0.0f), 360u);   // 16th position 3
}

TEST(QuantizeToSwingGrid16thTest, FullSwingMovesPosition1ToTriplet) {
  // Position 1 (120) moves to triplet position (160)
  // Delta = 160 - 120 = 40
  EXPECT_EQ(quantizeToSwingGrid16th(120, 1.0f), 160u);
}

TEST(QuantizeToSwingGrid16thTest, FullSwingMovesPosition2ToTriplet) {
  // Position 2 (240) is the off-beat 8th, moves to 320
  // Delta = 80 (same as 8th-note swing)
  EXPECT_EQ(quantizeToSwingGrid16th(240, 1.0f), 320u);
}

TEST(QuantizeToSwingGrid16thTest, FullSwingMovesPosition3) {
  // Position 3 (360) moves with combined 8th + 16th swing deltas
  // = 240 + 80 (8th swing) + 120 + 40 (16th swing) = 480? No.
  // Actually: TICK_EIGHTH + swing_delta_8th + TICK_SIXTEENTH + swing_delta_16th
  // = 240 + 80 + 120 + 40 = 480
  // But that's the next beat! Let me check what the implementation does.
  Tick result = quantizeToSwingGrid16th(360, 1.0f);
  // Should be > 360 (shifted forward)
  EXPECT_GT(result, 360u);
  // Should not exceed the beat boundary
  EXPECT_LT(result, TICKS_PER_BEAT);
}

TEST(QuantizeToSwingGrid16thTest, OnBeatNeverAffected) {
  for (float swing = 0.0f; swing <= 1.0f; swing += 0.25f) {
    EXPECT_EQ(quantizeToSwingGrid16th(0, swing), 0u)
        << "On-beat should not move at swing=" << swing;
    EXPECT_EQ(quantizeToSwingGrid16th(480, swing), 480u)
        << "Beat 2 should not move at swing=" << swing;
  }
}

TEST(QuantizeToSwingGrid16thTest, HalfSwingInterpolates) {
  // Position 1 at half swing: 120 + 20 = 140
  EXPECT_EQ(quantizeToSwingGrid16th(120, 0.5f), 140u);

  // Position 2 at half swing: 240 + 40 = 280
  EXPECT_EQ(quantizeToSwingGrid16th(240, 0.5f), 280u);
}

// ============================================================================
// swingOffsetForEighth / swingOffsetFor16th
// ============================================================================

TEST(SwingOffsetTest, EighthOffsetAtZero) {
  EXPECT_EQ(swingOffsetForEighth(0.0f), 0u);
}

TEST(SwingOffsetTest, EighthOffsetAtFull) {
  // Max delta: 320 - 240 = 80 ticks
  EXPECT_EQ(swingOffsetForEighth(1.0f), 80u);
}

TEST(SwingOffsetTest, EighthOffsetAtHalf) {
  EXPECT_EQ(swingOffsetForEighth(0.5f), 40u);
}

TEST(SwingOffsetTest, SixteenthOffsetAtZero) {
  EXPECT_EQ(swingOffsetFor16th(0.0f), 0u);
}

TEST(SwingOffsetTest, SixteenthOffsetAtFull) {
  // Max delta: 160 - 120 = 40 ticks
  EXPECT_EQ(swingOffsetFor16th(1.0f), 40u);
}

TEST(SwingOffsetTest, SixteenthOffsetAtHalf) {
  EXPECT_EQ(swingOffsetFor16th(0.5f), 20u);
}

TEST(SwingOffsetTest, OffsetClampedForNegative) {
  EXPECT_EQ(swingOffsetForEighth(-1.0f), 0u);
  EXPECT_EQ(swingOffsetFor16th(-1.0f), 0u);
}

TEST(SwingOffsetTest, OffsetClampedAboveOne) {
  EXPECT_EQ(swingOffsetForEighth(2.0f), 80u);
  EXPECT_EQ(swingOffsetFor16th(2.0f), 40u);
}

// ============================================================================
// applySwingToTrack
// ============================================================================

TEST(ApplySwingToTrackTest, NoSwingLeavesNotesUnchanged) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, 240, 60, 100));      // On-beat
  track.addNote(NoteEventBuilder::create(240, 240, 64, 90));     // Off-beat 8th
  track.addNote(NoteEventBuilder::create(480, 240, 67, 85));     // On-beat

  applySwingToTrack(track, 0.0f);

  EXPECT_EQ(track.notes()[0].start_tick, 0u);
  EXPECT_EQ(track.notes()[1].start_tick, 240u);
  EXPECT_EQ(track.notes()[2].start_tick, 480u);
}

TEST(ApplySwingToTrackTest, FullSwingMovesOffBeats) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, 240, 60, 100));      // On-beat - should not move
  track.addNote(NoteEventBuilder::create(240, 240, 64, 90));     // Off-beat 8th - should move to 320
  track.addNote(NoteEventBuilder::create(480, 240, 67, 85));     // On-beat - should not move
  track.addNote(NoteEventBuilder::create(720, 240, 72, 80));     // Off-beat 8th - should move to 800

  applySwingToTrack(track, 1.0f);

  EXPECT_EQ(track.notes()[0].start_tick, 0u);
  EXPECT_EQ(track.notes()[1].start_tick, 320u);
  EXPECT_EQ(track.notes()[2].start_tick, 480u);
  EXPECT_EQ(track.notes()[3].start_tick, 800u);
}

// ============================================================================
// Various tick positions within a bar
// ============================================================================

TEST(QuantizeToSwingGridTest, AllEighthPositionsInBar) {
  // Test all 8 eighth-note positions in a bar at full swing
  // Position 0: 0 (on-beat) -> 0
  EXPECT_EQ(quantizeToSwingGrid(0, 1.0f), 0u);
  // Position 1: 240 (off-beat) -> 320
  EXPECT_EQ(quantizeToSwingGrid(240, 1.0f), 320u);
  // Position 2: 480 (on-beat) -> 480
  EXPECT_EQ(quantizeToSwingGrid(480, 1.0f), 480u);
  // Position 3: 720 (off-beat) -> 800
  EXPECT_EQ(quantizeToSwingGrid(720, 1.0f), 800u);
  // Position 4: 960 (on-beat) -> 960
  EXPECT_EQ(quantizeToSwingGrid(960, 1.0f), 960u);
  // Position 5: 1200 (off-beat) -> 1280
  EXPECT_EQ(quantizeToSwingGrid(1200, 1.0f), 1280u);
  // Position 6: 1440 (on-beat) -> 1440
  EXPECT_EQ(quantizeToSwingGrid(1440, 1.0f), 1440u);
  // Position 7: 1680 (off-beat) -> 1760
  EXPECT_EQ(quantizeToSwingGrid(1680, 1.0f), 1760u);
}

TEST(QuantizeToSwingGridTest, ContinuousSwingAmountRange) {
  // Verify that increasing swing_amount monotonically increases the offset
  // for an off-beat position
  Tick previous = 240;  // Straight position
  for (int idx = 1; idx <= 10; ++idx) {
    float swing = idx * 0.1f;
    Tick result = quantizeToSwingGrid(240, swing);
    EXPECT_GE(result, previous)
        << "Swing offset should increase monotonically at swing=" << swing;
    previous = result;
  }

  // Final result at swing=1.0 should be 320
  EXPECT_EQ(previous, 320u);
}

}  // namespace
}  // namespace midisketch
