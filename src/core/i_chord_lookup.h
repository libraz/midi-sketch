/**
 * @file i_chord_lookup.h
 * @brief Shared interface for chord degree lookup at any tick.
 *
 * Used by both generation (ChordProgressionTracker, HarmonyContext) and
 * analysis (dissonance.cpp) to ensure consistent chord identification.
 */

#ifndef MIDISKETCH_CORE_I_CHORD_LOOKUP_H
#define MIDISKETCH_CORE_I_CHORD_LOOKUP_H

#include <vector>

#include "core/basic_types.h"

namespace midisketch {

/**
 * @brief Interface for chord degree lookup at any tick position.
 *
 * Extracted so that both generation and analysis share the same
 * tick-accurate chord lookup logic, avoiding bar-level rounding
 * mismatches when dense harmonic rhythm splits a bar.
 */
class IChordLookup {
 public:
  virtual ~IChordLookup() = default;

  /**
   * @brief Get chord degree at a specific tick.
   * @param tick Position in ticks
   * @return Scale degree (0=I, 1=ii, 2=iii, 3=IV, 4=V, 5=vi, 6=vii)
   */
  virtual int8_t getChordDegreeAt(Tick tick) const = 0;

  /**
   * @brief Get chord tones as pitch classes at a specific tick.
   * @param tick Position in ticks
   * @return Vector of pitch classes (0-11) that are chord tones
   */
  virtual std::vector<int> getChordTonesAt(Tick tick) const = 0;

  /**
   * @brief Get the tick of the next chord change after the given tick.
   * @param after Position to search from
   * @return Tick of next chord change, or 0 if none found
   */
  virtual Tick getNextChordChangeTick(Tick after) const = 0;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_I_CHORD_LOOKUP_H
