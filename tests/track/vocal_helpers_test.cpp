/**
 * @file vocal_helpers_test.cpp
 * @brief Unit tests for vocal helper functions.
 */

#include "track/vocal/vocal_helpers.h"

#include <gtest/gtest.h>

#include "core/melody_templates.h"
#include "core/midi_track.h"
#include "core/note_source.h"
#include "core/note_timeline_utils.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "test_helpers/note_event_test_helper.h"
#include "test_support/stub_harmony_context.h"
#include "track/melody/melody_utils.h"
#include "track/vocal/melody_designer.h"
#include "track/vocal/phrase_plan.h"
#include "track/vocal/phrase_planner.h"
#include "track/vocal/vocal_post_process.h"

namespace midisketch {
namespace {

TEST(RegisterShiftTest, LaterVerseDoesNotReceiveProgressiveLift) {
  StyleMelodyParams params;
  params.verse_register_shift = -2;

  EXPECT_EQ(getRegisterShift(SectionType::A, params, 1), -2);
  EXPECT_EQ(getRegisterShift(SectionType::A, params, 2), -2);
  EXPECT_EQ(getRegisterShift(SectionType::A, params, 4), -2);
}

TEST(RegisterShiftTest, LaterChorusReceivesProgressiveLift) {
  StyleMelodyParams params;
  params.chorus_register_shift = 5;

  EXPECT_EQ(getRegisterShift(SectionType::Chorus, params, 1), 5);
  EXPECT_EQ(getRegisterShift(SectionType::Chorus, params, 2), 7);
  EXPECT_EQ(getRegisterShift(SectionType::Chorus, params, 4), 9);
}

TEST(NonChordToneLegalityTest, SuspensionMayRemainAccentedAtBoundary) {
  EXPECT_TRUE(isLegalSuspensionTone(65, 65, 64, 0, TICK_EIGHTH, 0));
  EXPECT_TRUE(isLegalNonChordTone(65, 65, 64, 0, TICK_EIGHTH, 0));

  EXPECT_FALSE(isLegalSuspensionTone(65, 65, 64, TICK_EIGHTH, TICK_EIGHTH, 0))
      << "Suspensions should be accented, not off-beat passing tones.";
  EXPECT_FALSE(isLegalSuspensionTone(65, 65, 67, 0, TICK_EIGHTH, 0))
      << "Suspensions must resolve downward by step.";
}

TEST(VocalPostProcessTest, BreakSameDirectionLeapChainsFoldsThirdLeap) {
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);

  std::vector<NoteEvent> notes = {
      NoteEventTestHelper::create(0, TICK_EIGHTH, 60, 90),
      NoteEventTestHelper::create(TICK_EIGHTH, TICK_EIGHTH, 64, 90),
      NoteEventTestHelper::create(TICK_EIGHTH * 2, TICK_EIGHTH, 67, 90),
      NoteEventTestHelper::create(TICK_EIGHTH * 3, TICK_EIGHTH, 71, 90),
  };

  breakSameDirectionLeapChains(notes, harmony, 57, 86);

  ASSERT_EQ(notes.size(), 4u);
  EXPECT_LT(std::abs(static_cast<int>(notes[3].note) - static_cast<int>(notes[2].note)), 3)
      << "The third same-direction leap should be folded into step/repeat motion.";
}

TEST(VocalPostProcessTest, SamePitchRunDoesNotCrossLongRest) {
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);

  std::vector<NoteEvent> notes = {
      NoteEventTestHelper::create(0, TICK_SIXTEENTH, 60, 90),
      NoteEventTestHelper::create(TICK_SIXTEENTH, TICK_SIXTEENTH, 60, 90),
      NoteEventTestHelper::create(TICK_SIXTEENTH * 2, TICK_SIXTEENTH, 60, 90),
      NoteEventTestHelper::create(TICKS_PER_BAR, TICK_SIXTEENTH, 60, 90),
  };

  breakConsecutiveSamePitch(notes, harmony, 57, 86, 3);

  for (const auto& note : notes) {
    EXPECT_EQ(note.note, 60)
        << "A same-pitch note after a rest longer than one beat starts a new streak.";
  }
}

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

  NoteTimeline::fixOverlapsWithMinDuration(notes, TICK_SIXTEENTH);

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

  NoteTimeline::fixOverlapsWithMinDuration(notes, TICK_SIXTEENTH);  // min_duration = 120

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

  NoteTimeline::fixOverlapsWithMinDuration(notes, TICK_SIXTEENTH);

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
  NoteTimeline::fixOverlapsWithMinDuration(notes1, 120);
  EXPECT_EQ(notes1[0].duration, 120u) << "Should extend to min_duration when space available";

  auto notes2 = createNotes({
      {0, 50, 60},     // Short note (50 ticks, < 60 min)
      {200, 240, 62},  // gap is 150 ticks
  });

  // With min_duration = 60 (UltraVocaloid), short note should be extended to 60
  NoteTimeline::fixOverlapsWithMinDuration(notes2, 60);
  EXPECT_EQ(notes2[0].duration, 60u) << "Should extend to min_duration of 60";
}

TEST_F(RemoveOverlapsTest, UltraVocaloidAllows32ndNotes) {
  // UltraVocaloid style allows 32nd notes (60 ticks)
  auto notes = createNotes({
      {0, 120, 60},   // 0-120
      {60, 120, 62},  // 60-180, overlap of 60 ticks
  });

  NoteTimeline::fixOverlapsWithMinDuration(notes, TICK_32ND);  // min_duration = 60 (32nd note)

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

  NoteTimeline::fixOverlapsWithMinDuration(notes, TICK_SIXTEENTH);

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
  EXPECT_LT(notes[0].start_tick, 480u)
      << "Note should be shifted earlier, not wrapped to huge value";
  EXPECT_LT(notes[1].start_tick, 1440u)
      << "Note should be shifted earlier, not wrapped to huge value";
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
      VocalGrooveFeel::Straight,   VocalGrooveFeel::OffBeat,     VocalGrooveFeel::Swing,
      VocalGrooveFeel::Syncopated, VocalGrooveFeel::Driving16th, VocalGrooveFeel::Bouncy8th,
  };

  for (auto groove : grooves) {
    // Create notes at various positions including edges
    auto notes = createNotes({
        {0, 240, 60},     // Start of bar
        {480, 240, 62},   // Beat 2
        {960, 240, 64},   // Beat 3
        {1440, 240, 66},  // Beat 4
        {1920, 240, 68},  // Start of next bar
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
      {300, 240, 60},  // Note A: 300-540
      {480, 360, 62},  // Note B: 480-840, will be shifted earlier by syncopation
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
        {0, 480, 60},     // Long note
        {480, 240, 62},   // Beat 2 (syncopated will shift earlier)
        {960, 240, 64},   // Beat 3
        {1440, 240, 66},  // Beat 4 (syncopated will shift earlier)
        {1920, 240, 68},  // Next bar
    });

    applyGrooveFeel(notes, groove);

    // Verify no overlaps
    for (size_t i = 0; i + 1 < notes.size(); ++i) {
      Tick end_tick = notes[i].start_tick + notes[i].duration;
      EXPECT_LE(end_tick, notes[i + 1].start_tick)
          << "Groove " << static_cast<int>(groove) << ": Note " << i << " (end=" << end_tick
          << ") overlaps with note " << (i + 1) << " (start=" << notes[i + 1].start_tick << ")";
    }

    // Verify minimum duration
    constexpr Tick kMinDuration = 60;  // TICK_32ND
    for (size_t i = 0; i < notes.size(); ++i) {
      EXPECT_GE(notes[i].duration, kMinDuration)
          << "Groove " << static_cast<int>(groove) << ": Note " << i << " has duration "
          << notes[i].duration << " < minimum " << kMinDuration;
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
      {480, 240, 60},  // On beat 2 - will be shifted earlier by syncopation
      {960, 240, 62},  // Beat 3
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
      {0, 480, 60},     // Long note extending to beat 2
      {480, 480, 62},   // Beat 2 - shifted, also extends to beat 4
      {1440, 240, 64},  // Beat 4 - also shifted
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
    EXPECT_GE(notes[i].duration, kMinDuration) << "Note " << i << " has duration below minimum";
  }
}

TEST_F(ApplyGrooveFeelTest, PreviousNoteAlreadyShortProtected) {
  // When the previous note is already near minimum duration,
  // it should not be shortened below the minimum.
  auto notes = createNotes({
      {350, 70, 60},   // Short note (70 ticks, just above minimum 60)
      {480, 240, 62},  // On beat 2 - will try to shift to ~420
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
      {0, 130, 60},    // Note ending at 130, overlaps with shifted next note
      {120, 240, 62},  // At 16th position - will shift earlier by ~30
  });

  applyGrooveFeel(notes, VocalGrooveFeel::Driving16th);

  // Verify no overlap
  Tick note_a_end = notes[0].start_tick + notes[0].duration;
  EXPECT_LE(note_a_end, notes[1].start_tick) << "Note A should not overlap with shifted Note B";

  // Both notes should have reasonable duration
  constexpr Tick kMinDuration = 60;
  EXPECT_GE(notes[0].duration, kMinDuration);
  EXPECT_GE(notes[1].duration, kMinDuration);
}

TEST_F(ApplyGrooveFeelTest, MinimumGapMaintainedBetweenNotes) {
  // The implementation uses kMinGap = 10 ticks between notes.
  // Verify this gap is maintained after adjustment.
  auto notes = createNotes({
      {0, 500, 60},    // Long note that will need shortening
      {480, 240, 62},  // On beat 2 - shifts to ~420
  });

  applyGrooveFeel(notes, VocalGrooveFeel::Syncopated);

  // Ticks are unsigned, so the subtraction that used to stand here turned an
  // overlap into a gap of about 4.29e9 and then compared it against zero. The
  // two positions are compared directly instead, which is the claim.
  Tick note_a_end = notes[0].start_tick + notes[0].duration;
  EXPECT_GE(notes[1].start_tick, note_a_end)
      << "the second note starts " << (note_a_end - notes[1].start_tick)
      << " ticks before the first one ends";
}

TEST_F(ApplyGrooveFeelTest, PositiveShiftDoesNotAffectPreviousNote) {
  // Positive shifts (OffBeat, Swing, Bouncy8th second half) should not
  // require adjusting the previous note's duration (unlike negative shifts).
  // OffBeat shifts notes where beat_pos < TICK_16TH (120), i.e., on-beat notes.
  //
  // Note B at 480 with beat_pos=0 will be shifted to ~540 (+60).
  // Note A should be placed so it doesn't overlap with the shifted position.
  auto notes = createNotes({
      {300, 200, 60},  // Note A: ends at 500, before shifted Note B (540)
      {480, 240, 62},  // Note B: beat_pos=0, will shift to ~540
  });

  Tick original_duration_a = notes[0].duration;

  applyGrooveFeel(notes, VocalGrooveFeel::OffBeat);

  // Note A's duration should remain unchanged (positive shift doesn't affect it)
  EXPECT_EQ(notes[0].duration, original_duration_a)
      << "Positive shift should not affect previous note duration";

  // Note B should be shifted later (beat_pos=0 < TICK_16TH=120)
  EXPECT_GT(notes[1].start_tick, 480u) << "Note B should be shifted later by OffBeat groove";

  // Verify no overlap
  Tick note_a_end = notes[0].start_tick + notes[0].duration;
  EXPECT_LE(note_a_end, notes[1].start_tick) << "No overlap after positive shift";
}

TEST_F(ApplyGrooveFeelTest, SwingGrooveDelaysSecondEighth) {
  // Swing groove delays the second 8th note of each beat pair.
  auto notes = createNotes({
      {0, 200, 60},    // First 8th
      {240, 200, 62},  // Second 8th (around TICK_8TH = 240) - should be delayed
      {480, 200, 64},  // First 8th of next beat
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
      {960, 240, 64},  // Third chronologically
      {0, 240, 60},    // First chronologically
      {480, 240, 62},  // Second chronologically
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
      {0, 300, 60},    // First 8th with long duration (>240) - should be shortened
      {300, 240, 62},  // Second 8th (beat_pos >= 240) - should be delayed
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
      {0, 960, 60},    // 2-beat note (0-960), extends way past beat 2
      {480, 240, 62},  // On beat 2 - will shift to ~420
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

// ============================================================================
// mergeSamePitchNotes — SyllabicSub preservation tests
// ============================================================================

class MergeSamePitchSyllabicSubTest : public ::testing::Test {
 protected:
  NoteEvent makeSubNote(Tick start, Tick duration, uint8_t pitch, uint8_t velocity = 80) {
    NoteEvent note = NoteEventTestHelper::create(start, duration, pitch, velocity);
    note.is_syllabic_subdivision = true;
#ifdef MIDISKETCH_NOTE_PROVENANCE
    note.prov_source = static_cast<uint8_t>(NoteSource::SyllabicSub);
#endif
    return note;
  }

  NoteEvent makeMelodyNote(Tick start, Tick duration, uint8_t pitch, uint8_t velocity = 80) {
    NoteEvent note = NoteEventTestHelper::create(start, duration, pitch, velocity);
#ifdef MIDISKETCH_NOTE_PROVENANCE
    note.prov_source = static_cast<uint8_t>(NoteSource::MelodyPhrase);
#endif
    return note;
  }
};

TEST_F(MergeSamePitchSyllabicSubTest, PreservesSyllabicSubNotes) {
  // Two SyllabicSub notes with gap=0 and same pitch must NOT be merged.
  std::vector<NoteEvent> notes = {
      makeSubNote(0, TICK_QUARTER, 72),
      makeSubNote(TICK_QUARTER, TICK_QUARTER, 72),
  };

  mergeSamePitchNotes(notes, TICK_EIGHTH);

  ASSERT_EQ(notes.size(), 2u) << "SyllabicSub notes must not be merged";
  EXPECT_EQ(notes[0].start_tick, 0u);
  EXPECT_EQ(notes[0].duration, TICK_QUARTER);
  EXPECT_EQ(notes[1].start_tick, TICK_QUARTER);
  EXPECT_EQ(notes[1].duration, TICK_QUARTER);
}

TEST_F(MergeSamePitchSyllabicSubTest, StillMergesNonSubdividedNotes) {
  // MelodyPhrase notes with gap=0 and same pitch should still merge.
  std::vector<NoteEvent> notes = {
      makeMelodyNote(0, TICK_QUARTER, 72),
      makeMelodyNote(TICK_QUARTER, TICK_QUARTER, 72),
  };

  mergeSamePitchNotes(notes, TICK_EIGHTH);

  ASSERT_EQ(notes.size(), 1u) << "MelodyPhrase notes should still be merged";
  EXPECT_EQ(notes[0].duration, TICK_QUARTER * 2);
}

TEST_F(MergeSamePitchSyllabicSubTest, DoesNotMergeSubdividedWithNormal) {
  // A SyllabicSub note adjacent to a MelodyPhrase note must not merge.
  std::vector<NoteEvent> notes = {
      makeMelodyNote(0, TICK_QUARTER, 72),
      makeSubNote(TICK_QUARTER, TICK_QUARTER, 72),
  };

  mergeSamePitchNotes(notes, TICK_EIGHTH);

  ASSERT_EQ(notes.size(), 2u) << "SyllabicSub boundary must prevent merge";
}

TEST_F(MergeSamePitchSyllabicSubTest, FourWaySplitPreserved) {
  // A 4-way syllabic split should survive mergeSamePitchNotes entirely.
  std::vector<NoteEvent> notes = {
      makeSubNote(0, TICK_QUARTER, 72),
      makeSubNote(TICK_QUARTER, TICK_QUARTER, 72),
      makeSubNote(TICK_QUARTER * 2, TICK_QUARTER, 72),
      makeSubNote(TICK_QUARTER * 3, TICK_QUARTER, 72),
  };

  mergeSamePitchNotes(notes, TICK_EIGHTH);

  ASSERT_EQ(notes.size(), 4u) << "All 4 syllabic sub-notes must survive";
  for (size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(notes[i].start_tick, TICK_QUARTER * i);
    EXPECT_EQ(notes[i].duration, TICK_QUARTER);
    EXPECT_EQ(notes[i].note, 72);
  }
}

// ============================================================================
// Chord-tone identity and non-chord-tone legality
// ============================================================================

/// @brief Minimal chord lookup that reports one chord for every tick.
///
/// The generation stubs answer isSecondaryDominantAt() with a constant false,
/// which is precisely the case these tests need to vary.
class FixedChordLookup : public IChordLookup {
 public:
  FixedChordLookup(int8_t degree, std::vector<int> tones, bool secondary_dominant)
      : degree_(degree), tones_(std::move(tones)), secondary_dominant_(secondary_dominant) {}

  int8_t getChordDegreeAt(Tick /*tick*/) const override { return degree_; }

  ChordTones getChordTonesAt(Tick /*tick*/) const override {
    ChordTones result{};
    result.pitch_classes.fill(-1);
    result.count = static_cast<uint8_t>(std::min<size_t>(tones_.size(), 5));
    std::copy_n(tones_.begin(), result.count, result.pitch_classes.begin());
    return result;
  }

  Tick getNextChordChangeTick(Tick /*after*/) const override { return 0; }

  bool isSecondaryDominantAt(Tick /*tick*/) const override { return secondary_dominant_; }

 private:
  int8_t degree_;
  std::vector<int> tones_;
  bool secondary_dominant_;
};

/// @brief Two chords in sequence, so a resolution can land on a different one.
class TwoChordLookup : public IChordLookup {
 public:
  TwoChordLookup(int8_t first_degree, std::vector<int> first_tones, bool first_is_secondary,
                 int8_t second_degree, std::vector<int> second_tones, Tick boundary)
      : first_(first_degree, std::move(first_tones), first_is_secondary),
        second_(second_degree, std::move(second_tones), false),
        boundary_(boundary) {}

  int8_t getChordDegreeAt(Tick tick) const override { return at(tick).getChordDegreeAt(tick); }
  ChordTones getChordTonesAt(Tick tick) const override { return at(tick).getChordTonesAt(tick); }
  Tick getNextChordChangeTick(Tick /*after*/) const override { return boundary_; }
  bool isSecondaryDominantAt(Tick tick) const override {
    return at(tick).isSecondaryDominantAt(tick);
  }

 private:
  const FixedChordLookup& at(Tick tick) const { return tick < boundary_ ? first_ : second_; }

  FixedChordLookup first_;
  FixedChordLookup second_;
  Tick boundary_;
};

std::vector<int> toVector(const ChordTones& tones) {
  std::vector<int> result(tones.begin(), tones.end());
  std::sort(result.begin(), result.end());
  return result;
}

TEST(VocalChordToneIdentityTest, DiatonicTriadPassesThroughUnchanged) {
  FixedChordLookup harmony(0, {0, 4, 7}, false);
  EXPECT_EQ(toVector(melody::vocalChordTonesAt(harmony, 0)), (std::vector<int>{0, 4, 7}));
}

TEST(VocalChordToneIdentityTest, SecondaryDominantDropsItsChromaticThird) {
  // V/vi on the iii degree sounds E7 (E G# B D). The vocal stays diatonic, so
  // the raised third is unavailable and the line must retreat to root/5th/b7.
  FixedChordLookup harmony(2, {4, 8, 11, 2}, true);
  EXPECT_EQ(toVector(melody::vocalChordTonesAt(harmony, 0)), (std::vector<int>{2, 4, 11}));
}

TEST(VocalChordToneIdentityTest, SecondaryDominantDropsTheUnalteredThirdToo) {
  // When the lookup only knows the diatonic triad under a registered secondary
  // dominant, keeping its third would sound the minor quality against the major
  // one the accompaniment plays. Root and fifth are what both chords share.
  FixedChordLookup harmony(2, {4, 7, 11}, true);
  EXPECT_EQ(toVector(melody::vocalChordTonesAt(harmony, 0)), (std::vector<int>{4, 11}));
}

TEST(VocalChordToneIdentityTest, SecondaryDominantOnAMajorDegreeKeepsItsThird) {
  // V/IV is built on I, whose third is already major: the alteration is the
  // seventh, not the third. Dropping the third here would strip the chord of
  // its quality for no reason.
  FixedChordLookup registered(0, {0, 4, 7, 10}, true);
  EXPECT_EQ(toVector(melody::vocalChordTonesAt(registered, 0)), (std::vector<int>{0, 4, 7}));

  FixedChordLookup triad_only(0, {0, 4, 7}, true);
  EXPECT_EQ(toVector(melody::vocalChordTonesAt(triad_only, 0)), (std::vector<int>{0, 4, 7}));
}

TEST(VocalToneLegalityTest, AccentedNonChordToneResolvingDownIsAdmitted) {
  FixedChordLookup harmony(0, {0, 4, 7}, false);  // C major triad
  melody::MelodicNeighborhood n;
  n.prev_pitch = 67;  // G4
  n.next_pitch = 64;  // E4, a chord tone
  n.start = 0;        // bar downbeat: the accent an appoggiatura needs
  n.duration = TICK_QUARTER;
  n.next_start = TICK_QUARTER;

  EXPECT_EQ(melody::classifyVocalTone(harmony, 65, n), melody::ToneLegality::Appoggiatura)
      << "F over C resolving down to E on a downbeat is the ballad appoggiatura";
}

TEST(VocalToneLegalityTest, AccentedNonChordToneResolvingUpIsNotAdmitted) {
  // The mirror image of the figure above, and deliberately not licensed. An
  // ascending dissonance that is weak, short and approached by step is already
  // a neighbour tone, so admitting the rise here would add only the accented,
  // long and leap-approached ones. Doing that was measured across the corpus:
  // the melody kept more of its steps, but the chord-tone discipline the rest
  // of the arrangement stands on went with them -- a zero tension budget no
  // longer held, the chord track's suspensions stopped resolving, and the
  // guitar sounded avoid notes. Restricting the rise to the accented, short
  // figure that the name would imply changed which of those broke, not how
  // many.
  FixedChordLookup harmony(0, {0, 4, 7}, false);  // C major triad
  melody::MelodicNeighborhood n;
  n.prev_pitch = 67;  // G4, reached by leap
  n.next_pitch = 64;  // E4, a chord tone a step above
  n.start = 0;        // bar downbeat
  n.duration = TICK_QUARTER;
  n.next_start = TICK_QUARTER;

  EXPECT_EQ(melody::classifyVocalTone(harmony, 62, n), melody::ToneLegality::Illegal);
}

TEST(VocalToneLegalityTest, NonChordToneThatLeapsAwayIsRejected) {
  FixedChordLookup harmony(0, {0, 4, 7}, false);
  melody::MelodicNeighborhood n;
  n.prev_pitch = 67;
  n.next_pitch = 72;  // leaps up instead of resolving down
  n.start = 0;
  n.duration = TICK_QUARTER;
  n.next_start = TICK_QUARTER;

  EXPECT_EQ(melody::classifyVocalTone(harmony, 65, n), melody::ToneLegality::Illegal);
}

TEST(VocalToneLegalityTest, ChromaticNonChordToneIsRejectedEvenWhenItResolves) {
  FixedChordLookup harmony(0, {0, 4, 7}, false);
  melody::MelodicNeighborhood n;
  n.prev_pitch = 67;
  n.next_pitch = 64;
  n.start = 0;
  n.duration = TICK_QUARTER;
  n.next_start = TICK_QUARTER;

  EXPECT_EQ(melody::classifyVocalTone(harmony, 66, n), melody::ToneLegality::Illegal)
      << "F# is outside the internal key and nothing licenses it";
}

TEST(VocalToneLegalityTest, CollisionAvoidanceKeepsTheFigureTheDesignerAdmits) {
  // The seam: a downbeat appoggiatura the melody designer deliberately keeps
  // must survive the collision-avoidance pass, which runs afterwards over the
  // whole section. Snapping it here collapses the figure onto its own
  // resolution pitch and the tension disappears.
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);
  harmony.setChordTones({0, 4, 7});

  std::vector<NoteEvent> notes = {
      NoteEventTestHelper::create(TICKS_PER_BAR - TICK_QUARTER, TICK_QUARTER, 67, 90),
      NoteEventTestHelper::create(TICKS_PER_BAR, TICK_QUARTER, 65, 90),
      NoteEventTestHelper::create(TICKS_PER_BAR + TICK_QUARTER, TICK_QUARTER, 64, 90),
  };

  applyCollisionAvoidanceWithIntervalConstraint(notes, harmony, 55, 79, SectionType::A, 12);

  EXPECT_EQ(notes[1].note, 65) << "The appoggiatura was snapped onto a chord tone";
  EXPECT_EQ(notes[2].note, 64) << "The resolution must stay where it was";
}

// ============================================================================
// Section transition pickup
// ============================================================================

TEST(TransitionLeadingToneTest, PickupIsScreenedAgainstThePitchItActuallyUses) {
  // The pickup pitch is a chord tone near the tessitura centre. Screening the
  // insertion against a differently-derived candidate rejected pickups that the
  // chosen pitch reaches comfortably.
  MelodyDesigner designer;
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(4);
  harmony.setChordTones({7, 11, 2});  // V: G B D

  MelodyDesigner::SectionContext ctx;
  ctx.section_type = SectionType::A;  // standard leap allowance: 9 semitones
  ctx.section_start = 0;
  ctx.section_end = TICKS_PER_BAR;
  ctx.section_bars = 1;
  ctx.chord_degree = 4;
  ctx.key_offset = 0;
  ctx.vocal_low = 55;
  ctx.vocal_high = 84;
  ctx.max_leap_semitones = 12;
  ctx.tessitura = calculateTessitura(ctx.vocal_low, ctx.vocal_high);
  ctx.tessitura.center = 73;

  SectionTransition transition{SectionType::A, SectionType::Chorus, 0, 1.0f, 1, true};
  ctx.transition_to_next = &transition;

  // The last note ends right where the pickup starts, a whole step below the
  // chord tone the pickup will pick. tessitura.center - 1 is 11 semitones away
  // from it; the chord tone the pickup actually uses is 9, which is allowed.
  std::vector<NoteEvent> notes = {
      NoteEventTestHelper::create(0, TICKS_PER_BAR - TICK_SIXTEENTH, 62, 90),
  };

  designer.applyTransitionApproach(notes, ctx, harmony);

  ASSERT_EQ(notes.size(), 2u) << "The pickup was rejected on a pitch it never uses";
  EXPECT_EQ(notes.back().start_tick, ctx.section_end - TICKS_PER_BEAT / 4);
  EXPECT_LE(std::abs(static_cast<int>(notes.back().note) - 62), 9)
      << "The inserted pickup must itself respect the section leap allowance";
}

// ============================================================================
// Hook repetition counting
// ============================================================================

std::vector<NoteEvent> makeHookHead(uint8_t pitch) {
  std::vector<NoteEvent> notes;
  for (int i = 0; i < 8; ++i) {
    notes.push_back(NoteEventTestHelper::create(static_cast<Tick>(i) * TICK_EIGHTH, TICK_EIGHTH,
                                                static_cast<uint8_t>(pitch + (i % 3)), 90));
  }
  return notes;
}

TEST(HookRepetitionTest, ReplayedHooksAdvanceTheCounter) {
  // A repeated chorus is served from the phrase cache, so counting only
  // generated hooks left the counter stuck below every template threshold.
  MelodyDesigner designer;
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);
  harmony.setChordTones({0, 4, 7});
  std::mt19937 rng(1234);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::RunUpTarget);
  ASSERT_EQ(tmpl.betrayal_threshold, 3) << "This test needs a template that varies on the 3rd";

  EXPECT_EQ(designer.hookRepetitionCount(), 0);
  for (uint8_t expected = 1; expected <= 3; ++expected) {
    std::vector<NoteEvent> replayed = makeHookHead(67);
    designer.replayHookOccurrence(tmpl, replayed, harmony, rng, 55, 79);
    EXPECT_EQ(designer.hookRepetitionCount(), expected);
  }
}

TEST(HookRepetitionTest, ThirdOccurrenceVariesWhileTheFirstTwoRepeatVerbatim) {
  MelodyDesigner designer;
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);
  harmony.setChordTones({0, 4, 7});
  std::mt19937 rng(20260903);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::RunUpTarget);

  const std::vector<NoteEvent> original = makeHookHead(67);
  std::vector<std::vector<NoteEvent>> heads;
  for (int occurrence = 0; occurrence < 3; ++occurrence) {
    std::vector<NoteEvent> replayed = original;
    designer.replayHookOccurrence(tmpl, replayed, harmony, rng, 55, 79);
    heads.push_back(std::move(replayed));
  }

  auto sameAsOriginal = [&original](const std::vector<NoteEvent>& head) {
    if (head.size() != original.size()) return false;
    for (size_t i = 0; i < head.size(); ++i) {
      if (head[i].note != original[i].note || head[i].duration != original[i].duration) {
        return false;
      }
    }
    return true;
  };

  EXPECT_TRUE(sameAsOriginal(heads[0])) << "The first hook must repeat verbatim";
  EXPECT_TRUE(sameAsOriginal(heads[1])) << "The second hook must repeat verbatim";
  EXPECT_FALSE(sameAsOriginal(heads[2]))
      << "The third hook must depart from the first two: that departure is the "
         "tension the threshold exists to create";
}

TEST(HookRepetitionTest, ZeroThresholdNeverVaries) {
  MelodyDesigner designer;
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);
  harmony.setChordTones({0, 4, 7});
  std::mt19937 rng(7);

  MelodyTemplate exact = getTemplate(MelodyTemplateId::RunUpTarget);
  exact.betrayal_threshold = 0;

  const std::vector<NoteEvent> original = makeHookHead(67);
  for (int occurrence = 0; occurrence < 6; ++occurrence) {
    std::vector<NoteEvent> replayed = original;
    EXPECT_FALSE(designer.replayHookOccurrence(exact, replayed, harmony, rng, 55, 79));
    for (size_t i = 0; i < replayed.size(); ++i) {
      EXPECT_EQ(replayed[i].note, original[i].note);
    }
  }
  EXPECT_EQ(designer.hookRepetitionCount(), 6)
      << "Occurrences are still counted; only the variation is disabled";
}

TEST(VocalToneLegalityTest, NoFigureLicensesTheUnalteredThirdUnderASecondaryDominant) {
  melody::MelodicNeighborhood n;
  n.prev_pitch = 74;  // D5
  n.next_pitch = 71;  // B4
  n.start = 0;
  n.duration = TICK_QUARTER;
  n.next_start = TICK_QUARTER;

  // A7 (V/ii, built on vi) into V. The accompaniment plays C#, so a vocal C is
  // a cross relation -- yet it steps down onto B, a chord tone of the V it
  // resolves to, which is exactly the shape of a textbook appoggiatura. The
  // resolution must not be allowed to license this particular pitch.
  TwoChordLookup secondary(5, {9, 1, 4, 7}, true, 4, {7, 11, 2}, TICK_QUARTER);
  EXPECT_EQ(melody::classifyVocalTone(secondary, 72, n), melody::ToneLegality::Illegal)
      << "C has no licence while the C# of the secondary dominant sounds";

  // The identical figure with no secondary dominant sounding is admitted.
  TwoChordLookup plain(4, {7, 11, 2}, false, 4, {7, 11, 2}, TICK_QUARTER);
  EXPECT_EQ(melody::classifyVocalTone(plain, 72, n), melody::ToneLegality::Appoggiatura)
      << "C over V steps down to B, a chord tone: an ordinary appoggiatura";
}

// ============================================================================
// Hook emphasis
// ============================================================================

std::vector<NoteEvent> makeHookWindow() {
  return {
      NoteEventTestHelper::create(0, TICK_EIGHTH, 72, 90),
      NoteEventTestHelper::create(TICK_EIGHTH, TICK_EIGHTH, 74, 90),
      NoteEventTestHelper::create(TICK_QUARTER, TICK_EIGHTH, 76, 90),
      NoteEventTestHelper::create(TICKS_PER_BEAT * 3, TICK_EIGHTH, 72, 90),
  };
}

TEST(HookIntensityTest, MaximumEmphasisReachesTheNotes) {
  // The blueprint that asks for Maximum used to get nothing at all: the value
  // was missing from the emphasis ladder, so the call computed its window and
  // then applied a 1.0x duration and a +0 velocity.
  std::vector<NoteEvent> notes = makeHookWindow();
  const std::vector<NoteEvent> before = notes;

  applyHookIntensity(notes, SectionType::Chorus, HookIntensity::Maximum, 0);

  bool changed = false;
  for (size_t i = 0; i < notes.size(); ++i) {
    if (notes[i].duration != before[i].duration || notes[i].velocity != before[i].velocity) {
      changed = true;
    }
  }
  EXPECT_TRUE(changed) << "Maximum must emphasise the hook, not pass through unchanged";
  EXPECT_GT(notes[0].duration, before[0].duration);
  EXPECT_GT(notes[0].velocity, before[0].velocity);
}

TEST(HookIntensityTest, MaximumIsAtLeastAsStrongAsStrong) {
  std::vector<NoteEvent> strong = makeHookWindow();
  std::vector<NoteEvent> maximum = makeHookWindow();

  applyHookIntensity(strong, SectionType::Chorus, HookIntensity::Strong, 0);
  applyHookIntensity(maximum, SectionType::Chorus, HookIntensity::Maximum, 0);

  ASSERT_EQ(strong.size(), maximum.size());
  for (size_t i = 0; i < strong.size(); ++i) {
    EXPECT_GE(maximum[i].duration, strong[i].duration) << "note " << i;
    EXPECT_GE(maximum[i].velocity, strong[i].velocity) << "note " << i;
  }
}

TEST(HookIntensityTest, MaximumReachesSectionsThatAreNotHookPoints) {
  // Strong and above are the levels that emphasise every section rather than
  // only the Chorus and Pre-chorus hook points.
  std::vector<NoteEvent> notes = makeHookWindow();
  const Tick before_duration = notes[0].duration;

  applyHookIntensity(notes, SectionType::A, HookIntensity::Maximum, 0);
  EXPECT_GT(notes[0].duration, before_duration);
}

TEST(HookIntensityTest, OffLeavesTheNotesAlone) {
  std::vector<NoteEvent> notes = makeHookWindow();
  const std::vector<NoteEvent> before = notes;
  applyHookIntensity(notes, SectionType::Chorus, HookIntensity::Off, 0);
  for (size_t i = 0; i < notes.size(); ++i) {
    EXPECT_EQ(notes[i].duration, before[i].duration);
    EXPECT_EQ(notes[i].velocity, before[i].velocity);
  }
}

// ============================================================================
// Section ceiling enforcement
// ============================================================================

/// @brief Harmony stub that rejects one specific pitch and accepts the rest.
class OnePitchBlockedHarmony : public test::StubHarmonyContext {
 public:
  explicit OnePitchBlockedHarmony(uint8_t blocked) : blocked_(blocked) {}

  bool isConsonantWithOtherTracks(uint8_t pitch, Tick /*start*/, Tick /*duration*/,
                                  TrackRole /*exclude*/,
                                  bool /*is_weak_beat*/ = false) const override {
    return pitch != blocked_;
  }

 private:
  uint8_t blocked_;
};

TEST(SectionCeilingTest, ClashAtTheCeilingStepsDownRatherThanDroppingAnOctave) {
  // The note has to come under the ceiling, and the pitch at the ceiling
  // clashes. Dropping to the octave below would trade a ceiling breach for a
  // 12-semitone hole -- a leap no section's bound allows -- and the note after
  // it would have to climb all the way back.
  OnePitchBlockedHarmony harmony(79);
  harmony.setChordDegree(0);
  harmony.setChordTones({0, 4, 7});

  std::vector<NoteEvent> notes = {NoteEventTestHelper::create(0, TICK_QUARTER, 84, 90)};
  enforceSectionCeiling(notes, harmony, 55, 79);

  EXPECT_LE(notes[0].note, 79) << "The ceiling must be respected";
  EXPECT_GE(notes[0].note, 79 - 7) << "The replacement must stay within a 5th of the ceiling, "
                                      "not fall to the octave below";
  EXPECT_NE(notes[0].note, 79) << "The blocked pitch must not be kept when a consonant one exists";
  EXPECT_TRUE(isScaleTone(getPitchClass(notes[0].note))) << "The vocal stays diatonic";
}

TEST(SectionCeilingTest, NotesUnderTheCeilingAreLeftAlone) {
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);
  harmony.setChordTones({0, 4, 7});

  std::vector<NoteEvent> notes = {NoteEventTestHelper::create(0, TICK_QUARTER, 72, 90)};
  enforceSectionCeiling(notes, harmony, 55, 79);
  EXPECT_EQ(notes[0].note, 72);
}

// ============================================================================
// Leap bound resolution
// ============================================================================

TEST(LeapBoundTest, PrefersAChordToneInsideTheBound) {
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);
  harmony.setChordTones({0, 4, 7});

  melody::MelodicNeighborhood n;
  n.start = 0;
  n.duration = TICK_QUARTER;
  n.prev_pitch = 60;  // C4
  n.next_pitch = 64;  // E4
  n.next_start = TICK_QUARTER;

  // The note sits an octave above its neighbour; the bound allows a fifth.
  const int fixed = melody::resolveLeapWithinBound(harmony, n, 72, 7, 55, 79);
  EXPECT_LE(std::abs(fixed - 60), 7) << "The bound must actually close";
  EXPECT_EQ(fixed % 12, 7) << "G4 is the chord tone nearest the pitch it had";
}

TEST(LeapBoundTest, FallsBackToALegalNonChordToneWhenNoChordToneFits) {
  // The chord's tones are all outside the window the bound leaves open, so a
  // chord-tone-only search would give up and leave the leap open. A figure the
  // shared rule admits is a better answer than an unsingable interval.
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);
  harmony.setChordTones({0});  // C only: 60 and 72 are the reachable octaves

  melody::MelodicNeighborhood n;
  n.start = TICK_EIGHTH;  // weak position, so a passing tone is admissible
  n.duration = TICK_EIGHTH;
  n.prev_pitch = 65;
  n.next_pitch = 67;
  n.next_start = TICK_EIGHTH * 2;

  const int fixed = melody::resolveLeapWithinBound(harmony, n, 79, 2, 55, 79);
  EXPECT_LE(std::abs(fixed - 65), 2) << "The bound must actually close";
  EXPECT_NE(fixed % 12, 0) << "No C is reachable inside the bound";
  EXPECT_NE(melody::classifyVocalTone(harmony, fixed, n), melody::ToneLegality::Illegal)
      << "The replacement still has to be a figure the shared rule admits";
}

TEST(LeapBoundTest, KeepsThePitchWhenNothingInsideTheBoundIsAdmissible) {
  // Every candidate collides with another track. An open leap is the lesser
  // problem: a clashing pitch breaks an invariant that outranks singability.
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(false);
  harmony.setChordDegree(0);
  harmony.setChordTones({0, 4, 7});

  melody::MelodicNeighborhood n;
  n.start = 0;
  n.duration = TICK_QUARTER;
  n.prev_pitch = 60;
  n.next_pitch = 64;
  n.next_start = TICK_QUARTER;

  EXPECT_EQ(melody::resolveLeapWithinBound(harmony, n, 79, 7, 55, 79), 79);
}

// ============================================================================
// Hold-burst marking
// ============================================================================

TEST(HoldBurstEntryTest, DensitySurgeIsAppliedExactlyOnce) {
  PlannedPhrase phrase;
  phrase.density_modifier = 1.0f;

  ASSERT_TRUE(PhrasePlanner::markHoldBurstEntry(phrase, SectionType::Chorus));
  EXPECT_TRUE(phrase.is_hold_burst_entry);
  const float after_first = phrase.density_modifier;
  const uint8_t target_after_first = phrase.target_note_count;
  EXPECT_GT(after_first, 1.0f) << "A burst phrase must actually get denser";

  EXPECT_FALSE(PhrasePlanner::markHoldBurstEntry(phrase, SectionType::Chorus))
      << "A phrase that is both the section climax and the post-hold entry would "
         "otherwise be boosted twice";
  EXPECT_FLOAT_EQ(phrase.density_modifier, after_first);
  EXPECT_EQ(phrase.target_note_count, target_after_first);
}

#ifdef MIDISKETCH_NOTE_PROVENANCE

/// A chord lookup that names a different degree in each bar, so a phrase moved
/// by a whole number of bars lands on a chord its source position did not name.
class BarWiseChordLookup : public IChordLookup {
 public:
  int8_t getChordDegreeAt(Tick tick) const override {
    static constexpr int8_t kDegrees[] = {0, 3, 4, 5};
    return kDegrees[(tick / TICKS_PER_BAR) % 4];
  }
  ChordTones getChordTonesAt(Tick tick) const override {
    return getChordTones(getChordDegreeAt(tick));
  }
  Tick getNextChordChangeTick(Tick after) const override {
    return ((after / TICKS_PER_BAR) + 1) * TICKS_PER_BAR;
  }
};

TEST(ShiftTimingTest, AReusedPhraseRecordsTheChordItNowSoundsOver) {
  // The recorded degree is the one field of a note's history that names a
  // position, and it is how a voice written against harmony that changed
  // underneath it is told apart from one that was not. A phrase replayed in a
  // later section carries that field to a bar it was never written for unless
  // the shift reads it again.
  BarWiseChordLookup harmony;
  constexpr Tick kStart = TICK_QUARTER;
  constexpr Tick kOffset = TICKS_PER_BAR * 2;

  NoteEvent note = NoteEventTestHelper::create(kStart, TICK_QUARTER, 60, 80);
  note.prov_chord_degree = harmony.getChordDegreeAt(kStart);
  note.prov_lookup_tick = kStart;
  note.prov_source = static_cast<uint8_t>(NoteSource::MelodyPhrase);

  // A corpus where both positions name the same chord could not tell the two
  // behaviours apart.
  ASSERT_NE(harmony.getChordDegreeAt(kStart), harmony.getChordDegreeAt(kStart + kOffset));

  const std::vector<NoteEvent> shifted = shiftTiming({note}, harmony, kOffset);

  ASSERT_EQ(shifted.size(), 1u);
  EXPECT_EQ(shifted[0].start_tick, kStart + kOffset);
  EXPECT_EQ(shifted[0].prov_lookup_tick, kStart + kOffset);
  EXPECT_EQ(shifted[0].prov_chord_degree, harmony.getChordDegreeAt(kStart + kOffset));
}

TEST(ShiftTimingTest, ATrackWithoutChordContextKeepsItsUnsetDegree) {
  // Drums and SE record -1 to say they have no chord context. Reading a degree
  // for them would replace "this note answers to no chord" with a chord it does
  // not answer to.
  BarWiseChordLookup harmony;
  NoteEvent note = NoteEventTestHelper::create(0, TICK_QUARTER, 36, 100);
  note.prov_chord_degree = -1;

  const std::vector<NoteEvent> shifted = shiftTiming({note}, harmony, TICKS_PER_BAR);

  ASSERT_EQ(shifted.size(), 1u);
  EXPECT_EQ(shifted[0].prov_chord_degree, -1);
}

#endif  // MIDISKETCH_NOTE_PROVENANCE

}  // namespace
}  // namespace midisketch
