/**
 * @file pitch_writeback_test.cpp
 * @brief Tests for pitch edits applied to a track after it was generated.
 *
 * Two properties are covered:
 * - TrackPitchEditor rejects a pitch the harmony state does not accept, records
 *   the moves it does apply, and refreshes the collision registry so a later
 *   query cannot answer from a pitch that is no longer sounding.
 * - In a finished song, no note on any track whose pitch differs from the one
 *   it was created with is missing a transform step explaining the difference.
 *   Such a note is invisible to every later pass and to the analysis output.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/arrangement.h"
#include "core/chord_utils.h"
#include "core/harmony_context.h"
#include "core/midi_track.h"
#include "core/note_creator.h"
#include "core/preset_data.h"
#include "core/timing_constants.h"
#include "core/track_pitch_editor.h"
#include "core/types.h"
#include "midisketch.h"

namespace midisketch {
namespace {

// Add a note to a track through the note creation path so it carries
// provenance, and return its index.
size_t addTrackNote(MidiTrack& track, IHarmonyContext& harmony, Tick start, Tick duration,
                    uint8_t pitch, TrackRole role) {
  NoteOptions opts;
  opts.start = start;
  opts.duration = duration;
  opts.desired_pitch = pitch;
  opts.velocity = 90;
  opts.role = role;
  opts.preference = PitchPreference::NoCollisionCheck;
  opts.source = NoteSource::PostProcess;
  createNoteAndAdd(track, harmony, opts);
  return track.notes().size() - 1;
}

TEST(TrackPitchEditorTest, RejectsAPitchTheHarmonyStateDoesNotAccept) {
  HarmonyContext harmony;
  MidiTrack guitar;
  size_t index = addTrackNote(guitar, harmony, 0, TICKS_PER_BEAT, 48, TrackRole::Guitar);

  // A vocal minor 2nd above the target pitch makes the move dissonant.
  harmony.registerNote(0, TICKS_PER_BEAT, 61, TrackRole::Vocal);

  TrackPitchEditor editor(guitar, harmony, TrackRole::Guitar);
  EXPECT_FALSE(editor.moveTo(index, 60, TransformStepType::OctaveAdjust));
  EXPECT_EQ(guitar.notes()[index].note, 48) << "A rejected move must leave the note untouched";
  EXPECT_FALSE(editor.dirty());
}

TEST(TrackPitchEditorTest, RefusesAPitchAVoiceOfItsOwnTrackIsSoundingAgainst) {
  HarmonyContext harmony;
  MidiTrack guitar;
  // One strum: the voices are raked a few ticks apart and sound together.
  addTrackNote(guitar, harmony, 0, TICKS_PER_BEAT, 59, TrackRole::Guitar);
  size_t index = addTrackNote(guitar, harmony, 8, TICKS_PER_BEAT, 48, TrackRole::Guitar);

  // Nothing else is sounding, so the cross-track check accepts the target and
  // the only thing that can refuse it is the voice beside it.
  TrackPitchEditor editor(guitar, harmony, TrackRole::Guitar);
  EXPECT_FALSE(editor.moveTo(index, 60, TransformStepType::OctaveAdjust, 12, 0));
  EXPECT_EQ(guitar.notes()[index].note, 48) << "A rejected move must leave the note untouched";
  EXPECT_FALSE(editor.dirty());
}

TEST(TrackPitchEditorTest, AllowsAStepThroughASemitoneItsOwnLineBarelyOverlaps) {
  HarmonyContext harmony;
  MidiTrack aux;
  // A line, not a chord: the notes are consecutive and overlap only by the
  // fraction of a beat a doubling offset produces. Guarding the property above
  // must not cost a melodic track its stepwise motion.
  addTrackNote(aux, harmony, 0, 240, 60, TrackRole::Aux);
  size_t index = addTrackNote(aux, harmony, 200, 240, 64, TrackRole::Aux);

  TrackPitchEditor editor(aux, harmony, TrackRole::Aux);
  EXPECT_TRUE(editor.moveTo(index, 61, TransformStepType::ChordToneSnap));
  EXPECT_EQ(aux.notes()[index].note, 61);
}

TEST(TrackPitchEditorTest, RecordsAnAppliedMoveOnTheNote) {
  HarmonyContext harmony;
  MidiTrack guitar;
  size_t index = addTrackNote(guitar, harmony, 0, TICKS_PER_BEAT, 48, TrackRole::Guitar);

  TrackPitchEditor editor(guitar, harmony, TrackRole::Guitar);
  ASSERT_TRUE(editor.moveTo(index, 60, TransformStepType::OctaveAdjust, 12, 0));

  const NoteEvent& moved = guitar.notes()[index];
  EXPECT_EQ(moved.note, 60);
#ifdef MIDISKETCH_NOTE_PROVENANCE
  EXPECT_EQ(moved.prov_original_pitch, 48);
  ASSERT_TRUE(moved.hasTransformHistory());
  bool has_octave_step = false;
  for (uint8_t i = 0; i < moved.transform_count; ++i) {
    const auto& step = moved.transform_steps[i];
    if (step.type == TransformStepType::OctaveAdjust && step.input_pitch == 48 &&
        step.output_pitch == 60) {
      has_octave_step = true;
    }
  }
  EXPECT_TRUE(has_octave_step) << "The move must be recorded as a transform step";
#endif
}

TEST(TrackPitchEditorTest, RefreshesTheRegistryWhenTheHandleGoesOutOfScope) {
  HarmonyContext harmony;
  MidiTrack guitar;
  size_t index = addTrackNote(guitar, harmony, 0, TICKS_PER_BEAT, 48, TrackRole::Guitar);
  harmony.registerTrack(guitar, TrackRole::Guitar);

  // Another track querying the guitar's register must see the moved pitch, not
  // the one the registry held before the edit.
  EXPECT_EQ(harmony.getLowestPitchForTrackInRange(0, TICKS_PER_BEAT, TrackRole::Guitar), 48);
  {
    TrackPitchEditor editor(guitar, harmony, TrackRole::Guitar);
    ASSERT_TRUE(editor.moveTo(index, 60, TransformStepType::OctaveAdjust, 12, 0));
  }
  EXPECT_EQ(harmony.getLowestPitchForTrackInRange(0, TICKS_PER_BEAT, TrackRole::Guitar), 60);
}

TEST(TrackPitchEditorTest, RemovingANoteClearsItFromTheRegistry) {
  HarmonyContext harmony;
  MidiTrack guitar;
  size_t index = addTrackNote(guitar, harmony, 0, TICKS_PER_BEAT, 48, TrackRole::Guitar);
  harmony.registerTrack(guitar, TrackRole::Guitar);
  ASSERT_EQ(harmony.getLowestPitchForTrackInRange(0, TICKS_PER_BEAT, TrackRole::Guitar), 48);

  {
    TrackPitchEditor editor(guitar, harmony, TrackRole::Guitar);
    ASSERT_TRUE(editor.removeAt(index));
  }
  EXPECT_TRUE(guitar.notes().empty());
  EXPECT_EQ(harmony.getLowestPitchForTrackInRange(0, TICKS_PER_BEAT, TrackRole::Guitar), 0)
      << "A removed note must not stay in the collision registry";
}

// ============================================================================
// Whole-song scan
// ============================================================================

struct UntracedMove {
  std::string track;
  Tick tick;
  int original_pitch;
  int final_pitch;
};

// Scan every note of the song and collect the ones whose pitch differs from the
// pitch they were created with while carrying no transform history at all.
std::vector<UntracedMove> findUntracedMoves(const Song& song, const std::vector<TrackRole>& roles) {
  std::vector<UntracedMove> out;
#ifdef MIDISKETCH_NOTE_PROVENANCE
  const std::pair<const MidiTrack*, const char*> all[] = {
      {&song.vocal(), "Vocal"},       {&song.motif(), "Motif"}, {&song.aux(), "Aux"},
      {&song.bass(), "Bass"},         {&song.chord(), "Chord"}, {&song.guitar(), "Guitar"},
      {&song.arpeggio(), "Arpeggio"},
  };
  const TrackRole role_of[] = {TrackRole::Vocal,   TrackRole::Motif, TrackRole::Aux,
                               TrackRole::Bass,    TrackRole::Chord, TrackRole::Guitar,
                               TrackRole::Arpeggio};

  for (size_t i = 0; i < std::size(all); ++i) {
    bool wanted = false;
    for (TrackRole role : roles) {
      if (role == role_of[i]) wanted = true;
    }
    if (!wanted) continue;

    for (const auto& note : all[i].first->notes()) {
      if (!note.hasValidProvenance()) continue;
      if (note.prov_original_pitch == 0) continue;
      if (note.prov_original_pitch == note.note) continue;
      if (note.hasTransformHistory()) continue;
      out.push_back({all[i].second, note.start_tick, note.prov_original_pitch, note.note});
    }
  }
#else
  (void)song;
  (void)roles;
#endif
  return out;
}

std::string describeUntracedMoves(const std::vector<UntracedMove>& moves) {
  std::string out;
  for (const auto& m : moves) {
    out += m.track + " tick " + std::to_string(m.tick) + ": " + std::to_string(m.original_pitch) +
           " -> " + std::to_string(m.final_pitch) + " with no transform history\n";
  }
  return out;
}

class PitchWritebackTest : public ::testing::Test {
 protected:
  void generateSong(uint32_t seed, uint8_t blueprint) {
    SongConfig config = createDefaultSongConfig(0);
    config.seed = seed;
    config.blueprint_id = blueprint;
    sketch_.generateFromConfig(config);
  }

  // The arpeggio-chord resolver only runs for the two styles that put the
  // accompaniment in the foreground, and only with an arpeggio to resolve.
  void generateArpeggioForwardSong(uint32_t seed, uint8_t blueprint, CompositionStyle style) {
    SongConfig config = createDefaultSongConfig(0);
    config.seed = seed;
    config.blueprint_id = blueprint;
    config.arpeggio_enabled = true;
    config.composition_style = style;
    config.composition_style_explicit = true;
    sketch_.generateFromConfig(config);
  }

  MidiSketch sketch_;
};

// Bass microvariation, the guitar/bass separation pass, the chord track's fold
// under the vocal and the phrase-pair cadence all move pitches on finished or
// nearly finished lines. Every such move has to leave a trace, otherwise the
// pitch that sounds cannot be explained from the note itself.
//
// The question is asked of every track that carries provenance. Asking it of
// two of them is what let the vocal and the chord track move pitches silently:
// a pass is only covered here if its track is named, so naming a subset makes
// the check look like a guarantee it is not making.
TEST_F(PitchWritebackTest, PitchMovesLeaveATrace) {
  constexpr uint32_t kSeeds[] = {12345, 777, 20260903, 424242, 31337};
  constexpr uint8_t kBlueprints[] = {0, 1, 2, 3, 4, 9};
  const std::vector<TrackRole> kRoles = {TrackRole::Vocal,   TrackRole::Motif, TrackRole::Aux,
                                         TrackRole::Bass,    TrackRole::Chord, TrackRole::Guitar,
                                         TrackRole::Arpeggio};

  size_t songs_scanned = 0;
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      ASSERT_FALSE(sketch_.getSong().bass().empty())
          << "blueprint=" << static_cast<int>(blueprint) << " seed=" << seed;
      auto moves = findUntracedMoves(sketch_.getSong(), kRoles);
      EXPECT_TRUE(moves.empty()) << "blueprint=" << static_cast<int>(blueprint) << " seed=" << seed
                                 << "\n"
                                 << describeUntracedMoves(moves);
      ++songs_scanned;
    }
  }
  EXPECT_EQ(songs_scanned, std::size(kSeeds) * std::size(kBlueprints));
}

// ============================================================================
// Where a moved note came from
// ============================================================================

// Not verified in the shipping build, and nothing there needs it to be: the
// recorded origin this pins is absent from that build, so no pass can overwrite
// it and no later reading can be misled by it.
#ifdef MIDISKETCH_NOTE_PROVENANCE

struct RewrittenOrigin {
  std::string track;
  Tick tick;
  int recorded_origin;
  int first_move_input;
};

// Steps whose input and output are pitches. Duration, velocity and timing steps
// carry zero in both fields, so they say nothing about where a pitch came from.
// Only the recorded steps are classified here, so the shipping build, which
// records none, has no use for this.
bool isPitchMove(TransformStepType type) {
  switch (type) {
    case TransformStepType::OctaveAdjust:
    case TransformStepType::MotionAdjust:
    case TransformStepType::VocalAvoid:
    case TransformStepType::RangeClamp:
    case TransformStepType::PatternOffset:
    case TransformStepType::CollisionAvoid:
    case TransformStepType::ScaleSnap:
    case TransformStepType::IntervalFix:
    case TransformStepType::ChordToneSnap:
      return true;
    default:
      return false;
  }
}

// Collect notes whose recorded origin is not the pitch the first pass to move
// them started from, which means a later pass overwrote it with its own input.
std::vector<RewrittenOrigin> findRewrittenOrigins(const Song& song, size_t& moved_notes) {
  std::vector<RewrittenOrigin> out;
  const std::pair<const MidiTrack*, const char*> all[] = {
      {&song.vocal(), "Vocal"},       {&song.motif(), "Motif"}, {&song.aux(), "Aux"},
      {&song.bass(), "Bass"},         {&song.chord(), "Chord"}, {&song.guitar(), "Guitar"},
      {&song.arpeggio(), "Arpeggio"},
  };
  for (const auto& entry : all) {
    for (const auto& note : entry.first->notes()) {
      for (uint8_t i = 0; i < note.transform_count; ++i) {
        const auto& step = note.transform_steps[i];
        if (!isPitchMove(step.type)) continue;
        ++moved_notes;
        if (note.prov_original_pitch != step.input_pitch) {
          out.push_back(
              {entry.second, note.start_tick, note.prov_original_pitch, step.input_pitch});
        }
        break;  // Only the first move can answer for where the note came from
      }
    }
  }
  return out;
}

std::string describeRewrittenOrigins(const std::vector<RewrittenOrigin>& origins) {
  std::string out;
  for (const auto& o : origins) {
    out += o.track + " tick " + std::to_string(o.tick) + ": origin recorded as " +
           std::to_string(o.recorded_origin) + " but the first move started from " +
           std::to_string(o.first_move_input) + "\n";
  }
  return out;
}

// A note's recorded origin answers one question: which pitch did the generator
// that created it choose. A pass that moves an already-moved note and writes
// its own input reports a transformed pitch as the generator's choice, and the
// moves that came before it read as decisions the line was written with -- a
// leap manufactured by a collision fix becomes a leap the phrase always had.
//
// The check is asked of every track, not of the pass being fixed: the rule is
// a property of the field, so a pass added later has to satisfy it too.
TEST_F(PitchWritebackTest, TheFirstPassToMoveANoteIsTheOneThatRecordsWhereItCameFrom) {
  constexpr uint32_t kSeeds[] = {11, 22, 55, 12345, 20260903};
  constexpr uint8_t kBlueprints[] = {0, 3, 5, 8};
  const CompositionStyle kStyles[] = {CompositionStyle::BackgroundMotif,
                                      CompositionStyle::SynthDriven};

  size_t moved_notes = 0;
  for (CompositionStyle style : kStyles) {
    for (uint8_t blueprint : kBlueprints) {
      for (uint32_t seed : kSeeds) {
        generateArpeggioForwardSong(seed, blueprint, style);
        auto origins = findRewrittenOrigins(sketch_.getSong(), moved_notes);
        EXPECT_TRUE(origins.empty())
            << "style=" << static_cast<int>(style) << " blueprint=" << static_cast<int>(blueprint)
            << " seed=" << seed << "\n"
            << describeRewrittenOrigins(origins);
      }
    }
  }
  // Without notes that a pass moved at all, the assertion above is vacuous.
  EXPECT_GT(moved_notes, 0u);
}
#endif  // MIDISKETCH_NOTE_PROVENANCE

// ============================================================================
// Bass / vocal pitch class doubling
// ============================================================================

// Everything below is read only by the guarded test at the end of this
// section, which needs the recorded origin to say whose note it is.
#ifdef MIDISKETCH_NOTE_PROVENANCE

/// Two octaves: closer than this, a shared pitch class reads as the bass
/// doubling the vocal instead of supporting it, and the low end goes hollow.
constexpr int kMinOctaveSeparation = 24;

/// Which bass notes a scan covers, and with it the pitch the rule is asked of.
///
/// The bass writer clears the doubling on the pitch it hands to the note
/// creation path, so that pitch is the one it answers for. On a note nothing
/// moved the two are the same pitch. On a note a pass moved they are not, and
/// the pass owns what now sounds -- but the writer's choice is still recorded,
/// so the rule can still be asked of it. Splitting the scan is what keeps the
/// moved notes in view: a single scan can only pick one of the two pitches, and
/// dropping the notes it cannot answer for takes them out of every test.
enum class BassPopulation {
  Unmoved,    ///< The pitch that sounds is the one its writer chose
  Moved,      ///< A pass replaced the pitch; the recorded origin is the writer's
  Relocated,  ///< The bass writer never wrote this note here; only what it
              ///< sounds was decided at this position
};

/// Whether the bass writer is the one that chose this note's recorded origin.
///
/// The voice limiter can place a bass note by copying the bar before it, and
/// such a copy starts its history over at the pitch it was copied with. That
/// pitch is a decision the writer made a bar earlier, against the vocal that
/// was singing there, so asking whether it clears the vocal here asks the
/// writer a question it was never posed. What was decided at this position is
/// the pitch the copy ended up sounding, and that is what the rule is asked of.
bool writerChoseTheOrigin(const NoteEvent& note) {
  return static_cast<NoteSource>(note.prov_source) == NoteSource::BassPattern;
}

struct CloseDoubling {
  Tick tick;
  int bass_pitch;      ///< The pitch the rule was asked of
  int sounding_pitch;  ///< What the note sounds, which a pass may have replaced
  int vocal_pitch;
};

/// The span the bass pass itself verified. The articulation gate is the last
/// step of bass generation, so its recorded output is the note's length at that
/// point; a later pass that lengthens the note can pull in a vocal note the
/// bass never saw, which is a different write-back path. Durations above the
/// byte the step can hold are not recoverable, so the current length is used.
Tick spanAtGeneration(const NoteEvent& note) {
  for (uint8_t i = 0; i < note.transform_count; ++i) {
    const auto& step = note.transform_steps[i];
    if (step.type == TransformStepType::ArticulationGate && step.output_pitch < 255) {
      return step.output_pitch;
    }
  }
  return note.duration;
}

/// Two notes have to sound together for at least this long to be heard as one
/// doubled pitch. Humanization nudges onsets by a handful of ticks, so two
/// adjacent notes can end up with their edges crossing; that is the timing
/// jitter touching, not a doubling.
constexpr Tick kAudibleOverlap = TICK_32ND;

/// Every bass note is compared against every vocal note overlapping the span it
/// sounds, not against a single sample at its onset: a vocal note entering
/// halfway through the bass note doubles it just as audibly.
///
/// @param population Which notes to scan; also selects the pitch asked of them.
/// @param scanned Number of notes the population held, so a caller can tell a
///        clean scan from one that found nothing to scan.
std::vector<CloseDoubling> findCloseDoublings(const MidiTrack& bass, const MidiTrack& vocal,
                                              BassPopulation population, size_t& scanned) {
  std::vector<CloseDoubling> out;
  for (const auto& bass_note : bass.notes()) {
    bool moved = bass_note.hasValidProvenance() && bass_note.prov_original_pitch != bass_note.note;
    BassPopulation belongs_to =
        !moved
            ? BassPopulation::Unmoved
            : (writerChoseTheOrigin(bass_note) ? BassPopulation::Moved : BassPopulation::Relocated);
    if (belongs_to != population) continue;
    int bass_pitch =
        (population == BassPopulation::Moved) ? bass_note.prov_original_pitch : bass_note.note;
    ++scanned;
    Tick bass_end = bass_note.start_tick + spanAtGeneration(bass_note);
    for (const auto& vocal_note : vocal.notes()) {
      Tick vocal_end = vocal_note.start_tick + vocal_note.duration;
      if (bass_note.start_tick >= vocal_end || vocal_note.start_tick >= bass_end) continue;
      Tick overlap =
          std::min(bass_end, vocal_end) - std::max(bass_note.start_tick, vocal_note.start_tick);
      if (overlap < kAudibleOverlap) continue;
      if ((bass_pitch % 12) != (vocal_note.note % 12)) continue;
      int separation = std::abs(bass_pitch - static_cast<int>(vocal_note.note));
      if (separation < kMinOctaveSeparation) {
        out.push_back({bass_note.start_tick, bass_pitch, bass_note.note, vocal_note.note});
        break;
      }
    }
  }
  return out;
}

std::string describeCloseDoublings(const std::vector<CloseDoubling>& doublings) {
  std::string out;
  for (const auto& d : doublings) {
    out += "tick " + std::to_string(d.tick) + ": bass " + std::to_string(d.bass_pitch) +
           " doubles vocal " + std::to_string(d.vocal_pitch);
    if (d.sounding_pitch != d.bass_pitch) {
      out += ", now sounding " + std::to_string(d.sounding_pitch);
    }
    out += "\n";
  }
  return out;
}
#endif  // MIDISKETCH_NOTE_PROVENANCE

// ============================================================================
// Secondary dominant voicing
// ============================================================================

struct CrossRelation {
  std::string track;
  Tick tick;
  int natural_third;
  int raised_third;
};

/// Every note of every track that can carry pitch, for the "is the raised third
/// sounding at the same time" side of the check.
std::vector<const MidiTrack*> pitchedTracks(const Song& song) {
  return {&song.vocal(), &song.motif(),  &song.aux(),     &song.bass(),
          &song.chord(), &song.guitar(), &song.arpeggio()};
}

/// Collect cross relations contributed by one track: a note on the natural
/// third of a degree the timeline has replaced with a secondary dominant, while
/// some other note sounds the raised third at the same time. The two thirds a
/// semitone apart in the same instant is the audible defect; the same pitch a
/// beat later is voice leading.
std::vector<CrossRelation> findCrossRelations(const Song& song, const IHarmonyContext& harmony,
                                              const MidiTrack& track, const char* name) {
  std::vector<CrossRelation> out;
  const auto all_tracks = pitchedTracks(song);
  Tick total = song.arrangement().totalTicks();
  Tick current = 0;
  while (current < total) {
    Tick next = harmony.getNextChordEntryTick(current);
    if (next == 0 || next <= current) next = total;
    if (!harmony.isSecondaryDominantAt(current)) {
      current = next;
      continue;
    }

    ChordTones planned = harmony.getChordTonesAt(current);
    ChordTones diatonic = getChordTones(harmony.getChordDegreeAt(current));
    if (planned.count < 2 || diatonic.count < 2 ||
        planned.pitch_classes[1] == diatonic.pitch_classes[1]) {
      current = next;  // No third was altered, so no cross relation is possible
      continue;
    }
    int raised_third = planned.pitch_classes[1];
    int natural_third = diatonic.pitch_classes[1];

    for (const auto& note : track.notes()) {
      Tick note_end = note.start_tick + note.duration;
      if (note_end <= current || note.start_tick >= next) continue;
      if (note.note % 12 != natural_third) continue;

      for (const MidiTrack* other : all_tracks) {
        for (const auto& raised : other->notes()) {
          Tick raised_end = raised.start_tick + raised.duration;
          if (raised_end <= current || raised.start_tick >= next) continue;
          if (raised.note % 12 != raised_third) continue;
          if (raised_end <= note.start_tick || note_end <= raised.start_tick) continue;
          out.push_back({name, note.start_tick, note.note, raised.note});
        }
      }
    }
    current = next;
  }
  return out;
}

std::string describeCrossRelations(const std::vector<CrossRelation>& relations) {
  std::string out;
  for (const auto& r : relations) {
    out += r.track + " tick " + std::to_string(r.tick) + ": pitch " +
           std::to_string(r.natural_third) + " sounds against the raised third " +
           std::to_string(r.raised_third) + "\n";
  }
  return out;
}

// Root, fifth, octave, approach, slap and ghost notes all have to clear a vocal
// pitch class by two octaves. A single sample at the bar root cannot see a
// vocal note that enters inside the bass note, so the whole span is scanned.
//
// Every bass creation site clears the doubling on the pitch it hands to the
// note creation path, so the rule is a property of the pitch the bass writer
// chose, whether or not that pitch is still the one sounding. A note the voice
// limiter placed by copying the bar before it has no such pitch -- its recorded
// origin is a decision made at another position -- so there the rule is asked of
// what it sounds. All three populations are scanned; only the pitch the rule is
// asked of differs.
//
// Not verified in the shipping build: the recorded origin is what names the
// pitch the writer chose, and that build records none, so it cannot tell the
// two populations apart or say which pitch either one speaks for.
#ifdef MIDISKETCH_NOTE_PROVENANCE
TEST_F(PitchWritebackTest, BassDoesNotDoubleAVocalPitchClassWithinTwoOctaves) {
  constexpr uint32_t kSeeds[] = {12345, 777, 20260903, 424242, 31337};
  constexpr uint8_t kBlueprints[] = {0, 1, 2, 3, 4, 9};

  size_t songs_scanned = 0;
  size_t unmoved_notes = 0;
  size_t moved_notes = 0;
  size_t relocated_notes = 0;
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const Song& song = sketch_.getSong();
      ASSERT_FALSE(song.bass().empty())
          << "blueprint=" << static_cast<int>(blueprint) << " seed=" << seed;
      ASSERT_FALSE(song.vocal().empty())
          << "blueprint=" << static_cast<int>(blueprint) << " seed=" << seed;

      auto unmoved =
          findCloseDoublings(song.bass(), song.vocal(), BassPopulation::Unmoved, unmoved_notes);
      EXPECT_TRUE(unmoved.empty())
          << "blueprint=" << static_cast<int>(blueprint) << " seed=" << seed << "\n"
          << describeCloseDoublings(unmoved);

      auto moved =
          findCloseDoublings(song.bass(), song.vocal(), BassPopulation::Moved, moved_notes);
      EXPECT_TRUE(moved.empty())
          << "the pitch the bass writer chose has to clear the vocal even where a"
             " pass replaced it\nblueprint="
          << static_cast<int>(blueprint) << " seed=" << seed << "\n"
          << describeCloseDoublings(moved);

      auto relocated =
          findCloseDoublings(song.bass(), song.vocal(), BassPopulation::Relocated, relocated_notes);
      EXPECT_TRUE(relocated.empty())
          << "a bass note the voice limiter placed has to clear the vocal where it"
             " was placed\nblueprint="
          << static_cast<int>(blueprint) << " seed=" << seed << "\n"
          << describeCloseDoublings(relocated);
      ++songs_scanned;
    }
  }
  EXPECT_EQ(songs_scanned, std::size(kSeeds) * std::size(kBlueprints));
  // No scan says anything about a population it never had a note from.
  EXPECT_GT(unmoved_notes, 0u);
  EXPECT_GT(moved_notes, 0u);
  EXPECT_GT(relocated_notes, 0u);
}
#endif  // MIDISKETCH_NOTE_PROVENANCE

// Every pitched track picks its tones from the shared timeline, so a secondary
// dominant registered at a tick is voiced with its own third rather than with
// the diatonic triad that shares its degree. Rebuilding the triad from the
// degree put the natural third under the chord track's raised one.
//
// The guitar is asked here too. It is not the track this corpus caught, but it
// is a track that voices vertical harmony from the timeline, and the property
// is the same one: the corpus tests are what decide which routes reach it, and
// leaving a pitched track out of the assertion is how a route stays unwatched.
TEST_F(PitchWritebackTest, NoTrackSoundsACrossRelationOverASecondaryDominant) {
  constexpr uint32_t kSeeds[] = {12345, 777, 20260903};
  constexpr uint8_t kBlueprints[] = {0, 3, 4, 9};

  size_t songs_scanned = 0;
  size_t secondary_dominant_spans = 0;
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const Song& song = sketch_.getSong();
      const IHarmonyContext& harmony = sketch_.getHarmonyContext();

      auto bass_relations = findCrossRelations(song, harmony, song.bass(), "Bass");
      auto aux_relations = findCrossRelations(song, harmony, song.aux(), "Aux");
      auto guitar_relations = findCrossRelations(song, harmony, song.guitar(), "Guitar");
      EXPECT_TRUE(bass_relations.empty())
          << "blueprint=" << static_cast<int>(blueprint) << " seed=" << seed << "\n"
          << describeCrossRelations(bass_relations);
      EXPECT_TRUE(aux_relations.empty())
          << "blueprint=" << static_cast<int>(blueprint) << " seed=" << seed << "\n"
          << describeCrossRelations(aux_relations);
      EXPECT_TRUE(guitar_relations.empty())
          << "blueprint=" << static_cast<int>(blueprint) << " seed=" << seed << "\n"
          << describeCrossRelations(guitar_relations);

      Tick total = song.arrangement().totalTicks();
      for (Tick t = 0; t < total; t += TICKS_PER_BEAT) {
        if (harmony.isSecondaryDominantAt(t)) ++secondary_dominant_spans;
      }
      ++songs_scanned;
    }
  }
  EXPECT_EQ(songs_scanned, std::size(kSeeds) * std::size(kBlueprints));
  // Without secondary dominants in the corpus the assertions above are vacuous.
  EXPECT_GT(secondary_dominant_spans, 0u);
}

}  // namespace
}  // namespace midisketch
