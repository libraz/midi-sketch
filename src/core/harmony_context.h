/**
 * @file harmony_context.h
 * @brief Harmonic context management for inter-track coordination.
 */

#ifndef MIDISKETCH_CORE_HARMONY_CONTEXT_H
#define MIDISKETCH_CORE_HARMONY_CONTEXT_H

#include "core/chord_utils.h"
#include "core/pitch_utils.h"
#include "core/types.h"
#include <vector>

namespace midisketch {

class Arrangement;
class MidiTrack;
struct ChordProgression;

/**
 * @brief Manages harmonic information for coordinated track generation.
 *
 * Provides chord-tone lookup and collision detection (minor 2nd, major 7th).
 * Low register (below C4) uses stricter collision thresholds.
 */
class HarmonyContext {
 public:
  HarmonyContext() = default;

  /**
   * @brief Initialize with arrangement and chord progression.
   * @param arrangement The song arrangement (sections and timing)
   * @param progression Chord progression to use
   * @param mood Mood affects harmonic rhythm
   */
  void initialize(const Arrangement& arrangement,
                  const ChordProgression& progression,
                  Mood mood);

  /**
   * @brief Get chord degree at a specific tick.
   * @param tick Position in ticks
   * @return Scale degree (0=I, 1=ii, 2=iii, 3=IV, 4=V, 5=vi, 6=vii)
   */
  int8_t getChordDegreeAt(Tick tick) const;

  /**
   * @brief Get chord tones as pitch classes at a specific tick.
   * @param tick Position in ticks
   * @return Vector of pitch classes (0-11) that are chord tones
   */
  std::vector<int> getChordTonesAt(Tick tick) const;

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
   * @return true if pitch doesn't clash with other tracks
   */
  bool isPitchSafe(uint8_t pitch, Tick start, Tick duration, TrackRole exclude) const;

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
  uint8_t getSafePitch(uint8_t desired, Tick start, Tick duration,
                       TrackRole track, uint8_t low, uint8_t high) const;

  /**
   * @brief Get the tick of the next chord change after the given tick.
   * @param after Position to search from
   * @return Tick of next chord change, or 0 if none found
   */
  Tick getNextChordChangeTick(Tick after) const;

  /// Clear all registered notes (useful for regeneration).
  void clearNotes();

  /// Clear notes from a specific track only.
  void clearNotesForTrack(TrackRole track);

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
  bool hasBassCollision(uint8_t pitch, Tick start, Tick duration,
                        int threshold = 3) const;

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

  /// C4 (middle C) - below this, stricter low-register rules apply.
  static constexpr uint8_t LOW_REGISTER_THRESHOLD = 60;

 private:
  // Chord information for a tick range.
  struct ChordInfo {
    Tick start;
    Tick end;
    int8_t degree;
  };

  // Registered note from a track.
  struct RegisteredNote {
    Tick start;
    Tick end;
    uint8_t pitch;
    TrackRole track;
  };

  std::vector<ChordInfo> chords_;
  std::vector<RegisteredNote> notes_;

  // Helper: check if two pitch classes create a dissonant interval.
  static bool isDissonantInterval(int pc1, int pc2);

  // Helper: get pitch classes for chord tones of a degree.
  static std::vector<int> getChordTonePitchClasses(int8_t degree);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_HARMONY_CONTEXT_H
