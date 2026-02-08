/**
 * @file secondary_dominant_planner_test.cpp
 * @brief Tests for secondary dominant pre-registration.
 */

#include <gtest/gtest.h>

#include "core/chord.h"
#include "core/generator.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "core/types.h"

namespace midisketch {
namespace {

// Verify that secondary dominants are pre-registered in the harmony context
// before any track generation, so coordinate axis tracks (Motif in RhythmSync)
// see the correct chord at secondary dominant ticks.
TEST(SecondaryDominantPlannerTest, HarmonyTimelineReflectsSecondaryDominants) {
  GeneratorParams params;
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::Yoasobi;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  params.bpm = 170;
  params.seed = 12345;
  params.blueprint_id = 1;  // RhythmLock (RhythmSync + Locked)
  params.composition_style = CompositionStyle::BackgroundMotif;

  Generator gen;
  gen.generate(params);

  const auto& harmony = gen.getHarmonyContext();
  const auto& sections = gen.getSong().arrangement().sections();

  // Find any tick where isSecondaryDominantAt() returns true
  int sec_dom_count = 0;
  for (const auto& section : sections) {
    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      // Check the second half of the bar (where within-bar sec. doms are placed)
      if (harmony.isSecondaryDominantAt(bar_start + TICK_HALF)) {
        sec_dom_count++;
      }
    }
    // Check section boundary (last half-bar before Chorus)
    if (section.type == SectionType::Chorus && section.start_tick > 0) {
      Tick boundary_tick = section.start_tick - TICK_HALF;
      if (harmony.isSecondaryDominantAt(boundary_tick)) {
        sec_dom_count++;
      }
    }
  }

  // With FullPop structure and standard chord progression,
  // we should get at least one secondary dominant
  EXPECT_GT(sec_dom_count, 0)
      << "Planner should register at least one secondary dominant";
}

// Verify that Motif notes generated in RhythmSync mode have zero avoid notes
// even at secondary dominant ticks (because they are now pre-registered).
TEST(SecondaryDominantPlannerTest, MotifHasNoAvoidNotesAtSecondaryDominants) {
  GeneratorParams params;
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::Yoasobi;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  params.bpm = 170;
  params.seed = 12345;
  params.blueprint_id = 1;
  params.composition_style = CompositionStyle::BackgroundMotif;

  Generator gen;
  gen.generate(params);

  const auto& motif_notes = gen.getSong().motif().notes();
  ASSERT_GT(motif_notes.size(), 0u) << "Motif should have notes";

  const auto& harmony = gen.getHarmonyContext();
  int avoid_at_sec_dom = 0;

  for (const auto& note : motif_notes) {
    if (!harmony.isSecondaryDominantAt(note.start_tick)) continue;

    int8_t degree = harmony.getChordDegreeAt(note.start_tick);
    uint8_t chord_root = degreeToRoot(degree, Key::C);
    Chord chord = getChordNotes(degree);
    bool is_minor = (chord.intervals[1] == 3);

    if (isAvoidNoteWithContext(note.note, chord_root, is_minor, degree)) {
      avoid_at_sec_dom++;
    }
  }

  EXPECT_EQ(avoid_at_sec_dom, 0)
      << "Motif should have zero avoid notes at secondary dominant ticks. "
      << "Found " << avoid_at_sec_dom;
}

}  // namespace
}  // namespace midisketch
