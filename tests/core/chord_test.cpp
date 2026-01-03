#include <gtest/gtest.h>
#include "core/chord.h"

namespace midisketch {
namespace {

TEST(ChordTest, CanonProgression) {
  const auto& prog = getChordProgression(0);
  // I – V – vi – IV
  EXPECT_EQ(prog.degrees[0], 0);   // I
  EXPECT_EQ(prog.degrees[1], 4);   // V
  EXPECT_EQ(prog.degrees[2], 5);   // vi
  EXPECT_EQ(prog.degrees[3], 3);   // IV
}

TEST(ChordTest, DegreeToRootC) {
  // In C major
  EXPECT_EQ(degreeToRoot(0, Key::C), 60);   // C4
  EXPECT_EQ(degreeToRoot(4, Key::C), 67);   // G4 (V)
  EXPECT_EQ(degreeToRoot(5, Key::C), 69);   // A4 (vi)
}

TEST(ChordTest, DegreeToRootG) {
  // In G major
  EXPECT_EQ(degreeToRoot(0, Key::G), 67);   // G4
}

TEST(ChordTest, MajorChord) {
  auto chord = getChordNotes(0);  // I chord (major)
  EXPECT_EQ(chord.note_count, 3);
  EXPECT_EQ(chord.intervals[0], 0);   // Root
  EXPECT_EQ(chord.intervals[1], 4);   // Major 3rd
  EXPECT_EQ(chord.intervals[2], 7);   // Perfect 5th
}

TEST(ChordTest, MinorChord) {
  auto chord = getChordNotes(5);  // vi chord (minor)
  EXPECT_EQ(chord.note_count, 3);
  EXPECT_EQ(chord.intervals[0], 0);   // Root
  EXPECT_EQ(chord.intervals[1], 3);   // Minor 3rd
  EXPECT_EQ(chord.intervals[2], 7);   // Perfect 5th
}

TEST(ChordTest, ProgressionNames) {
  EXPECT_STREQ(getChordProgressionName(0), "Canon");
  EXPECT_STREQ(getChordProgressionName(1), "Pop1");
}

TEST(ChordTest, ProgressionDisplay) {
  EXPECT_STREQ(getChordProgressionDisplay(0), "I - V - vi - IV");
}

// ===== Chord Extension Tests =====

TEST(ChordTest, ExtendedChordSus2) {
  auto chord = getExtendedChord(0, ChordExtension::Sus2);  // Isus2
  EXPECT_EQ(chord.note_count, 3);
  EXPECT_EQ(chord.intervals[0], 0);   // Root
  EXPECT_EQ(chord.intervals[1], 2);   // Major 2nd
  EXPECT_EQ(chord.intervals[2], 7);   // Perfect 5th
}

TEST(ChordTest, ExtendedChordSus4) {
  auto chord = getExtendedChord(0, ChordExtension::Sus4);  // Isus4
  EXPECT_EQ(chord.note_count, 3);
  EXPECT_EQ(chord.intervals[0], 0);   // Root
  EXPECT_EQ(chord.intervals[1], 5);   // Perfect 4th
  EXPECT_EQ(chord.intervals[2], 7);   // Perfect 5th
}

TEST(ChordTest, ExtendedChordMaj7) {
  auto chord = getExtendedChord(0, ChordExtension::Maj7);  // Imaj7
  EXPECT_EQ(chord.note_count, 4);
  EXPECT_EQ(chord.intervals[0], 0);   // Root
  EXPECT_EQ(chord.intervals[1], 4);   // Major 3rd
  EXPECT_EQ(chord.intervals[2], 7);   // Perfect 5th
  EXPECT_EQ(chord.intervals[3], 11);  // Major 7th
}

TEST(ChordTest, ExtendedChordMin7) {
  auto chord = getExtendedChord(5, ChordExtension::Min7);  // vi7
  EXPECT_EQ(chord.note_count, 4);
  EXPECT_EQ(chord.intervals[0], 0);   // Root
  EXPECT_EQ(chord.intervals[1], 3);   // Minor 3rd
  EXPECT_EQ(chord.intervals[2], 7);   // Perfect 5th
  EXPECT_EQ(chord.intervals[3], 10);  // Minor 7th
}

TEST(ChordTest, ExtendedChordDom7) {
  auto chord = getExtendedChord(4, ChordExtension::Dom7);  // V7
  EXPECT_EQ(chord.note_count, 4);
  EXPECT_EQ(chord.intervals[0], 0);   // Root
  EXPECT_EQ(chord.intervals[1], 4);   // Major 3rd
  EXPECT_EQ(chord.intervals[2], 7);   // Perfect 5th
  EXPECT_EQ(chord.intervals[3], 10);  // Minor 7th (dominant 7th)
}

TEST(ChordTest, ExtendedChordNone) {
  // None extension should return basic triad
  auto basic = getChordNotes(0);
  auto extended = getExtendedChord(0, ChordExtension::None);
  EXPECT_EQ(extended.note_count, basic.note_count);
  EXPECT_EQ(extended.intervals[0], basic.intervals[0]);
  EXPECT_EQ(extended.intervals[1], basic.intervals[1]);
  EXPECT_EQ(extended.intervals[2], basic.intervals[2]);
}

}  // namespace
}  // namespace midisketch
