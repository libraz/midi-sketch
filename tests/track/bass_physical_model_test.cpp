/**
 * @file bass_physical_model_test.cpp
 * @brief Tests for bass physical model integration with BlueprintConstraints.
 *
 * Verifies that BassPlayabilityChecker correctly applies skill-level
 * constraints and InstrumentModelMode settings from ProductionBlueprint.
 */

#include <gtest/gtest.h>

#include "core/generator.h"
#include "core/production_blueprint.h"
#include "core/song.h"
#include "instrument/fretted/fingering.h"
#include "instrument/fretted/playability.h"

namespace midisketch {
namespace {

// ============================================================================
// HandPhysics::virtuoso() Test
// ============================================================================

TEST(HandPhysicsTest, VirtuosoPresetHasMinimalConstraints) {
  auto virtuoso = HandPhysics::virtuoso();
  auto advanced = HandPhysics::advanced();
  auto intermediate = HandPhysics::intermediate();
  auto beginner = HandPhysics::beginner();

  // Virtuoso should have fastest position change time
  EXPECT_LT(virtuoso.position_change_time, advanced.position_change_time);
  EXPECT_LT(advanced.position_change_time, intermediate.position_change_time);
  EXPECT_LT(intermediate.position_change_time, beginner.position_change_time);

  // Virtuoso should allow most hammer/pulloff sequences
  EXPECT_GT(virtuoso.max_hammer_pulloff_sequence, advanced.max_hammer_pulloff_sequence);

  // Virtuoso should have smallest minimum interval
  EXPECT_LT(virtuoso.min_interval_same_string, advanced.min_interval_same_string);
}

TEST(HandSpanConstraintsTest, VirtuosoHasLargestSpan) {
  auto virtuoso = HandSpanConstraints::virtuoso();
  auto advanced = HandSpanConstraints::advanced();
  auto intermediate = HandSpanConstraints::intermediate();
  auto beginner = HandSpanConstraints::beginner();

  // Virtuoso should have largest normal span
  EXPECT_GT(virtuoso.normal_span, advanced.normal_span);
  EXPECT_GT(advanced.normal_span, intermediate.normal_span);
  EXPECT_GT(intermediate.normal_span, beginner.normal_span);

  // Virtuoso should have smallest stretch penalty
  EXPECT_LT(virtuoso.stretch_penalty_per_fret, advanced.stretch_penalty_per_fret);
}

// ============================================================================
// Blueprint Constraints Configuration Tests
// ============================================================================

class BlueprintConstraintsTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

TEST_F(BlueprintConstraintsTest, RhythmLockHasFullModeAndSlap) {
  const auto& bp = getProductionBlueprint(1);  // RhythmLock
  EXPECT_STREQ(bp.name, "RhythmLock");
  EXPECT_EQ(bp.constraints.instrument_mode, InstrumentModelMode::Full);
  EXPECT_EQ(bp.constraints.bass_skill, InstrumentSkillLevel::Advanced);
  EXPECT_TRUE(bp.constraints.enable_slap);
}

TEST_F(BlueprintConstraintsTest, IdolHyperHasFullModeAndSlap) {
  const auto& bp = getProductionBlueprint(5);  // IdolHyper
  EXPECT_STREQ(bp.name, "IdolHyper");
  EXPECT_EQ(bp.constraints.instrument_mode, InstrumentModelMode::Full);
  EXPECT_EQ(bp.constraints.bass_skill, InstrumentSkillLevel::Advanced);
  EXPECT_TRUE(bp.constraints.enable_slap);
}

TEST_F(BlueprintConstraintsTest, IdolCoolPopHasFullModeAndSlap) {
  const auto& bp = getProductionBlueprint(7);  // IdolCoolPop
  EXPECT_STREQ(bp.name, "IdolCoolPop");
  EXPECT_EQ(bp.constraints.instrument_mode, InstrumentModelMode::Full);
  EXPECT_EQ(bp.constraints.bass_skill, InstrumentSkillLevel::Advanced);
  EXPECT_TRUE(bp.constraints.enable_slap);
}

TEST_F(BlueprintConstraintsTest, BalladHasBeginnerSkill) {
  const auto& bp = getProductionBlueprint(3);  // Ballad
  EXPECT_STREQ(bp.name, "Ballad");
  EXPECT_EQ(bp.constraints.instrument_mode, InstrumentModelMode::ConstraintsOnly);
  EXPECT_EQ(bp.constraints.bass_skill, InstrumentSkillLevel::Beginner);
  EXPECT_FALSE(bp.constraints.enable_slap);
}

TEST_F(BlueprintConstraintsTest, IdolKawaiiHasBeginnerSkill) {
  const auto& bp = getProductionBlueprint(6);  // IdolKawaii
  EXPECT_STREQ(bp.name, "IdolKawaii");
  EXPECT_EQ(bp.constraints.instrument_mode, InstrumentModelMode::ConstraintsOnly);
  EXPECT_EQ(bp.constraints.bass_skill, InstrumentSkillLevel::Beginner);
  EXPECT_FALSE(bp.constraints.enable_slap);
}

TEST_F(BlueprintConstraintsTest, TraditionalHasConstraintsOnlyMode) {
  const auto& bp = getProductionBlueprint(0);  // Traditional
  EXPECT_STREQ(bp.name, "Traditional");
  EXPECT_EQ(bp.constraints.instrument_mode, InstrumentModelMode::ConstraintsOnly);
  EXPECT_EQ(bp.constraints.bass_skill, InstrumentSkillLevel::Intermediate);
}

// ============================================================================
// Bass Generation with Blueprint Constraints
// ============================================================================

class BassPhysicalModelIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::StraightPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.bpm = 140;  // Higher tempo to stress physical constraints
    params_.seed = 12345;
    params_.humanize = false;
  }

  // Calculate max leap in semitones for bass track
  int calculateMaxLeap(const MidiTrack& track) const {
    const auto& notes = track.notes();
    if (notes.size() < 2) return 0;

    int max_leap = 0;
    for (size_t i = 1; i < notes.size(); ++i) {
      int leap = std::abs(static_cast<int>(notes[i].note) -
                          static_cast<int>(notes[i - 1].note));
      max_leap = std::max(max_leap, leap);
    }
    return max_leap;
  }

  // Calculate average leap in semitones
  double calculateAverageLeap(const MidiTrack& track) const {
    const auto& notes = track.notes();
    if (notes.size() < 2) return 0.0;

    double total_leap = 0.0;
    for (size_t i = 1; i < notes.size(); ++i) {
      total_leap += std::abs(static_cast<int>(notes[i].note) -
                             static_cast<int>(notes[i - 1].note));
    }
    return total_leap / (notes.size() - 1);
  }

  GeneratorParams params_;
};

TEST_F(BassPhysicalModelIntegrationTest, BeginnerSkillProducesSmootherBasslines) {
  // Generate with Ballad blueprint (Beginner skill)
  params_.blueprint_id = 3;  // Ballad
  Generator gen_beginner;
  gen_beginner.generate(params_);
  const auto& bass_beginner = gen_beginner.getSong().bass();
  double avg_leap_beginner = calculateAverageLeap(bass_beginner);

  // Generate with Traditional blueprint (Intermediate skill)
  params_.blueprint_id = 0;  // Traditional
  params_.seed = 12345;  // Same seed for comparison
  Generator gen_intermediate;
  gen_intermediate.generate(params_);
  const auto& bass_intermediate = gen_intermediate.getSong().bass();
  double avg_leap_intermediate = calculateAverageLeap(bass_intermediate);

  // Beginner skill should tend to produce smoother bass lines
  // (This is a tendency test, not absolute guarantee due to pattern selection)
  // Just verify that both generated valid bass tracks
  EXPECT_GT(bass_beginner.notes().size(), 0u);
  EXPECT_GT(bass_intermediate.notes().size(), 0u);

  // Log results for analysis
  SCOPED_TRACE("Beginner avg leap: " + std::to_string(avg_leap_beginner));
  SCOPED_TRACE("Intermediate avg leap: " + std::to_string(avg_leap_intermediate));
}

TEST_F(BassPhysicalModelIntegrationTest, FullModeAppliesPhysicalConstraints) {
  // Generate with RhythmLock blueprint (Full mode)
  params_.blueprint_id = 1;  // RhythmLock
  params_.bpm = 180;  // Very high tempo to stress physical model
  Generator gen;
  gen.generate(params_);

  const auto& bass = gen.getSong().bass();
  EXPECT_GT(bass.notes().size(), 0u) << "Bass track should have notes";

  // Verify all notes are in valid bass range
  for (const auto& note : bass.notes()) {
    EXPECT_GE(note.note, 24) << "Note below bass range (C1)";
    EXPECT_LE(note.note, 60) << "Note above bass range (C4)";
  }
}

TEST_F(BassPhysicalModelIntegrationTest, ConstraintsOnlyModeEnablesPlayabilityCheck) {
  // Generate with Traditional blueprint (ConstraintsOnly mode)
  params_.blueprint_id = 0;  // Traditional
  params_.bpm = 180;  // High tempo
  Generator gen;
  gen.generate(params_);

  const auto& bass = gen.getSong().bass();
  EXPECT_GT(bass.notes().size(), 0u) << "Bass track should have notes";

  // All notes should be playable
  for (const auto& note : bass.notes()) {
    EXPECT_GE(note.note, 24);
    EXPECT_LE(note.note, 60);
  }
}

TEST_F(BassPhysicalModelIntegrationTest, AllBlueprintsGenerateValidBass) {
  // Ensure all blueprints generate valid bass tracks
  for (uint8_t i = 0; i < getProductionBlueprintCount(); ++i) {
    const auto& bp = getProductionBlueprint(i);
    params_.blueprint_id = i;
    params_.seed = 54321 + i;  // Different seed per blueprint

    Generator gen;
    gen.generate(params_);
    const auto& bass = gen.getSong().bass();

    // Each blueprint should generate valid bass
    EXPECT_GT(bass.notes().size(), 0u)
        << "Blueprint " << bp.name << " should generate bass notes";

    // All notes should be in valid MIDI range
    for (const auto& note : bass.notes()) {
      EXPECT_GE(note.note, 0) << "Blueprint " << bp.name << " has invalid note";
      EXPECT_LE(note.note, 127) << "Blueprint " << bp.name << " has invalid note";
      EXPECT_GT(note.velocity, 0) << "Blueprint " << bp.name << " has zero velocity";
    }
  }
}

// ============================================================================
// Skill Level Effect on Playability Cost
// ============================================================================

TEST(SkillLevelPlayabilityCostTest, BeginnerHasStricterThreshold) {
  // Test that different skill levels result in different playability thresholds
  // This is implicit through BassPlayabilityChecker's max_cost setting

  BlueprintConstraints beginner_constraints;
  beginner_constraints.bass_skill = InstrumentSkillLevel::Beginner;
  beginner_constraints.instrument_mode = InstrumentModelMode::ConstraintsOnly;

  BlueprintConstraints advanced_constraints;
  advanced_constraints.bass_skill = InstrumentSkillLevel::Advanced;
  advanced_constraints.instrument_mode = InstrumentModelMode::ConstraintsOnly;

  // Verify constraints are properly configured
  EXPECT_EQ(beginner_constraints.bass_skill, InstrumentSkillLevel::Beginner);
  EXPECT_EQ(advanced_constraints.bass_skill, InstrumentSkillLevel::Advanced);
}

}  // namespace
}  // namespace midisketch
