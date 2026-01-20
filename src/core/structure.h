/**
 * @file structure.h
 * @brief Song structure patterns and section builders.
 */

#ifndef MIDISKETCH_CORE_STRUCTURE_H
#define MIDISKETCH_CORE_STRUCTURE_H

#include "core/production_blueprint.h"
#include "core/types.h"
#include <vector>

namespace midisketch {

/**
 * @brief Build sections from structure pattern.
 * @param pattern Structure pattern to use
 * @return Vector of Section structs
 */
std::vector<Section> buildStructure(StructurePattern pattern);

/**
 * @brief Build sections to match target duration.
 * @param target_seconds Target duration in seconds
 * @param bpm Tempo in beats per minute
 * @param pattern Base pattern (default: FullPop)
 * @return Vector of Section structs
 */
std::vector<Section> buildStructureForDuration(
    uint16_t target_seconds,
    uint16_t bpm,
    StructurePattern pattern = StructurePattern::FullPop);

/**
 * @brief Calculate total ticks for sections.
 * @param sections Vector of sections
 * @return Total ticks
 */
Tick calculateTotalTicks(const std::vector<Section>& sections);

/**
 * @brief Calculate total bars for sections.
 * @param sections Vector of sections
 * @return Sum of all bar counts
 */
uint16_t calculateTotalBars(const std::vector<Section>& sections);

/// @name Call System Structure Functions
/// @{

/**
 * @brief Build sections with call support.
 * @param target_seconds Target duration in seconds
 * @param bpm Tempo in beats per minute
 * @param call_enabled Whether call is enabled
 * @param intro_chant IntroChant pattern
 * @param mix_pattern MixPattern
 * @param pattern Base pattern (default: FullPop)
 * @return Vector of Section structs with call sections
 */
std::vector<Section> buildStructureForDuration(
    uint16_t target_seconds,
    uint16_t bpm,
    bool call_enabled,
    IntroChant intro_chant,
    MixPattern mix_pattern,
    StructurePattern pattern = StructurePattern::FullPop);

/**
 * @brief Insert call sections into existing structure (in-place).
 * @param sections Sections to modify
 * @param intro_chant IntroChant pattern (after Intro)
 * @param mix_pattern MixPattern (before last Chorus)
 * @param bpm BPM for bar count calculation
 */
void insertCallSections(
    std::vector<Section>& sections,
    IntroChant intro_chant,
    MixPattern mix_pattern,
    uint16_t bpm);

/**
 * @brief Recalculate start_tick for all sections.
 * @param sections Sections to update
 */
void recalculateSectionTicks(std::vector<Section>& sections);

/// @}

/// @name ProductionBlueprint Structure Functions
/// @{

/**
 * @brief Build sections from a ProductionBlueprint's section flow.
 *
 * Converts SectionSlot array to Section array with proper timing
 * and track mask to density conversions.
 *
 * @param blueprint The production blueprint
 * @return Vector of Section structs
 */
std::vector<Section> buildStructureFromBlueprint(const ProductionBlueprint& blueprint);

/**
 * @brief Convert TrackMask to VocalDensity.
 *
 * - If Vocal is disabled -> VocalDensity::None
 * - If only Vocal+Drums -> VocalDensity::Sparse
 * - Otherwise -> VocalDensity::Full
 *
 * @param mask Track mask
 * @return Corresponding VocalDensity
 */
VocalDensity trackMaskToVocalDensity(TrackMask mask);

/**
 * @brief Convert TrackMask to BackingDensity.
 *
 * Based on number of enabled backing tracks (Chord, Bass, Motif, Arpeggio, Aux).
 * - 0-1 tracks -> BackingDensity::Thin
 * - 2-3 tracks -> BackingDensity::Normal
 * - 4+ tracks  -> BackingDensity::Thick
 *
 * @param mask Track mask
 * @return Corresponding BackingDensity
 */
BackingDensity trackMaskToBackingDensity(TrackMask mask);

/// @}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_STRUCTURE_H
