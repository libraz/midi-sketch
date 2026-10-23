/**
 * @file accompaniment_ceiling.h
 * @brief Upper pitch bound for a track that stays under the sounding vocal.
 */

#ifndef MIDISKETCH_TRACK_ACCOMPANIMENT_CEILING_H
#define MIDISKETCH_TRACK_ACCOMPANIMENT_CEILING_H

#include <algorithm>
#include <cstdint>

#include "core/basic_types.h"
#include "core/i_harmony_context.h"

namespace midisketch {

/// @brief Semitones a track keeps between itself and the vocal, for tracks that
/// derive their ceiling through resolveVocalCeiling().
///
/// The chord track does not use this: its voicings follow getVocalCeilingForRange(),
/// which keeps a minimum register and falls back to a bar-level ceiling.
namespace VocalCeilingMargin {

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

/**
 * @brief Whether the vocal sounds exactly @p pitch anywhere in [start, end).
 *
 * The cross-track consonance check treats a unison as consonant, so a resolver
 * can settle an accompaniment note on the lead's own pitch; this is the screen
 * that refuses it. The context reports the vocal's extremes per window rather
 * than its pitches, so the window is halved until a pitch is an extreme of one.
 *
 * @param harmony Harmony context with the vocal registered.
 * @param pitch Accompaniment pitch to test.
 * @param start Start of the window (inclusive).
 * @param end End of the window (exclusive).
 */
inline bool vocalSoundsPitch(const IHarmonyContext& harmony, uint8_t pitch, Tick start, Tick end) {
  const uint8_t high = harmony.getHighestPitchForTrackInRange(start, end, TrackRole::Vocal);
  if (high == 0 || pitch > high) return false;
  const uint8_t low = harmony.getLowestPitchForTrackInRange(start, end, TrackRole::Vocal);
  if (pitch < low) return false;
  if (pitch == high || pitch == low) return true;
  if (end - start <= 1) return false;
  const Tick mid = start + (end - start) / 2;
  return vocalSoundsPitch(harmony, pitch, start, mid) || vocalSoundsPitch(harmony, pitch, mid, end);
}

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_ACCOMPANIMENT_CEILING_H
