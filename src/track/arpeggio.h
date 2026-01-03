#ifndef MIDISKETCH_TRACK_ARPEGGIO_H
#define MIDISKETCH_TRACK_ARPEGGIO_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"
#include <random>

namespace midisketch {

// Generates arpeggio track for synth-driven compositions.
// The arpeggio follows the chord progression with configurable pattern and speed.
// @param track Target MidiTrack to populate
// @param song Song containing arrangement and chord info
// @param params Generation parameters
// @param rng Random number generator
void generateArpeggioTrack(MidiTrack& track, const Song& song,
                           const GeneratorParams& params, std::mt19937& rng);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_ARPEGGIO_H
