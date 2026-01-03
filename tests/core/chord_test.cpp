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

}  // namespace
}  // namespace midisketch
