#include "midi/midi_writer.h"
#include <algorithm>
#include <fstream>

namespace midisketch {

MidiWriter::MidiWriter() {}

void MidiWriter::writeVariableLength(std::vector<uint8_t>& buf, uint32_t value) {
  if (value == 0) {
    buf.push_back(0);
    return;
  }

  std::vector<uint8_t> temp;
  while (value > 0) {
    temp.push_back(value & 0x7F);
    value >>= 7;
  }

  for (size_t i = temp.size(); i > 0; --i) {
    uint8_t b = temp[i - 1];
    if (i > 1) b |= 0x80;
    buf.push_back(b);
  }
}

uint8_t MidiWriter::transposePitch(uint8_t pitch, Key key) {
  int offset = static_cast<int>(key);
  int result = pitch + offset;
  return static_cast<uint8_t>(std::clamp(result, 0, 127));
}

void MidiWriter::writeHeader(uint16_t num_tracks, uint16_t division) {
  // MThd
  data_.push_back('M');
  data_.push_back('T');
  data_.push_back('h');
  data_.push_back('d');

  // Header length = 6
  data_.push_back(0);
  data_.push_back(0);
  data_.push_back(0);
  data_.push_back(6);

  // Format = 1
  data_.push_back(0);
  data_.push_back(1);

  // Number of tracks
  data_.push_back((num_tracks >> 8) & 0xFF);
  data_.push_back(num_tracks & 0xFF);

  // Division (ticks per quarter note)
  data_.push_back((division >> 8) & 0xFF);
  data_.push_back(division & 0xFF);
}

void MidiWriter::writeTrack(const TrackData& track, const std::string& name,
                            uint16_t bpm, Key key, bool is_first_track,
                            Tick mod_tick, int8_t mod_amount) {
  std::vector<uint8_t> track_data;

  // Track name (Meta event 0x03)
  track_data.push_back(0x00);  // Delta time
  track_data.push_back(0xFF);
  track_data.push_back(0x03);
  track_data.push_back(static_cast<uint8_t>(name.size()));
  for (char c : name) {
    track_data.push_back(static_cast<uint8_t>(c));
  }

  // Tempo (only in first track)
  if (is_first_track) {
    uint32_t microseconds_per_beat = 60000000 / bpm;
    track_data.push_back(0x00);  // Delta time
    track_data.push_back(0xFF);
    track_data.push_back(0x51);
    track_data.push_back(0x03);
    track_data.push_back((microseconds_per_beat >> 16) & 0xFF);
    track_data.push_back((microseconds_per_beat >> 8) & 0xFF);
    track_data.push_back(microseconds_per_beat & 0xFF);

    // Time signature 4/4
    track_data.push_back(0x00);
    track_data.push_back(0xFF);
    track_data.push_back(0x58);
    track_data.push_back(0x04);
    track_data.push_back(0x04);  // Numerator
    track_data.push_back(0x02);  // Denominator (power of 2)
    track_data.push_back(0x18);  // Clocks per metronome click
    track_data.push_back(0x08);  // 32nd notes per quarter
  }

  // Program change
  if (track.channel != 9) {  // Skip for drums
    track_data.push_back(0x00);  // Delta time
    track_data.push_back(0xC0 | track.channel);
    track_data.push_back(track.program);
  }

  // Sort notes by start time
  std::vector<Note> sorted_notes = track.notes;
  std::sort(sorted_notes.begin(), sorted_notes.end(),
            [](const Note& a, const Note& b) { return a.start < b.start; });

  // Build note on/off events
  struct Event {
    Tick time;
    uint8_t type;  // 0x90 = note on, 0x80 = note off
    uint8_t pitch;
    uint8_t velocity;
  };
  std::vector<Event> events;

  for (const auto& note : sorted_notes) {
    uint8_t pitch = note.pitch;
    if (track.channel != 9) {  // Not drums
      pitch = transposePitch(pitch, key);
      // Apply modulation if note starts after modulation point
      if (mod_tick > 0 && note.start >= mod_tick && mod_amount != 0) {
        int new_pitch = pitch + mod_amount;
        pitch = static_cast<uint8_t>(std::clamp(new_pitch, 0, 127));
      }
    }
    events.push_back({note.start, 0x90, pitch, note.velocity});
    events.push_back({note.start + note.duration, 0x80, pitch, 0});
  }

  // Sort events by time
  std::sort(events.begin(), events.end(),
            [](const Event& a, const Event& b) { return a.time < b.time; });

  // Write events with delta times
  Tick prev_time = 0;
  for (const auto& evt : events) {
    Tick delta = evt.time - prev_time;
    prev_time = evt.time;

    writeVariableLength(track_data, delta);
    track_data.push_back((evt.type & 0xF0) | track.channel);
    track_data.push_back(evt.pitch);
    track_data.push_back(evt.type == 0x90 ? evt.velocity : 0);
  }

  // End of track
  track_data.push_back(0x00);
  track_data.push_back(0xFF);
  track_data.push_back(0x2F);
  track_data.push_back(0x00);

  // Write track header
  data_.push_back('M');
  data_.push_back('T');
  data_.push_back('r');
  data_.push_back('k');

  uint32_t track_length = static_cast<uint32_t>(track_data.size());
  data_.push_back((track_length >> 24) & 0xFF);
  data_.push_back((track_length >> 16) & 0xFF);
  data_.push_back((track_length >> 8) & 0xFF);
  data_.push_back(track_length & 0xFF);

  // Append track data
  data_.insert(data_.end(), track_data.begin(), track_data.end());
}

void MidiWriter::writeMarkerTrack(const std::vector<TextEvent>& markers,
                                   uint16_t bpm) {
  std::vector<uint8_t> track_data;

  // Track name (Meta event 0x03)
  track_data.push_back(0x00);  // Delta time
  track_data.push_back(0xFF);
  track_data.push_back(0x03);
  track_data.push_back(2);
  track_data.push_back('S');
  track_data.push_back('E');

  // Tempo
  uint32_t microseconds_per_beat = 60000000 / bpm;
  track_data.push_back(0x00);  // Delta time
  track_data.push_back(0xFF);
  track_data.push_back(0x51);
  track_data.push_back(0x03);
  track_data.push_back((microseconds_per_beat >> 16) & 0xFF);
  track_data.push_back((microseconds_per_beat >> 8) & 0xFF);
  track_data.push_back(microseconds_per_beat & 0xFF);

  // Time signature 4/4
  track_data.push_back(0x00);
  track_data.push_back(0xFF);
  track_data.push_back(0x58);
  track_data.push_back(0x04);
  track_data.push_back(0x04);  // Numerator
  track_data.push_back(0x02);  // Denominator (power of 2)
  track_data.push_back(0x18);  // Clocks per metronome click
  track_data.push_back(0x08);  // 32nd notes per quarter

  // Write marker events (Meta event 0x06)
  Tick prev_time = 0;
  for (const auto& marker : markers) {
    Tick delta = marker.time - prev_time;
    prev_time = marker.time;

    writeVariableLength(track_data, delta);
    track_data.push_back(0xFF);
    track_data.push_back(0x06);  // Marker event
    track_data.push_back(static_cast<uint8_t>(marker.text.size()));
    for (char c : marker.text) {
      track_data.push_back(static_cast<uint8_t>(c));
    }
  }

  // End of track
  track_data.push_back(0x00);
  track_data.push_back(0xFF);
  track_data.push_back(0x2F);
  track_data.push_back(0x00);

  // Write track header
  data_.push_back('M');
  data_.push_back('T');
  data_.push_back('r');
  data_.push_back('k');

  uint32_t track_length = static_cast<uint32_t>(track_data.size());
  data_.push_back((track_length >> 24) & 0xFF);
  data_.push_back((track_length >> 16) & 0xFF);
  data_.push_back((track_length >> 8) & 0xFF);
  data_.push_back(track_length & 0xFF);

  // Append track data
  data_.insert(data_.end(), track_data.begin(), track_data.end());
}

void MidiWriter::build(const GenerationResult& result, Key key) {
  data_.clear();

  // Count non-empty tracks (SE/marker track always included)
  uint16_t num_tracks = 1;  // SE track (marker track)
  if (!result.vocal.notes.empty()) num_tracks++;
  if (!result.chord.notes.empty()) num_tracks++;
  if (!result.bass.notes.empty()) num_tracks++;
  if (!result.drums.notes.empty()) num_tracks++;

  writeHeader(num_tracks, TICKS_PER_BEAT);

  // SE track first (contains tempo and markers)
  writeMarkerTrack(result.markers, result.bpm);

  Tick mod_tick = result.modulation_tick;
  int8_t mod_amount = result.modulation_amount;

  if (!result.vocal.notes.empty()) {
    writeTrack(result.vocal, "Vocal", result.bpm, key, false, mod_tick, mod_amount);
  }

  if (!result.chord.notes.empty()) {
    writeTrack(result.chord, "Chord", result.bpm, key, false, mod_tick, mod_amount);
  }

  if (!result.bass.notes.empty()) {
    writeTrack(result.bass, "Bass", result.bpm, key, false, mod_tick, mod_amount);
  }

  if (!result.drums.notes.empty()) {
    writeTrack(result.drums, "Drums", result.bpm, key, false, 0, 0);  // No modulation for drums
  }
}

std::vector<uint8_t> MidiWriter::toBytes() const {
  return data_;
}

bool MidiWriter::writeToFile(const std::string& path) const {
  std::ofstream file(path, std::ios::binary);
  if (!file) return false;

  file.write(reinterpret_cast<const char*>(data_.data()),
             static_cast<std::streamsize>(data_.size()));
  return file.good();
}

}  // namespace midisketch
