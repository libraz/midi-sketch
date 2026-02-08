/**
 * @file chord_utils.h
 * @brief Chord voicing utilities and tone helpers.
 */

#ifndef MIDISKETCH_CORE_CHORD_UTILS_H
#define MIDISKETCH_CORE_CHORD_UTILS_H

#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include "core/pitch_utils.h"  // For TessituraRange

namespace midisketch {

// ============================================================================
// ChordTones
// ============================================================================
// Note: Use SCALE from pitch_utils.h for degree to pitch class conversion.
// SCALE[degree] gives the pitch class offset for diatonic degrees (0-6).

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
// Guide Tone Functions
// ============================================================================

/// @brief Get guide tone pitch classes (3rd and 7th) for a chord degree.
///
/// Guide tones define harmonic quality and are priorities for melodic voice leading.
/// The 3rd determines major/minor quality, the 7th adds tension.
/// For triads without an explicit 7th, the diatonic 7th is inferred.
///
/// @param degree Scale degree (0-based: 0=I, 1=ii, 2=iii, 3=IV, 4=V, 5=vi, 6=vii)
/// @return Vector of pitch classes (mod 12) for 3rd and 7th of the chord
std::vector<int> getGuideTonePitchClasses(int8_t degree);

// ============================================================================
// Scale Tone Functions
// ============================================================================

// Get all pitch classes in the major scale of the given key.
// @param key Current key (0-11, 0=C)
// @returns Vector of 7 pitch classes (0-11)
std::vector<int> getScalePitchClasses(uint8_t key);

// Get available tension pitch classes for a chord degree.
// Returns 9th, 11th, 13th tensions that work over this chord.
// @param degree Scale degree (0-6 for I-vii)
// @returns Vector of tension pitch classes (0-11)
std::vector<int> getAvailableTensionPitchClasses(int8_t degree);

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
int nearestChordToneWithinInterval(int target_pitch, int prev_pitch, int8_t chord_degree,
                                   int max_interval, int range_low, int range_high,
                                   const TessituraRange* tessitura = nullptr);

// ============================================================================
// ChordToneHelper - Unified chord tone operations
// ============================================================================

/**
 * @brief Helper class for chord tone operations with a specific degree.
 *
 * Consolidates common chord tone checking patterns like `pitch % 12` and
 * `std::find()` into a reusable class.
 *
 * Usage:
 * @code
 * ChordToneHelper helper(degree);
 * if (helper.isChordTone(pitch)) { ... }
 * uint8_t nearest = helper.nearestInRange(pitch, BASS_LOW, BASS_HIGH);
 * @endcode
 */
class ChordToneHelper {
 public:
  /**
   * @brief Construct helper for a specific chord degree.
   * @param degree Scale degree (0-6 for I-vii)
   */
  explicit ChordToneHelper(int8_t degree);

  /**
   * @brief Check if a MIDI pitch is a chord tone.
   * @param pitch MIDI pitch (0-127)
   * @return true if pitch class matches any chord tone
   */
  bool isChordTone(uint8_t pitch) const;

  /**
   * @brief Check if a pitch class (0-11) is a chord tone.
   * @param pitch_class Pitch class (0-11)
   * @return true if pitch class matches any chord tone
   */
  bool isChordTonePitchClass(int pitch_class) const;

  /**
   * @brief Get the nearest chord tone to the given pitch.
   * @param pitch MIDI pitch (0-127)
   * @return Nearest chord tone pitch
   */
  uint8_t nearestChordTone(uint8_t pitch) const;

  /**
   * @brief Get the nearest chord tone within a pitch range.
   * @param pitch Target MIDI pitch
   * @param low Minimum allowed pitch
   * @param high Maximum allowed pitch
   * @return Nearest chord tone within range, or pitch if none found
   */
  uint8_t nearestInRange(uint8_t pitch, uint8_t low, uint8_t high) const;

  /**
   * @brief Get all chord tone pitches within a range.
   * @param low Minimum pitch
   * @param high Maximum pitch
   * @return Vector of chord tone pitches in the range
   */
  std::vector<uint8_t> allInRange(uint8_t low, uint8_t high) const;

  /**
   * @brief Get the root pitch class (0-11) for this chord.
   * @return Root pitch class
   */
  int rootPitchClass() const;

  /**
   * @brief Get the chord tones as pitch classes.
   * @return Vector of pitch classes (0-11)
   */
  const std::vector<int>& pitchClasses() const { return pitch_classes_; }

 private:
  int8_t degree_;
  int root_pc_;
  std::vector<int> pitch_classes_;
};

// ============================================================================
// Tritone Detection
// ============================================================================

/// @brief Check if a pitch forms a tritone interval with any chord pitch class.
/// @param pitch_pc Pitch class to check (0-11)
/// @param chord_pcs Vector of chord pitch classes
/// @return true if any interval is a tritone (6 semitones)
bool hasTritoneWithChord(int pitch_pc, const std::vector<int>& chord_pcs);

// ============================================================================
// Diatonic Fifth Utilities
// ============================================================================

class IHarmonyContext;
enum class TrackRole : uint8_t;

/// @brief Get diatonic 5th above root in C major, clamped to bass range.
///
/// Returns perfect 5th for most roots, diminished 5th for B (vii chord).
/// Shifts down an octave if above BASS_HIGH.
/// @param root Root MIDI pitch
/// @return Fifth pitch clamped to bass range
uint8_t getDiatonicFifth(uint8_t root);

/// @brief Get safe chord tone (preferring 5th) that doesn't clash with other tracks.
///
/// When slash chords change the bass root, the diatonic 5th may not be a chord tone.
/// Falls back to the chord's actual 5th, then 3rd, then root.
/// @param root The chord root pitch (may be slash chord bass note)
/// @param harmony Harmony context for collision and chord degree lookup
/// @param start Start tick for collision check
/// @param duration Duration for collision check
/// @param role Track role for collision checking (default: Bass)
/// @param range_low Minimum pitch (default: BASS_LOW)
/// @param range_high Maximum pitch (default: BASS_HIGH)
/// @return Safe pitch that is a chord tone and consonant, or root as fallback
uint8_t getSafeChordTone(uint8_t root, const IHarmonyContext& harmony, Tick start, Tick duration,
                         TrackRole role, uint8_t range_low, uint8_t range_high);

// ============================================================================
// Stepwise Motion Functions
// ============================================================================

// Move stepwise (1-2 semitones) toward target, preferring scale tones.
// This creates more singable melodies by avoiding large chord-tone jumps.
// @param prev_pitch Current pitch to move from
// @param target_pitch Desired target pitch (direction indicator)
// @param chord_degree Scale degree of current chord
// @param range_low Minimum allowed pitch
// @param range_high Maximum allowed pitch
// @param key Current key (0-11, 0=C)
// @param prefer_same_note Probability (0-100) to stay on same note
// @param rng Random number generator for prefer_same_note
// @returns New pitch moved stepwise toward target
int stepwiseToTarget(int prev_pitch, int target_pitch, int8_t chord_degree, int range_low,
                     int range_high, uint8_t key = 0, int prefer_same_note = 30,
                     std::mt19937* rng = nullptr);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_CHORD_UTILS_H
