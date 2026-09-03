/**
 * @file note_creator_test.cpp
 * @brief Tests for unified note creation API (v2 Architecture).
 */

#include "core/note_creator.h"

#include <gtest/gtest.h>

#include <algorithm>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/harmony_context.h"
#include "core/midi_track.h"
#include "core/timing_constants.h"
#include "test_support/stub_harmony_context.h"

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

class SelectivePitchHarmony final : public test::StubHarmonyContext {
 public:
  explicit SelectivePitchHarmony(uint8_t safe_pitch) : safe_pitch_(safe_pitch) {}

  bool isConsonantWithOtherTracks(uint8_t pitch, Tick /*start*/, Tick /*duration*/,
                                  TrackRole /*exclude*/,
                                  bool /*is_weak_beat*/ = false) const override {
    return pitch == safe_pitch_;
  }

 private:
  uint8_t safe_pitch_;
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
  createNoteWithoutHarmonyAndAdd(track, 0, 480, 60, 100);

  EXPECT_EQ(track.noteCount(), 1);
  EXPECT_EQ(track.notes()[0].note, 60);
}

/// Real chord lookup, but only one pitch in the whole range is collision free.
class SinglePitchIsSafeHarmony final : public HarmonyContext {
 public:
  explicit SinglePitchIsSafeHarmony(uint8_t safe_pitch) : safe_pitch_(safe_pitch) {}

  bool isConsonantWithOtherTracks(uint8_t pitch, Tick /*start*/, Tick /*duration*/,
                                  TrackRole /*exclude*/,
                                  bool /*allow_accented_nct*/ = false) const override {
    return pitch == safe_pitch_;
  }

 private:
  uint8_t safe_pitch_;
};

TEST_F(NoteCreatorTest, PreferChordTonesNeverReturnsANonChordTone) {
  // The only pitch this context accepts is D4, a non-chord tone over the tonic.
  // PreferChordTones is a contract, not a ranking hint: producing no note is the
  // correct answer, because the caller can shorten it or leave the voice to the
  // minimum-voice fill, while a held D contradicts the chord every other track
  // is voiced against.
  SinglePitchIsSafeHarmony harmony(62);
  harmony.initialize(arrangement_, progression_, Mood::StraightPop);

  auto chord_tones = harmony.getChordTonesAt(0);
  ASSERT_GT(chord_tones.count, 0u) << "the fixture must expose a real chord at tick 0";
  ASSERT_EQ(std::find(chord_tones.begin(), chord_tones.end(), 2), chord_tones.end())
      << "D must be a non-chord tone here for this test to mean anything";

  NoteOptions opts;
  opts.start = 0;
  opts.duration = TICK_WHOLE;
  opts.desired_pitch = 62;
  opts.velocity = 100;
  opts.role = TrackRole::Chord;
  opts.preference = PitchPreference::PreferChordTones;
  opts.range_low = 48;
  opts.range_high = 84;
  opts.source = NoteSource::ChordVoicing;

  auto note = createNote(harmony, opts);

  if (note.has_value()) {
    int pitch_class = note->note % 12;
    EXPECT_NE(std::find(chord_tones.begin(), chord_tones.end(), pitch_class), chord_tones.end())
        << "PreferChordTones returned " << static_cast<int>(note->note)
        << ", which the chord sounding at this tick does not contain";
  }
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

TEST_F(NoteCreatorTest, GetMaxSafeEndUsesCurrentChordDegreeForTritoneContext) {
  Section section;
  section.type = SectionType::Chorus;
  section.start_tick = 0;
  section.bars = 1;
  section.name = "Chorus";
  Arrangement arrangement({section});

  ChordProgression tonic{{0, -1, -1, -1, -1, -1, -1, -1}, 1};
  HarmonyContext tonic_harmony;
  tonic_harmony.initialize(arrangement, tonic, Mood::StraightPop);
  tonic_harmony.registerNote(TICK_QUARTER, TICK_QUARTER, 65, TrackRole::Chord);  // F4

  EXPECT_EQ(tonic_harmony.getMaxSafeEnd(0, 71, TrackRole::Motif, TICK_HALF), TICK_QUARTER)
      << "B-F tritone should trim on tonic harmony";

  ChordProgression dominant{{4, -1, -1, -1, -1, -1, -1, -1}, 1};
  HarmonyContext dominant_harmony;
  dominant_harmony.initialize(arrangement, dominant, Mood::StraightPop);
  dominant_harmony.registerNote(TICK_QUARTER, TICK_QUARTER, 65, TrackRole::Chord);  // F4

  EXPECT_EQ(dominant_harmony.getMaxSafeEnd(0, 71, TrackRole::Motif, TICK_HALF), TICK_HALF)
      << "B-F tritone is chord-defining in V7 context and must not over-trim";
}

TEST_F(NoteCreatorTest, MaxSafeEndPreservesRegisteredWideTonicMajorSeventhColour) {
  Section section;
  section.type = SectionType::A;
  section.name = "A";
  section.bars = 1;
  section.start_tick = 0;
  Arrangement arrangement({section});
  HarmonyContext harmony;
  harmony.initialize(arrangement, getChordProgression(0), Mood::StraightPop);
  harmony.registerChordExtension(0, TICKS_PER_BAR, ChordExtension::Maj7);
  harmony.registerNote(TICK_QUARTER, TICK_QUARTER, 71, TrackRole::Chord);  // B4

  EXPECT_EQ(harmony.getMaxSafeEnd(0, 48, TrackRole::Motif, TICK_HALF), TICK_HALF)
      << "C3-B4 is a registered Imaj7 root/seventh pair with wide separation";
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

TEST_F(NoteCreatorTest, NoCollisionCheckClampsToRange) {
  // desired_pitch > range_high is folded down by octaves (pitch class is
  // preserved; a chromatic clamp to the range edge would manufacture a
  // non-chord tone the caller never verified).
  NoteOptions opts;
  opts.start = 0;
  opts.duration = 480;
  opts.desired_pitch = 90;  // Well above range_high
  opts.velocity = 100;
  opts.role = TrackRole::Vocal;
  opts.preference = PitchPreference::NoCollisionCheck;
  opts.range_low = 60;
  opts.range_high = 84;
  opts.source = NoteSource::MelodyPhrase;

  auto note = createNote(harmony_, opts);

  ASSERT_TRUE(note.has_value());
  EXPECT_EQ(note->note, 78);  // 90 folded down one octave (same pitch class)
}

TEST_F(NoteCreatorTest, NoCollisionCheckClampsToRangeLow) {
  // desired_pitch < range_low is folded up by octaves (pitch class preserved)
  NoteOptions opts;
  opts.start = 0;
  opts.duration = 480;
  opts.desired_pitch = 50;  // Below range_low
  opts.velocity = 100;
  opts.role = TrackRole::Vocal;
  opts.preference = PitchPreference::NoCollisionCheck;
  opts.range_low = 60;
  opts.range_high = 84;
  opts.source = NoteSource::MelodyPhrase;

  auto note = createNote(harmony_, opts);

  ASSERT_TRUE(note.has_value());
  EXPECT_EQ(note->note, 62);  // 50 folded up one octave (same pitch class)
}

TEST_F(NoteCreatorTest, NoCollisionCheckNoClampWhenInRange) {
  // desired_pitch within range should pass through unchanged
  NoteOptions opts;
  opts.start = 0;
  opts.duration = 480;
  opts.desired_pitch = 72;  // Within range
  opts.velocity = 100;
  opts.role = TrackRole::Vocal;
  opts.preference = PitchPreference::NoCollisionCheck;
  opts.range_low = 60;
  opts.range_high = 84;
  opts.source = NoteSource::MelodyPhrase;

  auto note = createNote(harmony_, opts);

  ASSERT_TRUE(note.has_value());
  EXPECT_EQ(note->note, 72);  // Unchanged
}

TEST_F(NoteCreatorTest, NoCollisionCheckNoClampWhenRangeUnset) {
  // When range_high=0 (unset), no clamping should occur
  NoteOptions opts;
  opts.start = 0;
  opts.duration = 480;
  opts.desired_pitch = 90;
  opts.velocity = 100;
  opts.role = TrackRole::Vocal;
  opts.preference = PitchPreference::NoCollisionCheck;
  // range_low=0, range_high=0 (defaults)
  opts.source = NoteSource::MelodyPhrase;

  auto note = createNote(harmony_, opts);

  ASSERT_TRUE(note.has_value());
  EXPECT_EQ(note->note, 90);  // No clamping when range is unset
}

TEST(NoteCreatorFallbackTest, FoldsBelowRangeUpwardWhenCandidateSearchHasNoMatch) {
  // No candidate around MIDI 0 is in the requested register. The fallback
  // must still preserve C by folding it upward to C4 instead of failing.
  SelectivePitchHarmony harmony(60);
  harmony.setChordTones({1, 5, 8});  // Keep the regular chord-tone search off C4.

  NoteOptions opts;
  opts.start = 0;
  opts.duration = TICKS_PER_BEAT;
  opts.desired_pitch = 0;
  opts.velocity = 100;
  opts.role = TrackRole::Motif;
  opts.range_low = 60;
  opts.range_high = 70;
  opts.source = NoteSource::Motif;

  const auto result = createNoteWithResult(harmony, opts);
  ASSERT_TRUE(result.note.has_value());
  EXPECT_EQ(result.note->note, 60);
  EXPECT_EQ(result.final_pitch, 60);
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

  EXPECT_FALSE(harmony_.isConsonantWithOtherTracks(61, 0, 480, TrackRole::Chord));
}

TEST_F(NoteCreatorTest, GetSafePitchCandidates) {
  // Register a note at C4
  harmony_.registerNote(0, 480, 60, TrackRole::Vocal);

  auto candidates =
      getSafePitchCandidates(harmony_,
                             61,  // desired: C#4 (clashes)
                             0, 480, TrackRole::Bass, 36, 72, PitchPreference::Default, 5);

  // Should return some candidates (not C#4)
  EXPECT_FALSE(candidates.empty());

  // First candidate should be safe
  for (const auto& c : candidates) {
    EXPECT_NE(c.pitch, 61);
    EXPECT_TRUE(harmony_.isConsonantWithOtherTracks(c.pitch, 0, 480, TrackRole::Bass));
  }
}

TEST(NoteCreatorCandidateTest, VocalDiversityFallbackKeepsOnlyVerifiedConsonantPitches) {
  SelectivePitchHarmony harmony(60);
  harmony.setChordDegree(0);
  harmony.setChordTones({0, 4, 7});

  auto candidates = getSafePitchCandidates(harmony, 60, 0, TICK_QUARTER, TrackRole::Vocal, 48, 84,
                                           PitchPreference::Default, 8, 60, 4);

  ASSERT_FALSE(candidates.empty());
  for (const auto& candidate : candidates) {
    EXPECT_EQ(candidate.pitch, 60)
        << "The diversity fallback must not admit an unverified wide M7/m9 candidate";
    EXPECT_TRUE(
        harmony.isConsonantWithOtherTracks(candidate.pitch, 0, TICK_QUARTER, TrackRole::Vocal));
  }
}

TEST_F(NoteCreatorTest, PreferRootFifth) {
  auto candidates =
      getSafePitchCandidates(harmony_,
                             64,  // desired: E4 (3rd of C chord)
                             0, 480, TrackRole::Bass, 36, 72, PitchPreference::PreferRootFifth, 10);

  EXPECT_FALSE(candidates.empty());

  // Check that root/5th candidates are marked
  bool found_root = false;
  bool found_fifth = false;
  for (const auto& c : candidates) {
    if (c.is_root_or_fifth) {
      int pc = c.pitch % 12;
      if (pc == 0) found_root = true;   // C
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

  auto candidates = getSafePitchCandidates(harmony_,
                                           73,  // desired: C#5 (clashes)
                                           0, 480, TrackRole::Motif, 48, 84,
                                           PitchPreference::PreserveContour, 10);

  EXPECT_FALSE(candidates.empty());

  // Verify we got candidates (may or may not include octave shifts)
  EXPECT_GT(candidates.size(), 0u);
}
