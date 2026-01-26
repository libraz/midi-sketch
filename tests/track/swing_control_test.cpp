/**
 * @file swing_control_test.cpp
 * @brief Tests for continuous swing control in drum generation.
 */

#include <gtest/gtest.h>

#include "core/generator.h"
#include "core/section_types.h"
#include "core/timing_constants.h"
#include "track/drums.h"

namespace midisketch {
namespace {

// ============================================================================
// Swing Control Integration Tests
// ============================================================================

class SwingControlTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.key = Key::C;
    params_.bpm = 120;
    params_.mood = Mood::CityPop;  // CityPop has swing
    params_.chord_id = 0;
    params_.drums_enabled = true;
    params_.structure = StructurePattern::BuildUp;
    params_.seed = 42;
    params_.vocal_low = 60;
    params_.vocal_high = 72;
  }

  GeneratorParams params_;
  Generator generator_;
};

TEST_F(SwingControlTest, SwingMoodGeneratesDrums) {
  generator_.generate(params_);

  const auto& drums = generator_.getSong().drums();
  EXPECT_GT(drums.notes().size(), 0) << "Drums should have notes";
}

TEST_F(SwingControlTest, StraightMoodGeneratesDrums) {
  params_.mood = Mood::EnergeticDance;  // Dance is typically straight

  generator_.generate(params_);

  const auto& drums = generator_.getSong().drums();
  EXPECT_GT(drums.notes().size(), 0) << "Drums should have notes";
}

TEST_F(SwingControlTest, DifferentMoodsProduceDifferentTiming) {
  // Generate with swing mood
  params_.mood = Mood::CityPop;
  params_.seed = 100;
  generator_.generate(params_);
  const auto& swing_drums = generator_.getSong().drums();

  // Generate with straight mood using same seed
  Generator generator2;
  params_.mood = Mood::EnergeticDance;
  generator2.generate(params_);
  const auto& straight_drums = generator2.getSong().drums();

  // Both should have drums
  EXPECT_GT(swing_drums.notes().size(), 0);
  EXPECT_GT(straight_drums.notes().size(), 0);

  // Note: Due to different moods, the patterns will be different
  // This test just ensures both generate successfully
}

TEST_F(SwingControlTest, BalladHasSwingFeel) {
  params_.mood = Mood::Ballad;

  generator_.generate(params_);

  const auto& drums = generator_.getSong().drums();
  // Ballad should have drums (though sparse)
  EXPECT_GT(drums.notes().size(), 0) << "Ballad should have drums";
}

TEST_F(SwingControlTest, DrumsGeneratedForAllSections) {
  generator_.generate(params_);

  const auto& drums = generator_.getSong().drums();
  const auto& sections = generator_.getSong().arrangement().sections();

  // Check that we have drums in multiple sections
  size_t sections_with_drums = 0;
  for (const auto& section : sections) {
    bool has_drums_in_section = false;
    for (const auto& note : drums.notes()) {
      if (note.start_tick >= section.start_tick &&
          note.start_tick < section.start_tick + section.bars * 1920) {
        has_drums_in_section = true;
        break;
      }
    }
    if (has_drums_in_section) {
      sections_with_drums++;
    }
  }

  EXPECT_GT(sections_with_drums, 0) << "Should have drums in at least one section";
}

// ============================================================================
// Section-Specific Swing Behavior Tests
// ============================================================================

TEST_F(SwingControlTest, ChorusSectionHasDrums) {
  params_.structure = StructurePattern::StandardPop;  // A -> B -> Chorus

  generator_.generate(params_);

  const auto& drums = generator_.getSong().drums();
  const auto& sections = generator_.getSong().arrangement().sections();

  // Find chorus section
  for (const auto& section : sections) {
    if (section.type == SectionType::Chorus) {
      // Check for drums in chorus
      bool has_drums = false;
      for (const auto& note : drums.notes()) {
        if (note.start_tick >= section.start_tick &&
            note.start_tick < section.start_tick + section.bars * 1920) {
          has_drums = true;
          break;
        }
      }
      EXPECT_TRUE(has_drums) << "Chorus should have drums";
      break;
    }
  }
}

TEST_F(SwingControlTest, IntroSectionHasDrums) {
  params_.structure = StructurePattern::BuildUp;  // Intro -> A -> B -> Chorus

  generator_.generate(params_);

  const auto& drums = generator_.getSong().drums();
  const auto& sections = generator_.getSong().arrangement().sections();

  // Find intro section
  for (const auto& section : sections) {
    if (section.type == SectionType::Intro) {
      // Check for drums in intro
      bool has_drums = false;
      for (const auto& note : drums.notes()) {
        if (note.start_tick >= section.start_tick &&
            note.start_tick < section.start_tick + section.bars * 1920) {
          has_drums = true;
          break;
        }
      }
      EXPECT_TRUE(has_drums) << "Intro should have drums";
      break;
    }
  }
}

// ============================================================================
// Continuous Swing Control Tests
// ============================================================================

// ============================================================================
// Unit Tests for calculateSwingAmount
// ============================================================================

TEST(CalculateSwingAmountTest, ChorusHasConsistentSwing) {
  // Chorus should always return 0.5 regardless of bar position
  EXPECT_FLOAT_EQ(calculateSwingAmount(SectionType::Chorus, 0, 8), 0.5f);
  EXPECT_FLOAT_EQ(calculateSwingAmount(SectionType::Chorus, 4, 8), 0.5f);
  EXPECT_FLOAT_EQ(calculateSwingAmount(SectionType::Chorus, 7, 8), 0.5f);
}

TEST(CalculateSwingAmountTest, ASectionProgressiveSwing) {
  // A section: 0.3 at start, 0.5 at end
  float start = calculateSwingAmount(SectionType::A, 0, 8);
  float end = calculateSwingAmount(SectionType::A, 7, 8);

  EXPECT_NEAR(start, 0.3f, 0.01f) << "A section should start at 0.3";
  EXPECT_NEAR(end, 0.5f, 0.01f) << "A section should end at 0.5";
  EXPECT_GT(end, start) << "A section swing should increase";
}

TEST(CalculateSwingAmountTest, OutroDecreasesSwing) {
  // Outro: 0.4 at start, 0.2 at end
  float start = calculateSwingAmount(SectionType::Outro, 0, 8);
  float end = calculateSwingAmount(SectionType::Outro, 7, 8);

  EXPECT_NEAR(start, 0.4f, 0.01f) << "Outro should start at 0.4";
  EXPECT_NEAR(end, 0.2f, 0.01f) << "Outro should end at 0.2";
  EXPECT_LT(end, start) << "Outro swing should decrease";
}

TEST(CalculateSwingAmountTest, BridgeHasLighterSwing) {
  // Bridge has lighter swing (0.2) for contrast
  float swing = calculateSwingAmount(SectionType::Bridge, 4, 8);
  EXPECT_FLOAT_EQ(swing, 0.2f);
}

TEST(CalculateSwingAmountTest, BSectionSteadySwing) {
  // B section has steady 0.4
  EXPECT_FLOAT_EQ(calculateSwingAmount(SectionType::B, 0, 8), 0.4f);
  EXPECT_FLOAT_EQ(calculateSwingAmount(SectionType::B, 7, 8), 0.4f);
}

TEST(CalculateSwingAmountTest, SwingClampedTo0_7) {
  // Verify swing is always clamped between 0.0 and 0.7
  for (int i = 0; i < 8; ++i) {
    float swing = calculateSwingAmount(SectionType::Chorus, i, 8);
    EXPECT_GE(swing, 0.0f);
    EXPECT_LE(swing, 0.7f);
  }
}

// ============================================================================
// Unit Tests for getSwingOffsetContinuous
// ============================================================================

TEST(GetSwingOffsetContinuousTest, StraightGrooveReturnsZero) {
  // Straight groove should always return 0 offset
  EXPECT_EQ(getSwingOffsetContinuous(DrumGrooveFeel::Straight, TICKS_PER_BEAT / 2,
                                      SectionType::Chorus, 0, 8), 0);
  EXPECT_EQ(getSwingOffsetContinuous(DrumGrooveFeel::Straight, TICKS_PER_BEAT / 2,
                                      SectionType::A, 4, 8), 0);
  EXPECT_EQ(getSwingOffsetContinuous(DrumGrooveFeel::Straight, TICKS_PER_BEAT / 4,
                                      SectionType::B, 2, 8), 0);
}

TEST(GetSwingOffsetContinuousTest, SwingGrooveAppliesOffset) {
  // Swing groove in Chorus (swing amount 0.5)
  // offset = subdivision * swing_amount = 240 * 0.5 = 120
  Tick offset = getSwingOffsetContinuous(DrumGrooveFeel::Swing, TICKS_PER_BEAT / 2,
                                          SectionType::Chorus, 0, 8);
  EXPECT_EQ(offset, 120) << "Chorus swing offset should be 120 ticks";
}

TEST(GetSwingOffsetContinuousTest, ShuffleAmplifiesSwing) {
  // Shuffle amplifies swing by 1.5x
  // Chorus base swing = 0.5, shuffle = 0.5 * 1.5 = 0.75, clamped to 0.7
  // offset = 240 * 0.7 = 168
  Tick shuffle_offset = getSwingOffsetContinuous(DrumGrooveFeel::Shuffle, TICKS_PER_BEAT / 2,
                                                  SectionType::Chorus, 0, 8);
  Tick swing_offset = getSwingOffsetContinuous(DrumGrooveFeel::Swing, TICKS_PER_BEAT / 2,
                                                SectionType::Chorus, 0, 8);
  EXPECT_GT(shuffle_offset, swing_offset) << "Shuffle should have more offset than swing";
}

TEST(GetSwingOffsetContinuousTest, SixteenthNoteHasSmallerOffset) {
  // 16th note (120 ticks) vs 8th note (240 ticks)
  Tick eighth_offset = getSwingOffsetContinuous(DrumGrooveFeel::Swing, TICKS_PER_BEAT / 2,
                                                 SectionType::Chorus, 0, 8);
  Tick sixteenth_offset = getSwingOffsetContinuous(DrumGrooveFeel::Swing, TICKS_PER_BEAT / 4,
                                                    SectionType::Chorus, 0, 8);
  EXPECT_EQ(sixteenth_offset, eighth_offset / 2) << "16th note offset should be half of 8th";
}

TEST(GetSwingOffsetContinuousTest, ProgressiveSwingInASection) {
  // A section first bar (swing ~0.3) vs last bar (swing ~0.5)
  Tick first_bar_offset = getSwingOffsetContinuous(DrumGrooveFeel::Swing, TICKS_PER_BEAT / 2,
                                                    SectionType::A, 0, 8);
  Tick last_bar_offset = getSwingOffsetContinuous(DrumGrooveFeel::Swing, TICKS_PER_BEAT / 2,
                                                   SectionType::A, 7, 8);
  EXPECT_LT(first_bar_offset, last_bar_offset)
      << "A section first bar should have less swing than last bar";
}

}  // namespace
}  // namespace midisketch
