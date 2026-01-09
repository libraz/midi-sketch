/**
 * @file drums.h
 * @brief Drum track generation for rhythmic foundation.
 */

#ifndef MIDISKETCH_TRACK_DRUMS_H
#define MIDISKETCH_TRACK_DRUMS_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"
#include <random>

namespace midisketch {

/**
 * @brief Generate drum track with style-appropriate patterns.
 * @param track Target MidiTrack to populate with drum events
 * @param song Song containing arrangement and section information
 * @param params Generation parameters (mood affects pattern style)
 * @param rng Random number generator for fill variation
 * @note Drums use MIDI channel 9 (GM standard).
 */
void generateDrumsTrack(MidiTrack& track, const Song& song,
                        const GeneratorParams& params, std::mt19937& rng);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_H
