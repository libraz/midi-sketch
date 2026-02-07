/**
 * @file pitch_monotony_tracker.h
 * @brief Shared tracker for consecutive same-pitch avoidance and leap guard.
 *
 * Used by Aux and Motif generators to prevent monotonous runs of the same note
 * and optionally constrain large melodic leaps.
 */

#ifndef MIDISKETCH_CORE_PITCH_MONOTONY_TRACKER_H
#define MIDISKETCH_CORE_PITCH_MONOTONY_TRACKER_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/chord_utils.h"

namespace midisketch {

/// Maximum consecutive same pitches before forcing variation
inline constexpr int kDefaultMaxConsecutiveSamePitch = 3;

/// Default maximum leap in semitones before constraining (1 octave)
inline constexpr int kDefaultMaxLeapSemitones = 12;

/**
 * @brief Track consecutive same pitches and optionally large leaps, suggest variations.
 *
 * Prevents:
 * 1. Monotonous runs of the same note (e.g., 16 consecutive G3)
 * 2. Large melodic leaps (> max_leap_semitones) when leap guard is enabled
 *
 * Fallback chain for monotony resolution:
 * 1. Chord tones (different pitch class, within leap constraint if enabled)
 * 2. Step ±2 semitones
 * 3. Octave shift ±12
 */
struct PitchMonotonyTracker {
  uint8_t last_pitch = 0;
  int consecutive_count = 0;

  /// Maximum consecutive same pitches before suggesting alternative
  int max_consecutive = kDefaultMaxConsecutiveSamePitch;

  /// Maximum leap in semitones (0 = no leap guard)
  int max_leap = 0;

  /**
   * @brief Construct tracker with optional leap guard.
   * @param enable_leap_guard If true, constrain leaps to max_leap_semitones
   * @param max_leap_semitones Maximum leap size (only used if enable_leap_guard is true)
   */
  explicit PitchMonotonyTracker(bool enable_leap_guard = false,
                                int max_leap_semitones = kDefaultMaxLeapSemitones)
      : max_leap(enable_leap_guard ? max_leap_semitones : 0) {}

  /**
   * @brief Record a pitch and return suggested pitch (may differ if issues detected).
   * @param desired Original desired pitch
   * @param range_low Lower bound of pitch range
   * @param range_high Upper bound of pitch range
   * @param chord_degree Current chord degree (-1 to skip chord-tone logic)
   * @return Suggested pitch (may be different if monotony or large leap detected)
   */
  uint8_t trackAndSuggest(uint8_t desired, uint8_t range_low, uint8_t range_high,
                          int8_t chord_degree) {
    uint8_t result = desired;

    // Step 1: Apply leap guard if enabled
    if (max_leap > 0 && last_pitch > 0) {
      result = applyLeapGuard(result, range_low, range_high, chord_degree);
    }

    // Step 2: Track consecutive count
    if (result == last_pitch) {
      consecutive_count++;
    } else {
      consecutive_count = 1;
    }

    // Step 3: Resolve monotony if threshold exceeded
    if (consecutive_count > max_consecutive) {
      uint8_t alternative = resolveMonotony(result, range_low, range_high, chord_degree);
      if (alternative != result) {
        last_pitch = alternative;
        consecutive_count = 1;
        return alternative;
      }
    }

    last_pitch = result;
    return result;
  }

  /// Reset tracker state (e.g., at section boundary)
  void reset() {
    last_pitch = 0;
    consecutive_count = 0;
  }

 private:
  /**
   * @brief Constrain pitch to be within max_leap of last_pitch.
   */
  uint8_t applyLeapGuard(uint8_t desired, uint8_t range_low, uint8_t range_high,
                         int8_t chord_degree) {
    int leap = std::abs(static_cast<int>(desired) - static_cast<int>(last_pitch));
    if (leap <= max_leap) return desired;

    // Try chord tones in nearby octaves within leap constraint
    if (chord_degree >= 0) {
      ChordToneHelper helper(chord_degree);
      auto chord_tones = helper.allInRange(range_low, range_high);

      int best_pitch = -1;
      int best_distance = 1000;

      for (uint8_t candidate : chord_tones) {
        int dist_from_last = std::abs(static_cast<int>(candidate) - static_cast<int>(last_pitch));
        if (dist_from_last > max_leap) continue;

        int dist_from_desired = std::abs(static_cast<int>(candidate) - static_cast<int>(desired));
        if (dist_from_desired < best_distance) {
          best_distance = dist_from_desired;
          best_pitch = candidate;
        }
      }

      if (best_pitch >= 0) {
        return static_cast<uint8_t>(best_pitch);
      }
    }

    // Fallback: clamp the leap
    if (desired > last_pitch) {
      return static_cast<uint8_t>(
          std::min(static_cast<int>(last_pitch) + max_leap, static_cast<int>(range_high)));
    }
    return static_cast<uint8_t>(
        std::max(static_cast<int>(last_pitch) - max_leap, static_cast<int>(range_low)));
  }

  /**
   * @brief Find alternative pitch to break monotony.
   */
  uint8_t resolveMonotony(uint8_t current, uint8_t range_low, uint8_t range_high,
                          int8_t chord_degree) {
    // Try chord tones first
    if (chord_degree >= 0) {
      ChordToneHelper helper(chord_degree);
      auto chord_tones = helper.allInRange(range_low, range_high);

      // Find chord tones with different pitch class
      std::vector<uint8_t> close_alternatives;
      std::vector<uint8_t> all_alternatives;

      for (uint8_t candidate : chord_tones) {
        if ((candidate % 12) == (current % 12)) continue;  // Skip same pitch class

        bool within_leap = (max_leap == 0) ||
            std::abs(static_cast<int>(candidate) - static_cast<int>(last_pitch)) <= max_leap;
        if (!within_leap) continue;

        all_alternatives.push_back(candidate);
        int dist = std::abs(static_cast<int>(candidate) - static_cast<int>(current));
        if (dist <= 12) {
          close_alternatives.push_back(candidate);
        }
      }

      // Prefer close alternatives to avoid large leaps
      const auto& candidates = close_alternatives.empty() ? all_alternatives : close_alternatives;

      if (!candidates.empty()) {
        // Select closest alternative
        uint8_t best = candidates[0];
        int best_dist = std::abs(static_cast<int>(best) - static_cast<int>(current));
        for (uint8_t alt : candidates) {
          int dist = std::abs(static_cast<int>(alt) - static_cast<int>(current));
          if (dist < best_dist) {
            best_dist = dist;
            best = alt;
          }
        }
        return best;
      }
    }

    // Fallback: try step up (+2 semitones for whole step)
    if (current + 2 <= range_high && current + 2 != last_pitch) {
      if (isWithinLeap(current + 2)) return current + 2;
    }
    // Try step down
    if (current >= range_low + 2 && current - 2 != last_pitch) {
      if (isWithinLeap(current - 2)) return static_cast<uint8_t>(current - 2);
    }
    // Try octave shift up
    if (current + 12 <= range_high) {
      if (isWithinLeap(current + 12)) return current + 12;
    }
    // Try octave shift down
    if (current >= range_low + 12) {
      if (isWithinLeap(current - 12)) return static_cast<uint8_t>(current - 12);
    }

    return current;  // No alternative found
  }

  /// Check if a candidate pitch is within the leap constraint
  bool isWithinLeap(uint8_t candidate) const {
    if (max_leap == 0) return true;
    return std::abs(static_cast<int>(candidate) - static_cast<int>(last_pitch)) <= max_leap;
  }
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_PITCH_MONOTONY_TRACKER_H
