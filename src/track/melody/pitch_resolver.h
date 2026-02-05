/**
 * @file pitch_resolver.h
 * @brief Pitch resolution logic for melody generation.
 */

#ifndef MIDISKETCH_TRACK_MELODY_PITCH_RESOLVER_H
#define MIDISKETCH_TRACK_MELODY_PITCH_RESOLVER_H

#include <cstdint>
#include <random>
#include <vector>

#include "core/melody_templates.h"
#include "core/melody_types.h"
#include "core/types.h"

namespace midisketch {

class IHarmonyContext;

namespace melody {

/// @brief Apply pitch choice to determine new pitch.
///
/// VocalAttitude affects candidate pitches:
///   Clean: chord tones only (1, 3, 5)
///   Expressive: chord tones + tensions (7, 9)
///   Raw: all scale tones
///
/// Rhythm-melody coupling:
///   Short notes (< 1 eighth): Force chord tones for stability
///   Long notes (>= 4 eighths): Allow tensions if attitude permits
///
/// @param choice Pitch movement choice (Same, StepUp, StepDown, TargetStep)
/// @param current_pitch Current pitch
/// @param target_pitch Target pitch for TargetStep choice (-1 if none)
/// @param chord_degree Current chord degree
/// @param key_offset Key offset from C major
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @param attitude Vocal attitude affecting tension allowance
/// @param disable_singability Allow large intervals (for machine-style vocals)
/// @param note_eighths Note duration in eighths (affects chord tone preference)
/// @param tension_usage Tension note probability (0.0=chord tones only, 1.0=always add tensions)
/// @return New pitch after applying choice
int applyPitchChoiceImpl(PitchChoice choice, int current_pitch, int target_pitch,
                         int8_t chord_degree, int key_offset, uint8_t vocal_low,
                         uint8_t vocal_high, VocalAttitude attitude,
                         bool disable_singability = false, float note_eighths = 2.0f,
                         float tension_usage = 0.2f);

/// @brief Calculate target pitch for phrase based on template and context.
///
/// Target is typically a chord tone in the upper part of tessitura.
///
/// @param tmpl Melody template with tessitura settings
/// @param tessitura_center Center of tessitura range
/// @param tessitura_range Range of tessitura
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @param section_start Section start tick
/// @param harmony Harmony context for chord lookup
/// @return Target pitch
int calculateTargetPitchImpl(const MelodyTemplate& tmpl, int tessitura_center, int tessitura_range,
                             uint8_t vocal_low, uint8_t vocal_high, Tick section_start,
                             const IHarmonyContext& harmony);

}  // namespace melody
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_PITCH_RESOLVER_H
