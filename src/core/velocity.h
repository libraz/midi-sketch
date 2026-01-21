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

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_VELOCITY_H
