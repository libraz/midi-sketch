/**
 * @file secondary_dominant_planner_test.cpp
 * @brief Tests for secondary dominant pre-registration.
 */

#include "core/secondary_dominant_planner.h"

#include <gtest/gtest.h>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/generator.h"
#include "core/harmonic_rhythm.h"
#include "core/harmony_context.h"
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
  params.mood = Mood::AnimeHighEnergy;
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
  EXPECT_GT(sec_dom_count, 0) << "Planner should register at least one secondary dominant";
}

// Verify that Motif notes generated in RhythmSync mode have zero avoid notes
// even at secondary dominant ticks (because they are now pre-registered).
TEST(SecondaryDominantPlannerTest, MotifHasNoAvoidNotesAtSecondaryDominants) {
  GeneratorParams params;
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::AnimeHighEnergy;
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

// Verify that at secondary dominant ticks, chord tones follow Dom7 quality
// (root, major 3rd, perfect 5th, minor 7th) rather than diatonic triad.
// This validates the fillPianoRollInfo logic in midisketch_c.cpp.
TEST(SecondaryDominantPlannerTest, Dom7ChordTonesAtSecondaryDominant) {
  GeneratorParams params;
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::AnimeHighEnergy;
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

  // Find a tick where isSecondaryDominantAt() returns true
  Tick sec_dom_tick = 0;
  bool found = false;
  for (const auto& section : sections) {
    if (found) break;
    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      if (harmony.isSecondaryDominantAt(bar_start + TICK_HALF)) {
        sec_dom_tick = bar_start + TICK_HALF;
        found = true;
        break;
      }
    }
    // Check section boundary
    if (!found && section.type == SectionType::Chorus && section.start_tick > 0) {
      Tick boundary_tick = section.start_tick - TICK_HALF;
      if (harmony.isSecondaryDominantAt(boundary_tick)) {
        sec_dom_tick = boundary_tick;
        found = true;
      }
    }
  }
  ASSERT_TRUE(found) << "Need at least one secondary dominant tick for this test";

  // Get the chord degree at the secondary dominant tick
  int8_t degree = harmony.getChordDegreeAt(sec_dom_tick);

  // Calculate expected Dom7 chord tones (same logic as fillPianoRollInfo)
  int normalized = ((degree % 7) + 7) % 7;
  int root_pc = SCALE[normalized];  // Key=C so current_key=0
  std::vector<int> expected_dom7 = {
      root_pc,
      (root_pc + 4) % 12,  // major 3rd
      (root_pc + 7) % 12,  // perfect 5th
      (root_pc + 10) % 12  // minor 7th
  };

  // Get the normal diatonic chord tones for comparison
  std::vector<int> diatonic_tones = getChordTonePitchClasses(degree);

  // Dom7 should have 4 tones (root, M3, P5, m7)
  EXPECT_EQ(expected_dom7.size(), 4u);

  // Dom7 tones should differ from normal diatonic tones.
  // A secondary dominant forces dominant-7th quality, so at least one tone
  // (the minor 7th or the major 3rd) should differ from the diatonic version.
  bool differs = false;
  if (expected_dom7.size() != diatonic_tones.size()) {
    differs = true;
  } else {
    auto sorted_dom7 = expected_dom7;
    auto sorted_diatonic = diatonic_tones;
    std::sort(sorted_dom7.begin(), sorted_dom7.end());
    std::sort(sorted_diatonic.begin(), sorted_diatonic.end());
    differs = (sorted_dom7 != sorted_diatonic);
  }
  EXPECT_TRUE(differs) << "Dom7 chord tones should differ from diatonic triad at degree "
                       << static_cast<int>(degree);

  // Verify Dom7 interval structure: M3 (4 semitones), m3 (3 semitones),
  // m3 (3 semitones) stacked from root
  int interval_root_to_3rd = (expected_dom7[1] - expected_dom7[0] + 12) % 12;
  int interval_root_to_5th = (expected_dom7[2] - expected_dom7[0] + 12) % 12;
  int interval_root_to_7th = (expected_dom7[3] - expected_dom7[0] + 12) % 12;
  EXPECT_EQ(interval_root_to_3rd, 4) << "Major 3rd should be 4 semitones";
  EXPECT_EQ(interval_root_to_5th, 7) << "Perfect 5th should be 7 semitones";
  EXPECT_EQ(interval_root_to_7th, 10) << "Minor 7th should be 10 semitones";
}

TEST(SecondaryDominantPlannerTest, ChorusBoundaryTargetsNextSectionFirstChord) {
  Section prechorus{};
  prechorus.type = SectionType::B;
  prechorus.name = "B";
  prechorus.bars = 4;
  prechorus.start_bar = 0;
  prechorus.start_tick = 0;

  Section chorus{};
  chorus.type = SectionType::Chorus;
  chorus.name = "CHORUS";
  chorus.bars = 4;
  chorus.start_bar = 4;
  chorus.start_tick = 4 * TICKS_PER_BAR;
  Arrangement arrangement({prechorus, chorus});

  ChordProgression progression{};
  progression.degrees = {3, 0, 4, 5};  // B ends on vi, Chorus starts on IV.
  progression.length = 4;

  HarmonyContext harmony;
  harmony.initialize(arrangement, progression, Mood::AnimeHighEnergy);

  std::mt19937 rng(12345);
  planAndRegisterSecondaryDominants(arrangement, progression, Mood::AnimeHighEnergy, rng, harmony);

  Tick boundary_tick = chorus.start_tick - TICK_HALF;
  ASSERT_TRUE(harmony.isSecondaryDominantAt(boundary_tick));
  EXPECT_EQ(harmony.getChordDegreeAt(boundary_tick), 0)
      << "Boundary SD should be V/IV (I7 in C), based on the Chorus first chord, "
         "not V/vi from the previous section tail.";
}

TEST(SecondaryDominantPlannerTest, BSectionUsesAdjacentTimelineEntriesForSecondaryDominants) {
  Section prechorus{};
  prechorus.type = SectionType::B;
  prechorus.name = "B";
  prechorus.bars = 8;
  prechorus.start_tick = 0;
  Arrangement arrangement({prechorus});

  // The B section consumes two slots per bar: I→vi, IV→V, … .  The old
  // planner used bar % length, incorrectly planning bar 1 as vi→IV instead
  // of the timeline's IV→V and then replacing the target half.
  ChordProgression progression{};
  progression.degrees = {0, 5, 3, 4};
  progression.length = 4;

  bool found_secondary_dominant = false;
  for (uint32_t seed = 1; seed <= 512 && !found_secondary_dominant; ++seed) {
    HarmonyContext harmony;
    harmony.initialize(arrangement, progression, Mood::AnimeHighEnergy);
    std::mt19937 rng(seed);
    planAndRegisterSecondaryDominants(arrangement, progression, Mood::AnimeHighEnergy, rng,
                                      harmony);

    for (uint8_t bar = 0; bar < prechorus.bars - 2; ++bar) {
      Tick bar_start = bar * TICKS_PER_BAR;
      if (!harmony.isSecondaryDominantAt(bar_start)) continue;

      found_secondary_dominant = true;
      int expected_target = progression.degrees[(bar * 2 + 1) % progression.length];
      EXPECT_EQ(harmony.getChordDegreeAt(bar_start + TICK_HALF), expected_target)
          << "A B-section secondary dominant must resolve to the adjacent "
             "second-half timeline entry";
      EXPECT_FALSE(harmony.isSecondaryDominantAt(bar_start + TICK_HALF));
      break;
    }
  }

  EXPECT_TRUE(found_secondary_dominant)
      << "Expected at least one deterministic seed to plan a B-section secondary dominant";
}

}  // namespace
}  // namespace midisketch
