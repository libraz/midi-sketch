/**
 * @file velocity_constants.h
 * @brief Centralized velocity-related constants.
 *
 * This header consolidates magic numbers from velocity.cpp for maintainability.
 * All velocity multipliers, thresholds, and curve parameters are defined here.
 */

#ifndef MIDISKETCH_CORE_VELOCITY_CONSTANTS_H
#define MIDISKETCH_CORE_VELOCITY_CONSTANTS_H

#include <array>
#include <cstdint>

namespace midisketch {
namespace velocity {

// ============================================================================
// Energy-based Velocity Multipliers
// ============================================================================

/// Velocity multiplier for Low energy sections (calm, sparse).
constexpr float kEnergyLowMultiplier = 0.75f;

/// Velocity multiplier for Medium energy sections (normal).
constexpr float kEnergyMediumMultiplier = 0.90f;

/// Velocity multiplier for High energy sections (active).
constexpr float kEnergyHighMultiplier = 1.00f;

/// Velocity multiplier for Peak energy sections (climax).
constexpr float kEnergyPeakMultiplier = 1.05f;

// ============================================================================
// Phrase Dynamics (4-bar phrase build→hit pattern)
// ============================================================================

/// Minimum velocity multiplier at phrase start.
constexpr float kPhraseMinMultiplier = 0.75f;

/// Maximum velocity multiplier at phrase end.
constexpr float kPhraseMaxMultiplier = 1.00f;

/// Pi constant for cosine interpolation.
constexpr float kPi = 3.14159265358979f;

// ============================================================================
// Section-Level Crescendo
// ============================================================================

/// Chorus crescendo start multiplier (beginning of section).
constexpr float kChorusCrescendoStart = 0.88f;

/// Chorus crescendo range (end - start multiplier).
constexpr float kChorusCrescendoRange = 0.24f;

/// Pre-chorus (B section) crescendo start multiplier.
constexpr float kPreChorusCrescendoStart = 0.95f;

/// Pre-chorus crescendo range.
constexpr float kPreChorusCrescendoRange = 0.05f;

// ============================================================================
// Transition Dynamics
// ============================================================================

/// B→Chorus suppression phase start multiplier.
constexpr float kTransitionSuppressionStart = 0.85f;

/// B→Chorus suppression phase range (end - start).
constexpr float kTransitionSuppressionRange = 0.07f;

/// B→Chorus crescendo phase start multiplier.
constexpr float kTransitionCrescendoStart = 0.92f;

/// B→Chorus crescendo phase range.
constexpr float kTransitionCrescendoRange = 0.08f;

/// Normal crescendo start multiplier (last bar).
constexpr float kNormalCrescendoStart = 0.85f;

/// Normal crescendo end multiplier.
constexpr float kNormalCrescendoEnd = 1.10f;

/// Decrescendo start multiplier.
constexpr float kDecrescendoStart = 1.00f;

/// Decrescendo end multiplier.
constexpr float kDecrescendoEnd = 0.75f;

// ============================================================================
// Entry Pattern Dynamics
// ============================================================================

/// GradualBuild start multiplier (60% velocity).
constexpr float kGradualBuildStart = 0.60f;

/// GradualBuild end multiplier (100% velocity).
constexpr float kGradualBuildEnd = 1.00f;

/// DropIn velocity boost multiplier.
constexpr float kDropInBoost = 1.10f;

// ============================================================================
// Melody Contour Velocity Boosts
// ============================================================================

/// Extra boost for highest notes in climax bars.
constexpr int kClimaxBarsBoost = 10;

/// Base boost for highest notes elsewhere.
constexpr int kNormalHighBoost = 5;

/// Maximum ascending interval boost.
constexpr int kAscendingMaxBoost = 8;

/// Maximum descending interval reduction.
constexpr int kDescendingMaxReduction = -6;

// ============================================================================
// Musical Accent Patterns
// ============================================================================

/// Phrase-head accent boost (first note of 2-bar phrase).
constexpr int kPhraseHeadBoost = 12;

/// Contour accent boost (highest note in phrase).
constexpr int kContourBoost = 15;

/// Agogic accent boost (notes longer than quarter note).
constexpr int kAgogicBoost = 8;

// ============================================================================
// EmotionCurve Velocity Thresholds
// ============================================================================

/// Tension threshold for low tension (ceiling reduction).
constexpr float kTensionLowThreshold = 0.3f;

/// Tension threshold for high tension (ceiling expansion).
constexpr float kTensionHighThreshold = 0.7f;

/// Low tension ceiling multiplier range (0.8 to 1.0).
constexpr float kTensionLowCeilingMin = 0.8f;

/// Low tension ceiling multiplier range width.
constexpr float kTensionLowCeilingRange = 0.2f;

/// High tension ceiling multiplier max bonus.
constexpr float kTensionHighCeilingMaxBonus = 0.2f;

// ============================================================================
// Energy-based Velocity Adjustments
// ============================================================================

/// Energy threshold for low energy.
constexpr float kEnergyLowThreshold = 0.3f;

/// Energy threshold for high energy.
constexpr float kEnergyHighThreshold = 0.7f;

/// Low energy velocity multiplier minimum.
constexpr float kEnergyLowVelocityMin = 0.75f;

/// Low energy velocity multiplier range.
constexpr float kEnergyLowVelocityRange = 0.15f;

/// Medium energy velocity multiplier minimum.
constexpr float kEnergyMediumVelocityMin = 0.90f;

/// Medium energy velocity multiplier range.
constexpr float kEnergyMediumVelocityRange = 0.10f;

/// High energy velocity multiplier max bonus.
constexpr float kEnergyHighVelocityMaxBonus = 0.15f;

// ============================================================================
// Energy-based Density Multipliers
// ============================================================================

/// Low energy density factor minimum (50% of base).
constexpr float kEnergyLowDensityMin = 0.5f;

/// Low energy density factor range.
constexpr float kEnergyLowDensityRange = 0.3f;

/// Medium energy density factor minimum (80% of base).
constexpr float kEnergyMediumDensityMin = 0.8f;

/// Medium energy density factor range.
constexpr float kEnergyMediumDensityRange = 0.2f;

/// High energy density factor max bonus (up to 130%).
constexpr float kEnergyHighDensityMaxBonus = 0.3f;

// ============================================================================
// 16th Note Micro-Dynamics Curve
// ============================================================================

/// 16th-note resolution velocity curve for natural groove.
/// Beat 1: strong→weak→medium→weak
/// Beat 2: medium→weak→medium→weak
/// Beat 3: medium-strong→weak→medium→weak
/// Beat 4: weak→weak→medium→weakest
// clang-format off
constexpr std::array<float, 16> kMicroDynamicsCurve16 = {{
    1.10f, 0.88f, 0.95f, 0.86f,  // Beat 1
    0.97f, 0.90f, 0.93f, 0.88f,  // Beat 2
    1.05f, 0.89f, 0.96f, 0.87f,  // Beat 3
    0.94f, 0.88f, 0.92f, 0.85f   // Beat 4
}};
// clang-format on

// ============================================================================
// Phrase-End Decay
// ============================================================================

/// Velocity decay multiplier at phrase end.
constexpr float kPhraseEndDecay = 0.85f;

/// Phrase length in bars for decay detection.
constexpr int kPhraseBars = 4;

// ============================================================================
// Syncopation Weights by Groove Feel
// ============================================================================

/// Base syncopation weight for Straight feel.
constexpr float kSyncoStraight = 0.08f;

/// Syncopation weight for Swing feel.
constexpr float kSyncoSwing = 0.12f;

/// Syncopation weight for Bouncy8th feel.
constexpr float kSyncoBouncy8th = 0.18f;

/// Syncopation weight for OffBeat feel.
constexpr float kSyncoOffBeat = 0.20f;

/// Syncopation weight for Driving16th feel.
constexpr float kSyncoDriving16th = 0.25f;

/// Syncopation weight for Syncopated feel.
constexpr float kSyncoSyncopated = 0.30f;

/// Default syncopation weight.
constexpr float kSyncoDefault = 0.15f;

/// B section syncopation reduction factor.
constexpr float kSyncoBSectionFactor = 0.7f;

/// Chorus syncopation boost factor.
constexpr float kSyncoChorusFactor = 1.2f;

/// Bridge syncopation reduction factor.
constexpr float kSyncoBridgeFactor = 0.85f;

/// Maximum syncopation weight cap.
constexpr float kSyncoMaxWeight = 0.35f;

// ============================================================================
// Contextual Syncopation Adjustments
// ============================================================================

/// Phrase progress threshold for syncopation boost.
constexpr float kSyncoPhraseProgressThreshold = 0.5f;

/// Maximum phrase-position syncopation boost.
constexpr float kSyncoPhraseBoostMax = 0.5f;

/// Backbeat syncopation boost factor.
constexpr float kSyncoBackbeatBoost = 1.15f;

/// Drop section syncopation boost factor.
constexpr float kSyncoDropBoost = 1.1f;

/// Maximum contextual syncopation weight cap.
constexpr float kSyncoContextualMax = 0.40f;

// ============================================================================
// Phrase Note Velocity Curve
// ============================================================================

/// Pre-climax minimum velocity multiplier.
constexpr float kPhraseNotePreClimaxMin = 0.88f;

/// Climax maximum velocity multiplier.
constexpr float kPhraseNoteClimaxMax = 1.08f;

/// Post-climax minimum velocity multiplier.
constexpr float kPhraseNotePostClimaxMin = 0.92f;

/// Climax position for Peak contour type (60% through phrase).
constexpr float kClimaxPositionPeak = 0.6f;

/// Climax position for other contour types (75% through phrase).
constexpr float kClimaxPositionOther = 0.75f;

// ============================================================================
// Tiered Multiplier Calculation Utility
// ============================================================================

/**
 * @brief Calculate a multiplier using a 3-tier interpolation pattern.
 *
 * Common pattern for energy/tension-based calculations:
 * - Low range (0.0 to low_threshold): interpolate from low_min to low_max
 * - Medium range (low_threshold to high_threshold): interpolate from mid_min to mid_max
 * - High range (high_threshold to 1.0): interpolate from high_min to high_max
 *
 * @param value Input value (0.0 to 1.0)
 * @param low_threshold Boundary between low and medium range
 * @param high_threshold Boundary between medium and high range
 * @param low_min Multiplier at value=0.0
 * @param low_max Multiplier at value=low_threshold (also mid_min)
 * @param high_min Multiplier at value=high_threshold
 * @param high_max Multiplier at value=1.0
 * @return Interpolated multiplier
 */
inline float calculateTieredMultiplier(float value, float low_threshold, float high_threshold,
                                        float low_min, float low_max, float high_min,
                                        float high_max) {
  if (value < low_threshold) {
    // Low range: interpolate from low_min to low_max
    return low_min + (value / low_threshold) * (low_max - low_min);
  } else if (value < high_threshold) {
    // Medium range: interpolate from low_max to high_min
    float progress = (value - low_threshold) / (high_threshold - low_threshold);
    return low_max + progress * (high_min - low_max);
  } else {
    // High range: interpolate from high_min to high_max
    float progress = (value - high_threshold) / (1.0f - high_threshold);
    return high_min + progress * (high_max - high_min);
  }
}

}  // namespace velocity
}  // namespace midisketch

#endif  // MIDISKETCH_CORE_VELOCITY_CONSTANTS_H
