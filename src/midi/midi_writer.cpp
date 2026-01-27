/**
 * @file midi_writer.cpp
 * @brief Implementation of SMF Type 1 and MIDI 2.0 file writer.
 */

#include "midi/midi_writer.h"

#include "core/harmony_context.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "core/timing_constants.h"
#include "midi/track_config.h"
#include "track/arpeggio.h"

#ifndef MIDISKETCH_WASM
#include "midi/midi2_writer.h"
#endif

#include <algorithm>
#include <cassert>
#include <fstream>

namespace midisketch {

// ============================================================================
// MIDI Metadata Length Limits
// ============================================================================
// MIDI meta events use a variable-length encoding, but track names and marker
// texts are typically limited to 255 bytes for compatibility with most software.
constexpr size_t kMaxMetaTextLength = 255;

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

// transposePitch moved to core/pitch_utils.h

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

void MidiWriter::writeTrack(const MidiTrack& track, const std::string& name, uint8_t channel,
                            uint8_t program, uint16_t bpm, Key key, bool is_first_track,
                            Tick mod_tick, int8_t mod_amount) {
  std::vector<uint8_t> track_data;

  // Defensive: BPM should be validated at buildSMF1() entry point
  if (bpm == 0) bpm = 120;

  // Track name (Meta event 0x03) - truncate if too long for MIDI meta text limit
  std::string track_name =
      name.size() > kMaxMetaTextLength ? name.substr(0, kMaxMetaTextLength) : name;
  track_data.push_back(0x00);
  track_data.push_back(0xFF);
  track_data.push_back(0x03);
  track_data.push_back(static_cast<uint8_t>(track_name.size()));
  for (char c : track_name) {
    track_data.push_back(static_cast<uint8_t>(c));
  }

  // Tempo (only in first track)
  if (is_first_track) {
    uint32_t microseconds_per_beat = kMicrosecondsPerMinute / bpm;
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

  // Sort events by time, with note-off before note-on at same time.
  // This ensures proper handling of overlapping notes with same pitch:
  // when a note ends and another starts at the same tick, the old note
  // is properly closed (note-off 0x80) before the new one starts (note-on 0x90).
  std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
    if (a.time != b.time) return a.time < b.time;
    // At same time: note-off (0x80) before note-on (0x90)
    return a.type < b.type;
  });

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

  // Defensive: BPM should be validated at buildSMF1() entry point
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
  uint32_t microseconds_per_beat = kMicrosecondsPerMinute / bpm;
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

    // Truncate marker text if too long for MIDI meta text limit
    std::string marker_text = marker.text.size() > kMaxMetaTextLength
                                  ? marker.text.substr(0, kMaxMetaTextLength)
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

void MidiWriter::build(const Song& song, Key key, Mood mood, const std::string& metadata,
                       MidiFormat format) {
#ifdef MIDISKETCH_WASM
  // WASM build only supports SMF1
  (void)format;
  buildSMF1(song, key, mood, metadata);
#else
  if (format == MidiFormat::SMF2) {
    buildSMF2(song, key, mood, metadata);
  } else {
    buildSMF1(song, key, mood, metadata);
  }
#endif
}

#ifndef MIDISKETCH_WASM
void MidiWriter::buildSMF2(const Song& song, Key key, Mood mood, const std::string& metadata) {
  if (!midi2_writer_) {
    midi2_writer_ = std::make_unique<Midi2Writer>();
  }
  // TODO: Pass mood to buildContainer when MIDI 2.0 supports instrument mapping
  (void)mood;
  midi2_writer_->buildContainer(song, key, metadata);
  data_ = midi2_writer_->toBytes();
}
#endif

void MidiWriter::buildSMF1(const Song& song, Key key, Mood mood, const std::string& metadata) {
  data_.clear();

  // Validate BPM once at entry point (downstream checks are defensive only)
  uint16_t bpm = song.bpm();
  if (bpm == 0) bpm = 120;  // Default BPM for safety

  // Get mood-specific program numbers
  const MoodProgramSet& progs = getMoodPrograms(mood);

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
  writeMarkerTrack(song.se(), bpm, metadata);

  Tick mod_tick = song.modulationTick();
  int8_t mod_amount = song.modulationAmount();

  if (!song.vocal().empty()) {
    writeTrack(song.vocal(), "Vocal", VOCAL_CH, progs.vocal, bpm, key, false, mod_tick, mod_amount);
  }

  if (!song.chord().empty()) {
    writeTrack(song.chord(), "Chord", CHORD_CH, progs.chord, bpm, key, false, mod_tick, mod_amount);
  }

  if (!song.bass().empty()) {
    writeTrack(song.bass(), "Bass", BASS_CH, progs.bass, bpm, key, false, mod_tick, mod_amount);
  }

  if (!song.motif().empty()) {
    writeTrack(song.motif(), "Motif", MOTIF_CH, progs.motif, bpm, key, false, mod_tick, mod_amount);
  }

  if (!song.arpeggio().empty()) {
    uint8_t arp_program = getArpeggioStyleForMood(mood).gm_program;
    writeTrack(song.arpeggio(), "Arpeggio", ARPEGGIO_CH, arp_program, bpm, key, false, mod_tick,
               mod_amount);
  }

  if (!song.aux().empty()) {
    writeTrack(song.aux(), "Aux", AUX_CH, progs.aux, bpm, key, false, mod_tick, mod_amount);
  }

  if (!song.drums().empty()) {
    writeTrack(song.drums(), "Drums", DRUMS_CH, DRUMS_PROG, bpm, key, false, 0,
               0);  // No modulation for drums
  }
}

std::vector<uint8_t> MidiWriter::toBytes() const { return data_; }

bool MidiWriter::writeToFile(const std::string& path) const {
  std::ofstream file(path, std::ios::binary);
  if (!file) return false;

  file.write(reinterpret_cast<const char*>(data_.data()),
             static_cast<std::streamsize>(data_.size()));
  return file.good();
}

void MidiWriter::buildVocalPreview(const Song& song, const IHarmonyContext& harmony, Key key) {
  data_.clear();

  // Create root bass track from chord changes
  MidiTrack root_bass;
  constexpr uint8_t BASS_OCTAVE = 36;  // C2 base
  constexpr uint8_t BASS_VELOCITY = 80;

  // Get total duration from song
  Tick total_ticks = song.arrangement().totalTicks();
  if (total_ticks == 0) {
    total_ticks = song.vocal().empty() ? 0
                                       : song.vocal().notes().back().start_tick +
                                             song.vocal().notes().back().duration;
  }

  // Generate root notes at each chord change
  Tick current_tick = 0;
  while (current_tick < total_ticks) {
    int8_t degree = harmony.getChordDegreeAt(current_tick);
    Tick next_change = harmony.getNextChordChangeTick(current_tick);
    if (next_change == 0 || next_change <= current_tick) {
      next_change = total_ticks;  // Last chord extends to end
    }

    // Calculate root pitch (C2 base + scale degree)
    int root_pc = SCALE[((degree % 7) + 7) % 7];
    uint8_t root_pitch = BASS_OCTAVE + static_cast<uint8_t>(root_pc);

    // Add whole note (or duration until next chord change)
    Tick duration = next_change - current_tick;
    root_bass.addNote(current_tick, duration, root_pitch, BASS_VELOCITY);

    current_tick = next_change;
  }

  // Count tracks: SE (tempo) + Vocal + Bass
  uint16_t num_tracks = 1;  // SE track for tempo
  if (!song.vocal().empty()) num_tracks++;
  if (!root_bass.empty()) num_tracks++;

  writeHeader(num_tracks, TICKS_PER_BEAT);

  // SE track (tempo only, no markers)
  MidiTrack empty_se;
  writeMarkerTrack(empty_se, song.bpm(), "");

  // Vocal track
  constexpr uint8_t VOCAL_CH = 0;
  constexpr uint8_t VOCAL_PROG = 0;  // Piano
  if (!song.vocal().empty()) {
    writeTrack(song.vocal(), "Vocal", VOCAL_CH, VOCAL_PROG, song.bpm(), key, false, 0, 0);
  }

  // Root bass track
  constexpr uint8_t BASS_CH = 2;
  constexpr uint8_t BASS_PROG = 33;  // Electric Bass
  if (!root_bass.empty()) {
    writeTrack(root_bass, "Bass", BASS_CH, BASS_PROG, song.bpm(), key, false, 0, 0);
  }
}

}  // namespace midisketch
