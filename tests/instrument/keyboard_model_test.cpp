/**
 * @file keyboard_model_test.cpp
 * @brief Unit tests for PianoModel and KeyboardNoteFactory.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "core/midi_track.h"
#include "core/midi_track.h"
#include "core/production_blueprint.h"
#include "instrument/keyboard/keyboard_instrument.h"
#include "instrument/keyboard/keyboard_note_factory.h"
#include "instrument/keyboard/keyboard_types.h"
#include "instrument/keyboard/piano_model.h"
#include "test_support/stub_harmony_context.h"

namespace midisketch {
namespace {

// =============================================================================
// PianoModel - Pitch Range Tests
// =============================================================================

TEST(PianoModelTest, PitchRange) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  EXPECT_EQ(piano.getLowestPitch(), 21);   // A0
  EXPECT_EQ(piano.getHighestPitch(), 108); // C8
}

TEST(PianoModelTest, PitchPlayabilityMiddleOfRange) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  EXPECT_TRUE(piano.isPitchPlayable(60));  // C4 - middle of range
}

TEST(PianoModelTest, PitchPlayabilityBoundaries) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  EXPECT_TRUE(piano.isPitchPlayable(21));    // A0 - lowest
  EXPECT_TRUE(piano.isPitchPlayable(108));   // C8 - highest
  EXPECT_FALSE(piano.isPitchPlayable(20));   // Below range
  EXPECT_FALSE(piano.isPitchPlayable(109));  // Above range
}

TEST(PianoModelTest, PitchPlayabilityExtremes) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  EXPECT_FALSE(piano.isPitchPlayable(0));    // Way below
  EXPECT_FALSE(piano.isPitchPlayable(127));  // MIDI max, still above piano range
}

// =============================================================================
// PianoModel - One Hand Playability
// =============================================================================

TEST(PianoModelTest, OneHandWithinNormalSpanBeginnerOK) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  // C4-G4 = 7 semitones = beginner normal_span, within max_span(8)
  EXPECT_TRUE(beginner.isPlayableByOneHand({60, 67}));
}

TEST(PianoModelTest, OneHandAtMaxSpanBeginnerOK) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  // C4-Ab4 = 8 semitones = beginner max_span, OK
  EXPECT_TRUE(beginner.isPlayableByOneHand({60, 68}));
}

TEST(PianoModelTest, OneHandBeyondMaxSpanBeginnerNG) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  // C4-A4 = 9 semitones > beginner max_span(8), NOT OK
  EXPECT_FALSE(beginner.isPlayableByOneHand({60, 69}));
}

TEST(PianoModelTest, OneHandOctaveBeginnerNG) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  // C4-C5 = 12 semitones > beginner max_span(8), NOT OK
  EXPECT_FALSE(beginner.isPlayableByOneHand({60, 72}));
}

TEST(PianoModelTest, OneHandAtMaxSpanIntermediateOK) {
  PianoModel intermediate(InstrumentSkillLevel::Intermediate);
  // C4-Bb4 = 10 = intermediate max_span, OK
  EXPECT_TRUE(intermediate.isPlayableByOneHand({60, 70}));
}

TEST(PianoModelTest, OneHandBeyondMaxSpanIntermediateNG) {
  PianoModel intermediate(InstrumentSkillLevel::Intermediate);
  // C4-B4 = 11 > intermediate max_span(10), NOT OK
  EXPECT_FALSE(intermediate.isPlayableByOneHand({60, 71}));
}

TEST(PianoModelTest, OneHandOctaveIntermediateNG) {
  PianoModel intermediate(InstrumentSkillLevel::Intermediate);
  // C4-C5 = 12 > intermediate max_span(10), NOT OK
  EXPECT_FALSE(intermediate.isPlayableByOneHand({60, 72}));
}

TEST(PianoModelTest, OneHandOctaveAdvancedOK) {
  PianoModel advanced(InstrumentSkillLevel::Advanced);
  // C4-C5 = 12 = advanced max_span(12), OK
  EXPECT_TRUE(advanced.isPlayableByOneHand({60, 72}));
}

TEST(PianoModelTest, OneHandOctaveVirtuosoOK) {
  PianoModel virtuoso(InstrumentSkillLevel::Virtuoso);
  // C4-C5 = 12 < virtuoso max_span(14), OK
  EXPECT_TRUE(virtuoso.isPlayableByOneHand({60, 72}));
}

TEST(PianoModelTest, OneHandAtMaxSpanVirtuosoOK) {
  PianoModel virtuoso(InstrumentSkillLevel::Virtuoso);
  // 14 semitones = virtuoso max_span, OK
  EXPECT_TRUE(virtuoso.isPlayableByOneHand({60, 74}));
}

TEST(PianoModelTest, OneHandBeyondMaxSpanVirtuosoNG) {
  PianoModel virtuoso(InstrumentSkillLevel::Virtuoso);
  // 15 semitones > virtuoso max_span(14), NOT OK
  EXPECT_FALSE(virtuoso.isPlayableByOneHand({60, 75}));
}

TEST(PianoModelTest, OneHandTooManyNotesBeginnerNG) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  // Beginner max_notes = 4; 5 notes within span should fail
  EXPECT_FALSE(beginner.isPlayableByOneHand({60, 61, 62, 63, 64}));
}

TEST(PianoModelTest, OneHandMaxNotesBeginnerOK) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  // 4 notes within span (C4-E4-F4-G4, span=7) should be OK
  EXPECT_TRUE(beginner.isPlayableByOneHand({60, 64, 65, 67}));
}

TEST(PianoModelTest, OneHandTooManyNotesIntermediateNG) {
  PianoModel intermediate(InstrumentSkillLevel::Intermediate);
  // Intermediate max_notes = 5; 6 notes within span should fail
  EXPECT_FALSE(intermediate.isPlayableByOneHand({60, 61, 62, 63, 64, 65}));
}

TEST(PianoModelTest, OneHandEmpty) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  EXPECT_TRUE(piano.isPlayableByOneHand({}));
}

TEST(PianoModelTest, OneHandSingleNote) {
  PianoModel piano(InstrumentSkillLevel::Beginner);
  EXPECT_TRUE(piano.isPlayableByOneHand({60}));
}

TEST(PianoModelTest, OneHandUnsortedInput) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  // Notes not sorted: G4, C4, E4 - span = 7 semitones, should still be OK
  EXPECT_TRUE(piano.isPlayableByOneHand({67, 60, 64}));
}

// =============================================================================
// PianoModel - Hand Assignment
// =============================================================================

TEST(PianoModelTest, AssignHandsEmpty) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  auto result = piano.assignHands({});
  EXPECT_TRUE(result.is_playable);
  EXPECT_TRUE(result.left_hand.empty());
  EXPECT_TRUE(result.right_hand.empty());
}

TEST(PianoModelTest, SingleHandVoicingGoesToRightHand) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  // Simple triad within one hand's span goes to right hand
  auto result = piano.assignHands({60, 64, 67});
  EXPECT_TRUE(result.is_playable);
  EXPECT_TRUE(result.left_hand.empty());
  EXPECT_EQ(result.right_hand.size(), 3u);
}

TEST(PianoModelTest, WideVoicingSplitsBetweenHands) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  // C3-E3-G3-C4-E4-G4 - spans 19 semitones, needs both hands
  auto result = piano.assignHands({48, 52, 55, 60, 64, 67});
  EXPECT_TRUE(result.is_playable);
  EXPECT_FALSE(result.left_hand.empty());
  EXPECT_FALSE(result.right_hand.empty());
  // All notes should be accounted for
  EXPECT_EQ(result.left_hand.size() + result.right_hand.size(), 6u);
}

TEST(PianoModelTest, HandAssignmentPreservesAllNotes) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  std::vector<uint8_t> input = {48, 55, 62, 69};
  auto result = beginner.assignHands(input);
  // All notes should be accounted for regardless of playability
  EXPECT_EQ(result.left_hand.size() + result.right_hand.size(), 4u);
}

TEST(PianoModelTest, HandAssignmentOverflowCorrection) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  // 6 notes in wide range: overflow correction should move notes between hands
  auto result = beginner.assignHands({36, 43, 50, 57, 64, 71});
  // All notes preserved
  EXPECT_EQ(result.left_hand.size() + result.right_hand.size(), 6u);
  // Each hand should have notes
  EXPECT_FALSE(result.left_hand.empty());
  EXPECT_FALSE(result.right_hand.empty());
}

TEST(PianoModelTest, HandAssignmentResultsSorted) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  auto result = piano.assignHands({48, 52, 55, 60, 64, 67});
  // Left hand notes should be sorted
  EXPECT_TRUE(std::is_sorted(result.left_hand.begin(), result.left_hand.end()));
  // Right hand notes should be sorted
  EXPECT_TRUE(std::is_sorted(result.right_hand.begin(), result.right_hand.end()));
}

// =============================================================================
// PianoModel - Voicing Playability
// =============================================================================

TEST(PianoModelTest, SimpleTriadPlayable) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  EXPECT_TRUE(piano.isVoicingPlayable({60, 64, 67}));
}

TEST(PianoModelTest, TwoHandVoicingPlayable) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  // Split across two hands: C3-E3-G3-C4-E4-G4
  EXPECT_TRUE(piano.isVoicingPlayable({48, 52, 55, 60, 64, 67}));
}

TEST(PianoModelTest, OutOfRangePitchNotPlayable) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  EXPECT_FALSE(piano.isVoicingPlayable({15, 60, 67}));  // 15 < A0(21)
}

TEST(PianoModelTest, AboveRangePitchNotPlayable) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  EXPECT_FALSE(piano.isVoicingPlayable({60, 67, 110}));  // 110 > C8(108)
}

TEST(PianoModelTest, EmptyVoicingPlayable) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  EXPECT_TRUE(piano.isVoicingPlayable({}));
}

TEST(PianoModelTest, SingleNotePlayable) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  EXPECT_TRUE(piano.isVoicingPlayable({60}));
}

TEST(PianoModelTest, WideVoicingTwoNotesPlayable) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  // C3 + C5: 24 semitone span, too wide for one hand but playable with two
  EXPECT_FALSE(piano.isPlayableByOneHand({48, 72}));
  EXPECT_TRUE(piano.isVoicingPlayable({48, 72}));
}

// =============================================================================
// PianoModel - Transition Feasibility
// =============================================================================

TEST(PianoModelTest, FirstVoicingAlwaysFeasible) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  EXPECT_TRUE(piano.isTransitionFeasible({}, {60, 64, 67}, 480, 120));
}

TEST(PianoModelTest, EmptyTargetAlwaysFeasible) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  EXPECT_TRUE(piano.isTransitionFeasible({60, 64, 67}, {}, 480, 120));
}

TEST(PianoModelTest, SmallTransitionFeasible) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  // C major to F major - small hand movement, plenty of time
  EXPECT_TRUE(piano.isTransitionFeasible({60, 64, 67}, {60, 65, 69}, 480, 120));
}

TEST(PianoModelTest, SameVoicingAlwaysFeasible) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  // Same voicing, zero shift - always feasible even with minimal time
  EXPECT_TRUE(piano.isTransitionFeasible({60, 64, 67}, {60, 64, 67}, 10, 200));
}

TEST(PianoModelTest, LargeTransitionVeryShortTimeBeginnerNG) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  // C3 triad to C6 triad = 3 octave jump at 180 BPM with very few ticks
  // Beginner shift_time = 90 ticks at reference 120 BPM
  // At 180 BPM: required_ticks = (90 * 180) / 120 = 135 base + leap penalty
  EXPECT_FALSE(beginner.isTransitionFeasible({48, 52, 55}, {84, 88, 91}, 60, 180));
}

TEST(PianoModelTest, TransitionInfeasibleIfTargetNotPlayable) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  // Target has out-of-range pitch
  EXPECT_FALSE(piano.isTransitionFeasible({60, 64, 67}, {15, 60, 67}, 480, 120));
}

// =============================================================================
// PianoModel - Transition Cost
// =============================================================================

TEST(PianoModelTest, TransitionCostZeroForFirstVoicing) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  auto cost = piano.calculateTransitionCost({}, {60, 64, 67}, 480, 120);
  EXPECT_EQ(cost.total_cost, 0.0f);
  EXPECT_TRUE(cost.is_feasible);
}

TEST(PianoModelTest, TransitionCostZeroForEmptyTarget) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  auto cost = piano.calculateTransitionCost({60, 64, 67}, {}, 480, 120);
  EXPECT_EQ(cost.total_cost, 0.0f);
}

TEST(PianoModelTest, TransitionCostIncreasesWithDistance) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  // Small move: C major -> D major
  auto small_cost = piano.calculateTransitionCost({60, 64, 67}, {62, 66, 69}, 480, 120);
  // Large move: C major -> C major two octaves up
  auto large_cost = piano.calculateTransitionCost({60, 64, 67}, {84, 88, 91}, 480, 120);
  EXPECT_LT(small_cost.total_cost, large_cost.total_cost);
}

TEST(PianoModelTest, TransitionCostSameVoicingIsZero) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  auto cost = piano.calculateTransitionCost({60, 64, 67}, {60, 64, 67}, 480, 120);
  EXPECT_EQ(cost.total_cost, 0.0f);
}

TEST(PianoModelTest, TransitionCostDecomposesIntoHands) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  // Two-hand voicing with movement in both hands
  auto cost = piano.calculateTransitionCost(
      {48, 52, 60, 64}, {52, 55, 64, 67}, 480, 120);
  // Total cost should be sum of hand costs
  EXPECT_FLOAT_EQ(cost.total_cost, cost.left_hand_cost + cost.right_hand_cost);
}

// =============================================================================
// PianoModel - BPM Boundary Tests
// =============================================================================

TEST(PianoModelTest, BPMBoundaryFeasibility) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  std::vector<uint8_t> from = {48, 52, 55};
  std::vector<uint8_t> to = {72, 76, 79};
  uint32_t ticks = 240;  // Half beat

  // Low BPM should be more feasible than high BPM for the same transition
  bool low_bpm = beginner.isTransitionFeasible(from, to, ticks, 80);
  bool high_bpm = beginner.isTransitionFeasible(from, to, ticks, 200);

  // High BPM should not be MORE feasible than low BPM
  if (!low_bpm) {
    EXPECT_FALSE(high_bpm);
  }
}

TEST(PianoModelTest, TempoAdjustmentCostBehavior) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  std::vector<uint8_t> from = {48, 52, 55};
  std::vector<uint8_t> to = {60, 64, 67};

  // At reference BPM (120), no tempo adjustment
  auto cost_120 = piano.calculateTransitionCost(from, to, 480, 120);
  // At high BPM (180), tempo penalty may apply
  auto cost_180 = piano.calculateTransitionCost(from, to, 480, 180);

  // Higher BPM should generally mean higher or equal cost
  EXPECT_LE(cost_120.total_cost, cost_180.total_cost);
}

// =============================================================================
// PianoModel - suggestPlayableVoicing
// =============================================================================

TEST(PianoModelTest, SuggestPlayableReturnsOriginalIfPlayable) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  std::vector<uint8_t> voicing = {60, 64, 67};
  auto result = piano.suggestPlayableVoicing(voicing, 0);
  EXPECT_EQ(result, voicing);
}

TEST(PianoModelTest, SuggestPlayableEmpty) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  auto result = piano.suggestPlayableVoicing({}, 0);
  EXPECT_TRUE(result.empty());
}

TEST(PianoModelTest, SuggestPlayableTriesInversions) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  // C3-E4-G4 = span 19, not playable by one hand for beginner
  // After inversion (move C3 up octave): C4-E4-G4 = span 7
  std::vector<uint8_t> wide = {48, 64, 67};
  auto result = beginner.suggestPlayableVoicing(wide, 0);
  EXPECT_FALSE(result.empty());
  // Should be playable after suggestion
  EXPECT_TRUE(beginner.isVoicingPlayable(result));
}

TEST(PianoModelTest, SuggestPlayableAlwaysReturnsNonEmpty) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  // Very wide voicing spanning many octaves
  std::vector<uint8_t> wide = {36, 48, 60, 72, 84, 96};
  auto result = beginner.suggestPlayableVoicing(wide, 0);
  // Should always return something (fallback returns original)
  EXPECT_FALSE(result.empty());
}

TEST(PianoModelTest, SuggestPlayableReturnsPlayableResult) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  // 4-note voicing that needs adjustment
  std::vector<uint8_t> voicing = {60, 64, 67, 72};
  auto suggested = beginner.suggestPlayableVoicing(voicing, 0);
  if (!suggested.empty()) {
    // The suggestion should be playable or be the original fallback
    EXPECT_TRUE(beginner.isVoicingPlayable(suggested) || suggested == voicing);
  }
}

TEST(PianoModelTest, SuggestPlayableOmitsFifth) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  // 5-note voicing with root=C: C4-E4-G4-Bb4-D5
  // root_pitch_class=0, 5th is pitch class 7 (G)
  std::vector<uint8_t> voicing = {60, 64, 67, 70, 74};
  auto result = beginner.suggestPlayableVoicing(voicing, 0);
  EXPECT_FALSE(result.empty());
}

TEST(PianoModelTest, SuggestPlayableClosePosition) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  // Notes spread across 3 octaves but only 3 notes
  // C3-E4-G5: span=31 semitones
  std::vector<uint8_t> spread = {48, 64, 79};
  auto result = beginner.suggestPlayableVoicing(spread, 0);
  EXPECT_FALSE(result.empty());
}

// =============================================================================
// PianoModel - State Management
// =============================================================================

TEST(PianoModelTest, InitialStateIsReset) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  auto state = piano.getState();
  EXPECT_EQ(state.last_voicing_span, 0);
  EXPECT_EQ(state.left.note_count, 0);
  EXPECT_EQ(state.right.note_count, 0);
  EXPECT_EQ(state.last_split_key, 60);
  EXPECT_FALSE(state.left.isInitialized());
  EXPECT_FALSE(state.right.isInitialized());
}

TEST(PianoModelTest, UpdateStateSingleHandVoicing) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);

  // C major triad - fits in one hand, goes to right hand
  piano.updateState({60, 64, 67});

  auto state = piano.getState();
  EXPECT_EQ(state.last_voicing_span, 7);  // 67 - 60
  // Right hand should be initialized (single-hand goes to right)
  EXPECT_TRUE(state.right.isInitialized());
  EXPECT_EQ(state.right.note_count, 3);
  EXPECT_EQ(state.right.last_low, 60);
  EXPECT_EQ(state.right.last_high, 67);
  // Left hand should NOT be initialized
  EXPECT_FALSE(state.left.isInitialized());
}

TEST(PianoModelTest, UpdateStateTwoHandVoicing) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);

  // Two-hand voicing spanning 19 semitones
  piano.updateState({48, 52, 55, 60, 64, 67});

  auto state = piano.getState();
  EXPECT_EQ(state.last_voicing_span, 19);  // 67 - 48
  // Both hands should be initialized
  EXPECT_TRUE(state.left.isInitialized());
  EXPECT_TRUE(state.right.isInitialized());
}

TEST(PianoModelTest, ResetStateClearsAll) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);

  // Play some notes to set state
  piano.updateState({48, 52, 55, 60, 64, 67});
  EXPECT_NE(piano.getState().last_voicing_span, 0);

  // Reset
  piano.resetState();

  auto state = piano.getState();
  EXPECT_EQ(state.last_voicing_span, 0);
  EXPECT_EQ(state.left.note_count, 0);
  EXPECT_EQ(state.right.note_count, 0);
  EXPECT_EQ(state.last_split_key, 60);
  EXPECT_FALSE(state.left.isInitialized());
  EXPECT_FALSE(state.right.isInitialized());
  EXPECT_EQ(state.left.last_center, 0);
  EXPECT_EQ(state.right.last_center, 0);
  EXPECT_EQ(state.pedal, PedalState::Off);
}

TEST(PianoModelTest, UpdateStateEmptyDoesNotChange) {
  PianoModel piano(InstrumentSkillLevel::Intermediate);
  piano.updateState({60, 64, 67});
  auto before = piano.getState();

  // Empty update should not change state
  piano.updateState({});
  auto after = piano.getState();

  EXPECT_EQ(before.last_voicing_span, after.last_voicing_span);
  EXPECT_EQ(before.right.note_count, after.right.note_count);
}

// =============================================================================
// PianoModel - Skill Level Constraints
// =============================================================================

TEST(PianoModelTest, BeginnerConstraints) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  EXPECT_EQ(beginner.getSpanConstraints().normal_span, 7);
  EXPECT_EQ(beginner.getSpanConstraints().max_span, 8);
  EXPECT_EQ(beginner.getSpanConstraints().max_notes, 4);
  EXPECT_EQ(beginner.getHandPhysics().position_shift_time, 90);
}

TEST(PianoModelTest, IntermediateConstraints) {
  PianoModel intermediate(InstrumentSkillLevel::Intermediate);
  EXPECT_EQ(intermediate.getSpanConstraints().normal_span, 8);
  EXPECT_EQ(intermediate.getSpanConstraints().max_span, 10);
  EXPECT_EQ(intermediate.getSpanConstraints().max_notes, 5);
  EXPECT_EQ(intermediate.getHandPhysics().position_shift_time, 60);
}

TEST(PianoModelTest, AdvancedConstraints) {
  PianoModel advanced(InstrumentSkillLevel::Advanced);
  EXPECT_EQ(advanced.getSpanConstraints().normal_span, 10);
  EXPECT_EQ(advanced.getSpanConstraints().max_span, 12);
  EXPECT_EQ(advanced.getSpanConstraints().max_notes, 5);
  EXPECT_EQ(advanced.getHandPhysics().position_shift_time, 40);
}

TEST(PianoModelTest, VirtuosoConstraints) {
  PianoModel virtuoso(InstrumentSkillLevel::Virtuoso);
  EXPECT_EQ(virtuoso.getSpanConstraints().normal_span, 12);
  EXPECT_EQ(virtuoso.getSpanConstraints().max_span, 14);
  EXPECT_EQ(virtuoso.getSpanConstraints().max_notes, 5);
  EXPECT_EQ(virtuoso.getHandPhysics().position_shift_time, 25);
}

TEST(PianoModelTest, SkillLevelMonotonicity) {
  PianoModel beginner(InstrumentSkillLevel::Beginner);
  PianoModel intermediate(InstrumentSkillLevel::Intermediate);
  PianoModel advanced(InstrumentSkillLevel::Advanced);
  PianoModel virtuoso(InstrumentSkillLevel::Virtuoso);

  // Normal span increases with skill
  EXPECT_LT(beginner.getSpanConstraints().normal_span,
             intermediate.getSpanConstraints().normal_span);
  EXPECT_LT(intermediate.getSpanConstraints().normal_span,
             advanced.getSpanConstraints().normal_span);
  EXPECT_LT(advanced.getSpanConstraints().normal_span,
             virtuoso.getSpanConstraints().normal_span);

  // Shift time decreases with skill (faster repositioning)
  EXPECT_GT(beginner.getHandPhysics().position_shift_time,
            intermediate.getHandPhysics().position_shift_time);
  EXPECT_GT(intermediate.getHandPhysics().position_shift_time,
            advanced.getHandPhysics().position_shift_time);
  EXPECT_GT(advanced.getHandPhysics().position_shift_time,
            virtuoso.getHandPhysics().position_shift_time);
}

TEST(PianoModelTest, FromSkillLevelFactory) {
  auto piano = PianoModel::fromSkillLevel(InstrumentSkillLevel::Advanced);
  EXPECT_EQ(piano.getSpanConstraints().normal_span, 10);
  EXPECT_EQ(piano.getHandPhysics().position_shift_time, 40);
}

// =============================================================================
// PianoModel - Custom Constraints Constructor
// =============================================================================

TEST(PianoModelTest, CustomConstraintsConstructor) {
  KeyboardSpanConstraints custom_span;
  custom_span.normal_span = 9;
  custom_span.max_span = 11;
  custom_span.max_notes = 4;
  custom_span.span_penalty = 8.0f;

  KeyboardHandPhysics custom_physics;
  custom_physics.position_shift_time = 50;
  custom_physics.max_repeated_note_speed = 5;

  PianoModel piano(custom_span, custom_physics);
  EXPECT_EQ(piano.getSpanConstraints().normal_span, 9);
  EXPECT_EQ(piano.getSpanConstraints().max_span, 11);
  EXPECT_EQ(piano.getSpanConstraints().max_notes, 4);
  EXPECT_EQ(piano.getHandPhysics().position_shift_time, 50);
}

// =============================================================================
// KeyboardSpanConstraints - Stretch Penalty
// =============================================================================

TEST(KeyboardSpanConstraintsTest, PenaltyZeroWithinNormalSpan) {
  auto span = KeyboardSpanConstraints::intermediate();
  EXPECT_EQ(span.calculateStretchPenalty(0), 0.0f);  // No span
  EXPECT_EQ(span.calculateStretchPenalty(5), 0.0f);  // Well within normal
  EXPECT_EQ(span.calculateStretchPenalty(8), 0.0f);  // At normal_span
}

TEST(KeyboardSpanConstraintsTest, PenaltyBetweenNormalAndMax) {
  auto span = KeyboardSpanConstraints::intermediate();
  // 9 = 1 semitone above normal_span(8), penalty = 1 * 10.0 = 10.0
  EXPECT_FLOAT_EQ(span.calculateStretchPenalty(9), 10.0f);
  // 10 = 2 semitones above normal_span(8), at max_span, penalty = 2 * 10.0 = 20.0
  EXPECT_FLOAT_EQ(span.calculateStretchPenalty(10), 20.0f);
}

TEST(KeyboardSpanConstraintsTest, PenaltyImpossibleBeyondMax) {
  auto span = KeyboardSpanConstraints::intermediate();
  // 11 > max_span(10), physically impossible
  EXPECT_EQ(span.calculateStretchPenalty(11), 999.0f);
  EXPECT_EQ(span.calculateStretchPenalty(20), 999.0f);
}

TEST(KeyboardSpanConstraintsTest, PenaltyScalesBySkillLevel) {
  auto beginner_span = KeyboardSpanConstraints::beginner();
  auto virtuoso_span = KeyboardSpanConstraints::virtuoso();

  // Beginner penalty per semitone is higher than virtuoso
  EXPECT_GT(beginner_span.span_penalty, virtuoso_span.span_penalty);
}

TEST(KeyboardSpanConstraintsTest, FactoryMethods) {
  auto beg = KeyboardSpanConstraints::beginner();
  auto inter = KeyboardSpanConstraints::intermediate();
  auto adv = KeyboardSpanConstraints::advanced();
  auto virt = KeyboardSpanConstraints::virtuoso();

  EXPECT_EQ(beg.normal_span, 7);
  EXPECT_EQ(inter.normal_span, 8);
  EXPECT_EQ(adv.normal_span, 10);
  EXPECT_EQ(virt.normal_span, 12);
}

// =============================================================================
// KeyboardHandPhysics - Factory Methods
// =============================================================================

TEST(KeyboardHandPhysicsTest, FactoryMethods) {
  auto beg = KeyboardHandPhysics::beginner();
  auto inter = KeyboardHandPhysics::intermediate();
  auto adv = KeyboardHandPhysics::advanced();
  auto virt = KeyboardHandPhysics::virtuoso();

  EXPECT_EQ(beg.position_shift_time, 90);
  EXPECT_EQ(inter.position_shift_time, 60);
  EXPECT_EQ(adv.position_shift_time, 40);
  EXPECT_EQ(virt.position_shift_time, 25);

  EXPECT_EQ(beg.max_repeated_note_speed, 2);
  EXPECT_EQ(inter.max_repeated_note_speed, 3);
  EXPECT_EQ(adv.max_repeated_note_speed, 4);
  EXPECT_EQ(virt.max_repeated_note_speed, 6);
}

// =============================================================================
// HandState Tests
// =============================================================================

TEST(HandStateTest, DefaultNotInitialized) {
  HandState hand;
  EXPECT_FALSE(hand.isInitialized());
  EXPECT_EQ(hand.note_count, 0);
}

TEST(HandStateTest, InitializedAfterSetting) {
  HandState hand;
  hand.note_count = 3;
  EXPECT_TRUE(hand.isInitialized());
}

TEST(HandStateTest, ResetClearsState) {
  HandState hand;
  hand.last_center = 60;
  hand.last_low = 55;
  hand.last_high = 67;
  hand.note_count = 3;

  hand.reset();

  EXPECT_EQ(hand.last_center, 0);
  EXPECT_EQ(hand.last_low, 0);
  EXPECT_EQ(hand.last_high, 0);
  EXPECT_EQ(hand.note_count, 0);
  EXPECT_FALSE(hand.isInitialized());
}

TEST(HandStateTest, GetLastSpanSingleNote) {
  HandState hand;
  hand.last_low = 60;
  hand.last_high = 60;
  hand.note_count = 1;
  EXPECT_EQ(hand.getLastSpan(), 0);
}

TEST(HandStateTest, GetLastSpanMultipleNotes) {
  HandState hand;
  hand.last_low = 60;
  hand.last_high = 67;
  hand.note_count = 3;
  EXPECT_EQ(hand.getLastSpan(), 7);
}

TEST(HandStateTest, GetLastSpanNoNotes) {
  HandState hand;
  hand.note_count = 0;
  EXPECT_EQ(hand.getLastSpan(), 0);
}

// =============================================================================
// KeyboardState Tests
// =============================================================================

TEST(KeyboardStateTest, DefaultValues) {
  KeyboardState state;
  EXPECT_EQ(state.last_split_key, 60);
  EXPECT_EQ(state.last_voicing_span, 0);
  EXPECT_EQ(state.pedal, PedalState::Off);
}

TEST(KeyboardStateTest, ResetAll) {
  KeyboardState state;
  state.left.note_count = 3;
  state.right.note_count = 4;
  state.last_split_key = 72;
  state.last_voicing_span = 19;
  state.pedal = PedalState::On;

  state.reset();

  EXPECT_EQ(state.left.note_count, 0);
  EXPECT_EQ(state.right.note_count, 0);
  EXPECT_EQ(state.last_split_key, 60);
  EXPECT_EQ(state.last_voicing_span, 0);
  EXPECT_EQ(state.pedal, PedalState::Off);
}

// =============================================================================
// KeyboardPlayabilityCost Tests
// =============================================================================

TEST(KeyboardPlayabilityCostTest, DefaultValues) {
  KeyboardPlayabilityCost cost;
  EXPECT_EQ(cost.left_hand_cost, 0.0f);
  EXPECT_EQ(cost.right_hand_cost, 0.0f);
  EXPECT_EQ(cost.total_cost, 0.0f);
  EXPECT_TRUE(cost.is_feasible);
}

TEST(KeyboardPlayabilityCostTest, AdditionOperator) {
  KeyboardPlayabilityCost cost_a;
  cost_a.left_hand_cost = 5.0f;
  cost_a.right_hand_cost = 3.0f;
  cost_a.total_cost = 8.0f;
  cost_a.is_feasible = true;

  KeyboardPlayabilityCost cost_b;
  cost_b.left_hand_cost = 2.0f;
  cost_b.right_hand_cost = 4.0f;
  cost_b.total_cost = 6.0f;
  cost_b.is_feasible = true;

  cost_a += cost_b;

  EXPECT_FLOAT_EQ(cost_a.left_hand_cost, 7.0f);
  EXPECT_FLOAT_EQ(cost_a.right_hand_cost, 7.0f);
  EXPECT_FLOAT_EQ(cost_a.total_cost, 14.0f);
  EXPECT_TRUE(cost_a.is_feasible);
}

TEST(KeyboardPlayabilityCostTest, InfeasiblePropagates) {
  KeyboardPlayabilityCost cost_a;
  cost_a.is_feasible = true;

  KeyboardPlayabilityCost cost_b;
  cost_b.is_feasible = false;

  cost_a += cost_b;
  EXPECT_FALSE(cost_a.is_feasible);
}

// =============================================================================
// VoicingHandAssignment Tests
// =============================================================================

TEST(VoicingHandAssignmentTest, DefaultNotPlayable) {
  VoicingHandAssignment assignment;
  EXPECT_FALSE(assignment.is_playable);
  EXPECT_TRUE(assignment.left_hand.empty());
  EXPECT_TRUE(assignment.right_hand.empty());
  EXPECT_EQ(assignment.split_point, 60);
}

// =============================================================================
// KeyboardTechnique Tests
// =============================================================================

TEST(KeyboardTechniqueTest, TechniqueToString) {
  EXPECT_STREQ(keyboardTechniqueToString(KeyboardTechnique::Normal), "normal");
  EXPECT_STREQ(keyboardTechniqueToString(KeyboardTechnique::Staccato), "staccato");
  EXPECT_STREQ(keyboardTechniqueToString(KeyboardTechnique::Legato), "legato");
  EXPECT_STREQ(keyboardTechniqueToString(KeyboardTechnique::Arpeggio), "arpeggio");
  EXPECT_STREQ(keyboardTechniqueToString(KeyboardTechnique::OctaveDoubling), "octave_doubling");
  EXPECT_STREQ(keyboardTechniqueToString(KeyboardTechnique::Tremolo), "tremolo");
  EXPECT_STREQ(keyboardTechniqueToString(KeyboardTechnique::GraceNote), "grace_note");
}

// =============================================================================
// KeyboardNoteFactory Tests
// =============================================================================

class KeyboardNoteFactoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    harmony_ = std::make_unique<test::StubHarmonyContext>();
    harmony_->setAllPitchesSafe(true);
    harmony_->setChordTones({0, 4, 7});  // C major

    piano_ = std::make_unique<PianoModel>(InstrumentSkillLevel::Intermediate);
    factory_ = std::make_unique<KeyboardNoteFactory>(*harmony_, *piano_, 120);
  }

  std::unique_ptr<test::StubHarmonyContext> harmony_;
  std::unique_ptr<PianoModel> piano_;
  std::unique_ptr<KeyboardNoteFactory> factory_;
};

TEST_F(KeyboardNoteFactoryTest, PlayableVoicingReturnedUnchanged) {
  std::vector<uint8_t> voicing = {60, 64, 67};
  auto result = factory_->ensurePlayableVoicing(voicing, 0, 0, 480);
  EXPECT_EQ(result, voicing);
}

TEST_F(KeyboardNoteFactoryTest, EmptyVoicingReturnsEmpty) {
  auto result = factory_->ensurePlayableVoicing({}, 0, 0, 480);
  EXPECT_TRUE(result.empty());
}

TEST_F(KeyboardNoteFactoryTest, IsVoicingPlayableDelegates) {
  EXPECT_TRUE(factory_->isVoicingPlayable({60, 64, 67}));
  EXPECT_TRUE(factory_->isVoicingPlayable({}));
  EXPECT_FALSE(factory_->isVoicingPlayable({15, 60, 67}));
}

TEST_F(KeyboardNoteFactoryTest, TransitionFeasibleNoHistory) {
  // No previous voicing, should always be feasible
  EXPECT_TRUE(factory_->isTransitionFeasible({60, 64, 67}, 480));
}

TEST_F(KeyboardNoteFactoryTest, TransitionFeasibleAfterPlaying) {
  // Play a voicing to set previous state
  factory_->ensurePlayableVoicing({60, 64, 67}, 0, 0, 480);

  // Small move should be feasible
  EXPECT_TRUE(factory_->isTransitionFeasible({62, 66, 69}, 480));
}

TEST_F(KeyboardNoteFactoryTest, ResetStateClearsPrevious) {
  // Play a voicing
  factory_->ensurePlayableVoicing({60, 64, 67}, 0, 0, 480);

  // Reset
  factory_->resetState();

  // After reset, transition should be feasible (no previous voicing)
  EXPECT_TRUE(factory_->isTransitionFeasible({84, 88, 91}, 60));
}

TEST_F(KeyboardNoteFactoryTest, ResetAlsoClearsPianoState) {
  // Play a voicing
  factory_->ensurePlayableVoicing({60, 64, 67}, 0, 0, 480);
  EXPECT_TRUE(piano_->getState().right.isInitialized());

  // Reset
  factory_->resetState();

  // Piano state should also be reset
  EXPECT_FALSE(piano_->getState().right.isInitialized());
  EXPECT_FALSE(piano_->getState().left.isInitialized());
}

TEST_F(KeyboardNoteFactoryTest, SetBpmAffectsTransition) {
  factory_->ensurePlayableVoicing({48, 52, 55}, 0, 0, 480);

  // At low BPM, more real time available
  factory_->setBpm(60);
  bool low_bpm = factory_->isTransitionFeasible({84, 88, 91}, 480);

  // Reset and replay with high BPM
  factory_->resetState();
  factory_->ensurePlayableVoicing({48, 52, 55}, 0, 0, 480);
  factory_->setBpm(200);
  bool high_bpm = factory_->isTransitionFeasible({84, 88, 91}, 480);

  // High BPM should not be MORE feasible than low BPM
  if (!low_bpm) {
    EXPECT_FALSE(high_bpm);
  }
}

TEST_F(KeyboardNoteFactoryTest, MaxPlayabilityCostDefault) {
  EXPECT_FLOAT_EQ(factory_->getMaxPlayabilityCost(), 50.0f);
}

TEST_F(KeyboardNoteFactoryTest, SetMaxPlayabilityCost) {
  factory_->setMaxPlayabilityCost(100.0f);
  EXPECT_FLOAT_EQ(factory_->getMaxPlayabilityCost(), 100.0f);
}

TEST_F(KeyboardNoteFactoryTest, EnsurePlayableUpdatesPianoState) {
  factory_->ensurePlayableVoicing({60, 64, 67}, 0, 0, 480);

  auto state = piano_->getState();
  EXPECT_TRUE(state.right.isInitialized());
  EXPECT_EQ(state.last_voicing_span, 7);  // 67 - 60
}

TEST_F(KeyboardNoteFactoryTest, SequentialVoicingsTrackTransitions) {
  // Play sequence of voicings
  factory_->ensurePlayableVoicing({60, 64, 67}, 0, 0, 480);    // C major
  factory_->ensurePlayableVoicing({65, 69, 72}, 0, 480, 480);   // F major
  factory_->ensurePlayableVoicing({67, 71, 74}, 0, 960, 480);   // G major

  // State should reflect the last played voicing
  auto state = piano_->getState();
  EXPECT_EQ(state.last_voicing_span, 7);  // 74 - 67
}

TEST_F(KeyboardNoteFactoryTest, UnplayableVoicingGetsSuggestion) {
  // Voicing with out-of-range pitch
  std::vector<uint8_t> bad_voicing = {15, 60, 67};  // 15 < A0(21)
  auto result = factory_->ensurePlayableVoicing(bad_voicing, 0, 0, 480);
  // Should attempt to fix; returns something non-empty
  EXPECT_FALSE(result.empty());
}

TEST_F(KeyboardNoteFactoryTest, HarmonyAccessor) {
  auto chord_tones = factory_->harmony().getChordTonesAt(0);
  ASSERT_EQ(chord_tones.size(), 3u);
  EXPECT_EQ(chord_tones[0], 0);
  EXPECT_EQ(chord_tones[1], 4);
  EXPECT_EQ(chord_tones[2], 7);
}

TEST_F(KeyboardNoteFactoryTest, InstrumentAccessor) {
  EXPECT_EQ(factory_->instrument().getLowestPitch(), 21);
  EXPECT_EQ(factory_->instrument().getHighestPitch(), 108);
}

}  // namespace
}  // namespace midisketch
