#ifndef MIDISKETCH_CORE_VELOCITY_H
#define MIDISKETCH_CORE_VELOCITY_H

#include "core/types.h"
#include <vector>

namespace midisketch {

// Forward declaration
class MidiTrack;

// Calculates the velocity for a note based on musical context.
// @param section Current section type
// @param beat Beat position within bar (0-3)
// @param mood Current mood preset
// @returns Calculated velocity (0-127)
uint8_t calculateVelocity(SectionType section, uint8_t beat, Mood mood);

// Returns the velocity adjustment multiplier for a mood.
// @param mood Mood preset
// @returns Multiplier (typically 0.9 - 1.1)
float getMoodVelocityAdjustment(Mood mood);

// Returns the energy level for a section type.
// Used for transition dynamics calculation.
// @param section Section type
// @returns Energy level (1=lowest, 4=highest)
int getSectionEnergy(SectionType section);

// Track-relative velocity multipliers for consistent mix balance.
struct VelocityBalance {
  static constexpr float VOCAL = 1.0f;
  static constexpr float CHORD = 0.75f;
  static constexpr float BASS = 0.85f;
  static constexpr float DRUMS = 0.90f;
  static constexpr float MOTIF = 0.70f;
  static constexpr float ARPEGGIO = 0.85f;
  static constexpr float AUX = 0.65f;  // Subdued aux track to not overpower vocal

  // Get the multiplier for a track role.
  // @param role Track role
  // @returns Velocity multiplier
  static float getMultiplier(TrackRole role);
};

// Named velocity ratio constants for consistent dynamics across tracks.
// Use these instead of magic numbers like "* 0.85f".
namespace VelocityRatio {
  constexpr float ACCENT = 0.95f;       // Accented notes (slightly above normal)
  constexpr float NORMAL = 0.9f;        // Standard velocity
  constexpr float WEAK_BEAT = 0.85f;    // Off-beat or weak beat notes
  constexpr float SOFT = 0.8f;          // Softer notes (e.g., intro sections)
  constexpr float TENSION = 0.7f;       // Tension notes, octave doublings
  constexpr float BACKGROUND = 0.65f;   // Background elements
  constexpr float VERY_SOFT = 0.6f;     // Very subdued notes
  constexpr float GHOST = 0.5f;         // Ghost notes (drums)
}  // namespace VelocityRatio

// Apply crescendo/decrescendo dynamics at section transitions.
// Modifies velocities of notes in the transition region.
// B->Chorus transition applies crescendo across entire B section.
// @param track Track to modify (in-place)
// @param section_start Start tick of the current section
// @param section_end End tick of the current section
// @param from Section type being exited
// @param to Section type being entered
void applyTransitionDynamics(MidiTrack& track, Tick section_start,
                              Tick section_end, SectionType from, SectionType to);

// Apply transition dynamics to all melodic tracks based on arrangement.
// @param tracks Vector of tracks to modify (in-place)
// @param sections Arrangement sections
void applyAllTransitionDynamics(std::vector<MidiTrack*>& tracks,
                                 const std::vector<Section>& sections);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_VELOCITY_H
