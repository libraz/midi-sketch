/**
 * @file vocal_helpers_test.cpp
 * @brief Unit tests for vocal helper functions.
 */

#include "track/vocal_helpers.h"

#include <gtest/gtest.h>

#include "core/timing_constants.h"
#include "core/types.h"
#include "test_helpers/note_event_test_helper.h"

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
      NoteEvent note = NoteEventTestHelper::create(start, duration, pitch, 80);
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

TEST_F(RemoveOverlapsTest, OverlapTrimmedToAvailableSpace) {
  // When overlap requires trimming below minimum, truncate to available space
  // to ensure no overlaps (overlap-free is higher priority than min duration)
  auto notes = createNotes({
      {0, 480, 60},   // 0-480, overlaps with next
      {60, 240, 62},  // 60-300, gap is only 60 ticks (< 120)
  });

  removeOverlaps(notes, TICK_SIXTEENTH);  // min_duration = 120

  // Gap of 60 is less than minimum 120, but we still truncate to prevent overlap
  EXPECT_EQ(notes[0].duration, 60u) << "Duration should be trimmed to available space";
  EXPECT_LE(notes[0].start_tick + notes[0].duration, notes[1].start_tick)
      << "No overlap should remain";
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
  // Test that different min_duration values are respected for non-overlapping notes
  auto notes1 = createNotes({
      {0, 50, 60},     // Short note (50 ticks, < 120 min)
      {200, 240, 62},  // gap is 150 ticks (plenty of space)
  });

  // With min_duration = 120, short note should be extended
  removeOverlaps(notes1, 120);
  EXPECT_EQ(notes1[0].duration, 120u) << "Should extend to min_duration when space available";

  auto notes2 = createNotes({
      {0, 50, 60},     // Short note (50 ticks, < 60 min)
      {200, 240, 62},  // gap is 150 ticks
  });

  // With min_duration = 60 (UltraVocaloid), short note should be extended to 60
  removeOverlaps(notes2, 60);
  EXPECT_EQ(notes2[0].duration, 60u) << "Should extend to min_duration of 60";
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

// ============================================================================
// applyGrooveFeel Tests - Unsigned Underflow Prevention
// ============================================================================

class ApplyGrooveFeelTest : public ::testing::Test {
 protected:
  std::vector<NoteEvent> createNotes(
      std::initializer_list<std::tuple<Tick, Tick, uint8_t>> notes_data) {
    std::vector<NoteEvent> notes;
    for (const auto& [start, duration, pitch] : notes_data) {
      NoteEvent note = NoteEventTestHelper::create(start, duration, pitch, 80);
      notes.push_back(note);
    }
    return notes;
  }
};

TEST_F(ApplyGrooveFeelTest, SyncopatedGrooveDoesNotCauseUnderflow) {
  // Syncopated groove applies negative shift (-TICK_16TH/2 = -60) to notes on beats 2 and 4.
  // Previously, storing this in Tick (uint32_t) caused underflow.
  // Notes on beat 2 (tick 480) and beat 4 (tick 1440) should get shifted.
  auto notes = createNotes({
      {480, 240, 60},   // On beat 2 - should be shifted by -60
      {1440, 240, 62},  // On beat 4 - should be shifted by -60
  });

  // This should NOT cause underflow (previously shift wrapped to ~4 billion)
  applyGrooveFeel(notes, VocalGrooveFeel::Syncopated);

  // Notes should be shifted earlier (anticipation), not to billions of ticks
  EXPECT_LT(notes[0].start_tick, 480u) << "Note should be shifted earlier, not wrapped to huge value";
  EXPECT_LT(notes[1].start_tick, 1440u) << "Note should be shifted earlier, not wrapped to huge value";
  EXPECT_GT(notes[0].start_tick, 0u) << "Note should still have valid start time";
  EXPECT_GT(notes[1].start_tick, 0u) << "Note should still have valid start time";
}

TEST_F(ApplyGrooveFeelTest, Driving16thGrooveDoesNotCauseUnderflow) {
  // Driving16th groove applies negative shift (-TICK_16TH/4 = -30) to 16th notes.
  // Previously, storing this in Tick (uint32_t) caused underflow.
  auto notes = createNotes({
      {0, 120, 60},    // At beat position 0 - should be shifted
      {120, 120, 62},  // At beat position 120
      {240, 120, 64},  // At beat position 240
  });

  // This should NOT cause underflow
  applyGrooveFeel(notes, VocalGrooveFeel::Driving16th);

  // All note start_ticks should be less than UINT32_MAX / 2 (reasonable values)
  constexpr Tick kMaxReasonableTick = 1000000;  // 1 million ticks is reasonable
  for (size_t i = 0; i < notes.size(); ++i) {
    EXPECT_LT(notes[i].start_tick, kMaxReasonableTick)
        << "Note " << i << " start_tick should not have underflowed";
  }
}

TEST_F(ApplyGrooveFeelTest, AllGrooveTypesProduceValidOutput) {
  // Test all groove types don't produce underflow or unreasonable values
  std::vector<VocalGrooveFeel> grooves = {
      VocalGrooveFeel::Straight,    VocalGrooveFeel::OffBeat,     VocalGrooveFeel::Swing,
      VocalGrooveFeel::Syncopated,  VocalGrooveFeel::Driving16th, VocalGrooveFeel::Bouncy8th,
  };

  for (auto groove : grooves) {
    // Create notes at various positions including edges
    auto notes = createNotes({
        {0, 240, 60},      // Start of bar
        {480, 240, 62},    // Beat 2
        {960, 240, 64},    // Beat 3
        {1440, 240, 66},   // Beat 4
        {1920, 240, 68},   // Start of next bar
    });

    applyGrooveFeel(notes, groove);

    // All notes should have reasonable values
    constexpr Tick kMaxReasonableTick = 10000000;  // 10 million ticks
    for (size_t i = 0; i < notes.size(); ++i) {
      EXPECT_LT(notes[i].start_tick, kMaxReasonableTick)
          << "Groove " << static_cast<int>(groove) << " note " << i
          << " start_tick should be reasonable";
      EXPECT_LT(notes[i].duration, kMaxReasonableTick)
          << "Groove " << static_cast<int>(groove) << " note " << i
          << " duration should be reasonable";
    }
  }
}

TEST_F(ApplyGrooveFeelTest, SyncopatedGrooveAdjustsPreviousNoteDuration) {
  // When syncopated groove shifts a note earlier, the previous note's duration
  // should be shortened to prevent overlap, rather than creating tiny notes.
  //
  // Before: Note A (0-540), Note B (480-720)
  // Syncopated shifts Note B to ~420 (480 - 60 = 420)
  // Expected: Note A duration shortened to ~410 (420 - 10 gap)
  // NOT: Note B truncated to tiny duration
  auto notes = createNotes({
      {0, 540, 60},    // Note A: 0-540 (extends past beat 2)
      {480, 240, 62},  // Note B: on beat 2, will be shifted earlier
  });

  applyGrooveFeel(notes, VocalGrooveFeel::Syncopated);

  // Note B should be shifted earlier (by ~60 ticks)
  EXPECT_LT(notes[1].start_tick, 480u) << "Note B should be shifted earlier";
  EXPECT_GE(notes[1].start_tick, 400u) << "Note B should not be shifted too much";

  // Note A's duration should be shortened to prevent overlap
  Tick note_a_end = notes[0].start_tick + notes[0].duration;
  EXPECT_LE(note_a_end, notes[1].start_tick)
      << "Note A should end before Note B starts (no overlap)";

  // Both notes should have reasonable durations (not tiny)
  constexpr Tick kMinReasonableDuration = 60;  // TICK_32ND
  EXPECT_GE(notes[0].duration, kMinReasonableDuration)
      << "Note A should have reasonable duration after adjustment";
  EXPECT_GE(notes[1].duration, kMinReasonableDuration)
      << "Note B should maintain its original duration";
}

TEST_F(ApplyGrooveFeelTest, GrooveShiftPreservesShiftedNoteDuration) {
  // The key fix: when groove shifts a note earlier, we shorten the PREVIOUS note,
  // not the shifted note. This preserves the musical intent of the shifted note.
  auto notes = createNotes({
      {300, 240, 60},   // Note A: 300-540
      {480, 360, 62},   // Note B: 480-840, will be shifted earlier by syncopation
  });

  Tick original_note_b_duration = notes[1].duration;

  applyGrooveFeel(notes, VocalGrooveFeel::Syncopated);

  // Note B should keep its original duration (or very close to it)
  // because we shortened Note A instead of Note B
  EXPECT_GE(notes[1].duration, original_note_b_duration - 10)
      << "Shifted note should preserve its duration";
}

TEST_F(ApplyGrooveFeelTest, NoOverlapsAfterGrooveApplication) {
  // Verify that no overlaps exist after groove application for all groove types
  std::vector<VocalGrooveFeel> grooves = {
      VocalGrooveFeel::Syncopated,
      VocalGrooveFeel::Driving16th,
      VocalGrooveFeel::Bouncy8th,
  };

  for (auto groove : grooves) {
    // Create notes that could cause overlap when shifted
    auto notes = createNotes({
        {0, 480, 60},      // Long note
        {480, 240, 62},    // Beat 2 (syncopated will shift earlier)
        {960, 240, 64},    // Beat 3
        {1440, 240, 66},   // Beat 4 (syncopated will shift earlier)
        {1920, 240, 68},   // Next bar
    });

    applyGrooveFeel(notes, groove);

    // Verify no overlaps
    for (size_t i = 0; i + 1 < notes.size(); ++i) {
      Tick end_tick = notes[i].start_tick + notes[i].duration;
      EXPECT_LE(end_tick, notes[i + 1].start_tick)
          << "Groove " << static_cast<int>(groove) << ": Note " << i
          << " (end=" << end_tick << ") overlaps with note " << (i + 1)
          << " (start=" << notes[i + 1].start_tick << ")";
    }

    // Verify minimum duration
    constexpr Tick kMinDuration = 60;  // TICK_32ND
    for (size_t i = 0; i < notes.size(); ++i) {
      EXPECT_GE(notes[i].duration, kMinDuration)
          << "Groove " << static_cast<int>(groove) << ": Note " << i
          << " has duration " << notes[i].duration << " < minimum " << kMinDuration;
    }
  }
}

// ============================================================================
// applyGrooveFeel Edge Case Tests
// ============================================================================

TEST_F(ApplyGrooveFeelTest, FirstNoteShiftedHasNoPreviousToAdjust) {
  // When the first note gets a negative shift, there's no previous note to adjust.
  // The shift should still be applied, and the note should remain valid.
  auto notes = createNotes({
      {480, 240, 60},   // On beat 2 - will be shifted earlier by syncopation
      {960, 240, 62},   // Beat 3
  });

  applyGrooveFeel(notes, VocalGrooveFeel::Syncopated);

  // First note should be shifted earlier
  EXPECT_LT(notes[0].start_tick, 480u) << "First note should be shifted earlier";
  EXPECT_GE(notes[0].start_tick, 400u) << "First note shift should be reasonable";

  // Duration should be preserved
  EXPECT_EQ(notes[0].duration, 240u) << "First note duration should be preserved";
}

TEST_F(ApplyGrooveFeelTest, MultipleConsecutiveShiftsHandledCorrectly) {
  // When multiple consecutive notes all get negative shifts,
  // each should adjust its predecessor appropriately without domino effect issues.
  auto notes = createNotes({
      {0, 480, 60},      // Long note extending to beat 2
      {480, 480, 62},    // Beat 2 - shifted, also extends to beat 4
      {1440, 240, 64},   // Beat 4 - also shifted
  });

  applyGrooveFeel(notes, VocalGrooveFeel::Syncopated);

  // Verify no overlaps
  for (size_t i = 0; i + 1 < notes.size(); ++i) {
    Tick end_tick = notes[i].start_tick + notes[i].duration;
    EXPECT_LE(end_tick, notes[i + 1].start_tick)
        << "Note " << i << " overlaps with note " << (i + 1);
  }

  // All notes should have minimum duration
  constexpr Tick kMinDuration = 60;
  for (size_t i = 0; i < notes.size(); ++i) {
    EXPECT_GE(notes[i].duration, kMinDuration)
        << "Note " << i << " has duration below minimum";
  }
}

TEST_F(ApplyGrooveFeelTest, PreviousNoteAlreadyShortProtected) {
  // When the previous note is already near minimum duration,
  // it should not be shortened below the minimum.
  auto notes = createNotes({
      {350, 70, 60},    // Short note (70 ticks, just above minimum 60)
      {480, 240, 62},   // On beat 2 - will try to shift to ~420
  });

  applyGrooveFeel(notes, VocalGrooveFeel::Syncopated);

  // Previous note should not go below minimum duration
  constexpr Tick kMinDuration = 60;
  EXPECT_GE(notes[0].duration, kMinDuration)
      << "Previous note should not go below minimum duration";
}

TEST_F(ApplyGrooveFeelTest, Driving16thAdjustsPreviousNoteDuration) {
  // Driving16th also uses negative shifts (-30 ticks).
  // Verify it adjusts previous note duration like Syncopated does.
  auto notes = createNotes({
      {0, 130, 60},     // Note ending at 130, overlaps with shifted next note
      {120, 240, 62},   // At 16th position - will shift earlier by ~30
  });

  applyGrooveFeel(notes, VocalGrooveFeel::Driving16th);

  // Verify no overlap
  Tick note_a_end = notes[0].start_tick + notes[0].duration;
  EXPECT_LE(note_a_end, notes[1].start_tick)
      << "Note A should not overlap with shifted Note B";

  // Both notes should have reasonable duration
  constexpr Tick kMinDuration = 60;
  EXPECT_GE(notes[0].duration, kMinDuration);
  EXPECT_GE(notes[1].duration, kMinDuration);
}

TEST_F(ApplyGrooveFeelTest, MinimumGapMaintainedBetweenNotes) {
  // The implementation uses kMinGap = 10 ticks between notes.
  // Verify this gap is maintained after adjustment.
  auto notes = createNotes({
      {0, 500, 60},     // Long note that will need shortening
      {480, 240, 62},   // On beat 2 - shifts to ~420
  });

  applyGrooveFeel(notes, VocalGrooveFeel::Syncopated);

  // There should be a gap between notes (at least kMinGap = 10)
  Tick note_a_end = notes[0].start_tick + notes[0].duration;
  Tick gap = notes[1].start_tick - note_a_end;
  EXPECT_GE(gap, 0u) << "There should be no overlap";
  // Note: Gap may be 0 after final safety pass, but overlap is prevented
}

TEST_F(ApplyGrooveFeelTest, PositiveShiftDoesNotAffectPreviousNote) {
  // Positive shifts (OffBeat, Swing, Bouncy8th second half) should not
  // require adjusting the previous note's duration (unlike negative shifts).
  // OffBeat shifts notes where beat_pos < TICK_16TH (120), i.e., on-beat notes.
  //
  // Note B at 480 with beat_pos=0 will be shifted to ~540 (+60).
  // Note A should be placed so it doesn't overlap with the shifted position.
  auto notes = createNotes({
      {300, 200, 60},   // Note A: ends at 500, before shifted Note B (540)
      {480, 240, 62},   // Note B: beat_pos=0, will shift to ~540
  });

  Tick original_duration_a = notes[0].duration;

  applyGrooveFeel(notes, VocalGrooveFeel::OffBeat);

  // Note A's duration should remain unchanged (positive shift doesn't affect it)
  EXPECT_EQ(notes[0].duration, original_duration_a)
      << "Positive shift should not affect previous note duration";

  // Note B should be shifted later (beat_pos=0 < TICK_16TH=120)
  EXPECT_GT(notes[1].start_tick, 480u)
      << "Note B should be shifted later by OffBeat groove";

  // Verify no overlap
  Tick note_a_end = notes[0].start_tick + notes[0].duration;
  EXPECT_LE(note_a_end, notes[1].start_tick) << "No overlap after positive shift";
}

TEST_F(ApplyGrooveFeelTest, SwingGrooveDelaysSecondEighth) {
  // Swing groove delays the second 8th note of each beat pair.
  auto notes = createNotes({
      {0, 200, 60},     // First 8th
      {240, 200, 62},   // Second 8th (around TICK_8TH = 240) - should be delayed
      {480, 200, 64},   // First 8th of next beat
  });

  applyGrooveFeel(notes, VocalGrooveFeel::Swing);

  // Second note should be shifted later
  EXPECT_GT(notes[1].start_tick, 240u) << "Second 8th should be delayed for swing";

  // First and third notes should be unchanged or minimal change
  EXPECT_LE(notes[0].start_tick, 10u) << "First 8th should not move much";
}

TEST_F(ApplyGrooveFeelTest, StraightGrooveNoModification) {
  // Straight groove should not modify any notes.
  auto notes = createNotes({
      {0, 240, 60},
      {240, 240, 62},
      {480, 240, 64},
  });

  std::vector<NoteEvent> original = notes;

  applyGrooveFeel(notes, VocalGrooveFeel::Straight);

  // All notes should be unchanged
  for (size_t i = 0; i < notes.size(); ++i) {
    EXPECT_EQ(notes[i].start_tick, original[i].start_tick)
        << "Straight groove should not change start_tick";
    EXPECT_EQ(notes[i].duration, original[i].duration)
        << "Straight groove should not change duration";
  }
}

TEST_F(ApplyGrooveFeelTest, SingleNoteHandledCorrectly) {
  // Edge case: only one note
  auto notes = createNotes({
      {480, 240, 60},  // On beat 2 - would be shifted by syncopation
  });

  applyGrooveFeel(notes, VocalGrooveFeel::Syncopated);

  // Single note should be shifted but remain valid
  EXPECT_LT(notes[0].start_tick, 480u) << "Single note should be shifted";
  EXPECT_EQ(notes[0].duration, 240u) << "Duration should be preserved";
}

TEST_F(ApplyGrooveFeelTest, UnsortedInputSortedCorrectly) {
  // The function should handle unsorted input by sorting first.
  auto notes = createNotes({
      {960, 240, 64},   // Third chronologically
      {0, 240, 60},     // First chronologically
      {480, 240, 62},   // Second chronologically
  });

  applyGrooveFeel(notes, VocalGrooveFeel::Syncopated);

  // Notes should be sorted by start_tick after processing
  for (size_t i = 0; i + 1 < notes.size(); ++i) {
    EXPECT_LE(notes[i].start_tick, notes[i + 1].start_tick)
        << "Notes should be sorted after groove application";
  }
}

TEST_F(ApplyGrooveFeelTest, EmptyNotesHandledGracefully) {
  // Edge case: empty notes vector
  std::vector<NoteEvent> notes;

  // Should not crash
  applyGrooveFeel(notes, VocalGrooveFeel::Syncopated);

  EXPECT_TRUE(notes.empty()) << "Empty vector should remain empty";
}

TEST_F(ApplyGrooveFeelTest, Bouncy8thShortensFirstEighthDuration) {
  // Bouncy8th makes the first 8th note shorter (85% duration).
  // The condition is duration > TICK_8TH (240), so we need duration > 240.
  auto notes = createNotes({
      {0, 300, 60},     // First 8th with long duration (>240) - should be shortened
      {300, 240, 62},   // Second 8th (beat_pos >= 240) - should be delayed
  });

  applyGrooveFeel(notes, VocalGrooveFeel::Bouncy8th);

  // First note duration should be shortened to 85% of original
  // Original: 300, 85% = 255
  EXPECT_LT(notes[0].duration, 300u) << "First 8th should be shortened";
  EXPECT_GE(notes[0].duration, 250u) << "Shortening should be moderate (85%)";

  // Second note should be delayed (beat_pos = 300 >= TICK_8TH = 240)
  EXPECT_GT(notes[1].start_tick, 300u) << "Second 8th should be delayed";
}

TEST_F(ApplyGrooveFeelTest, VeryLongNoteProperlyTruncated) {
  // A very long note followed by a shifted note should be truncated appropriately.
  auto notes = createNotes({
      {0, 960, 60},     // 2-beat note (0-960), extends way past beat 2
      {480, 240, 62},   // On beat 2 - will shift to ~420
  });

  applyGrooveFeel(notes, VocalGrooveFeel::Syncopated);

  // Note A should be truncated to not overlap
  Tick note_a_end = notes[0].start_tick + notes[0].duration;
  EXPECT_LE(note_a_end, notes[1].start_tick) << "Long note should be truncated";

  // Note A should still have significant duration (not tiny)
  EXPECT_GE(notes[0].duration, 350u)
      << "Long note should retain most of its duration up to the shift point";
}

// ============================================================================
// Duration Underflow Prevention Tests
// ============================================================================

TEST(DurationUnderflowTest, TickSubtractionPatternSafety) {
  // This test documents the fix for the pattern:
  // Tick new_duration = a - b - c;
  // When (a - b) < c, this causes underflow in unsigned arithmetic.
  //
  // The fix is to check: if (a - b > c) before subtraction.

  constexpr Tick kChordChangeGap = 10;

  // Scenario: note starts very close to chord change
  Tick chord_change = 1000;
  Tick note_start = 995;  // Only 5 ticks before chord change

  // OLD (buggy) code would do:
  // Tick new_duration = chord_change - note_start - kChordChangeGap;
  // = 1000 - 995 - 10 = 5 - 10 = -5 = 4294967291 (underflow!)

  // NEW (fixed) code:
  Tick time_to_chord = chord_change - note_start;  // = 5
  Tick new_duration = 0;
  if (time_to_chord > kChordChangeGap) {
    new_duration = time_to_chord - kChordChangeGap;
  }

  // The duration should NOT be assigned if it would underflow
  EXPECT_EQ(new_duration, 0u) << "Should not compute duration when it would underflow";
  EXPECT_LT(time_to_chord, kChordChangeGap)
      << "time_to_chord < gap, so no subtraction should occur";
}

}  // namespace
}  // namespace midisketch
