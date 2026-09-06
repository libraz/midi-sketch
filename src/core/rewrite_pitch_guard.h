/**
 * @file rewrite_pitch_guard.h
 * @brief Where a pitch may land when a late pass moves it.
 *
 * Notes placed during generation go through the note_creator API, which asks
 * the harmony context before it writes. A pass that runs after generation is
 * past that gate: it already holds the note and is changing the number in it.
 * These are the constraints such a pass still has to satisfy by hand -- stay a
 * scale tone, stay inside the track's register, and do not land on the avoid
 * note of any chord the note sounds over.
 *
 * They answer where a pitch may go, not where it should. Choosing among the
 * legal landings is the moving pass's own business.
 */

#ifndef MIDISKETCH_CORE_REWRITE_PITCH_GUARD_H
#define MIDISKETCH_CORE_REWRITE_PITCH_GUARD_H

#include <cstdint>

#include "core/types.h"

namespace midisketch {

class IHarmonyContext;

/// @brief Nearest scale tone to @p pitch inside [low, high].
///
/// The range wins over proximity: when snapping to the scale pushes the pitch
/// back out of the range, the outermost scale tone still inside it is returned.
///
/// @param pitch Desired pitch (may sit far outside the range)
/// @param low Lowest pitch the track may sound
/// @param high Highest pitch the track may sound
/// @return A scale tone in [low, high]
uint8_t clampScalePitch(int pitch, uint8_t low, uint8_t high);

/// @brief Nearest scale tone in range that is not the chord's avoid note.
///
/// Candidates are walked outward from the *clamped* pitch rather than the raw
/// input. A raw pitch far outside the range (a register shift from a lower
/// octave, say) puts every offset out of range, the walk finds nothing, and the
/// avoid note from the initial clamp leaks through.
///
/// Only the chord sounding at @p tick is consulted. A note that sustains past a
/// chord change needs isAvoidNoteOverSpan as well.
///
/// @param pitch Desired pitch
/// @param tick Onset the chord is read at
/// @param harmony Harmony context supplying the chord
/// @param low Lowest pitch the track may sound
/// @param high Highest pitch the track may sound
/// @return A scale tone in [low, high], avoiding the chord's avoid note when one is reachable
uint8_t clampScalePitchAvoidingChord(int pitch, Tick tick, const IHarmonyContext& harmony,
                                     uint8_t low, uint8_t high);

/// @brief Whether @p pitch is the avoid note of any chord sounding under it.
///
/// A pitch checked only at its onset can sustain across a chord boundary into
/// an avoid relationship -- a B held from V into IV forms a tritone with the
/// new F root -- so every chord the note spans is asked, not just the first.
///
/// @param pitch Pitch to test
/// @param tick Onset
/// @param duration Sounding length; a zero duration is treated as one tick
/// @param harmony Harmony context supplying the chords
/// @return true when some chord in the span makes this pitch an avoid note
bool isAvoidNoteOverSpan(uint8_t pitch, Tick tick, Tick duration, const IHarmonyContext& harmony);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_REWRITE_PITCH_GUARD_H
