/**
 * @file hook_rhythm_patterns.h
 * @brief Pop hook rhythm patterns for chorus and hook generation.
 */

#ifndef MIDISKETCH_TRACK_MELODY_HOOK_RHYTHM_PATTERNS_H
#define MIDISKETCH_TRACK_MELODY_HOOK_RHYTHM_PATTERNS_H

#include <random>

#include "core/melody_templates.h"
#include "core/timing_constants.h"
#include "core/types.h"

namespace midisketch {
namespace melody {

/// @brief Hook rhythm pattern definition.
struct HookRhythmPattern {
  uint8_t durations[6];  ///< Note durations in eighths (0 = end marker)
  uint8_t note_count;    ///< Number of notes in pattern
  Tick gap_after;        ///< Gap after pattern (in ticks)
  const char* name;      ///< Pattern name for debugging
};

/// @brief Get the array of hook rhythm patterns.
/// @return Pointer to pattern array
const HookRhythmPattern* getHookRhythmPatterns();

/// @brief Get number of hook rhythm patterns.
/// @return Pattern count
size_t getHookRhythmPatternCount();

/// @brief Select a hook rhythm pattern index.
/// @param tmpl Melody template
/// @param rng Random number generator
/// @return Selected pattern index
size_t selectHookRhythmPatternIndex(const MelodyTemplate& tmpl, std::mt19937& rng);

}  // namespace melody
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_HOOK_RHYTHM_PATTERNS_H
