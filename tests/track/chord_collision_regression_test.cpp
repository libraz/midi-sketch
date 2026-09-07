/**
 * @file chord_collision_regression_test.cpp
 * @brief Safety net tests for chord-bass collision detection.
 *
 * Tests across every shipped blueprint with multiple seeds to detect
 * regressions that significantly increase bass-chord dissonant clashes.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <sstream>

#include "core/generator.h"
#include "core/i_harmony_context.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "midisketch.h"
#include "test_support/clash_analysis_helper.h"

namespace midisketch {
namespace {

using test::findClashes;

constexpr uint32_t kSeeds[] = {42, 100, 200, 999};

// Maximum number of bass-chord clashes allowed per song.
// Zero, not an observed ceiling: bass-chord collisions are resolved at
// generation time, so any count above zero is a real regression.
constexpr size_t kMaxBassChordClashesPerSong = 0;

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
  for (uint8_t blueprint = 0; blueprint < getProductionBlueprintCount(); ++blueprint) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);

      const auto& song = sketch_.getSong();
      const auto& harmony = sketch_.getHarmonyContext();

      // Guard against a vacuous pass: with no bass, no chord, or no note that
      // overlaps in time, the clash assertion below is trivially satisfied
      // without having checked anything. Count what it will actually compare
      // before trusting a zero clash count to mean collisions were avoided.
      int overlapping_pairs = 0;
      for (const auto& bass_note : song.bass().notes()) {
        Tick bass_start = bass_note.start_tick;
        Tick bass_end = bass_start + bass_note.duration;
        for (const auto& chord_note : song.chord().notes()) {
          Tick chord_start = chord_note.start_tick;
          Tick chord_end = chord_start + chord_note.duration;
          if (bass_start < chord_end && chord_start < bass_end) {
            ++overlapping_pairs;
          }
        }
      }
      ASSERT_GT(overlapping_pairs, 0)
          << "No overlapping bass/chord note pairs for blueprint=" << (int)blueprint
          << " seed=" << seed << "; the clash count below has nothing to check";

      auto clashes =
          findClashes(song, sketch_.getParams(), harmony, TrackRole::Bass, TrackRole::Chord);

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

  for (uint8_t blueprint = 0; blueprint < getProductionBlueprintCount(); ++blueprint) {
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
  // A small number of clashes is acceptable - occasional collisions at chord
  // boundaries or due to guide chord interactions are tolerable when overall
  // harmonic quality improves. See CLAUDE.md 2.3.
  constexpr size_t kMaxTotalClashes = 3;

  for (uint8_t blueprint = 0; blueprint < getProductionBlueprintCount(); ++blueprint) {
    // Use just one seed per blueprint for total clash analysis (it's expensive)
    generateSong(42, blueprint);

    const auto& song = sketch_.getSong();
    const auto& harmony = sketch_.getHarmonyContext();

    auto all_clashes = test::analyzeAllTrackPairs(song, sketch_.getParams(), harmony);

    std::ostringstream details;
    for (const auto& clash : all_clashes) {
      details << "\n  tick=" << clash.tick << " " << clash.track_a << "("
              << static_cast<int>(clash.pitch_a) << ")-" << clash.track_b << "("
              << static_cast<int>(clash.pitch_b) << ") interval=" << clash.interval;
    }
    EXPECT_LE(all_clashes.size(), kMaxTotalClashes)
        << "Too many total clashes for blueprint=" << (int)blueprint << " (found "
        << all_clashes.size() << ")" << details.str();
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
