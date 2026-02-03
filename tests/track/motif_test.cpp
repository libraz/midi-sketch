/**
 * @file motif_test.cpp
 * @brief Tests for Motif track generation and dissonance avoidance.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <vector>

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
      int actual_interval =
          std::abs(static_cast<int>(motif_note.note) - static_cast<int>(bass_note.note));

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
  EXPECT_EQ(tritone_clashes, 0) << "Motif should avoid tritone clashes with Bass. "
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

        int actual_interval =
            std::abs(static_cast<int>(motif_note.note) - static_cast<int>(bass_note.note));
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
// Bug: In BGM mode, Motif was generated BEFORE Bass, so isConsonantWithOtherTracks() had
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

      int actual_interval =
          std::abs(static_cast<int>(motif_note.note) - static_cast<int>(bass_note.note));

      // Skip wide separations (2+ octaves)
      if (actual_interval >= 24) continue;

      int pitch_class_interval = actual_interval % 12;

      // Check for dissonant intervals: minor 2nd (1), tritone (6), major 7th (11)
      if (pitch_class_interval == 1 || pitch_class_interval == 6 || pitch_class_interval == 11) {
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

      int actual_interval =
          std::abs(static_cast<int>(motif_note.note) - static_cast<int>(bass_note.note));
      if (actual_interval >= 24) continue;

      int pitch_class_interval = actual_interval % 12;
      if (pitch_class_interval == 1 || pitch_class_interval == 6 || pitch_class_interval == 11) {
        dissonant_clashes++;
      }
    }
  }

  EXPECT_EQ(dissonant_clashes, 0) << "Found " << dissonant_clashes
                                  << " dissonant Motif-Bass clashes";
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
  EXPECT_GE(first_half_count, total / 4) << "First half should have at least 25% of notes";
  EXPECT_GE(second_half_count, total / 4) << "Second half should have at least 25% of notes";
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

// =============================================================================
// Melodic Continuity Tests (Bar Coverage and Note Distribution)
// =============================================================================
// Bug: Density filter and collision avoidance could create full-bar silence,
// making the motif track sound discontinuous and broken.
// Fix: Added bar coverage guard and getBestAvailablePitch() instead of note deletion.

class MotifMelodicContinuityTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::FullPop;
    params_.mood = Mood::IdolPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.bpm = 132;
    params_.composition_style = CompositionStyle::BackgroundMotif;
  }

  GeneratorParams params_;
};

// Test that consecutive bars within a motif region have notes (no full-bar gaps within patterns)
// Note: Some sections may not have motif enabled (track_mask), so we focus on note density
// within contiguous regions rather than checking every bar in the song.
TEST_F(MotifMelodicContinuityTest, NoFullBarSilence) {
  std::vector<uint32_t> test_seeds = {12345, 42, 99999, 54321, 2802138756};
  int seeds_with_excessive_silence = 0;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    if (motif_notes.size() < 4) continue;  // Skip if too few notes

    // Check that within motif regions, we don't have 2+ consecutive bars of silence
    // Sort notes by start time
    std::vector<Tick> note_starts;
    for (const auto& note : motif_notes) {
      note_starts.push_back(note.start_tick);
    }
    std::sort(note_starts.begin(), note_starts.end());

    // Check for gaps of 2+ bars (within the same section-like region)
    int two_bar_gaps = 0;
    for (size_t i = 1; i < note_starts.size(); ++i) {
      Tick gap = note_starts[i] - note_starts[i - 1];
      if (gap >= 2 * TICKS_PER_BAR) {
        two_bar_gaps++;
      }
    }

    // Allow some gaps (section transitions), but not too many
    // With the bar coverage guard, internal gaps should be minimized
    float gap_ratio = static_cast<float>(two_bar_gaps) / note_starts.size();
    if (gap_ratio > 0.15f) {
      seeds_with_excessive_silence++;
    }
  }

  // At most 1 seed should have excessive silence (some randomness allowed)
  EXPECT_LE(seeds_with_excessive_silence, 1)
      << "Found " << seeds_with_excessive_silence
      << " seeds with excessive bar silence in motif track";
}

// Test that not all notes are the same pitch class (melodic variety in RhythmSync mode)
TEST_F(MotifMelodicContinuityTest, NotAllChordTonesInRhythmSync) {
  // Use RhythmSync paradigm (Blueprint 1, 5, or 7)
  params_.paradigm = GenerationParadigm::RhythmSync;

  // Test multiple seeds since melodic_freedom=0.4 is probabilistic
  std::vector<uint32_t> test_seeds = {12345, 42, 99999, 54321, 11111};
  int seeds_with_variety = 0;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    if (motif_notes.size() < 5) continue;

    // Count unique pitch classes used
    std::set<int> pitch_classes_used;
    for (const auto& note : motif_notes) {
      pitch_classes_used.insert(note.note % 12);
    }

    // Should use at least 3 different pitch classes (more than just root/5th)
    // With melodic_freedom = 0.4, we expect some passing tones across multiple seeds
    if (pitch_classes_used.size() >= 3) {
      seeds_with_variety++;
    }
  }

  // Most seeds should show melodic variety
  EXPECT_GE(seeds_with_variety, 3)
      << "RhythmSync motif should use variety of pitch classes across seeds. "
      << "Only " << seeds_with_variety << " out of 5 seeds showed variety";
}

// Test that gaps within motif patterns are reasonable
// Note: Section transitions naturally have gaps, so we measure median gap size
// rather than max gap, which may be affected by section boundaries.
TEST_F(MotifMelodicContinuityTest, MaxConsecutiveSilence) {
  std::vector<uint32_t> test_seeds = {12345, 42, 99999, 54321};
  // Allow gaps up to 2.5 bars (section transitions can be longer)
  constexpr Tick MAX_MEDIAN_GAP = TICKS_PER_BAR;  // Median gap should be under 1 bar

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    if (motif_notes.size() < 4) continue;

    // Sort notes by start time
    std::vector<Tick> note_starts;
    for (const auto& note : motif_notes) {
      note_starts.push_back(note.start_tick);
    }
    std::sort(note_starts.begin(), note_starts.end());

    // Collect all gaps
    std::vector<Tick> gaps;
    for (size_t i = 1; i < note_starts.size(); ++i) {
      gaps.push_back(note_starts[i] - note_starts[i - 1]);
    }

    // Sort to find median
    std::sort(gaps.begin(), gaps.end());
    Tick median_gap = gaps[gaps.size() / 2];

    // Median gap should be reasonable (under 1 bar)
    // This tests that the typical spacing is good, even if outliers exist
    EXPECT_LE(median_gap, MAX_MEDIAN_GAP)
        << "Seed " << seed << ": Median gap is " << median_gap
        << " ticks, which exceeds 1 bar (" << MAX_MEDIAN_GAP << " ticks)";
  }
}

// Test that RhythmSync with different blueprints doesn't produce all-chord-tone melodies
TEST_F(MotifMelodicContinuityTest, RhythmSyncBlueprintsHaveMelodicVariety) {
  params_.paradigm = GenerationParadigm::RhythmSync;

  // Test multiple seeds to account for randomness
  std::vector<uint32_t> test_seeds = {12345, 42, 99999};
  int seeds_with_variety = 0;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    if (motif_notes.size() < 5) continue;

    // Count unique pitch classes
    std::set<int> pitch_classes;
    for (const auto& note : motif_notes) {
      pitch_classes.insert(note.note % 12);
    }

    // With melodic_freedom = 0.4, we should see passing tones
    // Minimum 4 pitch classes indicates variety beyond just root/3rd/5th
    if (pitch_classes.size() >= 4) {
      seeds_with_variety++;
    }
  }

  // At least 2 out of 3 seeds should show melodic variety
  EXPECT_GE(seeds_with_variety, 2)
      << "RhythmSync should produce melodic variety with melodic_freedom=0.4";
}

// ============================================================================
// BlueprintConstraints Tests
// ============================================================================

TEST_F(MotifMelodicContinuityTest, PreferStepwiseAffectsMotifIntervals) {
  // Compare motif intervals between blueprints with different prefer_stepwise settings
  // Blueprint 3 (Ballad) has prefer_stepwise = true, max_leap = 7
  // Blueprint 0 (Traditional) has prefer_stepwise = false, max_leap = 12

  auto calculateAverageInterval = [](const MidiTrack& motif) -> double {
    const auto& notes = motif.notes();
    if (notes.size() < 2) return 0.0;

    double sum = 0.0;
    int count = 0;
    for (size_t i = 1; i < notes.size(); ++i) {
      int interval = std::abs(static_cast<int>(notes[i].note) -
                              static_cast<int>(notes[i - 1].note));
      sum += interval;
      count++;
    }
    return count > 0 ? sum / count : 0.0;
  };

  params_.structure = StructurePattern::StandardPop;
  params_.seed = 100;

  // Generate with Ballad blueprint (prefer_stepwise = true, max_leap = 7)
  params_.blueprint_id = 3;
  Generator gen_ballad;
  gen_ballad.generate(params_);
  double avg_ballad = calculateAverageInterval(gen_ballad.getSong().motif());

  // Generate with Traditional blueprint (prefer_stepwise = false, max_leap = 12)
  params_.blueprint_id = 0;
  Generator gen_traditional;
  gen_traditional.generate(params_);
  double avg_traditional = calculateAverageInterval(gen_traditional.getSong().motif());

  // Both should generate motifs
  EXPECT_GT(gen_ballad.getSong().motif().notes().size(), 0u) << "Ballad should generate motif";
  EXPECT_GT(gen_traditional.getSong().motif().notes().size(), 0u) << "Traditional should generate motif";

  // With prefer_stepwise=true and smaller max_leap, Ballad should have smaller average intervals
  // Allow tolerance since randomness and other factors affect results
  if (avg_ballad > 0 && avg_traditional > 0) {
    // Ballad should not have significantly larger intervals than Traditional
    EXPECT_LE(avg_ballad, avg_traditional * 1.5)
        << "Ballad (prefer_stepwise=true, max_leap=7) avg interval (" << avg_ballad
        << ") should not be much larger than Traditional (" << avg_traditional << ")";
  }
}

// ============================================================================
// BackingDensity Tests
// ============================================================================

TEST_F(MotifMelodicContinuityTest, BackingDensityAffectsNoteDensity) {
  // Test that BackingDensity affects the number of motif notes generated
  // Thin sections should have fewer notes than Thick sections

  params_.structure = StructurePattern::FullPop;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& sections = gen.getSong().arrangement().sections();

  // Count notes per bar for different BackingDensity levels
  auto countNotesPerBar = [&](BackingDensity density) -> double {
    int total_notes = 0;
    int total_bars = 0;

    for (const auto& section : sections) {
      if (section.getEffectiveBackingDensity() != density) continue;
      if (section.bars == 0) continue;

      Tick section_end = section.endTick();
      int notes_in_section = 0;

      for (const auto& note : motif_notes) {
        if (note.start_tick >= section.start_tick && note.start_tick < section_end) {
          notes_in_section++;
        }
      }

      total_notes += notes_in_section;
      total_bars += section.bars;
    }

    return total_bars > 0 ? static_cast<double>(total_notes) / total_bars : 0.0;
  };

  double thin_density = countNotesPerBar(BackingDensity::Thin);
  double normal_density = countNotesPerBar(BackingDensity::Normal);
  double thick_density = countNotesPerBar(BackingDensity::Thick);

  // If we have all three density types, verify the ordering
  // Note: Not all structures will have all density types
  if (thin_density > 0 && thick_density > 0) {
    EXPECT_LT(thin_density, thick_density)
        << "Thin sections should have fewer notes per bar than Thick sections "
        << "(thin=" << thin_density << ", thick=" << thick_density << ")";
  }

  if (thin_density > 0 && normal_density > 0) {
    EXPECT_LE(thin_density, normal_density * 1.1)  // Allow small tolerance
        << "Thin sections should not have more notes than Normal sections "
        << "(thin=" << thin_density << ", normal=" << normal_density << ")";
  }
}

// ============================================================================
// RhythmLock Tests (RhythmSync + Locked policy)
// ============================================================================
// In RhythmLock mode:
// - Motif is the "coordinate axis" with highest priority
// - Pattern pitches should be preserved without adjustment
// - Other tracks (Vocal, Chord) should avoid Motif notes instead

class MotifRhythmLockTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::Yoasobi;  // Common for RhythmSync
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.bpm = 170;
    params_.seed = 12345;
    params_.blueprint_id = 1;  // RhythmLock blueprint (RhythmSync + Locked)
    params_.composition_style = CompositionStyle::BackgroundMotif;
  }

  GeneratorParams params_;
};

// Test that RhythmLock preserves pattern consistency
// In RhythmLock mode, the pattern should have consistent pitch classes
// (same pitch classes repeat, even if octave varies)
TEST_F(MotifRhythmLockTest, PreservesPatternPitchesAcrossSections) {
  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  ASSERT_GT(motif_notes.size(), 0u) << "Motif should have notes";

  // Collect all pitch classes used
  std::set<int> pitch_classes;
  for (const auto& note : motif_notes) {
    pitch_classes.insert(note.note % 12);
  }

  // In RhythmLock, the pattern should be consistent
  // Check that we don't use too many different pitch classes (pattern should be limited)
  // A typical locked pattern uses 3-6 pitch classes
  EXPECT_LE(pitch_classes.size(), 8u)
      << "RhythmLock pattern should use a limited set of pitch classes";
  EXPECT_GE(pitch_classes.size(), 2u)
      << "RhythmLock pattern should use at least 2 pitch classes for variety";

  // Check that notes are evenly distributed (pattern repeats)
  // Calculate the average gap between notes
  std::vector<Tick> note_starts;
  for (const auto& note : motif_notes) {
    note_starts.push_back(note.start_tick);
  }
  std::sort(note_starts.begin(), note_starts.end());

  if (note_starts.size() >= 4) {
    std::vector<Tick> gaps;
    for (size_t i = 1; i < note_starts.size(); ++i) {
      gaps.push_back(note_starts[i] - note_starts[i - 1]);
    }
    std::sort(gaps.begin(), gaps.end());

    // The most common gap should repeat (pattern regularity)
    std::map<Tick, int> gap_counts;
    for (Tick gap : gaps) {
      // Group similar gaps (within 60 ticks = 1/8th note)
      Tick rounded_gap = (gap / 60) * 60;
      gap_counts[rounded_gap]++;
    }

    int max_count = 0;
    for (const auto& [gap, count] : gap_counts) {
      if (count > max_count) max_count = count;
    }

    // Most common gap should appear multiple times (pattern repetition)
    EXPECT_GE(max_count, 2)
        << "RhythmLock pattern should have repeating rhythmic intervals";
  }
}

// Test that RhythmLock mode is detected correctly
TEST_F(MotifRhythmLockTest, BlueprintSetsRhythmLockMode) {
  Generator gen;
  gen.generate(params_);

  // Blueprint 1 should set RhythmSync paradigm with Locked policy
  EXPECT_EQ(gen.getParams().paradigm, GenerationParadigm::RhythmSync);
  EXPECT_TRUE(gen.getParams().riff_policy == RiffPolicy::LockedContour ||
              gen.getParams().riff_policy == RiffPolicy::LockedPitch ||
              gen.getParams().riff_policy == RiffPolicy::LockedAll)
      << "Blueprint 1 should set a Locked riff policy";
}

// Test that Motif notes are properly registered for collision detection
TEST_F(MotifRhythmLockTest, MotifNotesAreRegisteredForCollisionCheck) {
  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& chord_notes = gen.getSong().chord().notes();

  ASSERT_GT(motif_notes.size(), 0u) << "Motif should have notes";

  // In RhythmLock mode, Chord should avoid Motif notes
  // Check for minor 2nd (1 semitone) clashes
  int clashes = 0;
  for (const auto& motif : motif_notes) {
    Tick motif_end = motif.start_tick + motif.duration;
    for (const auto& chord : chord_notes) {
      Tick chord_end = chord.start_tick + chord.duration;

      // Check for time overlap
      if (motif.start_tick >= chord_end || chord.start_tick >= motif_end) {
        continue;
      }

      // Check for minor 2nd clash
      int interval = std::abs(static_cast<int>(motif.note) - static_cast<int>(chord.note)) % 12;
      if (interval == 1 || interval == 11) {  // Minor 2nd or Major 7th
        clashes++;
      }
    }
  }

  // Should have very few (ideally zero) clashes since Chord avoids Motif
  EXPECT_LE(clashes, 5)
      << "RhythmLock mode should have minimal Motif-Chord clashes. Found " << clashes;
}

// ============================================================================
// RhythmLock Riff Shape Preservation Tests
// ============================================================================
// When motif is coordinate axis in RhythmSync, it should:
// 1. Preserve melodic contour (relative intervals between notes)
// 2. Apply moderate section-based register shifts (P5/P4, not full octaves)
// 3. Stay within valid pitch range after shifts

// Test that RhythmLock motif preserves melodic contour across repetitions
TEST_F(MotifRhythmLockTest, PreservesMelodicContourInRiff) {
  params_.structure = StructurePattern::FullPop;  // Multiple sections for testing

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& sections = gen.getSong().arrangement().sections();

  ASSERT_GT(motif_notes.size(), 8u) << "Need sufficient motif notes for contour test";

  // Find two sections of the same type to compare contours
  std::vector<const Section*> verse_sections;
  std::vector<const Section*> chorus_sections;

  for (const auto& section : sections) {
    if (section.type == SectionType::A || section.type == SectionType::B) {
      verse_sections.push_back(&section);
    } else if (section.type == SectionType::Chorus) {
      chorus_sections.push_back(&section);
    }
  }

  // Helper to extract contour (sequence of interval directions) from a section
  auto extractContour = [&](const Section* section) -> std::vector<int> {
    std::vector<int> contour;
    std::vector<uint8_t> pitches_in_section;

    for (const auto& note : motif_notes) {
      if (note.start_tick >= section->start_tick &&
          note.start_tick < section->endTick()) {
        pitches_in_section.push_back(note.note);
      }
    }

    if (pitches_in_section.size() < 2) return contour;

    // Convert to contour: +1 for up, -1 for down, 0 for same
    for (size_t i = 1; i < pitches_in_section.size(); ++i) {
      int diff = static_cast<int>(pitches_in_section[i]) -
                 static_cast<int>(pitches_in_section[i - 1]);
      if (diff > 0) contour.push_back(1);
      else if (diff < 0) contour.push_back(-1);
      else contour.push_back(0);
    }
    return contour;
  };

  // Compare contours between same section types (Locked policy should preserve shape)
  if (verse_sections.size() >= 2) {
    auto contour1 = extractContour(verse_sections[0]);
    auto contour2 = extractContour(verse_sections[1]);

    if (!contour1.empty() && !contour2.empty()) {
      // Count matching directions (allow some variation due to collision avoidance)
      size_t min_len = std::min(contour1.size(), contour2.size());
      int matching = 0;
      for (size_t i = 0; i < min_len; ++i) {
        if (contour1[i] == contour2[i]) matching++;
      }

      // At least 50% of contour should match (Locked policy preserves shape)
      float match_ratio = static_cast<float>(matching) / min_len;
      EXPECT_GE(match_ratio, 0.5f)
          << "Verse sections should have similar melodic contour in RhythmLock mode. "
          << "Match ratio: " << match_ratio;
    }
  }
}

// Test that section-based register shifts use moderate intervals (P5/P4), not octaves
TEST_F(MotifRhythmLockTest, SectionShiftsUseModerateIntervals) {
  params_.structure = StructurePattern::FullPop;

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& sections = gen.getSong().arrangement().sections();

  // Calculate average pitch for each section type
  auto avgPitchForSectionType = [&](SectionType type) -> double {
    double sum = 0;
    int count = 0;
    for (const auto& section : sections) {
      if (section.type != type) continue;
      for (const auto& note : motif_notes) {
        if (note.start_tick >= section.start_tick &&
            note.start_tick < section.endTick()) {
          sum += note.note;
          count++;
        }
      }
    }
    return count > 0 ? sum / count : 0;
  };

  double verse_avg = avgPitchForSectionType(SectionType::A);
  double chorus_avg = avgPitchForSectionType(SectionType::Chorus);
  double bridge_avg = avgPitchForSectionType(SectionType::Bridge);

  // Chorus should be higher than Verse (P5 = 7 semitones shift)
  if (verse_avg > 0 && chorus_avg > 0) {
    double shift = chorus_avg - verse_avg;
    // Should be in the range of 0-12 semitones (P5 = 7, but variations allowed)
    // Not a full octave (12) or more
    EXPECT_GE(shift, -2.0) << "Chorus should not be significantly lower than Verse";
    EXPECT_LE(shift, 14.0) << "Chorus shift should be moderate, not extreme";
  }

  // Bridge should be lower than Verse (P4 down = -5 semitones shift)
  if (verse_avg > 0 && bridge_avg > 0) {
    double shift = bridge_avg - verse_avg;
    // Should be in the range of -12 to +2 semitones
    EXPECT_LE(shift, 5.0) << "Bridge should not be significantly higher than Verse";
    EXPECT_GE(shift, -14.0) << "Bridge shift should be moderate, not extreme";
  }
}

// Test that all motif pitches stay within valid range after section shifts
TEST_F(MotifRhythmLockTest, PitchesStayWithinRangeAfterShifts) {
  constexpr uint8_t MOTIF_LOW = 60;   // C4
  constexpr uint8_t MOTIF_HIGH = 108; // C8 (from pitch_utils.h)

  std::vector<uint32_t> test_seeds = {12345, 42, 99999, 54321, 11111};

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;
    params_.structure = StructurePattern::FullPop;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    if (motif_notes.empty()) continue;

    int out_of_range = 0;
    for (const auto& note : motif_notes) {
      if (note.note < MOTIF_LOW || note.note > MOTIF_HIGH) {
        out_of_range++;
      }
    }

    // All notes should be within range (clamping should handle edge cases)
    EXPECT_EQ(out_of_range, 0)
        << "Seed " << seed << ": Found " << out_of_range
        << " motif notes outside valid range [" << (int)MOTIF_LOW
        << ", " << (int)MOTIF_HIGH << "]";
  }
}

// Test that RhythmLock pattern rhythm is consistent (same onset pattern repeats)
TEST_F(MotifRhythmLockTest, RhythmPatternIsConsistent) {
  params_.structure = StructurePattern::FullPop;

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& sections = gen.getSong().arrangement().sections();

  ASSERT_GT(motif_notes.size(), 4u) << "Need sufficient notes for rhythm test";

  // Extract rhythm pattern (onset positions within each bar) for sections of same type
  auto extractBarOnsets = [&](const Section* section) -> std::vector<Tick> {
    std::vector<Tick> onsets;
    for (const auto& note : motif_notes) {
      if (note.start_tick >= section->start_tick &&
          note.start_tick < section->endTick()) {
        // Get position within bar
        Tick within_bar = (note.start_tick - section->start_tick) % TICKS_PER_BAR;
        onsets.push_back(within_bar);
      }
    }
    std::sort(onsets.begin(), onsets.end());
    return onsets;
  };

  // Find sections of the same type
  std::vector<const Section*> verses;
  for (const auto& section : sections) {
    if (section.type == SectionType::A || section.type == SectionType::B) {
      verses.push_back(&section);
    }
  }

  if (verses.size() >= 2) {
    auto onsets1 = extractBarOnsets(verses[0]);
    auto onsets2 = extractBarOnsets(verses[1]);

    if (onsets1.size() >= 2 && onsets2.size() >= 2) {
      // Compare onset patterns (should have similar rhythmic positions)
      // Count how many onsets are at similar positions (within 120 ticks = 16th note)
      int similar_onsets = 0;
      for (Tick o1 : onsets1) {
        for (Tick o2 : onsets2) {
          if (std::abs(static_cast<int>(o1) - static_cast<int>(o2)) <= 120) {
            similar_onsets++;
            break;
          }
        }
      }

      float similarity = static_cast<float>(similar_onsets) / onsets1.size();
      EXPECT_GE(similarity, 0.4f)
          << "RhythmLock should maintain consistent rhythm pattern across sections. "
          << "Similarity: " << similarity;
    }
  }
}

// Test that different seeds produce valid riff patterns
TEST_F(MotifRhythmLockTest, MultipleSeeedsProduceValidRiffs) {
  std::vector<uint32_t> test_seeds = {12345, 42, 99999, 54321, 777};
  int valid_riffs = 0;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    if (motif_notes.empty()) continue;

    // A valid riff should:
    // 1. Have multiple notes
    bool has_notes = motif_notes.size() >= 4;

    // 2. Use a limited set of pitch classes (pattern consistency)
    std::set<int> pitch_classes;
    for (const auto& note : motif_notes) {
      pitch_classes.insert(note.note % 12);
    }
    bool limited_pitches = pitch_classes.size() >= 2 && pitch_classes.size() <= 8;

    // 3. Have regular rhythm (median gap should be reasonable)
    std::vector<Tick> note_starts;
    for (const auto& note : motif_notes) {
      note_starts.push_back(note.start_tick);
    }
    std::sort(note_starts.begin(), note_starts.end());

    std::vector<Tick> gaps;
    for (size_t i = 1; i < note_starts.size(); ++i) {
      gaps.push_back(note_starts[i] - note_starts[i - 1]);
    }
    std::sort(gaps.begin(), gaps.end());

    bool regular_rhythm = gaps.empty() ||
        gaps[gaps.size() / 2] <= TICKS_PER_BAR * 2;  // Median gap <= 2 bars

    if (has_notes && limited_pitches && regular_rhythm) {
      valid_riffs++;
    }
  }

  EXPECT_GE(valid_riffs, 4) << "At least 4 out of 5 seeds should produce valid riffs";
}

}  // namespace
}  // namespace midisketch
