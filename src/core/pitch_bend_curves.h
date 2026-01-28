/**
 * @file pitch_bend_curves.h
 * @brief Pitch bend curve generation for expressive vocal performance.
 *
 * Provides functions to generate natural-sounding pitch bend curves for
 * vocal expressions like scoop-up (shakuri-age), fall-off (gobi-fall),
 * and pitch slides (glide).
 */

#ifndef MIDISKETCH_CORE_PITCH_BEND_CURVES_H_
#define MIDISKETCH_CORE_PITCH_BEND_CURVES_H_

#include <vector>

#include "core/basic_types.h"
#include "core/timing_constants.h"

namespace midisketch {

/// @brief Pitch bend curve generation utilities.
namespace PitchBendCurves {

/// @brief Generate attack bend (scoop-up / shakuri-age).
///
/// Creates a curve that starts below the target pitch and quickly rises
/// to center (no bend). Common vocal ornament for expressiveness.
///
/// @param note_start Note start tick
/// @param depth_cents Initial dip depth in cents (negative, -20 to -50 recommended)
/// @param duration Curve length in ticks (default: 16th note)
/// @return Vector of pitch bend events forming the curve
std::vector<PitchBendEvent> generateAttackBend(Tick note_start, int depth_cents = -30,
                                                Tick duration = TICK_SIXTEENTH);

/// @brief Generate fall-off at phrase end (gobi-fall).
///
/// Creates a curve that starts at center and gradually falls below pitch.
/// Common vocal ornament for phrase endings.
///
/// @param note_end Note end tick
/// @param depth_cents Fall depth in cents (negative, -50 to -100 recommended)
/// @param duration Curve length in ticks (default: 8th note)
/// @return Vector of pitch bend events forming the curve
std::vector<PitchBendEvent> generateFallOff(Tick note_end, int depth_cents = -80,
                                             Tick duration = TICK_EIGHTH);

/// @brief Generate pitch slide between notes (glide).
///
/// Creates a smooth transition curve for sliding between pitches.
///
/// @param from_tick Start tick of the slide
/// @param to_tick End tick of the slide
/// @param semitone_diff Semitone difference (positive = up, negative = down)
/// @return Vector of pitch bend events forming the slide
std::vector<PitchBendEvent> generateSlide(Tick from_tick, Tick to_tick, int semitone_diff);

/// @brief Generate vibrato pattern.
///
/// Creates a sinusoidal pitch oscillation for vibrato effect.
///
/// @param start_tick Start tick of vibrato
/// @param duration Duration of vibrato in ticks
/// @param depth_cents Vibrato depth in cents (typically 10-30)
/// @param rate_hz Vibrato rate in Hz (typically 5-7 Hz)
/// @param bpm Tempo for timing calculation
/// @return Vector of pitch bend events forming the vibrato
std::vector<PitchBendEvent> generateVibrato(Tick start_tick, Tick duration, int depth_cents = 20,
                                             float rate_hz = 5.5f, uint16_t bpm = 120);

/// @brief Convert cents to pitch bend value.
///
/// Assumes standard +/- 2 semitone (200 cents) bend range.
///
/// @param cents Pitch offset in cents
/// @return Pitch bend value (-8192 to +8191)
int16_t centsToBendValue(int cents);

/// @brief Reset pitch bend to center at specified tick.
///
/// @param tick Position to reset
/// @return Single pitch bend event at center
PitchBendEvent resetBend(Tick tick);

}  // namespace PitchBendCurves

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_PITCH_BEND_CURVES_H_
