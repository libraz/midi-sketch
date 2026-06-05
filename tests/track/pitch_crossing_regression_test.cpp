/**
 * @file pitch_crossing_regression_test.cpp
 * @brief Regression tests for accompaniment-above-vocal pitch crossings.
 *
 * Mirrors scripts/check_pitch_crossing.py: an accompaniment note (Motif, Aux,
 * Chord, Arpeggio) that temporally overlaps a vocal note must not sound ABOVE
 * it (the vocal owns the top register in pop arrangement).
 *
 * The specific seeds below reproduced violations before the June 2026 fixes:
 * - RhythmLock (BP1) seed 10: the motif-vocal dissonance pass raised motif C4
 *   to A4 over a D4 vocal because the strict register [MOTIF_LOW, ceiling]
 *   contained only clashing chord tones (pinched range). Fixed by relaxing the
 *   resolution floor one octave below MOTIF_LOW in resolveMotifAboveVocal.
 * - IdolStandard (BP4) seed 8: the aux monotony tracker raised A4 to C5 over
 *   an A4 vocal because only the section-level ceiling (vocal HIGHEST pitch)
 *   was applied. Fixed by a per-onset vocal ceiling in the aux note loop.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/preset_data.h"
#include "core/types.h"
#include "midisketch.h"

namespace midisketch {
namespace {

struct Crossing {
  std::string track;
  Tick tick;
  uint8_t pitch;
  uint8_t vocal_pitch;
};

// Count accompaniment notes sounding above a temporally overlapping vocal
// note (threshold 0, same criterion as scripts/check_pitch_crossing.py).
std::vector<Crossing> findCrossings(const Song& song) {
  std::vector<Crossing> crossings;
  const auto& vocal_notes = song.vocal().notes();
  if (vocal_notes.empty()) return crossings;

  const std::pair<const MidiTrack*, const char*> tracks[] = {
      {&song.motif(), "Motif"},
      {&song.aux(), "Aux"},
      {&song.chord(), "Chord"},
      {&song.arpeggio(), "Arpeggio"},
  };

  for (const auto& [track, name] : tracks) {
    for (const auto& note : track->notes()) {
      Tick note_end = note.start_tick + note.duration;
      for (const auto& v_note : vocal_notes) {
        Tick v_end = v_note.start_tick + v_note.duration;
        if (note.start_tick < v_end && note_end > v_note.start_tick && note.note > v_note.note) {
          crossings.push_back({name, note.start_tick, note.note, v_note.note});
          break;
        }
      }
    }
  }
  return crossings;
}

std::string describeCrossings(const std::vector<Crossing>& crossings) {
  std::string out;
  for (const auto& c : crossings) {
    out += c.track + " tick " + std::to_string(c.tick) + " pitch " + std::to_string(c.pitch) +
           " above vocal " + std::to_string(c.vocal_pitch) + "\n";
  }
  return out;
}

class PitchCrossingRegressionTest : public ::testing::Test {
 protected:
  void generateSong(uint32_t seed, uint8_t style, uint8_t chord, uint8_t blueprint) {
    SongConfig config = createDefaultSongConfig(style);
    config.seed = seed;
    config.chord_progression_id = chord;
    config.blueprint_id = blueprint;
    sketch_.generateFromConfig(config);
  }

  MidiSketch sketch_;
};

// RhythmLock pinched-range regression: motif raised above the vocal because
// no consonant chord tone existed in [MOTIF_LOW, vocal_floor].
TEST_F(PitchCrossingRegressionTest, RhythmLockMotifStaysAtOrBelowVocal) {
  generateSong(/*seed=*/10, /*style=*/0, /*chord=*/0, /*blueprint=*/1);
  auto crossings = findCrossings(sketch_.getSong());
  EXPECT_TRUE(crossings.empty()) << describeCrossings(crossings);
}

// IdolStandard aux regression: monotony tracker raised aux above a locally
// lower vocal note (section-level ceiling only).
TEST_F(PitchCrossingRegressionTest, IdolStandardAuxStaysAtOrBelowVocal) {
  generateSong(/*seed=*/8, /*style=*/0, /*chord=*/0, /*blueprint=*/4);
  auto crossings = findCrossings(sketch_.getSong());
  EXPECT_TRUE(crossings.empty()) << describeCrossings(crossings);
}

// Broader safety net across the blueprints that dominated the residual
// violations (RhythmSync BP1/5/7, MelodyDriven BP2/4) and the seeds that
// reproduced them.
TEST_F(PitchCrossingRegressionTest, NoCrossingsAcrossProblemBlueprints) {
  constexpr uint32_t kSeeds[] = {1, 4, 6, 7, 10};
  constexpr uint8_t kBlueprints[] = {1, 2, 4, 5, 7};

  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, /*style=*/0, /*chord=*/0, blueprint);
      auto crossings = findCrossings(sketch_.getSong());
      EXPECT_TRUE(crossings.empty())
          << "blueprint=" << static_cast<int>(blueprint) << " seed=" << seed << "\n"
          << describeCrossings(crossings);
    }
  }
}

}  // namespace
}  // namespace midisketch
