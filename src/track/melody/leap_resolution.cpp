/**
 * @file leap_resolution.cpp
 * @brief Implementation of leap resolution logic for melody generation.
 */

#include "track/melody/leap_resolution.h"

#include <algorithm>
#include <cmath>

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

int applyLeapReversalRule(int new_pitch, int current_pitch, int prev_interval,
                          const std::vector<int>& chord_tones, uint8_t vocal_low, uint8_t vocal_high,
                          bool prefer_stepwise, std::mt19937& rng) {
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
    float reversal_probability = prefer_stepwise ? 1.0f : 0.80f;
    std::uniform_real_distribution<float> rev_dist(0.0f, 1.0f);
    if (rev_dist(rng) < reversal_probability) {
      return best_reversal_pitch;
    }
  }

  return new_pitch;
}

}  // namespace melody
}  // namespace midisketch
