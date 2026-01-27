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

// ===== Section-Based Reharmonization Tests =====

TEST(ChordTest, ReharmonizeChorusAddsExtensions) {
  // Chorus: dominant chord (V, degree 4) should get Dom7
  auto result_dom = reharmonizeForSection(4, SectionType::Chorus, false, true);
  EXPECT_EQ(result_dom.degree, 4);  // Degree unchanged
  EXPECT_TRUE(result_dom.extension_overridden);
  EXPECT_EQ(result_dom.extension, ChordExtension::Dom7);

  // Chorus: minor chord (vi, degree 5) should get Min7
  auto result_min = reharmonizeForSection(5, SectionType::Chorus, true, false);
  EXPECT_EQ(result_min.degree, 5);
  EXPECT_TRUE(result_min.extension_overridden);
  EXPECT_EQ(result_min.extension, ChordExtension::Min7);

  // Chorus: tonic (I, degree 0) should get Maj7
  auto result_tonic = reharmonizeForSection(0, SectionType::Chorus, false, false);
  EXPECT_EQ(result_tonic.degree, 0);
  EXPECT_TRUE(result_tonic.extension_overridden);
  EXPECT_EQ(result_tonic.extension, ChordExtension::Maj7);

  // Chorus: IV chord (degree 3) should get Add9
  auto result_iv = reharmonizeForSection(3, SectionType::Chorus, false, false);
  EXPECT_EQ(result_iv.degree, 3);
  EXPECT_TRUE(result_iv.extension_overridden);
  EXPECT_EQ(result_iv.extension, ChordExtension::Add9);
}

TEST(ChordTest, ReharmonizeVerseIVToii) {
  // Verse (A): IV chord (degree 3) should be substituted to ii (degree 1)
  auto result = reharmonizeForSection(3, SectionType::A, false, false);
  EXPECT_EQ(result.degree, 1);  // IV -> ii substitution
  EXPECT_FALSE(result.extension_overridden);

  // Verse (A): other chords should be unchanged
  auto result_tonic = reharmonizeForSection(0, SectionType::A, false, false);
  EXPECT_EQ(result_tonic.degree, 0);  // I stays I
  EXPECT_FALSE(result_tonic.extension_overridden);

  auto result_v = reharmonizeForSection(4, SectionType::A, false, true);
  EXPECT_EQ(result_v.degree, 4);  // V stays V
  EXPECT_FALSE(result_v.extension_overridden);
}

TEST(ChordTest, ReharmonizeOtherSectionsUnchanged) {
  // Bridge section: no changes
  auto result = reharmonizeForSection(3, SectionType::Bridge, false, false);
  EXPECT_EQ(result.degree, 3);  // IV stays IV
  EXPECT_FALSE(result.extension_overridden);

  // Intro section: no changes
  auto result_intro = reharmonizeForSection(0, SectionType::Intro, false, false);
  EXPECT_EQ(result_intro.degree, 0);
  EXPECT_FALSE(result_intro.extension_overridden);
}

TEST(ChordTest, PassingDiminishedInBSection) {
  // B section should insert passing diminished chord
  auto info = checkPassingDiminished(0, 4, SectionType::B);  // I -> V transition
  EXPECT_TRUE(info.should_insert);

  // The diminished chord root should be a half-step below the target (V = G)
  // G is 7 semitones from C, so half-step below is F# = 6 semitones
  EXPECT_EQ(info.root_semitone, 6);

  // Should be a diminished triad
  EXPECT_TRUE(info.chord.is_diminished);
  EXPECT_EQ(info.chord.note_count, 3);
  EXPECT_EQ(info.chord.intervals[0], 0);  // Root
  EXPECT_EQ(info.chord.intervals[1], 3);  // Minor 3rd
  EXPECT_EQ(info.chord.intervals[2], 6);  // Diminished 5th
}

TEST(ChordTest, PassingDiminishedOnlyInBSection) {
  // Non-B sections should not get passing diminished chords
  auto info_chorus = checkPassingDiminished(0, 4, SectionType::Chorus);
  EXPECT_FALSE(info_chorus.should_insert);

  auto info_verse = checkPassingDiminished(0, 4, SectionType::A);
  EXPECT_FALSE(info_verse.should_insert);

  auto info_intro = checkPassingDiminished(0, 4, SectionType::Intro);
  EXPECT_FALSE(info_intro.should_insert);
}

TEST(ChordTest, PassingDiminishedTargetChords) {
  // Test various target chords in B section
  // I -> IV (F): half-step below F is E = 4 semitones
  auto info_iv = checkPassingDiminished(0, 3, SectionType::B);
  EXPECT_TRUE(info_iv.should_insert);
  EXPECT_EQ(info_iv.root_semitone, 4);  // E (half-step below F)

  // vi -> ii (Dm): half-step below D is C# = 1 semitone
  auto info_ii = checkPassingDiminished(5, 1, SectionType::B);
  EXPECT_TRUE(info_ii.should_insert);
  EXPECT_EQ(info_ii.root_semitone, 1);  // C# (half-step below D)
}

// ===== Modal Interchange Expansion Tests (iv, bII, #IVdim) =====

TEST(ChordTest, BorrowedChordMinorIV) {
  // iv (degree 12) in C major = Fm (root at F = 5 semitones from C)
  EXPECT_EQ(degreeToRoot(12, Key::C), 65);  // F4 (MIDI 65)

  // iv should be minor quality: (0, 3, 7)
  auto chord = getChordNotes(12);
  EXPECT_EQ(chord.note_count, 3);
  EXPECT_EQ(chord.intervals[0], 0);  // Root
  EXPECT_EQ(chord.intervals[1], 3);  // Minor 3rd
  EXPECT_EQ(chord.intervals[2], 7);  // Perfect 5th
  EXPECT_FALSE(chord.is_diminished);
}

TEST(ChordTest, BorrowedChordNeapolitan) {
  // bII (degree 13) in C major = Db (root at Db = 1 semitone from C)
  EXPECT_EQ(degreeToRoot(13, Key::C), 61);  // Db4 (MIDI 61)

  // bII should be major quality: (0, 4, 7)
  auto chord = getChordNotes(13);
  EXPECT_EQ(chord.note_count, 3);
  EXPECT_EQ(chord.intervals[0], 0);  // Root
  EXPECT_EQ(chord.intervals[1], 4);  // Major 3rd
  EXPECT_EQ(chord.intervals[2], 7);  // Perfect 5th
  EXPECT_FALSE(chord.is_diminished);
}

TEST(ChordTest, BorrowedChordSharpIVDim) {
  // #IVdim (degree 14) in C major = F#dim (root at F# = 6 semitones from C)
  EXPECT_EQ(degreeToRoot(14, Key::C), 66);  // F#4 (MIDI 66)

  // #IVdim should be diminished quality: (0, 3, 6)
  auto chord = getChordNotes(14);
  EXPECT_EQ(chord.note_count, 3);
  EXPECT_EQ(chord.intervals[0], 0);  // Root
  EXPECT_EQ(chord.intervals[1], 3);  // Minor 3rd
  EXPECT_EQ(chord.intervals[2], 6);  // Diminished 5th
  EXPECT_TRUE(chord.is_diminished);
}

TEST(ChordTest, BorrowedChordMinorIVInOtherKeys) {
  // iv in G major = Cm (root at C)
  // degreeToSemitone(12) = 5, key G = 7, (5+7) % 12 = 0 => 0 + 60 = 60 (C4)
  EXPECT_EQ(degreeToRoot(12, Key::G), 60);  // C4
}

TEST(ChordTest, BorrowedChordNeapolitanInOtherKeys) {
  // bII in G major = Ab (root at Ab)
  // degreeToSemitone(13) = 1, plus key G (7) = 8 => Ab
  // MIDI: 8 + 60 = 68 (Ab4)
  EXPECT_EQ(degreeToRoot(13, Key::G), 68);  // Ab4
}

TEST(ChordTest, ExistingDiatonicDegreesUnaffected) {
  // Verify all diatonic degrees still produce correct results
  // I = C (0 semitones)
  EXPECT_EQ(degreeToRoot(0, Key::C), 60);  // C4
  // ii = D (2 semitones)
  EXPECT_EQ(degreeToRoot(1, Key::C), 62);  // D4
  // iii = E (4 semitones)
  EXPECT_EQ(degreeToRoot(2, Key::C), 64);  // E4
  // IV = F (5 semitones)
  EXPECT_EQ(degreeToRoot(3, Key::C), 65);  // F4
  // V = G (7 semitones)
  EXPECT_EQ(degreeToRoot(4, Key::C), 67);  // G4
  // vi = A (9 semitones)
  EXPECT_EQ(degreeToRoot(5, Key::C), 69);  // A4
  // vii = B (11 semitones)
  EXPECT_EQ(degreeToRoot(6, Key::C), 71);  // B4

  // Existing borrowed chords unchanged
  EXPECT_EQ(degreeToRoot(8, Key::C), 68);   // bVI = Ab4
  EXPECT_EQ(degreeToRoot(10, Key::C), 70);  // bVII = Bb4
  EXPECT_EQ(degreeToRoot(11, Key::C), 63);  // bIII = Eb4
}

TEST(ChordTest, ExistingChordQualitiesUnaffected) {
  // Major chords: I, IV, V
  auto chord_I = getChordNotes(0);
  EXPECT_EQ(chord_I.intervals[1], 4);  // Major 3rd
  auto chord_IV = getChordNotes(3);
  EXPECT_EQ(chord_IV.intervals[1], 4);  // Major 3rd
  auto chord_V = getChordNotes(4);
  EXPECT_EQ(chord_V.intervals[1], 4);  // Major 3rd

  // Minor chords: ii, iii, vi
  auto chord_ii = getChordNotes(1);
  EXPECT_EQ(chord_ii.intervals[1], 3);  // Minor 3rd
  auto chord_iii = getChordNotes(2);
  EXPECT_EQ(chord_iii.intervals[1], 3);  // Minor 3rd
  auto chord_vi = getChordNotes(5);
  EXPECT_EQ(chord_vi.intervals[1], 3);  // Minor 3rd

  // Diminished: vii
  auto chord_vii = getChordNotes(6);
  EXPECT_TRUE(chord_vii.is_diminished);
  EXPECT_EQ(chord_vii.intervals[1], 3);  // Minor 3rd
  EXPECT_EQ(chord_vii.intervals[2], 6);  // Diminished 5th
}

// ===== Tritone Substitution Tests =====

TEST(ChordTest, TritoneSubRootCalculation) {
  // G (7 semitones) -> Db (1 semitone): tritone is 6 semitones
  EXPECT_EQ(getTritoneSubRoot(7), 1);   // G -> Db
  // C (0) -> F# (6)
  EXPECT_EQ(getTritoneSubRoot(0), 6);   // C -> F#/Gb
  // D (2) -> Ab (8)
  EXPECT_EQ(getTritoneSubRoot(2), 8);   // D -> Ab
  // F (5) -> B (11)
  EXPECT_EQ(getTritoneSubRoot(5), 11);  // F -> B
  // Symmetry: applying tritone sub twice returns to original
  EXPECT_EQ(getTritoneSubRoot(getTritoneSubRoot(7)), 7);
  EXPECT_EQ(getTritoneSubRoot(getTritoneSubRoot(0)), 0);
}

TEST(ChordTest, TritoneSubOnDominantChord) {
  // V chord (degree 4) is dominant -> should substitute when roll < probability
  auto info = checkTritoneSubstitution(4, true, 0.5f, 0.3f);
  EXPECT_TRUE(info.should_substitute);

  // V in C major: root is G (semitone 7), tritone sub = Db (semitone 1)
  EXPECT_EQ(info.sub_root_semitone, 1);

  // The substituted chord should be a dominant 7th: (0, 4, 7, 10)
  EXPECT_EQ(info.chord.note_count, 4);
  EXPECT_EQ(info.chord.intervals[0], 0);   // Root
  EXPECT_EQ(info.chord.intervals[1], 4);   // Major 3rd
  EXPECT_EQ(info.chord.intervals[2], 7);   // Perfect 5th
  EXPECT_EQ(info.chord.intervals[3], 10);  // Minor 7th (dominant quality)
  EXPECT_FALSE(info.chord.is_diminished);
}

TEST(ChordTest, TritoneSubNotAppliedToNonDominant) {
  // I chord (degree 0) is not dominant -> should not substitute
  auto info_tonic = checkTritoneSubstitution(0, false, 1.0f, 0.0f);
  EXPECT_FALSE(info_tonic.should_substitute);

  // vi chord (degree 5) is not dominant -> should not substitute
  auto info_minor = checkTritoneSubstitution(5, false, 1.0f, 0.0f);
  EXPECT_FALSE(info_minor.should_substitute);

  // IV chord (degree 3) is not dominant -> should not substitute
  auto info_sub = checkTritoneSubstitution(3, false, 1.0f, 0.0f);
  EXPECT_FALSE(info_sub.should_substitute);
}

TEST(ChordTest, TritoneSubProbabilityRejected) {
  // Dominant chord but roll >= probability -> should not substitute
  auto info = checkTritoneSubstitution(4, true, 0.5f, 0.5f);
  EXPECT_FALSE(info.should_substitute);

  auto info2 = checkTritoneSubstitution(4, true, 0.5f, 0.8f);
  EXPECT_FALSE(info2.should_substitute);
}

TEST(ChordTest, TritoneSubProbabilityAccepted) {
  // Dominant chord with roll < probability -> should substitute
  auto info = checkTritoneSubstitution(4, true, 0.5f, 0.49f);
  EXPECT_TRUE(info.should_substitute);

  // 100% probability always substitutes
  auto info2 = checkTritoneSubstitution(4, true, 1.0f, 0.99f);
  EXPECT_TRUE(info2.should_substitute);
}

TEST(ChordTest, TritoneSubZeroProbability) {
  // Zero probability never substitutes
  auto info = checkTritoneSubstitution(4, true, 0.0f, 0.0f);
  EXPECT_FALSE(info.should_substitute);
}

TEST(ChordTest, TritoneSubFlagDisabledByDefault) {
  // ChordExtensionParams default should have tritone_sub disabled
  ChordExtensionParams params;
  EXPECT_FALSE(params.tritone_sub);
  EXPECT_FLOAT_EQ(params.tritone_sub_probability, 0.5f);
}

}  // namespace
}  // namespace midisketch
