/**
 * @file safe_pitch_resolver.h
 * @brief Resolves safe pitches that avoid track collisions.
 *
 * Extracted from HarmonyContext as part of responsibility separation.
 * Uses chord progression and collision detection to find safe pitches.
 */

#ifndef MIDISKETCH_CORE_SAFE_PITCH_RESOLVER_H
#define MIDISKETCH_CORE_SAFE_PITCH_RESOLVER_H

#include <cstdint>

#include "core/basic_types.h"
#include "core/types.h"

namespace midisketch {

class ChordProgressionTracker;
class TrackCollisionDetector;

/**
 * @brief Result of pitch resolution with strategy information.
 */
struct PitchResolutionResult {
  uint8_t pitch;                                        ///< Resolved pitch
  CollisionAvoidStrategy strategy;                      ///< Strategy that succeeded
};

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
   * @brief Get the best available pitch that minimizes clashes with other tracks.
   *
   * Tries chord tones first, then semitone adjustments.
   * Note: This function returns the best available pitch, but does NOT guarantee
   * the returned pitch is collision-free. If no safe alternative exists,
   * the original desired pitch is returned.
   *
   * @param desired Desired MIDI pitch
   * @param start Start tick
   * @param duration Duration in ticks
   * @param track Track that will play this note
   * @param low Minimum allowed pitch
   * @param high Maximum allowed pitch
   * @param chord_tracker Chord progression tracker for chord tones
   * @param collision_detector Collision detector with registered notes
   * @return Best available pitch within range, or desired if no better option found
   */
  uint8_t getBestAvailablePitch(uint8_t desired, Tick start, Tick duration, TrackRole track,
                                uint8_t low, uint8_t high,
                                const ChordProgressionTracker& chord_tracker,
                                const TrackCollisionDetector& collision_detector) const;

  /**
   * @brief Resolve pitch with strategy tracking.
   *
   * Same as getBestAvailablePitch but also returns which strategy succeeded.
   * Used for debugging and provenance tracking.
   *
   * @param desired Desired MIDI pitch
   * @param start Start tick
   * @param duration Duration in ticks
   * @param track Track that will play this note
   * @param low Minimum allowed pitch
   * @param high Maximum allowed pitch
   * @param chord_tracker Chord progression tracker for chord tones
   * @param collision_detector Collision detector with registered notes
   * @return PitchResolutionResult with resolved pitch and strategy used
   */
  PitchResolutionResult resolvePitchWithStrategy(uint8_t desired, Tick start, Tick duration,
                                                  TrackRole track, uint8_t low, uint8_t high,
                                                  const ChordProgressionTracker& chord_tracker,
                                                  const TrackCollisionDetector& collision_detector) const;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_SAFE_PITCH_RESOLVER_H
