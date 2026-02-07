/**
 * @file i_collision_detector.h
 * @brief Interface for inter-track collision detection and pitch queries.
 *
 * Extracted from IHarmonyContext to allow consumers that only need
 * collision detection to depend on a narrower interface.
 */

#ifndef MIDISKETCH_CORE_I_COLLISION_DETECTOR_H
#define MIDISKETCH_CORE_I_COLLISION_DETECTOR_H

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "core/i_chord_lookup.h"

namespace midisketch {

/**
 * @brief Interface for collision detection and pitch queries.
 *
 * Extends IChordLookup with collision detection (minor 2nd, major 7th)
 * and pitch range queries. Consumers that only need to check pitch safety
 * and query sounding pitches should depend on this interface rather than
 * the full IHarmonyContext.
 */
class ICollisionDetector : public IChordLookup {
 public:
  virtual ~ICollisionDetector() = default;

  /**
   * @brief Check if a pitch is safe from collisions.
   *
   * Detects minor 2nd (1 semitone) and major 7th (11 semitones) clashes.
   * On weak beats (is_weak_beat=true), major 2nd (2 semitones) is allowed
   * as a passing tone.
   *
   * @param pitch MIDI pitch to check
   * @param start Start tick
   * @param duration Duration in ticks
   * @param exclude Exclude notes from this track when checking
   * @param is_weak_beat If true, allow major 2nd as passing tone (default: false)
   * @return true if pitch doesn't clash with other tracks
   */
  virtual bool isConsonantWithOtherTracks(uint8_t pitch, Tick start, Tick duration, TrackRole exclude,
                           bool is_weak_beat = false) const = 0;

  /**
   * @brief Get detailed collision information for a pitch.
   *
   * Returns information about the first collision found, including
   * the colliding note's pitch, track, and interval.
   *
   * @param pitch MIDI pitch to check
   * @param start Start tick
   * @param duration Duration in ticks
   * @param exclude Exclude notes from this track when checking
   * @return CollisionInfo with details about the collision (if any)
   */
  virtual CollisionInfo getCollisionInfo(uint8_t pitch, Tick start, Tick duration,
                                         TrackRole exclude) const {
    // Default implementation: just report if collision exists
    CollisionInfo info;
    info.has_collision = !isConsonantWithOtherTracks(pitch, start, duration, exclude);
    return info;
  }

  /**
   * @brief Check for low register collision with bass.
   *
   * Uses stricter thresholds below C4 (intervals sound muddy in low register).
   *
   * @param pitch MIDI pitch to check
   * @param start Start tick
   * @param duration Duration in ticks
   * @param threshold Semitone threshold for collision (default: 3)
   * @return true if collision detected (pitch is unsafe)
   */
  virtual bool hasBassCollision(uint8_t pitch, Tick start, Tick duration,
                                int threshold = 3) const = 0;

  /**
   * @brief Get the maximum safe end tick for extending a note without creating clashes.
   *
   * Used by PostProcessor when extending note durations. Returns the earliest tick
   * where extending the note would create a dissonant interval with another track.
   *
   * @param note_start Start tick of the note being extended
   * @param pitch MIDI pitch of the note
   * @param exclude Track role to exclude (the track containing the note)
   * @param desired_end The desired end tick (extension target)
   * @return Safe end tick (may be less than desired_end if clash would occur)
   */
  virtual Tick getMaxSafeEnd(Tick note_start, uint8_t pitch, TrackRole exclude,
                             Tick desired_end) const = 0;

  // =========================================================================
  // Pitch class queries
  // =========================================================================

  /**
   * @brief Get pitch classes from a specific track at a tick.
   *
   * Returns all pitch classes (0-11) for notes from the specified track
   * that are sounding at the given tick.
   *
   * @param tick Position in ticks
   * @param role Which track to query
   * @return Vector of pitch classes (may be empty if no notes sounding)
   */
  virtual std::vector<int> getPitchClassesFromTrackAt(Tick tick, TrackRole role) const = 0;

  /**
   * @brief Get pitch classes from a specific track within a time range.
   *
   * Returns all pitch classes (0-11) for notes from the specified track
   * that are sounding at any point within [start, end).
   *
   * @param start Start of time range
   * @param end End of time range
   * @param role Which track to query
   * @return Vector of pitch classes (may be empty if no notes in range)
   */
  virtual std::vector<int> getPitchClassesFromTrackInRange(Tick start, Tick end,
                                                            TrackRole role) const = 0;

  /**
   * @brief Get pitch classes currently sounding from all tracks except one.
   *
   * Used for chord voicing to find doubling candidates when no unique safe pitch exists.
   * Returns pitch classes (0-11) for notes sounding in [start, end) from all tracks
   * except the excluded track.
   *
   * @param start Start of time range
   * @param end End of time range
   * @param exclude Track role to exclude (typically the track being generated)
   * @return Vector of unique pitch classes (may be empty if no notes in range)
   */
  virtual std::vector<int> getSoundingPitchClasses(Tick start, Tick end,
                                                     TrackRole exclude) const = 0;

  /**
   * @brief Get actual pitches currently sounding from all tracks except one.
   *
   * Unlike getSoundingPitchClasses which returns pitch classes (0-11), this returns
   * actual MIDI pitches (0-127). Used for doubling where we need the exact pitch
   * to avoid collisions with other simultaneous notes.
   *
   * @param start Start of time range
   * @param end End of time range
   * @param exclude Track role to exclude (typically the track being generated)
   * @return Vector of unique MIDI pitches (may be empty if no notes in range)
   */
  virtual std::vector<uint8_t> getSoundingPitches(Tick start, Tick end,
                                                    TrackRole exclude) const = 0;

  // =========================================================================
  // Track range queries
  // =========================================================================

  /**
   * @brief Get the highest MIDI pitch from a specific track within a time range.
   *
   * Returns the highest actual MIDI pitch (0-127) for notes from the specified
   * track that overlap with [start, end). Returns 0 if no notes found.
   * Used for per-bar vocal ceiling in accompaniment tracks.
   *
   * @param start Start of time range
   * @param end End of time range
   * @param role Which track to query
   * @return Highest MIDI pitch, or 0 if no notes in range
   */
  virtual uint8_t getHighestPitchForTrackInRange(Tick start, Tick end, TrackRole role) const = 0;

  /**
   * @brief Get the lowest MIDI pitch from a specific track within a time range.
   *
   * Returns the lowest actual MIDI pitch (1-127) for notes from the specified
   * track that overlap with [start, end). Returns 0 if no notes found.
   * Used for per-onset vocal ceiling: accompaniment should not exceed the
   * lowest concurrent vocal pitch to prevent pitch crossing at any point.
   *
   * @param start Start of time range
   * @param end End of time range
   * @param role Which track to query
   * @return Lowest MIDI pitch, or 0 if no notes in range
   */
  virtual uint8_t getLowestPitchForTrackInRange(Tick start, Tick end, TrackRole role) const = 0;

  // =========================================================================
  // Debugging
  // =========================================================================

  /**
   * @brief Dump collision state at a specific tick for debugging.
   *
   * @param tick The tick to inspect
   * @param range_ticks How many ticks around the target to include (default: 1920 = 1 bar)
   * @return Formatted string with collision state
   */
  virtual std::string dumpNotesAt(Tick tick, Tick range_ticks = 1920) const = 0;

  /**
   * @brief Get a structured snapshot of collision state at a specific tick.
   *
   * Returns structured data for programmatic analysis and testing.
   *
   * @param tick The tick to inspect
   * @param range_ticks How many ticks around the target to include (default: 1920 = 1 bar)
   * @return CollisionSnapshot with notes and clashes
   */
  virtual CollisionSnapshot getCollisionSnapshot(Tick tick, Tick range_ticks = 1920) const = 0;

  /// C4 (middle C) - below this, stricter low-register rules apply.
  static constexpr uint8_t LOW_REGISTER_THRESHOLD = 60;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_I_COLLISION_DETECTOR_H
