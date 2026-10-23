/**
 * @file style_bias.h
 * @brief Style-specific probability weights for melody generation.
 *
 * Instead of hard constraints, style is expressed through probability biases.
 * Higher weights increase the likelihood of certain melodic choices.
 *
 * Note: Style presets are now defined in vocal_style_profile.h for unified
 * management with EvaluatorConfig. Use getVocalStyleProfile() to get both.
 */

#ifndef MIDISKETCH_CORE_STYLE_BIAS_H
#define MIDISKETCH_CORE_STYLE_BIAS_H

#include <cstdint>

#include "core/types.h"

namespace midisketch {

/// @brief Style-specific probability weights for melody generation.
///
/// All weights are multipliers (1.0 = neutral, >1.0 = encouraged, <1.0 = discouraged).
/// Scores each whole melody candidate by its interval mix when the designer picks
/// a winner; no weight biases an individual pitch or rhythm choice.
struct StyleBias {
  // === Interval Selection Weights ===
  float stepwise_weight;  ///< 2nd intervals (1-2 semitones)
  float skip_weight;      ///< 3rd intervals (3-4 semitones)
  float leap_weight;      ///< 5th+ intervals (5+ semitones)

  // === Repetition Weights ===
  float same_pitch_weight;  ///< Same note repetition
};

// ============================================================================
// Bias Adjustment Functions
// ============================================================================

/// @brief Adjust style bias based on melodic complexity.
/// @param base Base style bias
/// @param complexity Melodic complexity level
/// @returns Adjusted StyleBias with complexity applied
inline StyleBias adjustBiasForComplexity(const StyleBias& base, MelodicComplexity complexity) {
  StyleBias adjusted = base;

  switch (complexity) {
    case MelodicComplexity::Simple:
      adjusted.stepwise_weight *= 1.3f;
      adjusted.leap_weight *= 0.5f;
      adjusted.same_pitch_weight *= 1.2f;
      break;
    case MelodicComplexity::Complex:
      adjusted.stepwise_weight *= 0.8f;
      adjusted.leap_weight *= 1.3f;
      adjusted.skip_weight *= 1.2f;
      break;
    default:  // Standard - no adjustment
      break;
  }
  return adjusted;
}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_STYLE_BIAS_H
