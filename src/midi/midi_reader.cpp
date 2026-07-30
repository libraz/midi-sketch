/**
 * @file midi_reader.cpp
 * @brief Implementation of MIDI file parser.
 */

#include "midi/midi_reader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>

#include "core/note_timeline_utils.h"
#include "core/timing_constants.h"
#include "midi/byte_order.h"

namespace midisketch {

namespace {

// Case-insensitive string compare
bool iequals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

}  // namespace

// Static format detection methods

DetectedMidiFormat MidiReader::detectFormat(const uint8_t* data, size_t size) {
  if (size >= 16) {
    // Check for ktmidi container first (16-byte header)
    if (std::memcmp(data, "AAAAAAAAEEEEEEEE", 16) == 0) {
      return DetectedMidiFormat::SMF2_ktmidi;
    }
  }

  if (size >= 8) {
    if (std::memcmp(data, "SMF2CLIP", 8) == 0) {
      return DetectedMidiFormat::SMF2_Clip;
    }
    if (std::memcmp(data, "SMF2CON1", 8) == 0) {
      return DetectedMidiFormat::SMF2_Container;
    }
  }

  if (size >= 4) {
    if (std::memcmp(data, "MThd", 4) == 0) {
      return DetectedMidiFormat::SMF1;
    }
  }

  return DetectedMidiFormat::Unknown;
}

bool MidiReader::isSMF1Format(const uint8_t* data, size_t size) {
  return detectFormat(data, size) == DetectedMidiFormat::SMF1;
}

bool MidiReader::isSMF2Format(const uint8_t* data, size_t size) {
  auto format = detectFormat(data, size);
  return format == DetectedMidiFormat::SMF2_Clip || format == DetectedMidiFormat::SMF2_Container ||
         format == DetectedMidiFormat::SMF2_ktmidi;
}

const ParsedTrack* ParsedMidi::getTrack(const std::string& name) const {
  for (const auto& track : tracks) {
    if (iequals(track.name, name)) {
      return &track;
    }
  }
  return nullptr;
}

bool MidiReader::read(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    error_ = "Failed to open file: " + path;
    return false;
  }

  auto size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> data(static_cast<size_t>(size));
  if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
    error_ = "Failed to read file: " + path;
    return false;
  }

  return read(data);
}

bool MidiReader::read(const std::vector<uint8_t>& data) {
  midi_ = ParsedMidi{};
  error_.clear();

  if (data.size() < 14) {
    error_ = "File too small for MIDI header";
    return false;
  }

  // Parse header
  if (!parseHeader(data.data(), data.size())) {
    return false;
  }

  // Parse tracks
  const size_t header_size = readUint32BE(data.data() + 4);
  size_t offset = 8 + header_size;
  for (uint16_t track_index = 0; track_index < midi_.num_tracks; ++track_index) {
    if (offset + 8 > data.size()) {
      error_ = "Truncated MTrk chunk header";
      return false;
    }

    // Check for MTrk chunk
    if (std::memcmp(data.data() + offset, "MTrk", 4) != 0) {
      error_ = "Expected MTrk chunk at offset " + std::to_string(offset);
      return false;
    }

    uint32_t track_size = readUint32BE(data.data() + offset + 4);
    offset += 8;

    if (offset + track_size > data.size()) {
      error_ = "Track data exceeds file size";
      return false;
    }

    if (!parseTrack(data.data() + offset, track_size)) {
      return false;
    }

    offset += track_size;
  }

  if (offset != data.size()) {
    error_ = "Unexpected trailing data after MIDI tracks";
    return false;
  }

  std::stable_sort(midi_.tempo_map.begin(), midi_.tempo_map.end(),
                   [](const TempoEvent& a, const TempoEvent& b) { return a.tick < b.tick; });
  if (!midi_.tempo_map.empty()) {
    midi_.bpm = midi_.tempo_map.front().bpm;
  }

  return true;
}

bool MidiReader::readVariableLength(const uint8_t* data, size_t& offset, size_t max_size,
                                    uint32_t& value) {
  return midisketch::readVariableLength(data, offset, max_size, value);
}

bool MidiReader::parseHeader(const uint8_t* data, size_t size) {
  if (size < 14) {
    error_ = "Invalid header size";
    return false;
  }

  // Check MThd magic
  if (std::memcmp(data, "MThd", 4) != 0) {
    error_ = "Invalid MIDI header (expected MThd)";
    return false;
  }

  uint32_t header_size = readUint32BE(data + 4);
  if (header_size < 6 || header_size > size - 8) {
    error_ = "Invalid header chunk size";
    return false;
  }

  midi_.format = readUint16BE(data + 8);
  midi_.num_tracks = readUint16BE(data + 10);
  midi_.division = readUint16BE(data + 12);
  if (midi_.division == 0) {
    error_ = "Invalid MIDI division: ticks per quarter note must be non-zero";
    return false;
  }
  if ((midi_.division & 0x8000) != 0) {
    error_ = "SMPTE time division is not supported";
    return false;
  }

  return true;
}

bool MidiReader::parseTrack(const uint8_t* data, size_t size) {
  ParsedTrack track;
  std::array<std::vector<NoteEvent>, 16> channel_notes;
  std::array<uint8_t, 16> channel_programs{};
  size_t offset = 0;
  uint32_t current_tick = 0;
  uint8_t running_status = 0;

  // Map of note-on events: key = (channel << 8) | pitch, value = (tick, velocity)
  std::map<uint16_t, std::pair<Tick, uint8_t>> active_notes;
  const auto append_note = [&](NoteEvent note, uint8_t channel) {
    if (midi_.format == 0) {
      channel_notes[channel].push_back(note);
    } else {
      track.notes.push_back(note);
    }
  };

  while (offset < size) {
    // Read delta time
    uint32_t delta = 0;
    if (!readVariableLength(data, offset, size, delta)) {
      error_ = "Invalid or truncated variable-length delta time";
      return false;
    }
    current_tick += delta;

    if (offset >= size) {
      error_ = "Truncated MIDI event after delta time";
      return false;
    }

    uint8_t status = data[offset];

    // Handle running status
    if (status < 0x80) {
      if (running_status == 0) {
        error_ = "Running status used before a channel status byte";
        return false;
      }
      status = running_status;
    } else {
      offset++;
      if (status < 0xF0) {
        running_status = status;
      }
    }

    uint8_t type = status & 0xF0;
    uint8_t channel = status & 0x0F;

    switch (type) {
      case 0x80: {  // Note Off
        if (size - offset < 2) {
          error_ = "Truncated note-off event";
          return false;
        }
        uint8_t pitch = data[offset++];
        offset++;  // velocity (ignored)

        uint16_t key = (static_cast<uint16_t>(channel) << 8) | pitch;
        auto it = active_notes.find(key);
        if (it != active_notes.end()) {
          NoteEvent note;
          note.note = pitch;
          note.velocity = it->second.second;
          note.start_tick = it->second.first;
          note.duration = current_tick - note.start_tick;
          append_note(note, channel);
          active_notes.erase(it);
        }
        track.channel = channel;
        break;
      }

      case 0x90: {  // Note On
        if (size - offset < 2) {
          error_ = "Truncated note-on event";
          return false;
        }
        uint8_t pitch = data[offset++];
        uint8_t velocity = data[offset++];

        uint16_t key = (static_cast<uint16_t>(channel) << 8) | pitch;

        if (velocity == 0) {
          // Note Off (velocity 0)
          auto it = active_notes.find(key);
          if (it != active_notes.end()) {
            NoteEvent note;
            note.note = pitch;
            note.velocity = it->second.second;
            note.start_tick = it->second.first;
            note.duration = current_tick - note.start_tick;
            append_note(note, channel);
            active_notes.erase(it);
          }
        } else {
          // Note On - close any existing note first
          auto it = active_notes.find(key);
          if (it != active_notes.end()) {
            NoteEvent note;
            note.note = pitch;
            note.velocity = it->second.second;
            note.start_tick = it->second.first;
            note.duration = current_tick - note.start_tick;
            append_note(note, channel);
          }
          active_notes[key] = {current_tick, velocity};
        }
        track.channel = channel;
        break;
      }

      case 0xA0:  // Polyphonic Key Pressure
        if (size - offset < 2) {
          error_ = "Truncated polyphonic key pressure event";
          return false;
        }
        offset += 2;
        break;

      case 0xB0:  // Control Change
        if (size - offset < 2) {
          error_ = "Truncated control change event";
          return false;
        }
        offset += 2;
        break;

      case 0xC0: {  // Program Change
        if (offset >= size) {
          error_ = "Truncated program change event";
          return false;
        }
        track.program = data[offset++];
        channel_programs[channel] = track.program;
        track.channel = channel;
        break;
      }

      case 0xD0:  // Channel Pressure
        if (offset >= size) {
          error_ = "Truncated channel pressure event";
          return false;
        }
        offset += 1;
        break;

      case 0xE0:  // Pitch Bend
        if (size - offset < 2) {
          error_ = "Truncated pitch bend event";
          return false;
        }
        offset += 2;
        break;

      case 0xF0: {  // System messages
        if (status == 0xFF) {
          // Meta event
          if (offset >= size) {
            error_ = "Truncated meta event type";
            return false;
          }
          uint8_t meta_type = data[offset++];
          uint32_t meta_len = 0;
          if (!readVariableLength(data, offset, size, meta_len)) {
            error_ = "Invalid or truncated meta event length";
            return false;
          }
          if (meta_len > size - offset) {
            error_ = "Meta event data exceeds track size";
            return false;
          }

          if (meta_type == 0x01 && meta_len > 0) {
            // Text Event - check for MIDISKETCH metadata
            std::string text(reinterpret_cast<const char*>(data + offset), meta_len);
            const std::string prefix = "MIDISKETCH:";
            if (text.compare(0, prefix.size(), prefix) == 0) {
              midi_.metadata = text.substr(prefix.size());
            }
          } else if (meta_type == 0x03 && meta_len > 0) {
            // Track name
            track.name = std::string(reinterpret_cast<const char*>(data + offset), meta_len);
          } else if (meta_type == 0x51 && meta_len == 3) {
            // Tempo
            uint32_t microseconds = (static_cast<uint32_t>(data[offset]) << 16) |
                                    (static_cast<uint32_t>(data[offset + 1]) << 8) |
                                    data[offset + 2];
            if (microseconds > 0) {
              const uint16_t bpm = static_cast<uint16_t>(kMicrosecondsPerMinute / microseconds);
              midi_.tempo_map.push_back({current_tick, bpm});
            }
          } else if (meta_type == 0x2F) {
            // End of track
            offset += meta_len;
            break;
          }
          offset += meta_len;
        } else if (status == 0xF0 || status == 0xF7) {
          // SysEx
          uint32_t sysex_len = 0;
          if (!readVariableLength(data, offset, size, sysex_len)) {
            error_ = "Invalid or truncated SysEx length";
            return false;
          }
          if (sysex_len > size - offset) {
            error_ = "SysEx data exceeds track size";
            return false;
          }
          offset += sysex_len;
        } else {
          error_ = "Unsupported system MIDI event";
          return false;
        }
        break;
      }

      default:
        break;
    }
  }

  // Close any remaining active notes
  for (const auto& [key, value] : active_notes) {
    NoteEvent note;
    note.note = key & 0xFF;
    note.velocity = value.second;
    note.start_tick = value.first;
    note.duration = current_tick - note.start_tick;
    append_note(note, static_cast<uint8_t>(key >> 8));
  }

  if (midi_.format == 0) {
    // Format-0 stores every MIDI channel in one physical MTrk chunk. Expose
    // logical per-channel tracks so analysis can compare simultaneous notes
    // and exclude channel-10 percussion without discarding melodic channels.
    for (size_t channel_index = 0; channel_index < channel_notes.size(); ++channel_index) {
      const auto channel = static_cast<uint8_t>(channel_index);
      if (channel_notes[channel].empty()) continue;
      ParsedTrack channel_track;
      channel_track.channel = channel;
      channel_track.program = channel_programs[channel];
      channel_track.name =
          channel == 9 ? "Drums"
          : track.name.empty()
              ? "Channel " + std::to_string(static_cast<unsigned>(channel) + 1)
              : track.name + " Ch " + std::to_string(static_cast<unsigned>(channel) + 1);
      channel_track.notes = std::move(channel_notes[channel]);
      NoteTimeline::sortByStartTick(channel_track.notes);
      midi_.tracks.push_back(std::move(channel_track));
    }
    return true;
  }

  // Sort notes by start time
  NoteTimeline::sortByStartTick(track.notes);
  midi_.tracks.push_back(std::move(track));
  return true;
}

}  // namespace midisketch
