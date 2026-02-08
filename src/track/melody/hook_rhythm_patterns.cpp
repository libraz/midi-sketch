/**
 * @file hook_rhythm_patterns.cpp
 * @brief Implementation of pop hook rhythm patterns.
 */

#include "track/melody/hook_rhythm_patterns.h"

#include <vector>

#include "core/rng_util.h"

namespace midisketch {
namespace melody {

// Common pop hook rhythm patterns
static constexpr HookRhythmPattern kHookRhythmPatterns[] = {
    // Pattern 1: "Ta-Ta-Taa" (8-8-4) - Classic buildup
    {{1, 1, 2, 0, 0, 0}, 3, TICK_EIGHTH, "buildup"},

    // Pattern 2: "Taa-Ta-Ta" (4-8-8) - Syncopated start
    {{2, 1, 1, 0, 0, 0}, 3, TICK_EIGHTH, "syncopated"},

    // Pattern 3: "Ta-Ta-Ta-Taa" (8-8-8-4) - Four-note energy
    {{1, 1, 1, 2, 0, 0}, 4, TICK_EIGHTH, "four-note"},

    // Pattern 4: "Taa-Taa" (4-4) - Simple and powerful
    {{2, 2, 0, 0, 0, 0}, 2, TICK_QUARTER, "powerful"},

    // Pattern 5: "Ta-Taa-Ta" (8-4-8) - Dotted rhythm feel
    {{1, 2, 1, 0, 0, 0}, 3, TICK_EIGHTH, "dotted"},

    // Pattern 6: "Taa-Ta-Ta-Ta" (4-8-8-8) - Call-and-response
    {{2, 1, 1, 1, 0, 0}, 4, TICK_SIXTEENTH, "call-response"},

    // Pattern 7: "Ta-Ta-Ta-Taa-Ta" (8-8-8-4-8) - Syncopated burst
    {{1, 1, 1, 2, 1, 0}, 5, TICK_SIXTEENTH, "synco-burst"},

    // Pattern 8: "Ta-Ta-Taa-Ta" (8-8-4-8) - Staccato with sustain
    {{1, 1, 2, 1, 0, 0}, 4, TICK_EIGHTH, "staccato"},

    // Pattern 9: "Taa-Ta-Taa" (4-8-4) - Anticipation pattern
    {{2, 1, 2, 0, 0, 0}, 3, TICK_EIGHTH, "anticipation"},

    // Pattern 10: "Ta-Ta-Ta-Ta-Taa" (8-8-8-8-4) - J-pop "drill" style
    {{1, 1, 1, 1, 2, 0}, 5, TICK_SIXTEENTH, "drill"},

    // Pattern 11: 2-mora pattern (4-8)
    {{2, 1, 0, 0, 0, 0}, 2, TICK_EIGHTH, "mora-2"},

    // Pattern 12: 3-mora pattern (8-8-4)
    {{1, 1, 2, 0, 0, 0}, 3, TICK_EIGHTH, "mora-3"},

    // Pattern 13: 3-mora start emphasis (4-8-8)
    {{2, 1, 1, 0, 0, 0}, 3, TICK_EIGHTH, "mora-3-start"},

    // Pattern 14: 4-mora pattern (8-8-8-4)
    {{1, 1, 1, 2, 0, 0}, 4, TICK_EIGHTH, "mora-4"},
};

static constexpr size_t kHookRhythmPatternCount =
    sizeof(kHookRhythmPatterns) / sizeof(kHookRhythmPatterns[0]);

const HookRhythmPattern* getHookRhythmPatterns() {
  return kHookRhythmPatterns;
}

size_t getHookRhythmPatternCount() {
  return kHookRhythmPatternCount;
}

size_t selectHookRhythmPatternIndex(const MelodyTemplate& tmpl, std::mt19937& rng) {
  std::vector<size_t> candidates;

  if (tmpl.rhythm_driven) {
    candidates = {0, 2, 5, 6, 7, 8, 9};
  } else if (tmpl.long_note_ratio > 0.3f) {
    candidates = {3, 1, 4, 10, 12};
  } else if (tmpl.sixteenth_density > 0.3f) {
    candidates = {2, 5, 6, 7, 9, 13};
  } else {
    for (size_t i = 0; i < kHookRhythmPatternCount; ++i) {
      candidates.push_back(i);
    }
  }

  return rng_util::selectRandom(rng, candidates);
}

}  // namespace melody
}  // namespace midisketch
