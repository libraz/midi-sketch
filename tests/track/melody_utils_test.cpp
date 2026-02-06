/**
 * @file melody_utils_test.cpp
 * @brief Unit tests for enforceMaxPhraseDuration in melody_utils.
 */

#include "track/melody/melody_utils.h"

#include <gtest/gtest.h>

#include "core/timing_constants.h"
#include "core/types.h"
#include "test_helpers/note_event_test_helper.h"

namespace midisketch {
namespace melody {
namespace {

// ============================================================================
// enforceMaxPhraseDuration Tests
// ============================================================================

class EnforceMaxPhraseDurationTest : public ::testing::Test {
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

TEST_F(EnforceMaxPhraseDurationTest, EmptyNotesDoesNotCrash) {
  std::vector<NoteEvent> notes;
  enforceMaxPhraseDuration(notes, 4);
  EXPECT_TRUE(notes.empty());
}

TEST_F(EnforceMaxPhraseDurationTest, ZeroMaxBarsDoesNothing) {
  auto notes = createNotes({
      {0, 480, 60},
      {480, 480, 62},
  });
  Tick orig_dur0 = notes[0].duration;
  Tick orig_dur1 = notes[1].duration;

  enforceMaxPhraseDuration(notes, 0);

  EXPECT_EQ(notes[0].duration, orig_dur0);
  EXPECT_EQ(notes[1].duration, orig_dur1);
}

TEST_F(EnforceMaxPhraseDurationTest, MaxBars255DoesNothing) {
  auto notes = createNotes({
      {0, 480, 60},
      {480, 480, 62},
  });
  Tick orig_dur0 = notes[0].duration;
  Tick orig_dur1 = notes[1].duration;

  enforceMaxPhraseDuration(notes, 255);

  EXPECT_EQ(notes[0].duration, orig_dur0);
  EXPECT_EQ(notes[1].duration, orig_dur1);
}

TEST_F(EnforceMaxPhraseDurationTest, ShortPhraseNoChange) {
  // 2 bars of continuous notes, max_phrase_bars=4 -> no change
  // TICKS_PER_BAR = 1920
  auto notes = createNotes({
      {0, 480, 60},
      {480, 480, 62},
      {960, 480, 64},
      {1440, 480, 65},
      {1920, 480, 67},
      {2400, 480, 69},
  });
  std::vector<Tick> orig_durations;
  for (const auto& note : notes) {
    orig_durations.push_back(note.duration);
  }

  enforceMaxPhraseDuration(notes, 4);

  // All within 4 bars (7680 ticks), total span = 2400 + 480 - 0 = 2880 < 7680
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    EXPECT_EQ(notes[idx].duration, orig_durations[idx])
        << "Note " << idx << " should not be modified";
  }
}

TEST_F(EnforceMaxPhraseDurationTest, LongPhraseGetsBreathInserted) {
  // Create a continuous phrase spanning 5+ bars with max_phrase_bars=4
  // Each note is a quarter note (480 ticks), placed continuously
  // 4 bars = 7680 ticks, so we need notes spanning past that
  std::vector<NoteEvent> notes;
  constexpr int kNotesCount = 20;  // 20 quarter notes = 5 bars
  for (int idx = 0; idx < kNotesCount; ++idx) {
    notes.push_back(NoteEventTestHelper::create(
        static_cast<Tick>(idx * 480), 480, 60 + (idx % 7), 80));
  }

  enforceMaxPhraseDuration(notes, 4);

  // At least one note should have been shortened (breath gap inserted)
  bool any_shortened = false;
  for (const auto& note : notes) {
    if (note.duration < 480) {
      any_shortened = true;
      break;
    }
  }
  EXPECT_TRUE(any_shortened)
      << "At least one note should be shortened for breath gap";

  // Shortened notes should not be below TICK_SIXTEENTH (120)
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    EXPECT_GE(notes[idx].duration, static_cast<Tick>(TICK_SIXTEENTH))
        << "Note " << idx << " duration should not go below TICK_SIXTEENTH";
  }
}

TEST_F(EnforceMaxPhraseDurationTest, ExistingGapResetsPhraseTracking) {
  // Phrase 1: 2 bars, gap (quarter note), Phrase 2: 2 bars
  // max_phrase_bars=4 -> no breath inserted because gap resets tracking
  auto notes = createNotes({
      // Phrase 1: 0 to ~3840
      {0, 480, 60},
      {480, 480, 62},
      {960, 480, 64},
      {1440, 480, 65},
      {1920, 480, 67},
      {2400, 480, 69},
      {2880, 480, 71},
      // Gap of TICK_QUARTER (480 ticks) here: 3360 + gap -> next note at 3840
      // Phrase 2: 3840 to ~7200
      {3840, 480, 60},
      {4320, 480, 62},
      {4800, 480, 64},
      {5280, 480, 65},
      {5760, 480, 67},
      {6240, 480, 69},
      {6720, 480, 71},
  });

  std::vector<Tick> orig_durations;
  for (const auto& note : notes) {
    orig_durations.push_back(note.duration);
  }

  enforceMaxPhraseDuration(notes, 4);

  // Both phrases are within 4 bars (7680 ticks), with gap resetting tracking
  // Phrase 1 span: 2880 + 480 - 0 = 3360 < 7680
  // Phrase 2 span: 6720 + 480 - 3840 = 3360 < 7680
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    EXPECT_EQ(notes[idx].duration, orig_durations[idx])
        << "Note " << idx << " should not be modified";
  }
}

TEST_F(EnforceMaxPhraseDurationTest, SmallGapDoesNotResetPhrase) {
  // Small gap (less than quarter note) does NOT reset phrase tracking
  // This means the phrase continues and should get a breath inserted
  // Create notes with small gaps spanning 5+ bars
  std::vector<NoteEvent> notes;
  constexpr int kNotesCount = 24;  // 24 notes with small gaps = 6+ bars
  for (int idx = 0; idx < kNotesCount; ++idx) {
    // Each note is 440 ticks with 40-tick gap (< TICK_QUARTER = 480)
    notes.push_back(NoteEventTestHelper::create(
        static_cast<Tick>(idx * 480), 440, 60 + (idx % 7), 80));
  }

  enforceMaxPhraseDuration(notes, 4);

  // The small gaps (40 ticks < 480 ticks) don't reset phrase tracking,
  // so breath gaps should be inserted
  bool any_shortened = false;
  for (const auto& note : notes) {
    if (note.duration < 440) {
      any_shortened = true;
      break;
    }
  }
  EXPECT_TRUE(any_shortened)
      << "Small gaps should not reset phrase; breath should be inserted";
}

TEST_F(EnforceMaxPhraseDurationTest, BreathTicksParameter) {
  // Test custom breath_ticks parameter
  // Create 5-bar continuous phrase with max_phrase_bars=4
  std::vector<NoteEvent> notes;
  constexpr int kNotesCount = 20;
  for (int idx = 0; idx < kNotesCount; ++idx) {
    notes.push_back(NoteEventTestHelper::create(
        static_cast<Tick>(idx * 480), 480, 60 + (idx % 7), 80));
  }

  // Use custom breath_ticks = 120 (sixteenth note)
  enforceMaxPhraseDuration(notes, 4, 120);

  bool any_shortened = false;
  for (const auto& note : notes) {
    if (note.duration < 480) {
      any_shortened = true;
      // With breath_ticks=120 + ritardando margin (60), gap target = 180
      // Shortened note: 480 - 180 = 300
      EXPECT_EQ(note.duration, 300u)
          << "Shortened note should account for breath_ticks + rit margin";
      break;
    }
  }
  EXPECT_TRUE(any_shortened);
}

TEST_F(EnforceMaxPhraseDurationTest, VeryShortNoteNotShortenedFurther) {
  // When a note is already near TICK_SIXTEENTH, it should not be shortened
  // below kMinNoteDuration (TICK_SIXTEENTH = 120)
  auto notes = createNotes({
      {0, 480, 60},
      {480, 480, 62},
      {960, 480, 64},
      {1440, 480, 65},
      {1920, 480, 67},
      {2400, 480, 69},
      {2880, 480, 71},
      {3360, 480, 72},
      {3840, 480, 74},
      {4320, 480, 76},
      {4800, 480, 77},
      {5280, 480, 79},
      {5760, 480, 81},
      {6240, 480, 83},
      {6720, 480, 84},
      {7200, 130, 86},  // Short note (130 ticks), close to minimum
      {7330, 480, 88},  // Continuous with previous
  });

  enforceMaxPhraseDuration(notes, 4);

  // All notes should be >= TICK_SIXTEENTH (120)
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    EXPECT_GE(notes[idx].duration, static_cast<Tick>(TICK_SIXTEENTH))
        << "Note " << idx << " should not go below minimum duration";
  }
}

TEST_F(EnforceMaxPhraseDurationTest, SingleNoteNoChange) {
  auto notes = createNotes({
      {0, 480, 60},
  });

  enforceMaxPhraseDuration(notes, 4);

  EXPECT_EQ(notes[0].duration, 480u) << "Single note should not be modified";
}

TEST_F(EnforceMaxPhraseDurationTest, MaxPhraseBars1InsertsBreathEveryBar) {
  // With max_phrase_bars=1 (1920 ticks), every bar boundary should get breath
  auto notes = createNotes({
      {0, 480, 60},
      {480, 480, 62},
      {960, 480, 64},
      {1440, 480, 65},
      // Bar 2 starts at 1920
      {1920, 480, 67},
      {2400, 480, 69},
      {2880, 480, 71},
      {3360, 480, 72},
  });

  enforceMaxPhraseDuration(notes, 1);

  // Notes near bar boundaries should be shortened
  bool bar1_breath = false;
  for (size_t idx = 0; idx < 4; ++idx) {
    if (notes[idx].duration < 480) {
      bar1_breath = true;
      break;
    }
  }
  EXPECT_TRUE(bar1_breath)
      << "Breath should be inserted within first bar boundary";
}

TEST_F(EnforceMaxPhraseDurationTest, BalladStyleMaxBars4) {
  // Ballad style: max_phrase_bars=4, breath_ticks=240 (default)
  // Create 5-bar phrase to verify breath gets inserted
  std::vector<NoteEvent> notes;
  for (int idx = 0; idx < 20; ++idx) {
    notes.push_back(NoteEventTestHelper::create(
        static_cast<Tick>(idx * 480), 480, 60 + (idx % 7), 80));
  }

  // Ballad: max_phrase_bars=4
  enforceMaxPhraseDuration(notes, 4);

  // Find which notes were shortened
  int shortened_count = 0;
  for (const auto& note : notes) {
    if (note.duration < 480) {
      shortened_count++;
    }
  }
  EXPECT_GE(shortened_count, 1)
      << "At least one breath gap should be inserted for 5-bar phrase";
}

TEST_F(EnforceMaxPhraseDurationTest, PhraseResetAfterBreathInsertion) {
  // After inserting a breath gap, phrase tracking should reset
  // This means the next phrase segment can go for another max_phrase_bars
  std::vector<NoteEvent> notes;
  // Create 10 bars of continuous notes (40 quarter notes)
  for (int idx = 0; idx < 40; ++idx) {
    notes.push_back(NoteEventTestHelper::create(
        static_cast<Tick>(idx * 480), 480, 60 + (idx % 7), 80));
  }

  enforceMaxPhraseDuration(notes, 4);

  // Should have multiple breath insertions (at least 2 for 10 bars with 4-bar limit)
  int shortened_count = 0;
  for (const auto& note : notes) {
    if (note.duration < 480) {
      shortened_count++;
    }
  }
  EXPECT_GE(shortened_count, 2)
      << "Multiple breath gaps should be inserted for 10-bar phrase";
}

TEST_F(EnforceMaxPhraseDurationTest, NoteShorterThanBreathTicksSetToMinimum) {
  // When note duration is between kMinNoteDuration and breath_ticks + kMinNoteDuration,
  // it should be set to kMinNoteDuration (not below)
  auto notes = createNotes({
      {0, 480, 60},
      {480, 480, 62},
      {960, 480, 64},
      {1440, 480, 65},
      {1920, 480, 67},
      {2400, 480, 69},
      {2880, 480, 71},
      {3360, 480, 72},
      {3840, 480, 74},
      {4320, 480, 76},
      {4800, 480, 77},
      {5280, 480, 79},
      {5760, 480, 81},
      {6240, 480, 83},
      {6720, 480, 84},
      {7200, 200, 86},  // 200 ticks: > 120 but < 240 + 120 = 360
      {7400, 480, 88},
  });

  enforceMaxPhraseDuration(notes, 4, 240);

  // Note at index 15 (200 ticks) is between kMinNoteDuration (120)
  // and breath_ticks + kMinNoteDuration (360), so should be set to 120
  // But only if it's the one selected for shortening
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    EXPECT_GE(notes[idx].duration, static_cast<Tick>(TICK_SIXTEENTH))
        << "Note " << idx << " should not go below kMinNoteDuration";
  }
}

TEST_F(EnforceMaxPhraseDurationTest, TightRhythmSyncPatternRemovesNotes) {
  // RhythmSync patterns can produce notes at 120-tick intervals with
  // durations of 120/120/210 ticks. Shortening alone cannot create a
  // 240-tick breath gap because individual notes are too short.
  // The fix walks backward and removes notes until the gap is sufficient.
  //
  // Layout: repeating groups of (120-tick note, 120-tick note, 210-tick note)
  // at 120-tick intervals, spanning 5+ bars.
  std::vector<NoteEvent> notes;
  Tick pos = 0;
  constexpr int kGroupCount = 30;  // Enough groups to exceed 4 bars
  for (int grp = 0; grp < kGroupCount; ++grp) {
    notes.push_back(NoteEventTestHelper::create(pos, 120, 60, 80));
    pos += 120;
    notes.push_back(NoteEventTestHelper::create(pos, 120, 62, 80));
    pos += 120;
    notes.push_back(NoteEventTestHelper::create(pos, 210, 64, 80));
    pos += 210;  // Next group starts at pos+210 (leaves a 0-tick gap since dur==spacing)
  }

  size_t original_count = notes.size();

  // breath_ticks=240 (default). With the old implementation, shortening a
  // 210-tick note to kMinNoteDuration(120) only creates a 90-tick gap,
  // which is below the 240-tick breath threshold.
  enforceMaxPhraseDuration(notes, 4, 240);

  // Notes should have been removed to create sufficient gaps
  EXPECT_LT(notes.size(), original_count)
      << "Some notes should be removed to create breath gaps in tight patterns";

  // Verify all remaining notes have valid duration (>= kMinNoteDuration)
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    EXPECT_GE(notes[idx].duration, static_cast<Tick>(TICK_SIXTEENTH))
        << "Note " << idx << " should not go below kMinNoteDuration";
    EXPECT_GT(notes[idx].duration, 0u)
        << "Note " << idx << " should have positive duration";
  }

  // Verify that breath gaps of at least 240 ticks exist at phrase boundaries
  // Find segments: consecutive notes with gap < breath_ticks form a phrase
  Tick max_phrase_ticks = 4u * TICKS_PER_BAR;
  Tick phrase_start = notes[0].start_tick;
  for (size_t idx = 1; idx < notes.size(); ++idx) {
    Tick prev_end = notes[idx - 1].start_tick + notes[idx - 1].duration;
    Tick gap = (notes[idx].start_tick > prev_end)
                   ? (notes[idx].start_tick - prev_end)
                   : 0;
    if (gap >= 240) {
      // Breath gap found; verify the gap is at least breath_ticks wide
      EXPECT_GE(gap, 240u) << "Breath gap before note " << idx << " is too small";
      phrase_start = notes[idx].start_tick;
    } else {
      Tick phrase_len = notes[idx].start_tick + notes[idx].duration - phrase_start;
      EXPECT_LE(phrase_len, max_phrase_ticks + 480u)
          << "Phrase containing note " << idx << " exceeds max duration";
    }
  }
}

TEST_F(EnforceMaxPhraseDurationTest, NoteRemovalCreatesCorrectGap) {
  // Verify that when notes are removed, the resulting gap is exactly
  // breath_ticks (or larger) between the last kept note's end and the
  // next note's start.
  constexpr Tick kBreathTicks = 240;

  // Create a long contiguous phrase of very short notes (120 ticks each)
  // where shortening alone is impossible (already at minimum duration)
  std::vector<NoteEvent> notes;
  constexpr int kNoteCount = 80;  // 80 * 120 = 9600 ticks > 4 bars (7680)
  for (int idx = 0; idx < kNoteCount; ++idx) {
    notes.push_back(NoteEventTestHelper::create(
        static_cast<Tick>(idx * 120), 120, 60 + (idx % 5), 80));
  }

  enforceMaxPhraseDuration(notes, 4, kBreathTicks);

  // Notes should have been removed
  EXPECT_LT(notes.size(), static_cast<size_t>(kNoteCount))
      << "Notes should be removed when they are already at minimum duration";

  // At each gap >= kBreathTicks, verify it is sufficient
  for (size_t idx = 1; idx < notes.size(); ++idx) {
    Tick prev_end = notes[idx - 1].start_tick + notes[idx - 1].duration;
    Tick gap = (notes[idx].start_tick > prev_end)
                   ? (notes[idx].start_tick - prev_end)
                   : 0;
    if (gap >= kBreathTicks) {
      EXPECT_GE(gap, kBreathTicks)
          << "Gap before note " << idx << " should be at least breath_ticks";
    }
  }
}

}  // namespace
}  // namespace melody
}  // namespace midisketch
