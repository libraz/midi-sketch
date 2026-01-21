/**
 * @file timing_constants.h
 * @brief Common timing constants for MIDI note durations.
 */

#ifndef MIDISKETCH_CORE_TIMING_CONSTANTS_H_
#define MIDISKETCH_CORE_TIMING_CONSTANTS_H_

#include "core/types.h"

namespace midisketch {

// Timing constants based on TICKS_PER_BEAT (480) and TICKS_PER_BAR (1920).
// Use these constants instead of local definitions to ensure consistency.

constexpr Tick TICK_WHOLE = TICKS_PER_BAR;           // 1920 ticks (4 beats)
constexpr Tick TICK_HALF = TICKS_PER_BAR / 2;        // 960 ticks (2 beats)
constexpr Tick TICK_QUARTER = TICKS_PER_BEAT;        // 480 ticks (1 beat)
constexpr Tick TICK_EIGHTH = TICKS_PER_BEAT / 2;     // 240 ticks (1/2 beat)
constexpr Tick TICK_SIXTEENTH = TICKS_PER_BEAT / 4;  // 120 ticks (1/4 beat)
constexpr Tick TICK_32ND = TICKS_PER_BEAT / 8;       // 60 ticks (1/8 beat)

// Triplet values
constexpr Tick TICK_QUARTER_TRIPLET = TICKS_PER_BEAT / 3;     // 160 ticks
constexpr Tick TICK_EIGHTH_TRIPLET = TICKS_PER_BEAT / 6;      // 80 ticks
constexpr Tick TICK_SIXTEENTH_TRIPLET = TICKS_PER_BEAT / 12;  // 40 ticks

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TIMING_CONSTANTS_H_
