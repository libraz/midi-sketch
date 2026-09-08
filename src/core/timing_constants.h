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
constexpr Tick TICK_64TH = TICKS_PER_BEAT / 16;      // 30 ticks (1/16 beat)

// Triplet values
constexpr Tick TICK_QUARTER_TRIPLET = TICKS_PER_BEAT / 3;     // 160 ticks
constexpr Tick TICK_EIGHTH_TRIPLET = TICKS_PER_BEAT / 6;      // 80 ticks
constexpr Tick TICK_SIXTEENTH_TRIPLET = TICKS_PER_BEAT / 12;  // 40 ticks

/// @brief Offset between one string of a strum and the next.
///
/// A strum is one chord, and once its notes are written this spacing is the
/// only thing that still says so: they carry no other mark of belonging
/// together, so a later pass sees several notes a few ticks apart rather than
/// one gesture. Anything that moves a pitch has to be able to ask, because
/// moving a single voice of a chord that was fingered as one shape breaks both
/// the shape and the order the pick travels in.
constexpr Tick kStringRakeTicks = 8;

// Tempo conversion constant
// 1 minute = 60,000,000 microseconds
// microseconds_per_beat = kMicrosecondsPerMinute / BPM
constexpr uint32_t kMicrosecondsPerMinute = 60000000;

/**
 * @brief Convert MIDI ticks to seconds at a given BPM.
 * @param ticks Number of MIDI ticks
 * @param bpm Beats per minute
 * @return Duration in seconds
 */
inline double ticksToSeconds(Tick ticks, double bpm) {
  return static_cast<double>(ticks) / TICKS_PER_BEAT / bpm * 60.0;
}

/// Convert ticks to seconds accounting for tempo changes.
inline double ticksToSecondsWithTempoMap(Tick ticks, double base_bpm,
                                         const std::vector<TempoEvent>& tempo_map) {
  if (tempo_map.empty()) return ticksToSeconds(ticks, base_bpm);

  double seconds = 0.0;
  Tick prev_tick = 0;
  double current_bpm = base_bpm;

  for (const auto& evt : tempo_map) {
    if (evt.tick >= ticks) break;
    seconds += static_cast<double>(evt.tick - prev_tick) / TICKS_PER_BEAT / current_bpm * 60.0;
    prev_tick = evt.tick;
    current_bpm = evt.bpm;
  }

  seconds += static_cast<double>(ticks - prev_tick) / TICKS_PER_BEAT / current_bpm * 60.0;
  return seconds;
}

/// @brief Check if current bar is in the phrase tail region.
/// Only meaningful when section.phrase_tail_rest == true.
/// @param bar_index 0-based bar index within section
/// @param section_bars Total bars in section
/// @return true if bar is in the tail region
inline bool isPhraseTail(uint8_t bar_index, uint8_t section_bars) {
  if (section_bars >= 4) return bar_index >= section_bars - 2;
  if (section_bars == 3) return bar_index >= section_bars - 1;
  return false;
}

/// @brief Check if this is the very last bar of the section.
inline bool isLastBar(uint8_t bar_index, uint8_t section_bars) {
  return bar_index == section_bars - 1;
}

/// @brief Offset within a bar from which a track stops starting notes, when the
///        section asks its phrases to breathe before the next one begins.
///
/// A phrase tail is a subtraction: the arrangement takes notes out so the end of
/// the phrase is heard as an end. This states *where* that happens -- the last
/// bar of a section that asked for it, and only there. *How much* is silenced
/// differs by instrument and is the caller's constant, so a track free to write
/// the whole bar gets TICKS_PER_BAR back and the result can be used as a cutoff
/// without first asking whether the section has a tail at all.
///
/// The last-bar test is not implied by the tail test: a two-bar section has a
/// last bar and no tail region.
///
/// @param phrase_tail_rest The section's phrase_tail_rest flag
/// @param bar_index 0-based bar index within the section
/// @param section_bars Total bars in section
/// @param offset_in_tail Offset this track stops at when the bar is a tail
/// @return Offset from the bar's start; TICKS_PER_BAR when the bar is not a tail
inline Tick phraseTailSilenceOffset(bool phrase_tail_rest, uint8_t bar_index, uint8_t section_bars,
                                    Tick offset_in_tail) {
  if (!phrase_tail_rest) return TICKS_PER_BAR;
  if (!isPhraseTail(bar_index, section_bars)) return TICKS_PER_BAR;
  if (!isLastBar(bar_index, section_bars)) return TICKS_PER_BAR;
  return offset_in_tail;
}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TIMING_CONSTANTS_H_
