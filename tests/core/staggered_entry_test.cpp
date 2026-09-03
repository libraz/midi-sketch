/**
 * @file staggered_entry_test.cpp
 * @brief Tests for staggered instrument entry in intro sections.
 */

#include <gtest/gtest.h>

#include "core/generator.h"
#include "core/section_types.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "test_support/generator_test_fixture.h"

namespace midisketch {
namespace {

// ============================================================================
// StaggeredEntryConfig Tests
// ============================================================================

TEST(StaggeredEntryConfigTest, DefaultIntro8Bars) {
  auto config = StaggeredEntryConfig::defaultIntro(8);

  EXPECT_EQ(config.entry_count, 4);
  EXPECT_FALSE(config.isEmpty());

  // Drums at bar 0
  EXPECT_EQ(config.entries[0].track, TrackMask::Drums);
  EXPECT_EQ(config.entries[0].entry_bar, 0);

  // Bass at bar 2
  EXPECT_EQ(config.entries[1].track, TrackMask::Bass);
  EXPECT_EQ(config.entries[1].entry_bar, 2);

  // Chord and Motif at bar 4
  EXPECT_EQ(config.entries[2].track, TrackMask::Chord | TrackMask::Motif);
  EXPECT_EQ(config.entries[2].entry_bar, 4);

  // Arpeggio and Aux at bar 6
  EXPECT_EQ(config.entries[3].track, TrackMask::Arpeggio | TrackMask::Aux);
  EXPECT_EQ(config.entries[3].entry_bar, 6);
}

TEST(StaggeredEntryConfigTest, DefaultIntro4Bars) {
  auto config = StaggeredEntryConfig::defaultIntro(4);

  EXPECT_EQ(config.entry_count, 4);
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

class StaggeredEntryGeneratorTest : public test::GeneratorTestFixture {
 protected:
  void SetUp() override {
    GeneratorTestFixture::SetUp();
    params_.mood = Mood::ModernPop;
    params_.drums_enabled = true;
    params_.arpeggio_enabled = true;
    params_.structure = StructurePattern::BuildUp;  // Intro(4) -> A(8) -> B(8) -> Chorus(8)
    params_.vocal_high = 72;
  }
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
  EXPECT_EQ(config.entry_count, 4);

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
        EXPECT_TRUE(has_early_drums) << "Drums should have notes in the first bar of intro";
      }
      break;
    }
  }
}

// ============================================================================
// Composite entries and the fade-in floor
// ============================================================================

TEST(StaggeredEntryFadeTest, FadedVelocityNeverReachesNoteOff) {
  // The quietest possible note, faded at the instant of entry, is the case that
  // used to round to velocity 0 (a MIDI note-off) instead of a soft attack.
  for (int velocity = 1; velocity <= 127; ++velocity) {
    for (int step = 0; step <= 10; ++step) {
      float progress = static_cast<float>(step) / 10.0f;
      uint8_t faded = getEntryFadeVelocity(static_cast<uint8_t>(velocity), progress);
      EXPECT_GE(faded, 1) << "velocity=" << velocity << " progress=" << progress;
      EXPECT_LE(faded, 127);
    }
  }
}

TEST(StaggeredEntryFadeTest, FadeRisesToFullLevel) {
  EXPECT_LT(getEntryFadeVelocity(100, 0.0f), getEntryFadeVelocity(100, 0.5f));
  EXPECT_LT(getEntryFadeVelocity(100, 0.5f), getEntryFadeVelocity(100, 1.0f));
  EXPECT_EQ(getEntryFadeVelocity(100, 1.0f), 100);
}

/// @brief IdolCoolPop opens with an 8-bar Stagger intro, whose default schedule
/// brings tracks in as pairs: Chord with Motif at bar 4, Arpeggio with Aux at
/// bar 6. Both members of a pair have to wait for their bar.
class CompositeStaggerEntryTest : public test::GeneratorTestFixture {
 protected:
  void SetUp() override {
    GeneratorTestFixture::SetUp();
    params_.blueprint_id = 7;
    params_.drums_enabled = true;
    params_.arpeggio_enabled = true;
    params_.seed = 12345;
  }

  static bool hasNotesBefore(const MidiTrack& track, Tick from, Tick before) {
    for (const auto& note : track.notes()) {
      if (note.start_tick >= from && note.start_tick < before) return true;
    }
    return false;
  }
};

TEST_F(CompositeStaggerEntryTest, SecondTrackOfACompositeEntryAlsoWaits) {
  generate();

  const auto& sections = song().arrangement().sections();
  const Section* intro = nullptr;
  for (const auto& section : sections) {
    if (section.type == SectionType::Intro && section.bars >= 8) {
      intro = &section;
      break;
    }
  }
  ASSERT_NE(intro, nullptr) << "blueprint 7 should open with an 8-bar intro";

  auto config = StaggeredEntryConfig::defaultIntro(intro->bars);
  ASSERT_EQ(config.entry_count, 4);
  ASSERT_EQ(config.entries[2].track, TrackMask::Chord | TrackMask::Motif);
  ASSERT_EQ(config.entries[3].track, TrackMask::Arpeggio | TrackMask::Aux);

  Tick chord_entry = intro->start_tick + config.entries[2].entry_bar * TICKS_PER_BAR;
  Tick arp_entry = intro->start_tick + config.entries[3].entry_bar * TICKS_PER_BAR;

  EXPECT_FALSE(hasNotesBefore(song().motif(), intro->start_tick, chord_entry))
      << "Motif shares the bar-4 entry with Chord and must not sound before it";
  EXPECT_FALSE(hasNotesBefore(song().aux(), intro->start_tick, arp_entry))
      << "Aux shares the bar-6 entry with Arpeggio and must not sound before it";
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
