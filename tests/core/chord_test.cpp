/**
 * @file chord_test.cpp
 * @brief Tests for chord progressions.
 */

#include "core/chord.h"

#include <gtest/gtest.h>

namespace midisketch {
namespace {

TEST(ChordTest, CanonProgression) {
  const auto& prog = getChordProgression(0);
  // I – V – vi – IV
  EXPECT_EQ(prog.degrees[0], 0);  // I
  EXPECT_EQ(prog.degrees[1], 4);  // V
  EXPECT_EQ(prog.degrees[2], 5);  // vi
  EXPECT_EQ(prog.degrees[3], 3);  // IV
}

TEST(ChordTest, DegreeToRootC) {
  // In C major
  EXPECT_EQ(degreeToRoot(0, Key::C), 60);  // C4
  EXPECT_EQ(degreeToRoot(4, Key::C), 67);  // G4 (V)
  EXPECT_EQ(degreeToRoot(5, Key::C), 69);  // A4 (vi)
}

TEST(ChordTest, DegreeToRootG) {
  // In G major
  EXPECT_EQ(degreeToRoot(0, Key::G), 67);  // G4
}

TEST(ChordTest, MajorChord) {
  auto chord = getChordNotes(0);  // I chord (major)
  EXPECT_EQ(chord.note_count, 3);
  EXPECT_EQ(chord.intervals[0], 0);  // Root
  EXPECT_EQ(chord.intervals[1], 4);  // Major 3rd
  EXPECT_EQ(chord.intervals[2], 7);  // Perfect 5th
}

TEST(ChordTest, MinorChord) {
  auto chord = getChordNotes(5);  // vi chord (minor)
  EXPECT_EQ(chord.note_count, 3);
  EXPECT_EQ(chord.intervals[0], 0);  // Root
  EXPECT_EQ(chord.intervals[1], 3);  // Minor 3rd
  EXPECT_EQ(chord.intervals[2], 7);  // Perfect 5th
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
  EXPECT_EQ(chord.intervals[0], 0);  // Root
  EXPECT_EQ(chord.intervals[1], 2);  // Major 2nd
  EXPECT_EQ(chord.intervals[2], 7);  // Perfect 5th
}

TEST(ChordTest, ExtendedChordSus4) {
  auto chord = getExtendedChord(0, ChordExtension::Sus4);  // Isus4
  EXPECT_EQ(chord.note_count, 3);
  EXPECT_EQ(chord.intervals[0], 0);  // Root
  EXPECT_EQ(chord.intervals[1], 5);  // Perfect 4th
  EXPECT_EQ(chord.intervals[2], 7);  // Perfect 5th
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

// ===== New YOASOBI-style Progressions =====

TEST(ChordTest, YOASOBI1Progression) {
  const auto& prog = getChordProgression(16);
  // vi – iii – IV – I
  EXPECT_EQ(prog.degrees[0], 5);  // vi
  EXPECT_EQ(prog.degrees[1], 2);  // iii
  EXPECT_EQ(prog.degrees[2], 3);  // IV
  EXPECT_EQ(prog.degrees[3], 0);  // I
}

TEST(ChordTest, JazzPopProgression) {
  const auto& prog = getChordProgression(17);
  // ii – V – I – vi
  EXPECT_EQ(prog.degrees[0], 1);  // ii
  EXPECT_EQ(prog.degrees[1], 4);  // V
  EXPECT_EQ(prog.degrees[2], 0);  // I
  EXPECT_EQ(prog.degrees[3], 5);  // vi
}

TEST(ChordTest, YOASOBI2Progression) {
  const auto& prog = getChordProgression(18);
  // vi – ii – V – I (turnaround)
  EXPECT_EQ(prog.degrees[0], 5);  // vi
  EXPECT_EQ(prog.degrees[1], 1);  // ii
  EXPECT_EQ(prog.degrees[2], 4);  // V
  EXPECT_EQ(prog.degrees[3], 0);  // I
}

TEST(ChordTest, CityPopProgression) {
  const auto& prog = getChordProgression(19);
  // I – vi – ii – V
  EXPECT_EQ(prog.degrees[0], 0);  // I
  EXPECT_EQ(prog.degrees[1], 5);  // vi
  EXPECT_EQ(prog.degrees[2], 1);  // ii
  EXPECT_EQ(prog.degrees[3], 4);  // V
}

TEST(ChordTest, NewProgressionNames) {
  EXPECT_STREQ(getChordProgressionName(16), "YOASOBI1");
  EXPECT_STREQ(getChordProgressionName(17), "JazzPop");
  EXPECT_STREQ(getChordProgressionName(18), "YOASOBI2");
  EXPECT_STREQ(getChordProgressionName(19), "CityPop");
}

TEST(ChordTest, NewProgressionDisplays) {
  EXPECT_STREQ(getChordProgressionDisplay(16), "vi - iii - IV - I");
  EXPECT_STREQ(getChordProgressionDisplay(17), "ii - V - I - vi");
  EXPECT_STREQ(getChordProgressionDisplay(18), "vi - ii - V - I");
  EXPECT_STREQ(getChordProgressionDisplay(19), "I - vi - ii - V");
}

// ===== 9th Chord Extension Tests =====

TEST(ChordTest, ExtendedChordAdd9) {
  auto chord = getExtendedChord(0, ChordExtension::Add9);  // Iadd9
  EXPECT_EQ(chord.note_count, 4);
  EXPECT_EQ(chord.intervals[0], 0);   // Root
  EXPECT_EQ(chord.intervals[1], 4);   // Major 3rd
  EXPECT_EQ(chord.intervals[2], 7);   // Perfect 5th
  EXPECT_EQ(chord.intervals[3], 14);  // 9th (octave + major 2nd)
}

TEST(ChordTest, ExtendedChordMaj9) {
  auto chord = getExtendedChord(0, ChordExtension::Maj9);  // Imaj9
  EXPECT_EQ(chord.note_count, 5);
  EXPECT_EQ(chord.intervals[0], 0);   // Root
  EXPECT_EQ(chord.intervals[1], 4);   // Major 3rd
  EXPECT_EQ(chord.intervals[2], 7);   // Perfect 5th
  EXPECT_EQ(chord.intervals[3], 11);  // Major 7th
  EXPECT_EQ(chord.intervals[4], 14);  // 9th
}

TEST(ChordTest, ExtendedChordMin9) {
  auto chord = getExtendedChord(5, ChordExtension::Min9);  // vi9
  EXPECT_EQ(chord.note_count, 5);
  EXPECT_EQ(chord.intervals[0], 0);   // Root
  EXPECT_EQ(chord.intervals[1], 3);   // Minor 3rd
  EXPECT_EQ(chord.intervals[2], 7);   // Perfect 5th
  EXPECT_EQ(chord.intervals[3], 10);  // Minor 7th
  EXPECT_EQ(chord.intervals[4], 14);  // 9th
}

TEST(ChordTest, ExtendedChordDom9) {
  auto chord = getExtendedChord(4, ChordExtension::Dom9);  // V9
  EXPECT_EQ(chord.note_count, 5);
  EXPECT_EQ(chord.intervals[0], 0);   // Root
  EXPECT_EQ(chord.intervals[1], 4);   // Major 3rd
  EXPECT_EQ(chord.intervals[2], 7);   // Perfect 5th
  EXPECT_EQ(chord.intervals[3], 10);  // Minor 7th
  EXPECT_EQ(chord.intervals[4], 14);  // 9th
}

// ===== Borrowed Chord Tests =====

TEST(ChordTest, BorrowedChordBVII) {
  // bVII in C major = Bb
  EXPECT_EQ(degreeToRoot(10, Key::C), 70);  // Bb4 (MIDI 70)
}

TEST(ChordTest, BorrowedChordBVI) {
  // bVI in C major = Ab
  EXPECT_EQ(degreeToRoot(8, Key::C), 68);  // Ab4 (MIDI 68)
}

TEST(ChordTest, BorrowedChordBIII) {
  // bIII in C major = Eb
  EXPECT_EQ(degreeToRoot(11, Key::C), 63);  // Eb4 (MIDI 63)
}

TEST(ChordTest, BorrowedChordQuality) {
  // All borrowed chords should be major quality
  auto bVII = getChordNotes(10);
  auto bVI = getChordNotes(8);
  auto bIII = getChordNotes(11);

  // Major triad intervals: 0, 4, 7
  EXPECT_EQ(bVII.intervals[1], 4);  // Major 3rd
  EXPECT_EQ(bVI.intervals[1], 4);   // Major 3rd
  EXPECT_EQ(bIII.intervals[1], 4);  // Major 3rd
}

}  // namespace
}  // namespace midisketch
