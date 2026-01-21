/**
 * @file section_utils_test.cpp
 * @brief Tests for section utilities.
 */

#include "core/section_utils.h"

#include <gtest/gtest.h>

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
// findFirstSection Tests
// ============================================================================

TEST(SectionUtilsTest, FindFirstSectionFound) {
  std::vector<Section> sections = {
      makeSection(SectionType::Intro, 4, 0),
      makeSection(SectionType::A, 8, 4 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 12 * TICKS_PER_BAR),
      makeSection(SectionType::A, 8, 20 * TICKS_PER_BAR),
  };

  auto result = findFirstSection(sections, SectionType::A);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->type, SectionType::A);
  EXPECT_EQ(result->start_tick, 4u * TICKS_PER_BAR);
}

TEST(SectionUtilsTest, FindFirstSectionNotFound) {
  std::vector<Section> sections = {
      makeSection(SectionType::Intro, 4, 0),
      makeSection(SectionType::A, 8, 4 * TICKS_PER_BAR),
  };

  auto result = findFirstSection(sections, SectionType::Bridge);

  EXPECT_FALSE(result.has_value());
}

TEST(SectionUtilsTest, FindFirstSectionEmpty) {
  std::vector<Section> sections;

  auto result = findFirstSection(sections, SectionType::Chorus);

  EXPECT_FALSE(result.has_value());
}

// ============================================================================
// findLastSection Tests
// ============================================================================

TEST(SectionUtilsTest, FindLastSectionFound) {
  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),
      makeSection(SectionType::A, 8, 8 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 16 * TICKS_PER_BAR),
      makeSection(SectionType::Outro, 4, 24 * TICKS_PER_BAR),
  };

  auto result = findLastSection(sections, SectionType::Chorus);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->type, SectionType::Chorus);
  EXPECT_EQ(result->start_tick, 16u * TICKS_PER_BAR);
}

TEST(SectionUtilsTest, FindLastSectionNotFound) {
  std::vector<Section> sections = {
      makeSection(SectionType::Intro, 4, 0),
      makeSection(SectionType::A, 8, 4 * TICKS_PER_BAR),
  };

  auto result = findLastSection(sections, SectionType::Bridge);

  EXPECT_FALSE(result.has_value());
}

TEST(SectionUtilsTest, FindLastSectionSingle) {
  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),
  };

  auto result = findLastSection(sections, SectionType::Chorus);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->start_tick, 0u);
}

// ============================================================================
// findNthSection Tests
// ============================================================================

TEST(SectionUtilsTest, FindNthSectionFirst) {
  std::vector<Section> sections = {
      makeSection(SectionType::A, 8, 0),
      makeSection(SectionType::Chorus, 8, 8 * TICKS_PER_BAR),
      makeSection(SectionType::A, 8, 16 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 24 * TICKS_PER_BAR),
  };

  auto result = findNthSection(sections, SectionType::Chorus, 1);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->start_tick, 8u * TICKS_PER_BAR);
}

TEST(SectionUtilsTest, FindNthSectionSecond) {
  std::vector<Section> sections = {
      makeSection(SectionType::A, 8, 0),
      makeSection(SectionType::Chorus, 8, 8 * TICKS_PER_BAR),
      makeSection(SectionType::A, 8, 16 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 24 * TICKS_PER_BAR),
  };

  auto result = findNthSection(sections, SectionType::Chorus, 2);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->start_tick, 24u * TICKS_PER_BAR);
}

TEST(SectionUtilsTest, FindNthSectionZeroReturnsNullopt) {
  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),
  };

  auto result = findNthSection(sections, SectionType::Chorus, 0);

  EXPECT_FALSE(result.has_value());
}

TEST(SectionUtilsTest, FindNthSectionBeyondCount) {
  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),
      makeSection(SectionType::Chorus, 8, 8 * TICKS_PER_BAR),
  };

  auto result = findNthSection(sections, SectionType::Chorus, 3);

  EXPECT_FALSE(result.has_value());
}

// ============================================================================
// findAllSections Tests
// ============================================================================

TEST(SectionUtilsTest, FindAllSectionsMultiple) {
  std::vector<Section> sections = {
      makeSection(SectionType::Intro, 4, 0),
      makeSection(SectionType::A, 8, 4 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 12 * TICKS_PER_BAR),
      makeSection(SectionType::A, 8, 20 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 28 * TICKS_PER_BAR),
  };

  auto result = findAllSections(sections, SectionType::A);

  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].start_tick, 4u * TICKS_PER_BAR);
  EXPECT_EQ(result[1].start_tick, 20u * TICKS_PER_BAR);
}

TEST(SectionUtilsTest, FindAllSectionsNone) {
  std::vector<Section> sections = {
      makeSection(SectionType::Intro, 4, 0),
      makeSection(SectionType::Chorus, 8, 4 * TICKS_PER_BAR),
  };

  auto result = findAllSections(sections, SectionType::Bridge);

  EXPECT_TRUE(result.empty());
}

TEST(SectionUtilsTest, FindAllSectionsEmpty) {
  std::vector<Section> sections;

  auto result = findAllSections(sections, SectionType::Chorus);

  EXPECT_TRUE(result.empty());
}

// ============================================================================
// findAllSectionTicks Tests
// ============================================================================

TEST(SectionUtilsTest, FindAllSectionTicksMultiple) {
  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),
      makeSection(SectionType::A, 8, 8 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 16 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 24 * TICKS_PER_BAR),
  };

  auto result = findAllSectionTicks(sections, SectionType::Chorus);

  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0], 0u);
  EXPECT_EQ(result[1], 16u * TICKS_PER_BAR);
  EXPECT_EQ(result[2], 24u * TICKS_PER_BAR);
}

TEST(SectionUtilsTest, FindAllSectionTicksNone) {
  std::vector<Section> sections = {
      makeSection(SectionType::Intro, 4, 0),
  };

  auto result = findAllSectionTicks(sections, SectionType::Chorus);

  EXPECT_TRUE(result.empty());
}

// ============================================================================
// findSectionAfter Tests
// ============================================================================

TEST(SectionUtilsTest, FindSectionAfterFound) {
  std::vector<Section> sections = {
      makeSection(SectionType::A, 8, 0), makeSection(SectionType::B, 8, 8 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 16 * TICKS_PER_BAR),  // After B
  };

  auto result = findSectionAfter(sections, SectionType::Chorus, {SectionType::B});

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->start_tick, 16u * TICKS_PER_BAR);
}

TEST(SectionUtilsTest, FindSectionAfterMultiplePrecedingTypes) {
  std::vector<Section> sections = {
      makeSection(SectionType::Bridge, 8, 0),
      makeSection(SectionType::Chorus, 8, 8 * TICKS_PER_BAR),  // After Bridge
  };

  auto result = findSectionAfter(sections, SectionType::Chorus,
                                 {SectionType::B, SectionType::Bridge, SectionType::Interlude});

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->start_tick, 8u * TICKS_PER_BAR);
}

TEST(SectionUtilsTest, FindSectionAfterWrongPreceding) {
  std::vector<Section> sections = {
      makeSection(SectionType::A, 8, 0),
      makeSection(SectionType::Chorus, 8, 8 * TICKS_PER_BAR),  // After A, not B
  };

  auto result = findSectionAfter(sections, SectionType::Chorus, {SectionType::B});

  EXPECT_FALSE(result.has_value());
}

TEST(SectionUtilsTest, FindSectionAfterFirstPosition) {
  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),  // First position, no preceding
      makeSection(SectionType::B, 8, 8 * TICKS_PER_BAR),
  };

  auto result = findSectionAfter(sections, SectionType::Chorus, {SectionType::Intro});

  EXPECT_FALSE(result.has_value());  // Chorus is at index 0, no preceding section
}

// ============================================================================
// findLastSectionAfter Tests
// ============================================================================

TEST(SectionUtilsTest, FindLastSectionAfterFound) {
  std::vector<Section> sections = {
      makeSection(SectionType::B, 8, 0),
      makeSection(SectionType::Chorus, 8, 8 * TICKS_PER_BAR),  // After B
      makeSection(SectionType::A, 8, 16 * TICKS_PER_BAR),
      makeSection(SectionType::B, 8, 24 * TICKS_PER_BAR),
      makeSection(SectionType::Chorus, 8, 32 * TICKS_PER_BAR),  // After B (last)
  };

  auto result = findLastSectionAfter(sections, SectionType::Chorus, {SectionType::B});

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->start_tick, 32u * TICKS_PER_BAR);
}

TEST(SectionUtilsTest, FindLastSectionAfterNotFound) {
  std::vector<Section> sections = {
      makeSection(SectionType::A, 8, 0),
      makeSection(SectionType::Chorus, 8, 8 * TICKS_PER_BAR),  // After A, not Bridge
  };

  auto result = findLastSectionAfter(sections, SectionType::Chorus, {SectionType::Bridge});

  EXPECT_FALSE(result.has_value());
}

TEST(SectionUtilsTest, FindLastSectionAfterSingleSection) {
  std::vector<Section> sections = {
      makeSection(SectionType::Chorus, 8, 0),
  };

  auto result = findLastSectionAfter(sections, SectionType::Chorus, {SectionType::B});

  EXPECT_FALSE(result.has_value());  // Single section, no preceding
}

}  // namespace
}  // namespace midisketch
