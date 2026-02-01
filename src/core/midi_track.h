/**
 * @file midi_track.h
 * @brief NoteEvent-based track container for MIDI generation.
 */

#ifndef MIDISKETCH_CORE_MIDI_TRACK_H
#define MIDISKETCH_CORE_MIDI_TRACK_H

#include <string>
#include <utility>
#include <vector>

#include "core/types.h"

namespace midisketch {

/// @brief NoteEvent-based track container for MIDI generation.
///
/// All editing happens at NoteEvent level; converts to MidiEvent for output.
class MidiTrack {
 public:
  MidiTrack() = default;

  /// @name Generation Operations
  /// @{

  /// @brief Add a note.
  /// @param event NoteEvent created via NoteFactory or NoteEventBuilder
  void addNote(const NoteEvent& event);

  void addText(Tick tick, const std::string& text);

  /// @brief Add a MIDI Control Change event.
  /// @param tick Position in ticks
  /// @param cc_number CC number (0-127)
  /// @param value CC value (0-127)
  void addCC(Tick tick, uint8_t cc_number, uint8_t value);

  /// @brief Add a MIDI Pitch Bend event.
  /// @param tick Position in ticks
  /// @param value Bend value (-8192 to +8191, 0=center)
  void addPitchBend(Tick tick, int16_t value);

  /// @brief Clear all pitch bend events from the track.
  void clearPitchBend();
  /// @}

  /// @name Editing Operations
  /// @{
  void transpose(int8_t semitones);
  void scaleVelocity(float factor);
  void clampVelocity(uint8_t min_vel, uint8_t max_vel);
  /// @}

  /// @name Structure Operations
  /// @{
  MidiTrack slice(Tick fromTick, Tick toTick) const;
  void append(const MidiTrack& other, Tick offsetTick);
  void clear();
  /// @}

  /// @name Output Conversion
  /// @{
  std::vector<MidiEvent> toMidiEvents(uint8_t channel) const;
  /// @}

  /// @name Accessors
  /// @{
  const std::vector<NoteEvent>& notes() const { return notes_; }
  std::vector<NoteEvent>& notes() { return notes_; }
  const std::vector<TextEvent>& textEvents() const { return textEvents_; }
  const std::vector<CCEvent>& ccEvents() const { return cc_events_; }
  std::vector<CCEvent>& ccEvents() { return cc_events_; }
  const std::vector<PitchBendEvent>& pitchBendEvents() const { return pitch_bend_events_; }
  std::vector<PitchBendEvent>& pitchBendEvents() { return pitch_bend_events_; }
  bool empty() const {
    return notes_.empty() && textEvents_.empty() && cc_events_.empty() && pitch_bend_events_.empty();
  }
  size_t noteCount() const { return notes_.size(); }
  Tick lastTick() const;
  /// @}

  /// @brief Analyze pitch range of this track.
  /// @return Pair of (lowest_note, highest_note). Returns (127, 0) if empty.
  std::pair<uint8_t, uint8_t> analyzeRange() const;

 private:
  std::vector<NoteEvent> notes_;
  std::vector<TextEvent> textEvents_;
  std::vector<CCEvent> cc_events_;
  std::vector<PitchBendEvent> pitch_bend_events_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_MIDI_TRACK_H
