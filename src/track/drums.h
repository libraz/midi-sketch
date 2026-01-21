/**
 * @file drums.h
 * @brief Drum track generation for rhythmic foundation.
 */

#ifndef MIDISKETCH_TRACK_DRUMS_H
#define MIDISKETCH_TRACK_DRUMS_H

#include <random>

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"
#include "track/vocal_analysis.h"

namespace midisketch {

/**
 * @brief Generate drum track with style-appropriate patterns.
 * @param track Target MidiTrack to populate with drum events
 * @param song Song containing arrangement and section information
 * @param params Generation parameters (mood affects pattern style)
 * @param rng Random number generator for fill variation
 * @note Drums use MIDI channel 9 (GM standard).
 */
void generateDrumsTrack(MidiTrack& track, const Song& song, const GeneratorParams& params,
                        std::mt19937& rng);

/**
 * @brief Generate drum track with vocal synchronization.
 *
 * When drums_sync_vocal is enabled, kick drums are placed to align with
 * vocal onset positions, creating a "rhythm lock" effect where the groove
 * follows the melody.
 *
 * @param track Target MidiTrack to populate with drum events
 * @param song Song containing arrangement and section information
 * @param params Generation parameters (mood affects pattern style)
 * @param rng Random number generator for fill variation
 * @param vocal_analysis Pre-analyzed vocal track data
 * @note Drums use MIDI channel 9 (GM standard).
 */
void generateDrumsTrackWithVocal(MidiTrack& track, const Song& song, const GeneratorParams& params,
                                 std::mt19937& rng, const VocalAnalysis& vocal_analysis);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_H
