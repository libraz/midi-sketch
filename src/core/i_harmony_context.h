/**
 * @file i_harmony_context.h
 * @brief Interface for harmonic context management.
 *
 * Enables dependency injection for testing Generator and track generators.
 */

#ifndef MIDISKETCH_CORE_I_HARMONY_CONTEXT_H
#define MIDISKETCH_CORE_I_HARMONY_CONTEXT_H

#include <vector>

#include "core/types.h"

namespace midisketch {

class Arrangement;
class MidiTrack;
struct ChordProgression;

/**
 * @brief Interface for harmonic context management.
 *
 * Provides chord-tone lookup and collision detection (minor 2nd, major 7th).
 * Implement this interface to create test doubles for Generator testing.
 */
class IHarmonyContext {
 public:
  virtual ~IHarmonyContext() = default;

  /**
   * @brief Initialize with arrangement and chord progression.
   * @param arrangement The song arrangement (sections and timing)
   * @param progression Chord progression to use
   * @param mood Mood affects harmonic rhythm
   */
  virtual void initialize(const Arrangement& arrangement, const ChordProgression& progression,
                          Mood mood) = 0;

  /**
   * @brief Get chord degree at a specific tick.
   * @param tick Position in ticks
   * @return Scale degree (0=I, 1=ii, 2=iii, 3=IV, 4=V, 5=vi, 6=vii)
   */
  virtual int8_t getChordDegreeAt(Tick tick) const = 0;

  /**
   * @brief Get chord tones as pitch classes at a specific tick.
   * @param tick Position in ticks
   * @return Vector of pitch classes (0-11) that are chord tones
   */
  virtual std::vector<int> getChordTonesAt(Tick tick) const = 0;

  /**
   * @brief Register a note from a track for collision detection.
   * @param start Start tick of the note
   * @param duration Duration in ticks
   * @param pitch MIDI pitch
   * @param track Which track this note belongs to
   */
  virtual void registerNote(Tick start, Tick duration, uint8_t pitch, TrackRole track) = 0;

  /**
   * @brief Register all notes from a completed track.
   * @param track The MidiTrack containing notes to register
   * @param role Which track role this represents
   */
  virtual void registerTrack(const MidiTrack& track, TrackRole role) = 0;

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
  virtual bool isPitchSafe(uint8_t pitch, Tick start, Tick duration, TrackRole exclude,
                           bool is_weak_beat = false) const = 0;

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
   * @return Safe pitch within range, or desired if no safe pitch found
   */
  virtual uint8_t getSafePitch(uint8_t desired, Tick start, Tick duration, TrackRole track,
                               uint8_t low, uint8_t high) const = 0;

  /**
   * @brief Get the tick of the next chord change after the given tick.
   * @param after Position to search from
   * @return Tick of next chord change, or 0 if none found
   */
  virtual Tick getNextChordChangeTick(Tick after) const = 0;

  /// Clear all registered notes (useful for regeneration).
  virtual void clearNotes() = 0;

  /// Clear notes from a specific track only.
  virtual void clearNotesForTrack(TrackRole track) = 0;

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
   * @brief Register a secondary dominant chord at a specific tick range.
   *
   * Used when chord_track inserts a V/x chord to update the chord progression
   * tracker, ensuring other tracks (bass, etc.) see the correct chord.
   *
   * @param start Start tick of the secondary dominant
   * @param end End tick of the secondary dominant
   * @param degree Scale degree of the secondary dominant
   */
  virtual void registerSecondaryDominant(Tick start, Tick end, int8_t degree) = 0;

  /// C4 (middle C) - below this, stricter low-register rules apply.
  static constexpr uint8_t LOW_REGISTER_THRESHOLD = 60;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_I_HARMONY_CONTEXT_H
