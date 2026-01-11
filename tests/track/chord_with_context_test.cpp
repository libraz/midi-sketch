/**
 * @file chord_with_context_test.cpp
 * @brief Tests for chord generation with context.
 */

#include <gtest/gtest.h>
#include "core/generator.h"
#include "core/harmony_context.h"
#include "core/song.h"
#include "core/types.h"
#include "track/bass.h"
#include "track/chord_track.h"
#include "track/vocal.h"
#include "track/vocal_analysis.h"
#include <random>
#include <set>

namespace midisketch {
namespace {

class ChordWithContextTest : public ::testing::Test {
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

TEST_F(ChordWithContextTest, GeneratesChordTrack) {
  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  // Generate bass first (chord needs bass for coordination)
  MidiTrack bass_track;
  std::mt19937 rng(params_.seed);
  HarmonyContext harmony;
  generateBassTrackWithVocal(bass_track, gen.getSong(), params_, rng, va, harmony);

  // Generate chord with context
  MidiTrack chord_track;
  std::mt19937 rng2(params_.seed + 1);
  generateChordTrackWithContext(chord_track, gen.getSong(), params_, rng2,
                                 &bass_track, va, nullptr, harmony);

  EXPECT_FALSE(chord_track.empty()) << "Chord track should be generated";
  EXPECT_GT(chord_track.noteCount(), 0u) << "Chord track should have notes";
}

TEST_F(ChordWithContextTest, ChordNotesInValidRange) {
  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass_track;
  std::mt19937 rng(params_.seed);
  HarmonyContext harmony;
  generateBassTrackWithVocal(bass_track, gen.getSong(), params_, rng, va, harmony);

  MidiTrack chord_track;
  std::mt19937 rng2(params_.seed + 1);
  generateChordTrackWithContext(chord_track, gen.getSong(), params_, rng2,
                                 &bass_track, va, nullptr, harmony);

  for (const auto& note : chord_track.notes()) {
    EXPECT_GE(note.note, 48) << "Chord note too low: " << static_cast<int>(note.note);
    EXPECT_LE(note.note, 84) << "Chord note too high: " << static_cast<int>(note.note);
  }
}

TEST_F(ChordWithContextTest, DeterministicGeneration) {
  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass_track;
  std::mt19937 rng_bass(params_.seed);
  HarmonyContext harmony;
  generateBassTrackWithVocal(bass_track, gen.getSong(), params_, rng_bass, va, harmony);

  // First generation
  MidiTrack chord1;
  std::mt19937 rng1(params_.seed + 1);
  generateChordTrackWithContext(chord1, gen.getSong(), params_, rng1,
                                 &bass_track, va, nullptr, harmony);

  // Second generation with same seed
  MidiTrack chord2;
  std::mt19937 rng2(params_.seed + 1);
  generateChordTrackWithContext(chord2, gen.getSong(), params_, rng2,
                                 &bass_track, va, nullptr, harmony);

  ASSERT_EQ(chord1.noteCount(), chord2.noteCount());
  for (size_t i = 0; i < chord1.noteCount(); ++i) {
    EXPECT_EQ(chord1.notes()[i].note, chord2.notes()[i].note);
    EXPECT_EQ(chord1.notes()[i].start_tick, chord2.notes()[i].start_tick);
  }
}

// === Vocal Doubling Avoidance Tests ===

TEST_F(ChordWithContextTest, AvoidsVocalDoublingWhenPossible) {
  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass_track;
  std::mt19937 rng_bass(params_.seed);
  HarmonyContext harmony;
  generateBassTrackWithVocal(bass_track, gen.getSong(), params_, rng_bass, va, harmony);

  MidiTrack chord_track;
  std::mt19937 rng(params_.seed + 1);
  generateChordTrackWithContext(chord_track, gen.getSong(), params_, rng,
                                 &bass_track, va, nullptr, harmony);

  const auto& vocal_notes = gen.getSong().vocal().notes();
  const auto& chord_notes = chord_track.notes();

  int doubling_count = 0;
  int overlap_count = 0;

  for (const auto& vocal_note : vocal_notes) {
    Tick vocal_end = vocal_note.start_tick + vocal_note.duration;
    int vocal_pc = vocal_note.note % 12;

    for (const auto& chord_note : chord_notes) {
      Tick chord_end = chord_note.start_tick + chord_note.duration;
      int chord_pc = chord_note.note % 12;

      // Check if notes overlap in time
      if (vocal_note.start_tick < chord_end && chord_note.start_tick < vocal_end) {
        overlap_count++;
        // Check if pitch class matches (doubling)
        if (vocal_pc == chord_pc) {
          doubling_count++;
        }
      }
    }
  }

  // We expect some overlaps (chord and vocal play together)
  EXPECT_GT(overlap_count, 0) << "Should have overlapping notes";

  // Doubling should be reduced compared to total overlaps
  // Allow some doubling (fallback case), but it should be minimized
  // Note: Close voicing increases doubling slightly vs Rootless, so threshold is 0.35
  float doubling_ratio = static_cast<float>(doubling_count) / overlap_count;
  EXPECT_LT(doubling_ratio, 0.35f) << "Doubling ratio should be low: "
                                    << doubling_count << "/" << overlap_count;
}

// === Aux Clash Avoidance Tests ===

TEST_F(ChordWithContextTest, GeneratesWithAuxTrack) {
  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass_track;
  std::mt19937 rng_bass(params_.seed);
  HarmonyContext harmony;
  generateBassTrackWithVocal(bass_track, gen.getSong(), params_, rng_bass, va, harmony);

  // Create a simple aux track
  MidiTrack aux_track;
  aux_track.addNote(0, 480, 72, 80);      // C5
  aux_track.addNote(1920, 480, 74, 80);   // D5
  aux_track.addNote(3840, 480, 76, 80);   // E5

  MidiTrack chord_track;
  std::mt19937 rng(params_.seed + 1);
  generateChordTrackWithContext(chord_track, gen.getSong(), params_, rng,
                                 &bass_track, va, &aux_track, harmony);

  EXPECT_FALSE(chord_track.empty()) << "Chord track should be generated with aux";
}

TEST_F(ChordWithContextTest, ReducesMinor2ndClashesWithAux) {
  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass_track;
  std::mt19937 rng_bass(params_.seed);
  HarmonyContext harmony;
  generateBassTrackWithVocal(bass_track, gen.getSong(), params_, rng_bass, va, harmony);

  // Create aux track with specific notes to test clash avoidance
  MidiTrack aux_track;
  // Add notes that could clash (C# would clash with C or D in chord)
  for (Tick t = 0; t < 7680; t += 1920) {
    aux_track.addNote(t, 480, 73, 80);  // C#5 (pitch class 1)
  }

  MidiTrack chord_track;
  std::mt19937 rng(params_.seed + 1);
  generateChordTrackWithContext(chord_track, gen.getSong(), params_, rng,
                                 &bass_track, va, &aux_track, harmony);

  // Count minor 2nd clashes
  int clash_count = 0;
  for (const auto& chord_note : chord_track.notes()) {
    Tick chord_end = chord_note.start_tick + chord_note.duration;
    int chord_pc = chord_note.note % 12;

    for (const auto& aux_note : aux_track.notes()) {
      Tick aux_end = aux_note.start_tick + aux_note.duration;
      int aux_pc = aux_note.note % 12;

      // Check overlap
      if (chord_note.start_tick < aux_end && aux_note.start_tick < chord_end) {
        // Check minor 2nd (1 semitone)
        int interval = std::abs(chord_pc - aux_pc);
        if (interval > 6) interval = 12 - interval;
        if (interval == 1) {
          clash_count++;
        }
      }
    }
  }

  // Expect few or no minor 2nd clashes
  EXPECT_LT(clash_count, 10) << "Should minimize minor 2nd clashes with aux";
}

// === Integration with Full Workflow ===

TEST_F(ChordWithContextTest, WorksWithGenerateAccompaniment) {
  Generator gen;
  gen.generateVocal(params_);
  gen.generateAccompanimentForVocal();

  // Verify chord track was generated
  const auto& chord_track = gen.getSong().chord();
  EXPECT_FALSE(chord_track.empty()) << "Chord track should be generated";
  EXPECT_GT(chord_track.noteCount(), 0u);
}

// === Fallback Behavior Tests ===

TEST_F(ChordWithContextTest, FallbackWhenAllVoicingsFiltered) {
  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass_track;
  std::mt19937 rng_bass(params_.seed);
  HarmonyContext harmony;
  generateBassTrackWithVocal(bass_track, gen.getSong(), params_, rng_bass, va, harmony);

  // Create aux track that covers many pitch classes
  MidiTrack aux_track;
  // Add many notes to potentially trigger fallback
  for (int pc = 0; pc < 12; pc++) {
    aux_track.addNote(0, 1920, 60 + pc, 80);
  }

  MidiTrack chord_track;
  std::mt19937 rng(params_.seed + 1);
  generateChordTrackWithContext(chord_track, gen.getSong(), params_, rng,
                                 &bass_track, va, &aux_track, harmony);

  // Even with aggressive filtering, chord should still be generated
  EXPECT_FALSE(chord_track.empty()) << "Chord track should fallback gracefully";
}

}  // namespace
}  // namespace midisketch
