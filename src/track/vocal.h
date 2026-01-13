/**
 * @file vocal.h
 * @brief Vocal melody track generation with music theory-based patterns.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_H
#define MIDISKETCH_TRACK_VOCAL_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/track_layer.h"
#include "core/types.h"
#include <random>

namespace midisketch {

class IHarmonyContext;

/**
 * @brief Generate a vocal melody track with music theory-based patterns.
 * @param track Target MidiTrack to populate with vocal notes
 * @param song Song containing arrangement structure and modulation info
 * @param params Generation parameters (key, chord_id, mood, vocal range, style)
 * @param rng Random number generator for melodic variation
 * @param motif_track Optional background motif track for range coordination
 * @param harmony_ctx Optional harmony context for dissonance avoidance
 * @param skip_collision_avoidance If true, skip collision checking (vocal-first)
 */
void generateVocalTrack(MidiTrack& track, Song& song,
                        const GeneratorParams& params, std::mt19937& rng,
                        const MidiTrack* motif_track,
                        const IHarmonyContext& harmony,
                        bool skip_collision_avoidance = false);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_H
