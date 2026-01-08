#include "core/midi_track.h"
#include <algorithm>

namespace midisketch {

void MidiTrack::addNote(Tick startTick, Tick length, uint8_t note,
                         uint8_t velocity) {
  notes_.push_back({startTick, length, note, velocity});
}

void MidiTrack::addText(Tick tick, const std::string& text) {
  textEvents_.push_back({tick, text});
}

void MidiTrack::transpose(int8_t semitones) {
  for (auto& note : notes_) {
    int new_pitch = note.note + semitones;
    note.note = static_cast<uint8_t>(std::clamp(new_pitch, 0, 127));
  }
}

void MidiTrack::scaleVelocity(float factor) {
  for (auto& note : notes_) {
    int new_vel = static_cast<int>(note.velocity * factor);
    note.velocity = static_cast<uint8_t>(std::clamp(new_vel, 1, 127));
  }
}

void MidiTrack::clampVelocity(uint8_t min_vel, uint8_t max_vel) {
  for (auto& note : notes_) {
    if (note.velocity < min_vel) note.velocity = min_vel;
    if (note.velocity > max_vel) note.velocity = max_vel;
  }
}

MidiTrack MidiTrack::slice(Tick fromTick, Tick toTick) const {
  MidiTrack result;
  for (const auto& note : notes_) {
    Tick noteEnd = note.start_tick + note.duration;
    if (note.start_tick >= fromTick && noteEnd <= toTick) {
      NoteEvent sliced = note;
      sliced.start_tick -= fromTick;  // Adjust to relative position
      result.notes_.push_back(sliced);
    }
  }
  for (const auto& text : textEvents_) {
    if (text.time >= fromTick && text.time < toTick) {
      TextEvent sliced = text;
      sliced.time -= fromTick;
      result.textEvents_.push_back(sliced);
    }
  }
  return result;
}

void MidiTrack::append(const MidiTrack& other, Tick offsetTick) {
  for (const auto& note : other.notes_) {
    NoteEvent shifted = note;
    shifted.start_tick += offsetTick;
    notes_.push_back(shifted);
  }
  for (const auto& text : other.textEvents_) {
    TextEvent shifted = text;
    shifted.time += offsetTick;
    textEvents_.push_back(shifted);
  }
}

void MidiTrack::clear() {
  notes_.clear();
  textEvents_.clear();
}

Tick MidiTrack::lastTick() const {
  Tick last = 0;
  for (const auto& note : notes_) {
    Tick noteEnd = note.start_tick + note.duration;
    if (noteEnd > last) last = noteEnd;
  }
  for (const auto& text : textEvents_) {
    if (text.time > last) last = text.time;
  }
  return last;
}

std::pair<uint8_t, uint8_t> MidiTrack::analyzeRange() const {
  if (notes_.empty()) {
    return {127, 0};  // Invalid range indicates empty track
  }

  uint8_t lowest = 127;
  uint8_t highest = 0;

  for (const auto& note : notes_) {
    if (note.note < lowest) lowest = note.note;
    if (note.note > highest) highest = note.note;
  }

  return {lowest, highest};
}

std::vector<MidiEvent> MidiTrack::toMidiEvents(uint8_t channel) const {
  std::vector<MidiEvent> events;

  // Convert NoteEvents to note-on/off MidiEvents
  for (const auto& note : notes_) {
    // Note on: status = 0x90 | channel
    events.push_back({note.start_tick, static_cast<uint8_t>(0x90 | channel),
                      note.note, note.velocity});

    // Note off: status = 0x80 | channel
    events.push_back({note.start_tick + note.duration,
                      static_cast<uint8_t>(0x80 | channel), note.note, 0});
  }

  // Sort by tick time
  std::sort(events.begin(), events.end(),
            [](const MidiEvent& a, const MidiEvent& b) {
              return a.tick < b.tick;
            });

  return events;
}

}  // namespace midisketch
