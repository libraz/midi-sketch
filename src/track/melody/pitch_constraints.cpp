/**
 * @file pitch_constraints.cpp
 * @brief Implementation of pitch constraints for melody generation.
 */

#include "track/melody/pitch_constraints.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/chord_utils.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "track/melody/melody_utils.h"

namespace midisketch {
namespace melody {

bool isDownbeat(Tick tick) {
  Tick bar_pos = tick % TICKS_PER_BAR;
  return bar_pos < TICKS_PER_BEAT / 4;
}

bool isStrongBeat(Tick tick) {
  Tick bar_pos = tick % TICKS_PER_BAR;
  Tick beat_in_bar = bar_pos / TICKS_PER_BEAT;
  return beat_in_bar == 0 || beat_in_bar == 2;
}

int findBestChordTonePreservingDirection(int target_pitch, int prev_pitch, int8_t chord_degree,
                                          uint8_t vocal_low, uint8_t vocal_high,
                                          int max_interval) {
  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);

  // Determine intended direction
  bool intended_movement = (target_pitch != prev_pitch);
  int intended_direction = (target_pitch > prev_pitch) ? 1 : -1;

  int best_pitch = target_pitch;
  int best_interval = 127;
  int best_directional_pitch = -1;
  int best_directional_interval = 127;

  for (int ct : chord_tones) {
    for (int oct = 3; oct <= 7; ++oct) {
      int candidate = oct * 12 + ct;
      if (candidate < vocal_low || candidate > vocal_high) continue;

      int interval = std::abs(candidate - prev_pitch);
      if (max_interval > 0 && interval > max_interval) continue;

      // Track absolute best
      if (interval < best_interval) {
        best_interval = interval;
        best_pitch = candidate;
      }

      // Track best in intended direction (excluding same pitch)
      if (candidate != prev_pitch) {
        int direction = (candidate > prev_pitch) ? 1 : -1;
        if (direction == intended_direction && interval < best_directional_interval) {
          best_directional_interval = interval;
          best_directional_pitch = candidate;
        }
      }
    }
  }

  // If movement was intended but best is same pitch, use directional best
  if (intended_movement && best_pitch == prev_pitch && best_directional_pitch >= 0 &&
      best_directional_interval <= 5) {  // Allow up to P4 (5 semitones)
    return best_directional_pitch;
  }

  return best_pitch;
}

int enforceDownbeatChordTone(int pitch, Tick tick, int8_t chord_degree, int prev_pitch,
                              uint8_t vocal_low, uint8_t vocal_high,
                              bool disable_singability) {
  if (!isDownbeat(tick)) {
    return pitch;
  }

  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);
  int pitch_pc = pitch % 12;

  // Check if already a chord tone
  for (int ct : chord_tones) {
    if (pitch_pc == ct) {
      return pitch;  // Already a chord tone
    }
  }

  // Need to adjust to chord tone
  if (disable_singability) {
    // Simple nearest chord tone for machine-style vocals
    int new_pitch = nearestChordTonePitch(pitch, chord_degree);
    return std::clamp(new_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
  }

  // Use direction-preserving adjustment for natural vocals
  return findBestChordTonePreservingDirection(pitch, prev_pitch, chord_degree,
                                               vocal_low, vocal_high, 0);
}

int enforceAvoidNoteConstraint(int pitch, int8_t chord_degree,
                                uint8_t vocal_low, uint8_t vocal_high) {
  int bass_root_pc = getBassRootPitchClass(chord_degree);
  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);
  int pitch_pc = pitch % 12;

  if (isAvoidNoteWithChord(pitch_pc, chord_tones, bass_root_pc)) {
    return getNearestSafeChordTone(pitch, chord_degree, bass_root_pc, vocal_low, vocal_high);
  }

  return pitch;
}

int enforceMaxIntervalConstraint(int new_pitch, int prev_pitch, int8_t chord_degree,
                                  int max_interval, uint8_t vocal_low, uint8_t vocal_high,
                                  const TessituraRange* tessitura) {
  int interval = std::abs(new_pitch - prev_pitch);
  if (interval <= max_interval) {
    return new_pitch;
  }

  return nearestChordToneWithinInterval(new_pitch, prev_pitch, chord_degree,
                                         max_interval, vocal_low, vocal_high, tessitura);
}

int applyLeapPreparationConstraint(int new_pitch, int prev_pitch, Tick prev_duration,
                                    int8_t chord_degree, uint8_t vocal_low, uint8_t vocal_high,
                                    const TessituraRange* tessitura) {
  // Short note threshold: 8th note (240 ticks)
  constexpr Tick SHORT_NOTE_THRESHOLD = TICK_EIGHTH;
  // Maximum leap after short note: 5 semitones (perfect 4th)
  constexpr int MAX_LEAP_AFTER_SHORT = 5;

  if (prev_duration >= SHORT_NOTE_THRESHOLD) {
    return new_pitch;  // Not a short note, no restriction
  }

  int leap = std::abs(new_pitch - prev_pitch);
  if (leap <= MAX_LEAP_AFTER_SHORT) {
    return new_pitch;  // Leap is within allowed range
  }

  // Constrain to maximum allowed leap
  return nearestChordToneWithinInterval(new_pitch, prev_pitch, chord_degree,
                                         MAX_LEAP_AFTER_SHORT, vocal_low, vocal_high, tessitura);
}

int encourageLeapAfterLongNote(int new_pitch, int prev_pitch, Tick prev_duration,
                                int8_t chord_degree, uint8_t vocal_low, uint8_t vocal_high,
                                std::mt19937& rng) {
  // Long note threshold: 1 beat (quarter note)
  constexpr Tick LONG_NOTE_THRESHOLD = TICKS_PER_BEAT;
  // Preferred minimum leap: major 3rd (4 semitones)
  constexpr int PREFERRED_LEAP_AFTER_LONG = 4;
  // Probability of encouraging leap
  constexpr float LEAP_ENCOURAGE_PROB = 0.6f;

  if (prev_duration < LONG_NOTE_THRESHOLD) {
    return new_pitch;  // Not a long note
  }

  int current_interval = std::abs(new_pitch - prev_pitch);
  if (current_interval >= PREFERRED_LEAP_AFTER_LONG) {
    return new_pitch;  // Already has sufficient movement
  }

  // Probabilistically encourage leap
  std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);
  if (prob_dist(rng) >= LEAP_ENCOURAGE_PROB) {
    return new_pitch;  // Keep original
  }

  // Find chord tones at preferred leap distance
  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);
  std::vector<int> leap_candidates;

  for (int pc : chord_tones) {
    for (int oct = 4; oct <= 6; ++oct) {
      int candidate = oct * 12 + pc;
      int interval = std::abs(candidate - prev_pitch);
      if (candidate >= vocal_low && candidate <= vocal_high &&
          interval >= PREFERRED_LEAP_AFTER_LONG && interval <= kMaxMelodicInterval) {
        leap_candidates.push_back(candidate);
      }
    }
  }

  if (leap_candidates.empty()) {
    return new_pitch;
  }

  // Pick random leap candidate
  std::uniform_int_distribution<size_t> idx_dist(0, leap_candidates.size() - 1);
  return leap_candidates[idx_dist(rng)];
}

}  // namespace melody
}  // namespace midisketch
