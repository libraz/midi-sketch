/**
 * @file types.cpp
 * @brief Section transition and type helper implementations.
 */

#include "core/types.h"

namespace midisketch {

namespace {

// Main section transition patterns
constexpr SectionTransition kTransitions[] = {
    // B→Chorus: Build anticipation with ascending tendency and leading tone
    // pitch_tendency=2 creates "run-up" expectation for chorus entry
    {SectionType::B, SectionType::Chorus, 2, 1.20f, 4, true},

    // A→B: Gentle rise
    {SectionType::A, SectionType::B, 1, 1.05f, 2, false},

    // Chorus→A: Settling down (transitioning to verse 2)
    {SectionType::Chorus, SectionType::A, -2, 0.90f, 2, false},

    // Intro→A: Natural introduction
    {SectionType::Intro, SectionType::A, 0, 1.0f, 2, true},

    // Bridge→Chorus: Dramatic buildup
    {SectionType::Bridge, SectionType::Chorus, 4, 1.20f, 4, true},
};

constexpr size_t kTransitionCount = sizeof(kTransitions) / sizeof(kTransitions[0]);

}  // namespace

const SectionTransition* getTransition(SectionType from, SectionType to) {
  for (size_t i = 0; i < kTransitionCount; ++i) {
    if (kTransitions[i].from == from && kTransitions[i].to == to) {
      return &kTransitions[i];
    }
  }
  return nullptr;
}

}  // namespace midisketch
