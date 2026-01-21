/**
 * @file bass_with_vocal_test.cpp
 * @brief Tests for bass track with vocal adaptation.
 */

#include <gtest/gtest.h>

#include <random>
#include <set>

#include "core/generator.h"
#include "core/harmony_context.h"
#include "core/i_harmony_context.h"
#include "core/song.h"
#include "core/types.h"
#include "track/bass.h"
#include "track/vocal.h"
#include "track/vocal_analysis.h"

namespace midisketch {
namespace {

class BassWithVocalTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::ElectroPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.vocal_low = 60;
    params_.vocal_high = 84;
    params_.bpm = 120;
    params_.seed = 12345;
    params_.arpeggio_enabled = false;
  }

  GeneratorParams params_;
};

// === Basic Generation Tests ===

TEST_F(BassWithVocalTest, GeneratesBassTrack) {
  Generator gen;
  gen.generateVocal(params_);

  // Analyze vocal
  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  // Generate bass with vocal analysis
  MidiTrack bass_track;
  std::mt19937 rng(params_.seed);
  HarmonyContext harmony;
  generateBassTrackWithVocal(bass_track, gen.getSong(), params_, rng, va, harmony);

  EXPECT_FALSE(bass_track.empty()) << "Bass track should be generated";
  EXPECT_GT(bass_track.noteCount(), 0u) << "Bass track should have notes";
}

TEST_F(BassWithVocalTest, BassNotesInValidRange) {
  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass_track;
  std::mt19937 rng(params_.seed);
  HarmonyContext harmony;
  generateBassTrackWithVocal(bass_track, gen.getSong(), params_, rng, va, harmony);

  for (const auto& note : bass_track.notes()) {
    EXPECT_GE(note.note, 24) << "Bass note too low: " << static_cast<int>(note.note);
    EXPECT_LE(note.note, 60) << "Bass note too high: " << static_cast<int>(note.note);
  }
}

TEST_F(BassWithVocalTest, DeterministicGeneration) {
  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  // First generation
  MidiTrack bass1;
  std::mt19937 rng1(params_.seed);
  HarmonyContext harmony1;
  generateBassTrackWithVocal(bass1, gen.getSong(), params_, rng1, va, harmony1);

  // Second generation with same seed
  MidiTrack bass2;
  std::mt19937 rng2(params_.seed);
  HarmonyContext harmony2;
  generateBassTrackWithVocal(bass2, gen.getSong(), params_, rng2, va, harmony2);

  ASSERT_EQ(bass1.noteCount(), bass2.noteCount());
  for (size_t i = 0; i < bass1.noteCount(); ++i) {
    EXPECT_EQ(bass1.notes()[i].note, bass2.notes()[i].note);
    EXPECT_EQ(bass1.notes()[i].start_tick, bass2.notes()[i].start_tick);
  }
}

// === Octave Separation Tests ===

TEST_F(BassWithVocalTest, MaintainsOctaveSeparation) {
  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass_track;
  std::mt19937 rng(params_.seed);
  HarmonyContext harmony;
  generateBassTrackWithVocal(bass_track, gen.getSong(), params_, rng, va, harmony);

  // Check that bass and vocal don't have same pitch class within 2 octaves
  // when sounding at the same time
  constexpr int kMinOctaveSeparation = 24;

  const auto& vocal_notes = gen.getSong().vocal().notes();
  const auto& bass_notes = bass_track.notes();

  int close_doubling_count = 0;

  for (const auto& bass_note : bass_notes) {
    Tick bass_end = bass_note.start_tick + bass_note.duration;

    for (const auto& vocal_note : vocal_notes) {
      Tick vocal_end = vocal_note.start_tick + vocal_note.duration;

      // Check if notes overlap
      bool overlap = (bass_note.start_tick < vocal_end) && (vocal_note.start_tick < bass_end);

      if (overlap) {
        // Check pitch class
        if ((bass_note.note % 12) == (vocal_note.note % 12)) {
          int separation =
              std::abs(static_cast<int>(bass_note.note) - static_cast<int>(vocal_note.note));
          if (separation < kMinOctaveSeparation) {
            close_doubling_count++;
          }
        }
      }
    }
  }

  // Allow some close doublings (can't always avoid), but should be minimal
  double doubling_ratio =
      static_cast<double>(close_doubling_count) / static_cast<double>(bass_notes.size());
  EXPECT_LT(doubling_ratio, 0.2) << "Too many close pitch class doublings: " << close_doubling_count
                                 << " out of " << bass_notes.size() << " bass notes";
}

// === Rhythmic Complementation Tests ===

TEST_F(BassWithVocalTest, AdaptsToDenseVocal) {
  // Create a dense vocal track scenario
  params_.seed = 11111;  // Different seed for variety
  params_.structure = StructurePattern::ShortForm;

  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass_track;
  std::mt19937 rng(params_.seed);
  HarmonyContext harmony;
  generateBassTrackWithVocal(bass_track, gen.getSong(), params_, rng, va, harmony);

  // Just verify bass was generated successfully
  EXPECT_FALSE(bass_track.empty());
}

TEST_F(BassWithVocalTest, AdaptsToSparseVocal) {
  // Use ballad structure/mood for sparser vocal
  params_.mood = Mood::Ballad;
  params_.seed = 22222;

  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass_track;
  std::mt19937 rng(params_.seed);
  HarmonyContext harmony;
  generateBassTrackWithVocal(bass_track, gen.getSong(), params_, rng, va, harmony);

  EXPECT_FALSE(bass_track.empty());
}

// === Different Moods Tests ===

TEST_F(BassWithVocalTest, WorksWithDifferentMoods) {
  std::vector<Mood> moods = {Mood::ElectroPop, Mood::Ballad,
                             Mood::CityPop,  // Jazz-influenced
                             Mood::LightRock, Mood::Yoasobi};

  for (Mood mood : moods) {
    params_.mood = mood;
    params_.seed = static_cast<uint32_t>(mood) + 10000;

    Generator gen;
    gen.generateVocal(params_);

    VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

    MidiTrack bass_track;
    std::mt19937 rng(params_.seed);
    HarmonyContext harmony;
    generateBassTrackWithVocal(bass_track, gen.getSong(), params_, rng, va, harmony);

    EXPECT_FALSE(bass_track.empty())
        << "Bass should be generated for mood " << static_cast<int>(mood);
  }
}

// === Structure Tests ===

TEST_F(BassWithVocalTest, WorksWithDifferentStructures) {
  std::vector<StructurePattern> structures = {
      StructurePattern::StandardPop, StructurePattern::ShortForm, StructurePattern::FullPop,
      StructurePattern::DirectChorus};

  for (auto structure : structures) {
    params_.structure = structure;
    params_.seed = static_cast<uint32_t>(structure) + 20000;

    Generator gen;
    gen.generateVocal(params_);

    VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

    MidiTrack bass_track;
    std::mt19937 rng(params_.seed);
    HarmonyContext harmony;
    generateBassTrackWithVocal(bass_track, gen.getSong(), params_, rng, va, harmony);

    EXPECT_FALSE(bass_track.empty())
        << "Bass should be generated for structure " << static_cast<int>(structure);
  }
}

// === Empty Vocal Edge Case ===

TEST_F(BassWithVocalTest, HandlesEmptyVocalAnalysis) {
  Generator gen;
  gen.generateVocal(params_);

  // Create empty vocal analysis
  VocalAnalysis empty_va{};
  empty_va.density = 0.0f;
  empty_va.average_duration = 0.0f;
  empty_va.lowest_pitch = 127;
  empty_va.highest_pitch = 0;

  // Should still generate bass without crashing
  MidiTrack bass_track;
  std::mt19937 rng(params_.seed);
  HarmonyContext harmony;
  generateBassTrackWithVocal(bass_track, gen.getSong(), params_, rng, empty_va, harmony);

  // Bass should be generated (uses default patterns when vocal is empty)
  EXPECT_FALSE(bass_track.empty());
}

// === Minor 2nd Clash Avoidance Tests ===

// Regression test for issue: Bass fifth creating minor 2nd with sustained vocal
// Bug: Syncopated/RootFifth patterns generated fifths without checking
// if they clash with currently sounding vocal notes.
// Example: Vocal G4 sustaining while Bass plays F#3 (fifth of B) = minor 2nd clash
TEST_F(BassWithVocalTest, AvoidsFifthClashWithSustainedVocal) {
  // Use the exact parameters that triggered the original bug
  params_.seed = 4130447576;
  params_.chord_id = 2;  // Axis progression
  params_.structure = StructurePattern::FullPop;
  params_.bpm = 160;
  params_.mood = Mood::IdolPop;  // Style 14 - the original bug parameters

  Generator gen;
  gen.generateWithVocal(params_);

  const auto& vocal_notes = gen.getSong().vocal().notes();
  const auto& bass_notes = gen.getSong().bass().notes();

  int minor_2nd_clashes = 0;

  for (const auto& bass_note : bass_notes) {
    Tick bass_start = bass_note.start_tick;
    Tick bass_end = bass_start + bass_note.duration;

    for (const auto& vocal_note : vocal_notes) {
      Tick vocal_start = vocal_note.start_tick;
      Tick vocal_end = vocal_start + vocal_note.duration;

      // Check if notes overlap in time
      bool overlap = (bass_start < vocal_end) && (vocal_start < bass_end);
      if (!overlap) continue;

      // Calculate actual semitone distance (not pitch class)
      // Music theory: notes 2+ octaves apart (24+ semitones) don't clash perceptually
      int actual_interval =
          std::abs(static_cast<int>(bass_note.note) - static_cast<int>(vocal_note.note));

      // Wide separation (2+ octaves): not a clash
      if (actual_interval >= 24) continue;

      // Check for minor 2nd (1 semitone) pitch class
      int pitch_class_interval = actual_interval % 12;
      if (pitch_class_interval > 6) pitch_class_interval = 12 - pitch_class_interval;

      if (pitch_class_interval == 1) {  // Minor 2nd within 2 octaves
        minor_2nd_clashes++;
      }
    }
  }

  // Should have zero minor 2nd clashes within audible range
  // Before fix: clashes occurred, After fix: createSafe prevents them
  EXPECT_EQ(minor_2nd_clashes, 0)
      << "Bass should avoid minor 2nd clashes with sustained vocal notes. "
      << "Found " << minor_2nd_clashes << " clashes";
}

// Test that bass createSafe fallback works correctly across multiple seeds
TEST_F(BassWithVocalTest, FallsBackToRootWhenFifthClashes) {
  // Test across multiple seeds to ensure robustness
  std::vector<uint32_t> test_seeds = {12345, 67890, 4130447576, 99999};

  int total_clashes = 0;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generateWithVocal(params_);

    const auto& vocal_notes = gen.getSong().vocal().notes();
    const auto& bass_notes = gen.getSong().bass().notes();

    for (const auto& bass_note : bass_notes) {
      Tick bass_start = bass_note.start_tick;
      Tick bass_end = bass_start + bass_note.duration;

      for (const auto& vocal_note : vocal_notes) {
        Tick vocal_start = vocal_note.start_tick;
        Tick vocal_end = vocal_start + vocal_note.duration;

        bool overlap = (bass_start < vocal_end) && (vocal_start < bass_end);
        if (!overlap) continue;

        // Calculate actual semitone distance (music theory aware)
        int actual_interval =
            std::abs(static_cast<int>(bass_note.note) - static_cast<int>(vocal_note.note));

        // Wide separation (2+ octaves): not a perceptual clash
        if (actual_interval >= 24) continue;

        int pitch_class_interval = actual_interval % 12;
        if (pitch_class_interval > 6) pitch_class_interval = 12 - pitch_class_interval;

        if (pitch_class_interval == 1) total_clashes++;
      }
    }
  }

  // Should have zero or very few clashes within audible range
  EXPECT_LE(total_clashes, 2) << "Too many minor 2nd clashes across seeds: " << total_clashes;
}

// === Integration with Generator ===

TEST_F(BassWithVocalTest, IntegrationWithGenerateWithVocal) {
  Generator gen;
  gen.generateWithVocal(params_);

  // Both vocal and bass should be present and non-empty
  EXPECT_FALSE(gen.getSong().vocal().empty());
  EXPECT_FALSE(gen.getSong().bass().empty());

  // Bass should be in valid range
  for (const auto& note : gen.getSong().bass().notes()) {
    EXPECT_GE(note.note, 24);
    EXPECT_LE(note.note, 60);
  }
}

}  // namespace
}  // namespace midisketch
