/**
 * @file motif_motion_test.cpp
 * @brief Tests for extended MotifMotion types (WideLeap, Chromatic, Disjunct).
 */

#include <gtest/gtest.h>

#include <vector>

#include "core/generator.h"
#include "core/preset_data.h"
#include "core/preset_types.h"
#include "test_support/generator_test_fixture.h"

namespace midisketch {
namespace {

// ============================================================================
// MotifMotion Enum Tests
// ============================================================================

TEST(MotifMotionEnumTest, AllValuesExist) {
  // Verify all motion types exist
  EXPECT_EQ(static_cast<uint8_t>(MotifMotion::Stepwise), 0);
  EXPECT_EQ(static_cast<uint8_t>(MotifMotion::GentleLeap), 1);
  EXPECT_EQ(static_cast<uint8_t>(MotifMotion::WideLeap), 2);
  EXPECT_EQ(static_cast<uint8_t>(MotifMotion::NarrowStep), 3);
  EXPECT_EQ(static_cast<uint8_t>(MotifMotion::Disjunct), 4);
}

TEST(MotifMotionEnumTest, CanAssignToParams) {
  MotifParams params;

  // Test default
  EXPECT_EQ(params.motion, MotifMotion::Stepwise);

  // Test assignment of all types
  params.motion = MotifMotion::Stepwise;
  EXPECT_EQ(params.motion, MotifMotion::Stepwise);

  params.motion = MotifMotion::GentleLeap;
  EXPECT_EQ(params.motion, MotifMotion::GentleLeap);

  params.motion = MotifMotion::WideLeap;
  EXPECT_EQ(params.motion, MotifMotion::WideLeap);

  params.motion = MotifMotion::NarrowStep;
  EXPECT_EQ(params.motion, MotifMotion::NarrowStep);

  params.motion = MotifMotion::Disjunct;
  EXPECT_EQ(params.motion, MotifMotion::Disjunct);
}

// ============================================================================
// Motif Generation with Different Motions Tests
// ============================================================================

class MotifMotionGenerationTest : public test::GeneratorTestFixture {
 protected:
  void SetUp() override {
    GeneratorTestFixture::SetUp();
    params_.mood = Mood::ModernPop;
    params_.vocal_high = 72;
    params_.drums_enabled = true;
    params_.composition_style = CompositionStyle::BackgroundMotif;
  }
  Generator generator_;
};

TEST_F(MotifMotionGenerationTest, StepwiseMotionGenerates) {
  params_.motif.motion = MotifMotion::Stepwise;

  generator_.generate(params_);

  const auto& motif = generator_.getSong().motif();
  EXPECT_GT(motif.notes().size(), 0) << "Stepwise motion should generate notes";
}

TEST_F(MotifMotionGenerationTest, GentleLeapMotionGenerates) {
  params_.motif.motion = MotifMotion::GentleLeap;

  generator_.generate(params_);

  const auto& motif = generator_.getSong().motif();
  EXPECT_GT(motif.notes().size(), 0) << "GentleLeap motion should generate notes";
}

TEST_F(MotifMotionGenerationTest, WideLeapMotionGenerates) {
  params_.motif.motion = MotifMotion::WideLeap;

  generator_.generate(params_);

  const auto& motif = generator_.getSong().motif();
  EXPECT_GT(motif.notes().size(), 0) << "WideLeap motion should generate notes";
}

TEST_F(MotifMotionGenerationTest, NarrowStepMotionGenerates) {
  params_.motif.motion = MotifMotion::NarrowStep;

  generator_.generate(params_);

  const auto& motif = generator_.getSong().motif();
  EXPECT_GT(motif.notes().size(), 0) << "Chromatic motion should generate notes";
}

TEST_F(MotifMotionGenerationTest, DisjunctMotionGenerates) {
  params_.motif.motion = MotifMotion::Disjunct;

  generator_.generate(params_);

  const auto& motif = generator_.getSong().motif();
  EXPECT_GT(motif.notes().size(), 0) << "Disjunct motion should generate notes";
}

TEST_F(MotifMotionGenerationTest, DifferentMotionsProduceDifferentPatterns) {
  params_.seed = 100;

  // Generate with Stepwise
  params_.motif.motion = MotifMotion::Stepwise;
  generator_.generate(params_);
  auto stepwise_notes = generator_.getSong().motif().notes();

  // Generate with WideLeap using same seed
  Generator generator2;
  params_.motif.motion = MotifMotion::WideLeap;
  generator2.generate(params_);
  auto wide_notes = generator2.getSong().motif().notes();

  // Both should have notes
  EXPECT_GT(stepwise_notes.size(), 0);
  EXPECT_GT(wide_notes.size(), 0);

  // Patterns should be different (unless very unlikely coincidence)
  if (stepwise_notes.size() == wide_notes.size()) {
    bool all_same = true;
    for (size_t i = 0; i < stepwise_notes.size(); ++i) {
      if (stepwise_notes[i].note != wide_notes[i].note) {
        all_same = false;
        break;
      }
    }
    // With different motions, patterns should differ
    EXPECT_FALSE(all_same) << "Different motion types should produce different pitch patterns";
  }
}

TEST_F(MotifMotionGenerationTest, NotesInValidRange) {
  // Test all motion types produce notes in valid MIDI range
  std::vector<MotifMotion> motions = {MotifMotion::Stepwise, MotifMotion::GentleLeap,
                                      MotifMotion::WideLeap, MotifMotion::NarrowStep,
                                      MotifMotion::Disjunct};

  for (auto motion : motions) {
    params_.motif.motion = motion;
    Generator gen;
    gen.generate(params_);

    const auto& motif = gen.getSong().motif();
    ASSERT_FALSE(motif.notes().empty())
        << "Motion " << static_cast<int>(motion) << " produced no motif notes";
    // The vocal-aware motif range reaches a fourth below the synth model's own
    // floor, down to G3, so that the motif is not forced to crowd C4-E4.
    constexpr int kMotifRangeLow = 55;    // G3
    constexpr int kMotifRangeHigh = 108;  // C8
    for (const auto& note : motif.notes()) {
      EXPECT_GE(static_cast<int>(note.note), kMotifRangeLow)
          << "Motion " << static_cast<int>(motion) << " produced invalid low pitch";
      EXPECT_LE(static_cast<int>(note.note), kMotifRangeHigh)
          << "Motion " << static_cast<int>(motion) << " produced invalid high pitch";
    }
  }
}

// ============================================================================
// Explicit motion against blueprint section hints
// ============================================================================

/// Motif pitches sounding inside the sections a blueprint slot gives a motion hint.
std::vector<uint8_t> hintedSectionMotifPitches(const Song& song) {
  std::vector<uint8_t> pitches;
  for (const auto& section : song.arrangement().sections()) {
    if (section.motif_motion_hint == 0) continue;
    for (const auto& note : song.motif().notes()) {
      if (note.start_tick >= section.start_tick && note.start_tick < section.endTick()) {
        pitches.push_back(note.note);
      }
    }
  }
  return pitches;
}

TEST(MotifExplicitMotionTest, ExplicitMotionGovernsSectionsTheBlueprintHints) {
  auto make = [](uint8_t motion) {
    SongConfig config = createDefaultSongConfig(0);
    config.blueprint_id = 1;  // RhythmLock A slots carry an Ostinato hint
    config.seed = 7;
    config.motif_motion = motion;
    return config;
  };
  Generator stepwise;
  stepwise.generateFromConfig(make(0));
  Generator wide;
  wide.generateFromConfig(make(2));
  ASSERT_TRUE(stepwise.getParams().motif_motion_explicit);
  const auto stepwise_pitches = hintedSectionMotifPitches(stepwise.getSong());
  ASSERT_FALSE(stepwise_pitches.empty()) << "the blueprint must hint at least one section";
  EXPECT_NE(stepwise_pitches, hintedSectionMotifPitches(wide.getSong()))
      << "a caller-set motion was replaced by the section hint";

  Generator preset;
  preset.generateFromConfig(make(0xFF));
  EXPECT_FALSE(preset.getParams().motif_motion_explicit);
}

}  // namespace
}  // namespace midisketch
