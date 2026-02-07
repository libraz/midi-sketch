/**
 * @file generator_iteration_snapshot_test.cpp
 * @brief Safety net tests for section-bar iteration refactoring.
 *
 * Verifies that key tracks produce valid notes across multiple
 * blueprints and seeds. These tests capture current behavior to
 * detect regressions during iteration pattern changes.
 */

#include <gtest/gtest.h>

#include <vector>

#include "core/generator.h"
#include "core/preset_data.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "midisketch.h"

namespace midisketch {
namespace {

constexpr uint32_t kSeeds[] = {42, 100};
constexpr uint8_t kBlueprints[] = {0, 1, 2, 3};

class GeneratorIterationSnapshotTest : public ::testing::Test {
 protected:
  void generateSong(uint32_t seed, uint8_t blueprint) {
    SongConfig config = createDefaultSongConfig(0);
    config.seed = seed;
    config.blueprint_id = blueprint;
    sketch_.generateFromConfig(config);
  }

  MidiSketch sketch_;
};

// Verify Bass track produces notes for all seed/blueprint combos
TEST_F(GeneratorIterationSnapshotTest, BassTrackProducesNotes) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& bass = sketch_.getSong().bass();

      EXPECT_GT(bass.noteCount(), 0u)
          << "Bass track empty for seed=" << seed << " bp=" << (int)blueprint;
    }
  }
}

// Verify Chord track produces notes for all combos
TEST_F(GeneratorIterationSnapshotTest, ChordTrackProducesNotes) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& chord = sketch_.getSong().chord();

      EXPECT_GT(chord.noteCount(), 0u)
          << "Chord track empty for seed=" << seed << " bp=" << (int)blueprint;
    }
  }
}

// Verify Guitar track behavior (may be empty depending on blueprint)
TEST_F(GeneratorIterationSnapshotTest, GuitarTrackBehavior) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& guitar = sketch_.getSong().guitar();

      // Guitar track may or may not have notes depending on blueprint
      // Just verify it doesn't crash and notes (if present) are valid
      for (const auto& note : guitar.notes()) {
        EXPECT_LE(note.note, 127)
            << "Guitar pitch > 127 for seed=" << seed << " bp=" << (int)blueprint;
        EXPECT_GT(note.duration, 0u)
            << "Guitar zero duration for seed=" << seed << " bp=" << (int)blueprint;
      }
    }
  }
}

// Verify Arpeggio track behavior (depends on arpeggio_enabled)
TEST_F(GeneratorIterationSnapshotTest, ArpeggioTrackBehavior) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      SongConfig config = createDefaultSongConfig(0);
      config.seed = seed;
      config.blueprint_id = blueprint;
      config.arpeggio_enabled = true;  // Explicitly enable
      sketch_.generateFromConfig(config);

      const auto& arpeggio = sketch_.getSong().arpeggio();

      EXPECT_GT(arpeggio.noteCount(), 0u)
          << "Arpeggio track empty when enabled for seed=" << seed << " bp=" << (int)blueprint;
    }
  }
}

// Verify note start ticks are within song duration bounds
TEST_F(GeneratorIterationSnapshotTest, NoteStartsWithinSongBounds) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& song = sketch_.getSong();
      Tick song_end = song.arrangement().totalTicks();

      // Check Bass
      for (const auto& note : song.bass().notes()) {
        EXPECT_LT(note.start_tick, song_end)
            << "Bass note starts after song end for seed=" << seed << " bp=" << (int)blueprint;
      }

      // Check Chord
      for (const auto& note : song.chord().notes()) {
        EXPECT_LT(note.start_tick, song_end)
            << "Chord note starts after song end for seed=" << seed << " bp=" << (int)blueprint;
      }

      // Check Vocal
      for (const auto& note : song.vocal().notes()) {
        EXPECT_LT(note.start_tick, song_end)
            << "Vocal note starts after song end for seed=" << seed << " bp=" << (int)blueprint;
      }
    }
  }
}

// Verify no notes have zero duration across key tracks
TEST_F(GeneratorIterationSnapshotTest, NoZeroDurationNotes) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& song = sketch_.getSong();

      auto check_no_zero_duration = [&](const MidiTrack& track, const std::string& name) {
        for (const auto& note : track.notes()) {
          EXPECT_GT(note.duration, 0u) << name << " note with zero duration at tick="
                                       << note.start_tick << " seed=" << seed
                                       << " bp=" << (int)blueprint;
        }
      };

      check_no_zero_duration(song.bass(), "Bass");
      check_no_zero_duration(song.chord(), "Chord");
      check_no_zero_duration(song.vocal(), "Vocal");
      check_no_zero_duration(song.motif(), "Motif");
      check_no_zero_duration(song.aux(), "Aux");
    }
  }
}

// Verify Vocal track produces notes (it's the coordinate axis for most paradigms)
TEST_F(GeneratorIterationSnapshotTest, VocalTrackProducesNotes) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& vocal = sketch_.getSong().vocal();

      EXPECT_GT(vocal.noteCount(), 0u)
          << "Vocal track empty for seed=" << seed << " bp=" << (int)blueprint;
    }
  }
}

// Verify Motif track produces notes for paradigms that use motif.
// Blueprint 0 (Traditional) may have empty motif depending on style/mood defaults.
TEST_F(GeneratorIterationSnapshotTest, MotifTrackProducesNotes) {
  // Blueprints 1-3 should produce motif notes
  constexpr uint8_t kMotifBlueprints[] = {1, 2, 3};
  for (uint8_t blueprint : kMotifBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& motif = sketch_.getSong().motif();

      EXPECT_GT(motif.noteCount(), 0u)
          << "Motif track empty for seed=" << seed << " bp=" << (int)blueprint;
    }
  }
}

// Verify Aux track produces notes
TEST_F(GeneratorIterationSnapshotTest, AuxTrackProducesNotes) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& aux = sketch_.getSong().aux();

      EXPECT_GT(aux.noteCount(), 0u)
          << "Aux track empty for seed=" << seed << " bp=" << (int)blueprint;
    }
  }
}

// Snapshot: record note counts per track for regression detection
TEST_F(GeneratorIterationSnapshotTest, NoteCountsAreStable) {
  // This test just verifies that note counts are non-zero and within
  // reasonable bounds. After refactoring, counts may change slightly
  // but should not become zero or astronomically large.
  constexpr size_t kMinNotesPerTrack = 5;
  constexpr size_t kMaxNotesPerTrack = 5000;

  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& song = sketch_.getSong();

      size_t bass_count = song.bass().noteCount();
      size_t chord_count = song.chord().noteCount();
      size_t vocal_count = song.vocal().noteCount();

      EXPECT_GE(bass_count, kMinNotesPerTrack)
          << "Bass too few notes for seed=" << seed << " bp=" << (int)blueprint;
      EXPECT_LE(bass_count, kMaxNotesPerTrack)
          << "Bass too many notes for seed=" << seed << " bp=" << (int)blueprint;

      EXPECT_GE(chord_count, kMinNotesPerTrack)
          << "Chord too few notes for seed=" << seed << " bp=" << (int)blueprint;
      EXPECT_LE(chord_count, kMaxNotesPerTrack)
          << "Chord too many notes for seed=" << seed << " bp=" << (int)blueprint;

      EXPECT_GE(vocal_count, kMinNotesPerTrack)
          << "Vocal too few notes for seed=" << seed << " bp=" << (int)blueprint;
      EXPECT_LE(vocal_count, kMaxNotesPerTrack)
          << "Vocal too many notes for seed=" << seed << " bp=" << (int)blueprint;
    }
  }
}

}  // namespace
}  // namespace midisketch
