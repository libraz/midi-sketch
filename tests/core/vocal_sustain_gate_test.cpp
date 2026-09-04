/**
 * @file vocal_sustain_gate_test.cpp
 * @brief Tests for the pass that releases a vocal note held into a chord that
 *        has no place for it.
 *
 * A melody note carried over a chord change is ordinary singing, and only worth
 * touching when the new chord cannot account for the pitch at all. Which notes
 * those are is one predicate's answer, shared with the report that raises the
 * same sustain, so the pass cannot cut melody the report would never mention.
 * What it does cut, it releases a hair before the change rather than at it, and
 * only when enough of the note is left to still be a note.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "core/arrangement.h"
#include "core/basic_types.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/harmony_context.h"
#include "core/midi_track.h"
#include "core/timing_constants.h"

namespace midisketch {

void trimVocalSustainsAtUnsafeChordChanges(MidiTrack& vocal, const IHarmonyContext& harmony);

namespace {

/// The note is released this far before the chord change, so the release is
/// heard as a phrase ending rather than as part of the new chord.
constexpr Tick kReleaseGap = 30;

Arrangement singleSection() {
  Section section{};
  section.type = SectionType::A;
  section.name = "A";
  section.bars = 4;
  section.start_bar = 0;
  section.start_tick = 0;
  return Arrangement({section});
}

HarmonyContext fourChordPop() {
  HarmonyContext harmony;
  harmony.initialize(singleSection(), getChordProgression(0), Mood::StraightPop);
  return harmony;
}

/// @brief The first chord change with room on both sides for a note to be held
///        across it: `lead_in` still under the old chord, `hold_over` under the
///        new one. Where the progression puts its changes is not this pass's
///        business, so the tests ask the timeline instead of naming a bar.
bool findChordChange(const HarmonyContext& harmony, Tick lead_in, Tick hold_over,
                     Tick* change_tick) {
  const Tick song_end = 4 * TICKS_PER_BAR;
  for (Tick tick = lead_in; tick + hold_over <= song_end; tick += TICK_SIXTEENTH) {
    const int8_t old_degree = harmony.getChordDegreeAt(tick - TICK_SIXTEENTH);
    const int8_t new_degree = harmony.getChordDegreeAt(tick);
    if (old_degree == new_degree) {
      continue;
    }
    bool steady = true;
    for (Tick t = tick - lead_in; t < tick; t += TICK_SIXTEENTH) {
      steady = steady && harmony.getChordDegreeAt(t) == old_degree;
    }
    for (Tick t = tick; t < tick + hold_over; t += TICK_SIXTEENTH) {
      steady = steady && harmony.getChordDegreeAt(t) == new_degree;
    }
    if (!steady) {
      continue;
    }
    *change_tick = tick;
    return true;
  }
  return false;
}

/// @brief The flat seventh over a degree: a pitch class the diatonic chord
///        neither contains nor offers as a colour, so only a dominant seventh
///        registered on the timeline can bring it into the chord.
int flatSeventhOf(int8_t degree) { return ((degreeToSemitone(degree) + 10) % 12 + 12) % 12; }

/// @brief Assert the fixture pitch really is one the plain chord cannot account
///        for, so a test about the extension is not passing for another reason.
void expectOutsideTheDiatonicChord(int8_t degree, int pitch_class) {
  const ChordTones tones = getChordTones(degree);
  ASSERT_EQ(std::find(tones.begin(), tones.end(), pitch_class), tones.end())
      << "the fixture pitch must not be a tone of the chord itself";
  const std::vector<int> tensions = getAvailableTensionPitchClasses(degree);
  ASSERT_EQ(std::find(tensions.begin(), tensions.end(), pitch_class), tensions.end())
      << "nor a tension the chord offers";
}

/// @brief A vocal pitch of the given pitch class, in the octave a melody sits in.
uint8_t vocalPitch(int pitch_class) { return static_cast<uint8_t>(60 + pitch_class); }

}  // namespace

TEST(VocalSustainGateTest, ASustainIntoAChordThatColoursItKeepsItsLength) {
  // The chord a note is held into is the one the timeline states there, not the
  // one its scale degree would build. A dominant seventh planned over the new
  // chord makes the held pitch one of that chord's own tones, and singing a
  // tone of the chord across the bar line is the phrasing the extension was
  // planned for -- judging by degree alone released the very note it coloured.
  HarmonyContext harmony = fourChordPop();

  const Tick lead_in = TICK_QUARTER;
  const Tick hold_over = TICK_QUARTER;
  Tick change_tick = 0;
  ASSERT_TRUE(findChordChange(harmony, lead_in, hold_over, &change_tick))
      << "the fixture progression must change chord with room on both sides";

  const int8_t new_degree = harmony.getChordDegreeAt(change_tick);
  const int pitch_class = flatSeventhOf(new_degree);
  ASSERT_NO_FATAL_FAILURE(expectOutsideTheDiatonicChord(new_degree, pitch_class));
  harmony.registerChordExtension(change_tick, change_tick + hold_over, ChordExtension::Dom7);

  const Tick start = change_tick - lead_in;
  const Tick duration = lead_in + hold_over;
  MidiTrack vocal;
  vocal.addNote(NoteEventBuilder::create(start, duration, vocalPitch(pitch_class), 90));

  trimVocalSustainsAtUnsafeChordChanges(vocal, harmony);

  ASSERT_EQ(vocal.notes().size(), 1u);
  EXPECT_EQ(vocal.notes()[0].duration, duration)
      << "the pitch is a tone of the seventh chord it is held into";
}

TEST(VocalSustainGateTest, ASustainIntoAChordWithNoPlaceForItIsReleasedBeforeTheChange) {
  // The same phrase over the same change, with nothing registered to account
  // for the pitch. The note is released a hair before the new chord arrives, so
  // the two are never heard together.
  HarmonyContext harmony = fourChordPop();

  const Tick lead_in = TICK_QUARTER;
  const Tick hold_over = TICK_QUARTER;
  Tick change_tick = 0;
  ASSERT_TRUE(findChordChange(harmony, lead_in, hold_over, &change_tick));

  const int8_t new_degree = harmony.getChordDegreeAt(change_tick);
  const int pitch_class = flatSeventhOf(new_degree);
  ASSERT_NO_FATAL_FAILURE(expectOutsideTheDiatonicChord(new_degree, pitch_class));

  const Tick start = change_tick - lead_in;
  MidiTrack vocal;
  vocal.addNote(NoteEventBuilder::create(start, lead_in + hold_over, vocalPitch(pitch_class), 90));

  trimVocalSustainsAtUnsafeChordChanges(vocal, harmony);

  ASSERT_EQ(vocal.notes().size(), 1u);
  EXPECT_EQ(vocal.notes()[0].duration, lead_in - kReleaseGap)
      << "the note should stop just short of the chord it does not belong to";
  EXPECT_EQ(vocal.notes()[0].start_tick, start) << "a release moves the end, never the attack";
}

TEST(VocalSustainGateTest, ANoteTooCloseToTheChangeIsLeftWholeRatherThanCutToAStub) {
  // Releasing this note would leave less of it sounding than an eighth, and a
  // melody note clipped that far is worse than the sustain it was clipped for.
  HarmonyContext harmony = fourChordPop();

  const Tick lead_in = TICK_EIGHTH;
  const Tick hold_over = TICK_HALF;
  Tick change_tick = 0;
  ASSERT_TRUE(findChordChange(harmony, lead_in, hold_over, &change_tick));

  const int8_t new_degree = harmony.getChordDegreeAt(change_tick);
  const int pitch_class = flatSeventhOf(new_degree);
  ASSERT_NO_FATAL_FAILURE(expectOutsideTheDiatonicChord(new_degree, pitch_class));

  // The attack sits an eighth before the change, so a release before it would
  // leave an eighth minus the gap -- under the floor the pass holds to.
  const Tick start = change_tick - lead_in;
  const Tick duration = lead_in + hold_over;
  ASSERT_LT(lead_in, TICK_EIGHTH + kReleaseGap) << "the fixture must sit under the floor";
  ASSERT_GT(duration, TICK_QUARTER) << "and still be long enough for the pass to consider it";

  MidiTrack vocal;
  vocal.addNote(NoteEventBuilder::create(start, duration, vocalPitch(pitch_class), 90));

  trimVocalSustainsAtUnsafeChordChanges(vocal, harmony);

  ASSERT_EQ(vocal.notes().size(), 1u);
  EXPECT_EQ(vocal.notes()[0].duration, duration)
      << "a note that cannot be released without becoming a stub is left as sung";
}

TEST(VocalSustainGateTest, AQuarterNoteIsNeverShortened) {
  // Only a sustain is worth releasing. A quarter that happens to straddle the
  // change is passing motion, and cutting it takes rhythm out of the melody for
  // a clash nobody hears.
  HarmonyContext harmony = fourChordPop();

  const Tick lead_in = 3 * TICK_SIXTEENTH;
  const Tick hold_over = TICK_SIXTEENTH;
  Tick change_tick = 0;
  ASSERT_TRUE(findChordChange(harmony, lead_in, hold_over, &change_tick));

  const int8_t new_degree = harmony.getChordDegreeAt(change_tick);
  const int pitch_class = flatSeventhOf(new_degree);
  ASSERT_NO_FATAL_FAILURE(expectOutsideTheDiatonicChord(new_degree, pitch_class));

  // Held across the change, and far enough past its attack that the release
  // would fire on any longer note.
  const Tick start = change_tick - lead_in;
  ASSERT_GT(lead_in, TICK_EIGHTH + kReleaseGap) << "the floor must not be what saves this note";

  MidiTrack vocal;
  vocal.addNote(NoteEventBuilder::create(start, TICK_QUARTER, vocalPitch(pitch_class), 90));

  trimVocalSustainsAtUnsafeChordChanges(vocal, harmony);

  ASSERT_EQ(vocal.notes().size(), 1u);
  EXPECT_EQ(vocal.notes()[0].duration, TICK_QUARTER) << "a quarter is not a sustain";
}

}  // namespace midisketch
