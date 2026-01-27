/**
 * @file structure.h
 * @brief Song structure patterns and section builders.
 */

#ifndef MIDISKETCH_CORE_STRUCTURE_H
#define MIDISKETCH_CORE_STRUCTURE_H

#include <vector>

#include "core/production_blueprint.h"
#include "core/types.h"

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
    uint16_t target_seconds, uint16_t bpm, StructurePattern pattern = StructurePattern::FullPop);

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
    uint16_t target_seconds, uint16_t bpm, bool call_enabled, IntroChant intro_chant,
    MixPattern mix_pattern, StructurePattern pattern = StructurePattern::FullPop);

/**
 * @brief Insert call sections into existing structure (in-place).
 * @param sections Sections to modify
 * @param intro_chant IntroChant pattern (after Intro)
 * @param mix_pattern MixPattern (before last Chorus)
 * @param bpm BPM for bar count calculation
 */
void insertCallSections(std::vector<Section>& sections, IntroChant intro_chant,
                        MixPattern mix_pattern, uint16_t bpm);

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

/// @name Layer Scheduling Functions
/// @{

/**
 * @brief Generate default layer events for a section based on its type and bar count.
 *
 * Only generates events for sections with 4+ bars. Short sections (1-3 bars)
 * are left without layer events (all tracks active throughout).
 *
 * Section type patterns:
 * - Intro: Staggered entry (Drums -> Bass -> Chord -> All)
 * - Verse (A, first): Vocal+minimal -> add layers at bar 2
 * - Pre-chorus (B): Full tracks immediately
 * - Chorus (first): All tracks immediately
 * - Outro: Remove tracks in last 2 bars
 *
 * @param section The section to generate events for
 * @param section_index Index of this section in the song
 * @param total_sections Total number of sections in the song
 * @return Vector of LayerEvent sorted by bar_offset
 */
std::vector<LayerEvent> generateDefaultLayerEvents(const Section& section,
                                                   size_t section_index,
                                                   size_t total_sections);

/**
 * @brief Apply default layer events to all qualifying sections.
 *
 * Iterates over all sections and assigns layer_events based on section type.
 * Only affects sections with 4+ bars and no existing layer_events.
 *
 * @param sections Sections to modify (in-place)
 */
void applyDefaultLayerSchedule(std::vector<Section>& sections);

/// @}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_STRUCTURE_H
