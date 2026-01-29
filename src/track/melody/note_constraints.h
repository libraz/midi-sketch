/**
 * @file note_constraints.h
 * @brief Note constraints for melody generation (consecutive notes, chord tones).
 */

#ifndef MIDISKETCH_TRACK_MELODY_NOTE_CONSTRAINTS_H
#define MIDISKETCH_TRACK_MELODY_NOTE_CONSTRAINTS_H

#include <cstdint>
#include <random>
#include <vector>

#include "core/types.h"

namespace midisketch {
namespace melody {

/// @brief State for tracking consecutive same-note repetitions.
///
/// Used to implement J-POP style probability curve where repeated notes
/// become progressively less likely:
///   2 notes: 85% allow (very common in hooks)
///   3 notes: 50% allow (rhythmic emphasis)
///   4 notes: 25% allow (occasional effect)
///   5+ notes: 5% allow (rare, intentional)
struct ConsecutiveSameNoteTracker {
  int count = 0;  ///< Number of consecutive same pitches

  /// @brief Reset counter (call when pitch changes).
  void reset() { count = 0; }

  /// @brief Increment counter (call when same pitch).
  void increment() { ++count; }

  /// @brief Get allow probability based on current count.
  /// @return Probability (0.0-1.0) of allowing another same note
  float getAllowProbability() const;

  /// @brief Check if movement should be forced.
  /// @param rng Random number generator
  /// @return true if forced movement needed
  bool shouldForceMovement(std::mt19937& rng) const;
};

/// @brief Find nearest chord tone different from current pitch.
///
/// When consecutive same notes exceed the probability threshold, this finds
/// the closest chord tone that differs from the current pitch.
///
/// @param current_pitch Current pitch to move away from
/// @param chord_degree Chord degree for chord tone lookup
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @param max_interval Maximum interval from current pitch (0 = no limit)
/// @return New pitch (different chord tone), or current if none found
int findNearestDifferentChordTone(int current_pitch, int8_t chord_degree,
                                   uint8_t vocal_low, uint8_t vocal_high,
                                   int max_interval = 0);

/// @brief Check if pitch is a chord tone.
/// @param pitch_pc Pitch class (0-11)
/// @param chord_degree Chord degree
/// @return true if pitch is a chord tone
bool isChordTone(int pitch_pc, int8_t chord_degree);

/// @brief Apply consecutive same note constraint.
///
/// Convenience function that combines tracking and forced movement.
/// Call this after determining the tentative next pitch.
///
/// @param[in,out] pitch Pitch to potentially modify
/// @param[in,out] tracker Consecutive note tracker
/// @param prev_pitch Previous pitch (for comparison)
/// @param chord_degree Current chord degree
/// @param vocal_low Minimum pitch
/// @param vocal_high Maximum pitch
/// @param max_interval Maximum interval constraint
/// @param rng Random number generator
/// @return true if pitch was modified
bool applyConsecutiveSameNoteConstraint(int& pitch, ConsecutiveSameNoteTracker& tracker,
                                         int prev_pitch, int8_t chord_degree,
                                         uint8_t vocal_low, uint8_t vocal_high,
                                         int max_interval, std::mt19937& rng);

}  // namespace melody
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_NOTE_CONSTRAINTS_H
