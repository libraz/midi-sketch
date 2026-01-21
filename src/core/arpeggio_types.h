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
  Up,      ///< Ascending notes
  Down,    ///< Descending notes
  UpDown,  ///< Ascending then descending
  Random   ///< Random order
};

/// @brief Arpeggio note speed.
enum class ArpeggioSpeed : uint8_t {
  Eighth,     ///< 8th notes
  Sixteenth,  ///< 16th notes (default, YOASOBI-style)
  Triplet     ///< Triplet feel
};

/// @brief Arpeggio track configuration.
struct ArpeggioParams {
  ArpeggioPattern pattern = ArpeggioPattern::Up;
  ArpeggioSpeed speed = ArpeggioSpeed::Sixteenth;
  uint8_t octave_range = 2;    ///< 1-3 octaves
  float gate = 0.8f;           ///< Gate length (0.0-1.0)
  bool sync_chord = true;      ///< Sync with chord changes
  uint8_t base_velocity = 90;  ///< Base velocity for arpeggio notes
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_ARPEGGIO_TYPES_H
