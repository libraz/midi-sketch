/**
 * @file kick_patterns.h
 * @brief Kick drum pattern generation.
 */

#ifndef MIDISKETCH_TRACK_DRUMS_KICK_PATTERNS_H
#define MIDISKETCH_TRACK_DRUMS_KICK_PATTERNS_H

#include <random>
#include <vector>

#include "core/preset_data.h"
#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {
namespace drums {

/// @brief Section-specific kick pattern flags.
struct KickPattern {
  bool beat1;      ///< Beat 1
  bool beat1_and;  ///< Beat 1&
  bool beat2;      ///< Beat 2
  bool beat2_and;  ///< Beat 2&
  bool beat3;      ///< Beat 3
  bool beat3_and;  ///< Beat 3&
  bool beat4;      ///< Beat 4
  bool beat4_and;  ///< Beat 4&
};

/// @brief Number of bars a pre-chorus lift covers.
constexpr uint8_t kPreChorusLiftBars = 2;

/// @brief Check if this bar is in the pre-chorus lift zone.
/// @param section Current section
/// @param bar Bar within section (0-based)
/// @param sections All sections in song
/// @param sec_idx Index of current section
/// @return true if in pre-chorus lift zone
bool isInPreChorusLift(const Section& section, uint8_t bar, const std::vector<Section>& sections,
                       size_t sec_idx);

/// @brief Snare hits the pre-chorus buildup writes in one bar of the lift.
///
/// The buildup is a crescendo in rhythm as well as in volume: the earlier lift
/// bars mark quarters and the last one doubles to eighths, so the section
/// accelerates into the chorus instead of holding one flat pulse.
///
/// @param bar_in_lift Bar index within the lift (0-based)
/// @return Number of snare hits written in that bar
uint8_t preChorusBuildupHitsPerBar(uint8_t bar_in_lift);

/// @brief Convert Euclidean bitmask (16-step) to KickPattern.
/// @param pattern Euclidean rhythm pattern
/// @return KickPattern struct
KickPattern euclideanToKickPattern(uint16_t pattern);

/// @brief Get kick pattern based on section type and style.
/// @param section Section type
/// @param style Drum style
/// @param bar Bar number (for variation)
/// @param rng Random number generator
/// @return KickPattern for the section
KickPattern getKickPattern(SectionType section, DrumStyle style, int bar, std::mt19937& rng);

}  // namespace drums
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_KICK_PATTERNS_H
