/**
 * @file arpeggio_types.h
 * @brief Arpeggio-related type definitions.
 */

#ifndef MIDISKETCH_CORE_ARPEGGIO_TYPES_H
#define MIDISKETCH_CORE_ARPEGGIO_TYPES_H

#include <cstdint>

namespace midisketch {

/// @brief Arpeggio pattern direction.
enum class ArpeggioPattern : uint8_t {
  Up,          ///< Ascending notes
  Down,        ///< Descending notes
  UpDown,      ///< Ascending then descending
  Random,      ///< Random order
  Pinwheel,    ///< 1-5-3-5 center alternating expansion
  PedalRoot,   ///< 1-3-1-5-1-7 root repetition
  Alberti,     ///< 1-5-3-5 classical broken chord
  BrokenChord  ///< 1-3-5-8-5-3 ascending then descending
};

/// @brief Arpeggio note speed.
enum class ArpeggioSpeed : uint8_t {
  Eighth,     ///< 8th notes
  Sixteenth,  ///< 16th notes (default, YOASOBI-style)
  Triplet     ///< Triplet feel
};

// Note: ArpeggioParams and ArpeggioStyle are defined in preset_types.h
// to avoid circular dependencies.

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_ARPEGGIO_TYPES_H
