#include "midi/midi_writer.h"

#ifndef MIDISKETCH_WASM
#include "midi/midi2_writer.h"
#endif

#include <algorithm>
#include <fstream>

namespace midisketch {

MidiWriter::MidiWriter() {}

MidiWriter::~MidiWriter() = default;

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

void MidiWriter::writeTrack(const MidiTrack& track, const std::string& name,
                            uint8_t channel, uint8_t program, uint16_t bpm,
                            Key key, bool is_first_track, Tick mod_tick,
                            int8_t mod_amount) {
  std::vector<uint8_t> track_data;

  // Validate BPM to prevent division by zero
  if (bpm == 0) bpm = 120;

  // Track name (Meta event 0x03) - truncate to 255 bytes max
  std::string track_name = name.size() > 255 ? name.substr(0, 255) : name;
  track_data.push_back(0x00);
  track_data.push_back(0xFF);
  track_data.push_back(0x03);
  track_data.push_back(static_cast<uint8_t>(track_name.size()));
  for (char c : track_name) {
    track_data.push_back(static_cast<uint8_t>(c));
  }

  // Tempo (only in first track)
  if (is_first_track) {
    uint32_t microseconds_per_beat = 60000000 / bpm;
    track_data.push_back(0x00);
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

  // Program change (skip for drums channel 9)
  if (channel != 9) {
    track_data.push_back(0x00);
    track_data.push_back(0xC0 | channel);
    track_data.push_back(program);
  }

  // Convert NoteEvents to note on/off events
  struct Event {
    Tick time;
    uint8_t type;  // 0x90 = note on, 0x80 = note off
    uint8_t pitch;
    uint8_t velocity;
  };
  std::vector<Event> events;
  events.reserve(track.notes().size() * 2);  // 2 events per note (on + off)

  for (const auto& note : track.notes()) {
    uint8_t pitch = note.note;
    if (channel != 9) {  // Not drums
      pitch = transposePitch(pitch, key);
      // Apply modulation if note starts after modulation point
      if (mod_tick > 0 && note.start_tick >= mod_tick && mod_amount != 0) {
        int new_pitch = pitch + mod_amount;
        pitch = static_cast<uint8_t>(std::clamp(new_pitch, 0, 127));
      }
    }
    events.push_back({note.start_tick, 0x90, pitch, note.velocity});
    events.push_back({note.start_tick + note.duration, 0x80, pitch, 0});
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
    track_data.push_back((evt.type & 0xF0) | channel);
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

  data_.insert(data_.end(), track_data.begin(), track_data.end());
}

void MidiWriter::writeMarkerTrack(const MidiTrack& track, uint16_t bpm,
                                   const std::string& metadata) {
  std::vector<uint8_t> track_data;

  // Validate BPM to prevent division by zero
  if (bpm == 0) bpm = 120;

  // Track name
  track_data.push_back(0x00);
  track_data.push_back(0xFF);
  track_data.push_back(0x03);
  track_data.push_back(2);
  track_data.push_back('S');
  track_data.push_back('E');

  // Generation metadata as Text Event (0xFF 0x01)
  // Prefix with "MIDISKETCH:" for easy identification
  if (!metadata.empty()) {
    std::string meta_text = "MIDISKETCH:" + metadata;
    track_data.push_back(0x00);  // Delta time
    track_data.push_back(0xFF);
    track_data.push_back(0x01);  // Text Event
    writeVariableLength(track_data, static_cast<uint32_t>(meta_text.size()));
    for (char c : meta_text) {
      track_data.push_back(static_cast<uint8_t>(c));
    }
  }

  // Tempo
  uint32_t microseconds_per_beat = 60000000 / bpm;
  track_data.push_back(0x00);
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
  track_data.push_back(0x04);
  track_data.push_back(0x02);
  track_data.push_back(0x18);
  track_data.push_back(0x08);

  // Write marker events (Meta event 0x06)
  Tick prev_time = 0;
  for (const auto& marker : track.textEvents()) {
    Tick delta = marker.time - prev_time;
    prev_time = marker.time;

    // Truncate marker text to 255 bytes max
    std::string marker_text = marker.text.size() > 255
                                  ? marker.text.substr(0, 255)
                                  : marker.text;

    writeVariableLength(track_data, delta);
    track_data.push_back(0xFF);
    track_data.push_back(0x06);
    track_data.push_back(static_cast<uint8_t>(marker_text.size()));
    for (char c : marker_text) {
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

  data_.insert(data_.end(), track_data.begin(), track_data.end());
}

void MidiWriter::build(const Song& song, Key key, const std::string& metadata,
                        MidiFormat format) {
#ifdef MIDISKETCH_WASM
  // WASM build only supports SMF1
  (void)format;
  buildSMF1(song, key, metadata);
#else
  if (format == MidiFormat::SMF2) {
    buildSMF2(song, key, metadata);
  } else {
    buildSMF1(song, key, metadata);
  }
#endif
}

#ifndef MIDISKETCH_WASM
void MidiWriter::buildSMF2(const Song& song, Key key,
                            const std::string& metadata) {
  if (!midi2_writer_) {
    midi2_writer_ = std::make_unique<Midi2Writer>();
  }
  midi2_writer_->buildContainer(song, key, metadata);
  data_ = midi2_writer_->toBytes();
}
#endif

void MidiWriter::buildSMF1(const Song& song, Key key,
                            const std::string& metadata) {
  data_.clear();

  // Count non-empty tracks (SE track always included)
  uint16_t num_tracks = 1;  // SE track
  if (!song.vocal().empty()) num_tracks++;
  if (!song.chord().empty()) num_tracks++;
  if (!song.bass().empty()) num_tracks++;
  if (!song.drums().empty()) num_tracks++;
  if (!song.motif().empty()) num_tracks++;
  if (!song.arpeggio().empty()) num_tracks++;
  if (!song.aux().empty()) num_tracks++;

  writeHeader(num_tracks, TICKS_PER_BEAT);

  // SE track first (contains tempo, markers, and metadata)
  writeMarkerTrack(song.se(), song.bpm(), metadata);

  Tick mod_tick = song.modulationTick();
  int8_t mod_amount = song.modulationAmount();

  // Channel and program assignments
  constexpr uint8_t VOCAL_CH = 0;
  constexpr uint8_t VOCAL_PROG = 0;    // Piano
  constexpr uint8_t CHORD_CH = 1;
  constexpr uint8_t CHORD_PROG = 4;    // Electric Piano
  constexpr uint8_t BASS_CH = 2;
  constexpr uint8_t BASS_PROG = 33;    // Electric Bass
  constexpr uint8_t MOTIF_CH = 3;
  constexpr uint8_t MOTIF_PROG = 81;   // Synth Lead
  constexpr uint8_t ARPEGGIO_CH = 4;
  constexpr uint8_t ARPEGGIO_PROG = 81;  // Saw Lead (Synth)
  constexpr uint8_t AUX_CH = 5;
  constexpr uint8_t AUX_PROG = 89;       // Pad 2 - Warm
  constexpr uint8_t DRUMS_CH = 9;
  constexpr uint8_t DRUMS_PROG = 0;

  if (!song.vocal().empty()) {
    writeTrack(song.vocal(), "Vocal", VOCAL_CH, VOCAL_PROG, song.bpm(), key,
               false, mod_tick, mod_amount);
  }

  if (!song.chord().empty()) {
    writeTrack(song.chord(), "Chord", CHORD_CH, CHORD_PROG, song.bpm(), key,
               false, mod_tick, mod_amount);
  }

  if (!song.bass().empty()) {
    writeTrack(song.bass(), "Bass", BASS_CH, BASS_PROG, song.bpm(), key,
               false, mod_tick, mod_amount);
  }

  if (!song.motif().empty()) {
    writeTrack(song.motif(), "Motif", MOTIF_CH, MOTIF_PROG, song.bpm(), key,
               false, mod_tick, mod_amount);
  }

  if (!song.arpeggio().empty()) {
    writeTrack(song.arpeggio(), "Arpeggio", ARPEGGIO_CH, ARPEGGIO_PROG,
               song.bpm(), key, false, mod_tick, mod_amount);
  }

  if (!song.aux().empty()) {
    writeTrack(song.aux(), "Aux", AUX_CH, AUX_PROG,
               song.bpm(), key, false, mod_tick, mod_amount);
  }

  if (!song.drums().empty()) {
    writeTrack(song.drums(), "Drums", DRUMS_CH, DRUMS_PROG, song.bpm(), key,
               false, 0, 0);  // No modulation for drums
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
