/**
 * @file motif_relocation_palette_test.cpp
 * @brief The chord a motif note is relocated onto when a pass has to move it.
 *
 * Three passes move a motif note: the vocal-clash resolver, the register
 * crossing resolver, and the run breaker. All three land the note on a chord
 * tone, and the chord they may read is the one the timeline states, not the one
 * the scale degree builds. A dominant seventh registered on a minor degree
 * turns vi into A7, whose third is major and whose seventh is flat; the
 * degree's own triad has neither. Relocating onto the triad there does not
 * merely miss a colour, it puts the note a semitone under the third the chord
 * is sounding.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

#include "core/arrangement.h"
#include "core/basic_types.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/harmony_context.h"
#include "core/midi_track.h"
#include "core/post_processor.h"
#include "core/section_types.h"
#include "core/timing_constants.h"
#include "core/types.h"

namespace midisketch {
namespace {

constexpr uint8_t kSectionBars = 4;

Arrangement fixtureArrangement() {
  Section section{};
  section.type = SectionType::A;
  section.name = "A";
  section.bars = kSectionBars;
  section.start_bar = 0;
  section.start_tick = 0;
  return Arrangement({section});
}

/// @brief The first bar the fixture progression fills with a chord whose
///        diatonic triad is minor, and which states that one chord for the
///        whole bar so a registered extension covers a single timeline entry.
///        Scanned rather than hardcoded: which bar carries the minor chord is
///        the progression's business, not this test's.
bool findMinorTriadBar(const HarmonyContext& harmony, Tick& out_tick) {
  for (Tick tick = 0; tick < kSectionBars * TICKS_PER_BAR; tick += TICKS_PER_BAR) {
    const int8_t degree = harmony.getChordDegreeAt(tick);
    if (harmony.getChordDegreeAt(tick + TICKS_PER_BAR - 1) != degree) continue;
    if (getChordQuality(degree) != ChordQuality::Minor) continue;
    out_tick = tick;
    return true;
  }
  return false;
}

bool containsPitchClass(const ChordTones& tones, int pitch_class) {
  return std::find(tones.begin(), tones.end(), pitch_class) != tones.end();
}

int pitchClassOf(uint8_t pitch) { return pitch % 12; }

/// @brief Pitch classes of the third, seventh and root of a dominant seventh
///        built on a scale degree, alongside the third the degree's own triad
///        would build.
struct MinorDegreeTones {
  int root;
  int minor_third;  ///< the third of the diatonic triad
  int major_third;  ///< the third the dominant seventh states instead
  int flat_seventh;
};

MinorDegreeTones tonesFor(int8_t degree) {
  const int root = ((degreeToSemitone(degree) % 12) + 12) % 12;
  return {root, (root + 3) % 12, (root + 4) % 12, (root + 10) % 12};
}

}  // namespace

TEST(MotifRelocationPaletteTest, ANoteMovedUnderADominantSeventhLandsOnItsMajorThird) {
  // The sharp case. The motif sits on the third the scale degree builds, which
  // the registered seventh has raised by a semitone, and the vocal a tritone
  // above forces the note to move. The third the chord is sounding is one
  // semitone away and clashes with nothing; relocating onto the degree's triad
  // instead walks past it to the root.
  HarmonyContext harmony;
  harmony.initialize(fixtureArrangement(), getChordProgression(0), Mood::StraightPop);

  Tick bar_tick = 0;
  ASSERT_TRUE(findMinorTriadBar(harmony, bar_tick))
      << "the fixture progression must state a chord whose diatonic triad is minor";
  const int8_t degree = harmony.getChordDegreeAt(bar_tick);
  harmony.registerChordExtension(bar_tick, bar_tick + TICKS_PER_BAR, ChordExtension::Dom7);

  const MinorDegreeTones tones = tonesFor(degree);
  const Tick note_start = bar_tick + TICK_QUARTER;
  const Tick note_duration = 2 * TICK_QUARTER;

  // Fixture premise: the two thirds really are different notes, the degree's
  // triad owns the minor one, and the timeline owns the major one.
  ASSERT_NE(tones.minor_third, tones.major_third);
  ASSERT_TRUE(containsPitchClass(getChordTones(degree), tones.minor_third));
  ASSERT_FALSE(containsPitchClass(getChordTones(degree), tones.major_third));
  const ChordTones sounding = harmony.getChordTonesAt(note_start);
  ASSERT_TRUE(containsPitchClass(sounding, tones.major_third));
  ASSERT_FALSE(containsPitchClass(sounding, tones.minor_third));

  const uint8_t motif_pitch = static_cast<uint8_t>(72 + tones.minor_third);
  MidiTrack motif, vocal;
  motif.addNote(NoteEventBuilder::create(note_start, note_duration, motif_pitch, 90));
  vocal.addNote(NoteEventBuilder::create(note_start, note_duration,
                                         static_cast<uint8_t>(motif_pitch + 6), 90));

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  ASSERT_EQ(motif.notes().size(), 1u);
  const uint8_t moved = motif.notes()[0].note;
  EXPECT_NE(pitchClassOf(moved), tones.minor_third)
      << "the note landed a semitone below the third the chord states";
  EXPECT_EQ(pitchClassOf(moved), tones.major_third)
      << "the nearest tone of the sounding chord is its major third, got pitch class "
      << pitchClassOf(moved);
}

TEST(MotifRelocationPaletteTest, ANoteMovedUnderADominantSeventhCanLandOnItsFlatSeventh) {
  // The colour the triad cannot offer at all. The motif sits a semitone above
  // the root with the vocal a semitone above that, so it has to move, and the
  // seventh is the closest tone of the chord that clears the vocal. Judged by
  // the degree's triad the seventh does not exist and the note drops to the
  // fifth, a minor third further away.
  HarmonyContext harmony;
  harmony.initialize(fixtureArrangement(), getChordProgression(0), Mood::StraightPop);

  Tick bar_tick = 0;
  ASSERT_TRUE(findMinorTriadBar(harmony, bar_tick));
  const int8_t degree = harmony.getChordDegreeAt(bar_tick);
  harmony.registerChordExtension(bar_tick, bar_tick + TICKS_PER_BAR, ChordExtension::Dom7);

  const MinorDegreeTones tones = tonesFor(degree);
  const Tick note_start = bar_tick + TICK_QUARTER;
  const Tick note_duration = 2 * TICK_QUARTER;

  // Fixture premise: the seventh makes the sounding chord strictly richer than
  // the triad the degree builds.
  const ChordTones sounding = harmony.getChordTonesAt(note_start);
  const ChordTones triad = getChordTones(degree);
  ASSERT_GT(sounding.size(), triad.size());
  ASSERT_TRUE(containsPitchClass(sounding, tones.flat_seventh));
  ASSERT_FALSE(containsPitchClass(triad, tones.flat_seventh));

  const uint8_t motif_pitch = static_cast<uint8_t>(72 + (tones.root + 1) % 12);
  MidiTrack motif, vocal;
  motif.addNote(NoteEventBuilder::create(note_start, note_duration, motif_pitch, 90));
  vocal.addNote(NoteEventBuilder::create(note_start, note_duration,
                                         static_cast<uint8_t>(motif_pitch + 1), 90));

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  ASSERT_EQ(motif.notes().size(), 1u);
  const uint8_t moved = motif.notes()[0].note;
  EXPECT_TRUE(containsPitchClass(sounding, pitchClassOf(moved)))
      << "the note must land on a tone of the chord the timeline states, got pitch class "
      << pitchClassOf(moved);
  EXPECT_EQ(pitchClassOf(moved), tones.flat_seventh)
      << "the seventh is the nearest tone that clears the vocal, got pitch class "
      << pitchClassOf(moved);
}

TEST(MotifRelocationPaletteTest, APlainTriadEntryRelocatesOntoThatTriad) {
  // Nothing was registered over this bar, so the chord is exactly the triad the
  // degree builds and the relocation target has to come from it. Reading the
  // timeline adds colour where the timeline states colour; it must not invent
  // any where the timeline states none.
  HarmonyContext harmony;
  harmony.initialize(fixtureArrangement(), getChordProgression(0), Mood::StraightPop);

  Tick bar_tick = 0;
  ASSERT_TRUE(findMinorTriadBar(harmony, bar_tick));
  const int8_t degree = harmony.getChordDegreeAt(bar_tick);

  const MinorDegreeTones tones = tonesFor(degree);
  const Tick note_start = bar_tick + TICK_QUARTER;
  const Tick note_duration = 2 * TICK_QUARTER;

  const ChordTones sounding = harmony.getChordTonesAt(note_start);
  const ChordTones triad = getChordTones(degree);
  ASSERT_EQ(sounding.size(), triad.size()) << "no extension was registered over this bar";

  const uint8_t motif_pitch = static_cast<uint8_t>(72 + tones.minor_third);
  MidiTrack motif, vocal;
  motif.addNote(NoteEventBuilder::create(note_start, note_duration, motif_pitch, 90));
  vocal.addNote(NoteEventBuilder::create(note_start, note_duration,
                                         static_cast<uint8_t>(motif_pitch + 6), 90));

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  ASSERT_EQ(motif.notes().size(), 1u);
  const uint8_t moved = motif.notes()[0].note;
  EXPECT_TRUE(containsPitchClass(triad, pitchClassOf(moved)))
      << "an unextended chord may only lend its own tones, got pitch class " << pitchClassOf(moved);
  EXPECT_NE(pitchClassOf(moved), tones.major_third)
      << "the major third belongs to a seventh nobody registered here";
}

TEST(MotifRelocationPaletteTest, ARegisterCrossingResolvesOntoTheSoundingChordsMajorThird) {
  // The crossing resolver takes the highest chord tone that still sits under
  // the vocal. Under the registered seventh that is its major third; the
  // degree's triad has to reach a further fourth down to the root, because the
  // note between them is the one the extension raised.
  HarmonyContext harmony;
  harmony.initialize(fixtureArrangement(), getChordProgression(0), Mood::StraightPop);

  Tick bar_tick = 0;
  ASSERT_TRUE(findMinorTriadBar(harmony, bar_tick));
  const int8_t degree = harmony.getChordDegreeAt(bar_tick);
  harmony.registerChordExtension(bar_tick, bar_tick + TICKS_PER_BAR, ChordExtension::Dom7);

  const MinorDegreeTones tones = tonesFor(degree);
  const Tick note_start = bar_tick + TICK_QUARTER;
  const Tick note_duration = 2 * TICK_QUARTER;

  // The vocal and the motif stand a fifth apart, so no dissonance rule applies
  // and the only reason to move the motif is that it buries the melody.
  const uint8_t vocal_pitch = static_cast<uint8_t>(72 + tones.minor_third + 6);
  const uint8_t motif_pitch = static_cast<uint8_t>(vocal_pitch + 7);
  MidiTrack motif, vocal;
  motif.addNote(NoteEventBuilder::create(note_start, note_duration, motif_pitch, 90));
  vocal.addNote(NoteEventBuilder::create(note_start, note_duration, vocal_pitch, 90));

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  ASSERT_EQ(motif.notes().size(), 1u);
  const uint8_t moved = motif.notes()[0].note;
  ASSERT_LE(moved, vocal_pitch) << "the crossing must be resolved for the rest to mean anything";
  EXPECT_EQ(pitchClassOf(moved), tones.major_third)
      << "the highest tone of the sounding chord under the vocal is its major third, got pitch "
         "class "
      << pitchClassOf(moved);
}

TEST(MotifRelocationPaletteTest, ARepeatedRunBreaksOntoTheSoundingChordsMajorThird) {
  // Breaking a monotone run picks the nearest different chord tone. On a bar
  // the timeline states as a dominant seventh, the note a semitone above the
  // run is a chord tone; the degree's triad offers nothing nearer than a minor
  // third away, so the run breaks with a wider leap than the music calls for.
  HarmonyContext harmony;
  harmony.initialize(fixtureArrangement(), getChordProgression(0), Mood::StraightPop);

  Tick bar_tick = 0;
  ASSERT_TRUE(findMinorTriadBar(harmony, bar_tick));
  const int8_t degree = harmony.getChordDegreeAt(bar_tick);
  harmony.registerChordExtension(bar_tick, bar_tick + TICKS_PER_BAR, ChordExtension::Dom7);

  const MinorDegreeTones tones = tonesFor(degree);
  const uint8_t run_pitch = static_cast<uint8_t>(72 + tones.minor_third);
  constexpr int kRunLength = 8;  // one bar of eighth notes
  constexpr int kMaxConsecutive = 5;

  MidiTrack motif, vocal;  // no vocal: the run is broken on harmonic grounds alone
  for (int i = 0; i < kRunLength; ++i) {
    motif.addNote(NoteEventBuilder::create(bar_tick + static_cast<Tick>(i) * TICK_EIGHTH,
                                           TICK_EIGHTH, run_pitch, 90));
  }

  PostProcessor::fixMotifRepeatedPitches(motif, vocal, harmony, kMaxConsecutive);

  ASSERT_EQ(motif.notes().size(), static_cast<size_t>(kRunLength));
  const uint8_t broken = motif.notes()[kMaxConsecutive].note;
  ASSERT_NE(broken, run_pitch) << "the run past the threshold must be broken";
  EXPECT_EQ(pitchClassOf(broken), tones.major_third)
      << "the nearest tone of the sounding chord is its major third, got pitch class "
      << pitchClassOf(broken);
}

}  // namespace midisketch
