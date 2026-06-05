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
#include "core/rng_util.h"
#include "core/timing_constants.h"
#include "track/melody/melody_utils.h"

namespace midisketch {
namespace melody {

bool isDownbeat(Tick tick) {
  Tick bar_pos = positionInBar(tick);
  return bar_pos < TICKS_PER_BEAT / 4;
}

bool isStrongBeat(Tick tick) {
  uint8_t beat = beatInBar(tick);
  return beat == 0 || beat == 2;
}

int findBestChordTonePreservingDirection(int target_pitch, int prev_pitch, int8_t chord_degree,
                                         uint8_t vocal_low, uint8_t vocal_high, int max_interval) {
  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);

  // Determine intended direction
  bool intended_movement = (target_pitch != prev_pitch);
  int intended_direction = (target_pitch > prev_pitch) ? 1 : -1;

  // Choose the chord tone NEAREST TO THE PROPOSED PITCH (not to prev_pitch).
  // In a diatonic context the nearest chord tone is at most a step away from
  // any scale tone, so this preserves the melodic line; minimizing distance
  // from prev_pitch instead erased intended motion (snap back to prev) or
  // replaced steps with chord-tone leaps.
  int best_pitch = target_pitch;
  int best_dist = 127;
  int best_directional_pitch = -1;
  int best_directional_dist = 127;

  for (int ct : chord_tones) {
    for (int oct = 3; oct <= 7; ++oct) {
      int candidate = oct * 12 + ct;
      if (candidate < vocal_low || candidate > vocal_high) continue;

      int interval_from_prev = std::abs(candidate - prev_pitch);
      if (max_interval > 0 && interval_from_prev > max_interval) continue;

      int dist = std::abs(candidate - target_pitch);

      // Track absolute best (closest to proposed pitch)
      if (dist < best_dist) {
        best_dist = dist;
        best_pitch = candidate;
      }

      // Track best in intended direction (excluding same pitch as prev)
      if (candidate != prev_pitch) {
        int direction = (candidate > prev_pitch) ? 1 : -1;
        if (direction == intended_direction && dist < best_directional_dist) {
          best_directional_dist = dist;
          best_directional_pitch = candidate;
        }
      }
    }
  }

  // If movement was intended but best is same pitch as prev, use directional
  // best to preserve the line's motion (capped at a 3rd: a perfect 4th here
  // turned intended steps into leaps on every bar start)
  if (intended_movement && best_pitch == prev_pitch && best_directional_pitch >= 0 &&
      std::abs(best_directional_pitch - prev_pitch) <= 4) {
    return best_directional_pitch;
  }

  return best_pitch;
}

int enforceDownbeatChordTone(int pitch, Tick tick, int8_t chord_degree, int prev_pitch,
                             uint8_t vocal_low, uint8_t vocal_high, bool disable_singability,
                             Tick duration) {
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

  // Appoggiatura exemption: a non-sustained bar-start note reached by step
  // may stay on a non-chord scale tone (classic pop suspension/appoggiatura).
  // Snapping every bar start to a chord tone converted stepwise lines into
  // chord-tone 3rds at each barline. Sustained notes (>= half) still anchor.
  if (!disable_singability && duration > 0 && duration < TICK_HALF && prev_pitch >= 0) {
    int interval_from_prev = std::abs(pitch - prev_pitch);
    if (interval_from_prev >= 1 && interval_from_prev <= 2) {
      return pitch;
    }
  }

  // Need to adjust to chord tone
  if (disable_singability) {
    // Simple nearest chord tone for machine-style vocals
    int new_pitch = nearestChordTonePitch(pitch, chord_degree);
    return std::clamp(new_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
  }

  // Use direction-preserving adjustment for natural vocals
  return findBestChordTonePreservingDirection(pitch, prev_pitch, chord_degree, vocal_low,
                                              vocal_high, 0);
}

int enforceGuideToneOnDownbeat(int pitch, Tick tick, int8_t chord_degree, uint8_t vocal_low,
                               uint8_t vocal_high, uint8_t guide_tone_rate, std::mt19937& rng,
                               int prev_pitch) {
  if (guide_tone_rate == 0) return pitch;
  if (!isStrongBeat(tick)) return pitch;

  // Roll probability
  if (rng_util::rollRange(rng, 1, 100) > guide_tone_rate) return pitch;

  // Get guide tones (3rd and 7th) for this chord
  std::vector<int> guide_pcs = getGuideTonePitchClasses(chord_degree);
  if (guide_pcs.empty()) return pitch;

  // Check if already a guide tone
  int pitch_pc = pitch % 12;
  for (int gpc : guide_pcs) {
    if (pitch_pc == gpc) return pitch;
  }

  // Find nearest guide tone in range. Only snap when the guide tone is
  // within a whole step: snapping across larger distances manufactured
  // leaps on every strong beat, which reference vocal corpora do not show.
  constexpr int kMaxGuideToneSnapDistance = 2;
  int best_pitch = -1;
  int best_dist = 127;
  for (int gpc : guide_pcs) {
    for (int oct = 4; oct <= 6; ++oct) {
      int candidate = oct * 12 + gpc;
      if (candidate < vocal_low || candidate > vocal_high) continue;
      int dist = std::abs(candidate - pitch);
      if (dist < best_dist) {
        best_dist = dist;
        best_pitch = candidate;
      }
    }
  }

  if (best_pitch < 0 || best_dist > kMaxGuideToneSnapDistance) return pitch;

  // Don't let the snap degrade melodic motion: a step from the previous
  // pitch must not become a 3rd (or a move collapse to a repeat) just to
  // reach a guide tone.
  if (prev_pitch >= 0) {
    int interval_before = std::abs(pitch - prev_pitch);
    int interval_after = std::abs(best_pitch - prev_pitch);
    bool was_step = interval_before >= 1 && interval_before <= 2;
    bool stays_step = interval_after >= 1 && interval_after <= 2;
    if (was_step && !stays_step) return pitch;
  }

  return best_pitch;
}

int enforceAvoidNoteConstraint(int pitch, int8_t chord_degree, uint8_t vocal_low,
                               uint8_t vocal_high, [[maybe_unused]] Tick tick, Tick duration) {
  // Passing-tone exemption: notes shorter than a half note may sound avoid
  // notes freely (passing tones, neighbor tones, appoggiaturas). Snapping
  // every such note to a chord tone destroyed stepwise lines (steps became
  // chord-tone 3rds or static repeats). Only sustained notes (>= half) keep
  // the theoretical avoid-note enforcement; real inter-track dissonance is
  // still caught by collision detection downstream.
  if (duration > 0 && duration < TICK_HALF) {
    return pitch;
  }

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

  return nearestChordToneWithinInterval(new_pitch, prev_pitch, chord_degree, max_interval,
                                        vocal_low, vocal_high, tessitura);
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
  return nearestChordToneWithinInterval(new_pitch, prev_pitch, chord_degree, MAX_LEAP_AFTER_SHORT,
                                        vocal_low, vocal_high, tessitura);
}

int encourageMovementAfterLongNote(int new_pitch, int prev_pitch, Tick prev_duration,
                                   int8_t chord_degree, int key_offset, uint8_t vocal_low,
                                   uint8_t vocal_high, std::mt19937& rng) {
  // Long note threshold: 1 beat (quarter note)
  constexpr Tick LONG_NOTE_THRESHOLD = TICKS_PER_BEAT;
  // Probability of encouraging movement
  constexpr float MOVEMENT_ENCOURAGE_PROB = 0.6f;
  // Fallback chord-tone movement cap: major 3rd (4 semitones)
  constexpr int MAX_FALLBACK_MOVE = 4;

  if (prev_duration < LONG_NOTE_THRESHOLD) {
    return new_pitch;  // Not a long note
  }

  if (new_pitch != prev_pitch) {
    return new_pitch;  // Already moving - any motion after a sustain is fine
  }

  // Probabilistically encourage movement
  if (!rng_util::rollProbability(rng, MOVEMENT_ENCOURAGE_PROB)) {
    return new_pitch;  // Keep original
  }

  // Prefer scale steps (whole step first) in either direction. Reference
  // vocals overwhelmingly leave sustains by step, not by leap.
  std::vector<int> step_candidates;
  for (int step : {2, 1}) {
    for (int dir : {1, -1}) {
      int candidate = prev_pitch + dir * step;
      if (candidate >= vocal_low && candidate <= vocal_high &&
          isScaleTone(candidate % 12, static_cast<uint8_t>(key_offset))) {
        step_candidates.push_back(candidate);
      }
    }
    if (!step_candidates.empty()) break;  // Whole steps found, don't mix in half steps
  }
  if (!step_candidates.empty()) {
    return rng_util::selectRandom(rng, step_candidates);
  }

  // Fallback: small chord-tone move (minor/major 3rd at most)
  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);
  std::vector<int> move_candidates;
  for (int pc : chord_tones) {
    for (int oct = 4; oct <= 6; ++oct) {
      int candidate = oct * 12 + pc;
      int interval = std::abs(candidate - prev_pitch);
      if (candidate >= vocal_low && candidate <= vocal_high && interval > 0 &&
          interval <= MAX_FALLBACK_MOVE) {
        move_candidates.push_back(candidate);
      }
    }
  }

  if (move_candidates.empty()) {
    return new_pitch;
  }

  return rng_util::selectRandom(rng, move_candidates);
}

namespace {

/// Minimum interval (semitones) that counts as a leap for chain tracking.
constexpr int kChainLeapMin = 3;
/// Maximum allowed consecutive same-direction leaps.
constexpr int kMaxLeapChain = 2;

}  // namespace

int enforceLeapChainLimit(int new_pitch, int prev_pitch, int chain_len, int chain_dir,
                          int key_offset, uint8_t vocal_low, uint8_t vocal_high) {
  int interval = new_pitch - prev_pitch;
  int sign = (interval > 0) - (interval < 0);
  if (std::abs(interval) < kChainLeapMin || sign == 0 || sign != chain_dir ||
      chain_len < kMaxLeapChain) {
    return new_pitch;  // Does not extend a forbidden chain
  }

  // Replace the third same-direction leap with a scale step. Same direction
  // first (preserves the intended contour while breaking the arpeggio
  // outline), then contrary direction.
  for (int dir : {sign, -sign}) {
    for (int step : {2, 1}) {
      int candidate = prev_pitch + dir * step;
      if (candidate >= vocal_low && candidate <= vocal_high &&
          isScaleTone(candidate % 12, static_cast<uint8_t>(key_offset))) {
        return candidate;
      }
    }
  }

  // Last resort: repeat the previous pitch (also breaks the chain)
  return prev_pitch;
}

void updateLeapChainState(int interval, int& chain_len, int& chain_dir) {
  int sign = (interval > 0) - (interval < 0);
  if (std::abs(interval) >= kChainLeapMin && sign != 0) {
    if (sign == chain_dir) {
      ++chain_len;
    } else {
      chain_dir = sign;
      chain_len = 1;
    }
  } else {
    chain_len = 0;
    chain_dir = 0;
  }
}

}  // namespace melody
}  // namespace midisketch
