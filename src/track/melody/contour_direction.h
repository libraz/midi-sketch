/**
 * @file contour_direction.h
 * @brief Pitch direction and contour control for melody generation.
 */

#ifndef MIDISKETCH_TRACK_MELODY_CONTOUR_DIRECTION_H
#define MIDISKETCH_TRACK_MELODY_CONTOUR_DIRECTION_H

#include <cstdint>
#include <optional>
#include <random>

#include "core/melody_templates.h"
#include "core/melody_types.h"
#include "core/pitch_utils.h"
#include "core/section_types.h"

namespace midisketch {
namespace melody {

/// @brief Get direction bias for explicit phrase contour template.
/// @param contour Contour type
/// @param phrase_pos Position within phrase (0.0-1.0)
/// @return Upward bias (0.0 = strongly down, 1.0 = strongly up)
float getDirectionBiasForContour(ContourType contour, float phrase_pos);

/// @brief Select pitch choice based on template and phrase position.
///
/// Implements rhythm-melody coupling: note duration influences pitch selection.
/// Short notes prefer chord tones for stability, long notes allow tensions.
///
/// Supports phrase contour templates when forced_contour is set.
///
/// @param tmpl Melody template with movement probabilities
/// @param phrase_pos Position within phrase (0.0-1.0)
/// @param has_target Whether we're approaching a target pitch
/// @param section_type Section type for directional bias
/// @param rng Random number generator
/// @param note_eighths Note duration in eighths (affects movement probability)
/// @param forced_contour Optional contour override for explicit phrase shaping
/// @return Selected pitch choice
PitchChoice selectPitchChoice(const MelodyTemplate& tmpl, float phrase_pos, bool has_target,
                              SectionType section_type, std::mt19937& rng,
                              float note_eighths = 2.0f,
                              std::optional<ContourType> forced_contour = std::nullopt);

/// @brief Apply direction inertia to pitch movement.
/// @param choice Current pitch choice
/// @param inertia Accumulated direction (-N to +N)
/// @param tmpl Melody template
/// @param rng Random number generator
/// @return Modified pitch choice (may change direction)
PitchChoice applyDirectionInertia(PitchChoice choice, int inertia, const MelodyTemplate& tmpl,
                                  std::mt19937& rng);

/// @brief Get effective plateau ratio considering register.
/// @param tmpl Melody template with base plateau ratio
/// @param current_pitch Current pitch
/// @param tessitura Tessitura range for register calculation
/// @return Effective plateau ratio (may be boosted for high notes)
float getEffectivePlateauRatio(const MelodyTemplate& tmpl, int current_pitch,
                               const TessituraRange& tessitura);

/// @brief Check if a leap should occur based on trigger conditions.
/// @param trigger Leap trigger type from template
/// @param phrase_pos Position within phrase (0.0-1.0)
/// @param section_pos Position within section (0.0-1.0)
/// @return true if conditions are right for a leap
bool shouldLeap(LeapTrigger trigger, float phrase_pos, float section_pos);

/// @brief Get stabilization step after a leap (leap compensation).
/// @param leap_direction Direction of the leap (+1 up, -1 down)
/// @param max_step Maximum step size in semitones
/// @return Stabilization step (opposite direction, small magnitude)
int getStabilizeStep(int leap_direction, int max_step);

/// @brief Check if two positions are in the same vowel section.
/// @param pos1 First position in beats
/// @param pos2 Second position in beats
/// @param phrase_length Phrase length in beats
/// @return true if positions are likely within same syllable
bool isInSameVowelSection(float pos1, float pos2, uint8_t phrase_length);

/// @brief Get maximum step size within a vowel section.
/// @param in_same_vowel Whether positions are in same vowel section
/// @return Maximum step in semitones (smaller if in same vowel)
int8_t getMaxStepInVowelSection(bool in_same_vowel);

}  // namespace melody
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_CONTOUR_DIRECTION_H
