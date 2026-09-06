/**
 * @file bass_motion.h
 * @brief Moving a bass pitch in answer to where the vocal is going.
 *
 * The bass is generated after the vocal and before the chord, which makes it
 * the first track that can answer the melody and the last one with no
 * accompaniment of its own to answer to. Contrary motion under a rising line,
 * an oblique hold under a moving one -- that choice is made here.
 *
 * It is made as a displacement of a pitch the pattern already chose, never as
 * a pitch of its own, and the displacement lands only on a tone of the chord
 * sounding at that moment. Which pitches those are comes from the caller, so a
 * bar carrying a registered secondary dominant is voiced against that chord
 * rather than against the diatonic triad sharing its degree.
 */

#ifndef MIDISKETCH_TRACK_BASS_BASS_MOTION_H
#define MIDISKETCH_TRACK_BASS_BASS_MOTION_H

#include <cstdint>

#include "core/chord_utils.h"
#include "track/vocal/vocal_analysis.h"

namespace midisketch {

/// @brief Adjust a bass pitch according to vocal motion while staying on chord tones.
///
/// @param base_pitch The pitch the pattern chose
/// @param motion How this bass note should move against the vocal
/// @param vocal_direction Which way the vocal is moving here (-1, 0, +1)
/// @param vocal_pitch The pitch the vocal is sounding, 0 when silent
/// @param degree Scale degree of the bar's chord, used for its diatonic tones
/// @return A playable, diatonic pitch; base_pitch when nothing better exists
uint8_t adjustPitchForMotion(uint8_t base_pitch, MotionType motion, int8_t vocal_direction,
                             uint8_t vocal_pitch, int8_t degree);

/// @brief Motion adjustment against an explicit chord-tone set.
///
/// The overload above resolves the degree to its diatonic triad; this one takes
/// the tones directly, which is what a bar whose chord was replaced on the
/// shared timeline has to pass.
uint8_t adjustPitchForMotion(uint8_t base_pitch, MotionType motion, int8_t vocal_direction,
                             uint8_t vocal_pitch, const ChordTones& chord_tones);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_BASS_BASS_MOTION_H
