/**
 * @file style_bias.h
 * @brief Style-specific probability weights for melody generation.
 *
 * Instead of hard constraints, style is expressed through probability biases.
 * Higher weights increase the likelihood of certain melodic choices.
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
// Preset Style Biases (C++17 compatible initialization)
// Order: stepwise, skip, leap, center, high, low, onbeat, offbeat, syncopation,
//        same_pitch, motif_repeat
// ============================================================================

/// @brief Idol-style bias: singable, repetitive hooks.
constexpr StyleBias kIdolBias = {
    1.5f,   // stepwise_weight - encouraged
    0.8f,   // skip_weight - moderate
    0.3f,   // leap_weight - discouraged
    1.2f,   // center_weight - preferred
    0.6f,   // high_weight - limited
    0.8f,   // low_weight - OK
    1.2f,   // onbeat_weight - emphasis
    0.8f,   // offbeat_weight - OK
    0.5f,   // syncopation_weight - minimal
    1.2f,   // same_pitch_weight - encouraged
    1.5f,   // motif_repeat_weight - strong
};

/// @brief Rock-style bias: powerful, dynamic range.
constexpr StyleBias kRockBias = {
    0.8f,   // stepwise_weight
    1.0f,   // skip_weight
    0.8f,   // leap_weight - allowed
    0.9f,   // center_weight
    1.2f,   // high_weight - emphasized
    1.0f,   // low_weight
    1.0f,   // onbeat_weight
    1.0f,   // offbeat_weight
    0.8f,   // syncopation_weight
    0.8f,   // same_pitch_weight
    1.0f,   // motif_repeat_weight
};

/// @brief Ballad-style bias: smooth, singable lines.
constexpr StyleBias kBalladBias = {
    1.6f,   // stepwise_weight - strong preference
    1.0f,   // skip_weight
    0.2f,   // leap_weight - rare
    1.4f,   // center_weight - very centered
    0.5f,   // high_weight - sparse
    0.7f,   // low_weight
    1.3f,   // onbeat_weight - emphasis
    0.6f,   // offbeat_weight
    0.3f,   // syncopation_weight - minimal
    1.0f,   // same_pitch_weight
    1.1f,   // motif_repeat_weight
};

/// @brief YOASOBI-style bias: dramatic contour, surprising intervals.
constexpr StyleBias kYoasobiBias = {
    1.0f,   // stepwise_weight
    1.2f,   // skip_weight - encouraged
    0.6f,   // leap_weight - some OK
    0.8f,   // center_weight
    1.3f,   // high_weight - dramatic
    1.1f,   // low_weight - variety
    0.9f,   // onbeat_weight
    1.1f,   // offbeat_weight - interest
    1.0f,   // syncopation_weight
    1.3f,   // same_pitch_weight - hooks
    1.4f,   // motif_repeat_weight - strong
};

/// @brief Vocaloid-style bias: technical, wide intervals.
constexpr StyleBias kVocaloidBias = {
    0.7f,   // stepwise_weight - less
    1.0f,   // skip_weight
    1.0f,   // leap_weight - allowed
    0.7f,   // center_weight - less centered
    1.2f,   // high_weight - OK
    1.2f,   // low_weight - OK
    0.8f,   // onbeat_weight
    1.2f,   // offbeat_weight - emphasis
    1.3f,   // syncopation_weight - encouraged
    0.9f,   // same_pitch_weight
    1.2f,   // motif_repeat_weight
};

/// @brief Standard pop bias: balanced, versatile.
constexpr StyleBias kStandardBias = {
    1.0f,   // stepwise_weight
    1.0f,   // skip_weight
    0.5f,   // leap_weight
    1.0f,   // center_weight
    0.8f,   // high_weight
    0.9f,   // low_weight
    1.0f,   // onbeat_weight
    0.9f,   // offbeat_weight
    0.6f,   // syncopation_weight
    1.0f,   // same_pitch_weight
    1.0f,   // motif_repeat_weight
};

/// @brief City pop bias: sophisticated, jazzy feel.
constexpr StyleBias kCityPopBias = {
    1.2f,   // stepwise_weight - smooth
    1.1f,   // skip_weight
    0.4f,   // leap_weight - limited
    1.1f,   // center_weight
    0.9f,   // high_weight
    1.0f,   // low_weight
    0.9f,   // onbeat_weight
    1.2f,   // offbeat_weight - groove
    1.1f,   // syncopation_weight - OK
    0.9f,   // same_pitch_weight
    1.0f,   // motif_repeat_weight
};

// ============================================================================
// Style Bias Selection
// ============================================================================

/// @brief Get style bias for a vocal style preset.
/// @param style Vocal style preset
/// @returns Reference to appropriate StyleBias
inline const StyleBias& getStyleBias(VocalStylePreset style) {
  switch (style) {
    case VocalStylePreset::Idol:
    case VocalStylePreset::BrightKira:
    case VocalStylePreset::CuteAffected:
      return kIdolBias;

    case VocalStylePreset::Rock:
    case VocalStylePreset::PowerfulShout:
      return kRockBias;

    case VocalStylePreset::Ballad:
      return kBalladBias;

    case VocalStylePreset::Anime:
      return kYoasobiBias;

    case VocalStylePreset::Vocaloid:
    case VocalStylePreset::UltraVocaloid:
    case VocalStylePreset::CoolSynth:
      return kVocaloidBias;

    case VocalStylePreset::CityPop:
      return kCityPopBias;

    default:  // Auto, Standard
      return kStandardBias;
  }
}

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
