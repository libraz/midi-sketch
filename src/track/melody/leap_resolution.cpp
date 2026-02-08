/**
 * @file leap_resolution.cpp
 * @brief Implementation of leap resolution logic for melody generation.
 */

#include "track/melody/leap_resolution.h"

#include <algorithm>
#include <cmath>

#include "core/rng_util.h"

namespace midisketch {
namespace melody {

int findStepwiseResolutionPitch(int current_pitch, const std::vector<int>& chord_tones,
                                 int resolution_direction, uint8_t vocal_low, uint8_t vocal_high) {
  int best_pitch = -1;
  int best_interval = 127;

  for (int ct : chord_tones) {
    for (int oct = 4; oct <= 6; ++oct) {
      int candidate = oct * 12 + ct;
      if (candidate < vocal_low || candidate > vocal_high) continue;

      int candidate_interval = candidate - current_pitch;
      int candidate_direction = (candidate_interval > 0) ? 1 : (candidate_interval < 0) ? -1 : 0;

      // Must be in resolution direction and be stepwise (1-3 semitones)
      if (candidate_direction == resolution_direction) {
        int abs_step = std::abs(candidate_interval);
        if (abs_step >= 1 && abs_step <= 3 && abs_step < best_interval) {
          best_interval = abs_step;
          best_pitch = candidate;
        }
      }
    }
  }

  return best_pitch;
}

// Section-type and phrase-position dependent reversal probability.
// SectionType enum: Intro=0, A=1, B=2, Chorus=3, Bridge=4, ...
static float getReversalProbability(int8_t section_type_int, float phrase_position) {
  // Base probability by section type
  float base_prob = 0.80f;  // Default
  float phrase_end_prob = 0.80f;  // Probability at phrase end (>0.8)

  // Section-specific probabilities
  // Index: SectionType enum value
  switch (section_type_int) {
    case 1:  // A (Verse): stable, resolves well
      base_prob = 0.85f;
      phrase_end_prob = 0.95f;
      break;
    case 2:  // B (Pre-chorus): maintain forward momentum
      base_prob = 0.80f;
      phrase_end_prob = 0.70f;  // Less resolution for drive toward chorus
      break;
    case 3:  // Chorus: allow sustained peaks
      base_prob = 0.75f;
      phrase_end_prob = 0.85f;
      break;
    case 4:  // Bridge: exploratory then resolve
      base_prob = 0.90f;
      phrase_end_prob = 0.95f;
      break;
    default:
      break;
  }

  // Apply phrase-end modification
  if (phrase_position >= 0.0f && phrase_position > 0.8f) {
    return phrase_end_prob;
  }
  return base_prob;
}

int applyLeapReversalRule(int new_pitch, int current_pitch, int prev_interval,
                          const std::vector<int>& chord_tones, uint8_t vocal_low, uint8_t vocal_high,
                          bool prefer_stepwise, std::mt19937& rng,
                          int8_t section_type_int, float phrase_position) {
  // Skip if no significant previous leap
  if (std::abs(prev_interval) < kLeapReversalThreshold) {
    return new_pitch;
  }

  // Skip if staying on same pitch
  if (new_pitch == current_pitch) {
    return new_pitch;
  }

  int current_interval = new_pitch - current_pitch;
  bool is_same_direction =
      (prev_interval > 0 && current_interval > 0) || (prev_interval < 0 && current_interval < 0);

  // Only apply reversal if continuing in same direction after leap
  if (!is_same_direction) {
    return new_pitch;
  }

  // Try to find a chord tone in the opposite direction (step motion)
  int preferred_direction = (prev_interval > 0) ? -1 : 1;  // Opposite of leap

  int best_reversal_pitch = -1;
  int best_reversal_interval = 127;

  for (int ct : chord_tones) {
    for (int oct = 4; oct <= 6; ++oct) {
      int candidate = oct * 12 + ct;
      if (candidate < vocal_low || candidate > vocal_high) continue;

      int interval_from_current = candidate - current_pitch;
      int direction = (interval_from_current > 0) ? 1 : (interval_from_current < 0) ? -1 : 0;

      // Must be in preferred direction and be a step (1-3 semitones)
      if (direction == preferred_direction) {
        int abs_interval = std::abs(interval_from_current);
        if (abs_interval >= 1 && abs_interval <= 3 && abs_interval < best_reversal_interval) {
          best_reversal_interval = abs_interval;
          best_reversal_pitch = candidate;
        }
      }
    }
  }

  // Apply reversal if found a good candidate
  // If prefer_stepwise is set (IdolKawaii), force 100% stepwise motion
  if (best_reversal_pitch >= 0) {
    float reversal_probability = prefer_stepwise ? 1.0f
        : getReversalProbability(section_type_int, phrase_position);
    if (rng_util::rollProbability(rng, reversal_probability)) {
      return best_reversal_pitch;
    }
  }

  return new_pitch;
}

}  // namespace melody
}  // namespace midisketch
