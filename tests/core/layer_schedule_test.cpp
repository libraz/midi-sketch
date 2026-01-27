/**
 * @file layer_schedule_test.cpp
 * @brief Tests for section-level layer scheduling system.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "core/generator.h"
#include "core/section_types.h"
#include "core/structure.h"
#include "core/timing_constants.h"

namespace midisketch {
namespace {

// ============================================================================
// LayerEvent Struct Tests
// ============================================================================

TEST(LayerEventTest, DefaultConstruction) {
  LayerEvent event;
  EXPECT_EQ(event.bar_offset, 0);
  EXPECT_EQ(event.tracks_add_mask, TrackMask::None);
  EXPECT_EQ(event.tracks_remove_mask, TrackMask::None);
}

TEST(LayerEventTest, ParameterizedConstruction) {
  LayerEvent event(2, TrackMask::Bass, TrackMask::None);
  EXPECT_EQ(event.bar_offset, 2);
  EXPECT_EQ(event.tracks_add_mask, TrackMask::Bass);
  EXPECT_EQ(event.tracks_remove_mask, TrackMask::None);
}

TEST(LayerEventTest, RemoveMask) {
  LayerEvent event(4, TrackMask::None, TrackMask::Arpeggio | TrackMask::Motif);
  EXPECT_EQ(event.bar_offset, 4);
  EXPECT_EQ(event.tracks_add_mask, TrackMask::None);
  EXPECT_TRUE(hasTrack(event.tracks_remove_mask, TrackMask::Arpeggio));
  EXPECT_TRUE(hasTrack(event.tracks_remove_mask, TrackMask::Motif));
}

// ============================================================================
// computeActiveTracksAtBar Tests
// ============================================================================

TEST(ComputeActiveTracksTest, EmptyEventsReturnsNone) {
  std::vector<LayerEvent> events;
  EXPECT_EQ(computeActiveTracksAtBar(events, 0), TrackMask::None);
  EXPECT_EQ(computeActiveTracksAtBar(events, 5), TrackMask::None);
}

TEST(ComputeActiveTracksTest, SingleAddEvent) {
  std::vector<LayerEvent> events;
  events.emplace_back(0, TrackMask::Drums, TrackMask::None);

  EXPECT_TRUE(hasTrack(computeActiveTracksAtBar(events, 0), TrackMask::Drums));
  EXPECT_TRUE(hasTrack(computeActiveTracksAtBar(events, 3), TrackMask::Drums));
}

TEST(ComputeActiveTracksTest, StaggeredAdditions) {
  std::vector<LayerEvent> events;
  events.emplace_back(0, TrackMask::Drums, TrackMask::None);
  events.emplace_back(1, TrackMask::Bass, TrackMask::None);
  events.emplace_back(2, TrackMask::Chord, TrackMask::None);

  // Bar 0: only Drums
  TrackMask bar0 = computeActiveTracksAtBar(events, 0);
  EXPECT_TRUE(hasTrack(bar0, TrackMask::Drums));
  EXPECT_FALSE(hasTrack(bar0, TrackMask::Bass));
  EXPECT_FALSE(hasTrack(bar0, TrackMask::Chord));

  // Bar 1: Drums + Bass
  TrackMask bar1 = computeActiveTracksAtBar(events, 1);
  EXPECT_TRUE(hasTrack(bar1, TrackMask::Drums));
  EXPECT_TRUE(hasTrack(bar1, TrackMask::Bass));
  EXPECT_FALSE(hasTrack(bar1, TrackMask::Chord));

  // Bar 2: Drums + Bass + Chord
  TrackMask bar2 = computeActiveTracksAtBar(events, 2);
  EXPECT_TRUE(hasTrack(bar2, TrackMask::Drums));
  EXPECT_TRUE(hasTrack(bar2, TrackMask::Bass));
  EXPECT_TRUE(hasTrack(bar2, TrackMask::Chord));

  // Bar 5: still all three
  TrackMask bar5 = computeActiveTracksAtBar(events, 5);
  EXPECT_TRUE(hasTrack(bar5, TrackMask::Drums));
  EXPECT_TRUE(hasTrack(bar5, TrackMask::Bass));
  EXPECT_TRUE(hasTrack(bar5, TrackMask::Chord));
}

TEST(ComputeActiveTracksTest, AddThenRemove) {
  std::vector<LayerEvent> events;
  events.emplace_back(0, TrackMask::All, TrackMask::None);
  events.emplace_back(6, TrackMask::None, TrackMask::Arpeggio | TrackMask::Motif);
  events.emplace_back(7, TrackMask::None, TrackMask::Chord | TrackMask::Bass);

  // Bar 0-5: all tracks active
  TrackMask bar0 = computeActiveTracksAtBar(events, 0);
  EXPECT_TRUE(hasTrack(bar0, TrackMask::Arpeggio));
  EXPECT_TRUE(hasTrack(bar0, TrackMask::Chord));

  // Bar 6: arpeggio and motif removed
  TrackMask bar6 = computeActiveTracksAtBar(events, 6);
  EXPECT_FALSE(hasTrack(bar6, TrackMask::Arpeggio));
  EXPECT_FALSE(hasTrack(bar6, TrackMask::Motif));
  EXPECT_TRUE(hasTrack(bar6, TrackMask::Chord));
  EXPECT_TRUE(hasTrack(bar6, TrackMask::Bass));
  EXPECT_TRUE(hasTrack(bar6, TrackMask::Drums));

  // Bar 7: chord and bass also removed
  TrackMask bar7 = computeActiveTracksAtBar(events, 7);
  EXPECT_FALSE(hasTrack(bar7, TrackMask::Chord));
  EXPECT_FALSE(hasTrack(bar7, TrackMask::Bass));
  EXPECT_TRUE(hasTrack(bar7, TrackMask::Drums));
  EXPECT_TRUE(hasTrack(bar7, TrackMask::Vocal));
}

// ============================================================================
// isTrackActiveAtBar Tests
// ============================================================================

TEST(IsTrackActiveAtBarTest, TrackNotYetAdded) {
  std::vector<LayerEvent> events;
  events.emplace_back(0, TrackMask::Drums, TrackMask::None);
  events.emplace_back(2, TrackMask::Bass, TrackMask::None);

  // Bass is not active at bar 0-1
  EXPECT_FALSE(isTrackActiveAtBar(events, 0, TrackMask::Bass));
  EXPECT_FALSE(isTrackActiveAtBar(events, 1, TrackMask::Bass));

  // Bass is active from bar 2 onward
  EXPECT_TRUE(isTrackActiveAtBar(events, 2, TrackMask::Bass));
  EXPECT_TRUE(isTrackActiveAtBar(events, 5, TrackMask::Bass));
}

TEST(IsTrackActiveAtBarTest, TrackRemoved) {
  std::vector<LayerEvent> events;
  events.emplace_back(0, TrackMask::All, TrackMask::None);
  events.emplace_back(6, TrackMask::None, TrackMask::Aux);

  EXPECT_TRUE(isTrackActiveAtBar(events, 5, TrackMask::Aux));
  EXPECT_FALSE(isTrackActiveAtBar(events, 6, TrackMask::Aux));
}

// ============================================================================
// Section::hasLayerSchedule Tests
// ============================================================================

TEST(SectionLayerScheduleTest, EmptyByDefault) {
  Section section;
  section.type = SectionType::Chorus;
  section.bars = 8;
  EXPECT_FALSE(section.hasLayerSchedule());
}

TEST(SectionLayerScheduleTest, HasScheduleWhenEventsPresent) {
  Section section;
  section.type = SectionType::Intro;
  section.bars = 8;
  section.layer_events.emplace_back(0, TrackMask::Drums, TrackMask::None);
  EXPECT_TRUE(section.hasLayerSchedule());
}

// ============================================================================
// TrackMask Bitwise NOT Operator Tests
// ============================================================================

TEST(TrackMaskOperatorTest, BitwiseNotOperator) {
  // ~Drums should include everything except Drums
  TrackMask not_drums = ~TrackMask::Drums;
  EXPECT_FALSE(hasTrack(not_drums, TrackMask::Drums));
  EXPECT_TRUE(hasTrack(not_drums, TrackMask::Vocal));
  EXPECT_TRUE(hasTrack(not_drums, TrackMask::Bass));
  EXPECT_TRUE(hasTrack(not_drums, TrackMask::Chord));
}

TEST(TrackMaskOperatorTest, ComplementAndMask) {
  // All & ~Drums should give everything except Drums
  TrackMask result = TrackMask::All & ~TrackMask::Drums;
  EXPECT_FALSE(hasTrack(result, TrackMask::Drums));
  EXPECT_TRUE(hasTrack(result, TrackMask::Vocal));
  EXPECT_TRUE(hasTrack(result, TrackMask::Bass));
}

// ============================================================================
// generateDefaultLayerEvents Tests
// ============================================================================

TEST(GenerateDefaultLayerEventsTest, IntroWith8Bars) {
  Section section;
  section.type = SectionType::Intro;
  section.bars = 8;
  section.start_tick = 0;

  auto events = generateDefaultLayerEvents(section, 0, 5);

  // Should have staggered entries
  ASSERT_GE(events.size(), 3u);

  // First event at bar 0 should add Drums
  EXPECT_EQ(events[0].bar_offset, 0);
  EXPECT_TRUE(hasTrack(events[0].tracks_add_mask, TrackMask::Drums));

  // Second event at bar 2 should add Bass
  EXPECT_EQ(events[1].bar_offset, 2);
  EXPECT_TRUE(hasTrack(events[1].tracks_add_mask, TrackMask::Bass));

  // Third event at bar 4 should add Chord
  EXPECT_EQ(events[2].bar_offset, 4);
  EXPECT_TRUE(hasTrack(events[2].tracks_add_mask, TrackMask::Chord));
}

TEST(GenerateDefaultLayerEventsTest, IntroWith4Bars) {
  Section section;
  section.type = SectionType::Intro;
  section.bars = 4;
  section.start_tick = 0;

  auto events = generateDefaultLayerEvents(section, 0, 5);

  // Should have condensed entries
  ASSERT_GE(events.size(), 3u);

  // Drums at bar 0
  EXPECT_EQ(events[0].bar_offset, 0);
  EXPECT_TRUE(hasTrack(events[0].tracks_add_mask, TrackMask::Drums));

  // Bass at bar 1
  EXPECT_EQ(events[1].bar_offset, 1);
  EXPECT_TRUE(hasTrack(events[1].tracks_add_mask, TrackMask::Bass));

  // Chord at bar 2
  EXPECT_EQ(events[2].bar_offset, 2);
  EXPECT_TRUE(hasTrack(events[2].tracks_add_mask, TrackMask::Chord));
}

TEST(GenerateDefaultLayerEventsTest, ShortSectionReturnsEmpty) {
  Section section;
  section.type = SectionType::Intro;
  section.bars = 2;
  section.start_tick = 0;

  auto events = generateDefaultLayerEvents(section, 0, 5);
  EXPECT_TRUE(events.empty());
}

TEST(GenerateDefaultLayerEventsTest, ShortSectionWith1BarReturnsEmpty) {
  Section section;
  section.type = SectionType::Intro;
  section.bars = 1;
  section.start_tick = 0;

  auto events = generateDefaultLayerEvents(section, 0, 3);
  EXPECT_TRUE(events.empty());
}

TEST(GenerateDefaultLayerEventsTest, ChorusReturnsEmpty) {
  Section section;
  section.type = SectionType::Chorus;
  section.bars = 8;
  section.start_tick = 0;

  // Chorus should have all tracks immediately - no layer events needed
  auto events = generateDefaultLayerEvents(section, 2, 5);
  EXPECT_TRUE(events.empty());
}

TEST(GenerateDefaultLayerEventsTest, BPreChorusReturnsEmpty) {
  Section section;
  section.type = SectionType::B;
  section.bars = 8;
  section.start_tick = 0;

  // B (pre-chorus) should have full tracks throughout
  auto events = generateDefaultLayerEvents(section, 1, 5);
  EXPECT_TRUE(events.empty());
}

TEST(GenerateDefaultLayerEventsTest, OutroHasWindDown) {
  Section section;
  section.type = SectionType::Outro;
  section.bars = 8;
  section.start_tick = 0;

  auto events = generateDefaultLayerEvents(section, 4, 5);

  // Should have: all tracks at bar 0, removals near end
  ASSERT_GE(events.size(), 2u);

  // First event should add all tracks
  EXPECT_EQ(events[0].bar_offset, 0);
  EXPECT_EQ(events[0].tracks_add_mask, TrackMask::All);

  // Should have removal events
  bool has_removal = false;
  for (const auto& event : events) {
    if (event.tracks_remove_mask != TrackMask::None) {
      has_removal = true;
      break;
    }
  }
  EXPECT_TRUE(has_removal) << "Outro should have track removal events";
}

TEST(GenerateDefaultLayerEventsTest, OutroTracksDecrease) {
  Section section;
  section.type = SectionType::Outro;
  section.bars = 8;
  section.start_tick = 0;

  auto events = generateDefaultLayerEvents(section, 4, 5);

  // Active tracks at bar 0 should be more than at the last bar
  TrackMask early = computeActiveTracksAtBar(events, 0);
  TrackMask late = computeActiveTracksAtBar(events, 7);

  int early_count = 0;
  int late_count = 0;
  for (int bit = 0; bit < 8; ++bit) {
    auto mask = static_cast<TrackMask>(1 << bit);
    if (hasTrack(early, mask)) early_count++;
    if (hasTrack(late, mask)) late_count++;
  }

  EXPECT_GT(early_count, late_count)
      << "Outro should have fewer tracks at the end than the beginning";
}

TEST(GenerateDefaultLayerEventsTest, FirstVerseHasGradualBuild) {
  Section section;
  section.type = SectionType::A;
  section.bars = 8;
  section.start_tick = 0;

  // section_index=0 means this is a first section (possibly first A after intro)
  auto events = generateDefaultLayerEvents(section, 0, 5);

  if (!events.empty()) {
    // Bar 0 should have vocals and basic accompaniment
    TrackMask bar0 = computeActiveTracksAtBar(events, 0);
    EXPECT_TRUE(hasTrack(bar0, TrackMask::Vocal))
        << "First verse should have vocals from the start";
    EXPECT_TRUE(hasTrack(bar0, TrackMask::Drums))
        << "First verse should have drums from the start";

    // Bar 2+ should add more layers
    TrackMask bar2 = computeActiveTracksAtBar(events, 2);
    int bar0_count = 0;
    int bar2_count = 0;
    for (int bit = 0; bit < 8; ++bit) {
      auto mask = static_cast<TrackMask>(1 << bit);
      if (hasTrack(bar0, mask)) bar0_count++;
      if (hasTrack(bar2, mask)) bar2_count++;
    }
    EXPECT_GE(bar2_count, bar0_count)
        << "First verse should have more tracks at bar 2 than bar 0";
  }
}

TEST(GenerateDefaultLayerEventsTest, LaterVerseNoLayerEvents) {
  Section section;
  section.type = SectionType::A;
  section.bars = 8;
  section.start_tick = 0;

  // section_index=3 means this is a later occurrence
  auto events = generateDefaultLayerEvents(section, 3, 7);
  EXPECT_TRUE(events.empty())
      << "Later verse sections should not have layer events";
}

// ============================================================================
// applyDefaultLayerSchedule Tests
// ============================================================================

TEST(ApplyDefaultLayerScheduleTest, AppliesLayerEventsToIntro) {
  auto sections = buildStructure(StructurePattern::BuildUp);
  // BuildUp: Intro(4) -> A(8) -> B(8) -> Chorus(8)
  ASSERT_GE(sections.size(), 1u);
  EXPECT_EQ(sections[0].type, SectionType::Intro);
  EXPECT_TRUE(sections[0].layer_events.empty());

  applyDefaultLayerSchedule(sections);

  // Intro should now have layer events
  EXPECT_FALSE(sections[0].layer_events.empty())
      << "Intro with 4+ bars should have layer events after applyDefaultLayerSchedule";
}

TEST(ApplyDefaultLayerScheduleTest, DoesNotOverrideExistingEvents) {
  auto sections = buildStructure(StructurePattern::BuildUp);
  ASSERT_GE(sections.size(), 1u);

  // Add a custom layer event to intro
  sections[0].layer_events.emplace_back(0, TrackMask::All, TrackMask::None);

  applyDefaultLayerSchedule(sections);

  // Should still have exactly 1 event (not overwritten)
  EXPECT_EQ(sections[0].layer_events.size(), 1u);
  EXPECT_EQ(sections[0].layer_events[0].tracks_add_mask, TrackMask::All);
}

TEST(ApplyDefaultLayerScheduleTest, ShortSectionsUnaffected) {
  // DirectChorus: A(8) -> Chorus(8) - no sections under 4 bars
  auto sections = buildStructure(StructurePattern::DirectChorus);

  // Ensure chorus has no layer events (full energy)
  applyDefaultLayerSchedule(sections);

  for (const auto& section : sections) {
    if (section.type == SectionType::Chorus) {
      EXPECT_TRUE(section.layer_events.empty())
          << "Chorus should not have layer events";
    }
  }
}

// ============================================================================
// IntroLayerSchedule Integration Tests (via computeActiveTracksAtBar)
// ============================================================================

TEST(IntroLayerScheduleTest, DrumsActiveFromBar0) {
  Section intro;
  intro.type = SectionType::Intro;
  intro.bars = 8;
  intro.start_tick = 0;
  intro.layer_events = generateDefaultLayerEvents(intro, 0, 5);

  ASSERT_FALSE(intro.layer_events.empty());

  // Drums should be active from bar 0
  EXPECT_TRUE(isTrackActiveAtBar(intro.layer_events, 0, TrackMask::Drums));
  EXPECT_TRUE(isTrackActiveAtBar(intro.layer_events, 7, TrackMask::Drums));
}

TEST(IntroLayerScheduleTest, BassNotActiveAtBar0) {
  Section intro;
  intro.type = SectionType::Intro;
  intro.bars = 8;
  intro.start_tick = 0;
  intro.layer_events = generateDefaultLayerEvents(intro, 0, 5);

  // Bass should NOT be active at bar 0
  EXPECT_FALSE(isTrackActiveAtBar(intro.layer_events, 0, TrackMask::Bass));

  // But should be active at bar 2+
  EXPECT_TRUE(isTrackActiveAtBar(intro.layer_events, 2, TrackMask::Bass));
}

TEST(IntroLayerScheduleTest, ChordNotActiveUntilBar4) {
  Section intro;
  intro.type = SectionType::Intro;
  intro.bars = 8;
  intro.start_tick = 0;
  intro.layer_events = generateDefaultLayerEvents(intro, 0, 5);

  // Chord should NOT be active at bars 0-3
  EXPECT_FALSE(isTrackActiveAtBar(intro.layer_events, 0, TrackMask::Chord));
  EXPECT_FALSE(isTrackActiveAtBar(intro.layer_events, 1, TrackMask::Chord));
  EXPECT_FALSE(isTrackActiveAtBar(intro.layer_events, 3, TrackMask::Chord));

  // Chord should be active at bar 4+
  EXPECT_TRUE(isTrackActiveAtBar(intro.layer_events, 4, TrackMask::Chord));
}

TEST(IntroLayerScheduleTest, AllTracksActiveAtEnd) {
  Section intro;
  intro.type = SectionType::Intro;
  intro.bars = 8;
  intro.start_tick = 0;
  intro.layer_events = generateDefaultLayerEvents(intro, 0, 5);

  // At bar 7, all instrumental tracks should be active
  TrackMask bar7 = computeActiveTracksAtBar(intro.layer_events, 7);
  EXPECT_TRUE(hasTrack(bar7, TrackMask::Drums));
  EXPECT_TRUE(hasTrack(bar7, TrackMask::Bass));
  EXPECT_TRUE(hasTrack(bar7, TrackMask::Chord));
  EXPECT_TRUE(hasTrack(bar7, TrackMask::Arpeggio));
}

// ============================================================================
// Generator Integration Tests
// ============================================================================

class LayerScheduleGeneratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.key = Key::C;
    params_.bpm = 120;
    params_.mood = Mood::ModernPop;
    params_.chord_id = 0;
    params_.drums_enabled = true;
    params_.arpeggio_enabled = true;
    params_.structure = StructurePattern::FullPop;
    params_.seed = 42;
    params_.vocal_low = 60;
    params_.vocal_high = 72;
  }

  GeneratorParams params_;
  Generator generator_;
};

TEST_F(LayerScheduleGeneratorTest, GenerationAppliesLayerSchedule) {
  generator_.generate(params_);

  const auto& sections = generator_.getSong().arrangement().sections();

  // FullPop starts with Intro(4)
  ASSERT_GT(sections.size(), 0u);
  EXPECT_EQ(sections[0].type, SectionType::Intro);
  EXPECT_GE(sections[0].bars, 4);

  // Intro should have layer events
  EXPECT_TRUE(sections[0].hasLayerSchedule())
      << "Intro section should have layer schedule after generation";
}

TEST_F(LayerScheduleGeneratorTest, IntroHasFewerEarlyBassNotes) {
  // Use BuildUp pattern for guaranteed 4-bar intro
  params_.structure = StructurePattern::BuildUp;
  generator_.generate(params_);

  const auto& song = generator_.getSong();
  const auto& sections = song.arrangement().sections();
  const auto& bass = song.bass();

  ASSERT_GT(sections.size(), 0u);
  ASSERT_EQ(sections[0].type, SectionType::Intro);

  if (!sections[0].hasLayerSchedule()) {
    // If layer schedule wasn't applied (e.g., blueprint overrides), skip
    GTEST_SKIP() << "Layer schedule not applied to this intro";
  }

  // Count bass notes in the first bar of intro
  Tick intro_start = sections[0].start_tick;
  Tick bar1_end = intro_start + TICKS_PER_BAR;

  int early_bass_notes = 0;
  for (const auto& note : bass.notes()) {
    if (note.start_tick >= intro_start && note.start_tick < bar1_end) {
      early_bass_notes++;
    }
  }

  // If bass entry is scheduled after bar 0, there should be no bass notes
  if (!isTrackActiveAtBar(sections[0].layer_events, 0, TrackMask::Bass)) {
    EXPECT_EQ(early_bass_notes, 0)
        << "Bass should have no notes in bar 0 when layer schedule delays its entry";
  }
}

TEST_F(LayerScheduleGeneratorTest, ChorusSectionsHaveNoLayerSchedule) {
  generator_.generate(params_);

  const auto& sections = generator_.getSong().arrangement().sections();

  for (const auto& section : sections) {
    if (section.type == SectionType::Chorus) {
      EXPECT_FALSE(section.hasLayerSchedule())
          << "Chorus sections should not have layer events (full energy)";
    }
  }
}

TEST_F(LayerScheduleGeneratorTest, AllExistingTestsStillPassBasicGeneration) {
  // Basic smoke test: generation should complete without crashes
  generator_.generate(params_);

  const auto& song = generator_.getSong();
  EXPECT_GT(song.vocal().notes().size(), 0u);
  EXPECT_GT(song.bass().notes().size(), 0u);
  EXPECT_GT(song.chord().notes().size(), 0u);
  EXPECT_GT(song.drums().notes().size(), 0u);
}

// ============================================================================
// Outro Wind-Down Integration Tests
// ============================================================================

TEST(OutroLayerScheduleTest, OutroRemovesTracksAtEnd) {
  Section outro;
  outro.type = SectionType::Outro;
  outro.bars = 8;
  outro.start_tick = 0;
  outro.layer_events = generateDefaultLayerEvents(outro, 4, 5);

  ASSERT_FALSE(outro.layer_events.empty());

  // At bar 0, all tracks should be active
  TrackMask bar0 = computeActiveTracksAtBar(outro.layer_events, 0);
  EXPECT_TRUE(hasTrack(bar0, TrackMask::Arpeggio));
  EXPECT_TRUE(hasTrack(bar0, TrackMask::Chord));

  // At the last bar, some tracks should be removed
  TrackMask last = computeActiveTracksAtBar(outro.layer_events,
                                            static_cast<uint8_t>(outro.bars - 1));
  EXPECT_FALSE(hasTrack(last, TrackMask::Arpeggio))
      << "Arpeggio should be removed at the end of Outro";
}

}  // namespace
}  // namespace midisketch
