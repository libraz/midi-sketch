/**
 * @file isolated_note_resolver.h
 * @brief Resolution logic for melodically isolated notes.
 *
 * A note is "melodically isolated" if both neighbors are far away (>= 7 semitones).
 * This module provides functions to detect and resolve such notes by moving them
 * to pitches that better connect with their neighbors.
 */

#ifndef MIDISKETCH_TRACK_MELODY_ISOLATED_NOTE_RESOLVER_H
#define MIDISKETCH_TRACK_MELODY_ISOLATED_NOTE_RESOLVER_H

#include <cstdint>
#include <vector>

#include "core/types.h"

namespace midisketch {

class IHarmonyContext;

namespace melody {

/// @brief Threshold for melodic isolation (in semitones).
/// A perfect 5th (7 semitones) or larger on both sides feels disconnected.
constexpr int kIsolationThreshold = 7;

/// @brief Check if a note is melodically isolated.
///
/// A note is isolated if the intervals to both neighbors are >= kIsolationThreshold.
///
/// @param prev_pitch Previous note pitch
/// @param curr_pitch Current note pitch
/// @param next_pitch Next note pitch
/// @return true if the current note is isolated
bool isIsolatedNote(int prev_pitch, int curr_pitch, int next_pitch);

/// @brief Find a connecting pitch that reduces isolation.
///
/// Calculates the midpoint between neighbors and snaps to the nearest chord tone.
///
/// @param prev_pitch Previous note pitch
/// @param next_pitch Next note pitch
/// @param chord_degree Chord degree at the note's position
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @return Suggested connecting pitch
int findConnectingPitch(int prev_pitch, int next_pitch, int8_t chord_degree,
                        uint8_t vocal_low, uint8_t vocal_high);

/// @brief Check if a pitch change improves melodic connectivity.
///
/// Returns true if the new pitch reduces at least one interval without
/// making the other worse.
///
/// @param prev_pitch Previous note pitch
/// @param curr_pitch Current note pitch
/// @param next_pitch Next note pitch
/// @param fixed_pitch Proposed fixed pitch
/// @return true if the fix improves connectivity
bool doesFixImproveConnectivity(int prev_pitch, int curr_pitch, int next_pitch, int fixed_pitch);

/// @brief Resolve isolated notes in a note sequence.
///
/// Scans through the notes and adjusts any isolated notes to better connect
/// with their neighbors while maintaining harmonic validity.
///
/// @param notes Notes to process (modified in place)
/// @param harmony Harmony context for chord lookups
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
void resolveIsolatedNotes(std::vector<NoteEvent>& notes, const IHarmonyContext& harmony,
                          uint8_t vocal_low, uint8_t vocal_high);

}  // namespace melody
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_ISOLATED_NOTE_RESOLVER_H
