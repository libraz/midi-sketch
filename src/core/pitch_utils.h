/**
 * @file pitch_utils.h
 * @brief Pitch manipulation utilities with music theory foundations.
 */

#ifndef MIDISKETCH_CORE_PITCH_UTILS_H
#define MIDISKETCH_CORE_PITCH_UTILS_H

#include <algorithm>
#include <cstdint>
#include <string>

#include "core/section_types.h"

namespace midisketch {

// ============================================================================
// Track Pitch Range Constants
// ============================================================================

/// @name Track Pitch Ranges
/// @{
constexpr uint8_t BASS_LOW = 28;   ///< E1 - Electric bass low range
constexpr uint8_t BASS_HIGH = 55;  ///< G3 - Bass upper limit

constexpr uint8_t CHORD_LOW = 60;   ///< C4 - Chord voicing lower limit (above bass)
constexpr uint8_t CHORD_HIGH = 84;  ///< C6 - Chord voicing upper limit

constexpr uint8_t MOTIF_LOW = 60;    ///< C4 - Motif lower limit (above bass)
constexpr uint8_t MOTIF_HIGH = 108;  ///< C8 - Motif upper limit (wide for synths)

constexpr uint8_t VOCAL_LOW_MIN = 36;   ///< C2 - Absolute minimum for vocal range
constexpr uint8_t VOCAL_HIGH_MAX = 96;  ///< C7 - Absolute maximum for vocal range
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

/**
 * @brief Get section-appropriate maximum melodic interval.
 *
 * Different sections benefit from different leap constraints:
 * - Chorus/MixBreak/Drop: Up to octave (12) for dramatic impact
 * - Bridge: Up to 14 semitones for maximum contrast
 * - B (Pre-chorus): Up to 10 for tension building
 * - Default (Verse, etc.): Standard 9 semitones for stability
 *
 * @param section Section type
 * @return Maximum allowed melodic interval in semitones
 */
inline int getMaxMelodicIntervalForSection(SectionType section) {
  switch (section) {
    case SectionType::Chorus:
    case SectionType::MixBreak:
    case SectionType::Drop:
      return 12;  // Octave for dramatic impact
    case SectionType::Bridge:
      return 14;  // Maximum contrast
    case SectionType::B:
      return 10;  // Tension building
    default:
      return kMaxMelodicInterval;  // Standard (9)
  }
}
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
// Interval Constants
// ============================================================================

/// @brief Common musical intervals in semitones.
/// Use these constants instead of magic numbers for interval calculations.
namespace Interval {
constexpr int UNISON = 0;
constexpr int HALF_STEP = 1;   ///< Minor 2nd / semitone
constexpr int WHOLE_STEP = 2;  ///< Major 2nd / tone
constexpr int MINOR_3RD = 3;
constexpr int MAJOR_3RD = 4;
constexpr int PERFECT_4TH = 5;
constexpr int TRITONE = 6;  ///< Augmented 4th / Diminished 5th
constexpr int PERFECT_5TH = 7;
constexpr int MINOR_6TH = 8;
constexpr int MAJOR_6TH = 9;
constexpr int MINOR_7TH = 10;
constexpr int MAJOR_7TH = 11;
constexpr int OCTAVE = 12;
constexpr int TWO_OCTAVES = 24;
}  // namespace Interval

// ============================================================================
// Debug/Display Utilities
// ============================================================================

/// @brief Note names using sharps (for display/logging).
constexpr const char* NOTE_NAMES[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                      "F#", "G",  "G#", "A",  "A#", "B"};

/// @brief Convert MIDI pitch to note name with octave (e.g., "C4", "F#5").
/// @param pitch MIDI pitch (0-127)
/// @return Note name string (e.g., "C4")
inline std::string pitchToNoteName(uint8_t pitch) {
  int octave = (pitch / 12) - 1;
  return std::string(NOTE_NAMES[pitch % 12]) + std::to_string(octave);
}

// ============================================================================
// Chord Function (Harmonic Function)
// ============================================================================

/// @brief Harmonic function of a chord in the key.
///
/// Tonic (T): I, vi, iii - stable, resting chords
/// Dominant (D): V, vii° - tension chords that resolve to tonic
/// Subdominant (S): IV, ii - transitional chords between T and D
enum class ChordFunction : uint8_t { Tonic, Dominant, Subdominant };

/**
 * @brief Get the harmonic function of a chord from its scale degree.
 *
 * @param degree Scale degree (0=I, 1=ii, 2=iii, 3=IV, 4=V, 5=vi, 6=vii°, 10=bVII)
 * @return ChordFunction (Tonic, Dominant, or Subdominant)
 */
inline ChordFunction getChordFunction(int8_t degree) {
  switch (degree) {
    case 0:  // I  - tonic
    case 2:  // iii - tonic substitute
    case 5:  // vi - relative minor (tonic function)
      return ChordFunction::Tonic;
    case 4:  // V  - dominant
    case 6:  // vii° - leading tone (dominant function)
      return ChordFunction::Dominant;
    case 1:   // ii - supertonic (subdominant function)
    case 3:   // IV - subdominant
    case 10:  // bVII - borrowed subdominant
    default:
      return ChordFunction::Subdominant;
  }
}

// ============================================================================
// Key Transposition
// ============================================================================

// Forward declaration of Key enum (defined in basic_types.h)
enum class Key : uint8_t;

/**
 * @brief Transpose pitch by key offset.
 * @param pitch MIDI pitch to transpose (0-127)
 * @param key Key to transpose to (semitones from C)
 * @return Transposed pitch clamped to MIDI range (0-127)
 */
inline uint8_t transposePitch(uint8_t pitch, Key key) {
  int offset = static_cast<int>(key);
  int result = pitch + offset;
  return static_cast<uint8_t>(std::clamp(result, 0, 127));
}

// ============================================================================
// TessituraRange
// ============================================================================

/// @brief Tessitura: The comfortable singing range within full vocal range.
struct TessituraRange {
  uint8_t low;         ///< Lower bound of comfortable range
  uint8_t high;        ///< Upper bound of comfortable range
  uint8_t center;      ///< Center of tessitura (optimal pitch)
  uint8_t vocal_low;   ///< Full vocal range lower bound (for passaggio calculation)
  uint8_t vocal_high;  ///< Full vocal range upper bound (for passaggio calculation)
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
 * @brief Calculated passaggio range for a given vocal range.
 */
struct PassaggioRange {
  uint8_t lower;  ///< Lower bound of passaggio zone
  uint8_t upper;  ///< Upper bound of passaggio zone

  /**
   * @brief Check if a pitch is in this passaggio range.
   * @param pitch MIDI pitch to check
   * @return true if pitch is within [lower, upper]
   */
  bool contains(uint8_t pitch) const { return pitch >= lower && pitch <= upper; }

  /**
   * @brief Get the center of the passaggio range.
   * @return Center pitch of passaggio
   */
  uint8_t center() const { return (lower + upper) / 2; }

  /**
   * @brief Get the width of the passaggio range.
   * @return Number of semitones in passaggio zone
   */
  uint8_t width() const { return upper - lower; }
};

/**
 * @brief Calculate passaggio range dynamically from vocal range.
 *
 * The passaggio is typically in the upper-middle portion of the vocal range,
 * approximately at 55%-75% of the total range. This represents the "break"
 * point where singers transition between registers.
 *
 * For a 12-semitone range (typical octave), passaggio is about 2-3 semitones.
 * For larger ranges, it scales proportionally.
 *
 * @param vocal_low Lower bound of vocal range (MIDI note)
 * @param vocal_high Upper bound of vocal range (MIDI note)
 * @return PassaggioRange with calculated bounds
 */
PassaggioRange calculateDynamicPassaggio(uint8_t vocal_low, uint8_t vocal_high);

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
 * - Minor 2nd (1): always dissonant (harsh beating)
 * - Major 2nd (2): dissonant only for simultaneous (vertical) intervals
 * - Tritone (6): allowed on V chord (dominant function) and vii° chord
 *
 * @param pc1 First pitch class (0-11)
 * @param pc2 Second pitch class (0-11)
 * @param chord_degree Current chord's scale degree (0=I, 4=V, etc.)
 * @param simultaneous true for vertical (same-time) intervals, false for melodic
 * @return true if interval is dissonant in this harmonic context
 */
bool isDissonantIntervalWithContext(int pc1, int pc2, int8_t chord_degree,
                                    bool simultaneous = true);

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
// Avoid Note Detection
// ============================================================================

/// @name Avoid Note Intervals (semitones from chord root)
/// @{
constexpr int AVOID_PERFECT_4TH = 5;  ///< P4 - avoid on major tonic
constexpr int AVOID_MINOR_6TH = 8;    ///< m6 - avoid on minor chords
constexpr int AVOID_TRITONE = 6;      ///< TT - essential on dominant, avoid elsewhere
constexpr int AVOID_MAJOR_7TH = 11;   ///< M7 - context-dependent
/// @}

/**
 * @brief Check if a pitch is an avoid note for the given chord.
 *
 * Avoid notes are tones that create undesirable dissonance when sustained
 * against a chord. However, this depends on the chord's harmonic function:
 *
 * - Dominant (V, vii°): Tritone is REQUIRED (resolution core), not avoided
 * - Tonic (I, vi, iii): Tritone is harsh, P4 may clash with major 3rd
 * - Subdominant (IV, ii): More lenient, P4 is acceptable
 *
 * @param pitch MIDI pitch to check
 * @param chord_root Chord root pitch (any octave, uses pitch class)
 * @param is_minor true if the chord quality is minor
 * @param chord_degree Scale degree of the chord (0=I, 4=V, etc.)
 * @return true if the pitch should be avoided in this harmonic context
 */
bool isAvoidNoteWithContext(int pitch, uint8_t chord_root, bool is_minor, int8_t chord_degree);

/**
 * @brief Simple avoid note check without harmonic context.
 *
 * For backward compatibility. Uses conservative rules:
 * - P4 (5) on major, m6 (8) on minor are avoided
 * - Tritone (6) and M7 (11) are always avoided
 *
 * @param pitch MIDI pitch to check
 * @param chord_root Chord root pitch
 * @param is_minor true if chord is minor quality
 * @return true if pitch is a traditional avoid note
 */
bool isAvoidNoteSimple(int pitch, uint8_t chord_root, bool is_minor);

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
