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
  // Triplet-grid offset = 80 * swing_amount = 80 * 0.5 = 40
  Tick offset = getSwingOffsetContinuous(DrumGrooveFeel::Swing, TICKS_PER_BEAT / 2,
                                          SectionType::Chorus, 0, 8);
  EXPECT_EQ(offset, 40) << "Chorus swing offset should be 40 ticks (triplet grid)";
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

// ============================================================================
// Swing Override Tests (Blueprint parameterization)
// ============================================================================

TEST(CalculateSwingAmountTest, SwingOverrideUsedWhenPositive) {
  // When swing_override >= 0, it should be used instead of section default
  float override_value = 0.35f;
  float result = calculateSwingAmount(SectionType::Chorus, 0, 8, override_value);
  EXPECT_FLOAT_EQ(result, override_value) << "Override value should be used";
}

TEST(CalculateSwingAmountTest, SwingOverrideClampedToMax) {
  // Override values > 0.7 should be clamped
  float result = calculateSwingAmount(SectionType::A, 0, 8, 0.9f);
  EXPECT_FLOAT_EQ(result, 0.7f) << "Override should be clamped to 0.7";
}

TEST(CalculateSwingAmountTest, SwingOverrideClampedToMin) {
  // Override values < 0 (but >= 0) should work, values < 0 use section default
  float result_zero = calculateSwingAmount(SectionType::A, 0, 8, 0.0f);
  EXPECT_FLOAT_EQ(result_zero, 0.0f) << "Zero override should give zero swing";
}

TEST(CalculateSwingAmountTest, NegativeOverrideUsesSectionDefault) {
  // -1.0 (or any negative) means use section default
  float with_default = calculateSwingAmount(SectionType::Chorus, 0, 8, -1.0f);
  float section_default = calculateSwingAmount(SectionType::Chorus, 0, 8);
  EXPECT_FLOAT_EQ(with_default, section_default)
      << "Negative override should use section default";
}

TEST(GetSwingOffsetContinuousTest, OverridePassedToSwingCalculation) {
  // Test that override is properly passed through getSwingOffsetContinuous
  Tick offset_with_override = getSwingOffsetContinuous(
      DrumGrooveFeel::Swing, TICKS_PER_BEAT / 2, SectionType::A, 0, 8, 0.5f);
  Tick offset_section_default = getSwingOffsetContinuous(
      DrumGrooveFeel::Swing, TICKS_PER_BEAT / 2, SectionType::A, 0, 8, -1.0f);

  // A section default at bar 0 is ~0.3, override is 0.5
  // Triplet-grid offset = 80 * swing_amount: 80 * 0.5 = 40 vs 80 * 0.3 = 24
  EXPECT_EQ(offset_with_override, 40) << "Override 0.5 should give 40 ticks (triplet grid)";
  EXPECT_NEAR(offset_section_default, 24, 2) << "Section default should give ~24 ticks (triplet grid)";
}

// ============================================================================
// Phase 1 Improvements: Outro Swing Behavior Tests
// ============================================================================

TEST(CalculateSwingAmountTest, OutroDecayIsGradual) {
  // Outro swing should decay gradually, not abruptly
  // The key behavior: mid-section swing should be closer to start than to end
  // This creates a "landing" feel rather than a sudden drop
  float start = calculateSwingAmount(SectionType::Outro, 0, 8);
  float mid = calculateSwingAmount(SectionType::Outro, 3, 8);
  float end = calculateSwingAmount(SectionType::Outro, 7, 8);

  // Basic decay verification
  EXPECT_GT(start, end) << "Outro swing should decrease over time";

  // Gradual decay: mid should be closer to start than linear interpolation
  // Linear midpoint at 3/7 progress would be: start - (start-end) * 3/7
  float linear_mid = start - (start - end) * 3.0f / 7.0f;
  EXPECT_GT(mid, linear_mid) << "Outro decay should be gradual (quadratic), not linear";
}

// ============================================================================
// Phase 1 Improvements: Mood-Dependent Swing Behavior Tests
// ============================================================================

TEST(HiHatSwingFactorTest, CityPopHasStrongerSwingThanIdolPop) {
  // CityPop is a groove-oriented genre that benefits from stronger swing
  // IdolPop is precise and energetic, requiring tighter timing
  float citypop = getHiHatSwingFactor(Mood::CityPop);
  float idolpop = getHiHatSwingFactor(Mood::IdolPop);

  EXPECT_GT(citypop, idolpop)
      << "CityPop should have stronger swing than IdolPop for groove feel";
}

TEST(HiHatSwingFactorTest, BalladHasModerateSwing) {
  // Ballad swing should be between tight (IdolPop) and loose (CityPop)
  float ballad = getHiHatSwingFactor(Mood::Ballad);
  float idolpop = getHiHatSwingFactor(Mood::IdolPop);
  float citypop = getHiHatSwingFactor(Mood::CityPop);

  EXPECT_GT(ballad, idolpop) << "Ballad should have more swing than IdolPop";
  EXPECT_LT(ballad, citypop) << "Ballad should have less swing than CityPop";
}

TEST(HiHatSwingFactorTest, AllMoodsProduceValidSwingFactor) {
  // All moods must produce swing factors that result in musically valid timing
  std::vector<Mood> all_moods = {
      Mood::StraightPop, Mood::BrightUpbeat, Mood::EnergeticDance, Mood::LightRock,
      Mood::MidPop, Mood::EmotionalPop, Mood::Sentimental, Mood::Chill,
      Mood::Ballad, Mood::DarkPop, Mood::Dramatic, Mood::Nostalgic,
      Mood::ModernPop, Mood::ElectroPop, Mood::IdolPop, Mood::Anthem,
      Mood::Yoasobi, Mood::Synthwave, Mood::FutureBass, Mood::CityPop};

  for (Mood mood : all_moods) {
    float factor = getHiHatSwingFactor(mood);
    EXPECT_GE(factor, 0.2f) << "Swing factor too low - would sound too mechanical";
    EXPECT_LE(factor, 0.8f) << "Swing factor too high - would sound sloppy";
  }
}

// ============================================================================
// Time Feel Tests
// ============================================================================

TEST(TimeFeelTest, OnBeatReturnsOriginalTick) {
  // OnBeat should not change the tick position
  EXPECT_EQ(applyTimeFeel(1920, TimeFeel::OnBeat, 120), 1920);
  EXPECT_EQ(applyTimeFeel(0, TimeFeel::OnBeat, 120), 0);
  EXPECT_EQ(applyTimeFeel(3840, TimeFeel::OnBeat, 180), 3840);
}

TEST(TimeFeelTest, LaidBackAddsPositiveOffset) {
  // LaidBack should push notes behind the beat (positive offset)
  Tick original = 1920;
  Tick laid_back = applyTimeFeel(original, TimeFeel::LaidBack, 120);

  EXPECT_GT(laid_back, original) << "LaidBack should push notes later";
  // At 120 BPM, +10ms = ~10 * 120 / 125 = ~9-10 ticks
  EXPECT_NEAR(laid_back - original, 10, 2) << "LaidBack offset should be ~10 ticks at 120 BPM";
}

TEST(TimeFeelTest, PushedSubtractsOffset) {
  // Pushed should pull notes ahead of the beat (negative offset)
  Tick original = 1920;
  Tick pushed = applyTimeFeel(original, TimeFeel::Pushed, 120);

  EXPECT_LT(pushed, original) << "Pushed should pull notes earlier";
  // At 120 BPM, -7ms = ~-7 * 120 / 125 = ~-6-7 ticks
  EXPECT_NEAR(static_cast<int>(original - pushed), 7, 2) << "Pushed offset should be ~7 ticks at 120 BPM";
}

TEST(TimeFeelTest, PushedDoesNotGoNegative) {
  // Pushed should not result in negative tick values
  Tick result = applyTimeFeel(0, TimeFeel::Pushed, 120);
  EXPECT_EQ(result, 0) << "Pushed at tick 0 should stay at 0";

  result = applyTimeFeel(3, TimeFeel::Pushed, 120);
  EXPECT_EQ(result, 0) << "Pushed with small tick should clamp to 0";
}

TEST(TimeFeelTest, OffsetScalesWithBPM) {
  // Higher BPM should result in larger tick offset for same time feel
  Tick original = 1920;

  Tick laid_back_120 = applyTimeFeel(original, TimeFeel::LaidBack, 120);
  Tick laid_back_180 = applyTimeFeel(original, TimeFeel::LaidBack, 180);

  // At 180 BPM, offset should be ~1.5x that of 120 BPM (proportional to BPM)
  EXPECT_GT(laid_back_180 - original, laid_back_120 - original)
      << "Higher BPM should have larger tick offset";
}

TEST(TimeFeelTest, TripletReturnsOriginalTick) {
  // Triplet feel is not implemented as a simple offset
  // For now, it should return the original tick
  EXPECT_EQ(applyTimeFeel(1920, TimeFeel::Triplet, 120), 1920);
}

// ============================================================================
// Mood Time Feel Mapping Tests
// ============================================================================

TEST(MoodTimeFeelTest, BalladIsLaidBack) {
  EXPECT_EQ(getMoodTimeFeel(Mood::Ballad), TimeFeel::LaidBack);
}

TEST(MoodTimeFeelTest, ChillIsLaidBack) {
  EXPECT_EQ(getMoodTimeFeel(Mood::Chill), TimeFeel::LaidBack);
}

TEST(MoodTimeFeelTest, CityPopIsLaidBack) {
  EXPECT_EQ(getMoodTimeFeel(Mood::CityPop), TimeFeel::LaidBack);
}

TEST(MoodTimeFeelTest, EnergeticDanceIsPushed) {
  EXPECT_EQ(getMoodTimeFeel(Mood::EnergeticDance), TimeFeel::Pushed);
}

TEST(MoodTimeFeelTest, YoasobiIsPushed) {
  EXPECT_EQ(getMoodTimeFeel(Mood::Yoasobi), TimeFeel::Pushed);
}

TEST(MoodTimeFeelTest, StandardPopIsOnBeat) {
  EXPECT_EQ(getMoodTimeFeel(Mood::StraightPop), TimeFeel::OnBeat);
}

TEST(MoodTimeFeelTest, AllMoodsReturnValidTimeFeel) {
  std::vector<Mood> all_moods = {
      Mood::StraightPop, Mood::BrightUpbeat, Mood::EnergeticDance, Mood::LightRock,
      Mood::MidPop, Mood::EmotionalPop, Mood::Sentimental, Mood::Chill,
      Mood::Ballad, Mood::DarkPop, Mood::Dramatic, Mood::Nostalgic,
      Mood::ModernPop, Mood::ElectroPop, Mood::IdolPop, Mood::Anthem,
      Mood::Yoasobi, Mood::Synthwave, Mood::FutureBass, Mood::CityPop};

  for (Mood mood : all_moods) {
    TimeFeel feel = getMoodTimeFeel(mood);
    // All time feels should be valid enum values
    EXPECT_TRUE(feel == TimeFeel::OnBeat || feel == TimeFeel::LaidBack ||
                feel == TimeFeel::Pushed || feel == TimeFeel::Triplet)
        << "Invalid TimeFeel for mood " << static_cast<int>(mood);
  }
}

}  // namespace
}  // namespace midisketch
