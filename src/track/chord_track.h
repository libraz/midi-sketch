#ifndef MIDISKETCH_TRACK_CHORD_TRACK_H
#define MIDISKETCH_TRACK_CHORD_TRACK_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"
#include <random>

namespace midisketch {

// Open voicing subtypes for wider chord spreads (C3 enhancement)
enum class OpenVoicingType : uint8_t {
  Drop2,   // Drop 2nd voice from top down an octave
  Drop3,   // Drop 3rd voice from top down an octave
  Spread   // Wide intervallic spacing (1-5-10 style)
};

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
