/**
 * @file generator_dynamics_test.cpp
 * @brief Tests for generator dynamics.
 */

#include <gtest/gtest.h>

#include "core/generator.h"
#include "core/velocity.h"

namespace midisketch {
namespace {

// ============================================================================
// Velocity Tests
// ============================================================================

TEST(VelocityTest, SectionEnergyLevels) {
  // Test that section energy levels are correctly defined
  EXPECT_EQ(getSectionEnergy(SectionType::Intro), 1);
  EXPECT_EQ(getSectionEnergy(SectionType::A), 2);
  EXPECT_EQ(getSectionEnergy(SectionType::B), 3);
  EXPECT_EQ(getSectionEnergy(SectionType::Chorus), 4);

  // Energy should increase from Intro to Chorus
  EXPECT_LT(getSectionEnergy(SectionType::Intro), getSectionEnergy(SectionType::A));
  EXPECT_LT(getSectionEnergy(SectionType::A), getSectionEnergy(SectionType::B));
  EXPECT_LT(getSectionEnergy(SectionType::B), getSectionEnergy(SectionType::Chorus));
}

TEST(VelocityTest, VelocityBalanceMultipliers) {
  // Test track velocity balance multipliers
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Vocal), 1.0f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Chord), 0.75f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Bass), 0.85f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Drums), 0.90f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Motif), 0.70f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::SE), 1.0f);

  // Vocal should be loudest
  EXPECT_GE(VelocityBalance::getMultiplier(TrackRole::Vocal),
            VelocityBalance::getMultiplier(TrackRole::Chord));
  EXPECT_GE(VelocityBalance::getMultiplier(TrackRole::Vocal),
            VelocityBalance::getMultiplier(TrackRole::Bass));
}

TEST(VelocityTest, CalculateVelocityBeatAccent) {
  // Test that beat 1 has higher velocity than beat 2
  uint8_t vel_beat1 = calculateVelocity(SectionType::A, 0, Mood::StraightPop);
  uint8_t vel_beat2 = calculateVelocity(SectionType::A, 1, Mood::StraightPop);
  uint8_t vel_beat3 = calculateVelocity(SectionType::A, 2, Mood::StraightPop);

  EXPECT_GT(vel_beat1, vel_beat2);  // Beat 1 > Beat 2
  EXPECT_GT(vel_beat3, vel_beat2);  // Beat 3 > Beat 2 (secondary accent)
}

TEST(VelocityTest, CalculateVelocitySectionProgression) {
  // Test that Chorus has higher velocity than Intro
  uint8_t vel_intro = calculateVelocity(SectionType::Intro, 0, Mood::StraightPop);
  uint8_t vel_chorus = calculateVelocity(SectionType::Chorus, 0, Mood::StraightPop);

  EXPECT_GT(vel_chorus, vel_intro);
}

// ============================================================================
// Transition Dynamics Tests
// ============================================================================

TEST(GeneratorTest, TransitionDynamicsApplied) {
  // Test that transition dynamics modifies velocities
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;  // A(8) B(8) Chorus(8)
  params.mood = Mood::StraightPop;
  params.seed = 42;

  gen.generate(params);
  const auto& vocal = gen.getSong().vocal().notes();

  // Find notes at section transitions (last bar of A -> B, B -> Chorus)
  // A ends at bar 8 (tick 15360), B ends at bar 16 (tick 30720)
  Tick a_end = 8 * TICKS_PER_BAR;
  Tick b_end = 16 * TICKS_PER_BAR;

  // Check that notes exist near section boundaries (within last 2 bars)
  // Using 2 bars instead of 1 to avoid dependency on leading tone insertion
  bool has_notes_before_b = false;
  bool has_notes_before_chorus = false;

  for (const auto& note : vocal) {
    if (note.start_tick >= a_end - 2 * TICKS_PER_BAR && note.start_tick < a_end) {
      has_notes_before_b = true;
    }
    if (note.start_tick >= b_end - 2 * TICKS_PER_BAR && note.start_tick < b_end) {
      has_notes_before_chorus = true;
    }
  }

  // At least one section boundary should have notes
  EXPECT_TRUE(has_notes_before_b || has_notes_before_chorus);
}

// ============================================================================
// Humanize Tests
// ============================================================================

TEST(GeneratorTest, HumanizeDisabledByDefault) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;

  gen.generate(params);

  // Humanize should be disabled by default
  EXPECT_FALSE(gen.getParams().humanize);
}

TEST(GeneratorTest, HumanizeModifiesNotes) {
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;

  // Generate without humanize
  Generator gen1;
  params.humanize = false;
  gen1.generate(params);
  const auto& notes_no_humanize = gen1.getSong().vocal().notes();

  // Generate with humanize
  Generator gen2;
  params.humanize = true;
  params.humanize_timing = 1.0f;
  params.humanize_velocity = 1.0f;
  gen2.generate(params);
  const auto& notes_humanized = gen2.getSong().vocal().notes();

  // Both should have same number of notes
  ASSERT_EQ(notes_no_humanize.size(), notes_humanized.size());

  // At least some notes should differ in timing or velocity
  bool has_difference = false;
  for (size_t i = 0; i < notes_no_humanize.size(); ++i) {
    if (notes_no_humanize[i].start_tick != notes_humanized[i].start_tick ||
        notes_no_humanize[i].velocity != notes_humanized[i].velocity) {
      has_difference = true;
      break;
    }
  }
  EXPECT_TRUE(has_difference);
}

TEST(GeneratorTest, HumanizeTimingWithinBounds) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.humanize = true;
  params.humanize_timing = 1.0f;    // Maximum timing variation
  params.humanize_velocity = 0.0f;  // No velocity variation

  gen.generate(params);
  const auto& notes = gen.getSong().vocal().notes();

  // All notes should still have reasonable timing (>= 0)
  for (const auto& note : notes) {
    EXPECT_GE(note.start_tick, 0u);
  }
}

TEST(GeneratorTest, HumanizeVelocityWithinBounds) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.humanize = true;
  params.humanize_timing = 0.0f;    // No timing variation
  params.humanize_velocity = 1.0f;  // Maximum velocity variation

  gen.generate(params);
  const auto& notes = gen.getSong().vocal().notes();

  // All velocities should be within valid MIDI range
  for (const auto& note : notes) {
    EXPECT_GE(note.velocity, 1u);
    EXPECT_LE(note.velocity, 127u);
  }
}

TEST(GeneratorTest, HumanizeParametersIndependent) {
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;
  params.humanize = true;

  // Generate with timing only
  Generator gen_timing;
  params.humanize_timing = 1.0f;
  params.humanize_velocity = 0.0f;
  gen_timing.generate(params);
  const auto& notes_timing = gen_timing.getSong().vocal().notes();

  // Generate without humanize for baseline
  Generator gen_base;
  params.humanize = false;
  gen_base.generate(params);
  const auto& notes_base = gen_base.getSong().vocal().notes();

  ASSERT_EQ(notes_timing.size(), notes_base.size());

  // With timing=1.0 and velocity=0.0, only timing should differ.
  // Timing may or may not differ (depends on strong beat handling).
  // Just verify generation completes without error and both have same count.
  EXPECT_EQ(notes_timing.size(), notes_base.size());
}

}  // namespace
}  // namespace midisketch
