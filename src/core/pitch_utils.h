/**
 * @file pitch_utils.h
 * @brief Pitch manipulation utilities with music theory foundations.
 */

#ifndef MIDISKETCH_CORE_PITCH_UTILS_H
#define MIDISKETCH_CORE_PITCH_UTILS_H

#include <algorithm>
#include <cstdint>

namespace midisketch {

// ============================================================================
// Track Pitch Range Constants
// ============================================================================

/// @name Track Pitch Ranges
/// @{
constexpr uint8_t BASS_LOW = 28;   ///< E1 - Electric bass low range
constexpr uint8_t BASS_HIGH = 55;  ///< G3 - Bass upper limit

constexpr uint8_t CHORD_LOW = 48;   ///< C3 - Chord voicing lower limit
constexpr uint8_t CHORD_HIGH = 84;  ///< C6 - Chord voicing upper limit

constexpr uint8_t MOTIF_LOW = 36;    ///< C2 - Motif lower limit
constexpr uint8_t MOTIF_HIGH = 108;  ///< C8 - Motif upper limit (wide for synths)
/// @}

// ============================================================================
// Melodic Interval Constants
// ============================================================================

/// @name Melodic Interval Limits
/// @{
/// Maximum melodic interval for singable melodies (Major 6th = 9 semitones).
/// Larger intervals are difficult to sing and sound unnatural in pop melodies.
/// Applied at multiple stages: pitch selection, adjustment, and final validation.
constexpr int kMaxMelodicInterval = 9;
/// @}

// ============================================================================
// Pitch Clamp Functions
// ============================================================================

/**
 * @brief Clamp pitch to specified range.
 * @param pitch Input pitch (may be out of range)
 * @param low Minimum allowed pitch
 * @param high Maximum allowed pitch
 * @return Clamped pitch within [low, high]
 */
inline uint8_t clampPitch(int pitch, uint8_t low, uint8_t high) {
  return static_cast<uint8_t>(std::clamp(pitch, static_cast<int>(low), static_cast<int>(high)));
}

/// Clamp pitch to bass range (E1-G3). Bass notes outside this sound muddy.
inline uint8_t clampBass(int pitch) { return clampPitch(pitch, BASS_LOW, BASS_HIGH); }

/// Clamp pitch to chord voicing range (C3-C6). Keeps chords out of bass/vocal.
inline uint8_t clampChord(int pitch) { return clampPitch(pitch, CHORD_LOW, CHORD_HIGH); }

/// Clamp pitch to motif range (C2-C8). Wide range for synth flexibility.
inline uint8_t clampMotif(int pitch) { return clampPitch(pitch, MOTIF_LOW, MOTIF_HIGH); }

// ============================================================================
// Passaggio Constants
// ============================================================================

/// @name Passaggio Zone
/// Vocal register transition zone (chest to head voice). E4-B4.
///
/// Music theory note on passaggio (register transition):
/// The passaggio is where the voice shifts between registers. Values vary by voice type:
///   - Soprano: F5-A5 (first passaggio at E5-F#5)
///   - Alto: D5-F#5
///   - Tenor: E4-G4 (similar to current values)
///   - Baritone: D4-F4
///   - Bass: C4-E4
///
/// Current implementation: Fixed E4-B4 range for tenor/average male voice.
/// This is appropriate for pop music where male lead vocals are common.
///
/// Future enhancement: Make passaggio dynamic based on vocal_low/vocal_high range,
/// or add voice_type parameter to select appropriate passaggio for the voice.
/// @{
constexpr uint8_t PASSAGGIO_LOW = 64;   ///< E4 - Lower bound of passaggio zone
constexpr uint8_t PASSAGGIO_HIGH = 71;  ///< B4 - Upper bound of passaggio zone
/// @}

// ============================================================================
// Scale Constants
// ============================================================================

/// Major scale intervals from tonic: 0,2,4,5,7,9,11 (W-W-H-W-W-W-H).
constexpr int SCALE[7] = {0, 2, 4, 5, 7, 9, 11};

// ============================================================================
// TessituraRange
// ============================================================================

/// @brief Tessitura: The comfortable singing range within full vocal range.
struct TessituraRange {
  uint8_t low;     ///< Lower bound of comfortable range
  uint8_t high;    ///< Upper bound of comfortable range
  uint8_t center;  ///< Center of tessitura (optimal pitch)
};

/**
 * @brief Calculate tessitura from vocal range.
 * @param vocal_low Minimum vocal pitch (full range)
 * @param vocal_high Maximum vocal pitch (full range)
 * @return TessituraRange with low, high, and center
 */
TessituraRange calculateTessitura(uint8_t vocal_low, uint8_t vocal_high);

/**
 * @brief Check if a pitch is within the tessitura.
 * @param pitch MIDI pitch to check
 * @param tessitura TessituraRange to check against
 * @return true if pitch is within comfortable range
 */
bool isInTessitura(uint8_t pitch, const TessituraRange& tessitura);

/**
 * @brief Calculate vocal comfort score for a pitch.
 * @param pitch MIDI pitch to evaluate
 * @param tessitura Current tessitura range
 * @param vocal_low Minimum vocal pitch
 * @param vocal_high Maximum vocal pitch
 * @return Comfort score from 0.0 (uncomfortable) to 1.0 (optimal)
 */
float getComfortScore(uint8_t pitch, const TessituraRange& tessitura, uint8_t vocal_low,
                      uint8_t vocal_high);

// ============================================================================
// Passaggio Functions
// ============================================================================

/**
 * @brief Check if a pitch is in the passaggio zone (E4-B4).
 * @param pitch MIDI pitch to check
 * @return true if pitch is in the passaggio
 */
bool isInPassaggio(uint8_t pitch);

/**
 * @brief Check if pitch is in dynamic passaggio zone based on vocal range.
 * @param pitch MIDI pitch to check
 * @param vocal_low Lower bound of vocal range
 * @param vocal_high Upper bound of vocal range
 * @return true if pitch is in passaggio (55-75% of range)
 */
bool isInPassaggioRange(uint8_t pitch, uint8_t vocal_low, uint8_t vocal_high);

// ============================================================================
// Interval Functions
// ============================================================================

/**
 * @brief Constrain pitch to be within max_interval of previous pitch.
 *
 * Typically max 9 semitones (major 6th) for singable melodies.
 *
 * @param target_pitch Desired target pitch
 * @param prev_pitch Previous pitch (-1 if none/first note)
 * @param max_interval Maximum allowed interval in semitones
 * @param range_low Minimum allowed pitch
 * @param range_high Maximum allowed pitch
 * @return Constrained pitch within range and interval limit
 */
int constrainInterval(int target_pitch, int prev_pitch, int max_interval, int range_low,
                      int range_high);

/**
 * @brief Check if two pitch classes create a dissonant interval.
 *
 * Minor 2nd (1 semitone) and tritone (6 semitones) are dissonant.
 *
 * @param pc1 First pitch class (0-11)
 * @param pc2 Second pitch class (0-11)
 * @return true if interval is minor 2nd or tritone
 */
bool isDissonantInterval(int pc1, int pc2);

/**
 * @brief Check for dissonance with chord context awareness.
 *
 * Tritone is allowed on V chord (dominant function).
 *
 * @param pc1 First pitch class (0-11)
 * @param pc2 Second pitch class (0-11)
 * @param chord_degree Current chord's scale degree (0=I, 4=V, etc.)
 * @return true if interval is dissonant in this harmonic context
 */
bool isDissonantIntervalWithContext(int pc1, int pc2, int8_t chord_degree);

/**
 * @brief Check if an actual semitone interval is dissonant (Pop theory).
 *
 * Uses actual semitone distance for accurate dissonance detection.
 * Compound intervals (1+ octave) are treated differently:
 * - Minor 2nd (1) and minor 9th (13): always harsh
 * - Major 2nd (2): harsh in close range only
 * - Minor 7th (10), major 9th (14): acceptable in Pop (7th chords, add9)
 *
 * @param actual_semitones Actual distance between notes in semitones
 * @param chord_degree Current chord's scale degree (0=I, 4=V, etc.)
 * @return true if interval is dissonant
 */
bool isDissonantActualInterval(int actual_semitones, int8_t chord_degree);

// ============================================================================
// Scale Functions
// ============================================================================

/**
 * @brief Snap a pitch to the nearest scale tone.
 * @param pitch MIDI pitch to snap (may be chromatic)
 * @param key_offset Transposition from C major (0 = C, 2 = D, 7 = G, etc.)
 * @return Pitch snapped to nearest scale tone in the given key
 */
int snapToNearestScaleTone(int pitch, int key_offset);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_PITCH_UTILS_H
