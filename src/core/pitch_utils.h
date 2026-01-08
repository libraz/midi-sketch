#ifndef MIDISKETCH_CORE_PITCH_UTILS_H
#define MIDISKETCH_CORE_PITCH_UTILS_H

#include <algorithm>
#include <cstdint>

namespace midisketch {

// ============================================================================
// Track Pitch Range Constants
// ============================================================================
// Standard pitch ranges for each track type.
// These ranges are chosen for musical clarity and to avoid frequency masking.
constexpr uint8_t BASS_LOW = 28;    // E1 - Electric bass low range
constexpr uint8_t BASS_HIGH = 55;   // G3 - Bass upper limit

constexpr uint8_t CHORD_LOW = 48;   // C3 - Chord voicing lower limit
constexpr uint8_t CHORD_HIGH = 84;  // C6 - Chord voicing upper limit

constexpr uint8_t MOTIF_LOW = 36;   // C2 - Motif lower limit
constexpr uint8_t MOTIF_HIGH = 108; // C8 - Motif upper limit (wide range for synths)

// ============================================================================
// Pitch Clamp Functions
// ============================================================================
// Clamp pitch to specified range. Returns uint8_t for direct MIDI use.
// @param pitch Input pitch (may be out of range)
// @param low Minimum allowed pitch
// @param high Maximum allowed pitch
// @returns Clamped pitch within [low, high]
inline uint8_t clampPitch(int pitch, uint8_t low, uint8_t high) {
  return static_cast<uint8_t>(std::clamp(pitch, static_cast<int>(low),
                                          static_cast<int>(high)));
}

// Clamp pitch to bass range (28-55, E1-G3).
// @param pitch Input pitch
// @returns Clamped pitch within bass range
inline uint8_t clampBass(int pitch) {
  return clampPitch(pitch, BASS_LOW, BASS_HIGH);
}

// Clamp pitch to chord voicing range (48-84, C3-C6).
// @param pitch Input pitch
// @returns Clamped pitch within chord range
inline uint8_t clampChord(int pitch) {
  return clampPitch(pitch, CHORD_LOW, CHORD_HIGH);
}

// Clamp pitch to motif range (36-108, C2-C8).
// @param pitch Input pitch
// @returns Clamped pitch within motif range
inline uint8_t clampMotif(int pitch) {
  return clampPitch(pitch, MOTIF_LOW, MOTIF_HIGH);
}

// ============================================================================
// Passaggio Constants
// ============================================================================
// Passaggio zone: generalized for mixed voice types
// Male passaggio: ~E4-F4 (64-65), Female: ~A4-B4 (69-71)
// Using a combined zone covering common transition areas
constexpr uint8_t PASSAGGIO_LOW = 64;   // E4
constexpr uint8_t PASSAGGIO_HIGH = 71;  // B4

// ============================================================================
// Scale Constants
// ============================================================================
// Major scale semitones (relative to tonic)
constexpr int SCALE[7] = {0, 2, 4, 5, 7, 9, 11};

// ============================================================================
// TessituraRange
// ============================================================================
// Tessitura: The comfortable singing range within the full vocal range.
// Tessitura is typically the middle 60-70% of the range
struct TessituraRange {
  uint8_t low;
  uint8_t high;
  uint8_t center;
};

// Calculate tessitura from vocal range.
// Leaves ~15-20% headroom at top and bottom for climactic moments.
// @param vocal_low Minimum vocal pitch
// @param vocal_high Maximum vocal pitch
// @returns TessituraRange struct with low, high, and center
TessituraRange calculateTessitura(uint8_t vocal_low, uint8_t vocal_high);

// Check if a pitch is within the tessitura.
// @param pitch MIDI pitch to check
// @param tessitura TessituraRange to check against
// @returns true if pitch is within tessitura
bool isInTessitura(uint8_t pitch, const TessituraRange& tessitura);

// Calculate a comfort score for a pitch (higher = more comfortable).
// Returns value 0.0-1.0.
// @param pitch MIDI pitch to evaluate
// @param tessitura Current tessitura range
// @param vocal_low Minimum vocal pitch
// @param vocal_high Maximum vocal pitch (unused but kept for interface)
// @returns Comfort score from 0.0 to 1.0
float getComfortScore(uint8_t pitch, const TessituraRange& tessitura,
                      uint8_t vocal_low, uint8_t vocal_high);

// ============================================================================
// Passaggio Functions
// ============================================================================

// Check if a pitch is in the passaggio zone.
// Passaggio is the transition zone between vocal registers (chest/head voice).
// @param pitch MIDI pitch to check
// @returns true if pitch is in passaggio
bool isInPassaggio(uint8_t pitch);

// ============================================================================
// Interval Functions
// ============================================================================

// Constrain pitch to be within max_interval of prev_pitch while respecting range.
// This is the KEY function for singable melodies - prevents large jumps.
// @param target_pitch Desired target pitch
// @param prev_pitch Previous pitch (-1 if none)
// @param max_interval Maximum allowed interval in semitones
// @param range_low Minimum allowed pitch
// @param range_high Maximum allowed pitch
// @returns Constrained pitch within range and interval limit
int constrainInterval(int target_pitch, int prev_pitch, int max_interval,
                      int range_low, int range_high);

// Check if two pitch classes create a dissonant interval.
// Dissonant intervals: minor 2nd (1 semitone), tritone (6 semitones)
// @param pc1 First pitch class (0-11)
// @param pc2 Second pitch class (0-11)
// @returns true if interval is dissonant
bool isDissonantInterval(int pc1, int pc2);

// Check if two pitch classes create a dissonant interval with chord context.
// More nuanced than isDissonantInterval:
// - Minor 2nd is always dissonant
// - Tritone is acceptable on dominant (V) chord
// @param pc1 First pitch class (0-11)
// @param pc2 Second pitch class (0-11)
// @param chord_degree Current chord's scale degree (0-6)
// @returns true if interval is dissonant in this context
bool isDissonantIntervalWithContext(int pc1, int pc2, int8_t chord_degree);

// ============================================================================
// Scale Functions
// ============================================================================

// Snap a pitch to the nearest scale tone.
// @param pitch MIDI pitch to snap
// @param key_offset Transposition amount (0 = C major)
// @returns Pitch snapped to nearest scale tone
int snapToNearestScaleTone(int pitch, int key_offset);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_PITCH_UTILS_H
