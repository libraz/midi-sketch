/**
 * @file chord_extension_planner.h
 * @brief Shared chord extension selection for harmonic timeline planning.
 */

#ifndef MIDISKETCH_CORE_CHORD_EXTENSION_PLANNER_H
#define MIDISKETCH_CORE_CHORD_EXTENSION_PLANNER_H

#include <random>

#include "core/preset_types.h"
#include "core/section_types.h"

namespace midisketch {

ChordExtension selectChordExtension(int8_t degree, SectionType section, int bar_in_section,
                                    int section_bars, const ChordExtensionParams& ext_params,
                                    std::mt19937& rng);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_CHORD_EXTENSION_PLANNER_H
