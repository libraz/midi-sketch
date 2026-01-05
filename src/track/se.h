#ifndef MIDISKETCH_TRACK_SE_H
#define MIDISKETCH_TRACK_SE_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"
#include <random>

namespace midisketch {

// Generates SE track with section markers and modulation events.
// @param track Target MidiTrack to populate
// @param song Song containing arrangement and modulation info
void generateSETrack(MidiTrack& track, const Song& song);

// Generates SE track with call support.
// @param track Target MidiTrack to populate
// @param song Song containing arrangement and modulation info
// @param call_enabled Whether call feature is enabled
// @param call_notes_enabled Whether to output calls as notes
// @param intro_chant IntroChant pattern
// @param mix_pattern MixPattern
// @param call_density CallDensity for normal sections
// @param rng Random number generator for call timing variation
void generateSETrack(
    MidiTrack& track,
    const Song& song,
    bool call_enabled,
    bool call_notes_enabled,
    IntroChant intro_chant,
    MixPattern mix_pattern,
    CallDensity call_density,
    std::mt19937& rng);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_SE_H
