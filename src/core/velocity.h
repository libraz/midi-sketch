#ifndef MIDISKETCH_CORE_VELOCITY_H
#define MIDISKETCH_CORE_VELOCITY_H

#include "core/types.h"

namespace midisketch {

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

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_VELOCITY_H
