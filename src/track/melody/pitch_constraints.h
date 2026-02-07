/**
 * @file pitch_constraints.h
 * @brief Pitch constraints for melody generation (downbeat chord tones, avoid notes).
 */

#ifndef MIDISKETCH_TRACK_MELODY_PITCH_CONSTRAINTS_H
#define MIDISKETCH_TRACK_MELODY_PITCH_CONSTRAINTS_H

#include <cstdint>
#include <random>

#include "core/pitch_utils.h"
#include "core/types.h"

namespace midisketch {
namespace melody {

/// @brief Check if a tick position is on a downbeat (beat 1 of a bar).
/// @param tick Tick position
/// @return true if within first quarter of beat 1
bool isDownbeat(Tick tick);

/// @brief Check if a tick position is on a strong beat (beat 1 or 3).
/// @param tick Tick position
/// @return true if on beat 1 or 3
bool isStrongBeat(Tick tick);

/// @brief Enforce downbeat chord-tone constraint.
///
/// On beat 1 of each bar, ensures the pitch is a chord tone to establish
/// clear harmonic grounding. For other positions, returns the pitch unchanged.
///
/// @param pitch Current pitch candidate
/// @param tick Tick position for downbeat check
/// @param chord_degree Current chord degree
/// @param prev_pitch Previous pitch (for direction preservation)
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @param disable_singability If true, use simple nearest chord tone
/// @return Adjusted pitch (chord tone if on downbeat)
int enforceDownbeatChordTone(int pitch, Tick tick, int8_t chord_degree, int prev_pitch,
                              uint8_t vocal_low, uint8_t vocal_high,
                              bool disable_singability = false);

/// @brief Find best chord tone preserving melodic direction.
///
/// When adjusting to a chord tone, this function tries to preserve the
/// intended melodic direction (up/down) from the previous pitch.
///
/// @param target_pitch Target pitch to adjust
/// @param prev_pitch Previous pitch (reference for direction)
/// @param chord_degree Current chord degree
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @param max_interval Maximum interval allowed (0 = no limit)
/// @return Best chord tone preserving direction intent
int findBestChordTonePreservingDirection(int target_pitch, int prev_pitch, int8_t chord_degree,
                                          uint8_t vocal_low, uint8_t vocal_high,
                                          int max_interval = 0);

/// @brief Bias downbeat pitch toward guide tones (3rd/7th) with given probability.
///
/// On strong beats (beats 1 and 3), with probability guide_tone_rate/100,
/// snaps the pitch to the nearest guide tone instead of any chord tone.
/// If no guide tone is in range, falls back to the original pitch.
///
/// @param pitch Current pitch candidate (should already be a chord tone from enforceDownbeatChordTone)
/// @param tick Tick position for strong beat check
/// @param chord_degree Current chord degree
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @param guide_tone_rate Probability percentage (0-100)
/// @param rng Random number generator
/// @return Adjusted pitch (guide tone if selected)
int enforceGuideToneOnDownbeat(int pitch, Tick tick, int8_t chord_degree,
                                uint8_t vocal_low, uint8_t vocal_high,
                                uint8_t guide_tone_rate, std::mt19937& rng);

/// @brief Enforce avoid note constraint against chord tones.
///
/// Checks if the pitch forms a dissonant interval (tritone, minor 2nd) with
/// any chord tone and adjusts to the nearest safe chord tone if so.
///
/// @param pitch Current pitch candidate
/// @param chord_degree Current chord degree
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @return Adjusted pitch (safe from avoid intervals)
int enforceAvoidNoteConstraint(int pitch, int8_t chord_degree,
                                uint8_t vocal_low, uint8_t vocal_high);

/// @brief Enforce maximum interval constraint between consecutive notes.
///
/// If the interval between new_pitch and prev_pitch exceeds max_interval,
/// adjusts new_pitch to the nearest chord tone within the allowed interval.
///
/// @param new_pitch New pitch candidate
/// @param prev_pitch Previous pitch
/// @param chord_degree Current chord degree
/// @param max_interval Maximum allowed interval in semitones
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @param tessitura Optional tessitura range for preference
/// @return Adjusted pitch within interval constraint
int enforceMaxIntervalConstraint(int new_pitch, int prev_pitch, int8_t chord_degree,
                                  int max_interval, uint8_t vocal_low, uint8_t vocal_high,
                                  const TessituraRange* tessitura = nullptr);

/// @brief Apply leap preparation constraint.
///
/// After short notes, large leaps are restricted because singers need time
/// to prepare for pitch changes. This constraint limits leaps after notes
/// shorter than the threshold.
///
/// @param new_pitch New pitch candidate
/// @param prev_pitch Previous pitch
/// @param prev_duration Previous note duration in ticks
/// @param chord_degree Current chord degree
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @param tessitura Optional tessitura range
/// @return Adjusted pitch respecting leap preparation
int applyLeapPreparationConstraint(int new_pitch, int prev_pitch, Tick prev_duration,
                                    int8_t chord_degree, uint8_t vocal_low, uint8_t vocal_high,
                                    const TessituraRange* tessitura = nullptr);

/// @brief Encourage leap after long notes.
///
/// After long notes (>= 1 beat), static pitches can feel anticlimactic.
/// This function probabilistically encourages larger intervals.
///
/// @param new_pitch New pitch candidate
/// @param prev_pitch Previous pitch
/// @param prev_duration Previous note duration in ticks
/// @param chord_degree Current chord degree
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @param rng Random number generator
/// @return Possibly adjusted pitch with encouraged leap
int encourageLeapAfterLongNote(int new_pitch, int prev_pitch, Tick prev_duration,
                                int8_t chord_degree, uint8_t vocal_low, uint8_t vocal_high,
                                std::mt19937& rng);

}  // namespace melody
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_PITCH_CONSTRAINTS_H
