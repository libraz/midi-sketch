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
        1.45f,  // stepwise_weight - singable group melody
        1.0f,   // skip_weight - allows memorable 3rd/4th movement
        0.45f,  // leap_weight - limited, but not flattened
        1.25f,  // same_pitch_weight - chantable repetition
    },
    // EvaluatorConfig (total = 1.0)
    {
        0.18f,  // singability_weight - still important
        0.10f,  // chord_tone_weight
        0.12f,  // contour_weight - avoid flat chant-only lines
        0.05f,  // surprise_weight - low (predictable hooks)
        0.20f,  // aaab_weight - emphasized
        0.17f,  // rhythm_interval_weight - idol-pop style rhythmic hooks
        0.18f,  // catchiness_weight - high, balanced with rhythm
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
        0.8f,  // same_pitch_weight
    },
    // EvaluatorConfig (total = 1.0)
    {
        0.15f,  // singability_weight - slightly lower
        0.15f,  // chord_tone_weight
        0.15f,  // contour_weight
        0.20f,  // surprise_weight - higher (rock energy)
        0.15f,  // aaab_weight
        0.10f,  // rhythm_interval_weight
        0.10f,  // catchiness_weight - lower (raw energy over hooks)
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
        1.0f,  // same_pitch_weight
    },
    // EvaluatorConfig (total = 1.0)
    {
        0.30f,  // singability_weight - maximum
        0.15f,  // chord_tone_weight
        0.15f,  // contour_weight
        0.05f,  // surprise_weight - minimal
        0.15f,  // aaab_weight
        0.15f,  // rhythm_interval_weight
        0.05f,  // catchiness_weight - low (emotional depth over hooks)
    },
};

/// @brief Anime/AnimeHighEnergy profile: dramatic contour, surprising intervals.
constexpr VocalStyleProfile kAnimeProfile = {
    "Anime",
    // StyleBias
    {
        1.0f,  // stepwise_weight
        1.2f,  // skip_weight - encouraged
        0.6f,  // leap_weight - some OK
        1.3f,  // same_pitch_weight - hooks
    },
    // EvaluatorConfig (total = 1.0)
    {
        0.10f,  // singability_weight - lower (difficult OK)
        0.15f,  // chord_tone_weight
        0.20f,  // contour_weight - emphasized
        0.15f,  // surprise_weight - allowed
        0.15f,  // aaab_weight
        0.10f,  // rhythm_interval_weight
        0.15f,  // catchiness_weight - moderate
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
        0.9f,  // same_pitch_weight
    },
    // EvaluatorConfig (total = 1.0)
    {
        0.10f,  // singability_weight - lower
        0.15f,  // chord_tone_weight
        0.15f,  // contour_weight
        0.20f,  // surprise_weight - higher
        0.15f,  // aaab_weight
        0.10f,  // rhythm_interval_weight
        0.15f,  // catchiness_weight - moderate (AnimeHighEnergy hooks matter)
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
        1.0f,  // same_pitch_weight
    },
    // EvaluatorConfig (total = 1.0)
    {
        0.15f,  // singability_weight
        0.15f,  // chord_tone_weight
        0.15f,  // contour_weight
        0.15f,  // surprise_weight
        0.15f,  // aaab_weight
        0.15f,  // rhythm_interval_weight
        0.10f,  // catchiness_weight - balanced
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
        0.9f,  // same_pitch_weight
    },
    // EvaluatorConfig (total = 1.0)
    {
        0.15f,  // singability_weight
        0.20f,  // chord_tone_weight - jazzy harmony
        0.15f,  // contour_weight
        0.15f,  // surprise_weight
        0.15f,  // aaab_weight
        0.10f,  // rhythm_interval_weight
        0.10f,  // catchiness_weight - lower (sophistication over hooks)
    },
};

/// @brief K-POP profile: syncopated, hook-driven, rap-like passages allowed.
constexpr VocalStyleProfile kKPopProfile = {
    "KPop",
    // StyleBias
    {
        0.9f,  // stepwise_weight - slightly less (allows interval variety)
        1.1f,  // skip_weight - moderate
        0.6f,  // leap_weight - some allowed
        1.4f,  // same_pitch_weight - rap-like repetition
    },
    // EvaluatorConfig (total = 1.0)
    {
        0.12f,  // singability_weight - lower (rap-like passages OK)
        0.10f,  // chord_tone_weight
        0.10f,  // contour_weight
        0.18f,  // surprise_weight - higher (unexpected intervals)
        0.15f,  // aaab_weight
        0.15f,  // rhythm_interval_weight
        0.20f,  // catchiness_weight - high (hook-driven)
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

    case VocalStylePreset::KPop:
      return kKPopProfile;

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
