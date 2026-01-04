#ifndef MIDISKETCH_CORE_STRUCTURE_H
#define MIDISKETCH_CORE_STRUCTURE_H

#include "core/types.h"
#include <vector>

namespace midisketch {

// Builds a list of sections based on the given structure pattern.
// @param pattern Structure pattern to use
// @returns Vector of Section structs representing the song structure
std::vector<Section> buildStructure(StructurePattern pattern);

// Builds a list of sections to match a target duration.
// @param target_seconds Target duration in seconds
// @param bpm Tempo in beats per minute
// @returns Vector of Section structs matching the target duration
std::vector<Section> buildStructureForDuration(uint16_t target_seconds, uint16_t bpm);

// Calculates the total duration in ticks for the given sections.
// @param sections Vector of sections
// @returns Total ticks from start to end of last section
Tick calculateTotalTicks(const std::vector<Section>& sections);

// Calculates the total number of bars in the given sections.
// @param sections Vector of sections
// @returns Sum of all section bar counts
uint16_t calculateTotalBars(const std::vector<Section>& sections);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_STRUCTURE_H
