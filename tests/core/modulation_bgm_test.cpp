/**
 * @file modulation_bgm_test.cpp
 * @brief Tests for BGM-only modulation feature.
 *
 * Verifies that modulation works correctly in BackgroundMotif and SynthDriven
 * composition styles (BGM-only modes).
 */

#include <gtest/gtest.h>

#include "core/generator.h"
#include "core/types.h"

namespace midisketch {
namespace {

class ModulationBGMTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Common setup for all tests
  }
};

// Test: BackgroundMotif mode should respect modulation settings
TEST_F(ModulationBGMTest, BackgroundMotifRespectsModulation) {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.drums_enabled = true;
  params.seed = 12345;

  Generator gen;
  gen.setModulationTiming(ModulationTiming::LastChorus, 2);
  gen.generate(params);

  const Song& song = gen.getSong();

  // Modulation should be set (not zero)
  EXPECT_GT(song.modulationTick(), 0) << "BackgroundMotif should allow modulation";
  EXPECT_EQ(song.modulationAmount(), 2) << "Modulation amount should be 2 semitones";
}

// Test: SynthDriven mode should respect modulation settings
TEST_F(ModulationBGMTest, SynthDrivenRespectsModulation) {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::FutureBass;
  params.chord_id = 0;
  params.composition_style = CompositionStyle::SynthDriven;
  params.drums_enabled = true;
  params.arpeggio_enabled = true;
  params.seed = 12345;

  Generator gen;
  gen.setModulationTiming(ModulationTiming::LastChorus, 3);
  gen.generate(params);

  const Song& song = gen.getSong();

  // Modulation should be set (not zero)
  EXPECT_GT(song.modulationTick(), 0) << "SynthDriven should allow modulation";
  EXPECT_EQ(song.modulationAmount(), 3) << "Modulation amount should be 3 semitones";
}

// Test: MelodyLead mode should continue to work with modulation
TEST_F(ModulationBGMTest, MelodyLeadContinuesToWork) {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.composition_style = CompositionStyle::MelodyLead;
  params.drums_enabled = true;
  params.seed = 12345;

  Generator gen;
  gen.setModulationTiming(ModulationTiming::LastChorus, 2);
  gen.generate(params);

  const Song& song = gen.getSong();

  // Modulation should be set
  EXPECT_GT(song.modulationTick(), 0) << "MelodyLead should allow modulation";
  EXPECT_EQ(song.modulationAmount(), 2) << "Modulation amount should be 2 semitones";
}

// Test: ModulationTiming::None should result in no modulation
TEST_F(ModulationBGMTest, NoneTimingDisablesModulation) {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.drums_enabled = true;
  params.seed = 12345;

  Generator gen;
  gen.setModulationTiming(ModulationTiming::None, 2);
  gen.generate(params);

  const Song& song = gen.getSong();

  // Modulation should be zero with None timing
  EXPECT_EQ(song.modulationTick(), 0) << "ModulationTiming::None should disable modulation";
}

// Test: generateVocal with BGM mode should respect modulation
TEST_F(ModulationBGMTest, GenerateVocalRespectsModulation) {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.drums_enabled = true;
  params.seed = 12345;

  Generator gen;
  gen.setModulationTiming(ModulationTiming::LastChorus, 2);
  gen.generateVocal(params);

  const Song& song = gen.getSong();

  // Modulation should be set even in vocal-only generation
  EXPECT_GT(song.modulationTick(), 0) << "generateVocal should allow modulation in BGM mode";
  EXPECT_EQ(song.modulationAmount(), 2);
}

}  // namespace
}  // namespace midisketch
