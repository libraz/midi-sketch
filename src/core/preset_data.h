#ifndef MIDISKETCH_CORE_PRESET_DATA_H
#define MIDISKETCH_CORE_PRESET_DATA_H

#include "core/types.h"
#include <cstdint>

namespace midisketch {

// Number of available structure patterns
constexpr uint8_t STRUCTURE_COUNT = 5;

// Number of available mood presets
constexpr uint8_t MOOD_COUNT = 16;

// Number of available chord progressions
constexpr uint8_t CHORD_COUNT = 16;

// Returns the default BPM for a given mood.
// @param mood Mood preset
// @returns Default BPM (60-180)
uint16_t getMoodDefaultBpm(Mood mood);

// Returns the note density parameter for a mood.
// @param mood Mood preset
// @returns Density value (0.0 - 1.0)
float getMoodDensity(Mood mood);

// Returns the name of a structure pattern.
// @param pattern Structure pattern
// @returns Pattern name (e.g., "StandardPop")
const char* getStructureName(StructurePattern pattern);

// Returns the name of a mood preset.
// @param mood Mood preset
// @returns Mood name (e.g., "straight_pop")
const char* getMoodName(Mood mood);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_PRESET_DATA_H
