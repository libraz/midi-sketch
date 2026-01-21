/**
 * @file track_collision_detector.h
 * @brief Detects pitch collisions between tracks.
 *
 * Extracted from HarmonyContext as part of responsibility separation.
 * Manages note registration and collision detection.
 */

#ifndef MIDISKETCH_CORE_TRACK_COLLISION_DETECTOR_H
#define MIDISKETCH_CORE_TRACK_COLLISION_DETECTOR_H

#include <vector>

#include "core/types.h"

namespace midisketch {

class MidiTrack;
class ChordProgressionTracker;

/**
 * @brief Detects pitch collisions between tracks.
 *
 * Registers notes from all tracks and provides collision detection.
 * Uses chord context for smarter dissonance detection.
 * Low register (below C4) uses stricter collision thresholds.
 */
class TrackCollisionDetector {
 public:
  /// C4 (middle C) - below this, stricter low-register rules apply.
  static constexpr uint8_t LOW_REGISTER_THRESHOLD = 60;

  TrackCollisionDetector() = default;

  /**
   * @brief Register a note from a track for collision detection.
   * @param start Start tick of the note
   * @param duration Duration in ticks
   * @param pitch MIDI pitch
   * @param track Which track this note belongs to
   */
  void registerNote(Tick start, Tick duration, uint8_t pitch, TrackRole track);

  /**
   * @brief Register all notes from a completed track.
   * @param track The MidiTrack containing notes to register
   * @param role Which track role this represents
   */
  void registerTrack(const MidiTrack& track, TrackRole role);

  /**
   * @brief Check if a pitch is safe from collisions.
   *
   * Detects minor 2nd (1 semitone) and major 7th (11 semitones) clashes.
   *
   * @param pitch MIDI pitch to check
   * @param start Start tick
   * @param duration Duration in ticks
   * @param exclude Exclude notes from this track when checking
   * @param chord_tracker Optional chord tracker for context-aware detection
   * @return true if pitch doesn't clash with other tracks
   */
  bool isPitchSafe(uint8_t pitch, Tick start, Tick duration, TrackRole exclude,
                   const ChordProgressionTracker* chord_tracker = nullptr) const;

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
  bool hasBassCollision(uint8_t pitch, Tick start, Tick duration, int threshold = 3) const;

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
  std::vector<int> getPitchClassesFromTrackAt(Tick tick, TrackRole role) const;

  /// Clear all registered notes (useful for regeneration).
  void clearNotes();

  /// Clear notes from a specific track only.
  void clearNotesForTrack(TrackRole track);

  /// Get all registered notes (for SafePitchResolver).
  const auto& notes() const { return notes_; }

 private:
  // Registered note from a track.
  struct RegisteredNote {
    Tick start;
    Tick end;
    uint8_t pitch;
    TrackRole track;
  };

  std::vector<RegisteredNote> notes_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TRACK_COLLISION_DETECTOR_H
