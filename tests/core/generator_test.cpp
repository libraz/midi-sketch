#include <gtest/gtest.h>
#include "core/generator.h"

namespace midisketch {
namespace {

TEST(GeneratorTest, ModulationStandardPop) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.modulation = true;
  params.seed = 12345;

  gen.generate(params);
  const auto& result = gen.getResult();

  // StandardPop: B(16 bars) -> Chorus, modulation at Chorus start
  // 16 bars * 4 beats * 480 ticks = 30720
  EXPECT_EQ(result.modulation_tick, 30720u);
  EXPECT_EQ(result.modulation_amount, 1);  // Non-ballad = +1 semitone
}

TEST(GeneratorTest, ModulationBallad) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::Ballad;
  params.modulation = true;
  params.seed = 12345;

  gen.generate(params);
  const auto& result = gen.getResult();

  EXPECT_EQ(result.modulation_amount, 2);  // Ballad = +2 semitones
}

TEST(GeneratorTest, ModulationRepeatChorus) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::RepeatChorus;
  params.mood = Mood::StraightPop;
  params.modulation = true;
  params.seed = 12345;

  gen.generate(params);
  const auto& result = gen.getResult();

  // RepeatChorus: A(8) + B(8) + Chorus(8) + Chorus(8)
  // Modulation at second Chorus = 24 bars * 4 * 480 = 46080
  EXPECT_EQ(result.modulation_tick, 46080u);
}

TEST(GeneratorTest, ModulationDisabled) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.modulation = false;
  params.seed = 12345;

  gen.generate(params);
  const auto& result = gen.getResult();

  EXPECT_EQ(result.modulation_tick, 0u);
  EXPECT_EQ(result.modulation_amount, 0);
}

TEST(GeneratorTest, NoModulationForShortStructures) {
  Generator gen;
  GeneratorParams params{};
  params.modulation = true;
  params.seed = 12345;

  // DirectChorus has no modulation point
  params.structure = StructurePattern::DirectChorus;
  gen.generate(params);
  EXPECT_EQ(gen.getResult().modulation_tick, 0u);

  // ShortForm has no modulation point
  params.structure = StructurePattern::ShortForm;
  gen.generate(params);
  EXPECT_EQ(gen.getResult().modulation_tick, 0u);
}

TEST(GeneratorTest, MarkerIncludesModulation) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.modulation = true;
  params.seed = 12345;

  gen.generate(params);
  const auto& result = gen.getResult();

  // Should have 4 markers: A, B, Chorus, Mod+1
  ASSERT_EQ(result.markers.size(), 4u);
  EXPECT_EQ(result.markers[3].text, "Mod+1");
}

}  // namespace
}  // namespace midisketch
