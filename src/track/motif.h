/**
 * @file motif.h
 * @brief Background motif track generation.
 */

#ifndef MIDISKETCH_TRACK_MOTIF_H
#define MIDISKETCH_TRACK_MOTIF_H

#include <random>

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"

namespace midisketch {

class IHarmonyContext;
struct MotifContext;

// Generates background motif track for BackgroundMotif composition style.
// The motif is a short repeating pattern that becomes the song's main focus.
// In MelodyLead mode with vocal_ctx, coordinates with vocal for response patterns.
// @param track Target MidiTrack to populate
// @param song Song containing arrangement info
// @param params Generation parameters
// @param rng Random number generator
// @param harmony HarmonyContext for provenance tracking (optional)
// @param vocal_ctx Optional vocal context for MelodyLead coordination (nullptr for BGM modes)
void generateMotifTrack(MidiTrack& track, Song& song, const GeneratorParams& params,
                        std::mt19937& rng, const IHarmonyContext& harmony,
                        const MotifContext* vocal_ctx = nullptr);

// Generates a single motif pattern (one cycle).
// @param params Generation parameters
// @param rng Random number generator
// @returns Vector of NoteEvents for one motif cycle
std::vector<NoteEvent> generateMotifPattern(const GeneratorParams& params, std::mt19937& rng);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MOTIF_H
