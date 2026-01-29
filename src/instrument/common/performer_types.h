/**
 * @file performer_types.h
 * @brief Common types for physical performer models.
 *
 * Defines base types shared by all performer implementations
 * (fretted instruments, vocals, drums, etc.)
 */

#ifndef MIDISKETCH_INSTRUMENT_COMMON_PERFORMER_TYPES_H
#define MIDISKETCH_INSTRUMENT_COMMON_PERFORMER_TYPES_H

#include <cstdint>

#include "core/basic_types.h"

namespace midisketch {

/// @brief Performer type identifier.
enum class PerformerType : uint8_t {
  FrettedInstrument,  ///< Bass, guitar
  Voice,              ///< Vocal
  Drums,              ///< Drums
  Keys                ///< Keyboard (future)
};

/// @brief Base state for all performer types.
///
/// Tracks common state shared by all performers. Derived classes
/// extend this with instrument-specific state.
struct PerformerState {
  Tick current_tick = 0;   ///< Current time position
  float fatigue = 0.0f;    ///< Accumulated fatigue (0.0-1.0)
  uint8_t last_pitch = 0;  ///< Last performed pitch

  virtual ~PerformerState() = default;

  /// @brief Reset state to initial values.
  virtual void reset() {
    current_tick = 0;
    fatigue = 0.0f;
    last_pitch = 0;
  }
};

}  // namespace midisketch

#endif  // MIDISKETCH_INSTRUMENT_COMMON_PERFORMER_TYPES_H
