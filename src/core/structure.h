/**
 * @file structure.h
 * @brief Song structure patterns and section builders.
 */

#ifndef MIDISKETCH_CORE_STRUCTURE_H
#define MIDISKETCH_CORE_STRUCTURE_H

#include <string>
#include <utility>
#include <vector>

#include "core/production_blueprint.h"
#include "core/types.h"

namespace midisketch {

/// @name Structure length bounds
///
/// The only definition of how long a generated song may be. buildStructureForDuration()
/// clamps to this range. validateSongConfig() rejects only a duration that no permitted
/// tempo could reach; one that merely overshoots the resolved tempo is clamped, and the
/// length actually built is reported back so the adjustment is visible.
/// @{

/// Shortest structure the builder will produce.
inline constexpr uint16_t kMinStructureBars = 12;

/// Longest structure the builder will produce (~4.8 min at 120 BPM).
inline constexpr uint16_t kMaxStructureBars = 144;

/// Slowest tempo a song may be built at. Also the tempo that reaches the longest
/// duration, since a bar lasts longer the slower the song runs.
inline constexpr uint16_t kMinSongBpm = 40;

/// Fastest tempo a song may be built at, and so the one that reaches the shortest
/// duration.
inline constexpr uint16_t kMaxSongBpm = 240;

/// @}

/**
 * @brief Bars a target duration works out to at a given tempo (4/4).
 * @param target_seconds Target duration in seconds
 * @param bpm Tempo in beats per minute; 0 yields 0 bars
 * @return Unclamped bar count
 */
uint16_t barsForDuration(uint16_t target_seconds, uint16_t bpm);

/**
 * @brief Whether a requested duration lands inside the structure length bounds.
 * @param target_seconds Target duration in seconds; 0 means "use the form pattern"
 * @param bpm Tempo in beats per minute
 * @return True when the duration is 0 or maps into [kMinStructureBars, kMaxStructureBars]
 */
bool isTargetDurationAchievable(uint16_t target_seconds, uint16_t bpm);

/**
 * @brief Shortest and longest duration in seconds that can be honoured at a tempo.
 * @param bpm Tempo in beats per minute
 * @return Pair of (minimum seconds, maximum seconds); (0, 0) when bpm is 0
 */
std::pair<uint16_t, uint16_t> achievableDurationRange(uint16_t bpm);

/**
 * @brief Build sections from structure pattern.
 * @param pattern Structure pattern to use
 * @return Vector of Section structs
 */
std::vector<Section> buildStructure(StructurePattern pattern);

/// @brief Everything that decides a song's section list.
struct StructureRequest {
  StructurePattern pattern = StructurePattern::FullPop;  ///< Form used when no flow applies
  bool form_explicit = false;            ///< The caller chose the form; it replaces the flow
  uint16_t target_duration_seconds = 0;  ///< 0 = the form's or flow's own length
  uint16_t bpm = 0;                      ///< Resolved tempo
  bool call_enabled = false;             ///< Insert the chant and MIX sections
  IntroChant intro_chant = IntroChant::None;
  MixPattern mix_pattern = MixPattern::None;
  bool addictive_mode = false;  ///< B sections cut off before the chorus
  EnergyCurve energy_curve = EnergyCurve::GradualBuild;
  const ProductionBlueprint* blueprint = nullptr;  ///< Flow and slot properties; may be null
};

/**
 * @brief Build a song's section list; every edit to the list is made here, once.
 *
 * Order: the base list (the blueprint flow, or the form when a duration or an
 * explicit form asks for one), sized to the target with the call sections'
 * bars reserved, the call sections inserted, the blueprint's slots overlaid
 * onto a form-built list, exit patterns and the climax marked, then the
 * addictive exits and the energy curve. Marking comes after every edit, so
 * exactly one chorus - the last - carries the climax.
 *
 * @param request What the song asks for
 * @param warnings Receives a note when the built length misses the target
 * @return Sections laid out from tick 0
 */
std::vector<Section> planSongStructure(const StructureRequest& request,
                                       std::vector<std::string>& warnings);

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
 *
 * A MixBreak already leading into the last Chorus is reused rather than
 * doubled. Exit patterns are left to the caller, which marks them once the
 * list is final.
 *
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

/**
 * @brief Apply addictive mode exit patterns to sections.
 *
 * In addictive mode (Behavioral Loop), B sections followed by Chorus
 * use CutOff exit pattern instead of Sustain to create a dramatic
 * "silence before the drop" effect.
 *
 * @param sections Sections to modify (in-place)
 * @param addictive_mode Whether addictive mode is enabled
 */
void applyAddictiveModeExitPatterns(std::vector<Section>& sections, bool addictive_mode);

/**
 * @brief Apply energy curve to sections based on song position.
 *
 * Adjusts section energy levels based on the selected curve:
 * - GradualBuild: Starts low, builds to peak at last chorus (default)
 * - FrontLoaded: High energy throughout, peak chorus, slight dip in bridge
 * - WavePattern: Alternates between low and high energy; a pre-chorus climbs
 *   between them and a chorus is never lowered
 * - SteadyState: Maintains consistent medium energy
 *
 * FrontLoaded and WavePattern leave ochisabi and climactic sections at the
 * energy their modifier declares.
 *
 * @param sections Sections to modify (energy levels may be adjusted)
 * @param curve Energy curve type to apply
 */
void applyEnergyCurve(std::vector<Section>& sections, EnergyCurve curve);

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

/**
 * @brief Apply blueprint section properties to duration-based structure.
 *
 * When target_duration > 0, buildStructureForDuration() creates sections from
 * StructurePattern, ignoring blueprint SectionSlot definitions. This function
 * overlays blueprint properties (track_mask, drum_role, energy, etc.) onto
 * those auto-generated sections by matching section types.
 *
 * Match strategy, per section type:
 * 1. The first occurrence takes the first slot, the last takes the last slot
 *    (a lone occurrence takes the last unless that slot is position-dependent)
 * 2. Middle occurrences take the slots between, aligned to the end so the slot
 *    before the terminal one stays next to it
 * 3. Surplus middle occurrences repeat a regular slot; a position-dependent
 *    slot (an ochisabi, a drop chorus) is used at most once
 * 4. Type not in blueprint: skip overlay (keep defaults)
 *
 * Exit patterns are reset to the slots' values and left for the caller to mark.
 *
 * @param sections Sections to modify (in-place)
 * @param blueprint The production blueprint with section flow
 */
void applyBlueprintOverlay(std::vector<Section>& sections, const ProductionBlueprint& blueprint);

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
 * A track the section's mask enables but no entry names (other than the vocal)
 * joins with the last entry, so a default schedule never silences it for the
 * whole section.
 *
 * @param section The section to generate events for
 * @param section_index Index of this section in the song
 * @param total_sections Total number of sections in the song
 * @return Vector of LayerEvent sorted by bar_offset
 */
std::vector<LayerEvent> generateDefaultLayerEvents(const Section& section, size_t section_index,
                                                   size_t total_sections);

/**
 * @brief Apply default layer events to all qualifying sections.
 *
 * Iterates over all sections and assigns layer_events based on section type.
 * Only affects sections with 4+ bars and no existing layer_events. A first verse
 * or an interlude whose blueprint set its track mask keeps that mask as is.
 *
 * @param sections Sections to modify (in-place)
 */
void applyDefaultLayerSchedule(std::vector<Section>& sections);

/**
 * @brief Finish a freshly planned structure the way every generation entry point does.
 *
 * RhythmSync raises section density and velocity with each repeat of a section
 * type; every paradigm then gets the default layer schedule.
 *
 * @param sections Sections to modify (in-place)
 * @param paradigm Generation paradigm of the song
 */
void applyDefaultSectionPlan(std::vector<Section>& sections, GenerationParadigm paradigm);

/// @}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_STRUCTURE_H
