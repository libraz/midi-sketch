/**
 * @file modulation_calculator_test.cpp
 * @brief Tests for modulation calculator.
 */

#include "core/modulation_calculator.h"

#include <gtest/gtest.h>

#include <set>

#include "core/preset_types.h"
#include "core/structure.h"

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

// Plan the modulation for the given settings.
ModulationCalculator::ModulationResult planFor(ModulationTiming timing, int8_t semitones,
                                               StructurePattern structure,
                                               const std::vector<Section>& sections,
                                               uint32_t seed = 42) {
  GeneratorParams params;
  params.modulation_timing = timing;
  params.modulation_semitones = semitones;
  params.structure = structure;
  params.seed = seed;
  return ModulationCalculator::plan(params, sections);
}

// ============================================================================
// ModulationTiming::None Tests
// ============================================================================

TEST(ModulationCalculatorTest, TimingNoneReturnsZero) {
  auto sections = buildStructure(StructurePattern::StandardPop);

  auto result = planFor(ModulationTiming::None, 2, StructurePattern::StandardPop, sections);

  EXPECT_EQ(result.tick, 0u);
  EXPECT_EQ(result.amount, 0);
}

// ============================================================================
// ModulationTiming::LastChorus Tests
// ============================================================================

TEST(ModulationCalculatorTest, LastChorusFindsLastChorus) {
  std::vector<Section> sections = {
      makeSection(SectionType::Intro, 4, 0), makeSection(SectionType::Chorus, 8, 4 * TICKS_PER_BAR),
      makeSection(SectionType::A, 8, 12 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 20 * TICKS_PER_BAR),  // Last chorus
  };

  auto result = planFor(ModulationTiming::LastChorus, 2, StructurePattern::StandardPop, sections);

  EXPECT_EQ(result.tick, 20u * TICKS_PER_BAR);
  EXPECT_EQ(result.amount, 2);
}

TEST(ModulationCalculatorTest, LastChorusNoChorus) {
  std::vector<Section> sections = {
      makeSection(SectionType::Intro, 4, 0),
      makeSection(SectionType::A, 8, 4 * TICKS_PER_BAR),
  };

  auto result = planFor(ModulationTiming::LastChorus, 2, StructurePattern::StandardPop, sections);

  EXPECT_EQ(result.tick, 0u);  // No chorus found
}

// ============================================================================
// ModulationTiming::AfterBridge Tests
// ============================================================================

TEST(ModulationCalculatorTest, AfterBridgeFindsChorusAfterBridge) {
  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),
      makeSection(SectionType::Bridge, 8, 8 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 16 * TICKS_PER_BAR),  // After bridge
  };

  auto result =
      planFor(ModulationTiming::AfterBridge, 2, StructurePattern::FullWithBridge, sections);

  EXPECT_EQ(result.tick, 16u * TICKS_PER_BAR);
}

TEST(ModulationCalculatorTest, AfterBridgeFallbackToLastChorus) {
  std::vector<Section> sections = {
      makeSection(SectionType::A, 8, 0),
      makeSection(SectionType::Chorus, 8, 8 * TICKS_PER_BAR),  // No bridge before
  };

  auto result = planFor(ModulationTiming::AfterBridge, 2, StructurePattern::StandardPop, sections);

  // Falls back to last chorus
  EXPECT_EQ(result.tick, 8u * TICKS_PER_BAR);
}

// ============================================================================
// ModulationTiming::EachChorus Tests
// ============================================================================

TEST(ModulationCalculatorTest, EachChorusFallsBackToLastChorus) {
  std::vector<Section> sections = {
      makeSection(SectionType::Intro, 4, 0),
      makeSection(SectionType::Chorus, 8, 4 * TICKS_PER_BAR),  // First
      makeSection(SectionType::A, 8, 12 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 20 * TICKS_PER_BAR),  // Second
  };

  auto result = planFor(ModulationTiming::EachChorus, 3, StructurePattern::StandardPop, sections);

  // Song has one modulation boundary, so EachChorus uses the final chorus.
  EXPECT_EQ(result.tick, 20u * TICKS_PER_BAR);
  EXPECT_EQ(result.amount, 3);
}

// ============================================================================
// ModulationTiming::Random Tests
// ============================================================================

TEST(ModulationCalculatorTest, RandomSelectsChorus) {
  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),
      makeSection(SectionType::A, 8, 8 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 16 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 24 * TICKS_PER_BAR),
  };

  auto result = planFor(ModulationTiming::Random, 1, StructurePattern::StandardPop, sections);

  // Should select one of the choruses the song can modulate into
  EXPECT_TRUE(result.tick == 16u * TICKS_PER_BAR || result.tick == 24u * TICKS_PER_BAR);
  EXPECT_EQ(result.amount, 1);
}

// A chorus opening the song has no earlier key to leave, so Random never picks
// it while a later chorus exists, on any chorus-first form and any seed.
TEST(ModulationCalculatorTest, RandomModulatesChorusFirstFormsAfterTheStart) {
  const StructurePattern forms[] = {StructurePattern::ChorusFirst,
                                    StructurePattern::ChorusFirstShort,
                                    StructurePattern::ChorusFirstFull};
  for (StructurePattern form : forms) {
    auto sections = buildStructure(form);
    ASSERT_FALSE(sections.empty());
    ASSERT_EQ(sections.front().type, SectionType::Chorus);
    bool has_later_chorus = false;
    for (const auto& section : sections) {
      has_later_chorus |= section.type == SectionType::Chorus && section.start_tick > 0;
    }
    ASSERT_TRUE(has_later_chorus) << static_cast<int>(form);

    for (uint32_t seed = 1; seed <= 64; ++seed) {
      auto result = planFor(ModulationTiming::Random, 2, form, sections, seed);
      EXPECT_GT(result.tick, 0u) << static_cast<int>(form) << " seed " << seed;
      EXPECT_EQ(result.amount, 2) << static_cast<int>(form) << " seed " << seed;
    }
  }
}

// The record is one fact: no exit path states an amount without its point.
TEST(ModulationCalculatorTest, AmountIsStatedOnlyWithAModulationPoint) {
  const std::vector<std::vector<Section>> layouts = {
      {},
      {makeSection(SectionType::Chorus, 8, 0)},
      {makeSection(SectionType::Intro, 4, 0), makeSection(SectionType::A, 8, 4 * TICKS_PER_BAR)},
      {makeSection(SectionType::Chorus, 8, 0), makeSection(SectionType::A, 8, 8 * TICKS_PER_BAR),
       makeSection(SectionType::Chorus, 8, 16 * TICKS_PER_BAR)},
  };
  const ModulationTiming timings[] = {ModulationTiming::None, ModulationTiming::LastChorus,
                                      ModulationTiming::AfterBridge, ModulationTiming::EachChorus,
                                      ModulationTiming::Random};
  int modulated = 0;
  int unmodulated = 0;
  for (const auto& sections : layouts) {
    for (ModulationTiming timing : timings) {
      for (uint32_t seed = 1; seed <= 8; ++seed) {
        auto result = planFor(timing, 3, StructurePattern::StandardPop, sections, seed);
        EXPECT_EQ(result.amount != 0, result.tick > 0)
            << "timing " << static_cast<int>(timing) << " tick " << result.tick << " amount "
            << static_cast<int>(result.amount);
        (result.tick > 0 ? modulated : unmodulated)++;
      }
    }
  }
  ASSERT_GT(modulated, 0);
  ASSERT_GT(unmodulated, 0);
}

// The planner and the song both ask plan() and must get the same answer, so
// Random draws from the seed alone: one seed always lands on the same chorus,
// and the seeds between them still reach more than one.
TEST(ModulationCalculatorTest, RandomDrawsFromTheSeedAlone) {
  std::vector<Section> sections = {
      makeSection(SectionType::A, 8, 0),
      makeSection(SectionType::Chorus, 8, 8 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 16 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 24 * TICKS_PER_BAR),
  };

  std::set<Tick> reached;
  for (uint32_t seed = 1; seed <= 32; ++seed) {
    auto first =
        planFor(ModulationTiming::Random, 2, StructurePattern::StandardPop, sections, seed);
    auto again =
        planFor(ModulationTiming::Random, 2, StructurePattern::StandardPop, sections, seed);
    ASSERT_GT(first.tick, 0u);
    EXPECT_EQ(first.tick, again.tick) << "seed " << seed;
    EXPECT_EQ(first.amount, again.amount) << "seed " << seed;
    reached.insert(first.tick);
  }
  EXPECT_GT(reached.size(), 1u);
}

// ============================================================================
// Structure Pattern Tests
// ============================================================================

TEST(ModulationCalculatorTest, RepeatChorusSecondChorus) {
  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),  // First chorus
      makeSection(SectionType::A, 8, 8 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 16 * TICKS_PER_BAR),  // Second chorus
  };

  // Use LastChorus timing to find modulation point
  auto result = planFor(ModulationTiming::LastChorus, 2, StructurePattern::RepeatChorus, sections);

  // Should find second chorus as the last chorus
  EXPECT_EQ(result.tick, 16u * TICKS_PER_BAR);
}

TEST(ModulationCalculatorTest, StandardPopChorusAfterB) {
  std::vector<Section> sections = {
      makeSection(SectionType::Intro, 4, 0), makeSection(SectionType::B, 8, 4 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 12 * TICKS_PER_BAR),  // After B
  };

  // Use AfterBridge timing to find chorus after B section
  auto result = planFor(ModulationTiming::AfterBridge, 2, StructurePattern::StandardPop, sections);

  // Should find the chorus after B section (using fallback to last chorus)
  EXPECT_EQ(result.tick, 12u * TICKS_PER_BAR);
}

// ============================================================================
// Short Structure Tests
// ============================================================================

TEST(ModulationCalculatorTest, ShortFormNoModulation) {
  auto sections = buildStructure(StructurePattern::ShortForm);

  auto result = planFor(ModulationTiming::LastChorus, 2, StructurePattern::ShortForm, sections);

  EXPECT_EQ(result.tick, 0u);  // Short form doesn't support modulation
}

TEST(ModulationCalculatorTest, DirectChorusNoModulation) {
  auto sections = buildStructure(StructurePattern::DirectChorus);

  auto result = planFor(ModulationTiming::LastChorus, 2, StructurePattern::DirectChorus, sections);

  EXPECT_EQ(result.tick, 0u);  // Direct chorus doesn't support modulation
}

TEST(ModulationCalculatorTest, FlowThatReplacedAShortFormLabelStillModulates) {
  // A blueprint flow replaces the drawn form, so a DirectChorus label does not
  // describe the sections that play; the short-form exception must not apply.
  constexpr uint8_t kBalladBlueprint = 3;
  const auto& blueprint = getProductionBlueprint(kBalladBlueprint);
  ASSERT_NE(blueprint.section_flow, nullptr);
  const auto sections = buildStructureFromBlueprint(blueprint);
  Tick last_chorus = 0;
  for (const auto& section : sections) {
    if (section.type == SectionType::Chorus) last_chorus = section.start_tick;
  }
  ASSERT_GT(last_chorus, 0u);

  for (StructurePattern label : {StructurePattern::DirectChorus, StructurePattern::ShortForm}) {
    GeneratorParams params;
    params.modulation_timing = ModulationTiming::LastChorus;
    params.structure = label;
    params.blueprint_id = kBalladBlueprint;
    ASSERT_NE(resolveBuiltForm(params).flow, nullptr);
    EXPECT_EQ(ModulationCalculator::plan(params, sections).tick, last_chorus)
        << "label " << static_cast<int>(label);
  }
}

TEST(ModulationCalculatorTest, ShortFormTheSectionsFollowStillDoesNotModulate) {
  // The same blueprint with the form chosen explicitly or sized to a duration
  // lays out the short form itself, so the exception holds there.
  const auto sections = buildStructure(StructurePattern::DirectChorus);
  for (bool explicit_form : {true, false}) {
    GeneratorParams params;
    params.modulation_timing = ModulationTiming::LastChorus;
    params.structure = StructurePattern::DirectChorus;
    params.blueprint_id = 3;
    params.form_explicit = explicit_form;
    params.target_duration_seconds = explicit_form ? 0 : 40;
    ASSERT_EQ(resolveBuiltForm(params).flow, nullptr);
    EXPECT_EQ(ModulationCalculator::plan(params, sections).tick, 0u)
        << (explicit_form ? "explicit form" : "duration");
  }
}

// ============================================================================
// Semitones Parameter Tests
// ============================================================================

TEST(ModulationCalculatorTest, SemitonesDefaultsToTwo) {
  std::vector<Section> sections = {
      makeSection(SectionType::A, 8, 0),
      makeSection(SectionType::Chorus, 8, 8 * TICKS_PER_BAR),
  };

  auto result = planFor(ModulationTiming::LastChorus, 0, StructurePattern::StandardPop, sections);

  EXPECT_EQ(result.amount, 2);  // Default when 0 is passed
}

TEST(ModulationCalculatorTest, SemitonesRespected) {
  std::vector<Section> sections = {
      makeSection(SectionType::A, 8, 0),
      makeSection(SectionType::Chorus, 8, 8 * TICKS_PER_BAR),
  };

  auto result = planFor(ModulationTiming::LastChorus, 4, StructurePattern::StandardPop, sections);

  EXPECT_EQ(result.amount, 4);
}

// ============================================================================
// Empty Sections Tests
// ============================================================================

TEST(ModulationCalculatorTest, EmptySections) {
  std::vector<Section> sections;

  auto result = planFor(ModulationTiming::LastChorus, 2, StructurePattern::StandardPop, sections);

  EXPECT_EQ(result.tick, 0u);
}

// ============================================================================
// Integration with buildStructure Tests
// ============================================================================

TEST(ModulationCalculatorTest, StandardPopIntegration) {
  auto sections = buildStructure(StructurePattern::StandardPop);

  auto result = planFor(ModulationTiming::LastChorus, 2, StructurePattern::StandardPop, sections);

  // StandardPop should have a chorus and thus a modulation point
  EXPECT_GT(result.tick, 0u);
}

TEST(ModulationCalculatorTest, ExtendedFullIntegration) {
  auto sections = buildStructure(StructurePattern::ExtendedFull);

  auto result = planFor(ModulationTiming::AfterBridge, 2, StructurePattern::ExtendedFull, sections);

  // ExtendedFull should have chorus after bridge
  EXPECT_GT(result.tick, 0u);
}

}  // namespace
}  // namespace midisketch
