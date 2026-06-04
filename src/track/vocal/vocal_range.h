/**
 * @file vocal_range.h
 * @brief Vocal range calculation considering constraints and motif collision.
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
class MidiTrack;

// Motif Collision Avoidance Constants
constexpr uint8_t kMotifHighRegisterThreshold = 72;  // C5 - motif considered "high" if above this
constexpr uint8_t kMotifLowRegisterThreshold = 60;   // C4 - motif considered "low" if below this
constexpr uint8_t kVocalAvoidHighLimit = 72;         // Limit vocal high when motif is high
constexpr uint8_t kVocalAvoidLowLimit = 65;          // Limit vocal low when motif is low
constexpr uint8_t kMinVocalOctaveRange = 12;         // Minimum 1 octave range required
constexpr uint8_t kVocalRangeFloor = 48;             // C3 - absolute minimum for vocal
constexpr uint8_t kVocalRangeCeiling = 96;           // C7 - absolute maximum for vocal

/// @brief Result of vocal range calculation.
struct VocalRangeResult {
  uint8_t effective_low;   ///< Effective lower bound of vocal range
  uint8_t effective_high;  ///< Effective upper bound of vocal range
  float velocity_scale;    ///< Velocity scaling factor for composition style
};

/// @brief Calculate effective vocal range considering constraints.
/// @param params Generation parameters
/// @param song Song with modulation info
/// @param motif_track Optional motif track for range separation
/// @return Calculated vocal range
VocalRangeResult calculateEffectiveVocalRange(const GeneratorParams& params, const Song& song,
                                              const MidiTrack* motif_track);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_VOCAL_RANGE_H
