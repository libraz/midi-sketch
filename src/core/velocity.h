/**
 * @file velocity.h
 * @brief Velocity (dynamics) calculation for musical expression.
 */

#ifndef MIDISKETCH_CORE_VELOCITY_H
#define MIDISKETCH_CORE_VELOCITY_H

#include <vector>

#include "core/types.h"

namespace midisketch {

// Forward declaration
class MidiTrack;

/**
 * @brief Calculate velocity for a note based on musical context.
 * @param section Current section type (affects base energy)
 * @param beat Beat position within bar (0-3, affects emphasis)
 * @param mood Current mood preset (affects intensity)
 * @return Calculated velocity (0-127)
 */
uint8_t calculateVelocity(SectionType section, uint8_t beat, Mood mood);

/**
 * @brief Get velocity adjustment multiplier for a mood.
 * @param mood Mood preset
 * @return Multiplier (typically 0.9-1.1)
 */
float getMoodVelocityAdjustment(Mood mood);

/**
 * @brief Get energy level for a section type.
 *
 * Intro/Outro=1, Verse=2, Pre-chorus/Bridge=3, Chorus=4.
 *
 * @param section Section type
 * @return Energy level (1=lowest, 4=highest)
 */
int getSectionEnergy(SectionType section);

/**
 * @brief Get velocity multiplier for a section type.
 *
 * Centralized section-based velocity scaling to ensure consistent
 * dynamics across all tracks. Values range from 0.6 (very quiet)
 * to 1.1 (energetic).
 *
 * @param section Section type
 * @return Velocity multiplier (0.6-1.1)
 */
float getSectionVelocityMultiplier(SectionType section);

// ============================================================================
// SectionEnergy and PeakLevel Functions
// ============================================================================

/**
 * @brief Get energy level for a section type (alias with clearer naming).
 *
 * Same as getSectionEnergy(), but with clearer naming to distinguish
 * from the new SectionEnergy enum.
 *
 * @param section Section type
 * @return Energy level (1=lowest, 4=highest)
 */
int getSectionEnergyLevel(SectionType section);

/**
 * @brief Get effective section energy from Section struct.
 *
 * Prioritizes Blueprint's explicit energy setting. Falls back to
 * estimating from SectionType if energy is Medium (default).
 *
 * @param section Section struct with energy field
 * @return SectionEnergy value
 */
SectionEnergy getEffectiveSectionEnergy(const Section& section);

/**
 * @brief Get velocity multiplier for PeakLevel.
 *
 * @param peak PeakLevel value
 * @return Velocity multiplier (1.0 for None, up to 1.1 for Max)
 */
float getPeakVelocityMultiplier(PeakLevel peak);

/**
 * @brief Calculate effective velocity for a section.
 *
 * Combines base_velocity, energy, and peak_level into final velocity.
 * This function integrates all velocity control parameters.
 *
 * @param section Section struct
 * @param beat Beat position (0-3)
 * @param mood Mood preset
 * @return Calculated velocity (0-127)
 */
uint8_t calculateEffectiveVelocity(const Section& section, uint8_t beat, Mood mood);

// Forward declaration
struct SectionEmotion;

/**
 * @brief Calculate effective velocity with EmotionCurve integration.
 *
 * Combines section properties, beat position, mood, and emotion curve
 * parameters (tension affects ceiling, energy affects base level).
 *
 * @param section Section struct
 * @param beat Beat position (0-3)
 * @param mood Mood preset
 * @param emotion EmotionCurve parameters for this section (optional)
 * @return Calculated velocity (0-127)
 */
uint8_t calculateEmotionAwareVelocity(const Section& section, uint8_t beat, Mood mood,
                                       const SectionEmotion* emotion);

/// @brief Track-relative velocity multipliers for consistent mix balance.
struct VelocityBalance {
  static constexpr float VOCAL = 1.0f;      ///< Lead vocal - always on top
  static constexpr float CHORD = 0.75f;     ///< Chords - supportive harmony
  static constexpr float BASS = 0.85f;      ///< Bass - prominent foundation
  static constexpr float DRUMS = 0.90f;     ///< Drums - rhythmic backbone
  static constexpr float MOTIF = 0.70f;     ///< Motif - decorative element
  static constexpr float ARPEGGIO = 0.85f;  ///< Arpeggio - rhythmic texture
  static constexpr float AUX = 0.65f;       ///< Aux - subdued support

  /**
   * @brief Get velocity multiplier for a track role.
   * @param role Track role to look up
   * @return Velocity multiplier for mix balance
   */
  static float getMultiplier(TrackRole role);
};

/**
 * @brief Get velocity multiplier for bar position within a section.
 *
 * Implements 4-bar phrase dynamics (build→hit pattern) and section-level
 * crescendo for Chorus sections. This adds subtle dynamics that prevent
 * sections from sounding flat.
 *
 * The 4-bar phrase pattern:
 * - Bar 0: 0.75 (setup)
 * - Bar 1: 0.83 (build)
 * - Bar 2: 0.92 (anticipation)
 * - Bar 3: 1.00 (hit)
 *
 * @param bar_in_section Bar number within the section (0-indexed)
 * @param total_bars Total number of bars in the section
 * @param section_type Type of section (affects curve shape)
 * @return Velocity multiplier (typically 0.75-1.12)
 */
float getBarVelocityMultiplier(int bar_in_section, int total_bars, SectionType section_type);

/// @brief Named velocity ratio constants for consistent dynamics.
namespace VelocityRatio {
constexpr float ACCENT = 0.95f;      ///< Accented notes (emphasized)
constexpr float NORMAL = 0.9f;       ///< Standard velocity
constexpr float WEAK_BEAT = 0.85f;   ///< Off-beat or weak beat notes
constexpr float SOFT = 0.8f;         ///< Softer notes (intro/outro)
constexpr float TENSION = 0.7f;      ///< Tension notes, doublings
constexpr float BACKGROUND = 0.65f;  ///< Background elements
constexpr float VERY_SOFT = 0.6f;    ///< Very subdued notes
constexpr float GHOST = 0.5f;        ///< Ghost notes (nearly silent)
}  // namespace VelocityRatio

/**
 * @brief Apply crescendo/decrescendo dynamics at section transitions.
 * @param track Track to modify (in-place)
 * @param section_start Start tick of the current section
 * @param section_end End tick of the current section
 * @param from Section type being exited
 * @param to Section type being entered
 */
void applyTransitionDynamics(MidiTrack& track, Tick section_start, Tick section_end,
                             SectionType from, SectionType to);

/**
 * @brief Apply transition dynamics to all melodic tracks.
 * @param tracks Vector of tracks to modify (in-place)
 * @param sections Arrangement sections for transition points
 */
void applyAllTransitionDynamics(std::vector<MidiTrack*>& tracks,
                                const std::vector<Section>& sections);

/**
 * @brief Apply entry pattern dynamics to notes at section start.
 *
 * Implements GradualBuild and DropIn entry effects.
 * - GradualBuild: Start at 60% velocity, ramp to 100% over 2 bars
 * - DropIn: Slight velocity boost at section start
 * - Immediate/Stagger: No velocity adjustment
 *
 * @param track Track to modify (in-place)
 * @param section_start Start tick of the section
 * @param bars Number of bars in the section
 * @param pattern Entry pattern type
 */
void applyEntryPatternDynamics(MidiTrack& track, Tick section_start, uint8_t bars,
                               EntryPattern pattern);

/**
 * @brief Apply entry pattern dynamics to all tracks for all sections.
 *
 * Processes each section's entry_pattern setting and applies
 * appropriate velocity modifications to tracks.
 *
 * @param tracks Vector of tracks to modify (in-place)
 * @param sections Arrangement sections with entry_pattern settings
 */
void applyAllEntryPatternDynamics(std::vector<MidiTrack*>& tracks,
                                  const std::vector<Section>& sections);

/**
 * @brief Apply bar-level velocity curves to a track within a section.
 *
 * Applies the 4-bar phrase dynamics (build→hit pattern) and section-level
 * crescendo for Chorus sections. This adds subtle dynamics that prevent
 * sections from sounding flat.
 *
 * @param track Track to modify (in-place)
 * @param section Section containing the notes to modify
 */
void applyBarVelocityCurve(MidiTrack& track, const Section& section);

/**
 * @brief Apply bar-level velocity curves to all tracks for all sections.
 *
 * Processes each section and applies bar-level velocity curves to create
 * natural phrase dynamics within each section.
 *
 * @param tracks Vector of tracks to modify (in-place)
 * @param sections Arrangement sections
 */
void applyAllBarVelocityCurves(std::vector<MidiTrack*>& tracks,
                               const std::vector<Section>& sections);

// ============================================================================
// Melody Contour Velocity
// ============================================================================

/**
 * @brief Apply melody-contour-following velocity to vocal notes.
 *
 * Boosts velocity for phrase-high notes and creates gradual velocity changes
 * for ascending/descending passages. This makes the melody feel more naturally
 * performed by following the melodic contour with dynamics.
 *
 * Rules:
 * - Phrase-local highest note: +15 velocity boost
 * - Ascending passages: gradual velocity increase
 * - Descending passages: gradual velocity decrease
 * - Phrase boundary resets contour tracking (every 4 bars)
 *
 * @param track Vocal track to modify (in-place)
 * @param sections Song sections for phrase boundary detection
 */
void applyMelodyContourVelocity(MidiTrack& track, const std::vector<Section>& sections);

// ============================================================================
// Musical Accent Patterns
// ============================================================================

/**
 * @brief Apply musical accent patterns to a track.
 *
 * Three accent types are applied:
 * - Phrase-head accent: +8 velocity on first note of each 2-bar phrase
 * - Contour accent: +10 on the highest note within each 2-bar phrase
 * - Agogic accent: +5 on notes longer than a quarter note (held notes naturally
 *   receive more emphasis from performers)
 *
 * @param track Track to modify (in-place)
 * @param sections Song sections for phrase boundary detection
 */
void applyAccentPatterns(MidiTrack& track, const std::vector<Section>& sections);

// ============================================================================
// EmotionCurve-based Velocity Calculations
// ============================================================================

// Forward declaration
struct SectionEmotion;

/**
 * @brief Calculate velocity ceiling based on EmotionCurve tension.
 *
 * Higher tension allows higher velocity ceiling, while low tension
 * limits the maximum velocity to maintain dynamic range.
 *
 * @param base_velocity Base velocity value (0-127)
 * @param tension Tension level from EmotionCurve (0.0-1.0)
 * @return Adjusted velocity ceiling (0-127)
 */
uint8_t calculateVelocityCeiling(uint8_t base_velocity, float tension);

/**
 * @brief Calculate base velocity adjusted by EmotionCurve energy.
 *
 * Energy level affects the starting point for velocity calculations.
 * Higher energy means louder base velocity.
 *
 * @param section_velocity Section-based velocity
 * @param energy Energy level from EmotionCurve (0.0-1.0)
 * @return Adjusted base velocity (0-127)
 */
uint8_t calculateEnergyAdjustedVelocity(uint8_t section_velocity, float energy);

/**
 * @brief Get note density multiplier based on EmotionCurve energy.
 *
 * Energy affects how many notes are generated. Higher energy means
 * denser patterns; lower energy means sparser, more spacious arrangements.
 *
 * @param base_density Base density value (0.0-1.0)
 * @param energy Energy level from EmotionCurve (0.0-1.0)
 * @return Adjusted density multiplier (0.5-1.5)
 */
float calculateEnergyDensityMultiplier(float base_density, float energy);

/**
 * @brief Get chord tone preference based on EmotionCurve resolution_need.
 *
 * Higher resolution_need means melody should favor chord tones over
 * non-chord tones for more stable, resolved sound.
 *
 * @param resolution_need Resolution need from EmotionCurve (0.0-1.0)
 * @return Chord tone probability boost (0.0-0.3)
 */
float getChordTonePreferenceBoost(float resolution_need);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_VELOCITY_H
