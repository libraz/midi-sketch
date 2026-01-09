/**
 * @file bass_with_vocal_test.cpp
 * @brief Tests for bass track with vocal adaptation.
 */

#include <gtest/gtest.h>
#include "core/generator.h"
#include "core/harmony_context.h"
#include "core/song.h"
#include "core/types.h"
#include "track/bass.h"
#include "track/vocal.h"
#include "track/vocal_analysis.h"
#include <random>
#include <set>

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
      bool overlap = (bass_note.start_tick < vocal_end) &&
                     (vocal_note.start_tick < bass_end);

      if (overlap) {
        // Check pitch class
        if ((bass_note.note % 12) == (vocal_note.note % 12)) {
          int separation = std::abs(static_cast<int>(bass_note.note) -
                                    static_cast<int>(vocal_note.note));
          if (separation < kMinOctaveSeparation) {
            close_doubling_count++;
          }
        }
      }
    }
  }

  // Allow some close doublings (can't always avoid), but should be minimal
  double doubling_ratio = static_cast<double>(close_doubling_count) /
                          static_cast<double>(bass_notes.size());
  EXPECT_LT(doubling_ratio, 0.2)
      << "Too many close pitch class doublings: " << close_doubling_count
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
  std::vector<Mood> moods = {
    Mood::ElectroPop,
    Mood::Ballad,
    Mood::CityPop,  // Jazz-influenced
    Mood::LightRock,
    Mood::Yoasobi
  };

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
    StructurePattern::StandardPop,
    StructurePattern::ShortForm,
    StructurePattern::FullPop,
    StructurePattern::DirectChorus
  };

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
