/**
 * @file leap_resolution.h
 * @brief Leap resolution logic for melody generation.
 *
 * Implements multi-note leap resolution: when a large leap occurs,
 * following notes prefer stepwise motion in the opposite direction
 * to create natural melodic flow.
 */

#ifndef MIDISKETCH_TRACK_MELODY_LEAP_RESOLUTION_H
#define MIDISKETCH_TRACK_MELODY_LEAP_RESOLUTION_H

#include <cstdint>
#include <random>
#include <vector>

// LeapResolutionState is defined in melody_utils.h
#include "track/melody/melody_utils.h"

namespace midisketch {
namespace melody {

/// @brief Threshold for what constitutes a "leap" (in semitones).
/// A perfect 4th (5 semitones) or larger is considered a leap.
constexpr int kLeapThreshold = 5;

/// @brief Threshold for leap-after-reversal rule (in semitones).
/// A major 3rd (4 semitones) or larger triggers reversal preference.
constexpr int kLeapReversalThreshold = 4;

/// @brief Find the best stepwise resolution pitch.
///
/// When leap resolution is pending, find a chord tone that provides
/// stepwise motion (1-3 semitones) in the resolution direction.
///
/// @param current_pitch Current pitch
/// @param chord_tones Chord tone pitch classes (0-11)
/// @param resolution_direction +1 for up, -1 for down
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @return Best stepwise pitch, or -1 if none found
int findStepwiseResolutionPitch(int current_pitch, const std::vector<int>& chord_tones,
                                 int resolution_direction, uint8_t vocal_low, uint8_t vocal_high);

/// @brief Apply leap-after-reversal rule.
///
/// After a large leap (4+ semitones), the melody should prefer step motion
/// in the opposite direction. This is a fundamental vocal principle:
/// singers need to "recover" after jumps.
///
/// Reversal probability is context-dependent:
/// - Section type: Bridge/Verse favor resolution, Chorus allows sustained peaks
/// - Phrase position: phrase endings (>0.8) modify probability for cadence control
///
/// @param new_pitch Proposed new pitch
/// @param current_pitch Current pitch
/// @param prev_interval Interval from previous note to current note (signed)
/// @param chord_tones Chord tone pitch classes (0-11)
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @param prefer_stepwise If true, force 100% stepwise (for IdolKawaii)
/// @param rng Random number generator
/// @param section_type_int SectionType as int (-1 = unknown, uses default)
/// @param phrase_position Position in phrase 0.0-1.0 (-1 = unknown)
/// @return Adjusted pitch (may be changed to reversal pitch)
int applyLeapReversalRule(int new_pitch, int current_pitch, int prev_interval,
                          const std::vector<int>& chord_tones, uint8_t vocal_low, uint8_t vocal_high,
                          bool prefer_stepwise, std::mt19937& rng,
                          int8_t section_type_int = -1, float phrase_position = -1.0f);

}  // namespace melody
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_LEAP_RESOLUTION_H
