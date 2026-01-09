/**
 * @file chord_track.h
 * @brief Chord track generation with intelligent voicing and voice leading.
 */

#ifndef MIDISKETCH_TRACK_CHORD_TRACK_H
#define MIDISKETCH_TRACK_CHORD_TRACK_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"
#include "track/vocal_analysis.h"
#include <random>

namespace midisketch {

// Forward declaration
class HarmonyContext;

/// @brief Open voicing subtypes. Drop2=jazz, Drop3=big band, Spread=atmospheric.
enum class OpenVoicingType : uint8_t {
  Drop2,   ///< Drop 2nd voice from top down an octave (jazz standard)
  Drop3,   ///< Drop 3rd voice from top down an octave (big band)
  Spread   ///< Wide intervallic spacing (1-5-10 style, atmospheric)
};

/**
 * @brief Generate chord track with intelligent voicing selection.
 * @param track Target MidiTrack to populate with chord notes
 * @param song Song containing arrangement and section information
 * @param params Generation parameters (key, chord_id, mood, extensions)
 * @param rng Random number generator for voicing selection tiebreakers
 * @param harmony HarmonyContext for provenance tracking
 * @param bass_track Optional bass track for collision avoidance
 */
void generateChordTrack(MidiTrack& track, const Song& song,
                        const GeneratorParams& params,
                        std::mt19937& rng,
                        const HarmonyContext& harmony,
                        const MidiTrack* bass_track = nullptr,
                        const MidiTrack* aux_track = nullptr);

/**
 * @brief Generate chord track adapted to vocal-first context.
 *
 * Avoids doubling vocal pitch class and clashing with bass/aux.
 *
 * @param track Target MidiTrack to populate with chord notes
 * @param song Song containing arrangement and section information
 * @param params Generation parameters (key, chord_id, mood, extensions)
 * @param rng Random number generator for voicing selection
 * @param bass_track Bass track for collision avoidance
 * @param vocal_analysis Pre-computed analysis of the vocal track
 * @param aux_track Optional aux track for clash avoidance
 * @param harmony HarmonyContext for provenance tracking
 */
void generateChordTrackWithContext(MidiTrack& track, const Song& song,
                                   const GeneratorParams& params,
                                   std::mt19937& rng,
                                   const MidiTrack* bass_track,
                                   const VocalAnalysis& vocal_analysis,
                                   const MidiTrack* aux_track,
                                   const HarmonyContext& harmony);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_CHORD_TRACK_H
