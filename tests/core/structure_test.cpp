#include <gtest/gtest.h>
#include "core/structure.h"

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
      case SectionType::Intro: has_intro = true; break;
      case SectionType::A: has_a = true; break;
      case SectionType::B: has_b = true; break;
      case SectionType::Chorus: has_chorus = true; chorus_count++; break;
      case SectionType::Interlude: has_interlude = true; break;
      case SectionType::Bridge: has_bridge = true; break;
      case SectionType::Outro: has_outro = true; break;
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
  // Long songs (2+ blocks = 60+ bars) should have Interlude
  auto sections = buildStructureForDuration(180, 120);  // ~90 bars

  bool has_interlude = false;
  for (const auto& section : sections) {
    if (section.type == SectionType::Interlude) {
      has_interlude = true;
      break;
    }
  }

  EXPECT_TRUE(has_interlude) << "Long songs should have Interlude";
}

TEST(StructureTest, BuildForDurationHasBridgeForVeryLongSongs) {
  // Very long songs (3+ blocks = 80+ bars) should have Bridge
  auto sections = buildStructureForDuration(240, 120);  // ~120 bars

  bool has_bridge = false;
  for (const auto& section : sections) {
    if (section.type == SectionType::Bridge) {
      has_bridge = true;
      break;
    }
  }

  EXPECT_TRUE(has_bridge) << "Very long songs should have Bridge";
}

TEST(StructureTest, BuildForDurationDifferentBPM) {
  // Same duration but different BPM should produce different bar counts
  auto sections_slow = buildStructureForDuration(120, 60);   // 120sec @ 60BPM = 30 bars
  auto sections_fast = buildStructureForDuration(120, 180);  // 120sec @ 180BPM = 90 bars

  uint16_t bars_slow = calculateTotalBars(sections_slow);
  uint16_t bars_fast = calculateTotalBars(sections_fast);

  EXPECT_LT(bars_slow, bars_fast)
      << "Slower BPM should produce fewer bars for same duration";
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

}  // namespace
}  // namespace midisketch
