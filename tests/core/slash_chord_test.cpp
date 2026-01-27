/**
 * @file slash_chord_test.cpp
 * @brief Tests for slash chord bass note override functionality.
 */

#include "core/chord.h"

#include <gtest/gtest.h>

namespace midisketch {
namespace {

// ============================================================================
// Basic SlashChordInfo Tests
// ============================================================================

TEST(SlashChordTest, DefaultNoOverride) {
  // Default SlashChordInfo should have no override
  SlashChordInfo info{false, 0};
  EXPECT_FALSE(info.has_override);
}

// ============================================================================
// Slash Chord Pattern Tests (probability_roll = 0.0 to guarantee activation)
// ============================================================================

TEST(SlashChordTest, IChordBeforeIV_CreatesSlashE) {
  // I (C) -> IV (F): should produce C/E (bass E, pitch class 4)
  // Bass walks: E -> F (1 semitone step)
  auto info = checkSlashChord(0, 3, SectionType::A, 0.0f);
  EXPECT_TRUE(info.has_override);
  EXPECT_EQ(info.bass_note_semitone, 4);  // E = 4 semitones from C
}

TEST(SlashChordTest, IChordBeforeVI_CreatesSlashE) {
  // I (C) -> vi (Am): should produce C/E (bass E, pitch class 4)
  auto info = checkSlashChord(0, 5, SectionType::A, 0.0f);
  EXPECT_TRUE(info.has_override);
  EXPECT_EQ(info.bass_note_semitone, 4);  // E
}

TEST(SlashChordTest, IVChordBeforeV_CreatesSlashA) {
  // IV (F) -> V (G): should produce F/A (bass A, pitch class 9)
  // Bass walks: A -> G (2 semitone step down)
  auto info = checkSlashChord(3, 4, SectionType::A, 0.0f);
  EXPECT_TRUE(info.has_override);
  EXPECT_EQ(info.bass_note_semitone, 9);  // A = (5 + 4) % 12 = 9
}

TEST(SlashChordTest, IVChordBeforeI_CreatesSlashA) {
  // IV (F) -> I (C): should produce F/A (bass A, pitch class 9)
  auto info = checkSlashChord(3, 0, SectionType::A, 0.0f);
  EXPECT_TRUE(info.has_override);
  EXPECT_EQ(info.bass_note_semitone, 9);  // A
}

TEST(SlashChordTest, VChordBeforeI_CreatesSlashB) {
  // V (G) -> I (C): should produce G/B (bass B, pitch class 11)
  // Leading tone resolution: B -> C (1 semitone)
  auto info = checkSlashChord(4, 0, SectionType::A, 0.0f);
  EXPECT_TRUE(info.has_override);
  EXPECT_EQ(info.bass_note_semitone, 11);  // B = (7 + 4) % 12 = 11
}

TEST(SlashChordTest, VIChordBeforeIV_CreatesSlashC) {
  // vi (Am) -> IV (F): should produce Am/C (bass C, pitch class 0)
  auto info = checkSlashChord(5, 3, SectionType::A, 0.0f);
  EXPECT_TRUE(info.has_override);
  EXPECT_EQ(info.bass_note_semitone, 0);  // C = (9 + 3) % 12 = 0
}

TEST(SlashChordTest, IIChordBeforeV_CreatesSlashF) {
  // ii (Dm) -> V (G): should produce Dm/F (bass F, pitch class 5)
  // Bass walks: F -> G (2 semitone step)
  auto info = checkSlashChord(1, 4, SectionType::A, 0.0f);
  EXPECT_TRUE(info.has_override);
  EXPECT_EQ(info.bass_note_semitone, 5);  // F = (2 + 3) % 12 = 5
}

// ============================================================================
// Stepwise Motion Validation
// ============================================================================

TEST(SlashChordTest, SlashChordCreatesStepwiseBassMotion) {
  // C/E -> F: E(4) -> F(5) = 1 semitone (stepwise)
  auto info = checkSlashChord(0, 3, SectionType::A, 0.0f);
  ASSERT_TRUE(info.has_override);
  int slash_bass_pc = info.bass_note_semitone;
  int next_root_pc = degreeToSemitone(3);  // F = 5
  int interval = std::abs(next_root_pc - slash_bass_pc);
  if (interval > 6) interval = 12 - interval;
  EXPECT_LE(interval, 2);  // Stepwise = 1 or 2 semitones
}

TEST(SlashChordTest, GSlashBToC_LeadingToneResolution) {
  // G/B -> C: B(11) -> C(0) = 1 semitone (leading tone)
  auto info = checkSlashChord(4, 0, SectionType::A, 0.0f);
  ASSERT_TRUE(info.has_override);
  int slash_bass_pc = info.bass_note_semitone;  // 11 (B)
  int next_root_pc = degreeToSemitone(0);       // 0 (C)
  int interval = ((next_root_pc - slash_bass_pc) + 12) % 12;
  EXPECT_EQ(interval, 1);  // Half step up: B -> C
}

// ============================================================================
// No Slash Chord When Already Stepwise
// ============================================================================

TEST(SlashChordTest, NoSlashWhenAlreadyStepwise) {
  // V (G, pc=7) -> vi (Am, pc=9): interval is 2 semitones (already stepwise)
  // No slash chord needed
  auto info = checkSlashChord(4, 5, SectionType::A, 0.0f);
  EXPECT_FALSE(info.has_override);
}

TEST(SlashChordTest, NoSlashForSameChord) {
  // I -> I: no movement, no slash chord needed
  auto info = checkSlashChord(0, 0, SectionType::A, 0.0f);
  EXPECT_FALSE(info.has_override);
}

// ============================================================================
// Section-Based Probability Tests
// ============================================================================

TEST(SlashChordTest, IntroSectionNeverGetsSlash) {
  // Intro sections should never get slash chords
  auto info = checkSlashChord(0, 3, SectionType::Intro, 0.0f);
  EXPECT_FALSE(info.has_override);
}

TEST(SlashChordTest, OutroSectionNeverGetsSlash) {
  // Outro sections should never get slash chords
  auto info = checkSlashChord(0, 3, SectionType::Outro, 0.0f);
  EXPECT_FALSE(info.has_override);
}

TEST(SlashChordTest, ChantSectionNeverGetsSlash) {
  // Chant sections should never get slash chords
  auto info = checkSlashChord(0, 3, SectionType::Chant, 0.0f);
  EXPECT_FALSE(info.has_override);
}

TEST(SlashChordTest, MixBreakSectionNeverGetsSlash) {
  // MixBreak sections should never get slash chords
  auto info = checkSlashChord(0, 3, SectionType::MixBreak, 0.0f);
  EXPECT_FALSE(info.has_override);
}

TEST(SlashChordTest, HighRollRejectsSlash) {
  // High probability roll (1.0) should never produce slash chord
  auto info = checkSlashChord(0, 3, SectionType::A, 1.0f);
  EXPECT_FALSE(info.has_override);
}

TEST(SlashChordTest, VerseHasHigherProbabilityThanChorus) {
  // Verse (A) threshold is 0.50, Chorus is 0.30
  // Roll of 0.35 should pass in Verse but fail in Chorus
  auto verse_info = checkSlashChord(0, 3, SectionType::A, 0.35f);
  auto chorus_info = checkSlashChord(0, 3, SectionType::Chorus, 0.35f);
  EXPECT_TRUE(verse_info.has_override);
  EXPECT_FALSE(chorus_info.has_override);
}

TEST(SlashChordTest, BSectionSlashChordActive) {
  // B section (pre-chorus) with low roll should get slash chord
  auto info = checkSlashChord(0, 3, SectionType::B, 0.1f);
  EXPECT_TRUE(info.has_override);
}

TEST(SlashChordTest, BridgeSlashChordActive) {
  // Bridge section with low roll should get slash chord
  auto info = checkSlashChord(0, 3, SectionType::Bridge, 0.1f);
  EXPECT_TRUE(info.has_override);
}

// ============================================================================
// Chord Voicing Unaffected by Slash Chord
// ============================================================================

TEST(SlashChordTest, ChordVoicingUnchanged) {
  // Slash chords only affect bass note, not chord intervals.
  // Verify getChordNotes still returns the same intervals regardless of slash.
  auto info = checkSlashChord(0, 3, SectionType::A, 0.0f);
  ASSERT_TRUE(info.has_override);

  // The chord track uses getChordNotes(degree), which should be unaffected
  auto chord_before = getChordNotes(0);  // I chord
  EXPECT_EQ(chord_before.intervals[0], 0);  // Root
  EXPECT_EQ(chord_before.intervals[1], 4);  // Major 3rd
  EXPECT_EQ(chord_before.intervals[2], 7);  // Perfect 5th
  EXPECT_EQ(chord_before.note_count, 3);
}

// ============================================================================
// Default (-1 equivalent) Preserves Normal Root Bass
// ============================================================================

TEST(SlashChordTest, NoOverridePreservesRoot) {
  // When has_override is false, the bass should use the normal chord root.
  // Test a case where no slash pattern applies.
  auto info = checkSlashChord(2, 0, SectionType::A, 0.0f);  // iii -> I
  // iii (Em) does not have a slash pattern to I
  EXPECT_FALSE(info.has_override);
}

// ============================================================================
// degreeToSemitone Public API Tests
// ============================================================================

TEST(SlashChordTest, DegreeToSemitone_Diatonic) {
  EXPECT_EQ(degreeToSemitone(0), 0);   // C
  EXPECT_EQ(degreeToSemitone(1), 2);   // D
  EXPECT_EQ(degreeToSemitone(2), 4);   // E
  EXPECT_EQ(degreeToSemitone(3), 5);   // F
  EXPECT_EQ(degreeToSemitone(4), 7);   // G
  EXPECT_EQ(degreeToSemitone(5), 9);   // A
  EXPECT_EQ(degreeToSemitone(6), 11);  // B
}

TEST(SlashChordTest, DegreeToSemitone_Borrowed) {
  EXPECT_EQ(degreeToSemitone(8), 8);    // bVI = Ab
  EXPECT_EQ(degreeToSemitone(10), 10);  // bVII = Bb
  EXPECT_EQ(degreeToSemitone(11), 3);   // bIII = Eb
  EXPECT_EQ(degreeToSemitone(12), 5);   // iv = F
  EXPECT_EQ(degreeToSemitone(13), 1);   // bII = Db
  EXPECT_EQ(degreeToSemitone(14), 6);   // #IVdim = F#
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(SlashChordTest, NoSlashForUnrecognizedDegree) {
  // Borrowed chord degrees (e.g., bVII = 10) have no slash patterns defined
  auto info = checkSlashChord(10, 0, SectionType::A, 0.0f);
  EXPECT_FALSE(info.has_override);
}

TEST(SlashChordTest, VIBeforeII_CreatesSlashC) {
  // vi (Am) -> ii (Dm): should produce Am/C (bass C)
  auto info = checkSlashChord(5, 1, SectionType::A, 0.0f);
  EXPECT_TRUE(info.has_override);
  EXPECT_EQ(info.bass_note_semitone, 0);  // C
}

TEST(SlashChordTest, VIBeforeI_CreatesSlashC) {
  // vi (Am) -> I (C): should produce Am/C (bass C)
  auto info = checkSlashChord(5, 0, SectionType::A, 0.0f);
  EXPECT_TRUE(info.has_override);
  EXPECT_EQ(info.bass_note_semitone, 0);  // C
}

}  // namespace
}  // namespace midisketch
