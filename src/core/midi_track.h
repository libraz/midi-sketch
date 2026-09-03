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

/// @brief MIDI channel reserved for percussion by General MIDI.
constexpr uint8_t kPercussionChannel = 9;

/// @brief True when @p channel carries drum-kit indices rather than pitches.
constexpr bool isPercussionChannel(uint8_t channel) { return channel == kPercussionChannel; }

/// @brief A note reduced to the start/end pair a MIDI channel actually carries.
struct SerializedNote {
  Tick start;
  Tick end;
  uint8_t pitch;
  uint8_t velocity;
};

/**
 * @brief Resolve same-pitch overlaps into a sequence one MIDI channel can carry.
 *
 * A MIDI 1.0 channel has a single voice per pitch: one note-off silences every
 * copy that is sounding. Two kinds of source material need opposite resolutions,
 * and this is the only place either is implemented.
 *
 * - Sustained material (@p percussive false): overlapping copies of a pitch are
 *   one sounding voice, so they collapse into their union (latest end, loudest
 *   velocity). Re-striking would not be audible anyway.
 * - Percussive material (@p percussive true): every note is a discrete strike
 *   whose nominal length only models decay. The earlier strike is truncated at
 *   the next onset so the later one keeps its own note-on. Collapsing these
 *   would delete hi-hat subdivisions and ghost snares from the written groove.
 *
 * Notes sharing a pitch and a start tick cannot be told apart on the wire, so
 * they collapse in both modes.
 *
 * @param notes Notes to resolve; pitches must already be in output space
 * @param percussive Whether the destination channel carries discrete strikes
 * @return Resolved notes ordered by pitch, then by start tick
 */
std::vector<SerializedNote> resolveSamePitchOverlaps(std::vector<SerializedNote> notes,
                                                     bool percussive);

/// @brief NoteEvent-based track container for MIDI generation.
///
/// All editing happens at NoteEvent level; converts to MidiEvent for output.
class MidiTrack {
 public:
  /// Typical generated track size; reserving this avoids early reallocation.
  static constexpr size_t kInitialNoteCapacity = 64;

  MidiTrack();

  /// @name Generation Operations
  /// @{

  /// @brief Add a note.
  /// @param event NoteEvent created via note_creator or NoteEventBuilder
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
    return notes_.empty() && textEvents_.empty() && cc_events_.empty() &&
           pitch_bend_events_.empty();
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
