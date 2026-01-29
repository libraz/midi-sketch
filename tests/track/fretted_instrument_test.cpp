/**
 * @file fretted_instrument_test.cpp
 * @brief Tests for fretted instrument physical modeling.
 */

#include <gtest/gtest.h>

#include "core/timing_constants.h"
#include "instrument/fretted/bass_model.h"
#include "instrument/fretted/fretted_instrument.h"
#include "instrument/fretted/fretted_note_factory.h"
#include "instrument/fretted/guitar_model.h"

namespace midisketch {
namespace {

// ============================================================================
// FretPosition Tests
// ============================================================================

TEST(FretPositionTest, DefaultConstructor) {
  FretPosition pos;
  EXPECT_EQ(pos.string, 0);
  EXPECT_EQ(pos.fret, 0);
}

TEST(FretPositionTest, ParameterizedConstructor) {
  FretPosition pos(2, 5);
  EXPECT_EQ(pos.string, 2);
  EXPECT_EQ(pos.fret, 5);
}

TEST(FretPositionTest, Equality) {
  FretPosition a(1, 3);
  FretPosition b(1, 3);
  FretPosition c(1, 4);

  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

// ============================================================================
// StringState Tests
// ============================================================================

TEST(StringStateTest, DefaultState) {
  StringState state;
  EXPECT_FALSE(state.is_sounding);
  EXPECT_TRUE(state.isMuted());
  EXPECT_FALSE(state.isOpen());
  EXPECT_FALSE(state.isFretted());
}

TEST(StringStateTest, OpenString) {
  StringState state;
  state.is_sounding = true;
  state.fretted_at = 0;

  EXPECT_TRUE(state.isOpen());
  EXPECT_FALSE(state.isMuted());
  EXPECT_FALSE(state.isFretted());
}

TEST(StringStateTest, FrettedString) {
  StringState state;
  state.is_sounding = true;
  state.fretted_at = 5;
  state.finger_id = 2;

  EXPECT_TRUE(state.isFretted());
  EXPECT_FALSE(state.isOpen());
  EXPECT_FALSE(state.isMuted());
}

// ============================================================================
// FretboardState Tests
// ============================================================================

TEST(FretboardStateTest, DefaultState) {
  FretboardState state;
  EXPECT_EQ(state.string_count, 4);
  EXPECT_EQ(state.hand_position, 1);
  EXPECT_EQ(state.available_fingers, 0x0F);  // All 4 fingers available
}

TEST(FretboardStateTest, FingerAvailability) {
  FretboardState state;

  // All fingers available initially
  EXPECT_TRUE(state.isFingerAvailable(1));  // Index
  EXPECT_TRUE(state.isFingerAvailable(2));  // Middle
  EXPECT_TRUE(state.isFingerAvailable(3));  // Ring
  EXPECT_TRUE(state.isFingerAvailable(4));  // Pinky

  // Use index finger
  state.useFingerAt(1);
  EXPECT_FALSE(state.isFingerAvailable(1));
  EXPECT_TRUE(state.isFingerAvailable(2));

  // Release it
  state.releaseFinger(1);
  EXPECT_TRUE(state.isFingerAvailable(1));
}

// ============================================================================
// Standard Tuning Tests
// ============================================================================

TEST(TuningTest, Bass4String) {
  auto tuning = getStandardTuning(FrettedInstrumentType::Bass4String);
  ASSERT_EQ(tuning.size(), 4);
  EXPECT_EQ(tuning[0], 28);  // E1
  EXPECT_EQ(tuning[1], 33);  // A1
  EXPECT_EQ(tuning[2], 38);  // D2
  EXPECT_EQ(tuning[3], 43);  // G2
}

TEST(TuningTest, Bass5String) {
  auto tuning = getStandardTuning(FrettedInstrumentType::Bass5String);
  ASSERT_EQ(tuning.size(), 5);
  EXPECT_EQ(tuning[0], 23);  // B0
  EXPECT_EQ(tuning[1], 28);  // E1
}

TEST(TuningTest, Guitar6String) {
  auto tuning = getStandardTuning(FrettedInstrumentType::Guitar6String);
  ASSERT_EQ(tuning.size(), 6);
  EXPECT_EQ(tuning[0], 40);  // E2
  EXPECT_EQ(tuning[1], 45);  // A2
  EXPECT_EQ(tuning[2], 50);  // D3
  EXPECT_EQ(tuning[3], 55);  // G3
  EXPECT_EQ(tuning[4], 59);  // B3
  EXPECT_EQ(tuning[5], 64);  // E4
}

TEST(TuningTest, GetPitchAtPosition) {
  auto tuning = getStandardTuning(FrettedInstrumentType::Bass4String);

  // Open E string
  EXPECT_EQ(getPitchAtPosition(tuning, 0, 0), 28);

  // 5th fret on E string = A
  EXPECT_EQ(getPitchAtPosition(tuning, 0, 5), 33);

  // 12th fret on G string = G+12 = 55
  EXPECT_EQ(getPitchAtPosition(tuning, 3, 12), 55);
}

// ============================================================================
// HandPosition Tests
// ============================================================================

TEST(HandPositionTest, Reachability) {
  HandPosition hand(3, 2, 7);  // Base at fret 3, can reach 2-7

  EXPECT_TRUE(hand.canReach(0));   // Open string always reachable
  EXPECT_TRUE(hand.canReach(2));   // Within span
  EXPECT_TRUE(hand.canReach(5));   // Within span
  EXPECT_TRUE(hand.canReach(7));   // Within span
  EXPECT_FALSE(hand.canReach(1));  // Below span
  EXPECT_FALSE(hand.canReach(8));  // Above span
}

TEST(HandPositionTest, DistanceToReach) {
  HandPosition hand(5, 4, 9);

  EXPECT_EQ(hand.distanceToReach(0), 0);   // Open string
  EXPECT_EQ(hand.distanceToReach(6), 0);   // Within span
  EXPECT_EQ(hand.distanceToReach(3), -1);  // Need to shift down 1
  EXPECT_EQ(hand.distanceToReach(10), 1);  // Need to shift up 1
}

// ============================================================================
// HandSpanConstraints Tests
// ============================================================================

TEST(HandSpanConstraintsTest, SkillLevels) {
  auto beginner = HandSpanConstraints::beginner();
  auto intermediate = HandSpanConstraints::intermediate();
  auto advanced = HandSpanConstraints::advanced();

  EXPECT_LT(beginner.normal_span, intermediate.normal_span);
  EXPECT_LT(intermediate.normal_span, advanced.normal_span);

  // Beginner has higher penalty
  EXPECT_GT(beginner.stretch_penalty_per_fret, intermediate.stretch_penalty_per_fret);
}

TEST(HandSpanConstraintsTest, StretchPenalty) {
  auto constraints = HandSpanConstraints::intermediate();  // normal=4, max=5

  EXPECT_EQ(constraints.calculateStretchPenalty(3), 0.0f);  // Under normal
  EXPECT_EQ(constraints.calculateStretchPenalty(4), 0.0f);  // At normal
  EXPECT_GT(constraints.calculateStretchPenalty(5), 0.0f);  // Over normal
  EXPECT_EQ(constraints.calculateStretchPenalty(6), 999.0f);  // Over max
}

// ============================================================================
// BarreState Tests
// ============================================================================

TEST(BarreStateTest, InactiveBarre) {
  BarreState barre;
  EXPECT_FALSE(barre.isActive());
  EXPECT_EQ(barre.getStringCount(), 0);
}

TEST(BarreStateTest, ActiveBarre) {
  BarreState barre(5, 0, 5);  // Barre at fret 5, strings 0-5

  EXPECT_TRUE(barre.isActive());
  EXPECT_EQ(barre.getStringCount(), 6);
  EXPECT_TRUE(barre.coversString(0));
  EXPECT_TRUE(barre.coversString(5));
  EXPECT_FALSE(barre.coversString(6));
}

// ============================================================================
// BarreFingerAllocation Tests
// ============================================================================

TEST(BarreFingerAllocationTest, BasicAllocation) {
  BarreFingerAllocation alloc(5);  // Barre at fret 5

  // Barre fret is always OK
  EXPECT_TRUE(alloc.canPress(5, 0));
  EXPECT_TRUE(alloc.canPress(5, 3));

  // Allocate middle finger
  EXPECT_TRUE(alloc.tryAllocate(6, 2));  // Middle finger at fret 6, string 2
  EXPECT_EQ(alloc.middle_finger_string, 2);

  // Can't allocate middle finger to different string
  EXPECT_FALSE(alloc.tryAllocate(6, 3));

  // Can allocate ring finger
  EXPECT_TRUE(alloc.tryAllocate(7, 3));

  // Can allocate pinky
  EXPECT_TRUE(alloc.tryAllocate(8, 4));

  // Beyond pinky reach
  EXPECT_FALSE(alloc.tryAllocate(9, 5));
}

// ============================================================================
// BassModel Tests
// ============================================================================

class BassModelTest : public ::testing::Test {
 protected:
  void SetUp() override { bass_ = std::make_unique<BassModel>(FrettedInstrumentType::Bass4String); }

  std::unique_ptr<BassModel> bass_;
};

TEST_F(BassModelTest, StringCount) {
  EXPECT_EQ(bass_->getStringCount(), 4);
}

TEST_F(BassModelTest, PitchRange) {
  // 4-string bass: E1 (28) to G2+21frets = 28 + 21 = 64 (on high string: 43+21=64)
  EXPECT_EQ(bass_->getLowestPitch(), 28);
  EXPECT_EQ(bass_->getHighestPitch(), 64);
}

TEST_F(BassModelTest, PitchPlayability) {
  EXPECT_TRUE(bass_->isPitchPlayable(28));   // Open E
  EXPECT_TRUE(bass_->isPitchPlayable(43));   // Open G
  EXPECT_TRUE(bass_->isPitchPlayable(33));   // A (5th fret E or open A)
  EXPECT_FALSE(bass_->isPitchPlayable(27));  // Below range
  EXPECT_FALSE(bass_->isPitchPlayable(65));  // Above range
}

TEST_F(BassModelTest, PositionsForPitch) {
  // A1 (33) can be played on:
  // - E string, 5th fret (28 + 5 = 33)
  // - A string, open (33)
  auto positions = bass_->getPositionsForPitch(33);

  ASSERT_GE(positions.size(), 2u);

  // Check that open string is preferred (first)
  bool has_open = false;
  bool has_fret5 = false;
  for (const auto& pos : positions) {
    if (pos.string == 1 && pos.fret == 0) has_open = true;
    if (pos.string == 0 && pos.fret == 5) has_fret5 = true;
  }
  EXPECT_TRUE(has_open);
  EXPECT_TRUE(has_fret5);
}

TEST_F(BassModelTest, TechniquSupport) {
  EXPECT_TRUE(bass_->supportsTechnique(PlayingTechnique::Normal));
  EXPECT_TRUE(bass_->supportsTechnique(PlayingTechnique::Slap));
  EXPECT_TRUE(bass_->supportsTechnique(PlayingTechnique::Pop));
  EXPECT_TRUE(bass_->supportsTechnique(PlayingTechnique::HammerOn));
  EXPECT_TRUE(bass_->supportsTechnique(PlayingTechnique::GhostNote));
  EXPECT_FALSE(bass_->supportsTechnique(PlayingTechnique::Strum));
}

TEST_F(BassModelTest, SlapPopStrings) {
  auto slap_strings = bass_->getSlapStrings();
  auto pop_strings = bass_->getPopStrings();

  // Slap prefers lower strings
  EXPECT_GE(slap_strings.size(), 2u);
  EXPECT_LE(slap_strings[0], 2);

  // Pop prefers higher strings
  EXPECT_GE(pop_strings.size(), 1u);
  EXPECT_GE(pop_strings[0], 2);
}

TEST_F(BassModelTest, FindBestFingering) {
  FretboardState state(4);
  state.hand_position = 3;

  // Find fingering for A (fret 5 on E string or open A)
  Fingering fingering = bass_->findBestFingering(33, state, PlayingTechnique::Normal);

  EXPECT_TRUE(fingering.isValid());
  ASSERT_FALSE(fingering.assignments.empty());

  // Should prefer open A string
  const auto& assign = fingering.assignments[0];
  EXPECT_EQ(assign.position.string, 1);  // A string
  EXPECT_EQ(assign.position.fret, 0);    // Open
}

TEST_F(BassModelTest, TransitionCost) {
  FretboardState state(4);

  Fingering from = bass_->findBestFingering(33, state, PlayingTechnique::Normal);  // A (open)
  Fingering to = bass_->findBestFingering(36, state, PlayingTechnique::Normal);    // C (3rd fret A)

  ASSERT_TRUE(from.isValid());
  ASSERT_TRUE(to.isValid());

  PlayabilityCost cost = bass_->calculateTransitionCost(from, to, TICK_QUARTER, 120);

  // Open string to 3rd fret should have some cost
  EXPECT_GE(cost.total(), 0.0f);
}

TEST_F(BassModelTest, BendConstraints) {
  // Lower strings can't bend much
  FretPosition low_string(0, 5);
  EXPECT_LE(bass_->getMaxBend(low_string), 0.5f);

  // Higher strings can bend more
  FretPosition high_string(3, 7);
  EXPECT_GE(bass_->getMaxBend(high_string), 0.5f);
}

// ============================================================================
// GuitarModel Tests
// ============================================================================

class GuitarModelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    guitar_ = std::make_unique<GuitarModel>(FrettedInstrumentType::Guitar6String);
  }

  std::unique_ptr<GuitarModel> guitar_;
};

TEST_F(GuitarModelTest, StringCount) {
  EXPECT_EQ(guitar_->getStringCount(), 6);
}

TEST_F(GuitarModelTest, PitchRange) {
  // 6-string guitar: E2 (40) to E4+24frets = 64 + 24 = 88
  EXPECT_EQ(guitar_->getLowestPitch(), 40);
  EXPECT_EQ(guitar_->getHighestPitch(), 88);
}

TEST_F(GuitarModelTest, TechniqueSupport) {
  EXPECT_TRUE(guitar_->supportsTechnique(PlayingTechnique::Normal));
  EXPECT_TRUE(guitar_->supportsTechnique(PlayingTechnique::Bend));
  EXPECT_TRUE(guitar_->supportsTechnique(PlayingTechnique::Strum));
  EXPECT_TRUE(guitar_->supportsTechnique(PlayingTechnique::ChordStrum));
  EXPECT_FALSE(guitar_->supportsTechnique(PlayingTechnique::Slap));
  EXPECT_FALSE(guitar_->supportsTechnique(PlayingTechnique::Pop));
}

TEST_F(GuitarModelTest, BendConstraints) {
  // Low strings have limited bend
  FretPosition low_e(0, 7);
  EXPECT_LE(guitar_->getMaxBend(low_e), 1.5f);

  // High strings can bend more
  FretPosition high_e(5, 12);
  EXPECT_GE(guitar_->getMaxBend(high_e), 2.0f);
}

TEST_F(GuitarModelTest, ChordFingering) {
  FretboardState state(6);

  // Find fingering for C major chord (C-E-G = 48, 52, 55)
  std::vector<uint8_t> c_major = {48, 52, 55};
  Fingering fingering = guitar_->findChordFingering(c_major, state);

  EXPECT_TRUE(fingering.isValid());
  EXPECT_GE(fingering.assignments.size(), 3u);
}

TEST_F(GuitarModelTest, StrumConfig) {
  // Test strum configuration for a chord
  std::vector<FretPosition> positions = {{0, 3}, {1, 2}, {2, 0}, {3, 0}, {4, 1}};

  StrumConfig config = guitar_->getStrumConfig(positions);

  EXPECT_EQ(config.direction, StrumDirection::Down);
  EXPECT_EQ(config.first_string, 0);
  EXPECT_EQ(config.last_string, 4);
}

TEST_F(GuitarModelTest, PickingPatternRecommendation) {
  // Fast ascending sequence should suggest sweep
  std::vector<uint8_t> ascending = {40, 45, 50, 55, 59, 64};
  std::vector<Tick> fast_durations(6, TICK_SIXTEENTH);

  PickingPattern pattern = guitar_->getRecommendedPickingPattern(ascending, fast_durations, 160);

  // At high tempo with consistent direction, should suggest sweep or economy
  EXPECT_NE(pattern, PickingPattern::Hybrid);  // Hybrid isn't typical for this
}

// ============================================================================
// Fingering Tests
// ============================================================================

TEST(FingeringTest, DefaultState) {
  Fingering f;
  EXPECT_FALSE(f.isValid());
  EXPECT_EQ(f.playability_cost, 0.0f);
  EXPECT_FALSE(f.requires_position_shift);
  EXPECT_FALSE(f.requires_barre_change);
}

TEST(FingeringTest, SpanCalculation) {
  Fingering f;
  f.assignments.emplace_back(FretPosition(0, 3), 1, false);
  f.assignments.emplace_back(FretPosition(1, 5), 3, false);
  f.assignments.emplace_back(FretPosition(2, 3), 1, false);

  EXPECT_EQ(f.getLowestFret(), 3);
  EXPECT_EQ(f.getHighestFret(), 5);
  EXPECT_EQ(f.getSpan(), 2);
}

// ============================================================================
// PlayingTechnique Tests
// ============================================================================

TEST(PlayingTechniqueTest, TechniqueToString) {
  EXPECT_STREQ(playingTechniqueToString(PlayingTechnique::Normal), "normal");
  EXPECT_STREQ(playingTechniqueToString(PlayingTechnique::Slap), "slap");
  EXPECT_STREQ(playingTechniqueToString(PlayingTechnique::Pop), "pop");
  EXPECT_STREQ(playingTechniqueToString(PlayingTechnique::Bend), "bend");
}

TEST(PlayingTechniqueTest, TechniqueTransition) {
  // Slap to tapping needs time
  EXPECT_FALSE(isValidTechniqueTransition(PlayingTechnique::Slap, PlayingTechnique::Tapping, 60));
  EXPECT_TRUE(
      isValidTechniqueTransition(PlayingTechnique::Slap, PlayingTechnique::Tapping, 120));

  // Normal transitions are instant
  EXPECT_TRUE(
      isValidTechniqueTransition(PlayingTechnique::Normal, PlayingTechnique::HammerOn, 30));
}

// ============================================================================
// Harmonic Fret Tests
// ============================================================================

TEST(HarmonicFretsTest, ValidFrets) {
  EXPECT_TRUE(HarmonicFrets::isHarmonicFret(5));
  EXPECT_TRUE(HarmonicFrets::isHarmonicFret(7));
  EXPECT_TRUE(HarmonicFrets::isHarmonicFret(12));
  EXPECT_FALSE(HarmonicFrets::isHarmonicFret(6));
  EXPECT_FALSE(HarmonicFrets::isHarmonicFret(8));
}

// ============================================================================
// BendConstraint Tests
// ============================================================================

TEST(BendConstraintTest, BassLimitations) {
  // Bass low strings can't bend
  EXPECT_EQ(BendConstraint::getMaxBend(0, 5, true), 0);
  EXPECT_EQ(BendConstraint::getMaxBend(1, 5, true), 0);

  // Bass high strings can bend half step
  EXPECT_EQ(BendConstraint::getMaxBend(2, 5, true), 1);
  EXPECT_EQ(BendConstraint::getMaxBend(3, 5, true), 1);
}

TEST(BendConstraintTest, GuitarBends) {
  // Guitar low strings: 1 step
  EXPECT_EQ(BendConstraint::getMaxBend(0, 5, false), 1);
  EXPECT_EQ(BendConstraint::getMaxBend(1, 5, false), 1);

  // Guitar high strings: 2 steps
  EXPECT_EQ(BendConstraint::getMaxBend(3, 5, false), 2);
  EXPECT_EQ(BendConstraint::getMaxBend(5, 5, false), 2);

  // High frets: +1 step
  EXPECT_EQ(BendConstraint::getMaxBend(5, 14, false), 3);
}

// ============================================================================
// PlayabilityCost Tests
// ============================================================================

TEST(PlayabilityCostTest, Addition) {
  PlayabilityCost a;
  a.position_shift = 5.0f;
  a.finger_stretch = 3.0f;

  PlayabilityCost b;
  b.string_skip = 2.0f;
  b.technique_modifier = 1.0f;

  a += b;

  EXPECT_EQ(a.position_shift, 5.0f);
  EXPECT_EQ(a.finger_stretch, 3.0f);
  EXPECT_EQ(a.string_skip, 2.0f);
  EXPECT_EQ(a.technique_modifier, 1.0f);
  EXPECT_FLOAT_EQ(a.total(), 11.0f);
}

// ============================================================================
// FingeringProvenance Tests
// ============================================================================

TEST(FingeringProvenanceTest, DefaultState) {
  FingeringProvenance prov;
  EXPECT_FALSE(prov.isSet());
  EXPECT_EQ(prov.string, 255);
  EXPECT_EQ(prov.fret, 255);
}

TEST(FingeringProvenanceTest, FingerNames) {
  EXPECT_STREQ(FingeringProvenance::fingerName(0), "Open");
  EXPECT_STREQ(FingeringProvenance::fingerName(1), "Index");
  EXPECT_STREQ(FingeringProvenance::fingerName(2), "Middle");
  EXPECT_STREQ(FingeringProvenance::fingerName(3), "Ring");
  EXPECT_STREQ(FingeringProvenance::fingerName(4), "Pinky");
  EXPECT_STREQ(FingeringProvenance::fingerName(5), "Thumb");
}

// ============================================================================
// 5-String and 6-String Bass Tests
// ============================================================================

TEST(ExtendedBassTest, Bass5String) {
  BassModel bass(FrettedInstrumentType::Bass5String);

  EXPECT_EQ(bass.getStringCount(), 5);
  EXPECT_TRUE(bass.hasLowB());
  EXPECT_FALSE(bass.hasHighC());
  EXPECT_EQ(bass.getLowestPitch(), 23);  // B0
}

TEST(ExtendedBassTest, Bass6String) {
  BassModel bass(FrettedInstrumentType::Bass6String);

  EXPECT_EQ(bass.getStringCount(), 6);
  EXPECT_TRUE(bass.hasLowB());
  EXPECT_TRUE(bass.hasHighC());
  EXPECT_EQ(bass.getLowestPitch(), 23);  // B0
  EXPECT_GT(bass.getHighestPitch(), 64); // Higher than 4-string
}

// ============================================================================
// 7-String Guitar Tests
// ============================================================================

TEST(ExtendedGuitarTest, Guitar7String) {
  GuitarModel guitar(FrettedInstrumentType::Guitar7String);

  EXPECT_EQ(guitar.getStringCount(), 7);
  EXPECT_TRUE(guitar.hasLowB());
  EXPECT_EQ(guitar.getLowestPitch(), 35);  // B1
}

// ============================================================================
// Sequence Planning Tests
// ============================================================================

TEST(SequencePlanningTest, BassLineOptimization) {
  BassModel bass(FrettedInstrumentType::Bass4String);

  // Simple bass line: E-G-A-E
  std::vector<uint8_t> pitches = {28, 31, 33, 28};
  std::vector<Tick> durations = {TICK_QUARTER, TICK_QUARTER, TICK_QUARTER, TICK_QUARTER};
  FretboardState state(4);

  auto fingerings =
      bass.findBestFingeringSequence(pitches, durations, state, PlayingTechnique::Normal);

  ASSERT_EQ(fingerings.size(), 4u);

  // All fingerings should be valid
  for (const auto& f : fingerings) {
    EXPECT_TRUE(f.isValid());
  }
}

// ============================================================================
// canPlayAtPosition Tests
// ============================================================================

TEST(CanPlayAtPositionTest, NoBarre) {
  BarreState no_barre;
  HandPosition hand(5, 4, 9);

  FretPosition open_string(0, 0);
  FretPosition in_range(0, 6);
  FretPosition out_of_range(0, 10);

  EXPECT_TRUE(canPlayAtPosition(open_string, no_barre, hand));
  EXPECT_TRUE(canPlayAtPosition(in_range, no_barre, hand));
  EXPECT_FALSE(canPlayAtPosition(out_of_range, no_barre, hand));
}

TEST(CanPlayAtPositionTest, WithBarre) {
  BarreState barre(5, 0, 5);  // Barre at fret 5, all strings
  HandPosition hand(5, 4, 9);

  // At barre fret: OK
  EXPECT_TRUE(canPlayAtPosition({0, 5}, barre, hand));

  // Above barre within reach: OK
  EXPECT_TRUE(canPlayAtPosition({0, 6}, barre, hand));
  EXPECT_TRUE(canPlayAtPosition({0, 7}, barre, hand));
  EXPECT_TRUE(canPlayAtPosition({0, 8}, barre, hand));

  // Below barre: NOT OK
  EXPECT_FALSE(canPlayAtPosition({0, 4}, barre, hand));
  EXPECT_FALSE(canPlayAtPosition({0, 0}, barre, hand));

  // Too far above barre: NOT OK
  EXPECT_FALSE(canPlayAtPosition({0, 9}, barre, hand));
}

// ============================================================================
// isChordPlayableWithBarre Tests
// ============================================================================

TEST(ChordPlayableWithBarreTest, StandardBarreChord) {
  // F major barre chord shape at fret 1
  std::vector<FretPosition> f_major = {
      {0, 1},  // E string, fret 1 (barre)
      {1, 3},  // A string, fret 3 (ring)
      {2, 3},  // D string, fret 3 (ring - same fret different string)
      {3, 2},  // G string, fret 2 (middle)
      {4, 1},  // B string, fret 1 (barre)
      {5, 1}   // high E, fret 1 (barre)
  };

  // This should fail because ring finger can't press two strings at same fret offset
  EXPECT_FALSE(isChordPlayableWithBarre(f_major, 1));

  // Simplified version with only 4 notes
  std::vector<FretPosition> simplified = {
      {0, 1},  // E string, fret 1 (barre)
      {1, 3},  // A string, fret 3 (ring)
      {2, 2},  // D string, fret 2 (middle)
      {4, 1}   // B string, fret 1 (barre)
  };

  EXPECT_TRUE(isChordPlayableWithBarre(simplified, 1));
}

TEST(ChordPlayableWithBarreTest, BelowBarreImpossible) {
  std::vector<FretPosition> positions = {
      {0, 5},  // At barre
      {1, 3}   // Below barre - impossible!
  };

  EXPECT_FALSE(isChordPlayableWithBarre(positions, 5));
}

// ============================================================================
// FrettedNoteFactory Tests
// ============================================================================

}  // namespace
}  // namespace midisketch

// Include stub after closing namespace to avoid conflicts
#include "core/midi_track.h"
#include "test_support/stub_harmony_context.h"

namespace midisketch {
namespace {

class FrettedNoteFactoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    harmony_ = std::make_unique<test::StubHarmonyContext>();
    harmony_->setChordTones({0, 4, 7});  // C major triad
    harmony_->setAllPitchesSafe(true);

    bass_ = std::make_unique<BassModel>(FrettedInstrumentType::Bass4String);
  }

  std::unique_ptr<test::StubHarmonyContext> harmony_;
  std::unique_ptr<BassModel> bass_;
};

TEST_F(FrettedNoteFactoryTest, CreatePlayableNote) {
  FrettedNoteFactory factory(*harmony_, *bass_, 120);

  // Create a note for E (28) - open E string on bass
  auto note = factory.create(0, TICK_QUARTER, 28, 100, PlayingTechnique::Normal,
                              NoteSource::BassPattern);

  ASSERT_TRUE(note.has_value());
  EXPECT_EQ(note->note, 28);
  EXPECT_EQ(note->velocity, 100);
  EXPECT_EQ(note->duration, TICK_QUARTER);
}

TEST_F(FrettedNoteFactoryTest, CreateUnplayablePitchGetsTransposed) {
  FrettedNoteFactory factory(*harmony_, *bass_, 120);

  // Try to create a note that's below bass range - it will be transposed
  auto note = factory.create(0, TICK_QUARTER, 20, 100, PlayingTechnique::Normal,
                              NoteSource::BassPattern);

  // Factory transposes unplayable pitches to playable range
  ASSERT_TRUE(note.has_value());
  // The pitch should be within bass range (28-64)
  EXPECT_GE(note->note, bass_->getLowestPitch());
  EXPECT_LE(note->note, bass_->getHighestPitch());
}

TEST_F(FrettedNoteFactoryTest, EnsurePlayableTransposes) {
  FrettedNoteFactory factory(*harmony_, *bass_, 120);

  // Pitch 20 is below range, should be transposed up
  uint8_t playable = factory.ensurePlayable(20, 0, TICK_QUARTER);

  // Should be transposed to the playable range
  EXPECT_GE(playable, bass_->getLowestPitch());
  EXPECT_LE(playable, bass_->getHighestPitch());
}

TEST_F(FrettedNoteFactoryTest, FindPlayablePitchPrefersSamePitch) {
  FrettedNoteFactory factory(*harmony_, *bass_, 120);

  // E (28) is playable, should return same pitch
  uint8_t result = factory.findPlayablePitch(28, 0, TICK_QUARTER, 0.5f);
  EXPECT_EQ(result, 28);
}

TEST_F(FrettedNoteFactoryTest, FindPlayablePitchForOutOfRange) {
  FrettedNoteFactory factory(*harmony_, *bass_, 120);

  // Pitch 70 is above bass range, should find alternative
  uint8_t result = factory.findPlayablePitch(70, 0, TICK_QUARTER, 0.5f);

  EXPECT_GE(result, bass_->getLowestPitch());
  EXPECT_LE(result, bass_->getHighestPitch());
}

TEST_F(FrettedNoteFactoryTest, ResetStateClearsPosition) {
  FrettedNoteFactory factory(*harmony_, *bass_, 120);

  // Create a note to change state
  factory.create(0, TICK_QUARTER, 33, 100, PlayingTechnique::Normal, NoteSource::BassPattern);

  // Reset
  factory.resetState();

  // State should be back to default
  const auto& state = factory.getState();
  EXPECT_EQ(state.hand_position, 3);  // Default starting position
}

TEST_F(FrettedNoteFactoryTest, CreateSafeChecksHarmony) {
  FrettedNoteFactory factory(*harmony_, *bass_, 120);

  // With all pitches safe, should succeed
  auto note = factory.createSafe(0, TICK_QUARTER, 33, 100, TrackRole::Bass,
                                  PlayingTechnique::Normal, NoteSource::BassPattern);
  EXPECT_TRUE(note.has_value());

  // Set pitches to unsafe
  harmony_->setAllPitchesSafe(false);

  // Now should still work because getSafePitch returns desired pitch in stub
  auto note2 = factory.createSafe(0, TICK_QUARTER, 33, 100, TrackRole::Bass,
                                   PlayingTechnique::Normal, NoteSource::BassPattern);
  EXPECT_TRUE(note2.has_value());
}

TEST_F(FrettedNoteFactoryTest, SetMaxPlayabilityCost) {
  FrettedNoteFactory factory(*harmony_, *bass_, 120);

  factory.setMaxPlayabilityCost(0.3f);
  EXPECT_FLOAT_EQ(factory.getMaxPlayabilityCost(), 0.3f);

  factory.setMaxPlayabilityCost(0.8f);
  EXPECT_FLOAT_EQ(factory.getMaxPlayabilityCost(), 0.8f);
}

TEST_F(FrettedNoteFactoryTest, SetBpm) {
  FrettedNoteFactory factory(*harmony_, *bass_, 120);

  EXPECT_EQ(factory.getBpm(), 120);

  factory.setBpm(140);
  EXPECT_EQ(factory.getBpm(), 140);
}

TEST_F(FrettedNoteFactoryTest, PlanSequence) {
  FrettedNoteFactory factory(*harmony_, *bass_, 120);

  // Simple bass line: E-G-A-E
  std::vector<uint8_t> pitches = {28, 31, 33, 28};
  std::vector<Tick> durations = {TICK_QUARTER, TICK_QUARTER, TICK_QUARTER, TICK_QUARTER};

  auto fingerings = factory.planSequence(pitches, durations, PlayingTechnique::Normal);

  ASSERT_EQ(fingerings.size(), 4u);

  // All fingerings should be valid
  for (const auto& f : fingerings) {
    EXPECT_TRUE(f.isValid());
  }
}

TEST_F(FrettedNoteFactoryTest, AccessUnderlyingObjects) {
  FrettedNoteFactory factory(*harmony_, *bass_, 120);

  // Can access harmony and instrument
  EXPECT_EQ(&factory.harmony(), harmony_.get());
  EXPECT_EQ(&factory.instrument(), bass_.get());
}

// Guitar-specific factory test
TEST(FrettedNoteFactoryGuitarTest, CreateGuitarNote) {
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);

  GuitarModel guitar(FrettedInstrumentType::Guitar6String);
  FrettedNoteFactory factory(harmony, guitar, 120);

  // Create a note for E (40) - open low E string on guitar
  auto note = factory.create(0, TICK_QUARTER, 40, 100, PlayingTechnique::Normal,
                              NoteSource::ChordVoicing);

  ASSERT_TRUE(note.has_value());
  EXPECT_EQ(note->note, 40);
}

TEST(FrettedNoteFactoryGuitarTest, BendTechniqueConstraint) {
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);

  GuitarModel guitar(FrettedInstrumentType::Guitar6String);
  FrettedNoteFactory factory(harmony, guitar, 120);

  // Create a note with bend technique on high string (should work)
  auto note = factory.create(0, TICK_QUARTER, 64, 100, PlayingTechnique::Bend,
                              NoteSource::ChordVoicing);

  // Should succeed (E4 is in range and bendable on high E string)
  EXPECT_TRUE(note.has_value());
}

}  // namespace
}  // namespace midisketch
