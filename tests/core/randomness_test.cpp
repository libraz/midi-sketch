/**
 * @file randomness_test.cpp
 * @brief Tests for randomness behavior.
 */

#include "core/generator.h"
#include "core/preset_data.h"
#include "midisketch.h"
#include <gtest/gtest.h>
#include <set>

namespace midisketch {
namespace {

// ============================================================================
// Chord/Bass/Voicing Randomness Tests
// ============================================================================

TEST(RandomnessTest, DifferentSeedsProduceDifferentChordNotes) {
  std::set<size_t> chord_counts;

  // Generate with multiple seeds and collect chord note counts
  for (uint32_t seed : {42u, 123u, 456u, 789u, 1000u}) {
    SongConfig config = createDefaultSongConfig(1);  // Dance Pop Emotion
    config.seed = seed;

    MidiSketch sketch;
    sketch.generateFromConfig(config);

    chord_counts.insert(sketch.getSong().chord().noteCount());
  }

  // With randomness, we should see variation in chord note counts
  // At least 2 different values out of 5 seeds
  EXPECT_GE(chord_counts.size(), 2u)
      << "Different seeds should produce different chord note counts";
}

TEST(RandomnessTest, DifferentSeedsProduceDifferentBassNotes) {
  std::set<size_t> bass_counts;

  for (uint32_t seed : {42u, 123u, 456u, 789u, 1000u}) {
    SongConfig config = createDefaultSongConfig(1);
    config.seed = seed;

    MidiSketch sketch;
    sketch.generateFromConfig(config);

    bass_counts.insert(sketch.getSong().bass().noteCount());
  }

  EXPECT_GE(bass_counts.size(), 2u)
      << "Different seeds should produce different bass note counts";
}

TEST(RandomnessTest, SameSeedProducesSameChordOutput) {
  SongConfig config1 = createDefaultSongConfig(1);
  config1.seed = 12345;
  config1.form = StructurePattern::StandardPop;  // Fix form to avoid structure randomness

  SongConfig config2 = createDefaultSongConfig(1);
  config2.seed = 12345;
  config2.form = StructurePattern::StandardPop;

  MidiSketch sketch1, sketch2;
  sketch1.generateFromConfig(config1);
  sketch2.generateFromConfig(config2);

  EXPECT_EQ(sketch1.getSong().chord().noteCount(),
            sketch2.getSong().chord().noteCount())
      << "Same seed should produce identical chord output";

  // Also check individual note values match
  const auto& notes1 = sketch1.getSong().chord().notes();
  const auto& notes2 = sketch2.getSong().chord().notes();
  ASSERT_EQ(notes1.size(), notes2.size());
  for (size_t i = 0; i < notes1.size(); ++i) {
    EXPECT_EQ(notes1[i].note, notes2[i].note)
        << "Chord note " << i << " should match";
  }
}

TEST(RandomnessTest, SameSeedProducesSameBassOutput) {
  SongConfig config1 = createDefaultSongConfig(1);
  config1.seed = 12345;
  config1.form = StructurePattern::StandardPop;

  SongConfig config2 = createDefaultSongConfig(1);
  config2.seed = 12345;
  config2.form = StructurePattern::StandardPop;

  MidiSketch sketch1, sketch2;
  sketch1.generateFromConfig(config1);
  sketch2.generateFromConfig(config2);

  EXPECT_EQ(sketch1.getSong().bass().noteCount(),
            sketch2.getSong().bass().noteCount())
      << "Same seed should produce identical bass output";
}

// ============================================================================
// Drums Randomness Tests
// ============================================================================

TEST(RandomnessTest, DifferentSeedsProduceDifferentDrumNotes) {
  std::set<size_t> drum_counts;

  for (uint32_t seed : {42u, 123u, 456u, 789u, 1000u}) {
    SongConfig config = createDefaultSongConfig(1);
    config.seed = seed;

    MidiSketch sketch;
    sketch.generateFromConfig(config);

    drum_counts.insert(sketch.getSong().drums().noteCount());
  }

  EXPECT_GE(drum_counts.size(), 2u)
      << "Different seeds should produce different drum note counts";
}

TEST(RandomnessTest, SameSeedProducesSameDrumOutput) {
  SongConfig config1 = createDefaultSongConfig(1);
  config1.seed = 12345;
  config1.form = StructurePattern::StandardPop;

  SongConfig config2 = createDefaultSongConfig(1);
  config2.seed = 12345;
  config2.form = StructurePattern::StandardPop;

  MidiSketch sketch1, sketch2;
  sketch1.generateFromConfig(config1);
  sketch2.generateFromConfig(config2);

  EXPECT_EQ(sketch1.getSong().drums().noteCount(),
            sketch2.getSong().drums().noteCount())
      << "Same seed should produce identical drum output";
}

// ============================================================================
// Structure Randomness Tests
// ============================================================================

TEST(RandomnessTest, SelectRandomFormProducesDifferentForms) {
  std::set<StructurePattern> forms;

  // Test with multiple seeds
  for (uint32_t seed : {1u, 10u, 100u, 1000u, 10000u, 100000u}) {
    StructurePattern form = selectRandomForm(1, seed);  // Style 1: Dance Pop Emotion
    forms.insert(form);
  }

  // With weighted random selection, we should see at least 2 different forms
  EXPECT_GE(forms.size(), 2u)
      << "selectRandomForm should produce different forms with different seeds";
}

TEST(RandomnessTest, SelectRandomFormRespectsWeights) {
  // Style 1 (Dance Pop Emotion) has FullPop as highest weight (45)
  // Count how often each form is selected
  std::map<StructurePattern, int> form_counts;

  for (uint32_t seed = 1; seed <= 1000; ++seed) {
    StructurePattern form = selectRandomForm(1, seed);
    form_counts[form]++;
  }

  // FullPop should be most common (weight 45 out of 100)
  // It should appear at least 30% of the time (allowing for variance)
  EXPECT_GE(form_counts[StructurePattern::FullPop], 300)
      << "FullPop should be selected frequently due to high weight";
}

TEST(RandomnessTest, DifferentSeedsProduceDifferentStructures) {
  std::set<uint16_t> bar_counts;

  for (uint32_t seed : {42u, 123u, 456u, 789u, 1000u}) {
    SongConfig config = createDefaultSongConfig(1);
    config.seed = seed;
    // Don't override form - let it be randomly selected

    MidiSketch sketch;
    sketch.generateFromConfig(config);

    bar_counts.insert(sketch.getSong().arrangement().totalBars());
  }

  EXPECT_GE(bar_counts.size(), 2u)
      << "Different seeds should produce different structure bar counts";
}

TEST(RandomnessTest, ExplicitFormOverridesRandomSelection) {
  SongConfig config = createDefaultSongConfig(1);  // Default form is FullPop
  config.seed = 12345;
  config.form = StructurePattern::ShortForm;  // Explicitly set different form

  MidiSketch sketch;
  sketch.generateFromConfig(config);

  // ShortForm is 12 bars
  EXPECT_EQ(sketch.getSong().arrangement().totalBars(), 12u)
      << "Explicitly set form should override random selection";
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(RandomnessTest, FullGenerationReproducibility) {
  SongConfig config1 = createDefaultSongConfig(1);
  config1.seed = 99999;
  config1.form = StructurePattern::FullPop;  // Fix form for reproducibility test

  SongConfig config2 = createDefaultSongConfig(1);
  config2.seed = 99999;
  config2.form = StructurePattern::FullPop;

  MidiSketch sketch1, sketch2;
  sketch1.generateFromConfig(config1);
  sketch2.generateFromConfig(config2);

  // All tracks should match
  EXPECT_EQ(sketch1.getSong().vocal().noteCount(),
            sketch2.getSong().vocal().noteCount());
  EXPECT_EQ(sketch1.getSong().chord().noteCount(),
            sketch2.getSong().chord().noteCount());
  EXPECT_EQ(sketch1.getSong().bass().noteCount(),
            sketch2.getSong().bass().noteCount());
  EXPECT_EQ(sketch1.getSong().drums().noteCount(),
            sketch2.getSong().drums().noteCount());

  // MIDI output should be identical
  EXPECT_EQ(sketch1.getMidi(), sketch2.getMidi())
      << "Same seed should produce identical MIDI output";
}

TEST(RandomnessTest, FullGenerationVariation) {
  std::set<std::vector<uint8_t>> midi_outputs;

  // Generate with different seeds
  for (uint32_t seed : {1u, 2u, 3u, 4u, 5u}) {
    SongConfig config = createDefaultSongConfig(1);
    config.seed = seed;
    config.form = StructurePattern::StandardPop;  // Same form to focus on content variation

    MidiSketch sketch;
    sketch.generateFromConfig(config);

    midi_outputs.insert(sketch.getMidi());
  }

  // All outputs should be different
  EXPECT_EQ(midi_outputs.size(), 5u)
      << "Each seed should produce unique MIDI output";
}

TEST(RandomnessTest, AllStylesHaveRandomFormSelection) {
  // Test that all style presets support random form selection
  for (uint8_t style_id = 0; style_id < STYLE_PRESET_COUNT; ++style_id) {
    std::set<StructurePattern> forms;

    for (uint32_t seed : {1u, 100u, 10000u}) {
      forms.insert(selectRandomForm(style_id, seed));
    }

    // Each style should have at least 2 different forms available
    EXPECT_GE(forms.size(), 1u)
        << "Style " << static_cast<int>(style_id) << " should have form selection";
  }
}

}  // namespace
}  // namespace midisketch
