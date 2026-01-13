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

#include "core/types.h"
#include <cstdint>

namespace midisketch {

/// @brief Style-specific probability weights for melody generation.
///
/// All weights are multipliers (1.0 = neutral, >1.0 = encouraged, <1.0 = discouraged).
/// Used during pitch candidate selection and rhythm generation.
struct StyleBias {
  // === Interval Selection Weights ===
  float stepwise_weight;       ///< 2nd intervals (1-2 semitones)
  float skip_weight;           ///< 3rd intervals (3-4 semitones)
  float leap_weight;           ///< 5th+ intervals (5+ semitones)

  // === Register Weights ===
  float center_weight;         ///< Middle register preference
  float high_weight;           ///< High register preference
  float low_weight;            ///< Low register preference

  // === Rhythm Weights ===
  float onbeat_weight;         ///< Notes on strong beats
  float offbeat_weight;        ///< Notes on weak beats
  float syncopation_weight;    ///< Off-grid placements

  // === Repetition Weights ===
  float same_pitch_weight;     ///< Same note repetition
  float motif_repeat_weight;   ///< Pattern repetition
};

// ============================================================================
// Bias Adjustment Functions
// ============================================================================

/// @brief Adjust style bias based on melodic complexity.
/// @param base Base style bias
/// @param complexity Melodic complexity level
/// @returns Adjusted StyleBias with complexity applied
inline StyleBias adjustBiasForComplexity(const StyleBias& base,
                                          MelodicComplexity complexity) {
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

/// @brief Apply interval bias to a score.
/// @param interval Interval size in semitones
/// @param bias Style bias to apply
/// @returns Weighted score multiplier
inline float applyIntervalBias(int interval, const StyleBias& bias) {
  int abs_interval = (interval < 0) ? -interval : interval;
  if (abs_interval <= 2) {
    return bias.stepwise_weight;
  } else if (abs_interval <= 4) {
    return bias.skip_weight;
  } else {
    return bias.leap_weight;
  }
}

/// @brief Apply register bias to a score.
/// @param pitch MIDI pitch
/// @param center Center of vocal range
/// @param range_width Width of vocal range
/// @param bias Style bias to apply
/// @returns Weighted score multiplier
inline float applyRegisterBias(int pitch, int center, int range_width,
                                const StyleBias& bias) {
  int distance = pitch - center;
  int quarter_range = range_width / 4;

  if (distance > quarter_range) {
    return bias.high_weight;
  } else if (distance < -quarter_range) {
    return bias.low_weight;
  } else {
    return bias.center_weight;
  }
}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_STYLE_BIAS_H
