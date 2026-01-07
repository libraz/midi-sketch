#ifndef MIDISKETCH_CORE_MOTIF_H
#define MIDISKETCH_CORE_MOTIF_H

#include "types.h"
#include <random>
#include <vector>

namespace midisketch {

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
