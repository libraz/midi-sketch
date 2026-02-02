/**
 * @file constraint_pipeline.h
 * @brief Composable constraint pipeline for melody generation.
 *
 * Consolidates common constraint application patterns from melody_designer.cpp
 * including gate ratio calculation, chord boundary clamping, and pitch constraints.
 */

#ifndef MIDISKETCH_TRACK_MELODY_CONSTRAINT_PIPELINE_H
#define MIDISKETCH_TRACK_MELODY_CONSTRAINT_PIPELINE_H

#include <cstdint>
#include <optional>

#include "core/basic_types.h"

namespace midisketch {

class IHarmonyContext;

namespace melody {

/**
 * @brief Context for gate ratio calculation.
 */
struct GateContext {
  bool is_phrase_end = false;
  bool is_phrase_start = false;
  int interval_from_prev = 0;  // Semitones from previous note
  Tick note_duration = 0;
};

/**
 * @brief Calculate gate ratio for natural vocal-style articulation.
 *
 * Based on pop vocal theory:
 * - Phrase endings need breath preparation (85%)
 * - Same pitch = legato connection (100%)
 * - Step motion (1-2 semitones) = smooth legato (98%)
 * - Skip (3-5 semitones) = slight articulation (95%)
 * - Leap (6+ semitones) = preparation time (92%)
 * - Long notes (quarter+) = no gate needed (100%)
 *
 * @param ctx Gate context with phrase position and interval info
 * @return Gate ratio (0.85 - 1.0)
 */
float calculateGateRatio(const GateContext& ctx);

/**
 * @brief Apply gate ratio to a note duration.
 *
 * Calculates the gate ratio based on context and applies it to the duration,
 * ensuring the result is at least the minimum duration.
 *
 * @param duration Original duration
 * @param ctx Gate context
 * @param min_duration Minimum allowed duration (default: TICK_SIXTEENTH)
 * @return Gated duration
 */
Tick applyGateRatio(Tick duration, const GateContext& ctx, Tick min_duration = 0);

/**
 * @brief Clamp note duration at chord boundary if pitch is unsafe in next chord.
 *
 * Uses analyzeChordBoundary() to determine if the pitch is a chord tone
 * in the next chord. If it is (ChordTone/Tension/NoBoundary), the note
 * sustains naturally. If NonChordTone or AvoidNote, clips to boundary.
 *
 * @param note_start Note start tick
 * @param note_duration Current note duration
 * @param harmony Harmony context for chord boundary analysis
 * @param pitch MIDI pitch to check against next chord (0 = no check, returns unchanged)
 * @param gap_ticks Gap before boundary (default: 10 ticks)
 * @param min_duration Minimum allowed duration
 * @return Clamped duration
 */
Tick clampToChordBoundary(Tick note_start, Tick note_duration, const IHarmonyContext& harmony,
                          uint8_t pitch, Tick gap_ticks = 10, Tick min_duration = 0);

/**
 * @brief Clamp note duration to phrase boundary.
 *
 * @param note_start Note start tick
 * @param note_duration Current note duration
 * @param phrase_end Phrase end tick
 * @param min_duration Minimum allowed duration
 * @return Clamped duration
 */
Tick clampToPhraseBoundary(Tick note_start, Tick note_duration, Tick phrase_end,
                           Tick min_duration = 0);

/**
 * @brief Find chord tone in a given direction from the current pitch.
 *
 * Searches for a chord tone that moves in the specified direction
 * while staying within the vocal range.
 *
 * @param current_pitch Current MIDI pitch
 * @param chord_degree Current chord degree
 * @param direction +1 for ascending, -1 for descending, 0 for nearest
 * @param vocal_low Minimum allowed pitch
 * @param vocal_high Maximum allowed pitch
 * @param max_interval Maximum allowed interval (0 = unlimited)
 * @return Found chord tone pitch, or current pitch if none found
 */
int findChordToneInDirection(int current_pitch, int8_t chord_degree, int direction,
                             uint8_t vocal_low, uint8_t vocal_high, int max_interval = 0);

/**
 * @brief Combined duration constraint application.
 *
 * Applies gate ratio, chord boundary, and phrase boundary constraints
 * in sequence for a complete note duration adjustment.
 *
 * @param note_start Note start tick
 * @param note_duration Original duration
 * @param harmony Harmony context
 * @param phrase_end Phrase end tick
 * @param ctx Gate context
 * @return Final constrained duration
 */
Tick applyAllDurationConstraints(Tick note_start, Tick note_duration,
                                  const IHarmonyContext& harmony, Tick phrase_end,
                                  const GateContext& ctx, uint8_t pitch = 0);

}  // namespace melody
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_CONSTRAINT_PIPELINE_H
