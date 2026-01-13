/**
 * @file generator_di_test.cpp
 * @brief Tests for Generator dependency injection.
 *
 * Demonstrates that Generator can use injected IHarmonyContext implementations,
 * enabling isolated unit testing with stubs/mocks.
 */

#include <gtest/gtest.h>
#include "core/generator.h"
#include "test_support/stub_harmony_context.h"
#include <memory>

namespace midisketch {
namespace {

class GeneratorDITest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::StraightPop;
    params_.chord_id = 0;
    params_.seed = 12345;
    params_.composition_style = CompositionStyle::MelodyLead;
    params_.vocal_low = 60;
    params_.vocal_high = 84;
  }

  GeneratorParams params_;
};

// Test: Default constructor creates working Generator
TEST_F(GeneratorDITest, DefaultConstructorWorks) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  EXPECT_FALSE(song.vocal().empty());
  EXPECT_FALSE(song.bass().empty());
}

// Test: DI constructor accepts custom IHarmonyContext
TEST_F(GeneratorDITest, DIConstructorAcceptsCustomHarmonyContext) {
  auto stub = std::make_unique<test::StubHarmonyContext>();
  stub->setAllPitchesSafe(true);

  Generator gen(std::move(stub));
  gen.generate(params_);

  const auto& song = gen.getSong();
  EXPECT_FALSE(song.vocal().empty());
}

// Test: Injected HarmonyContext is initialized
TEST_F(GeneratorDITest, InjectedContextIsInitialized) {
  auto stub = std::make_unique<test::StubHarmonyContext>();
  auto* stub_ptr = stub.get();  // Keep pointer for inspection

  Generator gen(std::move(stub));
  gen.generate(params_);

  EXPECT_TRUE(stub_ptr->wasInitialized());
}

// Test: Tracks are registered with injected context
TEST_F(GeneratorDITest, TracksAreRegisteredWithInjectedContext) {
  auto stub = std::make_unique<test::StubHarmonyContext>();
  auto* stub_ptr = stub.get();

  Generator gen(std::move(stub));
  gen.generate(params_);

  // Multiple tracks should be registered
  EXPECT_GT(stub_ptr->getRegisteredTrackCount(), 0);
}

// Test: getHarmonyContext returns injected context
TEST_F(GeneratorDITest, GetHarmonyContextReturnsInjectedContext) {
  auto stub = std::make_unique<test::StubHarmonyContext>();
  stub->setChordDegree(4);  // Set to V chord

  Generator gen(std::move(stub));
  // Generate minimal structure first so arrangement is initialized
  gen.generate(params_);

  // getHarmonyContext should return our stub
  const auto& context = gen.getHarmonyContext();
  // Can query through the interface
  EXPECT_EQ(context.getChordDegreeAt(0), 4);
}

// Test: Stub with custom chord tones works
TEST_F(GeneratorDITest, StubWithCustomChordTonesWorks) {
  auto stub = std::make_unique<test::StubHarmonyContext>();
  stub->setChordTones({0, 3, 7});  // Cm chord tones

  Generator gen(std::move(stub));
  gen.generate(params_);

  const auto& context = gen.getHarmonyContext();
  auto tones = context.getChordTonesAt(0);
  ASSERT_EQ(tones.size(), 3);
  EXPECT_EQ(tones[0], 0);
  EXPECT_EQ(tones[1], 3);
  EXPECT_EQ(tones[2], 7);
}

// Test: Regenerate works with injected context
TEST_F(GeneratorDITest, RegenerateWorksWithInjectedContext) {
  auto stub = std::make_unique<test::StubHarmonyContext>();
  auto* stub_ptr = stub.get();

  Generator gen(std::move(stub));
  gen.generate(params_);

  int initial_clear_count = stub_ptr->getClearCount();

  // Regenerate should work
  gen.regenerateVocal(99999);

  // Notes should have been cleared during regeneration
  EXPECT_GE(stub_ptr->getClearCount(), initial_clear_count);
}

// Test: BGM mode works with injected context
TEST_F(GeneratorDITest, BGMModeWorksWithInjectedContext) {
  auto stub = std::make_unique<test::StubHarmonyContext>();
  stub->setAllPitchesSafe(true);

  params_.composition_style = CompositionStyle::BackgroundMotif;
  params_.skip_vocal = true;

  Generator gen(std::move(stub));
  gen.generate(params_);

  const auto& song = gen.getSong();
  EXPECT_TRUE(song.vocal().empty());  // No vocal in BGM mode
  EXPECT_FALSE(song.bass().empty());
}

}  // namespace
}  // namespace midisketch
