/**
 * @file midi2_reader.cpp
 * @brief Implementation of MIDI 2.0 Clip and Container file reader.
 */

#include "midi/midi2_reader.h"

#include <cstring>

#include "midi/byte_order.h"
#include "midi/midi2_format.h"
#include "midi/ump.h"

namespace midisketch {

bool Midi2Reader::isMidi2Format(const uint8_t* data, size_t size) {
  if (size >= kContainerMagicLen && std::memcmp(data, kContainerMagic, kContainerMagicLen) == 0) {
    return true;
  }
  if (size >= kClipMagicLen && std::memcmp(data, kClipMagic, kClipMagicLen) == 0) {
    return true;
  }
  return false;
}

bool Midi2Reader::read(const uint8_t* data, size_t size) {
  midi_ = ParsedMidi2();
  error_.clear();

  // Check for ktmidi container
  if (size >= kContainerMagicLen && std::memcmp(data, kContainerMagic, kContainerMagicLen) == 0) {
    return parseContainer(data, size);
  }

  // Check for SMF2 Clip
  if (size >= kClipMagicLen && std::memcmp(data, kClipMagic, kClipMagicLen) == 0) {
    return parseClip(data, size);
  }

  if (size < kClipMagicLen) {
    error_ = "File too small";
    return false;
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

  bool have_division = false;
  bool have_tempo = false;

  while (offset + 4 <= size) {
    // A container embeds whole clips; step over each clip's "SMF2CLIP" header.
    if (offset + kClipMagicLen <= size &&
        std::memcmp(data + offset, kClipMagic, kClipMagicLen) == 0) {
      offset += kClipMagicLen;
      continue;
    }

    const uint32_t word0 = readUint32BE(data + offset);
    const uint8_t mt = (word0 >> 28) & 0x0F;  // Message Type

    const size_t msgSize = ump::messageSize(mt);

    if (offset + msgSize > size) break;

    if (mt == static_cast<uint8_t>(ump::MessageType::Utility)) {
      uint16_t ticks = 0;
      if (!have_division && ump::decodeDCTPQ(word0, ticks) && ticks > 0) {
        midi_.division = ticks;
        have_division = true;
      }
    } else if (mt == static_cast<uint8_t>(ump::MessageType::FlexData)) {
      // Set Tempo: status byte 0x00, word 1 in 10 ns units per quarter note.
      const uint8_t statusByte = word0 & 0xFF;
      if (!have_tempo && statusByte == 0x00) {
        const uint16_t bpm = ump::bpmFromTempo(readUint32BE(data + offset + 4));
        if (bpm > 0) {
          midi_.bpm = bpm;
          have_tempo = true;
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

}  // namespace midisketch
