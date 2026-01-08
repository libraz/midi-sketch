#include <gtest/gtest.h>
#include "core/modulation_calculator.h"
#include "core/structure.h"
#include <random>

namespace midisketch {
namespace {

// Helper to create a section
Section makeSection(SectionType type, uint8_t bars, Tick start_tick) {
  Section s;
  s.type = type;
  s.bars = bars;
  s.start_tick = start_tick;
  return s;
}

// ============================================================================
// ModulationTiming::None Tests
// ============================================================================

TEST(ModulationCalculatorTest, TimingNoneReturnsZero) {
  std::mt19937 rng(42);
  auto sections = buildStructure(StructurePattern::StandardPop);

  auto result = ModulationCalculator::calculate(
      ModulationTiming::None, 2, StructurePattern::StandardPop, sections, rng);

  EXPECT_EQ(result.tick, 0u);
  EXPECT_EQ(result.amount, 0);
}

// ============================================================================
// ModulationTiming::LastChorus Tests
// ============================================================================

TEST(ModulationCalculatorTest, LastChorusFindsLastChorus) {
  std::mt19937 rng(42);

  std::vector<Section> sections = {
      makeSection(SectionType::Intro, 4, 0),
      makeSection(SectionType::Chorus, 8, 4 * TICKS_PER_BAR),
      makeSection(SectionType::A, 8, 12 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 20 * TICKS_PER_BAR),  // Last chorus
  };

  auto result = ModulationCalculator::calculate(
      ModulationTiming::LastChorus, 2, StructurePattern::StandardPop, sections, rng);

  EXPECT_EQ(result.tick, 20u * TICKS_PER_BAR);
  EXPECT_EQ(result.amount, 2);
}

TEST(ModulationCalculatorTest, LastChorusNoChorus) {
  std::mt19937 rng(42);

  std::vector<Section> sections = {
      makeSection(SectionType::Intro, 4, 0),
      makeSection(SectionType::A, 8, 4 * TICKS_PER_BAR),
  };

  auto result = ModulationCalculator::calculate(
      ModulationTiming::LastChorus, 2, StructurePattern::StandardPop, sections, rng);

  EXPECT_EQ(result.tick, 0u);  // No chorus found
}

// ============================================================================
// ModulationTiming::AfterBridge Tests
// ============================================================================

TEST(ModulationCalculatorTest, AfterBridgeFindsChorusAfterBridge) {
  std::mt19937 rng(42);

  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),
      makeSection(SectionType::Bridge, 8, 8 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 16 * TICKS_PER_BAR),  // After bridge
  };

  auto result = ModulationCalculator::calculate(
      ModulationTiming::AfterBridge, 2, StructurePattern::FullWithBridge, sections, rng);

  EXPECT_EQ(result.tick, 16u * TICKS_PER_BAR);
}

TEST(ModulationCalculatorTest, AfterBridgeFallbackToLastChorus) {
  std::mt19937 rng(42);

  std::vector<Section> sections = {
      makeSection(SectionType::A, 8, 0),
      makeSection(SectionType::Chorus, 8, 8 * TICKS_PER_BAR),  // No bridge before
  };

  auto result = ModulationCalculator::calculate(
      ModulationTiming::AfterBridge, 2, StructurePattern::StandardPop, sections, rng);

  // Falls back to last chorus
  EXPECT_EQ(result.tick, 8u * TICKS_PER_BAR);
}

// ============================================================================
// ModulationTiming::EachChorus Tests
// ============================================================================

TEST(ModulationCalculatorTest, EachChorusReturnsFirstChorus) {
  std::mt19937 rng(42);

  std::vector<Section> sections = {
      makeSection(SectionType::Intro, 4, 0),
      makeSection(SectionType::Chorus, 8, 4 * TICKS_PER_BAR),    // First
      makeSection(SectionType::A, 8, 12 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 20 * TICKS_PER_BAR),   // Second
  };

  auto result = ModulationCalculator::calculate(
      ModulationTiming::EachChorus, 3, StructurePattern::StandardPop, sections, rng);

  // Currently only returns first chorus (noted limitation)
  EXPECT_EQ(result.tick, 4u * TICKS_PER_BAR);
  EXPECT_EQ(result.amount, 3);
}

// ============================================================================
// ModulationTiming::Random Tests
// ============================================================================

TEST(ModulationCalculatorTest, RandomSelectsChorus) {
  std::mt19937 rng(42);

  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),
      makeSection(SectionType::A, 8, 8 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 16 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 24 * TICKS_PER_BAR),
  };

  auto result = ModulationCalculator::calculate(
      ModulationTiming::Random, 1, StructurePattern::StandardPop, sections, rng);

  // Should select one of the chorus ticks
  EXPECT_TRUE(result.tick == 0 ||
              result.tick == 16u * TICKS_PER_BAR ||
              result.tick == 24u * TICKS_PER_BAR);
  EXPECT_EQ(result.amount, 1);
}

TEST(ModulationCalculatorTest, RandomDeterministic) {
  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),
      makeSection(SectionType::Chorus, 8, 8 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 16 * TICKS_PER_BAR),
  };

  // Same seed should give same result
  std::mt19937 rng1(12345);
  auto result1 = ModulationCalculator::calculate(
      ModulationTiming::Random, 2, StructurePattern::StandardPop, sections, rng1);

  std::mt19937 rng2(12345);
  auto result2 = ModulationCalculator::calculate(
      ModulationTiming::Random, 2, StructurePattern::StandardPop, sections, rng2);

  EXPECT_EQ(result1.tick, result2.tick);
}

// ============================================================================
// Legacy Structure Pattern Tests
// ============================================================================

TEST(ModulationCalculatorTest, RepeatChorusSecondChorus) {
  std::mt19937 rng(42);

  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),               // First chorus
      makeSection(SectionType::A, 8, 8 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 16 * TICKS_PER_BAR),  // Second chorus
  };

  // Use LastChorus timing to find modulation point
  auto result = ModulationCalculator::calculate(
      ModulationTiming::LastChorus, 2, StructurePattern::RepeatChorus, sections, rng);

  // Should find second chorus as the last chorus
  EXPECT_EQ(result.tick, 16u * TICKS_PER_BAR);
}

TEST(ModulationCalculatorTest, StandardPopChorusAfterB) {
  std::mt19937 rng(42);

  std::vector<Section> sections = {
      makeSection(SectionType::Intro, 4, 0),
      makeSection(SectionType::B, 8, 4 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 12 * TICKS_PER_BAR),  // After B
  };

  // Use AfterBridge timing to find chorus after B section
  auto result = ModulationCalculator::calculate(
      ModulationTiming::AfterBridge, 2, StructurePattern::StandardPop, sections, rng);

  // Should find the chorus after B section (using fallback to last chorus)
  EXPECT_EQ(result.tick, 12u * TICKS_PER_BAR);
}

// ============================================================================
// Short Structure Tests
// ============================================================================

TEST(ModulationCalculatorTest, ShortFormNoModulation) {
  std::mt19937 rng(42);
  auto sections = buildStructure(StructurePattern::ShortForm);

  auto result = ModulationCalculator::calculate(
      ModulationTiming::LastChorus, 2, StructurePattern::ShortForm, sections, rng);

  EXPECT_EQ(result.tick, 0u);  // Short form doesn't support modulation
}

TEST(ModulationCalculatorTest, DirectChorusNoModulation) {
  std::mt19937 rng(42);
  auto sections = buildStructure(StructurePattern::DirectChorus);

  auto result = ModulationCalculator::calculate(
      ModulationTiming::LastChorus, 2, StructurePattern::DirectChorus, sections, rng);

  EXPECT_EQ(result.tick, 0u);  // Direct chorus doesn't support modulation
}

// ============================================================================
// Semitones Parameter Tests
// ============================================================================

TEST(ModulationCalculatorTest, SemitonesDefaultsToTwo) {
  std::mt19937 rng(42);

  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),
  };

  auto result = ModulationCalculator::calculate(
      ModulationTiming::LastChorus, 0, StructurePattern::StandardPop, sections, rng);

  EXPECT_EQ(result.amount, 2);  // Default when 0 is passed
}

TEST(ModulationCalculatorTest, SemitonesRespected) {
  std::mt19937 rng(42);

  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),
  };

  auto result = ModulationCalculator::calculate(
      ModulationTiming::LastChorus, 4, StructurePattern::StandardPop, sections, rng);

  EXPECT_EQ(result.amount, 4);
}

// ============================================================================
// Empty Sections Tests
// ============================================================================

TEST(ModulationCalculatorTest, EmptySections) {
  std::mt19937 rng(42);
  std::vector<Section> sections;

  auto result = ModulationCalculator::calculate(
      ModulationTiming::LastChorus, 2, StructurePattern::StandardPop, sections, rng);

  EXPECT_EQ(result.tick, 0u);
}

// ============================================================================
// Integration with buildStructure Tests
// ============================================================================

TEST(ModulationCalculatorTest, StandardPopIntegration) {
  std::mt19937 rng(42);
  auto sections = buildStructure(StructurePattern::StandardPop);

  auto result = ModulationCalculator::calculate(
      ModulationTiming::LastChorus, 2, StructurePattern::StandardPop, sections, rng);

  // StandardPop should have a chorus and thus a modulation point
  EXPECT_GT(result.tick, 0u);
}

TEST(ModulationCalculatorTest, ExtendedFullIntegration) {
  std::mt19937 rng(42);
  auto sections = buildStructure(StructurePattern::ExtendedFull);

  auto result = ModulationCalculator::calculate(
      ModulationTiming::AfterBridge, 2, StructurePattern::ExtendedFull, sections, rng);

  // ExtendedFull should have chorus after bridge
  EXPECT_GT(result.tick, 0u);
}

}  // namespace
}  // namespace midisketch
