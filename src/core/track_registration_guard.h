/**
 * @file track_registration_guard.h
 * @brief RAII guard for automatic track registration with HarmonyContext.
 *
 * Ensures tracks are registered with HarmonyContext when generation scope ends,
 * preventing the common bug of forgetting to call registerTrack().
 */

#ifndef MIDISKETCH_CORE_TRACK_REGISTRATION_GUARD_H
#define MIDISKETCH_CORE_TRACK_REGISTRATION_GUARD_H

#include "core/basic_types.h"

namespace midisketch {

// Forward declarations
class IHarmonyContext;
class MidiTrack;

/**
 * @brief RAII guard that automatically registers a track when destroyed.
 *
 * Usage:
 * @code
 * {
 *   TrackRegistrationGuard guard(harmony_context, song.vocal(), TrackRole::Vocal);
 *   // Generate notes...
 *   generateVocalTrack(song.vocal(), ...);
 * } // Track automatically registered here
 * @endcode
 *
 * This prevents the common bug of forgetting to call registerTrack() after
 * generating a track, which would cause track-to-track coordination to fail.
 */
class TrackRegistrationGuard {
 public:
  /**
   * @brief Construct a guard for deferred track registration.
   * @param harmony Reference to harmony context (must outlive this guard)
   * @param track Reference to track being generated (must outlive this guard)
   * @param role Role of the track
   */
  TrackRegistrationGuard(IHarmonyContext& harmony, const MidiTrack& track, TrackRole role);

  /// @brief Destructor - registers the track with HarmonyContext.
  ~TrackRegistrationGuard();

  // Non-copyable
  TrackRegistrationGuard(const TrackRegistrationGuard&) = delete;
  TrackRegistrationGuard& operator=(const TrackRegistrationGuard&) = delete;

  // Movable
  TrackRegistrationGuard(TrackRegistrationGuard&& other) noexcept;
  TrackRegistrationGuard& operator=(TrackRegistrationGuard&& other) noexcept;

  /**
   * @brief Cancel registration (e.g., if generation failed).
   *
   * Call this if you don't want the track to be registered (e.g., on error).
   * After calling cancel(), the destructor will not register the track.
   */
  void cancel();

  /**
   * @brief Manually register now and prevent destructor registration.
   *
   * Useful when you need to register before the scope ends.
   */
  void registerNow();

 private:
  IHarmonyContext* harmony_;
  const MidiTrack* track_;
  TrackRole role_;
  bool active_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TRACK_REGISTRATION_GUARD_H
