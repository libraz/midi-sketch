/**
 * @file midi_track.cpp
 * @brief Implementation of MidiTrack operations.
 */

#include "core/midi_track.h"

#include <algorithm>

#include "core/velocity_helper.h"

namespace midisketch {

MidiTrack::MidiTrack() { notes_.reserve(kInitialNoteCapacity); }

void MidiTrack::addNote(const NoteEvent& event) { notes_.push_back(event); }

void MidiTrack::addText(Tick tick, const std::string& text) { textEvents_.push_back({tick, text}); }

void MidiTrack::addCC(Tick tick, uint8_t cc_number, uint8_t value) {
  cc_events_.push_back({tick, cc_number, value});
}

void MidiTrack::addPitchBend(Tick tick, int16_t value) {
  // Clamp value to valid range
  int16_t clamped = std::clamp(value, static_cast<int16_t>(-8192), static_cast<int16_t>(8191));
  pitch_bend_events_.push_back({tick, clamped});
}

void MidiTrack::clearPitchBend() { pitch_bend_events_.clear(); }

void MidiTrack::transpose(int8_t semitones) {
  for (auto& note : notes_) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
    uint8_t old_pitch = note.note;
#endif
    int new_pitch = note.note + semitones;
    note.note = static_cast<uint8_t>(std::clamp(new_pitch, 0, 127));
#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (old_pitch != note.note) {
      note.prov_original_pitch = old_pitch;
      note.addTransformStep(TransformStepType::RangeClamp, old_pitch, note.note, 0, 0);
    }
#endif
  }
}

void MidiTrack::scaleVelocity(float factor) {
  for (auto& note : notes_) {
    int new_vel = static_cast<int>(note.velocity * factor);
    note.velocity = vel::clamp(new_vel);
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
#ifdef MIDISKETCH_NOTE_PROVENANCE
      if (sliced.prov_lookup_tick >= fromTick) {
        sliced.prov_lookup_tick -= fromTick;
      }
#endif
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
  for (const auto& cc_evt : cc_events_) {
    if (cc_evt.tick >= fromTick && cc_evt.tick < toTick) {
      CCEvent sliced = cc_evt;
      sliced.tick -= fromTick;
      result.cc_events_.push_back(sliced);
    }
  }
  for (const auto& pb_evt : pitch_bend_events_) {
    if (pb_evt.tick >= fromTick && pb_evt.tick < toTick) {
      PitchBendEvent sliced = pb_evt;
      sliced.tick -= fromTick;
      result.pitch_bend_events_.push_back(sliced);
    }
  }
  return result;
}

void MidiTrack::append(const MidiTrack& other, Tick offsetTick) {
  for (const auto& note : other.notes_) {
    NoteEvent shifted = note;
    shifted.start_tick += offsetTick;
#ifdef MIDISKETCH_NOTE_PROVENANCE
    shifted.prov_lookup_tick += offsetTick;
#endif
    notes_.push_back(shifted);
  }
  for (const auto& text : other.textEvents_) {
    TextEvent shifted = text;
    shifted.time += offsetTick;
    textEvents_.push_back(shifted);
  }
  for (const auto& cc_evt : other.cc_events_) {
    CCEvent shifted = cc_evt;
    shifted.tick += offsetTick;
    cc_events_.push_back(shifted);
  }
  for (const auto& pb_evt : other.pitch_bend_events_) {
    PitchBendEvent shifted = pb_evt;
    shifted.tick += offsetTick;
    pitch_bend_events_.push_back(shifted);
  }
}

void MidiTrack::clear() {
  notes_.clear();
  textEvents_.clear();
  cc_events_.clear();
  pitch_bend_events_.clear();
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
  for (const auto& cc_evt : cc_events_) {
    if (cc_evt.tick > last) last = cc_evt.tick;
  }
  for (const auto& pb_evt : pitch_bend_events_) {
    if (pb_evt.tick > last) last = pb_evt.tick;
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

  struct MergedNote {
    Tick start;
    Tick end;
    uint8_t pitch;
    uint8_t velocity;
  };
  std::vector<MergedNote> merged_notes;
  merged_notes.reserve(notes_.size());
  for (const auto& note : notes_) {
    merged_notes.push_back(
        {note.start_tick, note.start_tick + note.duration, note.note, note.velocity});
  }

  std::stable_sort(merged_notes.begin(), merged_notes.end(),
                   [](const MergedNote& a, const MergedNote& b) {
                     if (a.pitch != b.pitch) return a.pitch < b.pitch;
                     return a.start < b.start;
                   });

  std::vector<MergedNote> normalized_notes;
  normalized_notes.reserve(merged_notes.size());
  for (const auto& note : merged_notes) {
    if (!normalized_notes.empty() && normalized_notes.back().pitch == note.pitch &&
        note.start < normalized_notes.back().end) {
      normalized_notes.back().end = std::max(normalized_notes.back().end, note.end);
      normalized_notes.back().velocity = std::max(normalized_notes.back().velocity, note.velocity);
    } else {
      normalized_notes.push_back(note);
    }
  }

  // Convert normalized NoteEvents to note-on/off MidiEvents. MIDI 1.0 cannot
  // represent overlapping notes of the same channel and pitch: either note-off
  // would terminate both voices. Their union is the audible, stable result.
  for (const auto& note : normalized_notes) {
    // Note on: status = 0x90 | channel
    events.push_back({note.start, static_cast<uint8_t>(0x90 | channel), note.pitch, note.velocity});

    // Note off: status = 0x80 | channel
    events.push_back({note.end, static_cast<uint8_t>(0x80 | channel), note.pitch, 0});
  }

  // Sort by tick time, closing notes before starting replacements at the same tick.
  // stable_sort keeps the original order for same-time, same-type events deterministic.
  std::stable_sort(events.begin(), events.end(), [](const MidiEvent& a, const MidiEvent& b) {
    if (a.tick != b.tick) return a.tick < b.tick;
    const bool a_is_note_off = (a.status & 0xF0) == 0x80;
    const bool b_is_note_off = (b.status & 0xF0) == 0x80;
    return a_is_note_off && !b_is_note_off;
  });

  return events;
}

}  // namespace midisketch
