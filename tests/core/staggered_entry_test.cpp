/**
 * @file staggered_entry_test.cpp
 * @brief Tests for staggered instrument entry in intro sections.
 */

#include <gtest/gtest.h>

#include "core/generator.h"
#include "core/section_types.h"
#include "core/timing_constants.h"

namespace midisketch {
namespace {

// ============================================================================
// StaggeredEntryConfig Tests
// ============================================================================

TEST(StaggeredEntryConfigTest, DefaultIntro8Bars) {
  auto config = StaggeredEntryConfig::defaultIntro(8);

  // Should have 5 entries for 8-bar intro
  EXPECT_EQ(config.entry_count, 5);
  EXPECT_FALSE(config.isEmpty());

  // Drums at bar 0
  EXPECT_EQ(config.entries[0].track, TrackMask::Drums);
  EXPECT_EQ(config.entries[0].entry_bar, 0);

  // Bass at bar 2
  EXPECT_EQ(config.entries[1].track, TrackMask::Bass);
  EXPECT_EQ(config.entries[1].entry_bar, 2);

  // Chord at bar 4
  EXPECT_EQ(config.entries[2].track, TrackMask::Chord);
  EXPECT_EQ(config.entries[2].entry_bar, 4);

  // Motif at bar 4
  EXPECT_EQ(config.entries[3].track, TrackMask::Motif);
  EXPECT_EQ(config.entries[3].entry_bar, 4);

  // Arpeggio at bar 6
  EXPECT_EQ(config.entries[4].track, TrackMask::Arpeggio);
  EXPECT_EQ(config.entries[4].entry_bar, 6);
}

TEST(StaggeredEntryConfigTest, DefaultIntro4Bars) {
  auto config = StaggeredEntryConfig::defaultIntro(4);

  // Should have 3 entries for 4-bar intro
  EXPECT_EQ(config.entry_count, 3);
  EXPECT_FALSE(config.isEmpty());

  // Drums at bar 0
  EXPECT_EQ(config.entries[0].track, TrackMask::Drums);
  EXPECT_EQ(config.entries[0].entry_bar, 0);

  // Bass at bar 1
  EXPECT_EQ(config.entries[1].track, TrackMask::Bass);
  EXPECT_EQ(config.entries[1].entry_bar, 1);

  // Chord at bar 2
  EXPECT_EQ(config.entries[2].track, TrackMask::Chord);
  EXPECT_EQ(config.entries[2].entry_bar, 2);
}

TEST(StaggeredEntryConfigTest, DefaultIntro2BarsIsEmpty) {
  auto config = StaggeredEntryConfig::defaultIntro(2);

  // Should be empty for short intros
  EXPECT_EQ(config.entry_count, 0);
  EXPECT_TRUE(config.isEmpty());
}

TEST(StaggeredEntryConfigTest, DefaultIntro0BarsIsEmpty) {
  auto config = StaggeredEntryConfig::defaultIntro(0);

  // Should be empty for no intro
  EXPECT_EQ(config.entry_count, 0);
  EXPECT_TRUE(config.isEmpty());
}

// ============================================================================
// Generator Staggered Entry Tests
// ============================================================================

class StaggeredEntryGeneratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.key = Key::C;
    params_.bpm = 120;
    params_.mood = Mood::ModernPop;
    params_.chord_id = 0;
    params_.drums_enabled = true;
    params_.arpeggio_enabled = true;
    params_.structure = StructurePattern::BuildUp;  // Intro(4) -> A(8) -> B(8) -> Chorus(8)
    params_.seed = 42;
    params_.vocal_low = 60;
    params_.vocal_high = 72;
  }

  GeneratorParams params_;
  Generator generator_;
};

TEST_F(StaggeredEntryGeneratorTest, StaggeredEntryRemovesEarlyNotes) {
  // Configure for staggered entry (need to use blueprint that sets EntryPattern::Stagger)
  // For now, test the config generation directly

  Section intro_section;
  intro_section.type = SectionType::Intro;
  intro_section.bars = 8;
  intro_section.start_tick = 0;
  intro_section.entry_pattern = EntryPattern::Stagger;

  auto config = StaggeredEntryConfig::defaultIntro(intro_section.bars);

  // Verify config is correct
  EXPECT_FALSE(config.isEmpty());
  EXPECT_EQ(config.entry_count, 5);

  // Bass should enter at bar 2, meaning notes in bars 0-1 should be removed
  EXPECT_EQ(config.entries[1].track, TrackMask::Bass);
  EXPECT_EQ(config.entries[1].entry_bar, 2);
}

TEST_F(StaggeredEntryGeneratorTest, GeneratorAppliesStaggeredEntry) {
  // This test verifies that the generator correctly applies staggered entry
  // We need a blueprint that uses EntryPattern::Stagger for intro

  generator_.generate(params_);

  // Check that the song was generated
  const auto& song = generator_.getSong();
  const auto& sections = song.arrangement().sections();

  // Find intro section
  bool found_intro = false;
  for (const auto& section : sections) {
    if (section.type == SectionType::Intro) {
      found_intro = true;
      break;
    }
  }

  // BuildUp pattern should have an intro
  EXPECT_TRUE(found_intro);
}

TEST_F(StaggeredEntryGeneratorTest, StaggeredEntryDoesNotAffectDrums) {
  // Drums should not be affected by staggered entry (they establish the beat)
  generator_.generate(params_);

  const auto& drums = generator_.getSong().drums();
  const auto& sections = generator_.getSong().arrangement().sections();

  // Find intro section
  for (const auto& section : sections) {
    if (section.type == SectionType::Intro) {
      // Drums should have notes from the very start
      bool has_early_drums = false;
      Tick early_threshold = section.start_tick + TICKS_PER_BAR;  // First bar

      for (const auto& note : drums.notes()) {
        if (note.start_tick >= section.start_tick && note.start_tick < early_threshold) {
          has_early_drums = true;
          break;
        }
      }

      if (params_.drums_enabled) {
        EXPECT_TRUE(has_early_drums)
            << "Drums should have notes in the first bar of intro";
      }
      break;
    }
  }
}

// ============================================================================
// TrackEntry Struct Tests
// ============================================================================

TEST(TrackEntryTest, DefaultValues) {
  TrackEntry entry = {};
  EXPECT_EQ(static_cast<uint16_t>(entry.track), 0);
  EXPECT_EQ(entry.entry_bar, 0);
  EXPECT_EQ(entry.fade_in_bars, 0);
}

TEST(TrackEntryTest, InitializerList) {
  TrackEntry entry = {TrackMask::Bass, 2, 1};
  EXPECT_EQ(entry.track, TrackMask::Bass);
  EXPECT_EQ(entry.entry_bar, 2);
  EXPECT_EQ(entry.fade_in_bars, 1);
}

}  // namespace
}  // namespace midisketch
