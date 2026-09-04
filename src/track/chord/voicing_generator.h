/**
 * @file voicing_generator.h
 * @brief Chord voicing generation with various voicing types.
 *
 * Provides VoicedChord structure and functions to generate Close, Open,
 * Drop3, Spread, and Rootless voicings for chords.
 */

#ifndef MIDISKETCH_TRACK_CHORD_VOICING_GENERATOR_H
#define MIDISKETCH_TRACK_CHORD_VOICING_GENERATOR_H

#include <array>
#include <cmath>
#include <vector>

#include "core/chord.h"
#include "core/pitch_utils.h"
#include "track/generators/chord.h"

namespace midisketch {
namespace chord_voicing {

/// Voicing type: Close (warm, stacked as tightly as the chord allows), Open
/// (1.5-2 octaves, powerful), Rootless (root omitted, jazz style).
enum class VoicingType {
  Close,    ///< Stacked in the chord's own interval order from the bass
  Open,     ///< At least one voice displaced by an octave for a wider spread
  Rootless  ///< Root omitted (bass handles it, jazz style)
};

/// A voiced chord with absolute MIDI pitches (e.g., C3-E3-G3 for close C major).
struct VoicedChord {
  std::array<uint8_t, 5> pitches{};  ///< MIDI pitches (up to 5 for 9th chords)
  uint8_t count = 0;                 ///< Number of notes in this voicing

  /// @brief Which generator built this candidate.
  ///
  /// Provenance, not a property of the pitches. The passes that run after
  /// generation -- the collision filter, the minimum-voice fill, the bass-clash
  /// removal, the keyboard playability adjustment -- rewrite the pitches and
  /// carry the label with them, so on a voicing that has been through any of
  /// them the name states where the notes came from rather than how they now
  /// sit. Ask voicingHasTexture() when the question is what the voicing
  /// sounds like; this field answers a different question.
  VoicingType type = VoicingType::Close;

  OpenVoicingType open_subtype = OpenVoicingType::Drop2;  ///< Open voicing variant
};

/// Check if two voiced chords have identical pitches (count and pitch values).
/// Does NOT compare voicing type or open subtype.
inline bool areVoicingsIdentical(const VoicedChord& a, const VoicedChord& b) {
  if (a.count != b.count) return false;
  for (uint8_t i = 0; i < a.count; ++i) {
    if (a.pitches[i] != b.pitches[i]) return false;
  }
  return true;
}

/// @brief Whether a voicing's pitches carry the texture a type names.
///
/// Every place that asks "is this candidate the texture the bar asked for?" has
/// to ask the pitches, because `VoicedChord::type` records the generator that
/// built the candidate and the passes that run afterwards rewrite the pitches
/// while carrying the old label along. A spread voicing whose displaced voice
/// was removed by the collision filter still says Open and is not open, and the
/// requested-texture gate that believes it discards the candidates that are.
///
/// What each name asserts about the notes:
///  - Open: the voices span more than an octave. A stack that wide sounds
///    spread whatever built it, which is the point -- a ninth chord in close
///    position has five tones and cannot fit inside an octave, so it answers
///    yes here and it should.
///  - Close: the complement of the above.
///  - Rootless: no voice sounds the root's pitch class.
///
/// @param voicing Candidate to measure
/// @param root Chord root as a MIDI pitch; only its pitch class is read
/// @param type The texture being asked about
bool voicingHasTexture(const VoicedChord& voicing, uint8_t root, VoicingType type);

/// @name Voice Leading Metrics
/// @{

/// Calculate voice leading distance with weighted voices.
/// Bass (index 0) and soprano (top) weighted 2x, inner voices 1x.
/// @param prev Previous chord voicing
/// @param next Next chord voicing
/// @return Total weighted distance
int voicingDistance(const VoicedChord& prev, const VoicedChord& next);

/// Count common tones (octave-equivalent). More = smoother progression.
/// @param prev Previous chord voicing
/// @param next Next chord voicing
/// @return Number of common tones
int countCommonTones(const VoicedChord& prev, const VoicedChord& next);

/// Check for parallel 5ths/octaves (forbidden in classical, relaxed in pop/dance).
/// @param prev Previous chord voicing
/// @param curr Current chord voicing
/// @return True if parallel 5ths or octaves detected
bool hasParallelFifthsOrOctaves(const VoicedChord& prev, const VoicedChord& curr);

/// @}

/// @name Voicing Generation
/// @{

/// Generate close voicings for a chord (within one octave).
/// @param root Root note (MIDI pitch)
/// @param chord Chord structure with intervals
/// @return Vector of possible close voicings
std::vector<VoicedChord> generateCloseVoicings(uint8_t root, const Chord& chord);

/// Generate open voicings (Drop 2 style, wider spread).
/// @param root Root note (MIDI pitch)
/// @param chord Chord structure with intervals
/// @return Vector of possible open voicings
std::vector<VoicedChord> generateOpenVoicings(uint8_t root, const Chord& chord);

/// Generate Drop 3 voicings (drop 3rd voice from top down an octave).
/// Creates wider spread than Drop 2, useful for big band/orchestral contexts.
/// @param root Root note (MIDI pitch)
/// @param chord Chord structure with intervals
/// @return Vector of possible Drop 3 voicings
std::vector<VoicedChord> generateDrop3Voicings(uint8_t root, const Chord& chord);

/// Generate Spread voicings (wide intervallic spacing 1-5-10 style).
/// Creates open, transparent texture suitable for pads and atmospheric sections.
/// @param root Root note (MIDI pitch)
/// @param chord Chord structure with intervals
/// @return Vector of possible spread voicings
std::vector<VoicedChord> generateSpreadVoicings(uint8_t root, const Chord& chord);

/// Generate rootless voicings (up to 4-voice, root omitted for bass).
/// Supports 4-voice rootless with safe tension additions.
/// @param root Root note (MIDI pitch)
/// @param chord Chord structure with intervals
/// @param bass_pitch_mask Bitmask of bass pitch classes, or 0 if unknown
/// @return Vector of possible rootless voicings
std::vector<VoicedChord> generateRootlessVoicings(uint8_t root, const Chord& chord,
                                                  uint16_t bass_pitch_mask = 0);

/// Generate all possible voicings for a chord.
/// @param root Root note (MIDI pitch)
/// @param chord Chord structure with intervals
/// @param preferred_type Preferred voicing type
/// @param bass_pitch_mask Bitmask of bass pitch classes for collision avoidance, or 0 if unknown
/// @param open_subtype Which open voicing variant to prefer
/// @return Vector of all possible voicings
std::vector<VoicedChord> generateVoicings(uint8_t root, const Chord& chord,
                                          VoicingType preferred_type, uint16_t bass_pitch_mask = 0,
                                          OpenVoicingType open_subtype = OpenVoicingType::Drop2);

/// @}

}  // namespace chord_voicing
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_CHORD_VOICING_GENERATOR_H
