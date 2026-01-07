#ifndef MIDISKETCH_TRACK_VOCAL_H
#define MIDISKETCH_TRACK_VOCAL_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/track_layer.h"
#include "core/types.h"
#include <random>

namespace midisketch {

class HarmonyContext;

// Generates vocal melody track with music theory-based patterns.
// @param track Target MidiTrack to populate
// @param song Song containing arrangement and modulation info
// @param params Generation parameters (key, chord_id, mood, vocal range)
// @param rng Random number generator for variation
// @param motif_track Optional motif track for range coordination (BackgroundMotif)
// @param harmony_ctx Optional harmony context for dissonance avoidance
void generateVocalTrack(MidiTrack& track, Song& song,
                        const GeneratorParams& params, std::mt19937& rng,
                        const MidiTrack* motif_track = nullptr,
                        const HarmonyContext* harmony_ctx = nullptr);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_H
