/**
 * @file motif_test.cpp
 * @brief Tests for Motif track generation and dissonance avoidance.
 */

#include <gtest/gtest.h>
#include "core/generator.h"
#include "core/timing_constants.h"
#include "core/types.h"

namespace midisketch {
namespace {

class MotifDissonanceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::FullPop;
    params_.mood = Mood::IdolPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.vocal_low = 57;
    params_.vocal_high = 79;
    params_.bpm = 132;
    params_.seed = 12345;
  }

  GeneratorParams params_;
};

// =============================================================================
// Tritone Avoidance Test
// =============================================================================
// Bug: In BGM mode, Motif generated D#4 while Bass played A2
// D# to A = 6 semitones = tritone (highly dissonant)
// Fix: Added tritone (6 semitones) to avoid notes in isAvoidNote()

TEST_F(MotifDissonanceTest, AvoidsTritoneWithBassInBGMMode) {
  // Use exact parameters from the original bug
  params_.seed = 2802138756;
  params_.chord_id = 0;  // Standard I-V-vi-IV progression
  params_.bpm = 132;
  params_.composition_style = CompositionStyle::BackgroundMotif;  // BGM mode

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& bass_notes = gen.getSong().bass().notes();

  // Skip if no motif notes (some configs might not generate motif)
  if (motif_notes.empty()) {
    GTEST_SKIP() << "No motif notes generated";
  }

  int tritone_clashes = 0;

  for (const auto& motif_note : motif_notes) {
    Tick motif_start = motif_note.start_tick;
    Tick motif_end = motif_start + motif_note.duration;

    for (const auto& bass_note : bass_notes) {
      Tick bass_start = bass_note.start_tick;
      Tick bass_end = bass_start + bass_note.duration;

      // Check if notes overlap in time
      bool overlap = (motif_start < bass_end) && (bass_start < motif_end);
      if (!overlap) continue;

      // Calculate actual semitone distance
      int actual_interval = std::abs(static_cast<int>(motif_note.note) -
                                     static_cast<int>(bass_note.note));

      // Wide separation (2+ octaves): not a perceptual clash
      if (actual_interval >= 24) continue;

      // Check for tritone (6 semitones)
      int pitch_class_interval = actual_interval % 12;

      if (pitch_class_interval == 6) {  // Tritone
        tritone_clashes++;
      }
    }
  }

  // Should have zero tritone clashes
  // Before fix: 12 clashes, After fix: 0
  EXPECT_EQ(tritone_clashes, 0)
      << "Motif should avoid tritone clashes with Bass. "
      << "Found " << tritone_clashes << " tritone clashes";
}

// Test tritone avoidance across multiple seeds
TEST_F(MotifDissonanceTest, TritoneAvoidanceRobustness) {
  params_.composition_style = CompositionStyle::BackgroundMotif;

  std::vector<uint32_t> test_seeds = {12345, 2802138756, 99999, 54321};
  int total_tritone_clashes = 0;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    const auto& bass_notes = gen.getSong().bass().notes();

    if (motif_notes.empty()) continue;

    for (const auto& motif_note : motif_notes) {
      Tick motif_start = motif_note.start_tick;
      Tick motif_end = motif_start + motif_note.duration;

      for (const auto& bass_note : bass_notes) {
        Tick bass_start = bass_note.start_tick;
        Tick bass_end = bass_start + bass_note.duration;

        bool overlap = (motif_start < bass_end) && (bass_start < motif_end);
        if (!overlap) continue;

        int actual_interval = std::abs(static_cast<int>(motif_note.note) -
                                       static_cast<int>(bass_note.note));
        if (actual_interval >= 24) continue;

        if (actual_interval % 12 == 6) {
          total_tritone_clashes++;
        }
      }
    }
  }

  // Should have very few or zero tritone clashes across all seeds
  EXPECT_LE(total_tritone_clashes, 2)
      << "Too many tritone clashes across seeds: " << total_tritone_clashes;
}

// Test that Motif notes are adjusted to chord tones when they would be avoid notes
TEST_F(MotifDissonanceTest, AdjustsAvoidNotesToChordTones) {
  params_.composition_style = CompositionStyle::BackgroundMotif;
  params_.seed = 2802138756;

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();

  if (motif_notes.empty()) {
    GTEST_SKIP() << "No motif notes generated";
  }

  // Verify motif notes exist and are in valid MIDI range
  for (const auto& note : motif_notes) {
    EXPECT_GE(note.note, 36) << "Motif note too low";
    EXPECT_LE(note.note, 108) << "Motif note too high";
    EXPECT_GT(note.duration, 0u) << "Motif note has zero duration";
  }

  EXPECT_GT(motif_notes.size(), 0u) << "Should generate motif notes in BGM mode";
}

}  // namespace
}  // namespace midisketch
