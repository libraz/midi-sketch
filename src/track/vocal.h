/**
 * @file vocal.h
 * @brief Vocal melody track generation with music theory-based patterns.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_H
#define MIDISKETCH_TRACK_VOCAL_H

#include "core/midi_track.h"
#include "core/section_types.h"
#include "core/song.h"
#include "core/track_layer.h"
#include "core/types.h"
#include <random>

namespace midisketch {

class IHarmonyContext;

struct CachedRhythmPattern;  // Forward declaration

/**
 * @brief Generate a vocal melody track with music theory-based patterns.
 * @param track Target MidiTrack to populate with vocal notes
 * @param song Song containing arrangement structure and modulation info
 * @param params Generation parameters (key, chord_id, mood, vocal range, style)
 * @param rng Random number generator for melodic variation
 * @param motif_track Optional background motif track for range coordination
 * @param harmony_ctx Optional harmony context for dissonance avoidance
 * @param skip_collision_avoidance If true, skip collision checking (vocal-first)
 * @param drum_grid Optional drum grid for RhythmSync quantization
 * @param rhythm_lock Optional pre-locked rhythm pattern for Orangestar style
 */
void generateVocalTrack(MidiTrack& track, Song& song,
                        const GeneratorParams& params, std::mt19937& rng,
                        const MidiTrack* motif_track,
                        const IHarmonyContext& harmony,
                        bool skip_collision_avoidance = false,
                        const DrumGrid* drum_grid = nullptr,
                        CachedRhythmPattern* rhythm_lock = nullptr);

/**
 * @brief Check if vocal rhythm lock should be used for the given params.
 * @param params Generation parameters
 * @return True if rhythm lock should be applied
 *
 * Rhythm lock is used when:
 * - paradigm is RhythmSync (Orangestar style)
 * - riff_policy is Locked or LockedContour
 */
bool shouldLockVocalRhythm(const GeneratorParams& params);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_H
