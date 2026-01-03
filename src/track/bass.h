#ifndef MIDISKETCH_TRACK_BASS_H
#define MIDISKETCH_TRACK_BASS_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"

namespace midisketch {

// Generates bass track with root notes following chord progression.
// @param track Target MidiTrack to populate
// @param song Song containing arrangement info
// @param params Generation parameters (key, chord_id, mood)
void generateBassTrack(MidiTrack& track, const Song& song,
                       const GeneratorParams& params);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_BASS_H
