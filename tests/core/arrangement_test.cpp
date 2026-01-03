#include <gtest/gtest.h>
#include "core/arrangement.h"
#include "core/structure.h"

namespace midisketch {
namespace {

TEST(ArrangementTest, EmptyArrangement) {
  Arrangement arr;
  EXPECT_EQ(arr.sectionCount(), 0u);
  EXPECT_EQ(arr.totalBars(), 0u);
  EXPECT_EQ(arr.totalTicks(), 0u);
}

TEST(ArrangementTest, FromSections) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  Arrangement arr(sections);

  EXPECT_EQ(arr.sectionCount(), 3u);  // A, B, Chorus
  EXPECT_EQ(arr.totalBars(), 24u);    // 8 + 8 + 8
  EXPECT_EQ(arr.totalTicks(), 24u * TICKS_PER_BAR);
}

TEST(ArrangementTest, BarToTick) {
  Arrangement arr;

  EXPECT_EQ(arr.barToTick(0), 0u);
  EXPECT_EQ(arr.barToTick(1), TICKS_PER_BAR);
  EXPECT_EQ(arr.barToTick(4), 4 * TICKS_PER_BAR);
}

TEST(ArrangementTest, SectionToTickRange) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  Arrangement arr(sections);

  // First section (A) should be 0 to 8 bars
  auto [start, end] = arr.sectionToTickRange(sections[0]);
  EXPECT_EQ(start, 0u);
  EXPECT_EQ(end, 8 * TICKS_PER_BAR);
}

TEST(ArrangementTest, IterateSections) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  Arrangement arr(sections);

  int count = 0;
  arr.iterateSections([&count](const Section&) { count++; });

  EXPECT_EQ(count, 3);
}

TEST(ArrangementTest, SectionAtBar) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  Arrangement arr(sections);

  // Bar 0-7: Section A
  const Section* s = arr.sectionAtBar(0);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(s->type, SectionType::A);

  s = arr.sectionAtBar(7);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(s->type, SectionType::A);

  // Bar 8-15: Section B
  s = arr.sectionAtBar(8);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(s->type, SectionType::B);

  // Bar 16-23: Chorus
  s = arr.sectionAtBar(16);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(s->type, SectionType::Chorus);

  // Bar 24: Out of range
  s = arr.sectionAtBar(24);
  EXPECT_EQ(s, nullptr);
}

TEST(ArrangementTest, TimeInfo) {
  Arrangement arr;

  EXPECT_EQ(arr.ticksPerBeat(), 480u);
  EXPECT_EQ(arr.beatsPerBar(), 4u);
  EXPECT_EQ(arr.ticksPerBar(), 1920u);
}

}  // namespace
}  // namespace midisketch
