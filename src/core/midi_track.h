#ifndef MIDISKETCH_CORE_MIDI_TRACK_H
#define MIDISKETCH_CORE_MIDI_TRACK_H

#include "core/types.h"
#include <string>
#include <utility>
#include <vector>

namespace midisketch {

// Core class for track-level operations.
// All editing happens at NoteEvent level; converts to MidiEvent for output.
class MidiTrack {
 public:
  MidiTrack() = default;

  // Generation operations
  void addNote(Tick startTick, Tick length, uint8_t note, uint8_t velocity);
  void addText(Tick tick, const std::string& text);

  // Editing operations
  void transpose(int8_t semitones);
  void scaleVelocity(float factor);
  void clampVelocity(uint8_t min_vel, uint8_t max_vel);

  // Structure operations
  MidiTrack slice(Tick fromTick, Tick toTick) const;
  void append(const MidiTrack& other, Tick offsetTick);
  void clear();

  // Output conversion
  std::vector<MidiEvent> toMidiEvents(uint8_t channel) const;

  // Accessors
  const std::vector<NoteEvent>& notes() const { return notes_; }
  std::vector<NoteEvent>& notes() { return notes_; }  // Non-const for modification
  const std::vector<TextEvent>& textEvents() const { return textEvents_; }
  bool empty() const { return notes_.empty() && textEvents_.empty(); }
  size_t noteCount() const { return notes_.size(); }

  // Get the last tick in this track
  Tick lastTick() const;

  // Analyze pitch range of this track.
  // @returns Pair of (lowest_note, highest_note). Returns (127, 0) if empty.
  std::pair<uint8_t, uint8_t> analyzeRange() const;

 private:
  std::vector<NoteEvent> notes_;
  std::vector<TextEvent> textEvents_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_MIDI_TRACK_H
