/**
 * @file safe_pitch_resolver.h
 * @brief Resolves safe pitches that avoid track collisions.
 *
 * Extracted from HarmonyContext as part of responsibility separation.
 * Uses chord progression and collision detection to find safe pitches.
 */

#ifndef MIDISKETCH_CORE_SAFE_PITCH_RESOLVER_H
#define MIDISKETCH_CORE_SAFE_PITCH_RESOLVER_H

#include "core/types.h"
#include <cstdint>

namespace midisketch {

class ChordProgressionTracker;
class TrackCollisionDetector;

/**
 * @brief Resolves safe pitches that avoid collisions with other tracks.
 *
 * Uses a multi-strategy approach:
 * 1. Check if desired pitch is already safe
 * 2. Try actual sounding pitches from other tracks (doubling is safe)
 * 3. Try theoretical chord tones
 * 4. Try consonant interval adjustments
 * 5. Exhaustive search in range
 */
class SafePitchResolver {
 public:
  SafePitchResolver() = default;

  /**
   * @brief Get a safe pitch that doesn't clash with other tracks.
   *
   * Tries chord tones first, then semitone adjustments.
   *
   * @param desired Desired MIDI pitch
   * @param start Start tick
   * @param duration Duration in ticks
   * @param track Track that will play this note
   * @param low Minimum allowed pitch
   * @param high Maximum allowed pitch
   * @param chord_tracker Chord progression tracker for chord tones
   * @param collision_detector Collision detector with registered notes
   * @return Safe pitch within range, or desired if no safe pitch found
   */
  uint8_t getSafePitch(uint8_t desired, Tick start, Tick duration,
                       TrackRole track, uint8_t low, uint8_t high,
                       const ChordProgressionTracker& chord_tracker,
                       const TrackCollisionDetector& collision_detector) const;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_SAFE_PITCH_RESOLVER_H
