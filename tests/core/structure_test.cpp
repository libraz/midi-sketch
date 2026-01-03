#include <gtest/gtest.h>
#include "core/structure.h"

namespace midisketch {
namespace {

TEST(StructureTest, StandardPopStructure) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  ASSERT_EQ(sections.size(), 3);

  // A(8) → B(8) → Chorus(8)
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

  // Intro(4) → A(8) → B(8) → Chorus(8)
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

  // A(8) → Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[1].type, SectionType::Chorus);
}

TEST(StructureTest, RepeatChorusStructure) {
  auto sections = buildStructure(StructurePattern::RepeatChorus);
  ASSERT_EQ(sections.size(), 4);

  // A(8) → B(8) → Chorus(8) → Chorus(8)
  EXPECT_EQ(sections[2].type, SectionType::Chorus);
  EXPECT_EQ(sections[3].type, SectionType::Chorus);
}

TEST(StructureTest, ShortFormStructure) {
  auto sections = buildStructure(StructurePattern::ShortForm);
  ASSERT_EQ(sections.size(), 2);

  // Intro(4) → Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::Intro);
  EXPECT_EQ(sections[0].bars, 4);
  EXPECT_EQ(sections[1].type, SectionType::Chorus);
  EXPECT_EQ(sections[1].bars, 8);
}

}  // namespace
}  // namespace midisketch
