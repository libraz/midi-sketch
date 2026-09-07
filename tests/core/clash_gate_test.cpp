/**
 * @file clash_gate_test.cpp
 * @brief Tests for the last gate that runs before the notes are emitted.
 *
 * Every pitch-moving pass runs before this gate, so it decides what a listener
 * hears when two of them reconcile into each other. Its dissonance test and its
 * measure of how long two notes actually clash both have to agree with the
 * analyzer, or a clash it was written to remove passes it untouched.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>

#include "core/arrangement.h"
#include "core/basic_types.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/harmony_context.h"
#include "core/pitch_utils.h"
#include "core/post_processor.h"
#include "core/preset_types.h"
#include "core/song.h"
#include "core/timing_constants.h"
#include "core/track_clash_gates.h"
#include "midisketch.h"

namespace midisketch {
namespace {

Arrangement singleSection() {
  Section section{};
  section.type = SectionType::A;
  section.name = "A";
  section.bars = 4;
  section.start_bar = 0;
  section.start_tick = 0;
  return Arrangement({section});
}

/// @brief The degree the timeline states at a tick, so a test can pick a pair
///        whose interval the analyzer calls dissonant there.
int8_t degreeAt(const HarmonyContext& harmony, Tick tick) { return harmony.getChordDegreeAt(tick); }

}  // namespace

TEST(ClashGateTest, ASustainedNoteIsTrimmedForAShortClashInsideIt) {
  Arrangement arrangement = singleSection();
  HarmonyContext harmony;
  harmony.initialize(arrangement, getChordProgression(0), Mood::StraightPop);

  // A tritone is dissonant everywhere except V and vii degrees, so pick a tick
  // whose chord is neither and build the pair there.
  Tick clash_tick = 0;
  bool found = false;
  for (Tick tick = TICK_QUARTER; tick < 4 * TICKS_PER_BAR; tick += TICK_QUARTER) {
    const int8_t degree = degreeAt(harmony, tick);
    const int normalized = ((degree % 7) + 7) % 7;
    if (normalized != 4 && normalized != 6 && degreeAt(harmony, tick - TICK_QUARTER) == degree) {
      clash_tick = tick;
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found) << "no tick with a chord a tritone is dissonant over";

  // The motif sustains from a quarter before the clash to well past it; the
  // chord states one short stab inside that sustain, a tritone away. The two
  // sound together only for the length of the stab, which is what the analyzer
  // counts and what the gate has to measure.
  const Tick motif_start = clash_tick - TICK_QUARTER;
  const Tick motif_duration = 4 * TICK_QUARTER;
  const uint8_t motif_pitch = 71;
  const uint8_t chord_pitch = 65;  // tritone below

  Song song;
  song.motif().addNote(NoteEventBuilder::create(motif_start, motif_duration, motif_pitch, 90));
  song.chord().addNote(NoteEventBuilder::create(clash_tick, TICK_EIGHTH, chord_pitch, 90));
  harmony.registerTrack(song.motif(), TrackRole::Motif);
  harmony.registerTrack(song.chord(), TrackRole::Chord);

  trimClashingNoteTails(song, harmony);

  ASSERT_EQ(song.motif().notes().size(), 1u);
  EXPECT_EQ(song.motif().notes()[0].duration, TICK_QUARTER)
      << "the motif should end where the stab begins; measuring the overlap to "
         "the motif's own end reports a clash far longer than the two notes share";
  EXPECT_EQ(song.chord().notes().size(), 1u) << "the chord stab is not the note to shorten";
}

TEST(ClashGateTest, TheGateLeavesTheTritoneASeventhChordIsBuiltOn) {
  // The scale degree alone cannot answer for a chord the timeline has replaced.
  // Registering a dominant seventh on vi makes A7, whose third and seventh are
  // a tritone apart -- the interval that makes it a dominant. Judging by degree
  // says vi is not V, so the gate shortened the very note the extension was
  // planned for. Asking whether both voices belong to the sounding chord is
  // what the analyzer does, and a gate stricter than the report takes music
  // nobody asked it to take.
  Arrangement arrangement = singleSection();
  HarmonyContext harmony;
  harmony.initialize(arrangement, getChordProgression(0), Mood::StraightPop);

  Tick chord_tick = 0;
  bool found = false;
  for (Tick tick = 0; tick < 4 * TICKS_PER_BAR; tick += TICKS_PER_BAR) {
    if (((degreeAt(harmony, tick) % 7) + 7) % 7 == 5) {
      chord_tick = tick;
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found) << "the fixture progression must state a vi chord somewhere";
  harmony.registerChordExtension(chord_tick, chord_tick + TICKS_PER_BAR, ChordExtension::Dom7);

  const ChordTones tones = harmony.getChordTonesAt(chord_tick);
  const uint8_t third = 73;    // C#5, the third of A7
  const uint8_t seventh = 67;  // G4, its seventh
  ASSERT_TRUE(bothVoicesAreChordTones(third, seventh, tones))
      << "the fixture must put both voices inside the registered chord";

  const Tick stab_tick = chord_tick + TICK_QUARTER;
  Song song;
  song.motif().addNote(NoteEventBuilder::create(chord_tick, 4 * TICK_QUARTER, seventh, 90));
  song.chord().addNote(NoteEventBuilder::create(stab_tick, TICK_EIGHTH, third, 90));
  harmony.registerTrack(song.motif(), TrackRole::Motif);
  harmony.registerTrack(song.chord(), TrackRole::Chord);

  trimClashingNoteTails(song, harmony);

  ASSERT_EQ(song.motif().notes().size(), 1u);
  EXPECT_EQ(song.motif().notes()[0].duration, 4 * TICK_QUARTER)
      << "the seventh should keep its length: the stab under it is the chord";
  EXPECT_EQ(song.chord().notes().size(), 1u);
}

TEST(ClashGateTest, TheGateStillTakesASemitoneBetweenChordTones) {
  // The one interval the sounding chord does not account for. Cmaj7 owns a B
  // and the C above it, and the pair still beats however the chord is spelled.
  Arrangement arrangement = singleSection();
  HarmonyContext harmony;
  harmony.initialize(arrangement, getChordProgression(0), Mood::StraightPop);

  Tick chord_tick = 0;
  bool found = false;
  for (Tick tick = 0; tick < 4 * TICKS_PER_BAR; tick += TICKS_PER_BAR) {
    if (((degreeAt(harmony, tick) % 7) + 7) % 7 == 0) {
      chord_tick = tick;
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found) << "the fixture progression must state a I chord somewhere";
  harmony.registerChordExtension(chord_tick, chord_tick + TICKS_PER_BAR, ChordExtension::Maj7);

  const uint8_t root = 72;     // C5
  const uint8_t seventh = 71;  // B4, a semitone under it
  ASSERT_TRUE(bothVoicesAreChordTones(root, seventh, harmony.getChordTonesAt(chord_tick)));

  const Tick stab_tick = chord_tick + TICK_QUARTER;
  Song song;
  song.motif().addNote(NoteEventBuilder::create(chord_tick, 4 * TICK_QUARTER, root, 90));
  song.chord().addNote(NoteEventBuilder::create(stab_tick, TICK_EIGHTH, seventh, 90));
  harmony.registerTrack(song.motif(), TrackRole::Motif);
  harmony.registerTrack(song.chord(), TrackRole::Chord);

  trimClashingNoteTails(song, harmony);

  ASSERT_EQ(song.motif().notes().size(), 1u);
  EXPECT_EQ(song.motif().notes()[0].duration, TICK_QUARTER)
      << "belonging to the chord does not excuse a minor 2nd";
}

TEST(ClashGateTest, TheRegistryDescribesTheNotesTheGateLeaves) {
  Arrangement arrangement = singleSection();
  HarmonyContext harmony;
  harmony.initialize(arrangement, getChordProgression(0), Mood::StraightPop);

  Tick clash_tick = 0;
  bool found = false;
  for (Tick tick = 0; tick < 4 * TICKS_PER_BAR; tick += TICK_QUARTER) {
    const int normalized = ((degreeAt(harmony, tick) % 7) + 7) % 7;
    if (normalized != 4 && normalized != 6) {
      clash_tick = tick;
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found);

  // A short arpeggio stab against a longer motif note at the same onset. The
  // gate has no way to move either, so it drops the more decorative of the two.
  Song song;
  song.motif().addNote(NoteEventBuilder::create(clash_tick, TICK_QUARTER, 71, 90));
  song.arpeggio().addNote(NoteEventBuilder::create(clash_tick, TICK_EIGHTH, 65, 90));
  harmony.registerTrack(song.motif(), TrackRole::Motif);
  harmony.registerTrack(song.arpeggio(), TrackRole::Arpeggio);
  ASSERT_FALSE(
      harmony.getSoundingPitches(clash_tick, clash_tick + TICK_EIGHTH, TrackRole::Motif).empty())
      << "the arpeggio stab has to be registered for the test to mean anything";

  trimClashingNoteTails(song, harmony);

  ASSERT_TRUE(song.arpeggio().notes().empty()) << "the decorative stab should have been dropped";
  // Reading from any other track's point of view, the dropped pitch must be gone
  // from the registry too: a pass that answers about notes it no longer holds
  // sends the next consumer to avoid a clash that is not there.
  const auto sounding =
      harmony.getSoundingPitches(clash_tick, clash_tick + TICK_EIGHTH, TrackRole::Motif);
  for (uint8_t pitch : sounding) {
    EXPECT_NE(pitch, 65) << "the registry still reports the note the gate deleted";
  }
}

TEST(ClashGateTest, ATrackSustainingIntoItsOwnNextNoteIsTrimmed) {
  // One instrument holding a note into the next note it plays states the same
  // interval two instruments would, and nothing else answers for it: the
  // same-track pass ahead of this gate judges voices that begin together, so a
  // tail is the one shape it never sees.
  Arrangement arrangement = singleSection();
  HarmonyContext harmony;
  harmony.initialize(arrangement, getChordProgression(0), Mood::StraightPop);

  const Tick held_start = TICK_QUARTER;
  Song song;
  song.aux().addNote(NoteEventBuilder::create(held_start, 4 * TICK_QUARTER, 72, 90));
  song.aux().addNote(NoteEventBuilder::create(held_start + TICK_QUARTER, TICK_EIGHTH, 71, 90));
  harmony.registerTrack(song.aux(), TrackRole::Aux);

  trimClashingNoteTails(song, harmony);

  ASSERT_EQ(song.aux().notes().size(), 2u) << "a tail is shortened, never deleted";
  EXPECT_EQ(song.aux().notes()[0].duration, TICK_QUARTER)
      << "the held note should end where the semitone under it begins";
  EXPECT_EQ(song.aux().notes()[1].duration, TICK_EIGHTH);
}

TEST(ClashGateTest, ShorteningANoteExposesTheOverlapTheCapHadExcused) {
  // The cap reads a long overlap as a simultaneity somebody chose rather than a
  // tail that slipped out, and leaves it. Trimming the same note for a later
  // neighbour then shortens the first overlap into the cap's range, so a single
  // sweep walks away from a clash its own rule condemns.
  Arrangement arrangement = singleSection();
  HarmonyContext harmony;
  harmony.initialize(arrangement, getChordProgression(0), Mood::StraightPop);

  Song song;
  // Held note, then two semitone neighbours. The first overlaps it by 960 ticks
  // (past the cap), the second by 480 (at the cap, so it is trimmed) -- and that
  // trim leaves the first overlapping by 480 as well.
  song.aux().addNote(NoteEventBuilder::create(0, 3 * TICK_QUARTER, 72, 90));
  song.aux().addNote(NoteEventBuilder::create(TICK_QUARTER, 3 * TICK_QUARTER, 71, 90));
  song.aux().addNote(NoteEventBuilder::create(TICK_HALF, 3 * TICK_QUARTER, 71, 90));
  harmony.registerTrack(song.aux(), TrackRole::Aux);

  trimClashingNoteTails(song, harmony);

  ASSERT_EQ(song.aux().notes().size(), 3u);
  EXPECT_EQ(song.aux().notes()[0].duration, TICK_QUARTER)
      << "the held note has to end at the first neighbour, not at the second";
}

TEST(ClashGateTest, AConsonantStabInsideASustainIsLeftAlone) {
  Arrangement arrangement = singleSection();
  HarmonyContext harmony;
  harmony.initialize(arrangement, getChordProgression(0), Mood::StraightPop);

  const Tick motif_start = TICK_QUARTER;
  const Tick motif_duration = 4 * TICK_QUARTER;

  Song song;
  // A perfect fifth is consonant wherever it appears, so nothing may be cut.
  song.motif().addNote(NoteEventBuilder::create(motif_start, motif_duration, 72, 90));
  song.chord().addNote(NoteEventBuilder::create(motif_start + TICK_QUARTER, TICK_EIGHTH, 67, 90));
  harmony.registerTrack(song.motif(), TrackRole::Motif);
  harmony.registerTrack(song.chord(), TrackRole::Chord);

  trimClashingNoteTails(song, harmony);

  ASSERT_EQ(song.motif().notes().size(), 1u);
  EXPECT_EQ(song.motif().notes()[0].duration, motif_duration);
}

TEST(ClashGateTest, TheDeletePassHandsATailToTheGateAndTheGateShortensIt) {
  // Deleting is the only answer removeClashingNotesAgainstReference has, and a
  // note whose last ticks are clipped by one arriving late does not deserve it.
  // The pair belongs to this gate, which stops the earlier note where the later
  // one begins. The overlap here is exactly the longest the gate accepts, the
  // boundary the delete pass used to take whole.
  Arrangement arrangement = singleSection();
  HarmonyContext harmony;
  harmony.initialize(arrangement, getChordProgression(0), Mood::StraightPop);

  const uint8_t chord_pitch = 65;  // F4
  const uint8_t vocal_pitch = 59;  // B3, a tritone below
  // What the gate will leave of the chord note. Stated as its own value so the
  // assertion below can be the equation the gate states rather than a number.
  const Tick remainder = TICK_QUARTER;

  // A chord that owns no tritone of its own and does not contain both voices,
  // so the pair is one both passes call a clash.
  Tick chord_tick = 0;
  bool found = false;
  for (Tick tick = 0; tick + remainder + kTailGateMaxOverlap < 4 * TICKS_PER_BAR;
       tick += TICK_QUARTER) {
    const int normalized = ((degreeAt(harmony, tick + remainder) % 7) + 7) % 7;
    if (normalized == 4 || normalized == 6) continue;
    if (bothVoicesAreChordTones(chord_pitch, vocal_pitch,
                                harmony.getChordTonesAt(tick + remainder))) {
      continue;
    }
    chord_tick = tick;
    found = true;
    break;
  }
  ASSERT_TRUE(found) << "the fixture progression must state a chord a tritone clashes over";

  Song song;
  song.chord().addNote(
      NoteEventBuilder::create(chord_tick, remainder + kTailGateMaxOverlap, chord_pitch, 90));
  song.vocal().addNote(
      NoteEventBuilder::create(chord_tick + remainder, kTailGateMaxOverlap, vocal_pitch, 90));
  harmony.registerTrack(song.chord(), TrackRole::Chord);
  harmony.registerTrack(song.vocal(), TrackRole::Vocal);

  PostProcessor::fixTrackVocalClashes(song.chord(), song.vocal(), TrackRole::Chord, &harmony);
  ASSERT_EQ(song.chord().notes().size(), 1u)
      << "a tail this gate answers for is not the delete pass's note to remove";

  trimClashingNoteTails(song, harmony);

  ASSERT_EQ(song.chord().notes().size(), 1u);
  ASSERT_EQ(song.vocal().notes().size(), 1u) << "the vocal is not the voice that gives way here";
  EXPECT_EQ(song.chord().notes()[0].duration,
            song.vocal().notes()[0].start_tick - song.chord().notes()[0].start_tick)
      << "the gate should stop the chord note exactly where the vocal begins; leaving it whole "
         "means the clash the delete pass was holding back now sounds";
}

TEST(ClashGateTest, NoGeneratedSongLeavesAClashTheGateItselfWouldTake) {
  // The gate's own rule, read off what it emits. A pair it would act on is a
  // tail short enough to be accidental, an interval the analyzer flags, and no
  // sounding chord that accounts for both voices -- between two tracks or
  // inside one. None may survive to the caller: one that does is the rule
  // spelled but not reaching the output.
  size_t songs = 0;
  for (int blueprint = 0; blueprint < 10; ++blueprint) {
    for (uint32_t seed : {11u, 22u, 33u}) {
      SongConfig config = createDefaultSongConfig(0);
      config.seed = seed;
      config.blueprint_id = static_cast<uint8_t>(blueprint);

      MidiSketch sketch;
      sketch.generateFromConfig(config);
      const Song& song = sketch.getSong();
      const IHarmonyContext& harmony = sketch.getHarmonyContext();
      ++songs;

      const std::pair<const char*, const MidiTrack*> tracks[] = {
          {"Vocal", &song.vocal()},  {"Chord", &song.chord()}, {"Bass", &song.bass()},
          {"Motif", &song.motif()},  {"Aux", &song.aux()},     {"Arpeggio", &song.arpeggio()},
          {"Guitar", &song.guitar()}};

      for (const auto& [earlier_name, earlier] : tracks) {
        for (const auto& [later_name, later] : tracks) {
          for (const NoteEvent& a : earlier->notes()) {
            const Tick a_end = a.start_tick + a.duration;
            for (const NoteEvent& b : later->notes()) {
              if (&a == &b) continue;
              if (b.start_tick <= a.start_tick || b.start_tick >= a_end) continue;
              const Tick b_end = b.start_tick + b.duration;
              if (std::min(a_end, b_end) - b.start_tick > TICK_QUARTER) continue;
              if (b.start_tick - a.start_tick < TICK_32ND) continue;
              const int semitones = std::abs(static_cast<int>(a.note) - static_cast<int>(b.note));
              if (semitones > 24) continue;
              if (chordExcusesFlaggedPair(semitones, a.note, b.note,
                                          harmony.getChordTonesAt(b.start_tick))) {
                continue;
              }
              if (!isDissonantActualInterval(semitones, harmony.getChordDegreeAt(b.start_tick))) {
                continue;
              }
              ADD_FAILURE() << "blueprint " << blueprint << " seed " << seed << ": " << earlier_name
                            << " " << static_cast<int>(a.note) << " at " << a.start_tick
                            << " still sounds into " << later_name << " "
                            << static_cast<int>(b.note) << " at " << b.start_tick << " ("
                            << semitones << " semitones)";
            }
          }
        }
      }
    }
  }
  ASSERT_EQ(songs, 30u);
}

}  // namespace midisketch
