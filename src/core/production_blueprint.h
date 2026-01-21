/**
 * @file production_blueprint.h
 * @brief Production blueprint types for declarative song generation control.
 *
 * ProductionBlueprint controls "how to generate" independently from
 * existing presets (StylePreset, Mood, VocalStyle) which control "what to generate".
 */

#ifndef MIDISKETCH_CORE_PRODUCTION_BLUEPRINT_H
#define MIDISKETCH_CORE_PRODUCTION_BLUEPRINT_H

#include "core/section_types.h"

#include <cstdint>
#include <random>

namespace midisketch {

// TrackMask, EntryPattern, GenerationParadigm, and RiffPolicy are defined in section_types.h

/// @brief Section slot definition for blueprint section flow.
struct SectionSlot {
  SectionType type;            ///< Section type (Intro, A, B, Chorus, etc.)
  uint8_t bars;                ///< Number of bars
  TrackMask enabled_tracks;    ///< Which tracks are active
  EntryPattern entry_pattern;  ///< How instruments enter

  // Time-based control fields
  SectionEnergy energy;        ///< Section energy level (Low/Medium/High/Peak)
  uint8_t base_velocity;       ///< Base velocity (60-100)
  uint8_t density_percent;     ///< Density percentage (50-100)
  PeakLevel peak_level;        ///< Peak level (replaces fill_before bool)
  DrumRole drum_role;          ///< Drum role (Full/Ambient/Minimal/FXOnly)
};

/// @brief Production blueprint defining how a song is generated.
///
/// This is independent from StylePreset/Mood/VocalStyle and controls:
/// - Generation paradigm (rhythm-sync vs melody-driven)
/// - Section flow with track enable/disable per section
/// - Riff management policy
/// - Drum-vocal synchronization
/// - Intro arrangement
struct ProductionBlueprint {
  const char* name;       ///< Blueprint name (e.g., "Traditional", "Orangestar")
  uint8_t weight;         ///< Random selection weight (0 = disabled)

  GenerationParadigm paradigm;  ///< Generation approach

  const SectionSlot* section_flow;  ///< Section flow array (nullptr = use StructurePattern)
  uint8_t section_count;            ///< Number of sections in flow

  RiffPolicy riff_policy;      ///< How riffs are managed across sections

  bool drums_sync_vocal;       ///< Sync drum kicks/snares to vocal onsets
  bool drums_required;         ///< Drums are required for this blueprint to work properly

  bool intro_kick_enabled;     ///< Enable kick in intro
  bool intro_bass_enabled;     ///< Enable bass in intro
};

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Get a production blueprint by ID.
 * @param id Blueprint ID (0 = Traditional, 1 = Orangestar, etc.)
 * @return Reference to the blueprint
 */
const ProductionBlueprint& getProductionBlueprint(uint8_t id);

/**
 * @brief Get the number of available blueprints.
 * @return Blueprint count
 */
uint8_t getProductionBlueprintCount();

/**
 * @brief Select a blueprint based on weights or explicit ID.
 * @param rng Random number generator
 * @param explicit_id If < 255, use this ID directly; otherwise random selection
 * @return Selected blueprint ID
 */
uint8_t selectProductionBlueprint(std::mt19937& rng, uint8_t explicit_id = 255);

/**
 * @brief Get blueprint name by ID.
 * @param id Blueprint ID
 * @return Blueprint name string
 */
const char* getProductionBlueprintName(uint8_t id);

/**
 * @brief Find blueprint ID by name (case-insensitive).
 * @param name Blueprint name
 * @return Blueprint ID, or 255 if not found
 */
uint8_t findProductionBlueprintByName(const char* name);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_PRODUCTION_BLUEPRINT_H
