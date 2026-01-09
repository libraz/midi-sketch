/**
 * @file section_utils.h
 * @brief Utilities for searching sections in arrangements.
 */

#ifndef MIDISKETCH_CORE_SECTION_UTILS_H_
#define MIDISKETCH_CORE_SECTION_UTILS_H_

#include "core/types.h"
#include <optional>
#include <vector>

namespace midisketch {

// Utility functions for searching sections in an arrangement.
// These functions help reduce duplicated section search logic.

// Finds the first section of the given type.
// @param sections Vector of sections to search
// @param type Section type to find
// @returns Optional containing the section if found
std::optional<Section> findFirstSection(
    const std::vector<Section>& sections, SectionType type);

// Finds the last section of the given type.
// @param sections Vector of sections to search
// @param type Section type to find
// @returns Optional containing the section if found
std::optional<Section> findLastSection(
    const std::vector<Section>& sections, SectionType type);

// Finds the Nth section of the given type (1-indexed).
// @param sections Vector of sections to search
// @param type Section type to find
// @param n Which occurrence to find (1 = first, 2 = second, etc.)
// @returns Optional containing the section if found
std::optional<Section> findNthSection(
    const std::vector<Section>& sections, SectionType type, size_t n);

// Finds all sections of the given type.
// @param sections Vector of sections to search
// @param type Section type to find
// @returns Vector of matching sections
std::vector<Section> findAllSections(
    const std::vector<Section>& sections, SectionType type);

// Finds all start ticks of sections of the given type.
// @param sections Vector of sections to search
// @param type Section type to find
// @returns Vector of start ticks
std::vector<Tick> findAllSectionTicks(
    const std::vector<Section>& sections, SectionType type);

// Finds a section of the given type that follows any of the specified preceding types.
// @param sections Vector of sections to search
// @param type Section type to find
// @param preceding_types Types that should precede the target section
// @returns Optional containing the section if found
std::optional<Section> findSectionAfter(
    const std::vector<Section>& sections,
    SectionType type,
    const std::vector<SectionType>& preceding_types);

// Finds the last section of the given type that follows any of the specified preceding types.
// @param sections Vector of sections to search
// @param type Section type to find
// @param preceding_types Types that should precede the target section
// @returns Optional containing the section if found
std::optional<Section> findLastSectionAfter(
    const std::vector<Section>& sections,
    SectionType type,
    const std::vector<SectionType>& preceding_types);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_SECTION_UTILS_H_
