/**
 * @file vocal_range.h
 * @brief Vocal range calculation considering blueprint and modulation headroom.
 *
 * Extracted from vocal.cpp to allow reuse and testing.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_VOCAL_RANGE_H
#define MIDISKETCH_TRACK_VOCAL_VOCAL_RANGE_H

#include <cstdint>

namespace midisketch {

// Forward declarations
struct GeneratorParams;
class Song;

/// @brief Result of vocal range calculation.
struct VocalRangeResult {
  uint8_t effective_low;   ///< Effective lower bound of vocal range
  uint8_t effective_high;  ///< Effective upper bound of vocal range
  float velocity_scale;    ///< Velocity scaling factor for composition style
};

/// @brief Calculate effective vocal range considering constraints.
///
/// The range is decided by the singer and the arrangement alone: the blueprint's
/// pitch ceiling, and the headroom a later modulation will need. It does not
/// consider the other tracks -- a part that wants to stay out of the vocal's way
/// moves itself, because the vocal is written first.
///
/// @param params Generation parameters
/// @param song Song with modulation info
/// @return Calculated vocal range
VocalRangeResult calculateEffectiveVocalRange(const GeneratorParams& params, const Song& song);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_VOCAL_RANGE_H
