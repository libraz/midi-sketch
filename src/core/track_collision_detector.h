/**
 * @file track_collision_detector.h
 * @brief Detects pitch collisions between tracks.
 *
 * Extracted from HarmonyContext as part of responsibility separation.
 * Manages note registration and collision detection.
 */

#ifndef MIDISKETCH_CORE_TRACK_COLLISION_DETECTOR_H
#define MIDISKETCH_CORE_TRACK_COLLISION_DETECTOR_H

#include <string>
#include <vector>

#include "core/basic_types.h"
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

  /// Duration thresholds for passing tone tolerance.
  /// Brief dissonances (m2, M2) are tolerated when overlap is short enough.
  static constexpr Tick PASSING_TONE_16TH_OVERLAP = 120;  ///< 16th note: m2,M2 tolerated
  static constexpr Tick PASSING_TONE_8TH_OVERLAP = 240;   ///< 8th note: M2 tolerated

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
   * On weak beats (is_weak_beat=true), major 2nd (2 semitones) is allowed
   * as a passing tone.
   *
   * @param pitch MIDI pitch to check
   * @param start Start tick
   * @param duration Duration in ticks
   * @param exclude Exclude notes from this track when checking
   * @param chord_tracker Optional chord tracker for context-aware detection
   * @param is_weak_beat If true, allow major 2nd as passing tone (default: false)
   * @return true if pitch doesn't clash with other tracks
   */
  bool isConsonantWithOtherTracks(uint8_t pitch, Tick start, Tick duration, TrackRole exclude,
                   const ChordProgressionTracker* chord_tracker = nullptr,
                   bool is_weak_beat = false) const;

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
   * @param chord_tracker Optional chord tracker for context-aware detection
   * @return CollisionInfo with details about the collision (if any)
   */
  CollisionInfo getCollisionInfo(uint8_t pitch, Tick start, Tick duration, TrackRole exclude,
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
  std::vector<int> getPitchClassesFromTrackInRange(Tick start, Tick end, TrackRole role) const;

  /**
   * @brief Get pitch classes from all tracks except one within a time range.
   *
   * Returns all pitch classes (0-11) for notes from all tracks except the excluded one
   * that are sounding at any point within [start, end). Used for finding doubling
   * candidates in chord voicing.
   *
   * @param start Start of time range
   * @param end End of time range
   * @param exclude Track role to exclude
   * @return Vector of unique pitch classes (may be empty if no notes in range)
   */
  std::vector<int> getSoundingPitchClasses(Tick start, Tick end, TrackRole exclude) const;

  /**
   * @brief Get actual pitches from all tracks except one within a time range.
   *
   * Returns all MIDI pitches (0-127) for notes from all tracks except the excluded one
   * that are sounding at any point within [start, end). Used for exact doubling
   * where we need the actual pitch to avoid collisions.
   *
   * @param start Start of time range
   * @param end End of time range
   * @param exclude Track role to exclude
   * @return Vector of unique MIDI pitches (may be empty if no notes in range)
   */
  std::vector<uint8_t> getSoundingPitches(Tick start, Tick end, TrackRole exclude) const;

  /**
   * @brief Get the highest MIDI pitch from a specific track within a time range.
   *
   * Returns the highest actual MIDI pitch (0-127) for notes from the specified
   * track that overlap with [start, end). Returns 0 if no notes found.
   *
   * @param start Start of time range
   * @param end End of time range
   * @param role Which track to query
   * @return Highest MIDI pitch, or 0 if no notes in range
   */
  uint8_t getHighestPitchForTrackInRange(Tick start, Tick end, TrackRole role) const;

  /**
   * @brief Get the lowest MIDI pitch from a specific track within a time range.
   *
   * Returns the lowest actual MIDI pitch (1-127) for notes from the specified
   * track that overlap with [start, end). Returns 0 if no notes found.
   *
   * @param start Start of time range
   * @param end End of time range
   * @param role Which track to query
   * @return Lowest MIDI pitch, or 0 if no notes in range
   */
  uint8_t getLowestPitchForTrackInRange(Tick start, Tick end, TrackRole role) const;

  /// Clear all registered notes (useful for regeneration).
  void clearNotes();

  /// Clear notes from a specific track only.
  void clearNotesForTrack(TrackRole track);

  /**
   * @brief Register a phantom note (guide chord).
   *
   * Same collision behavior as normal notes, but can be selectively
   * cleared without affecting real track notes.
   *
   * @param start Start tick of the note
   * @param duration Duration in ticks
   * @param pitch MIDI pitch
   * @param track Which track this note belongs to
   */
  void registerPhantomNote(Tick start, Tick duration, uint8_t pitch, TrackRole track);

  /// Clear only phantom notes (preserves real track notes).
  void clearPhantomNotes();

  /// Get all registered notes.
  const auto& notes() const { return notes_; }

  /**
   * @brief Dump registered notes at a specific tick for debugging.
   *
   * Outputs all notes overlapping with the given tick, including their
   * track, pitch, and time range. Also shows potential clashes.
   *
   * @param tick The tick to inspect
   * @param range_ticks How many ticks around the target to include (default: 1920 = 1 bar)
   * @return Formatted string with collision state
   */
  std::string dumpNotesAt(Tick tick, Tick range_ticks = 1920) const;

  /**
   * @brief Get a structured snapshot of collision state at a specific tick.
   *
   * Returns structured data instead of a formatted string, suitable for
   * programmatic analysis and testing.
   *
   * @param tick The tick to inspect
   * @param range_ticks How many ticks around the target to include (default: 1920 = 1 bar)
   * @return CollisionSnapshot with notes and clashes
   */
  CollisionSnapshot getCollisionSnapshot(Tick tick, Tick range_ticks = 1920) const;

  /**
   * @brief Get the maximum safe end tick for extending a note.
   *
   * Scans registered notes to find the earliest start tick of a note that
   * would create a dissonant interval if the given note were extended to overlap it.
   *
   * @param note_start Start tick of the note being extended
   * @param pitch MIDI pitch of the note
   * @param exclude Track role to exclude
   * @param desired_end The desired end tick
   * @return Safe end tick (may be less than desired_end)
   */
  Tick getMaxSafeEnd(Tick note_start, uint8_t pitch, TrackRole exclude,
                     Tick desired_end) const;

 private:
  // Registered note from a track.
  struct RegisteredNote {
    Tick start;
    Tick end;
    uint8_t pitch;
    TrackRole track;
    bool is_phantom = false;  ///< Guide chord phantom note (clearable separately)
  };

  // Collect note indices overlapping [start, end) using beat index.
  // Caller must handle potential duplicate indices (when notes span multiple beats).
  void collectNoteIndices(Tick start, Tick end, std::vector<size_t>& out) const;

  // Rebuild beat_index_ from scratch (used after clearNotesForTrack).
  void rebuildBeatIndex();

  std::vector<RegisteredNote> notes_;

  // Beat-indexed note lookup: beat_index_[beat_number] = {note indices in notes_}
  std::vector<std::vector<size_t>> beat_index_;
};

/// @brief Check if a dissonance should be tolerated as a brief passing tone.
///
/// Applies strong-beat reduction: thresholds halved on beats 1 and 3.
/// Only tolerates stepwise intervals (m2 = 1, M2 = 2).
/// Low register guard: both notes below C4 are never tolerated (muddy).
///
/// @param actual_semitones Absolute interval in semitones
/// @param overlap_duration Temporal overlap in ticks between the two notes
/// @param candidate_pitch MIDI pitch of the note being checked
/// @param existing_pitch MIDI pitch of the already-registered note
/// @param note_start Start tick of the candidate note (for beat position)
/// @return true if the dissonance should be tolerated
inline bool isToleratedPassingTone(int actual_semitones, Tick overlap_duration,
                                   uint8_t candidate_pitch, uint8_t existing_pitch,
                                   Tick note_start) {
  if (actual_semitones != 1 && actual_semitones != 2) return false;

  // Low register guard: both notes < C4 → muddy regardless of duration
  if (candidate_pitch < TrackCollisionDetector::LOW_REGISTER_THRESHOLD &&
      existing_pitch < TrackCollisionDetector::LOW_REGISTER_THRESHOLD) {
    return false;
  }

  // Strong beat reduction: halve thresholds on beats 1 and 3
  constexpr Tick kHalfBar = TICKS_PER_BAR / 2;
  Tick tick_in_bar = note_start % TICKS_PER_BAR;
  bool is_strong_beat = (tick_in_bar % kHalfBar) < TICKS_PER_BEAT;

  Tick threshold_16th = TrackCollisionDetector::PASSING_TONE_16TH_OVERLAP;
  Tick threshold_8th = TrackCollisionDetector::PASSING_TONE_8TH_OVERLAP;
  if (is_strong_beat) {
    threshold_16th /= 2;  // 120 → 60
    threshold_8th /= 2;   // 240 → 120
  }

  // m2 (1 semitone): chromatic passing tone, short overlap only
  if (actual_semitones == 1) {
    return overlap_duration <= threshold_16th;
  }

  // M2 (2 semitones): diatonic passing tone, wider tolerance
  return overlap_duration <= threshold_8th;
}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TRACK_COLLISION_DETECTOR_H
