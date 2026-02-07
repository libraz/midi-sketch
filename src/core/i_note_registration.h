/**
 * @file i_note_registration.h
 * @brief Interface for registering notes with the harmony system.
 *
 * Extracted from IHarmonyContext to allow consumers that only need
 * to register notes to depend on a narrower interface.
 */

#ifndef MIDISKETCH_CORE_I_NOTE_REGISTRATION_H
#define MIDISKETCH_CORE_I_NOTE_REGISTRATION_H

#include "core/basic_types.h"

namespace midisketch {

class MidiTrack;

/**
 * @brief Interface for note registration.
 *
 * Provides methods to register notes from tracks for collision detection.
 * Consumers that only need to register notes (not detect collisions)
 * should depend on this interface.
 */
class INoteRegistration {
 public:
  virtual ~INoteRegistration() = default;

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

  /// Clear all registered notes (useful for regeneration).
  virtual void clearNotes() = 0;

  /// Clear notes from a specific track only.
  virtual void clearNotesForTrack(TrackRole track) = 0;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_I_NOTE_REGISTRATION_H
