/**
 * @file arpeggio.h
 * @brief Arpeggio track generation for synth-driven compositions.
 */

#ifndef MIDISKETCH_TRACK_ARPEGGIO_H
#define MIDISKETCH_TRACK_ARPEGGIO_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"
#include <random>

namespace midisketch {

class HarmonyContext;

/**
 * @brief Generate arpeggio track following chord progression.
 * @param track Target MidiTrack to populate with arpeggio notes
 * @param song Song containing arrangement and chord information
 * @param params Generation parameters (mood affects pattern style)
 * @param rng Random number generator for pattern variation
 * @param harmony HarmonyContext for provenance tracking (optional)
 * @note Uses GM Program 81 (Saw Lead). May be empty for some moods.
 */
void generateArpeggioTrack(MidiTrack& track, const Song& song,
                           const GeneratorParams& params, std::mt19937& rng,
                           const HarmonyContext& harmony);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_ARPEGGIO_H
