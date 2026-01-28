/**
 * @file motif.h
 * @brief Motif system for memorable melodic patterns.
 */

#ifndef MIDISKETCH_CORE_MOTIF_H
#define MIDISKETCH_CORE_MOTIF_H

#include <random>
#include <vector>

#include "core/i_harmony_context.h"
#include "types.h"

namespace midisketch {

/// @brief Musical function classification for motifs.
enum class MotifRole : uint8_t {
  Hook,     ///< Primary hook - exact repetition, high prominence
  Texture,  ///< Background texture - flexible, fills harmonic space
  Counter   ///< Counter melody - complementary to main theme
};

/**
 * @brief Metadata defining behavior for each motif role.
 */
struct MotifRoleMeta {
  MotifRole role;           ///< Which role this describes
  float exact_repeat_prob;  ///< Probability of exact repetition (0.0-1.0)
  float variation_range;    ///< Allowed variation (0.0=none, 1.0=full)
  uint8_t velocity_base;    ///< Base MIDI velocity for this role
  bool allow_octave_layer;  ///< Whether octave doubling is appropriate
};

/**
 * @brief Get metadata for a motif role.
 *
 * Returns behavior parameters appropriate for each role type.
 *
 * @param role The motif role to query
 * @return MotifRoleMeta with role-appropriate parameters
 */
inline MotifRoleMeta getMotifRoleMeta(MotifRole role) {
  switch (role) {
    case MotifRole::Hook:
      // Hooks: 90% exact, minimal variation, prominent velocity, octave OK
      return {role, 0.90f, 0.1f, 85, true};
    case MotifRole::Texture:
      // Texture: 60% exact, moderate variation, softer, no octave
      return {role, 0.60f, 0.5f, 65, false};
    case MotifRole::Counter:
      // Counter: 70% exact, some variation, moderate velocity, octave OK
      return {role, 0.70f, 0.3f, 75, true};
  }
  return {MotifRole::Hook, 0.90f, 0.1f, 85, true};
}

/// @brief A short musical idea that can be repeated and varied.
struct Motif {
  std::vector<RhythmNote> rhythm;         ///< Rhythm pattern (durations)
  std::vector<int8_t> contour_degrees;    ///< Degrees relative to chord root
  std::vector<uint8_t> absolute_pitches;  ///< Original absolute MIDI pitches
  uint8_t climax_index = 0;               ///< Index of highest note
  uint8_t length_beats = 8;               ///< Length in beats (default: 2 bars)
  int8_t register_center = 0;             ///< Center register offset
  bool ends_on_chord_tone = true;         ///< True if last note is chord tone
};

/// @brief Types of variation that can be applied to a motif.
enum class MotifVariation : uint8_t {
  Exact,       ///< Exact repetition (most common for hooks)
  Transposed,  ///< Pitch shifted up or down
  Inverted,    ///< Melodic inversion (mirror)
  Augmented,   ///< Duration doubled (slower)
  Diminished,  ///< Duration halved (faster)
  Fragmented,  ///< Use only part of the motif
  Sequenced,   ///< Sequential transposition
  Embellished  ///< Add ornamental notes
};

/**
 * @brief Apply a variation technique to a motif.
 * @param original The source motif
 * @param variation Type of variation to apply
 * @param param Additional parameter (e.g., transposition semitones)
 * @param rng Random number generator for stochastic variations
 * @return New Motif with the variation applied
 */
Motif applyVariation(const Motif& original, MotifVariation variation, int8_t param,
                     std::mt19937& rng);

/**
 * @brief Design a chorus hook motif from scratch.
 * @param params Style parameters affecting hook characteristics
 * @param rng Random number generator
 * @return A newly designed hook motif
 */
Motif designChorusHook(const StyleMelodyParams& params, std::mt19937& rng);

/**
 * @brief Select a hook-appropriate variation.
 *
 * 80% Exact, 20% Fragmented. Other variations destroy hook identity.
 *
 * @param rng Random number generator
 * @return MotifVariation (Exact or Fragmented)
 */
MotifVariation selectHookVariation(std::mt19937& rng);

/**
 * @brief Check if a variation preserves hook identity.
 * @param variation The variation to check
 * @return true if variation is safe for hooks (Exact or Fragmented)
 */
bool isHookAppropriateVariation(MotifVariation variation);

/**
 * @brief Extract a motif from existing chorus vocal notes.
 * @param chorus_notes Notes from the chorus section
 * @param max_notes Maximum notes to include (default 8)
 * @return Extracted motif with relative contour
 */
Motif extractMotifFromChorus(const std::vector<NoteEvent>& chorus_notes, size_t max_notes = 8);

/**
 * @brief Place a motif in the intro section.
 * @param motif The motif to place
 * @param intro_start Start tick of the intro section
 * @param intro_end End tick of the intro section
 * @param base_pitch Base pitch for transposition
 * @param velocity Base MIDI velocity for notes
 * @return Vector of note events for the intro
 */
std::vector<NoteEvent> placeMotifInIntro(const Motif& motif, Tick intro_start, Tick intro_end,
                                         uint8_t base_pitch, uint8_t velocity);

/**
 * @brief Place a motif in the aux track.
 * @param motif The motif to place
 * @param section_start Start tick of the section
 * @param section_end End tick of the section
 * @param base_pitch Base pitch for transposition
 * @param velocity_ratio Velocity multiplier (0.0-1.0)
 * @return Vector of note events for the aux track
 */
std::vector<NoteEvent> placeMotifInAux(const Motif& motif, Tick section_start, Tick section_end,
                                       uint8_t base_pitch, float velocity_ratio);

/**
 * @brief Place a varied motif in Bridge section for thematic continuity.
 *
 * Uses inverted or retrograde variation to provide contrast while
 * maintaining melodic connection to chorus motif. Notes are snapped
 * to chord tones and checked for collisions to avoid dissonance.
 *
 * @param motif The chorus motif to transform
 * @param section_start Start tick of the bridge section
 * @param section_end End tick of the bridge section
 * @param base_pitch Base pitch for transposition
 * @param velocity Base MIDI velocity for notes
 * @param rng Random number generator for variation selection
 * @param harmony Harmony context for chord lookup and collision detection
 * @param track Track role for collision detection (default: Motif)
 * @return Vector of note events for the bridge
 */
std::vector<NoteEvent> placeMotifInBridge(const Motif& motif, Tick section_start, Tick section_end,
                                          uint8_t base_pitch, uint8_t velocity, std::mt19937& rng,
                                          const IHarmonyContext& harmony,
                                          TrackRole track = TrackRole::Motif);

/**
 * @brief Place enhanced motif in FinalChorus (peak) section.
 *
 * Strengthens the motif with octave doubling and increased velocity
 * for climactic impact. Notes are snapped to chord tones and checked
 * for collisions to avoid dissonance.
 *
 * @param motif The chorus motif to enhance
 * @param section_start Start tick of the final chorus section
 * @param section_end End tick of the final chorus section
 * @param base_pitch Base pitch for transposition
 * @param velocity Base MIDI velocity for notes (will be boosted)
 * @param harmony Harmony context for chord lookup and collision detection
 * @param track Track role for collision detection (default: Motif)
 * @return Vector of note events including octave doubles
 */
std::vector<NoteEvent> placeMotifInFinalChorus(const Motif& motif, Tick section_start,
                                                Tick section_end, uint8_t base_pitch,
                                                uint8_t velocity, const IHarmonyContext& harmony,
                                                TrackRole track = TrackRole::Motif);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_MOTIF_H
