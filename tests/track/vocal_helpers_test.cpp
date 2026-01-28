/**
 * @file vocal_helpers_test.cpp
 * @brief Unit tests for vocal helper functions.
 */

#include "track/vocal_helpers.h"

#include <gtest/gtest.h>

#include "core/timing_constants.h"
#include "core/types.h"

namespace midisketch {
namespace {

// ============================================================================
// removeOverlaps Tests
// ============================================================================

class RemoveOverlapsTest : public ::testing::Test {
 protected:
  std::vector<NoteEvent> createNotes(
      std::initializer_list<std::tuple<Tick, Tick, uint8_t>> notes_data) {
    std::vector<NoteEvent> notes;
    for (const auto& [start, duration, pitch] : notes_data) {
      NoteEvent note;
      note.start_tick = start;
      note.duration = duration;
      note.note = pitch;
      note.velocity = 80;
      notes.push_back(note);
    }
    return notes;
  }
};

TEST_F(RemoveOverlapsTest, NoOverlapNoChange) {
  // Notes with no overlap should remain unchanged
  auto notes = createNotes({
      {0, 240, 60},    // 0-240
      {240, 240, 62},  // 240-480 (no overlap)
      {480, 240, 64},  // 480-720 (no overlap)
  });

  removeOverlaps(notes);

  EXPECT_EQ(notes[0].duration, 240u);
  EXPECT_EQ(notes[1].duration, 240u);
  EXPECT_EQ(notes[2].duration, 240u);
}

TEST_F(RemoveOverlapsTest, OverlapTrimmedToMinimumDuration) {
  // When overlap requires trimming below minimum, keep original duration
  auto notes = createNotes({
      {0, 480, 60},   // 0-480, overlaps with next
      {60, 240, 62},  // 60-300, gap is only 60 ticks (< 120)
  });

  removeOverlaps(notes, TICK_SIXTEENTH);  // min_duration = 120

  // Gap of 60 is less than minimum 120, so original duration should be kept
  EXPECT_EQ(notes[0].duration, 480u) << "Duration should remain unchanged when gap < min_duration";
}

TEST_F(RemoveOverlapsTest, OverlapTrimmedWhenAboveMinimum) {
  // When overlap can be resolved while staying above minimum, trim the note
  auto notes = createNotes({
      {0, 480, 60},    // 0-480, overlaps with next
      {240, 240, 62},  // 240-480, gap is 240 ticks (>= 120)
  });

  removeOverlaps(notes, TICK_SIXTEENTH);

  // Gap of 240 is >= minimum 120, so duration should be trimmed
  EXPECT_EQ(notes[0].duration, 240u) << "Duration should be trimmed to prevent overlap";
  EXPECT_EQ(notes[0].start_tick + notes[0].duration, notes[1].start_tick);
}

TEST_F(RemoveOverlapsTest, MinDurationParameterRespected) {
  // Test that different min_duration values are respected
  auto notes1 = createNotes({
      {0, 480, 60},
      {100, 240, 62},  // gap is 100 ticks
  });

  // With min_duration = 120, gap of 100 is too small
  removeOverlaps(notes1, 120);
  EXPECT_EQ(notes1[0].duration, 480u) << "Should keep original when gap < 120";

  auto notes2 = createNotes({
      {0, 480, 60},
      {100, 240, 62},  // gap is 100 ticks
  });

  // With min_duration = 60 (UltraVocaloid), gap of 100 is acceptable
  removeOverlaps(notes2, 60);
  EXPECT_EQ(notes2[0].duration, 100u) << "Should trim when gap >= 60";
}

TEST_F(RemoveOverlapsTest, UltraVocaloidAllows32ndNotes) {
  // UltraVocaloid style allows 32nd notes (60 ticks)
  auto notes = createNotes({
      {0, 120, 60},   // 0-120
      {60, 120, 62},  // 60-180, overlap of 60 ticks
  });

  removeOverlaps(notes, TICK_32ND);  // min_duration = 60 (32nd note)

  // Gap is exactly 60, which equals min_duration, so it should be trimmed
  EXPECT_EQ(notes[0].duration, 60u) << "32nd note duration should be allowed for UltraVocaloid";
}

TEST_F(RemoveOverlapsTest, ChainedOverlapsHandled) {
  // Multiple overlapping notes in sequence
  auto notes = createNotes({
      {0, 480, 60},    // 0-480
      {240, 480, 62},  // 240-720, overlaps with previous
      {480, 480, 64},  // 480-960, overlaps with previous (after adjustment)
  });

  removeOverlaps(notes, TICK_SIXTEENTH);

  // Each note should end where the next begins
  EXPECT_LE(notes[0].start_tick + notes[0].duration, notes[1].start_tick);
  EXPECT_LE(notes[1].start_tick + notes[1].duration, notes[2].start_tick);
}

// ============================================================================
// applyCollisionAvoidanceWithIntervalConstraint Tests (chord boundary)
// ============================================================================

// Note: Full integration tests for chord boundary handling are in vocal_test.cpp
// These tests focus on the specific minimum duration behavior

}  // namespace
}  // namespace midisketch
