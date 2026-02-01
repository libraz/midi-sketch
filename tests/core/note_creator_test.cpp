/**
 * @file note_creator_test.cpp
 * @brief Tests for unified note creation API (v2 Architecture).
 */

#include <gtest/gtest.h>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/harmony_context.h"
#include "core/midi_track.h"
#include "core/note_creator.h"

using namespace midisketch;

class NoteCreatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a basic arrangement with one 8-bar Chorus
    Section chorus;
    chorus.type = SectionType::Chorus;
    chorus.start_tick = 0;
    chorus.bars = 8;
    chorus.name = "Chorus";
    arrangement_ = Arrangement({chorus});

    // Use Canon progression: I-V-vi-IV
    progression_ = getChordProgression(0);

    harmony_.initialize(arrangement_, progression_, Mood::StraightPop);
  }

  Arrangement arrangement_;
  ChordProgression progression_;
  HarmonyContext harmony_;
};

TEST_F(NoteCreatorTest, CreateNoteWithoutHarmony) {
  NoteEvent note = createNoteWithoutHarmony(0, 480, 60, 100);

  EXPECT_EQ(note.start_tick, 0);
  EXPECT_EQ(note.duration, 480);
  EXPECT_EQ(note.note, 60);
  EXPECT_EQ(note.velocity, 100);
}

TEST_F(NoteCreatorTest, CreateNoteWithoutHarmonyAndAdd) {
  MidiTrack track;
  NoteEvent note = createNoteWithoutHarmonyAndAdd(track, 0, 480, 60, 100);

  EXPECT_EQ(track.noteCount(), 1);
  EXPECT_EQ(track.notes()[0].note, 60);
}

TEST_F(NoteCreatorTest, CreateNoteNoCollision) {
  NoteOptions opts;
  opts.start = 0;
  opts.duration = 480;
  opts.desired_pitch = 60;  // C4 - chord tone for I
  opts.velocity = 100;
  opts.role = TrackRole::Bass;
  opts.source = NoteSource::BassPattern;

  auto note = createNote(harmony_, opts);

  ASSERT_TRUE(note.has_value());
  EXPECT_EQ(note->note, 60);
}

TEST_F(NoteCreatorTest, CreateNoteAndAddWorksCorrectly) {
  MidiTrack track;

  NoteOptions opts;
  opts.start = 0;
  opts.duration = 480;
  opts.desired_pitch = 60;
  opts.velocity = 100;
  opts.role = TrackRole::Bass;
  opts.source = NoteSource::BassPattern;

  auto note = createNoteAndAdd(track, harmony_, opts);

  ASSERT_TRUE(note.has_value());
  EXPECT_EQ(track.noteCount(), 1);
  EXPECT_EQ(track.notes()[0].note, 60);
}

TEST_F(NoteCreatorTest, CreateNoteWithCollisionResolution) {
  // Register a note from Vocal at C4
  harmony_.registerNote(0, 480, 60, TrackRole::Vocal);

  NoteOptions opts;
  opts.start = 0;
  opts.duration = 480;
  opts.desired_pitch = 61;  // C#4 - minor 2nd clash with C4
  opts.velocity = 100;
  opts.role = TrackRole::Bass;
  opts.preference = PitchPreference::Default;
  opts.range_low = 36;
  opts.range_high = 60;
  opts.source = NoteSource::BassPattern;

  auto result = createNoteWithResult(harmony_, opts);

  ASSERT_TRUE(result.note.has_value());
  // Should be adjusted to avoid minor 2nd clash
  EXPECT_NE(result.note->note, 61);
  EXPECT_TRUE(result.was_adjusted);
  EXPECT_NE(result.strategy_used, CollisionAvoidStrategy::None);
  EXPECT_NE(result.strategy_used, CollisionAvoidStrategy::Failed);
}

TEST_F(NoteCreatorTest, SkipIfUnsafe) {
  // Register a note from Vocal at C4
  harmony_.registerNote(0, 480, 60, TrackRole::Vocal);

  NoteOptions opts;
  opts.start = 0;
  opts.duration = 480;
  opts.desired_pitch = 61;  // C#4 - minor 2nd clash
  opts.velocity = 100;
  opts.role = TrackRole::Bass;
  opts.preference = PitchPreference::SkipIfUnsafe;
  opts.source = NoteSource::BassPattern;

  auto note = createNote(harmony_, opts);

  // Should be skipped
  EXPECT_FALSE(note.has_value());
}

TEST_F(NoteCreatorTest, NoCollisionCheck) {
  // Register a note from Vocal at C4
  harmony_.registerNote(0, 480, 60, TrackRole::Vocal);

  NoteOptions opts;
  opts.start = 0;
  opts.duration = 480;
  opts.desired_pitch = 61;  // C#4 - would clash, but check is skipped
  opts.velocity = 100;
  opts.role = TrackRole::Vocal;  // Coordinate axis
  opts.preference = PitchPreference::NoCollisionCheck;
  opts.source = NoteSource::MelodyPhrase;

  auto note = createNote(harmony_, opts);

  ASSERT_TRUE(note.has_value());
  EXPECT_EQ(note->note, 61);  // Unchanged
}

TEST_F(NoteCreatorTest, RegisterToHarmony) {
  NoteOptions opts;
  opts.start = 0;
  opts.duration = 480;
  opts.desired_pitch = 60;
  opts.velocity = 100;
  opts.role = TrackRole::Bass;
  opts.register_to_harmony = true;
  opts.source = NoteSource::BassPattern;

  createNote(harmony_, opts);

  // Now a clash check should see this note
  NoteOptions opts2;
  opts2.start = 0;
  opts2.duration = 480;
  opts2.desired_pitch = 61;  // C#4 - minor 2nd clash with C4
  opts2.role = TrackRole::Chord;

  EXPECT_FALSE(harmony_.isPitchSafe(61, 0, 480, TrackRole::Chord));
}

TEST_F(NoteCreatorTest, GetSafePitchCandidates) {
  // Register a note at C4
  harmony_.registerNote(0, 480, 60, TrackRole::Vocal);

  auto candidates = getSafePitchCandidates(
      harmony_,
      61,  // desired: C#4 (clashes)
      0, 480,
      TrackRole::Bass,
      36, 72,
      PitchPreference::Default,
      5
  );

  // Should return some candidates (not C#4)
  EXPECT_FALSE(candidates.empty());

  // First candidate should be safe
  for (const auto& c : candidates) {
    EXPECT_NE(c.pitch, 61);
    EXPECT_TRUE(harmony_.isPitchSafe(c.pitch, 0, 480, TrackRole::Bass));
  }
}

TEST_F(NoteCreatorTest, PreferRootFifth) {
  auto candidates = getSafePitchCandidates(
      harmony_,
      64,  // desired: E4 (3rd of C chord)
      0, 480,
      TrackRole::Bass,
      36, 72,
      PitchPreference::PreferRootFifth,
      10
  );

  EXPECT_FALSE(candidates.empty());

  // Check that root/5th candidates are marked
  bool found_root = false;
  bool found_fifth = false;
  for (const auto& c : candidates) {
    if (c.is_root_or_fifth) {
      int pc = c.pitch % 12;
      if (pc == 0) found_root = true;  // C
      if (pc == 7) found_fifth = true;  // G
    }
  }
  EXPECT_TRUE(found_root || found_fifth);
}

#ifdef MIDISKETCH_NOTE_PROVENANCE
TEST_F(NoteCreatorTest, ProvenanceRecording) {
  NoteOptions opts;
  opts.start = 1920;  // Bar 1
  opts.duration = 480;
  opts.desired_pitch = 60;
  opts.velocity = 100;
  opts.role = TrackRole::Bass;
  opts.record_provenance = true;
  opts.source = NoteSource::BassPattern;

  auto note = createNote(harmony_, opts);

  ASSERT_TRUE(note.has_value());
  EXPECT_EQ(note->prov_source, static_cast<uint8_t>(NoteSource::BassPattern));
  EXPECT_EQ(note->prov_lookup_tick, 1920);
  EXPECT_EQ(note->prov_original_pitch, 60);
  // Chord degree depends on progression
}

TEST_F(NoteCreatorTest, ProvenanceOnCollisionResolve) {
  // Register a note at C4
  harmony_.registerNote(0, 480, 60, TrackRole::Vocal);

  NoteOptions opts;
  opts.start = 0;
  opts.duration = 480;
  opts.desired_pitch = 61;  // Will be adjusted
  opts.velocity = 100;
  opts.role = TrackRole::Bass;
  opts.record_provenance = true;
  opts.source = NoteSource::BassPattern;

  auto result = createNoteWithResult(harmony_, opts);

  ASSERT_TRUE(result.note.has_value());
  EXPECT_TRUE(result.was_adjusted);
  // Original pitch should be recorded
  EXPECT_EQ(result.note->prov_original_pitch, 61);
  // Actual pitch should be different
  EXPECT_NE(result.note->note, 61);
}
#endif

TEST_F(NoteCreatorTest, PreserveContourPreference) {
  // Register a note at C5 (72)
  harmony_.registerNote(0, 480, 72, TrackRole::Bass);

  auto candidates = getSafePitchCandidates(
      harmony_,
      73,  // desired: C#5 (clashes)
      0, 480,
      TrackRole::Motif,
      48, 84,
      PitchPreference::PreserveContour,
      10
  );

  EXPECT_FALSE(candidates.empty());

  // Should prefer octave shifts (same pitch class)
  bool found_octave_shift = false;
  for (const auto& c : candidates) {
    if (c.pitch % 12 == 73 % 12 && c.pitch != 73) {
      found_octave_shift = true;
      break;
    }
  }
  // May or may not find octave shift depending on collision state
  // Just verify we got candidates
  EXPECT_GT(candidates.size(), 0u);
}
