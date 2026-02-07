/**
 * @file midi_reader.cpp
 * @brief Implementation of MIDI file parser.
 */

#include "midi/midi_reader.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>

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
  size_t offset = 14;  // Skip header
  while (offset < data.size() - 8) {
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

  return true;
}

uint32_t MidiReader::readVariableLength(const uint8_t* data, size_t& offset, size_t max_size) {
  uint32_t result = 0;
  midisketch::readVariableLength(data, offset, max_size, result);
  return result;
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
  if (header_size < 6) {
    error_ = "Invalid header chunk size";
    return false;
  }

  midi_.format = readUint16BE(data + 8);
  midi_.num_tracks = readUint16BE(data + 10);
  midi_.division = readUint16BE(data + 12);

  return true;
}

bool MidiReader::parseTrack(const uint8_t* data, size_t size) {
  ParsedTrack track;
  size_t offset = 0;
  uint32_t current_tick = 0;
  uint8_t running_status = 0;

  // Map of note-on events: key = (channel << 8) | pitch, value = (tick, velocity)
  std::map<uint16_t, std::pair<Tick, uint8_t>> active_notes;

  while (offset < size) {
    // Read delta time
    uint32_t delta = readVariableLength(data, offset, size);
    current_tick += delta;

    if (offset >= size) break;

    uint8_t status = data[offset];

    // Handle running status
    if (status < 0x80) {
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
        if (offset + 1 >= size) break;
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
          track.notes.push_back(note);
          active_notes.erase(it);
        }
        track.channel = channel;
        break;
      }

      case 0x90: {  // Note On
        if (offset + 1 >= size) break;
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
            track.notes.push_back(note);
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
            track.notes.push_back(note);
          }
          active_notes[key] = {current_tick, velocity};
        }
        track.channel = channel;
        break;
      }

      case 0xA0:  // Polyphonic Key Pressure
        offset += 2;
        break;

      case 0xB0:  // Control Change
        offset += 2;
        break;

      case 0xC0: {  // Program Change
        if (offset >= size) break;
        track.program = data[offset++];
        track.channel = channel;
        break;
      }

      case 0xD0:  // Channel Pressure
        offset += 1;
        break;

      case 0xE0:  // Pitch Bend
        offset += 2;
        break;

      case 0xF0: {  // System messages
        if (status == 0xFF) {
          // Meta event
          if (offset + 1 >= size) break;
          uint8_t meta_type = data[offset++];
          uint32_t meta_len = readVariableLength(data, offset, size);

          if (meta_type == 0x01 && meta_len > 0 && offset + meta_len <= size) {
            // Text Event - check for MIDISKETCH metadata
            std::string text(reinterpret_cast<const char*>(data + offset), meta_len);
            const std::string prefix = "MIDISKETCH:";
            if (text.compare(0, prefix.size(), prefix) == 0) {
              midi_.metadata = text.substr(prefix.size());
            }
          } else if (meta_type == 0x03 && meta_len > 0 && offset + meta_len <= size) {
            // Track name
            track.name = std::string(reinterpret_cast<const char*>(data + offset), meta_len);
          } else if (meta_type == 0x51 && meta_len == 3 && offset + 3 <= size) {
            // Tempo
            uint32_t microseconds = (static_cast<uint32_t>(data[offset]) << 16) |
                                    (static_cast<uint32_t>(data[offset + 1]) << 8) |
                                    data[offset + 2];
            if (microseconds > 0) {
              midi_.bpm = static_cast<uint16_t>(kMicrosecondsPerMinute / microseconds);
            }
          } else if (meta_type == 0x2F) {
            // End of track
            offset += meta_len;
            break;
          }
          offset += meta_len;
        } else if (status == 0xF0 || status == 0xF7) {
          // SysEx
          uint32_t sysex_len = readVariableLength(data, offset, size);
          offset += sysex_len;
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
    track.notes.push_back(note);
  }

  // Sort notes by start time
  std::sort(track.notes.begin(), track.notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });

  midi_.tracks.push_back(std::move(track));
  return true;
}

}  // namespace midisketch
