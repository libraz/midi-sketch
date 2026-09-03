/**
 * @file accompaniment_ceiling.h
 * @brief Single derivation of an accompaniment track's upper pitch bound.
 */

#ifndef MIDISKETCH_TRACK_ACCOMPANIMENT_CEILING_H
#define MIDISKETCH_TRACK_ACCOMPANIMENT_CEILING_H

#include <algorithm>
#include <cstdint>

#include "core/basic_types.h"
#include "core/i_harmony_context.h"

namespace midisketch {

/// @brief Semitones an accompaniment track keeps between itself and the vocal.
///
/// Deliberately one value per track rather than one shared number: a dense chord bed
/// masks the lead far more than a single guitar line does, so they need different
/// room. Both sit inside the tolerance the generated output is scored against, which
/// allows an accompaniment note up to 2 semitones above the vocal.
namespace VocalCeilingMargin {

/// Chord voicings sit a minor third under the lead.
inline constexpr uint8_t kChord = 3;

/// A single guitar line may reach the lead's pitch but not pass it.
inline constexpr uint8_t kGuitar = 0;

}  // namespace VocalCeilingMargin

/**
 * @brief Upper pitch bound for an accompaniment track over one time window.
 *
 * The bound follows the vocal actually sounding in [start, end) rather than a fixed
 * per-instrument offset, because the same instrument needs a different ceiling
 * depending on where the lead sits at that moment.
 *
 * When the vocal rests across the whole window there is nothing to stay under, so the
 * track's own upper bound is returned. An accompaniment note cannot crowd a voice that
 * is not sounding, and this matches how the output is judged: the ceiling check is
 * skipped for ticks with no vocal. Callers that want a narrower bound during rests
 * must pass a lower @p range_high; this function does not invent one.
 *
 * @param harmony Harmony context used to query the vocal.
 * @param start Start of the window (inclusive).
 * @param end End of the window (exclusive).
 * @param range_low Track's lowest playable pitch; the result never falls below it.
 * @param range_high Track's highest playable pitch; the result never exceeds it.
 * @param margin_below_vocal Semitones to stay under the sounding vocal.
 * @return Effective upper bound, inside [range_low, range_high].
 */
inline uint8_t resolveVocalCeiling(const IHarmonyContext& harmony, Tick start, Tick end,
                                   uint8_t range_low, uint8_t range_high,
                                   uint8_t margin_below_vocal) {
  const uint8_t vocal_high = harmony.getHighestPitchForTrackInRange(start, end, TrackRole::Vocal);
  if (vocal_high == 0) return range_high;

  // Floor at range_low first: a vocal below the track's physical range would otherwise
  // invert [range_low, range_high] and clamp every note under the instrument's lowest
  // playable pitch.
  const int ceiling = static_cast<int>(vocal_high) - static_cast<int>(margin_below_vocal);
  return static_cast<uint8_t>(
      std::max(static_cast<int>(range_low), std::min(static_cast<int>(range_high), ceiling)));
}

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_ACCOMPANIMENT_CEILING_H
