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

#include "core/modulation_calculator.h"
#include "core/types.h"

namespace midisketch {

class Arrangement;
struct ChordProgression;
class IHarmonyContext;

/// @brief Plan and register all secondary dominants in harmony context.
///
///   1. Section boundary: a Chorus opening on ii/IV/V/vi gets its dominant in
///      the last half-bar before it. The chorus the key changes at always gets
///      one, a dominant of its first chord in the new key: the half-bar still
///      sounds in the old key, so the root is shifted by the modulation.
///   2. Within-bar: checkSecondaryDominant() + rollProbability() → RNG-dependent,
///      capped per section and spaced by a cooldown. Chorus restatement later
///      copies the first statement's chords, so the final timeline can hold
///      more than the cap and closer than the cooldown.
///
/// Must be called after harmony initialization but before any track generation
/// so that coordinate axis tracks (Motif in RhythmSync) see the correct chords.
///
/// @param arrangement Section structure
/// @param progression Chord progression
/// @param mood Mood (affects harmonic rhythm / tension)
/// @param rng RNG for probabilistic in-bar decisions
/// @param harmony Harmony context to register secondary dominants into
/// @param modulation The song's modulation; none by default
void planAndRegisterSecondaryDominants(
    const Arrangement& arrangement, const ChordProgression& progression, Mood mood,
    std::mt19937& rng, IHarmonyContext& harmony,
    const ModulationCalculator::ModulationResult& modulation = {});

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_SECONDARY_DOMINANT_PLANNER_H
