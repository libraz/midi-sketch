#ifndef MIDISKETCH_CORE_CHORD_UTILS_H
#define MIDISKETCH_CORE_CHORD_UTILS_H

#include "core/pitch_utils.h"  // For TessituraRange
#include <array>
#include <cstdint>
#include <vector>

namespace midisketch {

// ============================================================================
// Constants
// ============================================================================

// Scale degree to pitch class offset (C major reference)
// C=0, D=2, E=4, F=5, G=7, A=9, B=11
constexpr int DEGREE_TO_PITCH_CLASS[7] = {0, 2, 4, 5, 7, 9, 11};

// ============================================================================
// ChordTones
// ============================================================================

// Chord tones as pitch classes (0-11, semitones from C)
struct ChordTones {
  std::array<int, 5> pitch_classes;  // Pitch classes (0-11), -1 = unused
  uint8_t count;                     // Number of chord tones
};

// Get chord tones as pitch classes for a chord built on given scale degree.
// Uses actual chord intervals from chord.cpp for accuracy.
// @param degree Scale degree (0-6 for I-vii)
// @returns ChordTones struct with pitch classes
ChordTones getChordTones(int8_t degree);

// Get pitch classes for chord tones of a degree as a vector.
// @param degree Scale degree (0-6 for I-vii)
// @returns Vector of pitch classes (0-11)
std::vector<int> getChordTonePitchClasses(int8_t degree);

// ============================================================================
// Nearest Chord Tone Functions
// ============================================================================

// Get nearest chord tone pitch to a given pitch.
// Returns the absolute MIDI pitch of the nearest chord tone.
// @param pitch Target MIDI pitch
// @param degree Scale degree of the chord
// @returns Nearest chord tone pitch
int nearestChordTonePitch(int pitch, int8_t degree);

// Find the closest chord tone to target within max_interval of prev_pitch.
// Optionally prefers pitches within the tessitura range.
// @param target_pitch Desired target pitch
// @param prev_pitch Previous pitch (-1 if none)
// @param chord_degree Scale degree of current chord
// @param max_interval Maximum allowed interval from prev_pitch
// @param range_low Minimum allowed pitch
// @param range_high Maximum allowed pitch
// @param tessitura Optional tessitura for preference scoring (can be nullptr)
// @returns Closest chord tone pitch within constraints
int nearestChordToneWithinInterval(int target_pitch, int prev_pitch,
                                   int8_t chord_degree, int max_interval,
                                   int range_low, int range_high,
                                   const TessituraRange* tessitura = nullptr);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_CHORD_UTILS_H
