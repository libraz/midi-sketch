/**
 * @file note_constraints.cpp
 * @brief Implementation of note constraints for melody generation.
 */

#include "track/melody/note_constraints.h"

#include <algorithm>
#include <cmath>

#include "core/chord_utils.h"

namespace midisketch {
namespace melody {

float ConsecutiveSameNoteTracker::getAllowProbability() const {
  // J-POP style probability curve:
  // Rhythmic repetition is common but should taper off naturally
  // Music theory: 4+ consecutive same notes is monotonous and should be avoided
  switch (count) {
    case 0:
    case 1:
      return 1.0f;   // First note always OK
    case 2:
      return 0.70f;  // 2nd repetition: 70%
    case 3:
      return 0.30f;  // 3rd repetition: 30%
    default:
      return 0.0f;   // 4+: never allow (force movement)
  }
}

bool ConsecutiveSameNoteTracker::shouldForceMovement(std::mt19937& rng) const {
  float allow_prob = getAllowProbability();
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  return dist(rng) > allow_prob;
}

bool isChordTone(int pitch_pc, int8_t chord_degree) {
  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);
  for (int ct : chord_tones) {
    if (pitch_pc == ct) {
      return true;
    }
  }
  return false;
}

int findNearestDifferentChordTone(int current_pitch, int8_t chord_degree,
                                   uint8_t vocal_low, uint8_t vocal_high,
                                   int max_interval) {
  std::vector<int> candidates;

  // First priority: chord tones (most harmonically stable)
  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);
  for (int pc : chord_tones) {
    for (int oct = 3; oct <= 6; ++oct) {
      int candidate = oct * 12 + pc;
      if (candidate >= vocal_low && candidate <= vocal_high &&
          candidate != current_pitch) {
        if (max_interval > 0 && std::abs(candidate - current_pitch) > max_interval) {
          continue;
        }
        candidates.push_back(candidate);
      }
    }
  }

  // Second priority: if no chord tones found, use all diatonic scale tones
  // C major scale: C=0, D=2, E=4, F=5, G=7, A=9, B=11
  if (candidates.empty()) {
    std::vector<int> scale_tones = {0, 2, 4, 5, 7, 9, 11};
    for (int pc : scale_tones) {
      for (int oct = 3; oct <= 6; ++oct) {
        int candidate = oct * 12 + pc;
        if (candidate >= vocal_low && candidate <= vocal_high &&
            candidate != current_pitch) {
          if (max_interval > 0 && std::abs(candidate - current_pitch) > max_interval) {
            continue;
          }
          candidates.push_back(candidate);
        }
      }
    }
  }

  if (candidates.empty()) {
    return current_pitch;  // No valid candidate found (should be rare)
  }

  // Pick closest different pitch (prefer stepwise motion)
  int best = candidates[0];
  int best_dist = std::abs(best - current_pitch);
  for (int c : candidates) {
    int dist = std::abs(c - current_pitch);
    if (dist > 0 && dist < best_dist) {
      best = c;
      best_dist = dist;
    }
  }

  return best;
}

bool applyConsecutiveSameNoteConstraint(int& pitch, ConsecutiveSameNoteTracker& tracker,
                                         int prev_pitch, int8_t chord_degree,
                                         uint8_t vocal_low, uint8_t vocal_high,
                                         int max_interval, std::mt19937& rng) {
  if (pitch == prev_pitch) {
    tracker.increment();
    if (tracker.shouldForceMovement(rng)) {
      int new_pitch = findNearestDifferentChordTone(
          pitch, chord_degree, vocal_low, vocal_high, max_interval);
      if (new_pitch != pitch) {
        pitch = new_pitch;
        tracker.reset();
        return true;
      }
    }
  } else {
    tracker.reset();
  }
  return false;
}

}  // namespace melody
}  // namespace midisketch
