/**
 * @file post_processor.h
 * @brief Track post-processing for humanization and dynamics.
 */

#ifndef MIDISKETCH_CORE_POST_PROCESSOR_H
#define MIDISKETCH_CORE_POST_PROCESSOR_H

#include <random>
#include <vector>

#include "core/midi_track.h"
#include "core/types.h"

namespace midisketch {

// Applies post-processing effects to generated tracks.
// Handles humanization (timing/velocity variation) and transition dynamics.
class PostProcessor {
 public:
  // Humanization parameters.
  struct HumanizeParams {
    float timing = 0.5f;    // Timing variation amount (0.0-1.0)
    float velocity = 0.5f;  // Velocity variation amount (0.0-1.0)
  };

  // Applies humanization to melodic tracks.
  // @param tracks Vector of track pointers to process
  // @param params Humanization parameters
  // @param rng Random number generator
  static void applyHumanization(std::vector<MidiTrack*>& tracks, const HumanizeParams& params,
                                std::mt19937& rng);

  // Fixes vocal overlaps that may be introduced by humanization.
  // Singers can only sing one note at a time.
  // @param vocal_track Vocal track to fix
  static void fixVocalOverlaps(MidiTrack& vocal_track);

 private:
  // Returns true if the tick position is on a strong beat (beats 1 or 3 in 4/4).
  static bool isStrongBeat(Tick tick);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_POST_PROCESSOR_H
