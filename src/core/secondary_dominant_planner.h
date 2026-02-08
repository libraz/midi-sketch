/**
 * @file secondary_dominant_planner.h
 * @brief Pre-registers secondary dominants in harmony context before track generation.
 *
 * Extracted from chord.cpp to ensure secondary dominant registrations are
 * visible to all tracks (including coordinate axis tracks in RhythmSync).
 */

#ifndef MIDISKETCH_CORE_SECONDARY_DOMINANT_PLANNER_H
#define MIDISKETCH_CORE_SECONDARY_DOMINANT_PLANNER_H

#include <random>

#include "core/types.h"

namespace midisketch {

class Arrangement;
struct ChordProgression;
class IHarmonyContext;

/// @brief Plan and register all secondary dominants in harmony context.
///
/// Replicates the secondary dominant decision logic from chord.cpp:
///   1. Section boundary: Chorus preceded by ii/IV/vi → deterministic insertion.
///   2. Within-bar: checkSecondaryDominant() + rollProbability() → RNG-dependent.
///
/// Must be called after harmony initialization but before any track generation
/// so that coordinate axis tracks (Motif in RhythmSync) see the correct chords.
///
/// @param arrangement Section structure
/// @param progression Chord progression
/// @param mood Mood (affects harmonic rhythm / tension)
/// @param rng RNG for probabilistic in-bar decisions
/// @param harmony Harmony context to register secondary dominants into
void planAndRegisterSecondaryDominants(const Arrangement& arrangement,
                                       const ChordProgression& progression,
                                       Mood mood,
                                       std::mt19937& rng,
                                       IHarmonyContext& harmony);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_SECONDARY_DOMINANT_PLANNER_H
