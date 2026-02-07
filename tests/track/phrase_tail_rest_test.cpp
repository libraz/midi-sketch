/**
 * @file phrase_tail_rest_test.cpp
 * @brief Tests for phrase tail rest feature in Guitar, Motif, and Arpeggio.
 *
 * Verifies that phrase_tail_rest reduces note density in section tail bars.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <vector>

#include "core/basic_types.h"
#include "core/generator.h"
#include "core/section_iteration_helper.h"
#include "core/section_types.h"
#include "core/song.h"
#include "core/timing_constants.h"

using namespace midisketch;

// ============================================================================
// Helper Function Unit Tests
// ============================================================================

TEST(PhraseTailHelperTest, IsPhraseTailWith8Bars) {
  // 8-bar section: tail = bars 6 and 7
  EXPECT_FALSE(isPhraseTail(0, 8));
  EXPECT_FALSE(isPhraseTail(3, 8));
  EXPECT_FALSE(isPhraseTail(5, 8));
  EXPECT_TRUE(isPhraseTail(6, 8));
  EXPECT_TRUE(isPhraseTail(7, 8));
}

TEST(PhraseTailHelperTest, IsPhraseTailWith4Bars) {
  // 4-bar section: tail = bars 2 and 3
  EXPECT_FALSE(isPhraseTail(0, 4));
  EXPECT_FALSE(isPhraseTail(1, 4));
  EXPECT_TRUE(isPhraseTail(2, 4));
  EXPECT_TRUE(isPhraseTail(3, 4));
}

TEST(PhraseTailHelperTest, IsPhraseTailWith3Bars) {
  // 3-bar section: tail = bar 2 only (last bar)
  EXPECT_FALSE(isPhraseTail(0, 3));
  EXPECT_FALSE(isPhraseTail(1, 3));
  EXPECT_TRUE(isPhraseTail(2, 3));
}

TEST(PhraseTailHelperTest, IsPhraseTailWith2Bars) {
  // 2-bar section: no tail (too short)
  EXPECT_FALSE(isPhraseTail(0, 2));
  EXPECT_FALSE(isPhraseTail(1, 2));
}

TEST(PhraseTailHelperTest, IsPhraseTailWith1Bar) {
  // 1-bar section: no tail
  EXPECT_FALSE(isPhraseTail(0, 1));
}

TEST(PhraseTailHelperTest, IsLastBar) {
  EXPECT_TRUE(isLastBar(7, 8));
  EXPECT_FALSE(isLastBar(6, 8));
  EXPECT_TRUE(isLastBar(3, 4));
  EXPECT_FALSE(isLastBar(2, 4));
  EXPECT_TRUE(isLastBar(0, 1));
}

// ============================================================================
// Section Iteration with phrase_tail_rest
// ============================================================================

TEST(PhraseTailIterationTest, BarContextExposesPhraseTailRest) {
  // Build a simple 8-bar section with phrase_tail_rest=true
  Section section;
  section.type = SectionType::Chorus;
  section.name = "Chorus";
  section.bars = 8;
  section.start_bar = 0;
  section.start_tick = 0;
  section.track_mask = TrackMask::All;
  section.phrase_tail_rest = true;

  std::vector<Section> sections = {section};

  // Track which bars are in phrase tail
  std::vector<bool> tail_flags;
  std::vector<bool> last_flags;

  forEachSectionBar(
      sections, Mood::StraightPop, TrackMask::Guitar,
      [](const Section&, size_t, SectionType, const HarmonicRhythmInfo&) {},
      [&](const BarContext& bc) {
        tail_flags.push_back(
            bc.section.phrase_tail_rest && isPhraseTail(bc.bar_index, bc.section.bars));
        last_flags.push_back(
            bc.section.phrase_tail_rest && isLastBar(bc.bar_index, bc.section.bars));
      });

  ASSERT_EQ(tail_flags.size(), 8u);
  // Bars 0-5: not in tail
  for (int idx = 0; idx < 6; ++idx) {
    EXPECT_FALSE(tail_flags[idx]) << "Bar " << idx << " should not be in phrase tail";
  }
  // Bars 6-7: in tail
  EXPECT_TRUE(tail_flags[6]) << "Bar 6 should be in phrase tail";
  EXPECT_TRUE(tail_flags[7]) << "Bar 7 should be in phrase tail";
  // Only bar 7 is the last bar
  EXPECT_FALSE(last_flags[6]) << "Bar 6 should not be the last bar";
  EXPECT_TRUE(last_flags[7]) << "Bar 7 should be the last bar";
}

TEST(PhraseTailIterationTest, DefaultPhraseTailRestIsFalse) {
  Section section;
  section.type = SectionType::A;
  section.name = "A";
  section.bars = 8;
  section.start_bar = 0;
  section.start_tick = 0;
  section.track_mask = TrackMask::All;
  // phrase_tail_rest defaults to false

  std::vector<Section> sections = {section};

  int tail_count = 0;
  forEachSectionBar(
      sections, Mood::StraightPop, TrackMask::Guitar,
      [](const Section&, size_t, SectionType, const HarmonicRhythmInfo&) {},
      [&](const BarContext& bc) {
        if (bc.section.phrase_tail_rest && isPhraseTail(bc.bar_index, bc.section.bars)) {
          tail_count++;
        }
      });

  EXPECT_EQ(tail_count, 0) << "Default phrase_tail_rest=false should produce no tail bars";
}

// ============================================================================
// Guitar Phrase Tail Rest Integration
// ============================================================================

class PhraseTailRestGuitarTest : public ::testing::Test {
 protected:
  /// @brief Count notes per bar in a track within a tick range.
  std::map<int, int> countNotesPerBar(const MidiTrack& track, Tick section_start,
                                       uint8_t section_bars) {
    std::map<int, int> bar_counts;
    for (int bar = 0; bar < section_bars; ++bar) {
      bar_counts[bar] = 0;
    }
    Tick section_end = section_start + section_bars * TICKS_PER_BAR;
    for (const auto& note : track.notes()) {
      if (note.start_tick >= section_start && note.start_tick < section_end) {
        int bar = static_cast<int>((note.start_tick - section_start) / TICKS_PER_BAR);
        bar_counts[bar]++;
      }
    }
    return bar_counts;
  }
};

TEST_F(PhraseTailRestGuitarTest, DefaultHasConsistentDensity) {
  // Generate without phrase_tail_rest (default).
  // All bars should have similar note counts.
  GeneratorParams params;
  params.mood = Mood::LightRock;  // Uses guitar strum style
  params.seed = 42;
  params.structure = StructurePattern::StandardPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.bpm = 120;
  params.guitar_enabled = true;

  Generator gen;
  gen.generate(params);

  const auto& guitar = gen.getSong().guitar();
  const auto& sections = gen.getSong().arrangement().sections();
  ASSERT_FALSE(guitar.notes().empty());

  // Find a section with >= 4 bars that actually contains guitar notes
  const Section* target_section = nullptr;
  for (const auto& sec : sections) {
    if (sec.bars >= 4 && hasTrack(sec.track_mask, TrackMask::Guitar)) {
      // Verify this section actually has notes
      Tick sec_end = sec.start_tick + sec.bars * TICKS_PER_BAR;
      bool has_notes = false;
      for (const auto& note : guitar.notes()) {
        if (note.start_tick >= sec.start_tick && note.start_tick < sec_end) {
          has_notes = true;
          break;
        }
      }
      if (has_notes) {
        target_section = &sec;
        break;
      }
    }
  }
  ASSERT_NE(target_section, nullptr) << "Need at least one 4+ bar section with guitar notes";

  auto bar_counts = countNotesPerBar(guitar, target_section->start_tick, target_section->bars);

  // Without phrase_tail_rest, most bars should have notes
  int bars_with_notes = 0;
  for (const auto& [bar, count] : bar_counts) {
    if (count > 0) bars_with_notes++;
  }
  // At least 75% of bars should have notes
  EXPECT_GE(bars_with_notes, static_cast<int>(target_section->bars * 3 / 4))
      << "Without phrase_tail_rest, most bars should have notes ("
      << bars_with_notes << "/" << static_cast<int>(target_section->bars) << ")";
}

// ============================================================================
// isPhraseTail edge cases
// ============================================================================

TEST(PhraseTailHelperTest, IsPhraseTailWith16Bars) {
  // 16-bar section: tail = bars 14 and 15
  EXPECT_FALSE(isPhraseTail(13, 16));
  EXPECT_TRUE(isPhraseTail(14, 16));
  EXPECT_TRUE(isPhraseTail(15, 16));
}

TEST(PhraseTailHelperTest, IsPhraseTailWith6Bars) {
  // 6-bar section: tail = bars 4 and 5
  EXPECT_FALSE(isPhraseTail(3, 6));
  EXPECT_TRUE(isPhraseTail(4, 6));
  EXPECT_TRUE(isPhraseTail(5, 6));
}

TEST(PhraseTailHelperTest, IsLastBarEdgeCases) {
  EXPECT_TRUE(isLastBar(0, 1));   // Single bar section
  EXPECT_TRUE(isLastBar(1, 2));   // Two bar section
  EXPECT_TRUE(isLastBar(15, 16)); // 16-bar section
  EXPECT_FALSE(isLastBar(14, 16));
}
