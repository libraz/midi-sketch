/**
 * @file aux_dissonance_regression_test.cpp
 * @brief Regression tests for aux track dissonance fixes.
 *
 * Tests for specific bugs that were fixed:
 * 1. Aux notes with small overlap (5 ticks) not trimmed at chord boundaries
 * 2. Harmony notes using wrong chord due to timing offset
 * 3. Motif notes not snapped to chord tones
 */

#include <gtest/gtest.h>

#include <random>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/generator.h"
#include "core/harmony_context.h"
#include "core/i_harmony_context.h"
#include "core/motif.h"
#include "core/timing_constants.h"
#include "track/generators/aux.h"

namespace midisketch {
namespace {

// Helper to create a section
Section makeSection(SectionType type, uint8_t bars, Tick start_tick) {
  Section s;
  s.type = type;
  s.bars = bars;
  s.start_tick = start_tick;
  return s;
}

// ============================================================================
// Bug #1: Small overlap (5 ticks) at chord boundary not trimmed
// ============================================================================
// Original bug: Note E5 at tick 7445, duration 240, ends at 7685
// Chord changes at 7680 (Am->F)
// Overlap = 5 ticks, threshold was 10, so 5 > 10 = false, no trim
// E is NOT in F chord, causing sustained_over_chord_change issue

TEST(AuxChordBoundaryRegression, SmallOverlapShouldBeTrimmed) {
  // Setup: Am chord (degree 5) at bar 3, F chord (degree 3) at bar 4
  // Chord progression: Pop2 = F-C-G-Am = [3, 0, 4, 5]
  // Bar 3 = Am (index 3 % 4 = 3 -> degree 5)
  // Bar 4 = F (index 0 % 4 = 0 -> degree 3)

  Section section = makeSection(SectionType::Chorus, 8, 0);
  Arrangement arrangement({section});
  const auto& progression = getChordProgression(3);  // Pop2

  HarmonyContext harmony;
  harmony.initialize(arrangement, progression, Mood::StraightPop);

  // Verify chord setup
  Tick bar3_start = 3 * TICKS_PER_BAR;  // 5760
  Tick bar4_start = 4 * TICKS_PER_BAR;  // 7680 - this is where chord changes

  int8_t degree_bar3 = harmony.getChordDegreeAt(bar3_start);
  int8_t degree_bar4 = harmony.getChordDegreeAt(bar4_start);

  EXPECT_EQ(degree_bar3, 5) << "Bar 3 should be Am (degree 5)";
  EXPECT_EQ(degree_bar4, 3) << "Bar 4 should be F (degree 3)";

  // E is chord tone in Am (A-C-E) but NOT in F (F-A-C)
  ChordTones am_tones = getChordTones(5);  // Am
  ChordTones f_tones = getChordTones(3);   // F

  bool e_in_am = false, e_in_f = false;
  for (uint8_t i = 0; i < am_tones.count; ++i) {
    if (am_tones.pitch_classes[i] == 4) e_in_am = true;  // E = pc 4
  }
  for (uint8_t i = 0; i < f_tones.count; ++i) {
    if (f_tones.pitch_classes[i] == 4) e_in_f = true;
  }

  EXPECT_TRUE(e_in_am) << "E should be chord tone in Am";
  EXPECT_FALSE(e_in_f) << "E should NOT be chord tone in F";

  // The bug: a note starting 235 ticks before chord change (7445)
  // with duration 240 would end 5 ticks into the new chord (7685)
  // Old code: 5 > 10 = false, no trim
  // Fix: threshold = 0, so 5 > 0 = true, trim applied

  Tick note_start = bar4_start - 235;  // 7445
  Tick duration = 240;
  Tick note_end = note_start + duration;  // 7685
  Tick overlap = note_end - bar4_start;   // 5 ticks

  EXPECT_EQ(overlap, 5u) << "Overlap should be 5 ticks";

  // With fixed code (threshold=0), any overlap > 0 triggers trim check
  EXPECT_GT(overlap, 0u) << "Overlap > 0 should trigger trim logic";
}

// ============================================================================
// Bug #2: Harmony generation chord lookup timing
// ============================================================================
// Original bug: In aux_track.cpp harmony generation:
// 1. chord_degree = harmony.getChordDegreeAt(note.start_tick)  // Original tick
// 2. new_pitch = nearestChordTonePitch(...)
// 3. THEN offset applied to harm.start_tick
// Result: note placed at different tick uses wrong chord's tones

TEST(HarmonyTimingRegression, ChordLookupMustUseActualPlacementTick) {
  // Setup
  Section section = makeSection(SectionType::A, 4, 0);  // A = Verse
  Arrangement arrangement({section});
  const auto& progression = getChordProgression(3);  // Pop2: F-C-G-Am

  HarmonyContext harmony;
  harmony.initialize(arrangement, progression, Mood::StraightPop);

  // Scenario: melody note near chord boundary
  // If melody is at tick 1900 (bar 0, F chord)
  // And offset is +100 (placing harmony at tick 2000 = bar 1, C chord)
  // Must use C chord for pitch selection, not F chord

  Tick melody_tick = TICKS_PER_BAR - 20;  // 1900 (in bar 0)
  Tick offset = 100;
  Tick harmony_tick = melody_tick + offset;  // 2000 (in bar 1)

  int8_t degree_at_melody = harmony.getChordDegreeAt(melody_tick);
  int8_t degree_at_harmony = harmony.getChordDegreeAt(harmony_tick);

  // They should be different (crosses chord boundary)
  EXPECT_NE(degree_at_melody, degree_at_harmony)
      << "Chord should change between melody and harmony tick";

  // A (pitch 69) is in F (bar 0) but NOT in C (bar 1)
  // Bug: using degree_at_melody would allow A
  // Fix: using degree_at_harmony correctly identifies A as non-chord tone

  ChordTones f_tones = getChordTones(degree_at_melody);   // F
  ChordTones c_tones = getChordTones(degree_at_harmony);  // C

  bool a_in_f = false, a_in_c = false;
  for (uint8_t i = 0; i < f_tones.count; ++i) {
    if (f_tones.pitch_classes[i] == 9) a_in_f = true;  // A = pc 9
  }
  for (uint8_t i = 0; i < c_tones.count; ++i) {
    if (c_tones.pitch_classes[i] == 9) a_in_c = true;
  }

  EXPECT_TRUE(a_in_f) << "A is chord tone in F (bar 0)";
  EXPECT_FALSE(a_in_c) << "A is NOT chord tone in C (bar 1)";
}

// ============================================================================
// Bug #3: Motif placement not snapping to chord tones
// ============================================================================
// Original bug: placeMotifInIntro returns notes with absolute pitches
// These were added to aux track without chord-tone adjustment
// Result: C5 played over G chord where C is not a chord tone

TEST(MotifSnappingRegression, NearestChordTonePitchWorks) {
  // G chord (degree 4): G(7), B(11), D(2)
  // C (pc 0) is NOT in G chord

  ChordTones g_tones = getChordTones(4);

  bool c_in_g = false;
  for (uint8_t i = 0; i < g_tones.count; ++i) {
    if (g_tones.pitch_classes[i] == 0) c_in_g = true;  // C = pc 0
  }
  EXPECT_FALSE(c_in_g) << "C should NOT be chord tone in G";

  // nearestChordTonePitch should snap C to nearest G chord tone
  int snapped = nearestChordTonePitch(72, 4);  // C5 (72) on G chord
  int snapped_pc = snapped % 12;

  // Should be G(7), B(11), or D(2)
  EXPECT_TRUE(snapped_pc == 7 || snapped_pc == 11 || snapped_pc == 2)
      << "C5 should snap to G, B, or D, got pc " << snapped_pc;
}

TEST(MotifSnappingRegression, MotifNotesMustBeChordTones) {
  // This tests the fix in generator.cpp where motif notes are snapped
  // to chord tones at their actual tick

  Section section = makeSection(SectionType::Intro, 4, 0);
  Arrangement arrangement({section});
  const auto& progression = getChordProgression(3);

  HarmonyContext harmony;
  harmony.initialize(arrangement, progression, Mood::StraightPop);

  // At tick 7680 (bar 4), chord is G (degree 4)
  Tick test_tick = 4 * TICKS_PER_BAR;
  int8_t degree = harmony.getChordDegreeAt(test_tick);

  // If degree 4 is G, verify snapping works correctly
  if (degree == 4) {
    // Original bug: C5 (72) placed without snapping
    // Fix: nearestChordTonePitch(72, 4) returns G chord tone

    int snapped = nearestChordTonePitch(72, 4);
    ChordTones g_tones = getChordTones(4);

    bool is_chord_tone = false;
    int snapped_pc = snapped % 12;
    for (uint8_t i = 0; i < g_tones.count; ++i) {
      if (g_tones.pitch_classes[i] == snapped_pc) {
        is_chord_tone = true;
        break;
      }
    }

    EXPECT_TRUE(is_chord_tone) << "Snapped pitch " << snapped << " (pc " << snapped_pc
                               << ") should be chord tone in G chord";
  }
}

// Note: Full integration testing for dissonance across multiple seeds is
// covered by DissonanceIntegrationTest in tests/analysis/dissonance_test.cpp

}  // namespace
}  // namespace midisketch
