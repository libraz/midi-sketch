#ifndef MIDISKETCH_CORE_MOTIF_H
#define MIDISKETCH_CORE_MOTIF_H

#include "types.h"
#include <random>
#include <vector>

namespace midisketch {

// =============================================================================
// M9: MotifRole - Musical function classification for motifs
// =============================================================================
// Hook:    Primary memorable melodic idea (strict repetition, high prominence)
// Texture: Background pattern for harmonic fill (flexible, low prominence)
// Counter: Melodic counterpoint to main theme (moderate variation)
enum class MotifRole : uint8_t {
  Hook,     // Main hook - exact repetition, catchy pattern
  Texture,  // Background texture - fills harmonic space
  Counter   // Counter melody - complementary to main vocal/lead
};

// M9: Metadata for motif role behavior
struct MotifRoleMeta {
  MotifRole role;
  float exact_repeat_prob;    // Probability of exact repetition (0.0-1.0)
  float variation_range;      // Allowed variation amount (0.0=none, 1.0=full)
  uint8_t velocity_base;      // Base velocity for this role
  bool allow_octave_layer;    // Whether octave layering is appropriate
};

// M9: Get metadata for a motif role
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

// Motif = rhythm + melodic contour integration
// A motif is a short musical idea that can be repeated and varied
struct Motif {
  std::vector<RhythmNote> rhythm;       // Rhythm pattern
  std::vector<int8_t> contour_degrees;  // Degrees relative to chord root
  uint8_t climax_index = 0;             // Index of highest note
  uint8_t length_beats = 8;             // Length in beats (default: 2 bars)
  int8_t register_center = 0;           // Center register offset
  bool ends_on_chord_tone = true;       // True if last note is chord tone
};

// Motif variation types
enum class MotifVariation : uint8_t {
  Exact,        // Exact repetition
  Transposed,   // Pitch shift
  Inverted,     // Melodic inversion
  Augmented,    // Duration doubled
  Diminished,   // Duration halved
  Fragmented,   // Use only part of the motif
  Sequenced,    // Sequential transposition (sequence)
  Embellished   // Add ornamental notes
};

// Section-specific motif usage plan
struct SectionMotifPlan {
  SectionType section;
  uint8_t primary_motif_id;
  MotifVariation variation;
  uint8_t repetition_count;
  int8_t pitch_shift;
};

// Apply variation to a motif
// @param original The original motif
// @param variation Type of variation to apply
// @param param Additional parameter (e.g., transposition amount)
// @param rng Random number generator
// @returns A new motif with the variation applied
Motif applyVariation(const Motif& original, MotifVariation variation,
                     int8_t param, std::mt19937& rng);

// Design a chorus hook motif
// @param params Melody parameters for the style
// @param rng Random number generator
// @returns A designed hook motif suitable for chorus
Motif designChorusHook(const StyleMelodyParams& params, std::mt19937& rng);

// Select a hook-appropriate variation.
// For hooks, only Exact (main) and Fragmented (exception) are allowed.
// "Variation is the enemy, Exact is justice"
// @param rng Random number generator
// @returns MotifVariation suitable for hooks (80% Exact, 20% Fragmented)
MotifVariation selectHookVariation(std::mt19937& rng);

// Check if a variation is appropriate for hooks.
// @param variation The variation to check
// @returns true if the variation preserves hook identity
bool isHookAppropriateVariation(MotifVariation variation);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_MOTIF_H
