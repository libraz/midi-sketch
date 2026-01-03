#ifndef MIDISKETCH_TRACK_MOTIF_H
#define MIDISKETCH_TRACK_MOTIF_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"
#include <random>

namespace midisketch {

// Generates background motif track for BackgroundMotif composition style.
// The motif is a short repeating pattern that becomes the song's main focus.
// @param track Target MidiTrack to populate
// @param song Song containing arrangement info
// @param params Generation parameters
// @param rng Random number generator
void generateMotifTrack(MidiTrack& track, Song& song,
                        const GeneratorParams& params, std::mt19937& rng);

// Generates a single motif pattern (one cycle).
// @param params Generation parameters
// @param rng Random number generator
// @returns Vector of NoteEvents for one motif cycle
std::vector<NoteEvent> generateMotifPattern(const GeneratorParams& params,
                                             std::mt19937& rng);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MOTIF_H
