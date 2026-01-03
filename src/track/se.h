#ifndef MIDISKETCH_TRACK_SE_H
#define MIDISKETCH_TRACK_SE_H

#include "core/midi_track.h"
#include "core/song.h"

namespace midisketch {

// Generates SE track with section markers and modulation events.
// @param track Target MidiTrack to populate
// @param song Song containing arrangement and modulation info
void generateSETrack(MidiTrack& track, const Song& song);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_SE_H
