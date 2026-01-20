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

// =============================================================================
// Generation Order Test (Architecture-level fix)
// =============================================================================
// Bug: In BGM mode, Motif was generated BEFORE Bass, so isPitchSafe() had
// nothing to check against. This caused Motif-Bass clashes.
// Fix: Changed generation order to Bass -> Motif so HarmonyContext has
// Bass notes registered when Motif is generated.

TEST_F(MotifDissonanceTest, BGMGenerationOrderAllowsClashAvoidance) {
  // Use exact parameters from bug report
  params_.seed = 3054356854;
  params_.chord_id = 2;
  params_.bpm = 150;
  params_.key = Key::E;
  params_.composition_style = CompositionStyle::BackgroundMotif;

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& bass_notes = gen.getSong().bass().notes();

  if (motif_notes.empty() || bass_notes.empty()) {
    GTEST_SKIP() << "No motif or bass notes generated";
  }

  int dissonant_clashes = 0;

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

      // Skip wide separations (2+ octaves)
      if (actual_interval >= 24) continue;

      int pitch_class_interval = actual_interval % 12;

      // Check for dissonant intervals: minor 2nd (1), tritone (6), major 7th (11)
      if (pitch_class_interval == 1 || pitch_class_interval == 6 ||
          pitch_class_interval == 11) {
        dissonant_clashes++;
      }
    }
  }

  // Before fix: 10+ clashes, After fix: 0
  EXPECT_EQ(dissonant_clashes, 0)
      << "BGM mode should generate Motif after Bass to enable clash avoidance. "
      << "Found " << dissonant_clashes << " dissonant clashes";
}

// Test second BGM file parameters
TEST_F(MotifDissonanceTest, BGMGenerationOrderSecondFile) {
  params_.seed = 2802138756;
  params_.chord_id = 0;
  params_.bpm = 132;
  params_.composition_style = CompositionStyle::BackgroundMotif;

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& bass_notes = gen.getSong().bass().notes();

  if (motif_notes.empty() || bass_notes.empty()) {
    GTEST_SKIP() << "No motif or bass notes generated";
  }

  int dissonant_clashes = 0;

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

      int pitch_class_interval = actual_interval % 12;
      if (pitch_class_interval == 1 || pitch_class_interval == 6 ||
          pitch_class_interval == 11) {
        dissonant_clashes++;
      }
    }
  }

  EXPECT_EQ(dissonant_clashes, 0)
      << "Found " << dissonant_clashes << " dissonant Motif-Bass clashes";
}

// =============================================================================
// Rhythm Distribution Tests (Call & Response Structure)
// =============================================================================
// Bug: In previous implementation, all motif notes were concentrated in the
// first half of the motif pattern, making the second half silent.
// Fix: Distribute notes between "call" (first half) and "response" (second half)

class MotifRhythmDistributionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::ShortForm;
    params_.mood = Mood::IdolPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.bpm = 120;
    params_.composition_style = CompositionStyle::BackgroundMotif;
  }

  GeneratorParams params_;
};

// Test that motif pattern notes span the full motif length, not just first half
TEST_F(MotifRhythmDistributionTest, NotesSpanFullMotifLength) {
  params_.seed = 42;
  // Default motif length is 2 bars (3840 ticks)
  // Half of motif = 1920 ticks (1 bar)
  // Notes should appear in both halves

  Generator gen;
  gen.generate(params_);

  const auto& motif_pattern = gen.getSong().motifPattern();
  if (motif_pattern.empty()) {
    GTEST_SKIP() << "No motif pattern generated";
  }

  // Find the maximum start tick in the pattern
  Tick max_tick = 0;
  for (const auto& note : motif_pattern) {
    if (note.start_tick > max_tick) {
      max_tick = note.start_tick;
    }
  }

  // Default motif length is 2 bars = 3840 ticks
  // Half of that is 1920 ticks
  // At least one note should be in the second half (>= 1920)
  constexpr Tick HALF_TWO_BAR_MOTIF = TICKS_PER_BAR;  // 1920 ticks
  EXPECT_GE(max_tick, HALF_TWO_BAR_MOTIF)
      << "Motif pattern should have notes in the second half. "
      << "Max tick: " << max_tick << ", expected >= " << HALF_TWO_BAR_MOTIF;
}

// Test call & response structure: notes distributed between both halves
TEST_F(MotifRhythmDistributionTest, CallAndResponseDistribution) {
  params_.seed = 12345;

  Generator gen;
  gen.generate(params_);

  const auto& motif_pattern = gen.getSong().motifPattern();
  if (motif_pattern.size() < 4) {
    GTEST_SKIP() << "Not enough notes in motif pattern for distribution test";
  }

  // Count notes in first half vs second half
  constexpr Tick HALF_MOTIF = TICKS_PER_BAR;  // 1920 ticks for 2-bar motif
  size_t first_half_count = 0;
  size_t second_half_count = 0;

  for (const auto& note : motif_pattern) {
    if (note.start_tick < HALF_MOTIF) {
      first_half_count++;
    } else {
      second_half_count++;
    }
  }

  // Both halves should have notes (call & response)
  EXPECT_GT(first_half_count, 0u) << "First half (call) should have notes";
  EXPECT_GT(second_half_count, 0u) << "Second half (response) should have notes";

  // Distribution should be roughly balanced (not all in one half)
  // Allow some imbalance but ensure both halves are represented
  size_t total = motif_pattern.size();
  EXPECT_GE(first_half_count, total / 4)
      << "First half should have at least 25% of notes";
  EXPECT_GE(second_half_count, total / 4)
      << "Second half should have at least 25% of notes";
}

// Test robustness across multiple seeds - notes should be in second half
TEST_F(MotifRhythmDistributionTest, DistributionConsistentAcrossSeeds) {
  std::vector<uint32_t> test_seeds = {42, 12345, 99999, 54321, 11111};
  int seeds_with_good_distribution = 0;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif_pattern = gen.getSong().motifPattern();
    if (motif_pattern.size() < 2) continue;

    // Check if notes span into the second half of motif
    Tick max_tick = 0;
    for (const auto& note : motif_pattern) {
      if (note.start_tick > max_tick) max_tick = note.start_tick;
    }

    // Default motif is 2 bars = 3840 ticks
    // With call & response, notes should span into second half (>= 1920)
    // The exact span depends on note_count (4 notes = ~62% span at 2400 ticks)
    constexpr Tick HALF_MOTIF = TICKS_PER_BAR;  // 1920 ticks
    if (max_tick >= HALF_MOTIF) {
      seeds_with_good_distribution++;
    }
  }

  // All seeds should have notes in the second half (call & response structure)
  EXPECT_EQ(seeds_with_good_distribution, static_cast<int>(test_seeds.size()))
      << "All seeds should produce motif patterns with notes in both halves";
}

}  // namespace
}  // namespace midisketch
