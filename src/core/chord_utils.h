/**
 * @file chord_utils.h
 * @brief Chord voicing utilities and tone helpers.
 */

#ifndef MIDISKETCH_CORE_CHORD_UTILS_H
#define MIDISKETCH_CORE_CHORD_UTILS_H

#include <array>
#include <cstddef>
#include <cstdint>
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

  /// Iteration over populated pitch classes only.  This lets generation hot
  /// paths use the fixed-size representation without materialising a vector.
  const int* begin() const { return pitch_classes.data(); }
  const int* end() const { return pitch_classes.data() + count; }
  bool empty() const { return count == 0; }
  size_t size() const { return count; }
  int operator[](size_t index) const { return pitch_classes[index]; }
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

/// @brief Find the nearest chord tone within a pitch range.
///
/// Searches chord tones in nearby octaves (up to +/-2 from the target's octave)
/// and returns the closest one within [range_low, range_high].
/// Returns the original pitch if no chord tone is found in range.
///
/// @param pitch Target MIDI pitch
/// @param degree Scale degree of the chord (0-6 for I-vii)
/// @param range_low Minimum allowed pitch (inclusive)
/// @param range_high Maximum allowed pitch (inclusive)
/// @return Nearest chord tone pitch within range, or pitch if none found
int findNearestChordToneInRange(int pitch, int8_t degree, int range_low, int range_high);

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
   * @return Fixed-size chord-tone collection (0-11 pitch classes)
   */
  const ChordTones& pitchClasses() const { return pitch_classes_; }

 private:
  int8_t degree_;
  int root_pc_;
  ChordTones pitch_classes_;
};

// ============================================================================
// Tritone Detection
// ============================================================================

/// @brief Check if a pitch forms a tritone interval with any chord pitch class.
/// @param pitch_pc Pitch class to check (0-11)
/// @param chord_pcs Chord pitch classes
/// @return true if any interval is a tritone (6 semitones)
bool hasTritoneWithChord(int pitch_pc, const std::vector<int>& chord_pcs);
bool hasTritoneWithChord(int pitch_pc, const ChordTones& chord_pcs);

// ============================================================================
// Voices of one track sounding together
// ============================================================================

/// @brief Whether two voices sit at an interval the model calls dissonant.
///
/// Minor second and its compound minor ninth are dissonant wherever they
/// appear; a major second is dissonant only when the voices actually sit next
/// to each other, since the same interval spread over an octave is a ninth.
/// A major seventh is deliberately absent: it is context dependent, and inside
/// a seventh chord it is the chord itself, so rejecting it would make every
/// major-seventh voicing drop either its root or its seventh.
bool isDissonantVoicingGap(int semitones);

/// @brief Whether two voices of one track sounding at one onset form a cluster.
///
/// The cross-track collision detector compares different tracks only, so a pair
/// a track states against itself -- a chord's own voices, a riff's lead and the
/// stab under it -- is answered here and nowhere else.
///
/// The gap rule alone cannot answer it: a major second between two tones of the
/// chord being sounded is the chord. A seventh sits a whole step under the root
/// and a ninth a whole step over it, so a rule that calls the pair a cluster
/// removes one of them -- and every screen that asks ranks the seventh below
/// the root, so the tone that makes the chord extended is the one that goes.
/// A whole step against a tone the chord does not contain is still a cluster,
/// and the minor second and minor ninth stay dissonant wherever they appear.
///
/// The major seventh takes the same condition. Calling it consonant outright
/// is right only for the case it was excused for -- inside a seventh chord it
/// is the chord -- and a major seventh against a tone the chord does not
/// contain is as harsh as any other clash; the analyzer has always said so, so
/// a rule that excused it unconditionally disagreed with the report.
///
/// This is the one place the question is answered. The rule used to be spelled
/// out at each screen that asks it, and a screen stating it separately can be
/// corrected on its own while the others keep undoing the correction.
///
/// @param pitch_a One voice
/// @param pitch_b The other voice, sounding at the same onset
/// @param tones Tones of the chord the timeline states at that onset
/// @return true when the pair is a cluster and one of the two has to give way
bool isVoicingCluster(uint8_t pitch_a, uint8_t pitch_b, const ChordTones& tones);

/// @brief Whether both voices belong to the chord the timeline states.
///
/// This answers only that question; which intervals it then excuses is the
/// caller's to decide, and they do not all decide the same way. It is the
/// condition under which a major second is a seventh sitting next to its root
/// rather than a clash, and it is asked both between the voices of one chord
/// and between a chord voice and whatever else is sounding, which is why it is
/// named here instead of being written out at each of those screens.
///
/// @param pitch_a One voice
/// @param pitch_b The other voice, sounding at the same time
/// @param tones Tones of the chord the timeline states there
/// @return true when both pitch classes appear among the chord's tones
bool bothVoicesAreChordTones(uint8_t pitch_a, uint8_t pitch_b, const ChordTones& tones);

/// @brief Whether the sounding chord accounts for a pair an interval rule flagged.
///
/// For the gates that sweep already-placed notes -- the post-processing clash
/// removal and the analysis report -- a flagged pair is the chord itself when
/// both voices belong to it. That covers the tritone, which between a dominant's
/// third and its seventh is not a clash inside the chord but the thing that
/// makes it one.
///
/// The semitone is the exception the chord cannot make. A minor second and its
/// compound the minor ninth beat audibly whichever voices state them, so a pair
/// of chord tones that close together is still a pair to answer for. Stating
/// that at each sweep instead let one of them be corrected while the other kept
/// excusing the same pair.
///
/// The generation-side collision check does not use this: it runs before the
/// notes exist and excuses only the major second, because the intervals this
/// leaves to the chord are settled there by register and extension rules of
/// their own. See isSoundingChordItself in track_collision_detector.cpp.
///
/// @param actual_semitones Absolute distance between the two voices
/// @param pitch_a One voice
/// @param pitch_b The other voice, sounding at the same time
/// @param tones Tones of the chord the timeline states there
/// @return true when the pair should not be counted as a clash
bool chordExcusesFlaggedPair(int actual_semitones, uint8_t pitch_a, uint8_t pitch_b,
                             const ChordTones& tones);

class IChordLookup;

/// @brief Pitch for one voice of an onset that clears the voices beside it.
///
/// Wherever a pass decides pitches one note at a time and several of them land
/// on the same onset of the same track -- a riff replay correcting a lead and
/// the stab under it, a frozen bar re-quantized voice by voice -- the interval
/// each decision leaves against its neighbours is nobody's answer unless it is
/// asked here. `placed` is what the onset already sounds, in the order the
/// caller chose the voices, so the first voice keeps its pitch and the ones
/// under it give way.
///
/// @param harmony Tick-accurate chord lookup
/// @param desired Pitch this voice would take on its own
/// @param tick Onset the voices share
/// @param placed Pitches already placed at this onset
/// @param range_low Lowest pitch the track may state
/// @param range_high Highest pitch the track may state here
/// @return A chord tone in range that clears `placed`, else `desired` unchanged
///         -- an onset that cannot be voiced cleanly keeps what it was given
uint8_t clearOfOnsetVoices(const IChordLookup& harmony, uint8_t desired, Tick tick,
                           const std::vector<uint8_t>& placed, uint8_t range_low,
                           uint8_t range_high);

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

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_CHORD_UTILS_H
