/**
 * @file fingering.h
 * @brief Hand position, fingering, and barre chord types for fretted instruments.
 *
 * Defines types for modeling hand positions, finger spans, barre chords,
 * and complete fingering solutions for fretted instruments.
 */

#ifndef MIDISKETCH_TRACK_FRETTED_FINGERING_H
#define MIDISKETCH_TRACK_FRETTED_FINGERING_H

#include <vector>

#include "track/fretted/fretted_types.h"

namespace midisketch {

/// @brief Hand position on the fretboard.
struct HandPosition {
  uint8_t base_fret;   ///< Position of the index finger (1st position = fret 1)
  uint8_t span_low;    ///< Lowest reachable fret from this position
  uint8_t span_high;   ///< Highest reachable fret from this position

  /// @brief Default constructor (1st position).
  HandPosition() : base_fret(1), span_low(0), span_high(4) {}

  /// @brief Construct with base fret and span.
  HandPosition(uint8_t base, uint8_t low, uint8_t high)
      : base_fret(base), span_low(low), span_high(high) {}

  /// @brief Check if a fret is reachable from this position.
  bool canReach(uint8_t fret) const {
    return (fret == 0) || (fret >= span_low && fret <= span_high);
  }

  /// @brief Calculate distance to move to reach a target fret.
  /// @return 0 if reachable, positive value = frets to shift up, negative = down
  int8_t distanceToReach(uint8_t fret) const {
    if (fret == 0) return 0;  // Open string always reachable
    if (fret < span_low) return static_cast<int8_t>(fret) - static_cast<int8_t>(span_low);
    if (fret > span_high) return static_cast<int8_t>(fret) - static_cast<int8_t>(span_high);
    return 0;
  }
};

/// @brief Hand span constraints based on skill level.
struct HandSpanConstraints {
  uint8_t normal_span;             ///< Comfortable fret span (e.g., 3 for beginner)
  uint8_t max_span;                ///< Maximum achievable span (e.g., 5 for advanced)
  uint8_t stretch_penalty_per_fret;  ///< Cost penalty per fret beyond normal span

  /// @brief Default (intermediate player).
  static HandSpanConstraints intermediate() { return {4, 5, 10}; }

  /// @brief Beginner constraints.
  static HandSpanConstraints beginner() { return {3, 4, 15}; }

  /// @brief Advanced player constraints.
  static HandSpanConstraints advanced() { return {5, 7, 5}; }

  /// @brief Virtuoso constraints (minimal penalty).
  static HandSpanConstraints virtuoso() { return {6, 8, 2}; }

  /// @brief Calculate stretch penalty for a given span.
  float calculateStretchPenalty(uint8_t actual_span) const {
    if (actual_span <= normal_span) return 0.0f;
    if (actual_span > max_span) return 999.0f;  // Impossible
    return static_cast<float>((actual_span - normal_span) * stretch_penalty_per_fret);
  }
};

/// @brief Barre (barré) chord state.
///
/// Physical constraints:
/// - Index finger covers all strings at the barre fret
/// - Open strings become unavailable (covered by barre)
/// - Remaining 3 fingers (middle, ring, pinky) can press frets above the barre
struct BarreState {
  uint8_t fret;           ///< Barre fret position (0 = no barre)
  uint8_t lowest_string;  ///< Lowest string covered by barre (0 = all from lowest)
  uint8_t highest_string; ///< Highest string covered by barre

  /// @brief Default constructor (no barre).
  BarreState() : fret(0), lowest_string(0), highest_string(0) {}

  /// @brief Construct with fret and string range.
  BarreState(uint8_t f, uint8_t low, uint8_t high)
      : fret(f), lowest_string(low), highest_string(high) {}

  /// @brief Check if barre is active.
  bool isActive() const { return fret > 0; }

  /// @brief Check if a string is covered by the barre.
  bool coversString(uint8_t string) const {
    return isActive() && string >= lowest_string && string <= highest_string;
  }

  /// @brief Get the number of strings covered by the barre.
  uint8_t getStringCount() const {
    return isActive() ? (highest_string - lowest_string + 1) : 0;
  }
};

/// @brief Finger allocation during a barre chord.
///
/// Physical constraints:
/// - Index finger: barre (covers all strings)
/// - Middle finger: barre+1 fret, one string only
/// - Ring finger: barre+2 frets, one string only
/// - Pinky finger: barre+3 frets, one string only
/// - Each finger can only press one additional position beyond the barre
struct BarreFingerAllocation {
  uint8_t barre_fret;          ///< Barre position
  int8_t middle_finger_string; ///< String pressed by middle finger (-1 = unused)
  int8_t ring_finger_string;   ///< String pressed by ring finger (-1 = unused)
  int8_t pinky_finger_string;  ///< String pressed by pinky finger (-1 = unused)

  static constexpr int8_t kUnused = -1;

  /// @brief Default constructor.
  BarreFingerAllocation()
      : barre_fret(0),
        middle_finger_string(kUnused),
        ring_finger_string(kUnused),
        pinky_finger_string(kUnused) {}

  /// @brief Construct with barre fret.
  explicit BarreFingerAllocation(uint8_t barre)
      : barre_fret(barre),
        middle_finger_string(kUnused),
        ring_finger_string(kUnused),
        pinky_finger_string(kUnused) {}

  /// @brief Get the number of active fingers beyond the barre.
  uint8_t activeFingerCount() const {
    uint8_t count = 0;
    if (middle_finger_string != kUnused) ++count;
    if (ring_finger_string != kUnused) ++count;
    if (pinky_finger_string != kUnused) ++count;
    return count;
  }

  /// @brief Check if a specific fret/string combination can be pressed.
  bool canPress(uint8_t fret, uint8_t string) const {
    if (fret == barre_fret) return true;  // Covered by barre

    // Each finger can only press one string at their designated offset
    if (fret == barre_fret + 1) {
      return middle_finger_string == kUnused ||
             middle_finger_string == static_cast<int8_t>(string);
    }
    if (fret == barre_fret + 2) {
      return ring_finger_string == kUnused ||
             ring_finger_string == static_cast<int8_t>(string);
    }
    if (fret == barre_fret + 3) {
      return pinky_finger_string == kUnused ||
             pinky_finger_string == static_cast<int8_t>(string);
    }

    return false;  // Beyond reach during barre
  }

  /// @brief Try to allocate a finger for a position.
  /// @return true if allocation succeeded, false if finger already used or out of range
  bool tryAllocate(uint8_t fret, uint8_t string) {
    if (fret == barre_fret) return true;  // Already covered by barre

    if (fret == barre_fret + 1) {
      if (middle_finger_string == kUnused) {
        middle_finger_string = static_cast<int8_t>(string);
        return true;
      }
      return middle_finger_string == static_cast<int8_t>(string);
    }
    if (fret == barre_fret + 2) {
      if (ring_finger_string == kUnused) {
        ring_finger_string = static_cast<int8_t>(string);
        return true;
      }
      return ring_finger_string == static_cast<int8_t>(string);
    }
    if (fret == barre_fret + 3) {
      if (pinky_finger_string == kUnused) {
        pinky_finger_string = static_cast<int8_t>(string);
        return true;
      }
      return pinky_finger_string == static_cast<int8_t>(string);
    }

    return false;
  }

  /// @brief Reset all finger allocations (keep barre fret).
  void reset() {
    middle_finger_string = kUnused;
    ring_finger_string = kUnused;
    pinky_finger_string = kUnused;
  }
};

/// @brief Complete fingering solution for a note or chord.
struct Fingering {
  std::vector<FingerAssignment> assignments;  ///< Per-note finger assignments
  HandPosition hand_pos;                      ///< Hand position for this fingering
  BarreState barre;                           ///< Barre state (if applicable)
  float playability_cost;                     ///< Total cost (lower = easier)
  bool requires_position_shift;               ///< True if hand must move from previous
  bool requires_barre_change;                 ///< True if barre must be formed/released

  /// @brief Default constructor.
  Fingering()
      : playability_cost(0.0f),
        requires_position_shift(false),
        requires_barre_change(false) {}

  /// @brief Check if this is a valid fingering (has at least one assignment).
  bool isValid() const { return !assignments.empty(); }

  /// @brief Get the lowest fret used in this fingering.
  uint8_t getLowestFret() const {
    uint8_t lowest = kMaxFrets;
    for (const auto& a : assignments) {
      if (a.position.fret < lowest && a.position.fret > 0) {
        lowest = a.position.fret;
      }
    }
    return lowest == kMaxFrets ? 0 : lowest;
  }

  /// @brief Get the highest fret used in this fingering.
  uint8_t getHighestFret() const {
    uint8_t highest = 0;
    for (const auto& a : assignments) {
      if (a.position.fret > highest) {
        highest = a.position.fret;
      }
    }
    return highest;
  }

  /// @brief Get the fret span (highest - lowest).
  uint8_t getSpan() const {
    uint8_t low = getLowestFret();
    uint8_t high = getHighestFret();
    if (low == 0 && high == 0) return 0;
    if (low == 0) return high;  // Open string to fret
    return high - low;
  }
};

/// @brief Check if a position is playable given a barre state and hand position.
///
/// @param pos Target position
/// @param barre Current barre state
/// @param hand Current hand position
/// @return true if the position can be played
inline bool canPlayAtPosition(const FretPosition& pos, const BarreState& barre,
                               const HandPosition& hand) {
  if (barre.isActive()) {
    // During barre: cannot play below barre fret (except if not covered string)
    if (barre.coversString(pos.string)) {
      if (pos.fret < barre.fret) {
        return false;  // Cannot play below barre on covered strings
      }
      if (pos.fret == barre.fret) {
        return true;  // Barre covers this
      }
      // Above barre: check if within reach (+1 to +3 frets from barre)
      return pos.fret <= barre.fret + 3;
    }
    // String not covered by barre: use normal hand position check
  }

  // No barre or uncovered string: check hand position
  if (pos.fret == 0) return true;  // Open string always playable
  return hand.canReach(pos.fret);
}

/// @brief Check if a chord is playable with a barre at the specified fret.
///
/// @param positions All positions to be played
/// @param barre_fret Proposed barre fret
/// @return true if all positions can be played with this barre
inline bool isChordPlayableWithBarre(const std::vector<FretPosition>& positions,
                                      uint8_t barre_fret) {
  BarreFingerAllocation alloc(barre_fret);

  for (const auto& pos : positions) {
    // Below barre fret: impossible
    if (pos.fret < barre_fret) return false;

    // Beyond reach of pinky (barre + 3): impossible
    if (pos.fret > barre_fret + 3) return false;

    // Try to allocate finger
    if (!alloc.tryAllocate(pos.fret, pos.string)) {
      return false;  // Finger already used for different string
    }
  }

  return true;
}

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_FRETTED_FINGERING_H
