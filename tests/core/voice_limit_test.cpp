/**
 * @file voice_limit_test.cpp
 * @brief Tests for max_moving_voices limiter in Coordinator.
 *
 * Tests exercise the limiter through the public generateAllTracks() API.
 * max_moving_voices is set on Section objects before generation.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

#include "core/arrangement.h"
#include "core/basic_types.h"
#include "core/chord.h"
#include "core/coordinator.h"
#include "core/harmony_coordinator.h"
#include "core/i_harmony_coordinator.h"
#include "core/i_track_base.h"
#include "core/song.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "test_helpers/note_event_test_helper.h"

namespace midisketch {
namespace test {

// ============================================================================
// Helpers
// ============================================================================

namespace {

/// @brief Tracks subject to voice limiting (same order as coordinator.cpp).
constexpr TrackRole kLimitedTracks[] = {
    TrackRole::Vocal, TrackRole::Bass,     TrackRole::Chord,  TrackRole::Aux,
    TrackRole::Motif, TrackRole::Arpeggio, TrackRole::Guitar,
};
constexpr size_t kLimitedTrackCount = sizeof(kLimitedTracks) / sizeof(kLimitedTracks[0]);

/// @brief Collect sorted note onset offsets within a bar (relative to bar_start).
///
/// Frozen tracks keep the same rhythmic pattern after re-quantization, so
/// we compare note onset timing (not pitches) to determine if a track is
/// independently moving vs harmonically adapted.
std::vector<Tick> getNoteOnsetOffsets(const std::vector<NoteEvent>& notes, Tick bar_start,
                                      Tick bar_end) {
  std::vector<Tick> result;
  for (const auto& note : notes) {
    if (note.start_tick >= bar_start && note.start_tick < bar_end) {
      result.push_back(note.start_tick - bar_start);
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

/// @brief Check if a track is moving between prev_bar and curr_bar.
///
/// A track is "moving" if its rhythmic pattern (note onset timing) differs
/// between bars. Pitch changes from chord-tone re-quantization are not
/// considered independent movement.
bool isMoving(const std::vector<NoteEvent>& notes, Tick prev_bar_start, Tick curr_bar_start) {
  Tick prev_bar_end = prev_bar_start + TICKS_PER_BAR;
  Tick curr_bar_end = curr_bar_start + TICKS_PER_BAR;

  auto prev_onsets = getNoteOnsetOffsets(notes, prev_bar_start, prev_bar_end);
  auto curr_onsets = getNoteOnsetOffsets(notes, curr_bar_start, curr_bar_end);

  return prev_onsets != curr_onsets;
}

/// @brief Count how many harmonic tracks are moving at a given bar transition.
size_t countMovingTracks(const Song& song, Tick prev_bar_start, Tick curr_bar_start) {
  size_t count = 0;
  for (size_t idx = 0; idx < kLimitedTrackCount; ++idx) {
    const auto& notes = song.track(kLimitedTracks[idx]).notes();
    if (isMoving(notes, prev_bar_start, curr_bar_start)) {
      ++count;
    }
  }
  return count;
}

/// @brief Build default params for voice limit testing.
GeneratorParams makeVoiceLimitParams() {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::ElectroPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  params.vocal_low = 60;
  params.vocal_high = 84;
  params.bpm = 120;
  params.seed = 42;
  params.arpeggio_enabled = true;
  params.se_enabled = false;
  params.guitar_enabled = false;
  params.humanize = false;
  return params;
}

/// @brief Generate a song through the Coordinator pipeline with max_moving_voices
///        applied to all sections.
///
/// Steps: build arrangement from params, modify sections' max_moving_voices,
/// then generate via generateAllTracks() which calls applyVoiceLimit internally.
void generateWithVoiceLimit(const GeneratorParams& params, uint8_t max_moving_voices,
                            Song& out_song, Coordinator& out_coord) {
  // Step 1: Initialize coordinator to get the arrangement (with default
  // max_moving_voices=0)
  out_coord.initialize(params);
  const auto& base_sections = out_coord.getArrangement().sections();

  // Step 2: Copy sections and set max_moving_voices
  std::vector<Section> modified_sections = base_sections;
  for (auto& sec : modified_sections) {
    sec.max_moving_voices = max_moving_voices;
  }
  Arrangement modified_arrangement(modified_sections);

  // Step 3: Create a HarmonyCoordinator and initialize the chord progression
  HarmonyCoordinator harmony;
  const auto& progression = getChordProgression(params.chord_id);
  harmony.initialize(modified_arrangement, progression, params.mood);

  // Step 4: Re-initialize coordinator with modified arrangement
  std::mt19937 rng(params.seed);
  out_coord.initialize(params, modified_arrangement, rng, &harmony);

  // Step 5: Generate - this calls applyVoiceLimit internally
  out_coord.generateAllTracks(out_song);
}

}  // namespace

// ============================================================================
// Tests
// ============================================================================

TEST(VoiceLimitTest, MaxMovingVoicesZeroNoEffect) {
  // max_moving_voices=0 (default) should not freeze any tracks
  auto params = makeVoiceLimitParams();

  Song song;
  Coordinator coord;
  generateWithVoiceLimit(params, 0, song, coord);

  // Verify tracks are generated normally
  EXPECT_FALSE(song.vocal().empty());
  EXPECT_FALSE(song.bass().empty());
  EXPECT_FALSE(song.chord().empty());
}

TEST(VoiceLimitTest, MaxMovingVoicesLimitApplied) {
  // Generate with max_moving_voices=2 on all sections
  auto params = makeVoiceLimitParams();

  Song song;
  Coordinator coord;
  generateWithVoiceLimit(params, 2, song, coord);

  // Verify: no more than 2 tracks moving on any bar transition
  const auto& sections = coord.getArrangement().sections();
  for (const auto& sec : sections) {
    if (sec.bars <= 1) continue;
    Tick section_start = sec.start_tick;

    for (uint8_t bar_idx = 1; bar_idx < sec.bars; ++bar_idx) {
      Tick prev_bar = section_start + (bar_idx - 1) * TICKS_PER_BAR;
      Tick curr_bar = section_start + bar_idx * TICKS_PER_BAR;

      size_t moving = countMovingTracks(song, prev_bar, curr_bar);
      EXPECT_LE(moving, 2u) << "Section " << sec.name << " bar " << static_cast<int>(bar_idx)
                            << " has " << moving << " moving tracks (limit=2)";
    }
  }
}

TEST(VoiceLimitTest, MaxMovingVoicesPreservesPriority) {
  // With max_moving_voices=2, Vocal and Bass (highest priority) should never
  // be frozen. Generate twice - once with limit=0, once with limit=2 - and
  // compare Vocal/Bass tracks.
  auto params = makeVoiceLimitParams();

  Song unlimited_song;
  Coordinator unlimited_coord;
  generateWithVoiceLimit(params, 0, unlimited_song, unlimited_coord);

  Song limited_song;
  Coordinator limited_coord;
  generateWithVoiceLimit(params, 2, limited_song, limited_coord);

  // Vocal and Bass should be identical between unlimited and limited
  EXPECT_EQ(unlimited_song.vocal().notes().size(), limited_song.vocal().notes().size())
      << "Vocal track should not be modified by voice limiter";
  EXPECT_EQ(unlimited_song.bass().notes().size(), limited_song.bass().notes().size())
      << "Bass track should not be modified by voice limiter";

  // Verify note content is identical
  const auto& vocal_unlimited = unlimited_song.vocal().notes();
  const auto& vocal_limited = limited_song.vocal().notes();
  for (size_t idx = 0; idx < std::min(vocal_unlimited.size(), vocal_limited.size()); ++idx) {
    EXPECT_EQ(vocal_unlimited[idx].start_tick, vocal_limited[idx].start_tick);
    EXPECT_EQ(vocal_unlimited[idx].note, vocal_limited[idx].note);
  }
  const auto& bass_unlimited = unlimited_song.bass().notes();
  const auto& bass_limited = limited_song.bass().notes();
  for (size_t idx = 0; idx < std::min(bass_unlimited.size(), bass_limited.size()); ++idx) {
    EXPECT_EQ(bass_unlimited[idx].start_tick, bass_limited[idx].start_tick);
    EXPECT_EQ(bass_unlimited[idx].note, bass_limited[idx].note);
  }
}

TEST(VoiceLimitTest, MaxMovingVoicesOnlyAffectsConstrainedSections) {
  // Sections with max_moving_voices=0 should produce identical output
  // to a generation without voice limiting at all.
  auto params = makeVoiceLimitParams();

  Song song_a;
  Coordinator coord_a;
  generateWithVoiceLimit(params, 0, song_a, coord_a);

  Song song_b;
  Coordinator coord_b;
  generateWithVoiceLimit(params, 0, song_b, coord_b);

  // All tracks should be identical
  for (size_t idx = 0; idx < kTrackCount; ++idx) {
    EXPECT_EQ(song_a.tracks()[idx].notes().size(), song_b.tracks()[idx].notes().size())
        << "Track " << idx << " should be identical when max_moving_voices=0";
  }
}

TEST(VoiceLimitTest, MaxMovingVoicesOneFreezesAllButOne) {
  // With max_moving_voices=1, only one track should move at each bar
  auto params = makeVoiceLimitParams();

  Song song;
  Coordinator coord;
  generateWithVoiceLimit(params, 1, song, coord);

  // Verify no more than 1 track moves at each bar transition
  const auto& sections = coord.getArrangement().sections();
  for (const auto& sec : sections) {
    if (sec.bars <= 1) continue;
    Tick section_start = sec.start_tick;

    for (uint8_t bar_idx = 1; bar_idx < sec.bars; ++bar_idx) {
      Tick prev_bar = section_start + (bar_idx - 1) * TICKS_PER_BAR;
      Tick curr_bar = section_start + bar_idx * TICKS_PER_BAR;

      size_t moving = countMovingTracks(song, prev_bar, curr_bar);
      EXPECT_LE(moving, 1u) << "Section " << sec.name << " bar " << static_cast<int>(bar_idx)
                            << " has " << moving << " moving tracks (limit=1)";
    }
  }
}

TEST(VoiceLimitTest, FrozenNotesAreReQuantizedToChordTones) {
  // With voice limiting, frozen (copied) notes should be snapped to the
  // current bar's chord tones, not left as stale pitches from the previous bar.
  auto params = makeVoiceLimitParams();

  Song song;
  Coordinator coord;
  generateWithVoiceLimit(params, 1, song, coord);

  IHarmonyCoordinator& harmony = coord.harmony();
  const auto& sections = coord.getArrangement().sections();

  int checked = 0;
  int chord_tone_count = 0;

  for (const auto& sec : sections) {
    if (sec.bars <= 1) continue;
    Tick section_start = sec.start_tick;

    for (uint8_t bar_idx = 1; bar_idx < sec.bars; ++bar_idx) {
      Tick curr_bar_start = section_start + bar_idx * TICKS_PER_BAR;
      Tick curr_bar_end = curr_bar_start + TICKS_PER_BAR;

      // Check all limited tracks for notes in this bar
      for (size_t ti = 0; ti < kLimitedTrackCount; ++ti) {
        const auto& notes = song.track(kLimitedTracks[ti]).notes();
        for (const auto& note : notes) {
          if (note.start_tick < curr_bar_start || note.start_tick >= curr_bar_end) continue;

          auto chord_tones = harmony.getChordTonesAt(note.start_tick);
          int pc = note.note % 12;
          bool is_ct = false;
          for (int ct : chord_tones) {
            if (ct == pc) {
              is_ct = true;
              break;
            }
          }
          ++checked;
          if (is_ct) ++chord_tone_count;
        }
      }
    }
  }

  // With max_moving_voices=1, many notes are frozen copies that have been
  // re-quantized. The vast majority of notes should land on chord tones.
  // Allow some non-chord-tone notes (passing tones, tensions from non-frozen tracks).
  ASSERT_GT(checked, 0) << "Should have checked at least some notes";
  double chord_tone_ratio = static_cast<double>(chord_tone_count) / static_cast<double>(checked);
  EXPECT_GE(chord_tone_ratio, 0.60)
      << "At least 60% of notes should be chord tones after re-quantization "
      << "(got " << chord_tone_count << "/" << checked << " = " << chord_tone_ratio * 100.0 << "%)";
}

// ============================================================================
// Frozen-bar re-quantization tests (direct applyVoiceLimit API)
// ============================================================================

namespace {

/// @brief Build a single 2-bar section at tick 0 with max_moving_voices=1.
std::vector<Section> makeFrozenBarSections() {
  Section sec;
  sec.type = SectionType::A;
  sec.name = "A";
  sec.bars = 2;
  sec.start_bar = 0;
  sec.start_tick = 0;
  sec.max_moving_voices = 1;
  return {sec};
}

/// @brief Craft a song that forces the limiter to freeze Motif's bar 1.
///
/// Vocal and Motif both change pitch classes between bar 0 and bar 1, so both
/// count as "moving". With max_moving_voices=1, the lowest-priority moving
/// track (Motif) is frozen: its bar-1 notes are replaced by copies of bar 0
/// and re-quantized to the current chord.
///
/// Motif uses F#4 (66, non-diatonic in C major) so re-quantization is
/// guaranteed to change the pitch regardless of which chord is active.
void buildFrozenMotifSong(Song& song) {
  // Vocal: C5 on every beat in bar 0, D5 in bar 1 (moving, stays unfrozen)
  for (int beat = 0; beat < 4; ++beat) {
    song.vocal().addNote(
        NoteEventTestHelper::create(beat * TICKS_PER_BEAT, TICKS_PER_BEAT, 72, 90));
    song.vocal().addNote(
        NoteEventTestHelper::create(TICKS_PER_BAR + beat * TICKS_PER_BEAT, TICKS_PER_BEAT, 74, 90));
  }
  // Motif: F#4 on every beat in bar 0, A4 in bar 1 (moving, gets frozen)
  for (int beat = 0; beat < 4; ++beat) {
    song.motif().addNote(
        NoteEventTestHelper::create(beat * TICKS_PER_BEAT, TICKS_PER_BEAT, 66, 80));
    song.motif().addNote(
        NoteEventTestHelper::create(TICKS_PER_BAR + beat * TICKS_PER_BEAT, TICKS_PER_BEAT, 69, 80));
  }
}

/// @brief Run applyVoiceLimit on a crafted song and return bar-1 motif notes
///        sorted chronologically.
std::vector<NoteEvent> applyVoiceLimitToFrozenMotif(Song& song) {
  auto params = makeVoiceLimitParams();
  auto sections = makeFrozenBarSections();
  Arrangement arrangement(sections);

  HarmonyCoordinator harmony;
  const auto& progression = getChordProgression(params.chord_id);
  harmony.initialize(arrangement, progression, params.mood);

  Coordinator coord;
  std::mt19937 rng(params.seed);
  coord.initialize(params, arrangement, rng, &harmony);

  buildFrozenMotifSong(song);
  coord.applyVoiceLimit(song, sections);

  std::vector<NoteEvent> bar1_notes;
  for (const auto& note : song.motif().notes()) {
    if (note.start_tick >= TICKS_PER_BAR && note.start_tick < 2 * TICKS_PER_BAR) {
      bar1_notes.push_back(note);
    }
  }
  std::sort(bar1_notes.begin(), bar1_notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });
  return bar1_notes;
}

/// @brief Freeze a high-register target track while Motif remains moving.
///
/// Bar 1 of FourChordPop is G major, so B7 remains a valid chord tone.  This
/// makes it possible to distinguish the generator's real high bound from a
/// stale lower limit in the re-quantization path.
std::vector<NoteEvent> applyVoiceLimitToFrozenHighTrack(Song& song, TrackRole role,
                                                        uint8_t source_pitch) {
  auto params = makeVoiceLimitParams();
  auto sections = makeFrozenBarSections();
  Arrangement arrangement(sections);

  HarmonyCoordinator harmony;
  harmony.initialize(arrangement, getChordProgression(params.chord_id), params.mood);

  Coordinator coord;
  std::mt19937 rng(params.seed);
  coord.initialize(params, arrangement, rng, &harmony);

  // Motif has higher freeze priority than Arpeggio and Guitar, keeping it
  // moving ensures the requested lower-priority track is frozen.
  song.motif().addNote(NoteEventTestHelper::create(0, TICKS_PER_BEAT, 60, 80));
  song.motif().addNote(NoteEventTestHelper::create(TICKS_PER_BAR, TICKS_PER_BEAT, 62, 80));
  song.track(role).addNote(NoteEventTestHelper::create(0, TICKS_PER_BEAT, source_pitch, 80));
  song.track(role).addNote(NoteEventTestHelper::create(TICKS_PER_BAR, TICKS_PER_BEAT, 60, 80));

  coord.applyVoiceLimit(song, sections);

  std::vector<NoteEvent> bar1_notes;
  for (const auto& note : song.track(role).notes()) {
    if (note.start_tick >= TICKS_PER_BAR && note.start_tick < 2 * TICKS_PER_BAR) {
      bar1_notes.push_back(note);
    }
  }
  return bar1_notes;
}

}  // namespace

TEST(VoiceLimitRequantizeTest, FrozenBarReplacesNotesWithPreviousBarCopy) {
  Song song;
  auto bar1_notes = applyVoiceLimitToFrozenMotif(song);

  // Bar 1's original A4 notes were replaced by copies of bar 0 (one per beat)
  ASSERT_EQ(bar1_notes.size(), 4u);
  for (const auto& note : bar1_notes) {
    EXPECT_NE(note.note, 69) << "Original bar-1 notes should have been removed";
  }
}

TEST(VoiceLimitRequantizeTest, RequantizedPitchChangesFromNonDiatonicSource) {
  Song song;
  auto bar1_notes = applyVoiceLimitToFrozenMotif(song);

  // F#4 (66) is not a chord tone of any diatonic C-major chord, so every
  // frozen copy must have been re-quantized to a different pitch.
  ASSERT_EQ(bar1_notes.size(), 4u);
  for (const auto& note : bar1_notes) {
    EXPECT_NE(note.note, 66) << "Non-diatonic frozen pitch must be re-quantized";
  }
}

namespace {

/// @brief Craft a three-bar song whose motif is fitted to the bar the freeze
///        is about to replace, and run the limiter over it.
///
/// Bar 0 holds one note long enough to reach the first onset of bar 1, and
/// bar 1 holds two shorter ones. Freezing bar 1 replaces those two with a copy
/// of bar 0's single long note, so both the note before the seam and the copy
/// inside it are now longer than the music that follows them.
std::vector<NoteEvent> applyVoiceLimitToLongMotifNote(Song& song) {
  auto params = makeVoiceLimitParams();
  Section sec;
  sec.type = SectionType::A;
  sec.name = "A";
  sec.bars = 3;
  sec.start_bar = 0;
  sec.start_tick = 0;
  sec.max_moving_voices = 1;
  std::vector<Section> sections{sec};
  Arrangement arrangement(sections);

  HarmonyCoordinator harmony;
  harmony.initialize(arrangement, getChordProgression(params.chord_id), params.mood);

  Coordinator coord;
  std::mt19937 rng(params.seed);
  coord.initialize(params, arrangement, rng, &harmony);

  // Vocal moves every bar, so it outranks the motif and stays unfrozen.
  for (int bar = 0; bar < 3; ++bar) {
    for (int beat = 0; beat < 4; ++beat) {
      song.vocal().addNote(NoteEventTestHelper::create(bar * TICKS_PER_BAR + beat * TICKS_PER_BEAT,
                                                       TICKS_PER_BEAT,
                                                       static_cast<uint8_t>(72 + bar), 90));
    }
  }

  // Motif, as a clean single line: every note ends exactly where the next
  // begins, and the first one is fitted to an onset a bar and a quarter away.
  song.motif().addNote(NoteEventTestHelper::create(0, 2400, 60, 80));
  song.motif().addNote(NoteEventTestHelper::create(2400, 480, 62, 80));
  song.motif().addNote(NoteEventTestHelper::create(2880, 960, 64, 80));
  song.motif().addNote(NoteEventTestHelper::create(2 * TICKS_PER_BAR, TICKS_PER_BAR, 60, 80));

  coord.applyVoiceLimit(song, sections);

  std::vector<NoteEvent> notes = song.motif().notes();
  std::sort(notes.begin(), notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });
  return notes;
}

}  // namespace

TEST(VoiceLimitRequantizeTest, AFrozenBarLeavesNoNoteSoundingOverTheNextOnset) {
  Song song;
  const auto notes = applyVoiceLimitToLongMotifNote(song);
  ASSERT_FALSE(notes.empty());

  // The freeze has to have happened, or the invariant below is vacuous: bar 1
  // must no longer hold the two onsets it was written with.
  std::vector<Tick> bar1_onsets;
  for (const auto& note : notes) {
    if (note.start_tick >= TICKS_PER_BAR && note.start_tick < 2 * TICKS_PER_BAR) {
      bar1_onsets.push_back(note.start_tick - TICKS_PER_BAR);
    }
  }
  ASSERT_EQ(bar1_onsets.size(), 1u) << "bar 1 should hold the single onset copied from bar 0";
  EXPECT_EQ(bar1_onsets[0], 0u);

  // A motif is one line. No note may still be sounding when a later one starts.
  for (size_t i = 0; i < notes.size(); ++i) {
    const Tick end = notes[i].start_tick + notes[i].duration;
    for (size_t j = i + 1; j < notes.size(); ++j) {
      if (notes[j].start_tick == notes[i].start_tick) continue;
      if (notes[j].start_tick >= end) break;
      ADD_FAILURE() << "motif note " << static_cast<int>(notes[i].note) << " at "
                    << notes[i].start_tick << " lasts " << notes[i].duration
                    << " ticks and is still sounding at " << notes[j].start_tick;
      break;
    }
  }
}

#ifdef MIDISKETCH_NOTE_PROVENANCE
TEST(VoiceLimitRequantizeTest, RecordsChordToneSnapTransform) {
  Song song;
  auto bar1_notes = applyVoiceLimitToFrozenMotif(song);

  // Every re-quantized note must record the pitch change as a ChordToneSnap
  // transform step so provenance debugging can trace the modification.
  ASSERT_EQ(bar1_notes.size(), 4u);
  for (const auto& note : bar1_notes) {
    if (note.note == 66) continue;  // Not re-quantized (should not happen)

    bool has_snap_step = false;
    for (uint8_t s = 0; s < note.transform_count; ++s) {
      const auto& step = note.transform_steps[s];
      if (step.type == TransformStepType::ChordToneSnap && step.input_pitch == 66) {
        EXPECT_EQ(step.output_pitch, note.note) << "Transform output should match the final pitch";
        has_snap_step = true;
      }
    }
    EXPECT_TRUE(has_snap_step) << "Re-quantized note at tick " << note.start_tick << " (pitch "
                               << static_cast<int>(note.note)
                               << ") must record a ChordToneSnap transform";
  }
}
#endif  // MIDISKETCH_NOTE_PROVENANCE

TEST(VoiceLimitRequantizeTest, BreaksSamePitchRuns) {
  Song song;
  auto bar1_notes = applyVoiceLimitToFrozenMotif(song);

  // All four frozen copies share the same source pitch (F#4), so naive
  // nearest-chord-tone snapping would map them all to the same pitch,
  // producing a monotone run. The limiter must diversify the run once it
  // exceeds kMaxFrozenSameRun (3).
  ASSERT_EQ(bar1_notes.size(), 4u);
  std::set<uint8_t> distinct_pitches;
  for (const auto& note : bar1_notes) {
    distinct_pitches.insert(note.note);
  }
  EXPECT_GE(distinct_pitches.size(), 2u)
      << "Re-quantization must not collapse a whole bar onto a single pitch";
}

TEST(VoiceLimitRequantizeTest, RequantizedPitchStaysBelowConcurrentVocal) {
  Song song;
  auto bar1_notes = applyVoiceLimitToFrozenMotif(song);

  // The source pitch (F#4=66) is below the concurrent vocal (D5=74).
  // Re-quantization must not push backing notes above the main melody.
  ASSERT_EQ(bar1_notes.size(), 4u);
  for (const auto& note : bar1_notes) {
    EXPECT_LT(note.note, 74) << "Backing note below the vocal must stay below it "
                             << "(tick " << note.start_tick << ")";
  }
}

TEST(VoiceLimitRequantizeTest, UsesArpeggioPhysicalModelRange) {
  Song song;
  const auto bar1_notes = applyVoiceLimitToFrozenHighTrack(song, TrackRole::Arpeggio, 107);

  ASSERT_EQ(bar1_notes.size(), 1u);
  EXPECT_EQ(bar1_notes.front().note, 107)
      << "B7 is a G-major chord tone and must not be truncated to the old C7 ceiling";
  EXPECT_LE(bar1_notes.front().note, PhysicalModels::kArpeggioSynth.pitch_high);
}

TEST(VoiceLimitRequantizeTest, KeepsAGuitarNoteInsideThePhysicalModel) {
  Song song;
  const auto bar1_notes = applyVoiceLimitToFrozenHighTrack(song, TrackRole::Guitar, 71);

  ASSERT_EQ(bar1_notes.size(), 1u);
  EXPECT_EQ(bar1_notes.front().note, 71)
      << "B4 is a G-major chord tone inside the guitar's range and must not be "
         "truncated by a stale lower limit in the re-quantization path";
}

TEST(VoiceLimitRequantizeTest, PullsAGuitarNoteBackIntoThePhysicalModel) {
  Song song;
  const auto bar1_notes = applyVoiceLimitToFrozenHighTrack(song, TrackRole::Guitar, 83);

  ASSERT_EQ(bar1_notes.size(), 1u);
  EXPECT_LE(bar1_notes.front().note, PhysicalModels::kElectricGuitar.pitch_high)
      << "The freeze copies a bar from elsewhere in the song, so re-quantization "
         "is the one pass that can place a guitar note the generator would never "
         "write. Its bound is the guitar's own range, which stops at E5.";
}

}  // namespace test
}  // namespace midisketch
