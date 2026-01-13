/**
 * @file chord_track.h
 * @brief Chord track generation with intelligent voicing and voice leading.
 */

#ifndef MIDISKETCH_TRACK_CHORD_TRACK_H
#define MIDISKETCH_TRACK_CHORD_TRACK_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/track_generation_context.h"
#include "core/types.h"
#include "track/vocal_analysis.h"
#include <random>

namespace midisketch {

/// @brief Open voicing subtypes. Drop2=jazz, Drop3=big band, Spread=atmospheric.
enum class OpenVoicingType : uint8_t {
  Drop2,   ///< Drop 2nd voice from top down an octave (jazz standard)
  Drop3,   ///< Drop 3rd voice from top down an octave (big band)
  Spread   ///< Wide intervallic spacing (1-5-10 style, atmospheric)
};

/**
 * @brief Generate chord track using TrackGenerationContext.
 *
 * Uses intelligent voicing selection with voice leading optimization.
 * Supports collision avoidance with bass, aux, and vocal tracks.
 *
 * @param track Target MidiTrack to populate with chord notes
 * @param ctx Generation context containing all parameters
 */
void generateChordTrack(MidiTrack& track, const TrackGenerationContext& ctx);

/**
 * @brief Generate chord track with vocal context.
 *
 * Avoids doubling vocal pitch class and clashing with bass/aux.
 * Falls back to basic generation if ctx.vocal_analysis is not set.
 *
 * @param track Target MidiTrack to populate with chord notes
 * @param ctx Generation context (should include vocal_analysis for best results)
 */
void generateChordTrackWithContext(MidiTrack& track,
                                   const TrackGenerationContext& ctx);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_CHORD_TRACK_H
