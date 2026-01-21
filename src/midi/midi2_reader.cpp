/**
 * @file midi2_reader.cpp
 * @brief Implementation of MIDI 2.0 Clip and Container file reader.
 */

#include "midi/midi2_reader.h"

#include <cstring>
#include <fstream>

#include "core/timing_constants.h"
#include "midi/midi2_format.h"

namespace midisketch {

namespace {

// Read big-endian uint32
uint32_t readUint32BE(const uint8_t* data) {
  return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) | data[3];
}

}  // namespace

bool Midi2Reader::isMidi2Format(const uint8_t* data, size_t size) {
  if (size >= kContainerMagicLen && std::memcmp(data, kContainerMagic, kContainerMagicLen) == 0) {
    return true;
  }
  if (size >= kClipMagicLen && std::memcmp(data, kClipMagic, kClipMagicLen) == 0) {
    return true;
  }
  return false;
}

bool Midi2Reader::read(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    error_ = "Failed to open file: " + path;
    return false;
  }

  auto size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> buffer(static_cast<size_t>(size));
  if (!file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size))) {
    error_ = "Failed to read file: " + path;
    return false;
  }

  return read(buffer.data(), buffer.size());
}

bool Midi2Reader::read(const uint8_t* data, size_t size) {
  midi_ = ParsedMidi2();
  error_.clear();

  if (size < kContainerMagicLen) {
    error_ = "File too small";
    return false;
  }

  // Check for ktmidi container
  if (std::memcmp(data, kContainerMagic, kContainerMagicLen) == 0) {
    return parseContainer(data, size);
  }

  // Check for SMF2 Clip
  if (size >= kClipMagicLen && std::memcmp(data, kClipMagic, kClipMagicLen) == 0) {
    return parseClip(data, size);
  }

  error_ = "Unknown MIDI 2.0 format";
  return false;
}

bool Midi2Reader::parseContainer(const uint8_t* data, size_t size) {
  // ktmidi container format:
  // "AAAAAAAAEEEEEEEE" (16 bytes)
  // deltaTimeSpec (i32, big-endian) - same as SMF division
  // numTracks (i32, big-endian)
  // Track data...

  if (size < kContainerMagicLen + 8) {
    error_ = "Container header too short";
    return false;
  }

  size_t offset = kContainerMagicLen;
  midi_.division = static_cast<uint16_t>(readUint32BE(data + offset));
  offset += 4;
  midi_.num_tracks = static_cast<uint16_t>(readUint32BE(data + offset));
  offset += 4;

  // Parse UMP messages to extract metadata and tempo
  parseUmpMessages(data, size, offset);

  return true;
}

bool Midi2Reader::parseClip(const uint8_t* data, size_t size) {
  // SMF2CLIP format:
  // "SMF2CLIP" (8 bytes)
  // UMP messages...

  size_t offset = kClipMagicLen;
  midi_.num_tracks = 1;

  // Parse UMP messages
  parseUmpMessages(data, size, offset);

  return true;
}

void Midi2Reader::parseUmpMessages(const uint8_t* data, size_t size, size_t offset) {
  // Parse UMP messages looking for:
  // - DCTPQ (division)
  // - Tempo
  // - MIDISKETCH metadata (via string search)

  while (offset + 4 <= size) {
    uint32_t word0 = readUint32BE(data + offset);
    uint8_t mt = (word0 >> 28) & 0x0F;  // Message Type

    size_t msgSize = 0;
    switch (mt) {
      case 0x0:  // Utility (32-bit)
      case 0x1:  // System (32-bit)
      case 0x2:  // MIDI 1.0 CV (32-bit)
        msgSize = 4;
        break;
      case 0x3:  // Data64 (64-bit)
      case 0x4:  // MIDI 2.0 CV (64-bit)
        msgSize = 8;
        break;
      case 0x5:  // Data128 - SysEx8 (128-bit)
      case 0xD:  // Flex Data (128-bit)
      case 0xF:  // UMP Stream (128-bit)
        msgSize = 16;
        break;
      default:
        // Unknown message type, skip 4 bytes
        msgSize = 4;
        break;
    }

    if (offset + msgSize > size) break;

    // Handle specific message types
    if (mt == 0xF) {
      // UMP Stream message
      uint16_t status = (word0 >> 16) & 0x3FF;
      if (status == 0x00 && offset + 16 <= size) {
        // DCTPQ message
        uint32_t word1 = readUint32BE(data + offset + 4);
        midi_.division = static_cast<uint16_t>((word1 >> 16) & 0xFFFF);
      }
    } else if (mt == 0xD) {
      // Flex Data message
      uint8_t statusByte = word0 & 0xFF;
      if (statusByte == 0x00 && offset + 16 <= size) {
        // Tempo message
        uint32_t word1 = readUint32BE(data + offset + 4);
        if (word1 > 0) {
          midi_.bpm = static_cast<uint16_t>(kMicrosecondsPerMinute / word1);
        }
      }
    }

    offset += msgSize;
  }

  // Extract SysEx8 data - UMP SysEx8 format has data split across packets
  // Each packet has 2-byte header (MT/Group/Status/NumBytes) + 14 bytes data
  // Look for SysEx8 packets (MT=5) and extract the text data

  std::string text_buffer;
  size_t pos = 0;
  while (pos + 16 <= size) {
    uint8_t mt = (data[pos] >> 4) & 0x0F;
    if (mt == 0x5) {
      // SysEx8 packet - extract data bytes
      // Skip first 2 bytes (header), next 14 bytes are data (possibly padded)
      for (int i = 2; i < 16 && pos + i < size; ++i) {
        uint8_t byte = data[pos + i];
        // Skip header/marker bytes and extract printable ASCII
        if (byte >= 0x20 && byte <= 0x7E) {
          text_buffer += static_cast<char>(byte);
        }
      }
      pos += 16;
    } else {
      pos += 4;  // Move to next potential packet
    }
  }

  // Now search for MIDISKETCH: in the extracted text
  const std::string prefix = "MIDISKETCH:";
  size_t meta_start = text_buffer.find(prefix);
  if (meta_start != std::string::npos) {
    size_t json_start = text_buffer.find('{', meta_start);
    if (json_start != std::string::npos) {
      int brace_count = 0;
      size_t json_end = json_start;
      for (size_t i = json_start; i < text_buffer.size(); ++i) {
        if (text_buffer[i] == '{')
          brace_count++;
        else if (text_buffer[i] == '}') {
          brace_count--;
          if (brace_count == 0) {
            json_end = i + 1;
            break;
          }
        }
      }
      if (json_end > json_start) {
        midi_.metadata = text_buffer.substr(json_start, json_end - json_start);
      }
    }
  }
}

void Midi2Reader::extractMetadataFromSysEx8(const uint8_t* data, size_t len) {
  // Look for MIDISKETCH: prefix
  const std::string prefix = "MIDISKETCH:";
  std::string text(reinterpret_cast<const char*>(data), len);

  if (text.compare(0, prefix.size(), prefix) == 0) {
    midi_.metadata = text.substr(prefix.size());
  }
}

}  // namespace midisketch
