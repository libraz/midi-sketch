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

#include <vector>

#include "core/arrangement.h"
#include "core/basic_types.h"
#include "core/chord.h"
#include "core/harmony_context.h"
#include "core/song.h"
#include "core/timing_constants.h"

namespace midisketch {

void trimClashingNoteTails(Song& song, const IHarmonyContext& harmony);

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

}  // namespace midisketch
