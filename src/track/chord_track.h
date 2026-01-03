#ifndef MIDISKETCH_TRACK_CHORD_TRACK_H
#define MIDISKETCH_TRACK_CHORD_TRACK_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"
#include <random>

namespace midisketch {

// Generates chord track with voicings following chord progression.
// @param track Target MidiTrack to populate
// @param song Song containing arrangement info
// @param params Generation parameters (key, chord_id, mood)
// @param rng Random number generator for chord extensions
// @param bass_track Optional bass track for coordination (voicing selection)
void generateChordTrack(MidiTrack& track, const Song& song,
                        const GeneratorParams& params,
                        std::mt19937& rng,
                        const MidiTrack* bass_track = nullptr);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_CHORD_TRACK_H
