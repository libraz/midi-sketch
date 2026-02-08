/**
 * @file contour_direction.cpp
 * @brief Implementation of pitch direction and contour control.
 */

#include "track/melody/contour_direction.h"

#include <algorithm>
#include <cmath>

#include "core/rng_util.h"

namespace midisketch {
namespace melody {

float getDirectionBiasForContour(ContourType contour, float phrase_pos) {
  switch (contour) {
    case ContourType::Ascending:
      // Gradually stronger upward bias toward phrase end
      return 0.65f + phrase_pos * 0.15f;  // 0.65 -> 0.80
    case ContourType::Descending:
      // Gradually stronger downward bias toward phrase end
      return 0.35f - phrase_pos * 0.15f;  // 0.35 -> 0.20
    case ContourType::Peak:
      // Rise in first half, fall in second half (arch shape)
      return phrase_pos < 0.5f ? 0.70f : 0.30f;
    case ContourType::Valley:
      // Fall in first half, rise in second half (bowl shape)
      return phrase_pos < 0.5f ? 0.30f : 0.70f;
    case ContourType::Plateau:
      // Balanced, no strong direction preference
      return 0.50f;
  }
  return 0.50f;  // Default: balanced
}

PitchChoice selectPitchChoice(const MelodyTemplate& tmpl, float phrase_pos, bool has_target,
                              SectionType section_type, std::mt19937& rng, float note_eighths,
                              std::optional<ContourType> forced_contour) {
  // Rhythm-melody coupling: note duration affects plateau probability
  // Short notes (16th or less) prefer staying on same pitch for stability
  // Long notes (half or longer) encourage movement for melodic interest
  float effective_plateau_ratio = tmpl.plateau_ratio;
  if (note_eighths < 1.0f) {
    // Very short notes: boost plateau ratio for stability
    effective_plateau_ratio = std::min(0.8f, tmpl.plateau_ratio + 0.15f);
  } else if (note_eighths >= 4.0f) {
    // Long notes: reduce plateau ratio to encourage movement
    effective_plateau_ratio = std::max(0.1f, tmpl.plateau_ratio - 0.1f);
  }

  // Step 1: Check for same pitch (plateau)
  if (rng_util::rollProbability(rng, effective_plateau_ratio)) {
    return PitchChoice::Same;
  }

  // Step 2: Target attraction (if applicable)
  if (has_target && tmpl.has_target_pitch) {
    if (phrase_pos >= tmpl.target_attraction_start) {
      if (rng_util::rollProbability(rng, tmpl.target_attraction_strength)) {
        return PitchChoice::TargetStep;
      }
    }
  }

  // Step 3: Directional bias
  // Use forced contour if specified, otherwise use section-aware defaults
  float upward_bias;
  if (forced_contour.has_value()) {
    // Phrase contour template: explicit control over melodic shape
    upward_bias = getDirectionBiasForContour(*forced_contour, phrase_pos);
  } else {
    // Section-aware directional bias (default behavior):
    // - A (Verse): slightly ascending for storytelling momentum
    // - B (Pre-chorus): ascending more strongly in second half for tension building
    // - Chorus: balanced for hook memorability
    // - Bridge: slightly descending for contrast
    switch (section_type) {
      case SectionType::A:
        upward_bias = 0.55f;  // Slight upward tendency
        break;
      case SectionType::B:
        upward_bias = phrase_pos > 0.5f ? 0.65f : 0.55f;  // Strong rise in second half
        break;
      case SectionType::Chorus:
        upward_bias = 0.50f;  // Balanced
        break;
      case SectionType::Bridge:
        upward_bias = 0.45f;  // Slight downward for contrast
        break;
      default:
        upward_bias = 0.50f;  // Balanced for other sections
        break;
    }
  }
  return rng_util::rollProbability(rng, upward_bias) ? PitchChoice::StepUp : PitchChoice::StepDown;
}

PitchChoice applyDirectionInertia(PitchChoice choice, int inertia,
                                  std::mt19937& rng) {
  // Same pitch or target step - don't modify
  if (choice == PitchChoice::Same || choice == PitchChoice::TargetStep) {
    return choice;
  }

  // Strong inertia can override random direction
  // Coefficient 0.7 for better melodic continuity (was 0.5)
  constexpr float kInertiaCoefficient = 0.7f;

  int abs_inertia = std::abs(inertia);

  // Decay after 3 consecutive same-direction moves to prevent monotony
  float decay_factor = 1.0f;
  if (abs_inertia > 3) {
    decay_factor = std::pow(0.8f, static_cast<float>(abs_inertia - 3));
  }

  float inertia_strength = (static_cast<float>(abs_inertia) / 3.0f) * decay_factor;

  if (rng_util::rollFloat(rng, 0.0f, 1.0f) < inertia_strength * kInertiaCoefficient) {
    // Follow inertia direction
    if (inertia > 0) {
      return PitchChoice::StepUp;
    } else if (inertia < 0) {
      return PitchChoice::StepDown;
    }
  }

  return choice;
}

float getEffectivePlateauRatio(const MelodyTemplate& tmpl, int current_pitch,
                               const TessituraRange& tessitura) {
  float base_ratio = tmpl.plateau_ratio;

  // Boost plateau ratio in high register for stability
  if (current_pitch > tessitura.high) {
    base_ratio += tmpl.high_register_plateau_boost;
  }

  // Also boost slightly near tessitura boundaries
  if (current_pitch <= tessitura.low + 2 || current_pitch >= tessitura.high - 2) {
    base_ratio += 0.1f;
  }

  return std::min(base_ratio, 0.9f);  // Cap at 90%
}

bool shouldLeap(LeapTrigger trigger, float phrase_pos, float section_pos) {
  switch (trigger) {
    case LeapTrigger::None:
      return false;

    case LeapTrigger::PhraseStart:
      return phrase_pos < 0.1f;

    case LeapTrigger::EmotionalPeak:
      // Emotional peak typically around 60-80% of section
      return section_pos >= 0.6f && section_pos <= 0.8f;

    case LeapTrigger::SectionBoundary:
      return section_pos < 0.05f || section_pos > 0.95f;
  }

  return false;
}

int getStabilizeStep(int leap_direction, int max_step) {
  // Return opposite direction, smaller magnitude
  int stabilize = -leap_direction;
  int magnitude = std::max(1, max_step / 2);
  return stabilize * magnitude;
}

bool isInSameVowelSection(float pos1, float pos2, [[maybe_unused]] uint8_t phrase_length) {
  // Simple vowel section model: divide phrase into 2-beat sections
  constexpr float VOWEL_SECTION_BEATS = 2.0f;

  int section1 = static_cast<int>(pos1 / VOWEL_SECTION_BEATS);
  int section2 = static_cast<int>(pos2 / VOWEL_SECTION_BEATS);

  return section1 == section2;
}

int8_t getMaxStepInVowelSection(bool in_same_vowel) { return in_same_vowel ? 2 : 4; }

}  // namespace melody
}  // namespace midisketch
