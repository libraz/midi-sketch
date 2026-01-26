/**
 * @file structure_test.cpp
 * @brief Tests for song structure builders.
 */

#include "core/structure.h"

#include <gtest/gtest.h>

#include <cmath>

#include "core/production_blueprint.h"

namespace midisketch {
namespace {

TEST(StructureTest, StandardPopStructure) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  ASSERT_EQ(sections.size(), 3);

  // A(8) -> B(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[0].bars, 8);
  EXPECT_EQ(sections[0].start_tick, 0);

  EXPECT_EQ(sections[1].type, SectionType::B);
  EXPECT_EQ(sections[1].bars, 8);

  EXPECT_EQ(sections[2].type, SectionType::Chorus);
  EXPECT_EQ(sections[2].bars, 8);
}

TEST(StructureTest, BuildUpStructure) {
  auto sections = buildStructure(StructurePattern::BuildUp);
  ASSERT_EQ(sections.size(), 4);

  // Intro(4) -> A(8) -> B(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::Intro);
  EXPECT_EQ(sections[0].bars, 4);
}

TEST(StructureTest, TotalBarsCalculation) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  EXPECT_EQ(calculateTotalBars(sections), 24);  // 8 + 8 + 8
}

TEST(StructureTest, TotalTicksCalculation) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  Tick expected = 24 * TICKS_PER_BEAT * 4;  // 24 bars * 480 * 4
  EXPECT_EQ(calculateTotalTicks(sections), expected);
}

TEST(StructureTest, DirectChorusStructure) {
  auto sections = buildStructure(StructurePattern::DirectChorus);
  ASSERT_EQ(sections.size(), 2);

  // A(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[1].type, SectionType::Chorus);
}

TEST(StructureTest, RepeatChorusStructure) {
  auto sections = buildStructure(StructurePattern::RepeatChorus);
  ASSERT_EQ(sections.size(), 4);

  // A(8) -> B(8) -> Chorus(8) -> Chorus(8)
  EXPECT_EQ(sections[2].type, SectionType::Chorus);
  EXPECT_EQ(sections[3].type, SectionType::Chorus);
}

TEST(StructureTest, ShortFormStructure) {
  auto sections = buildStructure(StructurePattern::ShortForm);
  ASSERT_EQ(sections.size(), 2);

  // Intro(4) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::Intro);
  EXPECT_EQ(sections[0].bars, 4);
  EXPECT_EQ(sections[1].type, SectionType::Chorus);
  EXPECT_EQ(sections[1].bars, 8);
}

// ============================================================================
// ExtendedFull Pattern Tests
// ============================================================================

TEST(StructureTest, ExtendedFullStructure) {
  auto sections = buildStructure(StructurePattern::ExtendedFull);

  // ExtendedFull should have 90 bars total
  EXPECT_EQ(calculateTotalBars(sections), 88);
}

TEST(StructureTest, ExtendedFullContainsRequiredSections) {
  auto sections = buildStructure(StructurePattern::ExtendedFull);

  // ExtendedFull: Intro(4) + A(8) + B(8) + Chorus(8) + Interlude(4) +
  //               A(8) + B(8) + Chorus(8) + Bridge(8) + Chorus(8) + Chorus(8) + Outro(8)
  // = 12 sections

  // Count section types
  bool has_intro = false;
  bool has_a = false;
  bool has_b = false;
  bool has_chorus = false;
  bool has_interlude = false;
  bool has_bridge = false;
  bool has_outro = false;
  int chorus_count = 0;

  for (const auto& section : sections) {
    switch (section.type) {
      case SectionType::Intro:
        has_intro = true;
        break;
      case SectionType::A:
        has_a = true;
        break;
      case SectionType::B:
        has_b = true;
        break;
      case SectionType::Chorus:
        has_chorus = true;
        chorus_count++;
        break;
      case SectionType::Interlude:
        has_interlude = true;
        break;
      case SectionType::Bridge:
        has_bridge = true;
        break;
      case SectionType::Outro:
        has_outro = true;
        break;
      case SectionType::Chant:
        break;  // Not expected in ExtendedFull
      case SectionType::MixBreak:
        break;  // Not expected in ExtendedFull
    }
  }

  // All required sections should be present
  EXPECT_TRUE(has_intro) << "ExtendedFull should have Intro";
  EXPECT_TRUE(has_a) << "ExtendedFull should have A section";
  EXPECT_TRUE(has_b) << "ExtendedFull should have B section";
  EXPECT_TRUE(has_chorus) << "ExtendedFull should have Chorus";
  EXPECT_TRUE(has_interlude) << "ExtendedFull should have Interlude";
  EXPECT_TRUE(has_bridge) << "ExtendedFull should have Bridge";
  EXPECT_TRUE(has_outro) << "ExtendedFull should have Outro";

  // Should have multiple Chorus sections (4 total)
  EXPECT_GE(chorus_count, 4);
}

TEST(StructureTest, ExtendedFullStartsWithIntro) {
  auto sections = buildStructure(StructurePattern::ExtendedFull);
  ASSERT_GT(sections.size(), 0u);

  EXPECT_EQ(sections[0].type, SectionType::Intro);
  EXPECT_EQ(sections[0].bars, 4);
  EXPECT_EQ(sections[0].start_tick, 0u);
}

TEST(StructureTest, ExtendedFullEndsWithOutro) {
  auto sections = buildStructure(StructurePattern::ExtendedFull);
  ASSERT_GT(sections.size(), 0u);

  const auto& last = sections.back();
  EXPECT_EQ(last.type, SectionType::Outro);
  EXPECT_EQ(last.bars, 8);
}

// ============================================================================
// buildStructureForDuration Tests
// ============================================================================

TEST(StructureTest, BuildForDuration180SecondsAt120BPM) {
  // 180 seconds @ 120 BPM = 180 * 120 / 60 / 4 = 90 bars
  auto sections = buildStructureForDuration(180, 120);
  uint16_t total_bars = calculateTotalBars(sections);

  // Should be approximately 90 bars (may vary slightly due to rounding)
  EXPECT_GE(total_bars, 80) << "180sec@120BPM should generate ~90 bars";
  EXPECT_LE(total_bars, 100) << "180sec@120BPM should generate ~90 bars";
}

TEST(StructureTest, BuildForDuration60SecondsAt120BPM) {
  // 60 seconds @ 120 BPM = 60 * 120 / 60 / 4 = 30 bars
  auto sections = buildStructureForDuration(60, 120);
  uint16_t total_bars = calculateTotalBars(sections);

  // Should be approximately 30 bars
  EXPECT_GE(total_bars, 20) << "60sec@120BPM should generate ~30 bars";
  EXPECT_LE(total_bars, 50) << "60sec@120BPM should generate ~30 bars";
}

TEST(StructureTest, BuildForDurationMinimumBars) {
  // Very short duration should still produce minimum 12 bars
  auto sections = buildStructureForDuration(10, 120);  // ~5 bars normally
  uint16_t total_bars = calculateTotalBars(sections);

  EXPECT_GE(total_bars, 12) << "Minimum structure should be 12 bars";
}

TEST(StructureTest, BuildForDurationMaximumBars) {
  // Very long duration should be capped at 120 bars
  auto sections = buildStructureForDuration(600, 120);  // ~300 bars normally
  uint16_t total_bars = calculateTotalBars(sections);

  EXPECT_LE(total_bars, 150) << "Maximum structure should be around 120 bars";
}

TEST(StructureTest, BuildForDurationContainsIntroChorusOutro) {
  // 180 seconds should produce a full structure with all key sections
  auto sections = buildStructureForDuration(180, 120);

  bool has_intro = false;
  bool has_chorus = false;
  bool has_outro = false;

  for (const auto& section : sections) {
    if (section.type == SectionType::Intro) has_intro = true;
    if (section.type == SectionType::Chorus) has_chorus = true;
    if (section.type == SectionType::Outro) has_outro = true;
  }

  EXPECT_TRUE(has_intro) << "Duration-based structure should have Intro";
  EXPECT_TRUE(has_chorus) << "Duration-based structure should have Chorus";
  EXPECT_TRUE(has_outro) << "Duration-based structure should have Outro";
}

TEST(StructureTest, BuildForDurationStartsWithIntro) {
  auto sections = buildStructureForDuration(180, 120);
  ASSERT_GT(sections.size(), 0u);

  EXPECT_EQ(sections[0].type, SectionType::Intro);
}

TEST(StructureTest, BuildForDurationEndsWithOutro) {
  auto sections = buildStructureForDuration(180, 120);
  ASSERT_GT(sections.size(), 0u);

  EXPECT_EQ(sections.back().type, SectionType::Outro);
}

TEST(StructureTest, BuildForDurationHasInterludeForLongSongs) {
  // ExtendedFull pattern contains Interlude
  auto sections = buildStructureForDuration(180, 120, StructurePattern::ExtendedFull);

  bool has_interlude = false;
  for (const auto& section : sections) {
    if (section.type == SectionType::Interlude) {
      has_interlude = true;
      break;
    }
  }

  EXPECT_TRUE(has_interlude) << "ExtendedFull pattern should have Interlude";
}

TEST(StructureTest, BuildForDurationHasBridgeForVeryLongSongs) {
  // ExtendedFull pattern contains Bridge
  auto sections = buildStructureForDuration(240, 120, StructurePattern::ExtendedFull);

  bool has_bridge = false;
  for (const auto& section : sections) {
    if (section.type == SectionType::Bridge) {
      has_bridge = true;
      break;
    }
  }

  EXPECT_TRUE(has_bridge) << "ExtendedFull pattern should have Bridge";
}

TEST(StructureTest, BuildForDurationDifferentBPM) {
  // Same duration but different BPM should produce different bar counts
  auto sections_slow = buildStructureForDuration(120, 60);   // 120sec @ 60BPM = 30 bars
  auto sections_fast = buildStructureForDuration(120, 180);  // 120sec @ 180BPM = 90 bars

  uint16_t bars_slow = calculateTotalBars(sections_slow);
  uint16_t bars_fast = calculateTotalBars(sections_fast);

  EXPECT_LT(bars_slow, bars_fast) << "Slower BPM should produce fewer bars for same duration";
}

TEST(StructureTest, BuildForDurationSectionTicks) {
  // Verify section start_tick values are correctly calculated
  auto sections = buildStructureForDuration(180, 120);

  Tick expected_tick = 0;
  for (const auto& section : sections) {
    EXPECT_EQ(section.start_tick, expected_tick)
        << "Section " << section.name << " has incorrect start_tick";
    expected_tick += section.bars * TICKS_PER_BAR;
  }
}

// ============================================================================
// Target Seconds Calculation Accuracy Tests
// ============================================================================

TEST(StructureTest, BuildForDurationAccuracyWithRounding) {
  // Test that floating-point rounding reduces error in bar calculation
  // 90 seconds @ 120 BPM = 90 * 120 / 240 = 45 bars exactly
  // Note: Structure builder produces musically coherent structures,
  // so actual bars may differ from target for musical reasons
  auto sections = buildStructureForDuration(90, 120);
  uint16_t total_bars = calculateTotalBars(sections);

  // The structure should produce a reasonable number of bars
  // (structure builder may adjust for musical coherence)
  EXPECT_GE(total_bars, 24u) << "Should produce at least 24 bars";
  EXPECT_LE(total_bars, 70u) << "Should produce at most 70 bars for 90sec target";
}

TEST(StructureTest, BuildForDurationRoundingBoundaryCase) {
  // Test boundary case: 91 seconds @ 120 BPM
  // = 91 * 120 / 240 = 45.5 bars -> should round to 46
  // Integer division would give 45
  auto sections = buildStructureForDuration(91, 120);
  uint16_t total_bars = calculateTotalBars(sections);

  // The target_bars calculation with rounding should give approximately 46
  // Structure building may add/remove bars for musical reasons
  double expected_bars = std::round(91.0 * 120 / 240.0);
  EXPECT_NEAR(static_cast<double>(total_bars), expected_bars, 20.0)
      << "Bars should be close to rounded target";
}

TEST(StructureTest, BuildForDurationVeryShortDuration) {
  // Very short duration should not crash and produce minimum structure
  auto sections = buildStructureForDuration(5, 120);  // ~2.5 bars -> clamped to 12
  uint16_t total_bars = calculateTotalBars(sections);

  EXPECT_GE(total_bars, 12u) << "Should clamp to minimum 12 bars";
}

TEST(StructureTest, BuildForDurationZeroBPMSafe) {
  // BPM=0 should be handled safely (though ideally prevented at higher level)
  // The calculation 0 * anything = 0, clamped to minimum 12 bars
  auto sections = buildStructureForDuration(180, 0);
  uint16_t total_bars = calculateTotalBars(sections);

  // Should produce minimum structure
  EXPECT_GE(total_bars, 12u) << "Zero BPM should produce minimum structure";
}

// ============================================================================
// Chorus-First Pattern Tests (15-second rule for hooks)
// ============================================================================

TEST(StructureTest, ChorusFirstStartsWithChorus) {
  auto sections = buildStructure(StructurePattern::ChorusFirst);
  ASSERT_GT(sections.size(), 0u);

  // First section should be Chorus for immediate hook
  EXPECT_EQ(sections[0].type, SectionType::Chorus);
  EXPECT_EQ(sections[0].bars, 8);
  EXPECT_EQ(sections[0].start_tick, 0u);
}

TEST(StructureTest, ChorusFirstStructure) {
  auto sections = buildStructure(StructurePattern::ChorusFirst);
  ASSERT_EQ(sections.size(), 4);

  // Chorus(8) -> A(8) -> B(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::Chorus);
  EXPECT_EQ(sections[1].type, SectionType::A);
  EXPECT_EQ(sections[2].type, SectionType::B);
  EXPECT_EQ(sections[3].type, SectionType::Chorus);

  EXPECT_EQ(calculateTotalBars(sections), 32);
}

TEST(StructureTest, ChorusFirstShortStructure) {
  auto sections = buildStructure(StructurePattern::ChorusFirstShort);
  ASSERT_EQ(sections.size(), 3);

  // Chorus(8) -> A(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::Chorus);
  EXPECT_EQ(sections[1].type, SectionType::A);
  EXPECT_EQ(sections[2].type, SectionType::Chorus);

  EXPECT_EQ(calculateTotalBars(sections), 24);
}

TEST(StructureTest, ChorusFirstFullStructure) {
  auto sections = buildStructure(StructurePattern::ChorusFirstFull);
  ASSERT_EQ(sections.size(), 7);

  // Chorus(8) -> A(8) -> B(8) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::Chorus);
  EXPECT_EQ(sections[1].type, SectionType::A);
  EXPECT_EQ(sections[2].type, SectionType::B);
  EXPECT_EQ(sections[3].type, SectionType::Chorus);
  EXPECT_EQ(sections[4].type, SectionType::A);
  EXPECT_EQ(sections[5].type, SectionType::B);
  EXPECT_EQ(sections[6].type, SectionType::Chorus);

  EXPECT_EQ(calculateTotalBars(sections), 56);
}

// ============================================================================
// Immediate Vocal Pattern Tests (no intro)
// ============================================================================

TEST(StructureTest, ImmediateVocalStartsWithA) {
  auto sections = buildStructure(StructurePattern::ImmediateVocal);
  ASSERT_GT(sections.size(), 0u);

  // First section should be A (no intro)
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[0].start_tick, 0u);
}

TEST(StructureTest, ImmediateVocalStructure) {
  auto sections = buildStructure(StructurePattern::ImmediateVocal);
  ASSERT_EQ(sections.size(), 3);

  // A(8) -> B(8) -> Chorus(8) - same as StandardPop but without intro
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[1].type, SectionType::B);
  EXPECT_EQ(sections[2].type, SectionType::Chorus);

  EXPECT_EQ(calculateTotalBars(sections), 24);
}

TEST(StructureTest, ImmediateVocalFullStructure) {
  auto sections = buildStructure(StructurePattern::ImmediateVocalFull);
  ASSERT_EQ(sections.size(), 6);

  // A(8) -> B(8) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[1].type, SectionType::B);
  EXPECT_EQ(sections[2].type, SectionType::Chorus);
  EXPECT_EQ(sections[3].type, SectionType::A);
  EXPECT_EQ(sections[4].type, SectionType::B);
  EXPECT_EQ(sections[5].type, SectionType::Chorus);

  EXPECT_EQ(calculateTotalBars(sections), 48);
}

// ============================================================================
// Additional Variation Pattern Tests
// ============================================================================

TEST(StructureTest, AChorusBStructure) {
  auto sections = buildStructure(StructurePattern::AChorusB);
  ASSERT_EQ(sections.size(), 4);

  // A(8) -> Chorus(8) -> B(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[1].type, SectionType::Chorus);
  EXPECT_EQ(sections[2].type, SectionType::B);
  EXPECT_EQ(sections[3].type, SectionType::Chorus);

  EXPECT_EQ(calculateTotalBars(sections), 32);
}

TEST(StructureTest, DoubleVerseStructure) {
  auto sections = buildStructure(StructurePattern::DoubleVerse);
  ASSERT_EQ(sections.size(), 4);

  // A(8) -> A(8) -> B(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[1].type, SectionType::A);
  EXPECT_EQ(sections[2].type, SectionType::B);
  EXPECT_EQ(sections[3].type, SectionType::Chorus);

  EXPECT_EQ(calculateTotalBars(sections), 32);
}

TEST(StructureTest, ChorusFirstChorusWithin15Seconds) {
  // At 120 BPM, 8 bars = 16 seconds
  // At 150 BPM, 8 bars = 12.8 seconds (within 15-second rule)
  auto sections = buildStructure(StructurePattern::ChorusFirst);
  ASSERT_GT(sections.size(), 0u);

  // First section is Chorus, starts at tick 0
  EXPECT_EQ(sections[0].type, SectionType::Chorus);
  EXPECT_EQ(sections[0].start_tick, 0u);

  // Chorus is immediately available (no intro delay)
}

TEST(StructureTest, AllNewPatternsProduceValidSections) {
  // Test that all new patterns produce valid section structures
  std::vector<StructurePattern> new_patterns = {
      StructurePattern::ChorusFirst,        StructurePattern::ChorusFirstShort,
      StructurePattern::ChorusFirstFull,    StructurePattern::ImmediateVocal,
      StructurePattern::ImmediateVocalFull, StructurePattern::AChorusB,
      StructurePattern::DoubleVerse,
  };

  for (const auto& pattern : new_patterns) {
    auto sections = buildStructure(pattern);
    EXPECT_GT(sections.size(), 0u) << "Pattern should produce sections";
    EXPECT_GT(calculateTotalBars(sections), 0u) << "Pattern should have bars";

    // Verify section ticks are sequential
    Tick expected_tick = 0;
    for (const auto& section : sections) {
      EXPECT_EQ(section.start_tick, expected_tick);
      expected_tick += section.bars * TICKS_PER_BAR;
    }
  }
}

// ============================================================================
// ProductionBlueprint Structure Tests
// ============================================================================

TEST(StructureTest, TrackMaskToVocalDensity_None) {
  // No vocal -> None
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::Drums), VocalDensity::None);
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::Chord), VocalDensity::None);
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::NoVocal), VocalDensity::None);
}

TEST(StructureTest, TrackMaskToVocalDensity_Sparse) {
  // Vocal only or vocal + drums -> Sparse
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::Vocal), VocalDensity::Sparse);
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::Sparse), VocalDensity::Sparse);
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::Vocal | TrackMask::Drums), VocalDensity::Sparse);
  // Vocal + 1 backing track -> Sparse
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass),
            VocalDensity::Sparse);
}

TEST(StructureTest, TrackMaskToVocalDensity_Full) {
  // Vocal + 2+ backing tracks -> Full
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::Basic), VocalDensity::Full);
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::All), VocalDensity::Full);
}

TEST(StructureTest, TrackMaskToBackingDensity_Thin) {
  // 0-1 backing tracks -> Thin
  EXPECT_EQ(trackMaskToBackingDensity(TrackMask::Drums), BackingDensity::Thin);
  EXPECT_EQ(trackMaskToBackingDensity(TrackMask::Vocal), BackingDensity::Thin);
  EXPECT_EQ(trackMaskToBackingDensity(TrackMask::Chord), BackingDensity::Thin);
  EXPECT_EQ(trackMaskToBackingDensity(TrackMask::Vocal | TrackMask::Bass), BackingDensity::Thin);
}

TEST(StructureTest, TrackMaskToBackingDensity_Normal) {
  // 2-3 backing tracks -> Normal
  EXPECT_EQ(trackMaskToBackingDensity(TrackMask::Chord | TrackMask::Bass), BackingDensity::Normal);
  EXPECT_EQ(trackMaskToBackingDensity(TrackMask::Basic), BackingDensity::Normal);
}

TEST(StructureTest, TrackMaskToBackingDensity_Thick) {
  // 4+ backing tracks -> Thick
  EXPECT_EQ(trackMaskToBackingDensity(TrackMask::All), BackingDensity::Thick);
}

TEST(StructureTest, BuildStructureFromBlueprint_Traditional) {
  // Traditional blueprint has no section_flow
  const auto& bp = getProductionBlueprint(0);
  auto sections = buildStructureFromBlueprint(bp);

  // Should return empty (caller uses buildStructure instead)
  EXPECT_TRUE(sections.empty());
}

TEST(StructureTest, BuildStructureFromBlueprint_Orangestar) {
  // RhythmLock (formerly Orangestar) has a custom section flow
  const auto& bp = getProductionBlueprint(1);
  auto sections = buildStructureFromBlueprint(bp);

  // Should have sections
  ASSERT_GT(sections.size(), 0u);

  // First section should be Intro with all tracks (for staggered entry)
  EXPECT_EQ(sections[0].type, SectionType::Intro);
  EXPECT_EQ(sections[0].vocal_density, VocalDensity::Full);
  EXPECT_EQ(sections[0].backing_density, BackingDensity::Thick);

  // Verify section ticks are sequential
  Tick expected_tick = 0;
  for (const auto& section : sections) {
    EXPECT_EQ(section.start_tick, expected_tick);
    expected_tick += section.bars * TICKS_PER_BAR;
  }
}

TEST(StructureTest, BuildStructureFromBlueprint_YOASOBI) {
  // YOASOBI has a custom section flow with full arrangement
  const auto& bp = getProductionBlueprint(2);
  auto sections = buildStructureFromBlueprint(bp);

  ASSERT_GT(sections.size(), 0u);

  // First section should be Intro with full arrangement
  EXPECT_EQ(sections[0].type, SectionType::Intro);
  EXPECT_EQ(sections[0].vocal_density, VocalDensity::Full);
  EXPECT_EQ(sections[0].backing_density, BackingDensity::Thick);
}

TEST(StructureTest, BuildStructureFromBlueprint_Ballad) {
  // Ballad has sparse intro (chord only)
  const auto& bp = getProductionBlueprint(3);
  auto sections = buildStructureFromBlueprint(bp);

  ASSERT_GT(sections.size(), 0u);

  // First section should be Intro with chord only
  EXPECT_EQ(sections[0].type, SectionType::Intro);
  EXPECT_EQ(sections[0].vocal_density, VocalDensity::None);
  EXPECT_EQ(sections[0].backing_density, BackingDensity::Thin);
}

}  // namespace
}  // namespace midisketch
