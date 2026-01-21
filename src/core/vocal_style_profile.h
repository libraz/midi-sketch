/**
 * @file vocal_style_profile.h
 * @brief Unified vocal style profiles combining generation bias and evaluation config.
 *
 * This file consolidates StyleBias (generation tendency) and EvaluatorConfig
 * (candidate scoring) into single VocalStyleProfile structures, eliminating
 * dual management of style-specific settings.
 */

#ifndef MIDISKETCH_CORE_VOCAL_STYLE_PROFILE_H
#define MIDISKETCH_CORE_VOCAL_STYLE_PROFILE_H

#include "core/melody_evaluator.h"
#include "core/style_bias.h"
#include "core/types.h"

namespace midisketch {

/// @brief Unified profile combining generation bias and evaluation config.
///
/// Each VocalStylePreset maps to exactly one profile, ensuring consistent
/// behavior between melody generation and candidate evaluation.
struct VocalStyleProfile {
  const char* name;           ///< Profile name for debugging
  StyleBias bias;             ///< Generation probability weights
  EvaluatorConfig evaluator;  ///< Candidate scoring weights
};

// ============================================================================
// Profile Definitions
// ============================================================================

/// @brief Idol profile: singable, repetitive hooks.
constexpr VocalStyleProfile kIdolProfile = {
    "Idol",
    // StyleBias
    {
        1.5f,  // stepwise_weight - encouraged
        0.8f,  // skip_weight - moderate
        0.3f,  // leap_weight - discouraged
        1.2f,  // center_weight - preferred
        0.6f,  // high_weight - limited
        0.8f,  // low_weight - OK
        1.2f,  // onbeat_weight - emphasis
        0.8f,  // offbeat_weight - OK
        0.5f,  // syncopation_weight - minimal
        1.2f,  // same_pitch_weight - encouraged
        1.5f,  // motif_repeat_weight - strong
    },
    // EvaluatorConfig
    {
        0.30f,  // singability_weight - emphasized
        0.20f,  // chord_tone_weight
        0.15f,  // contour_weight
        0.10f,  // surprise_weight
        0.25f,  // aaab_weight - emphasized
    },
};

/// @brief Rock profile: powerful, dynamic range.
constexpr VocalStyleProfile kRockProfile = {
    "Rock",
    // StyleBias
    {
        0.8f,  // stepwise_weight
        1.0f,  // skip_weight
        0.8f,  // leap_weight - allowed
        0.9f,  // center_weight
        1.2f,  // high_weight - emphasized
        1.0f,  // low_weight
        1.0f,  // onbeat_weight
        1.0f,  // offbeat_weight
        0.8f,  // syncopation_weight
        0.8f,  // same_pitch_weight
        1.0f,  // motif_repeat_weight
    },
    // EvaluatorConfig (was kStandardConfig, now custom)
    {
        0.20f,  // singability_weight - slightly lower
        0.25f,  // chord_tone_weight - emphasized
        0.20f,  // contour_weight
        0.20f,  // surprise_weight - higher
        0.15f,  // aaab_weight
    },
};

/// @brief Ballad profile: smooth, singable lines.
constexpr VocalStyleProfile kBalladProfile = {
    "Ballad",
    // StyleBias
    {
        1.6f,  // stepwise_weight - strong preference
        1.0f,  // skip_weight
        0.2f,  // leap_weight - rare
        1.4f,  // center_weight - very centered
        0.5f,  // high_weight - sparse
        0.7f,  // low_weight
        1.3f,  // onbeat_weight - emphasis
        0.6f,  // offbeat_weight
        0.3f,  // syncopation_weight - minimal
        1.0f,  // same_pitch_weight
        1.1f,  // motif_repeat_weight
    },
    // EvaluatorConfig
    {
        0.40f,  // singability_weight - maximum
        0.25f,  // chord_tone_weight
        0.15f,  // contour_weight
        0.05f,  // surprise_weight - minimal
        0.15f,  // aaab_weight
    },
};

/// @brief Anime/YOASOBI profile: dramatic contour, surprising intervals.
constexpr VocalStyleProfile kAnimeProfile = {
    "Anime",
    // StyleBias
    {
        1.0f,  // stepwise_weight
        1.2f,  // skip_weight - encouraged
        0.6f,  // leap_weight - some OK
        0.8f,  // center_weight
        1.3f,  // high_weight - dramatic
        1.1f,  // low_weight - variety
        0.9f,  // onbeat_weight
        1.1f,  // offbeat_weight - interest
        1.0f,  // syncopation_weight
        1.3f,  // same_pitch_weight - hooks
        1.4f,  // motif_repeat_weight - strong
    },
    // EvaluatorConfig
    {
        0.15f,  // singability_weight - lower (difficult OK)
        0.20f,  // chord_tone_weight
        0.25f,  // contour_weight - emphasized
        0.20f,  // surprise_weight - allowed
        0.20f,  // aaab_weight
    },
};

/// @brief Vocaloid profile: technical, wide intervals.
constexpr VocalStyleProfile kVocaloidProfile = {
    "Vocaloid",
    // StyleBias
    {
        0.7f,  // stepwise_weight - less
        1.0f,  // skip_weight
        1.0f,  // leap_weight - allowed
        0.7f,  // center_weight - less centered
        1.2f,  // high_weight - OK
        1.2f,  // low_weight - OK
        0.8f,  // onbeat_weight
        1.2f,  // offbeat_weight - emphasis
        1.3f,  // syncopation_weight - encouraged
        0.9f,  // same_pitch_weight
        1.2f,  // motif_repeat_weight
    },
    // EvaluatorConfig
    {
        0.10f,  // singability_weight - lower
        0.25f,  // chord_tone_weight
        0.20f,  // contour_weight
        0.25f,  // surprise_weight - higher
        0.20f,  // aaab_weight
    },
};

/// @brief Standard pop profile: balanced, versatile.
constexpr VocalStyleProfile kStandardProfile = {
    "Standard",
    // StyleBias
    {
        1.0f,  // stepwise_weight
        1.0f,  // skip_weight
        0.5f,  // leap_weight
        1.0f,  // center_weight
        0.8f,  // high_weight
        0.9f,  // low_weight
        1.0f,  // onbeat_weight
        0.9f,  // offbeat_weight
        0.6f,  // syncopation_weight
        1.0f,  // same_pitch_weight
        1.0f,  // motif_repeat_weight
    },
    // EvaluatorConfig
    {
        0.25f,  // singability_weight
        0.20f,  // chord_tone_weight
        0.20f,  // contour_weight
        0.15f,  // surprise_weight
        0.20f,  // aaab_weight
    },
};

/// @brief City pop profile: sophisticated, jazzy feel.
constexpr VocalStyleProfile kCityPopProfile = {
    "CityPop",
    // StyleBias
    {
        1.2f,  // stepwise_weight - smooth
        1.1f,  // skip_weight
        0.4f,  // leap_weight - limited
        1.1f,  // center_weight
        0.9f,  // high_weight
        1.0f,  // low_weight
        0.9f,  // onbeat_weight
        1.2f,  // offbeat_weight - groove
        1.1f,  // syncopation_weight - OK
        0.9f,  // same_pitch_weight
        1.0f,  // motif_repeat_weight
    },
    // EvaluatorConfig (now custom, was kStandardConfig)
    {
        0.25f,  // singability_weight
        0.25f,  // chord_tone_weight - jazzy harmony
        0.20f,  // contour_weight
        0.15f,  // surprise_weight
        0.15f,  // aaab_weight
    },
};

// ============================================================================
// Profile Selection
// ============================================================================

/// @brief Get unified profile for a vocal style preset.
/// @param style Vocal style preset
/// @returns Reference to appropriate VocalStyleProfile
inline const VocalStyleProfile& getVocalStyleProfile(VocalStylePreset style) {
  switch (style) {
    case VocalStylePreset::Idol:
    case VocalStylePreset::BrightKira:
    case VocalStylePreset::CuteAffected:
      return kIdolProfile;

    case VocalStylePreset::Rock:
    case VocalStylePreset::PowerfulShout:
      return kRockProfile;

    case VocalStylePreset::Ballad:
      return kBalladProfile;

    case VocalStylePreset::Anime:
      return kAnimeProfile;

    case VocalStylePreset::Vocaloid:
    case VocalStylePreset::UltraVocaloid:
    case VocalStylePreset::CoolSynth:
      return kVocaloidProfile;

    case VocalStylePreset::CityPop:
      return kCityPopProfile;

    default:  // Auto, Standard
      return kStandardProfile;
  }
}

// ============================================================================
// Compatibility Wrappers (for gradual migration)
// ============================================================================

/// @brief Get style bias from unified profile.
/// @param style Vocal style preset
/// @returns Reference to StyleBias from profile
inline const StyleBias& getStyleBiasFromProfile(VocalStylePreset style) {
  return getVocalStyleProfile(style).bias;
}

/// @brief Get evaluator config from unified profile.
/// @param style Vocal style preset
/// @returns Reference to EvaluatorConfig from profile
inline const EvaluatorConfig& getEvaluatorConfigFromProfile(VocalStylePreset style) {
  return getVocalStyleProfile(style).evaluator;
}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_VOCAL_STYLE_PROFILE_H
