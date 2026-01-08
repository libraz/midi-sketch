#include "midi/midi_validator.h"
#include <cstring>
#include <fstream>
#include <sstream>

namespace midisketch {


// MidiValidationReport methods

size_t MidiValidationReport::errorCount() const {
  size_t count = 0;
  for (const auto& issue : issues) {
    if (issue.severity == ValidationSeverity::Error) ++count;
  }
  return count;
}

size_t MidiValidationReport::warningCount() const {
  size_t count = 0;
  for (const auto& issue : issues) {
    if (issue.severity == ValidationSeverity::Warning) ++count;
  }
  return count;
}

std::string MidiValidationReport::toJson() const {
  std::ostringstream ss;
  ss << "{\n";
  ss << "  \"valid\": " << (valid ? "true" : "false") << ",\n";
  ss << "  \"summary\": {\n";
  ss << "    \"file_size\": " << summary.file_size << ",\n";
  ss << "    \"format\": \"" << MidiValidator::formatName(summary.format)
     << "\",\n";
  ss << "    \"midi_type\": " << summary.midi_type << ",\n";
  ss << "    \"num_tracks\": " << summary.num_tracks << ",\n";
  ss << "    \"division\": " << summary.division << ",\n";
  ss << "    \"timing_type\": \"" << summary.timing_type << "\",\n";
  ss << "    \"ticks_per_quarter\": " << summary.ticks_per_quarter << ",\n";
  ss << "    \"error_count\": " << errorCount() << ",\n";
  ss << "    \"warning_count\": " << warningCount() << "\n";
  ss << "  },\n";

  // Tracks
  ss << "  \"tracks\": [\n";
  for (size_t i = 0; i < tracks.size(); ++i) {
    const auto& t = tracks[i];
    ss << "    {\n";
    ss << "      \"index\": " << t.index << ",\n";
    ss << "      \"name\": \"" << t.name << "\",\n";
    ss << "      \"length\": " << t.length << ",\n";
    ss << "      \"event_count\": " << t.event_count << ",\n";
    ss << "      \"has_end_of_track\": " << (t.has_end_of_track ? "true" : "false")
       << "\n";
    ss << "    }" << (i + 1 < tracks.size() ? "," : "") << "\n";
  }
  ss << "  ],\n";

  // Issues
  ss << "  \"issues\": [\n";
  for (size_t i = 0; i < issues.size(); ++i) {
    const auto& issue = issues[i];
    ss << "    {\n";
    ss << "      \"severity\": \""
       << (issue.severity == ValidationSeverity::Error     ? "error"
           : issue.severity == ValidationSeverity::Warning ? "warning"
                                                           : "info")
       << "\",\n";
    ss << "      \"message\": \"" << issue.message << "\"";
    if (issue.offset > 0) {
      ss << ",\n      \"offset\": " << issue.offset;
    }
    if (issue.track_index >= 0) {
      ss << ",\n      \"track\": " << issue.track_index;
    }
    ss << "\n    }" << (i + 1 < issues.size() ? "," : "") << "\n";
  }
  ss << "  ]\n";
  ss << "}\n";

  return ss.str();
}

std::string MidiValidationReport::toTextReport(
    const std::string& filename) const {
  std::ostringstream ss;

  ss << std::string(60, '=') << "\n";
  ss << "MIDI Validation Report";
  if (!filename.empty()) {
    ss << ": " << filename;
  }
  ss << "\n";
  ss << std::string(60, '=') << "\n";
  ss << "File size: " << summary.file_size << " bytes\n\n";

  // File info
  ss << "--- File Info ---\n";
  ss << "Format: " << MidiValidator::formatName(summary.format) << "\n";
  if (summary.format == DetectedMidiFormat::SMF1) {
    ss << "MIDI Type: " << summary.midi_type << "\n";
  }
  ss << "Tracks: " << summary.num_tracks << "\n";
  if (summary.timing_type == "PPQN") {
    ss << "Resolution: " << summary.ticks_per_quarter << " ticks/quarter\n";
  }
  ss << "\n";

  // Tracks
  if (!tracks.empty()) {
    ss << "--- Tracks ---\n";
    for (const auto& t : tracks) {
      ss << "  [" << t.index << "] "
         << (t.name.empty() ? "(unnamed)" : t.name) << ": " << t.event_count
         << " events, " << t.length << " bytes";
      if (!t.has_end_of_track) {
        ss << " (missing EOT)";
      }
      ss << "\n";
    }
    ss << "\n";
  }

  // Warnings
  bool has_warnings = false;
  for (const auto& issue : issues) {
    if (issue.severity == ValidationSeverity::Warning) {
      if (!has_warnings) {
        ss << "--- Warnings ---\n";
        has_warnings = true;
      }
      ss << "  ! " << issue.message << "\n";
    }
  }
  if (has_warnings) ss << "\n";

  // Errors
  bool has_errors = false;
  for (const auto& issue : issues) {
    if (issue.severity == ValidationSeverity::Error) {
      if (!has_errors) {
        ss << "--- Errors ---\n";
        has_errors = true;
      }
      ss << "  X " << issue.message << "\n";
    }
  }
  if (has_errors) ss << "\n";

  // Result
  ss << "Result: " << (valid ? "VALID" : "INVALID") << "\n";
  ss << std::string(60, '=') << "\n";

  return ss.str();
}

// MidiValidator methods

MidiValidationReport MidiValidator::validate(const std::string& path) const {
  MidiValidationReport report;

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    addError(report, "Cannot open file: " + path);
    return report;
  }

  auto size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> data(static_cast<size_t>(size));
  if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
    addError(report, "Cannot read file: " + path);
    return report;
  }

  return validate(data);
}

MidiValidationReport MidiValidator::validate(
    const std::vector<uint8_t>& data) const {
  return validate(data.data(), data.size());
}

MidiValidationReport MidiValidator::validate(const uint8_t* data,
                                             size_t size) const {
  MidiValidationReport report;
  report.summary.file_size = size;

  if (size < 8) {
    addError(report, "File too small for valid MIDI header");
    return report;
  }

  // Detect format
  report.summary.format = detectFormat(data, size);

  switch (report.summary.format) {
    case DetectedMidiFormat::SMF1:
      report.valid = validateSMF1(data, size, report);
      break;
    case DetectedMidiFormat::SMF2_Clip:
      report.valid = validateSMF2Clip(data, size, report);
      break;
    case DetectedMidiFormat::SMF2_Container:
      addWarning(report, "SMF2 Container (SMF2CON1) validation not yet implemented");
      report.valid = true;
      break;
    case DetectedMidiFormat::SMF2_ktmidi:
      report.valid = validateSMF2Container(data, size, report);
      break;
    case DetectedMidiFormat::Unknown:
    default:
      addError(report, "Unknown MIDI format");
      break;
  }

  return report;
}

DetectedMidiFormat MidiValidator::detectFormat(const uint8_t* data,
                                               size_t size) {
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
    if (std::memcmp(data, "MThd", 4) == 0) {
      return DetectedMidiFormat::SMF1;
    }
  }

  return DetectedMidiFormat::Unknown;
}

std::string MidiValidator::formatName(DetectedMidiFormat format) {
  switch (format) {
    case DetectedMidiFormat::SMF1:
      return "SMF1";
    case DetectedMidiFormat::SMF2_Clip:
      return "SMF2_Clip";
    case DetectedMidiFormat::SMF2_Container:
      return "SMF2_Container";
    case DetectedMidiFormat::SMF2_ktmidi:
      return "SMF2_ktmidi";
    case DetectedMidiFormat::Unknown:
    default:
      return "Unknown";
  }
}

bool MidiValidator::validateSMF1(const uint8_t* data, size_t size,
                                  MidiValidationReport& report) const {
  // Validate header
  if (!validateSMF1Header(data, size, report)) {
    return false;
  }

  // Validate tracks
  size_t offset = 8 + readUint32BE(data + 4);  // After header chunk
  int tracks_found = 0;

  while (offset < size) {
    if (offset + 8 > size) {
      addError(report, "Unexpected end of file at track " +
                           std::to_string(tracks_found));
      return false;
    }

    // Check MTrk
    if (std::memcmp(data + offset, "MTrk", 4) != 0) {
      addError(report,
               "Expected MTrk chunk at offset " + std::to_string(offset));
      return false;
    }

    uint32_t track_len = readUint32BE(data + offset + 4);
    size_t track_start = offset + 8;
    size_t track_end = track_start + track_len;

    if (track_end > size) {
      addError(report, "Track " + std::to_string(tracks_found) +
                           " extends beyond file (" + std::to_string(track_end) +
                           " > " + std::to_string(size) + ")");
      return false;
    }

    ValidatedTrack track_info;
    track_info.index = tracks_found;
    track_info.length = track_len;

    if (!validateSMF1Track(data + track_start, track_len, tracks_found, report,
                           track_info)) {
      return false;
    }

    report.tracks.push_back(track_info);

    if (!track_info.has_end_of_track) {
      addWarning(report,
                 "Track " + std::to_string(tracks_found) +
                     " missing End of Track event",
                 0, tracks_found);
    }

    offset = track_end;
    tracks_found++;
  }

  if (tracks_found != report.summary.num_tracks) {
    addError(report, "Expected " + std::to_string(report.summary.num_tracks) +
                         " tracks, found " + std::to_string(tracks_found));
    return false;
  }

  return true;
}

bool MidiValidator::validateSMF1Header(const uint8_t* data, size_t size,
                                        MidiValidationReport& report) const {
  if (size < 14) {
    addError(report, "Invalid header size");
    return false;
  }

  // Check MThd magic
  if (std::memcmp(data, "MThd", 4) != 0) {
    addError(report, "Invalid MIDI header (expected MThd)");
    return false;
  }

  uint32_t header_len = readUint32BE(data + 4);
  if (header_len < 6) {
    addError(report,
             "Invalid header length: " + std::to_string(header_len));
    return false;
  }

  report.summary.midi_type = readUint16BE(data + 8);
  if (report.summary.midi_type > 2) {
    addError(report,
             "Invalid MIDI format: " + std::to_string(report.summary.midi_type));
    return false;
  }

  report.summary.num_tracks = readUint16BE(data + 10);
  if (report.summary.num_tracks == 0) {
    addError(report, "No tracks in file");
    return false;
  }

  report.summary.division = readUint16BE(data + 12);
  if (report.summary.division & 0x8000) {
    report.summary.timing_type = "SMPTE";
  } else {
    report.summary.timing_type = "PPQN";
    report.summary.ticks_per_quarter = report.summary.division;
  }

  return true;
}

bool MidiValidator::validateSMF1Track(const uint8_t* data, size_t size,
                                       int track_index,
                                       MidiValidationReport& report,
                                       ValidatedTrack& track_info) const {
  size_t pos = 0;
  size_t event_count = 0;
  uint8_t running_status = 0;
  uint32_t current_tick = 0;

  while (pos < size) {
    // Read delta time
    uint32_t delta = 0;
    if (!readVariableLength(data, pos, size, delta)) {
      addError(report,
               "Invalid delta time at track " + std::to_string(track_index) +
                   ", offset " + std::to_string(pos),
               pos, track_index);
      return false;
    }
    current_tick += delta;

    if (pos >= size) {
      addError(report,
               "Unexpected end of track " + std::to_string(track_index) +
                   " after delta time",
               pos, track_index);
      return false;
    }

    // Read status byte
    uint8_t status = data[pos];
    if (status < 0x80) {
      // Running status
      if (running_status == 0) {
        addError(report,
                 "Missing status byte at track " + std::to_string(track_index) +
                     ", offset " + std::to_string(pos),
                 pos, track_index);
        return false;
      }
      status = running_status;
    } else {
      pos++;
      if (status < 0xF0) {
        running_status = status;
      }
    }

    uint8_t event_type = status & 0xF0;

    if (status == 0xFF) {
      // Meta event
      if (pos >= size) {
        addError(report,
                 "Incomplete meta event at track " + std::to_string(track_index),
                 pos, track_index);
        return false;
      }
      uint8_t meta_type = data[pos++];
      uint32_t meta_len = 0;
      if (!readVariableLength(data, pos, size, meta_len)) {
        addError(report,
                 "Invalid meta event length at track " +
                     std::to_string(track_index),
                 pos, track_index);
        return false;
      }

      if (pos + meta_len > size) {
        addError(report,
                 "Meta event data extends beyond track " +
                     std::to_string(track_index),
                 pos, track_index);
        return false;
      }

      // Track name (meta type 0x03)
      if (meta_type == 0x03 && meta_len > 0) {
        track_info.name =
            std::string(reinterpret_cast<const char*>(data + pos), meta_len);
      }

      // End of track (meta type 0x2F)
      if (meta_type == 0x2F) {
        track_info.has_end_of_track = true;
      }

      pos += meta_len;
    } else if (status == 0xF0 || status == 0xF7) {
      // SysEx
      uint32_t sysex_len = 0;
      if (!readVariableLength(data, pos, size, sysex_len)) {
        addError(report,
                 "Invalid SysEx length at track " + std::to_string(track_index),
                 pos, track_index);
        return false;
      }
      pos += sysex_len;
    } else if (event_type == 0x80 || event_type == 0x90 ||
               event_type == 0xA0 || event_type == 0xB0 ||
               event_type == 0xE0) {
      // Two data bytes
      if (pos + 1 >= size) {
        addError(report,
                 "Incomplete channel message at track " +
                     std::to_string(track_index),
                 pos, track_index);
        return false;
      }
      uint8_t data1 = data[pos++];
      uint8_t data2 = data[pos++];

      // Validate data bytes (must be < 128)
      if (data1 > 127 || data2 > 127) {
        addWarning(report,
                   "Invalid data byte in channel message at track " +
                       std::to_string(track_index) + ", tick " +
                       std::to_string(current_tick),
                   pos - 2, track_index);
      }
    } else if (event_type == 0xC0 || event_type == 0xD0) {
      // One data byte
      if (pos >= size) {
        addError(report,
                 "Incomplete channel message at track " +
                     std::to_string(track_index),
                 pos, track_index);
        return false;
      }
      uint8_t data1 = data[pos++];
      if (data1 > 127) {
        addWarning(report,
                   "Invalid data byte in channel message at track " +
                       std::to_string(track_index),
                   pos - 1, track_index);
      }
    } else {
      addWarning(report,
                 "Unknown status byte 0x" +
                     std::to_string(static_cast<int>(status)) + " at track " +
                     std::to_string(track_index),
                 pos, track_index);
      break;
    }

    event_count++;
  }

  track_info.event_count = event_count;
  return true;
}

bool MidiValidator::validateSMF2Clip(const uint8_t* data, size_t size,
                                      MidiValidationReport& report) const {
  if (size < 16) {
    addError(report, "SMF2 Clip file too small");
    return false;
  }

  report.summary.num_tracks = 1;

  // After "SMF2CLIP" header, should be UMP messages
  size_t offset = 8;

  // Check for valid UMP structure
  if (offset + 4 > size) {
    addError(report, "No UMP data after header");
    return false;
  }

  // Read first UMP word to verify message type
  uint32_t first_word = readUint32BE(data + offset);
  uint8_t first_mt = (first_word >> 28) & 0x0F;

  // First message should typically be utility (0x0) or stream (0xF)
  if (first_mt != 0x0 && first_mt != 0xF) {
    addWarning(report,
               "First UMP message type is 0x" + std::to_string(first_mt) +
                   " (expected 0x0 or 0xF)");
  }

  // Scan UMP messages to count events and check structure
  size_t event_count = 0;
  bool has_end_of_clip = false;

  while (offset + 4 <= size) {
    uint32_t word = readUint32BE(data + offset);
    uint8_t mt = (word >> 28) & 0x0F;

    // Determine message size based on type
    size_t msg_size = 4;  // Default: 32-bit
    if (mt == 0x3 || mt == 0x4) {
      msg_size = 8;  // 64-bit
    } else if (mt == 0xD || mt == 0xF) {
      msg_size = 16;  // 128-bit
    }

    // Count channel voice messages
    if (mt == 0x2 || mt == 0x4) {
      event_count++;
    }

    // Check for End of Clip (MT=0xF, Status=0x21)
    if (mt == 0xF && msg_size == 16 && offset + 16 <= size) {
      uint8_t status = (word >> 16) & 0xFF;
      if (status == 0x21) {
        has_end_of_clip = true;
      }
    }

    offset += msg_size;
  }

  ValidatedTrack clip_track;
  clip_track.index = 0;
  clip_track.name = "Clip";
  clip_track.length = size - 8;
  clip_track.event_count = event_count;
  clip_track.has_end_of_track = has_end_of_clip;
  report.tracks.push_back(clip_track);

  if (!has_end_of_clip) {
    addWarning(report, "Clip missing End of Clip message");
  }

  return true;
}

bool MidiValidator::validateSMF2Container(const uint8_t* data, size_t size,
                                           MidiValidationReport& report) const {
  // ktmidi container format:
  // 0-15: "AAAAAAAAEEEEEEEE"
  // 16-19: deltaTimeSpec (i32 BE)
  // 20-23: numTracks (i32 BE)
  // 24+: embedded clips

  if (size < 24) {
    addError(report, "ktmidi container too small");
    return false;
  }

  int32_t delta_time_spec =
      static_cast<int32_t>(readUint32BE(data + 16));
  int32_t num_tracks = static_cast<int32_t>(readUint32BE(data + 20));

  report.summary.division = static_cast<uint16_t>(delta_time_spec > 0 ? delta_time_spec : 480);
  report.summary.ticks_per_quarter = report.summary.division;
  report.summary.timing_type = "PPQN";
  report.summary.num_tracks = static_cast<uint16_t>(num_tracks);

  addInfo(report, "ktmidi container with " + std::to_string(num_tracks) +
                      " tracks, deltaTimeSpec=" +
                      std::to_string(delta_time_spec));

  // Parse embedded clips
  size_t offset = 24;
  for (int i = 0; i < num_tracks && offset < size; ++i) {
    // Each clip should start with "SMF2CLIP"
    if (offset + 8 > size) {
      addError(report, "Clip " + std::to_string(i) + " header truncated");
      return false;
    }

    if (std::memcmp(data + offset, "SMF2CLIP", 8) != 0) {
      addError(report,
               "Expected SMF2CLIP header at clip " + std::to_string(i));
      return false;
    }

    // Find the end of this clip by looking for End of Clip message
    // or the next SMF2CLIP header
    size_t clip_start = offset;
    offset += 8;

    size_t event_count = 0;
    bool has_end_of_clip = false;

    // Scan UMP messages
    while (offset + 4 <= size) {
      // Check for next clip header
      if (offset + 8 <= size &&
          std::memcmp(data + offset, "SMF2CLIP", 8) == 0) {
        break;
      }

      // Read UMP word
      uint32_t word = readUint32BE(data + offset);
      uint8_t mt = (word >> 28) & 0x0F;

      // Determine message size based on type
      size_t msg_size = 4;  // Default: 32-bit
      if (mt == 0x3 || mt == 0x4) {
        msg_size = 8;  // 64-bit
      } else if (mt == 0xD || mt == 0xF) {
        msg_size = 16;  // 128-bit
      }

      // Count channel voice messages (Note On/Off, etc.)
      if (mt == 0x2 || mt == 0x4) {
        event_count++;
      }

      // Check for End of Clip (MT=0xF, Status=0x21)
      if (mt == 0xF && msg_size == 16 && offset + 16 <= size) {
        uint8_t status = (word >> 16) & 0xFF;
        if (status == 0x21) {
          has_end_of_clip = true;
        }
      }

      offset += msg_size;
    }

    ValidatedTrack track_info;
    track_info.index = i;
    track_info.name = "Clip " + std::to_string(i);
    track_info.length = offset - clip_start;
    track_info.event_count = event_count;
    track_info.has_end_of_track = has_end_of_clip;
    report.tracks.push_back(track_info);

    if (!has_end_of_clip) {
      addWarning(report, "Clip " + std::to_string(i) + " missing End of Clip",
                 0, i);
    }
  }

  return true;
}

bool MidiValidator::readVariableLength(const uint8_t* data, size_t& offset,
                                        size_t max_size, uint32_t& value) {
  value = 0;
  size_t count = 0;

  do {
    if (offset >= max_size || count > 4) {
      return false;
    }
    uint8_t byte = data[offset++];
    value = (value << 7) | (byte & 0x7F);
    if (!(byte & 0x80)) break;
    count++;
  } while (true);

  return true;
}

uint16_t MidiValidator::readUint16BE(const uint8_t* data) {
  return (static_cast<uint16_t>(data[0]) << 8) | data[1];
}

uint32_t MidiValidator::readUint32BE(const uint8_t* data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) | data[3];
}

void MidiValidator::addError(MidiValidationReport& report,
                              const std::string& msg, size_t offset,
                              int track) {
  report.issues.push_back(
      {ValidationSeverity::Error, msg, offset, track});
}

void MidiValidator::addWarning(MidiValidationReport& report,
                                const std::string& msg, size_t offset,
                                int track) {
  report.issues.push_back(
      {ValidationSeverity::Warning, msg, offset, track});
}

void MidiValidator::addInfo(MidiValidationReport& report, const std::string& msg,
                             size_t offset, int track) {
  report.issues.push_back(
      {ValidationSeverity::Info, msg, offset, track});
}

}  // namespace midisketch
