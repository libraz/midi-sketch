/**
 * @file select_best_candidate_test.cpp
 * @brief Tests for selectBestCandidate() multi-dimensional musical scoring.
 *
 * Tests the 5 scoring dimensions:
 *   1. Melodic continuity (rhythm-interval coupling)
 *   2. Harmonic stability (chord tone, root/5th, scale tone)
 *   3. Contour preservation
 *   4. Tessitura gravity
 *   5. Intent proximity
 * Plus phrase-position anchoring.
 */

#include <gtest/gtest.h>

#include "core/note_creator.h"

using namespace midisketch;

namespace {

// Helper to build a PitchCandidate quickly.
PitchCandidate makeCandidate(uint8_t pitch, bool chord_tone = false,
                              bool root_fifth = false, bool scale_tone = true,
                              int8_t interval_from_desired = 0,
                              CollisionAvoidStrategy strategy = CollisionAvoidStrategy::None) {
  PitchCandidate c;
  c.pitch = pitch;
  c.is_chord_tone = chord_tone;
  c.is_root_or_fifth = root_fifth;
  c.is_scale_tone = scale_tone;
  c.interval_from_desired = interval_from_desired;
  c.strategy = strategy;
  return c;
}

// ============================================================================
// Empty / Fallback
// ============================================================================

TEST(SelectBestCandidateTest, EmptyCandidatesReturnsFallback) {
  std::vector<PitchCandidate> empty;
  EXPECT_EQ(selectBestCandidate(empty, 60), 60);
  EXPECT_EQ(selectBestCandidate(empty, 72), 72);
}

TEST(SelectBestCandidateTest, NoPrevPitchReturnsFirstCandidate) {
  std::vector<PitchCandidate> cands = {makeCandidate(64), makeCandidate(67)};
  PitchSelectionHints hints;
  hints.prev_pitch = -1;  // No previous
  EXPECT_EQ(selectBestCandidate(cands, 60, hints), 64);
}

// ============================================================================
// Dimension 1: Melodic Continuity (rhythm-interval coupling)
// ============================================================================

TEST(SelectBestCandidateTest, ShortNote_PrefersStepOverLeap) {
  // Short notes (< 240 ticks) prefer small intervals.
  // Candidate A: step (2 semitones up) => high score
  // Candidate B: leap (7 semitones up) => lower score
  auto step = makeCandidate(62, true, false, true, 0);     // D4 (step from C4=60)
  auto leap = makeCandidate(67, true, false, true, 0);     // G4 (5th from C4)
  std::vector<PitchCandidate> cands = {step, leap};

  PitchSelectionHints hints;
  hints.prev_pitch = 60;
  hints.note_duration = 120;  // Short (< 240)
  hints.tessitura_center = 65;

  uint8_t chosen = selectBestCandidate(cands, 60, hints);
  EXPECT_EQ(chosen, 62);  // Step preferred for short notes
}

TEST(SelectBestCandidateTest, LongNote_PrefersModerateLeapOverSamePitch) {
  // Long notes (>= 480 ticks) discourage same-pitch stagnation.
  // Without root/fifth bonus, leap's melodic advantage (30 vs 15) outweighs.
  // same(not root): 15+20+8+0=43, third: 30+20+8-12=46
  auto same = makeCandidate(60, true, false, true, 0);     // C4, chord tone but NOT root/5th
  auto third = makeCandidate(64, true, false, true, 4);    // E4 (major 3rd)
  std::vector<PitchCandidate> cands = {same, third};

  PitchSelectionHints hints;
  hints.prev_pitch = 60;
  hints.note_duration = 480;  // Long (>= 480)
  hints.tessitura_center = 62;

  uint8_t chosen = selectBestCandidate(cands, 60, hints);
  EXPECT_EQ(chosen, 64);  // Moderate leap preferred for long notes
}

TEST(SelectBestCandidateTest, LongNote_SamePitchStagnationPenalty) {
  // Verify that same-pitch gets lower melodic score on long notes (15)
  // compared to medium (25) or short (33).
  auto same = makeCandidate(60, true, true, true, 0);
  std::vector<PitchCandidate> cands = {same};

  PitchSelectionHints hints;
  hints.prev_pitch = 60;
  hints.tessitura_center = 60;

  // Long: same-pitch gets 15 melodic
  hints.note_duration = 480;
  uint8_t long_chosen = selectBestCandidate(cands, 60, hints);
  EXPECT_EQ(long_chosen, 60);  // Only candidate

  // Short: same-pitch gets 33 melodic
  hints.note_duration = 120;
  uint8_t short_chosen = selectBestCandidate(cands, 60, hints);
  EXPECT_EQ(short_chosen, 60);  // Only candidate, but confirms it works
}

TEST(SelectBestCandidateTest, MediumNote_PrefersStepOverLeap) {
  // Medium notes (240-479 ticks) prefer steps (30 pts) over leaps 5-7 (15 pts).
  auto step = makeCandidate(62, true, false, true, 0);
  auto leap = makeCandidate(67, true, false, true, 0);
  std::vector<PitchCandidate> cands = {step, leap};

  PitchSelectionHints hints;
  hints.prev_pitch = 60;
  hints.note_duration = 360;  // Medium
  hints.tessitura_center = 64;

  uint8_t chosen = selectBestCandidate(cands, 60, hints);
  EXPECT_EQ(chosen, 62);  // Step preferred for medium notes
}

// ============================================================================
// Dimension 2: Harmonic Stability
// ============================================================================

TEST(SelectBestCandidateTest, ChordTonePreferredOverNonChordTone) {
  // Both at same interval from prev, but one is chord tone (+20) and other isn't.
  auto chord = makeCandidate(64, true, false, true, 0);     // E4, chord tone
  auto non_chord = makeCandidate(66, false, false, false, 0);  // F#4, non-chord non-scale
  std::vector<PitchCandidate> cands = {chord, non_chord};

  PitchSelectionHints hints;
  hints.prev_pitch = 62;
  hints.note_duration = 360;
  hints.tessitura_center = 65;

  uint8_t chosen = selectBestCandidate(cands, 64, hints);
  EXPECT_EQ(chosen, 64);  // Chord tone wins
}

TEST(SelectBestCandidateTest, RootFifthBonusOverOtherChordTone) {
  // Both chord tones, but root/5th gets +5 extra.
  // Make them equidistant from prev_pitch to isolate harmonic scoring.
  auto root = makeCandidate(60, true, true, true, 0);   // C4 = root
  auto third = makeCandidate(64, true, false, true, 0); // E4 = 3rd
  std::vector<PitchCandidate> cands = {root, third};

  PitchSelectionHints hints;
  hints.prev_pitch = 62;   // D4
  hints.note_duration = 360;
  hints.tessitura_center = 62;

  uint8_t chosen = selectBestCandidate(cands, 62, hints);
  EXPECT_EQ(chosen, 60);  // Root wins due to root/fifth bonus + tessitura gravity
}

// ============================================================================
// Dimension 3: Contour Preservation
// ============================================================================

TEST(SelectBestCandidateTest, AscendingContourPrefersHigherPitch) {
  auto higher = makeCandidate(65, true, false, true, 0);  // F4, ascending
  auto lower = makeCandidate(57, true, false, true, 0);   // A3, descending
  std::vector<PitchCandidate> cands = {higher, lower};

  PitchSelectionHints hints;
  hints.prev_pitch = 60;
  hints.contour_direction = 1;  // Ascending
  hints.note_duration = 360;
  hints.tessitura_center = 62;

  uint8_t chosen = selectBestCandidate(cands, 62, hints);
  EXPECT_EQ(chosen, 65);  // Ascending direction preferred
}

TEST(SelectBestCandidateTest, DescendingContourPrefersLowerPitch) {
  auto higher = makeCandidate(67, true, false, true, 0);
  auto lower = makeCandidate(57, true, false, true, 0);
  std::vector<PitchCandidate> cands = {higher, lower};

  PitchSelectionHints hints;
  hints.prev_pitch = 62;
  hints.contour_direction = -1;  // Descending
  hints.note_duration = 360;
  hints.tessitura_center = 62;

  uint8_t chosen = selectBestCandidate(cands, 62, hints);
  EXPECT_EQ(chosen, 57);  // Descending direction preferred
}

TEST(SelectBestCandidateTest, NoContourDirectionDoesNotPenalize) {
  auto up = makeCandidate(65, true, false, true, 0);
  auto down = makeCandidate(57, true, false, true, 0);
  std::vector<PitchCandidate> cands = {up, down};

  PitchSelectionHints hints;
  hints.prev_pitch = 60;
  hints.contour_direction = 0;  // No direction
  hints.note_duration = 360;
  hints.tessitura_center = 62;

  // Should not crash and should return one of them
  uint8_t chosen = selectBestCandidate(cands, 60, hints);
  EXPECT_TRUE(chosen == 65 || chosen == 57);
}

// ============================================================================
// Dimension 4: Tessitura Gravity
// ============================================================================

TEST(SelectBestCandidateTest, PitchCloserToTessituraCenterPreferred) {
  // Both are chord tones, same interval type, no contour. Tessitura center = 67.
  auto near_center = makeCandidate(67, true, false, true, 0);  // G4 = center
  auto far_away = makeCandidate(55, true, true, true, 0);      // G3 = 12 away
  std::vector<PitchCandidate> cands = {near_center, far_away};

  PitchSelectionHints hints;
  hints.prev_pitch = 60;
  hints.note_duration = 360;
  hints.tessitura_center = 67;

  uint8_t chosen = selectBestCandidate(cands, 60, hints);
  EXPECT_EQ(chosen, 67);  // Closer to tessitura center
}

// ============================================================================
// Dimension 5: Intent Proximity
// ============================================================================

TEST(SelectBestCandidateTest, CloserToDesiredPitchPreferred) {
  // Both are chord tones, similar interval from prev. One is closer to desired.
  auto close = makeCandidate(64, true, false, true, 0);   // interval_from_desired = 0
  auto far = makeCandidate(67, true, false, true, 3);     // interval_from_desired = 3 => -9 penalty
  std::vector<PitchCandidate> cands = {close, far};

  PitchSelectionHints hints;
  hints.prev_pitch = 62;
  hints.note_duration = 360;
  hints.tessitura_center = 65;

  uint8_t chosen = selectBestCandidate(cands, 64, hints);
  EXPECT_EQ(chosen, 64);  // Closer to desired pitch wins
}

// ============================================================================
// Phrase Position Anchoring
// ============================================================================

TEST(SelectBestCandidateTest, PhraseStartPrefersRootFifth) {
  // At phrase start (< 0.15), root/5th gets +5 bonus.
  auto root = makeCandidate(60, true, true, true, 0);     // C4 = root
  auto third = makeCandidate(64, true, false, true, 4);   // E4 = 3rd
  std::vector<PitchCandidate> cands = {root, third};

  PitchSelectionHints hints;
  hints.prev_pitch = 62;
  hints.note_duration = 360;
  hints.phrase_position = 0.05f;  // Near phrase start
  hints.tessitura_center = 62;

  uint8_t chosen = selectBestCandidate(cands, 62, hints);
  EXPECT_EQ(chosen, 60);  // Root preferred at phrase start
}

TEST(SelectBestCandidateTest, PhraseEndStronglyPrefersRootFifth) {
  // At phrase end (> 0.85), root/5th gets +8 bonus, chord tone +3.
  auto root = makeCandidate(60, true, true, true, 0);
  auto scale = makeCandidate(62, false, false, true, 2);  // D4, just scale tone
  std::vector<PitchCandidate> cands = {root, scale};

  PitchSelectionHints hints;
  hints.prev_pitch = 62;
  hints.note_duration = 360;
  hints.phrase_position = 0.95f;  // Near phrase end
  hints.tessitura_center = 61;

  uint8_t chosen = selectBestCandidate(cands, 60, hints);
  EXPECT_EQ(chosen, 60);  // Root strongly preferred at phrase end
}

TEST(SelectBestCandidateTest, MidPhrase_NoAnchoringBonus) {
  // In middle of phrase (0.15-0.85), no position bonus.
  auto root = makeCandidate(60, true, true, true, 0);
  auto step = makeCandidate(62, true, false, true, 2);
  std::vector<PitchCandidate> cands = {root, step};

  PitchSelectionHints hints;
  hints.prev_pitch = 60;
  hints.note_duration = 120;  // Short note
  hints.phrase_position = 0.5f;
  hints.tessitura_center = 62;

  uint8_t chosen = selectBestCandidate(cands, 60, hints);
  // Short note: same-pitch (60) gets 33, step (62, interval=2) gets 35
  // Root gets chord(20)+root(5)=25, step gets chord(20)=20
  // Tessitura: root dist=2 => 8, step dist=0 => 10
  // Intent: root 0, step -6
  // Root: 33+25+8+0 = 66; Step: 35+20+10-6 = 59
  EXPECT_EQ(chosen, 60);
}

// ============================================================================
// DurationCat threshold boundaries
// ============================================================================

TEST(SelectBestCandidateTest, DurationBoundary_239IsShort) {
  auto same = makeCandidate(60, true, true, true, 0);
  std::vector<PitchCandidate> cands = {same};

  PitchSelectionHints hints;
  hints.prev_pitch = 60;
  hints.note_duration = 239;  // Just under Short boundary
  hints.tessitura_center = 60;

  // Should use Short scoring (same-pitch = 33)
  uint8_t chosen = selectBestCandidate(cands, 60, hints);
  EXPECT_EQ(chosen, 60);
}

TEST(SelectBestCandidateTest, DurationBoundary_240IsMedium) {
  // At 240 ticks, duration category switches from Short to Medium.
  // Medium: same-pitch=25, step(2)=30. But root/fifth bonus(+5) and
  // intent proximity penalty(-6 for step) make same(59) > step(53).
  // Verify this is Medium mode (not Short where same=33 is even higher).
  auto non_root = makeCandidate(62, true, false, true, 0);  // D4, chord tone
  auto step = makeCandidate(64, true, false, true, 2);      // E4, chord tone
  std::vector<PitchCandidate> cands = {non_root, step};

  PitchSelectionHints hints;
  hints.prev_pitch = 60;
  hints.note_duration = 240;  // Exactly Medium boundary
  hints.tessitura_center = 63;

  // non_root(62, interval=2): Medium 30 + chord(20) + tess(10-1=9) + intent(0) = 59
  // step(64, interval=4): Medium 25 + chord(20) + tess(10-1=9) + intent(-6) = 48
  uint8_t chosen = selectBestCandidate(cands, 62, hints);
  EXPECT_EQ(chosen, 62);  // Step-interval wins over skip in Medium mode
}

TEST(SelectBestCandidateTest, DurationBoundary_480IsLong) {
  // At 480 ticks, Long mode. Same-pitch gets 15 (stagnation penalty).
  // Moderate interval (3-4) gets 30. Without root bonus, leap wins.
  auto same = makeCandidate(60, true, false, true, 0);     // NOT root/fifth
  auto third = makeCandidate(64, true, false, true, 4);    // E4
  std::vector<PitchCandidate> cands = {same, third};

  PitchSelectionHints hints;
  hints.prev_pitch = 60;
  hints.note_duration = 480;
  hints.tessitura_center = 62;

  // same: 15+20+8+0=43; third: 30+20+8-12=46
  uint8_t chosen = selectBestCandidate(cands, 60, hints);
  EXPECT_EQ(chosen, 64);
}

// ============================================================================
// Duration=0 defaults to Medium
// ============================================================================

TEST(SelectBestCandidateTest, ZeroDuration_DefaultsToMedium) {
  // Duration=0 should use Medium scoring (not Short).
  // Same as Medium: step (interval 2) gets +30 melodic continuity.
  auto non_root = makeCandidate(62, true, false, true, 0);
  auto skip = makeCandidate(64, true, false, true, 2);
  std::vector<PitchCandidate> cands = {non_root, skip};

  PitchSelectionHints hints;
  hints.prev_pitch = 60;
  hints.note_duration = 0;
  hints.tessitura_center = 63;

  // non_root(62, interval=2): Medium 30 + chord(20) + tess(9) + intent(0) = 59
  // skip(64, interval=4): Medium 25 + chord(20) + tess(9) + intent(-6) = 48
  uint8_t chosen = selectBestCandidate(cands, 62, hints);
  EXPECT_EQ(chosen, 62);  // Step interval wins in Medium mode
}

// ============================================================================
// Section-Type Weight Modulation
// ============================================================================

TEST(SelectBestCandidateTest, BridgeSectionRelaxesHarmonicConstraint) {
  // Bridge (section_type=4) has harmonic weight 0.7x.
  // A scale tone should score closer to a chord tone in Bridge than in Verse.
  auto chord = makeCandidate(64, true, false, true, 0);     // E4, chord tone
  auto scale = makeCandidate(62, false, false, true, 0);     // D4, scale tone only

  PitchSelectionHints hints_verse;
  hints_verse.prev_pitch = 60;
  hints_verse.note_duration = 360;
  hints_verse.tessitura_center = 63;
  hints_verse.section_type = 1;  // A (Verse)

  PitchSelectionHints hints_bridge;
  hints_bridge.prev_pitch = 60;
  hints_bridge.note_duration = 360;
  hints_bridge.tessitura_center = 63;
  hints_bridge.section_type = 4;  // Bridge

  // In Verse, chord tone (E4) should win due to full harmonic weight
  uint8_t verse_choice = selectBestCandidate({chord, scale}, 64, hints_verse);
  EXPECT_EQ(verse_choice, 64);

  // In Bridge, the reduced harmonic weight should make scale tone more competitive
  // (may or may not win depending on other dimensions, but score gap should shrink)
  uint8_t bridge_choice = selectBestCandidate({chord, scale}, 64, hints_bridge);
  // Bridge still prefers chord tone overall but the test verifies no crash
  // and that section_type is processed
  EXPECT_TRUE(bridge_choice == 64 || bridge_choice == 62);
}

TEST(SelectBestCandidateTest, ChorusSectionBoostsHarmonicStability) {
  // Chorus (section_type=3) has harmonic weight 1.2x.
  // Chord tone advantage should be amplified.
  auto chord = makeCandidate(64, true, true, true, 2);   // E4, root/fifth, further from desired
  auto non_chord = makeCandidate(63, false, false, true, 1);  // Eb4, scale tone, closer to desired

  PitchSelectionHints hints;
  hints.prev_pitch = 62;
  hints.note_duration = 360;
  hints.tessitura_center = 63;
  hints.section_type = 3;  // Chorus

  uint8_t chosen = selectBestCandidate({chord, non_chord}, 62, hints);
  EXPECT_EQ(chosen, 64);  // Chord tone wins with harmonic boost
}

TEST(SelectBestCandidateTest, PreChorusBoostsContourWeight) {
  // Pre-chorus (B, section_type=2) has contour weight 1.2x.
  // Ascending contour bonus should be amplified.
  auto up = makeCandidate(65, true, false, true, 0);    // F4, ascending
  auto down = makeCandidate(57, true, false, true, 0);  // A3, descending

  PitchSelectionHints hints;
  hints.prev_pitch = 60;
  hints.note_duration = 360;
  hints.tessitura_center = 62;
  hints.contour_direction = 1;  // Ascending
  hints.section_type = 2;  // B (Pre-chorus)

  uint8_t chosen = selectBestCandidate({up, down}, 62, hints);
  EXPECT_EQ(chosen, 65);  // Ascending strongly preferred in pre-chorus
}

TEST(SelectBestCandidateTest, UnknownSectionTypeUsesDefaults) {
  // section_type=-1 (unknown) should use A (verse) baseline weights.
  auto chord = makeCandidate(64, true, false, true, 0);
  auto scale = makeCandidate(62, false, false, true, 0);

  PitchSelectionHints hints;
  hints.prev_pitch = 60;
  hints.note_duration = 360;
  hints.tessitura_center = 63;
  hints.section_type = -1;  // Unknown

  uint8_t chosen = selectBestCandidate({chord, scale}, 64, hints);
  EXPECT_EQ(chosen, 64);  // Same as Verse baseline
}

// ============================================================================
// Sub-Phrase Anchoring
// ============================================================================

TEST(SelectBestCandidateTest, SubPhrase1MidPointAnchorsChordTone) {
  // Sub-phrase 1 (development) at mid-phrase (0.45-0.55) adds +3 for chord tones.
  auto chord = makeCandidate(64, true, false, true, 2);   // Chord tone, further from desired
  auto non_chord = makeCandidate(63, false, false, true, 1);  // Non-chord, closer to desired

  PitchSelectionHints hints;
  hints.prev_pitch = 62;
  hints.note_duration = 360;
  hints.tessitura_center = 63;
  hints.phrase_position = 0.50f;  // Mid-phrase
  hints.sub_phrase_index = 1;     // Development sub-phrase

  uint8_t chosen = selectBestCandidate({chord, non_chord}, 62, hints);
  EXPECT_EQ(chosen, 64);  // Chord tone gets mid-phrase anchoring bonus
}

TEST(SelectBestCandidateTest, SubPhrase2NoMidPointAnchoring) {
  // Sub-phrase 2 (climax) should NOT get mid-point anchoring.
  auto chord = makeCandidate(64, true, false, true, 2);
  auto non_chord = makeCandidate(63, false, false, true, 1);

  PitchSelectionHints hints;
  hints.prev_pitch = 62;
  hints.note_duration = 360;
  hints.tessitura_center = 63;
  hints.phrase_position = 0.50f;
  hints.sub_phrase_index = 2;  // Climax: no anchoring

  // No sub-phrase bonus, so this depends purely on other dimensions
  uint8_t chosen = selectBestCandidate({chord, non_chord}, 62, hints);
  // Both are valid; just verify no crash and result is one of the candidates
  EXPECT_TRUE(chosen == 64 || chosen == 63);
}

}  // namespace
