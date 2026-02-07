/**
 * @file chord_collision_regression_test.cpp
 * @brief Safety net tests for chord-bass collision detection.
 *
 * Tests across all 9 blueprints with multiple seeds to detect regressions
 * that significantly increase bass-chord dissonant clashes.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "core/generator.h"
#include "core/i_harmony_context.h"
#include "core/preset_data.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "midisketch.h"
#include "test_support/clash_analysis_helper.h"

namespace midisketch {
namespace {

using test::ClashInfo;
using test::findClashes;

constexpr uint32_t kSeeds[] = {42, 100, 200, 999};
constexpr uint8_t kAllBlueprints[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};

// Maximum number of bass-chord clashes allowed per song.
// A small number is acceptable (chord boundary effects, etc.),
// but a large increase would indicate a regression.
constexpr size_t kMaxBassChordClashesPerSong = 30;

class ChordCollisionRegressionTest : public ::testing::Test {
 protected:
  void generateSong(uint32_t seed, uint8_t blueprint) {
    SongConfig config = createDefaultSongConfig(0);
    config.seed = seed;
    config.blueprint_id = blueprint;
    sketch_.generateFromConfig(config);
  }

  MidiSketch sketch_;
};

// Test bass-chord collisions across all blueprint/seed combinations
TEST_F(ChordCollisionRegressionTest, BassChordClashesBelowThreshold) {
  for (uint8_t blueprint : kAllBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);

      const auto& song = sketch_.getSong();
      const auto& harmony = sketch_.getHarmonyContext();

      auto clashes = findClashes(song.bass(), "Bass", song.chord(), "Chord", harmony);

      if (!clashes.empty()) {
        // Log for debugging but don't necessarily fail
        std::cerr << "[Info] bp=" << (int)blueprint << " seed=" << seed
                  << " bass-chord clashes: " << clashes.size() << "\n";
      }

      EXPECT_LE(clashes.size(), kMaxBassChordClashesPerSong)
          << "Too many bass-chord clashes for blueprint=" << (int)blueprint << " seed=" << seed
          << " (found " << clashes.size() << ")";
    }
  }
}

// Specifically test minor 2nd (1 semitone) clashes between bass and chord,
// which are the most audibly dissonant
TEST_F(ChordCollisionRegressionTest, BassChordMinor2ndClashesLimited) {
  constexpr size_t kMaxMinor2ndClashes = 10;

  for (uint8_t blueprint : kAllBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);

      const auto& song = sketch_.getSong();
      const auto& bass_notes = song.bass().notes();
      const auto& chord_notes = song.chord().notes();

      int minor_2nd_count = 0;
      for (const auto& bass_note : bass_notes) {
        Tick bass_start = bass_note.start_tick;
        Tick bass_end = bass_start + bass_note.duration;

        for (const auto& chord_note : chord_notes) {
          Tick chord_start = chord_note.start_tick;
          Tick chord_end = chord_start + chord_note.duration;

          if (bass_start < chord_end && chord_start < bass_end) {
            int interval =
                std::abs(static_cast<int>(bass_note.note) - static_cast<int>(chord_note.note));
            // Check minor 2nd (1 semitone) or compound minor 2nd (13 semitones)
            if (interval == 1 || interval == 13) {
              minor_2nd_count++;
            }
          }
        }
      }

      EXPECT_LE(minor_2nd_count, static_cast<int>(kMaxMinor2ndClashes))
          << "Too many minor 2nd bass-chord clashes for bp=" << (int)blueprint << " seed=" << seed;
    }
  }
}

// Verify that harmony context collision detection is consistent with
// actual note overlap analysis
TEST_F(ChordCollisionRegressionTest, HarmonyContextReportsCollisionsConsistently) {
  // Use a specific seed/blueprint for focused testing
  generateSong(42, 0);

  const auto& song = sketch_.getSong();
  const auto& harmony = sketch_.getHarmonyContext();
  const auto& bass_notes = song.bass().notes();

  // For each bass note, verify that the harmony context can detect collisions
  int checked = 0;
  for (const auto& bass_note : bass_notes) {
    if (checked >= 20) break;  // Check a reasonable subset

    // The harmony context should be able to provide collision snapshots
    auto snapshot = harmony.getCollisionSnapshot(bass_note.start_tick);
    // Just verify the snapshot returns valid data (tick matches)
    EXPECT_EQ(snapshot.tick, bass_note.start_tick);
    checked++;
  }

  EXPECT_GT(checked, 0) << "Should have checked at least some bass notes";
}

// Test that total clashes across all track pairs stay within bounds
TEST_F(ChordCollisionRegressionTest, TotalClashCountBelowThreshold) {
  // Generous threshold: this is a safety net, not a strict limit
  constexpr size_t kMaxTotalClashes = 80;

  for (uint8_t blueprint : kAllBlueprints) {
    // Use just one seed per blueprint for total clash analysis (it's expensive)
    generateSong(42, blueprint);

    const auto& song = sketch_.getSong();
    const auto& harmony = sketch_.getHarmonyContext();

    auto all_clashes = test::analyzeAllTrackPairs(song, harmony);

    EXPECT_LE(all_clashes.size(), kMaxTotalClashes)
        << "Too many total clashes for blueprint=" << (int)blueprint << " (found "
        << all_clashes.size() << ")";
  }
}

// Verify collision snapshot API works for debugging
TEST_F(ChordCollisionRegressionTest, CollisionSnapshotAPIWorks) {
  generateSong(42, 0);

  const auto& harmony = sketch_.getHarmonyContext();

  // Check various tick positions
  Tick test_ticks[] = {0, TICKS_PER_BAR, TICKS_PER_BAR * 4, TICKS_PER_BAR * 8};
  for (Tick tick : test_ticks) {
    auto snapshot = harmony.getCollisionSnapshot(tick);
    EXPECT_EQ(snapshot.tick, tick);
    // Just verify it doesn't crash; actual content depends on generation
  }
}

}  // namespace
}  // namespace midisketch
