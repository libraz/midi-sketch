#ifndef MIDISKETCH_TRACK_DRUMS_H
#define MIDISKETCH_TRACK_DRUMS_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"
#include <random>

namespace midisketch {

// Generates drums track with style-appropriate patterns.
// @param track Target MidiTrack to populate
// @param song Song containing arrangement info
// @param params Generation parameters (mood)
// @param rng Random number generator for variation
void generateDrumsTrack(MidiTrack& track, const Song& song,
                        const GeneratorParams& params, std::mt19937& rng);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_H
