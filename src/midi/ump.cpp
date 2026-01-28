/**
 * @file ump.cpp
 * @brief Implementation of UMP message builders for MIDI 2.0.
 */

#include "midi/ump.h"

#include <algorithm>
#include <string>

namespace midisketch {
namespace ump {

void writeUint32BE(std::vector<uint8_t>& buf, uint32_t value) {
  buf.push_back((value >> 24) & 0xFF);
  buf.push_back((value >> 16) & 0xFF);
  buf.push_back((value >> 8) & 0xFF);
  buf.push_back(value & 0xFF);
}

void writeUint16BE(std::vector<uint8_t>& buf, uint16_t value) {
  buf.push_back((value >> 8) & 0xFF);
  buf.push_back(value & 0xFF);
}

uint32_t makeNoteOn(uint8_t group, uint8_t channel, uint8_t note, uint8_t velocity) {
  // [MT=2:4][Group:4][Status=9:4][Channel:4][Note:8][Velocity:8]
  return (static_cast<uint32_t>(MessageType::Midi1ChannelVoice) << 28) | ((group & 0x0F) << 24) |
         (0x9 << 20) | ((channel & 0x0F) << 16) | ((note & 0x7F) << 8) | (velocity & 0x7F);
}

uint32_t makeNoteOff(uint8_t group, uint8_t channel, uint8_t note, uint8_t velocity) {
  // [MT=2:4][Group:4][Status=8:4][Channel:4][Note:8][Velocity:8]
  return (static_cast<uint32_t>(MessageType::Midi1ChannelVoice) << 28) | ((group & 0x0F) << 24) |
         (0x8 << 20) | ((channel & 0x0F) << 16) | ((note & 0x7F) << 8) | (velocity & 0x7F);
}

uint32_t makeProgramChange(uint8_t group, uint8_t channel, uint8_t program) {
  // [MT=2:4][Group:4][Status=C:4][Channel:4][Program:8][0:8]
  return (static_cast<uint32_t>(MessageType::Midi1ChannelVoice) << 28) | ((group & 0x0F) << 24) |
         (0xC << 20) | ((channel & 0x0F) << 16) | ((program & 0x7F) << 8);
}

uint32_t makeControlChange(uint8_t group, uint8_t channel, uint8_t cc, uint8_t value) {
  // [MT=2:4][Group:4][Status=B:4][Channel:4][CC#:8][Value:8]
  return (static_cast<uint32_t>(MessageType::Midi1ChannelVoice) << 28) | ((group & 0x0F) << 24) |
         (0xB << 20) | ((channel & 0x0F) << 16) | ((cc & 0x7F) << 8) | (value & 0x7F);
}

uint32_t makeDeltaClockstamp(uint8_t group, uint16_t ticks) {
  // [MT=0:4][Group:4][Status=4:4][0:4][Ticks:16]
  // Status 0x4 = Delta Clockstamp (JR Clock)
  return (static_cast<uint32_t>(MessageType::Utility) << 28) | ((group & 0x0F) << 24) |
         (0x4 << 20) | ticks;
}

void writeDeltaClockstamp(std::vector<uint8_t>& buf, uint8_t group, uint32_t ticks) {
  // For ticks <= 0xFFFF, use single 32-bit message
  // For larger values, we need to split into multiple DCS messages
  // Maximum single DCS is 16 bits (0xFFFF = 65535 ticks)

  while (ticks > 0xFFFF) {
    writeUint32BE(buf, makeDeltaClockstamp(group, 0xFFFF));
    ticks -= 0xFFFF;
  }

  if (ticks > 0) {
    writeUint32BE(buf, makeDeltaClockstamp(group, static_cast<uint16_t>(ticks)));
  } else {
    // Always write at least one DCS (even if 0)
    writeUint32BE(buf, makeDeltaClockstamp(group, 0));
  }
}

void writeDCTPQ(std::vector<uint8_t>& buf, uint16_t ticksPerQuarter) {
  // UMP Stream Message (MT=0xF), 128-bit
  // Word 0: [MT=F:4][Format=0:2][Status=0:10][Form=0:2][0:14]
  // Word 1: [TicksPerQuarter:16][0:16]
  // Word 2: [0:32]
  // Word 3: [0:32]

  // Format 0 = complete message, Status 0x00 = DCTPQ
  uint32_t word0 = (0xF << 28) | (0x0 << 26) | (0x00 << 16);
  uint32_t word1 = (static_cast<uint32_t>(ticksPerQuarter) << 16);
  uint32_t word2 = 0;
  uint32_t word3 = 0;

  writeUint32BE(buf, word0);
  writeUint32BE(buf, word1);
  writeUint32BE(buf, word2);
  writeUint32BE(buf, word3);
}

void writeStartOfClip(std::vector<uint8_t>& buf) {
  // UMP Stream Message (MT=0xF), 128-bit
  // Word 0: [MT=F:4][Format=0:2][Status=0x20:10][0:16]
  // Word 1-3: [0:32] each

  uint32_t word0 =
      (0xF << 28) | (0x0 << 26) | (static_cast<uint32_t>(StreamStatus::StartOfClip) << 16);

  writeUint32BE(buf, word0);
  writeUint32BE(buf, 0);
  writeUint32BE(buf, 0);
  writeUint32BE(buf, 0);
}

void writeEndOfClip(std::vector<uint8_t>& buf) {
  // UMP Stream Message (MT=0xF), 128-bit
  // Word 0: [MT=F:4][Format=0:2][Status=0x21:10][0:16]
  // Word 1-3: [0:32] each

  uint32_t word0 =
      (0xF << 28) | (0x0 << 26) | (static_cast<uint32_t>(StreamStatus::EndOfClip) << 16);

  writeUint32BE(buf, word0);
  writeUint32BE(buf, 0);
  writeUint32BE(buf, 0);
  writeUint32BE(buf, 0);
}

void writeTempo(std::vector<uint8_t>& buf, uint8_t group, uint32_t microsPerQuarter) {
  // Flex Data Message (MT=0xD), 128-bit
  // Word 0: [MT=D:4][Group:4][Form=0:2][Addr=0:2][BankSelect=0:8][Status=0x00:8]
  // Word 1: [TempoMicroseconds:32] (microseconds per quarter note)
  // Word 2-3: [0:32] each

  // Status 0x00 = Set Tempo, Bank 0, Address 0 (channel independent)
  uint32_t word0 =
      (0xD << 28) | ((group & 0x0F) << 24) | (0x0 << 22) | (0x0 << 20) | (0x00 << 8) | 0x00;

  writeUint32BE(buf, word0);
  writeUint32BE(buf, microsPerQuarter);
  writeUint32BE(buf, 0);
  writeUint32BE(buf, 0);
}

void writeTimeSignature(std::vector<uint8_t>& buf, uint8_t group, uint8_t numerator,
                        uint8_t denominator) {
  // Flex Data Message (MT=0xD), 128-bit
  // Word 0: [MT=D:4][Group:4][Form=0:2][Addr=0:2][BankSelect=0:8][Status=0x01:8]
  // Word 1: [Numerator:8][Denominator:8][NumOf32nds:8][0:8]
  // Word 2-3: [0:32] each

  // Status 0x01 = Set Time Signature
  uint32_t word0 =
      (0xD << 28) | ((group & 0x0F) << 24) | (0x0 << 22) | (0x0 << 20) | (0x00 << 8) | 0x01;

  // denominator is stored as power of 2 (e.g., 4 for quarter note = 2)
  uint8_t denomPower = 0;
  uint8_t tempDenom = denominator;
  while (tempDenom > 1) {
    tempDenom >>= 1;
    denomPower++;
  }

  // Number of 32nd notes per beat = 32 / denominator
  uint8_t numOf32nds = 32 / denominator;

  uint32_t word1 = (static_cast<uint32_t>(numerator) << 24) |
                   (static_cast<uint32_t>(denomPower) << 16) |
                   (static_cast<uint32_t>(numOf32nds) << 8);

  writeUint32BE(buf, word0);
  writeUint32BE(buf, word1);
  writeUint32BE(buf, 0);
  writeUint32BE(buf, 0);
}

void writeMetadataText(std::vector<uint8_t>& buf, uint8_t group, const std::string& text) {
  // SysEx8 message (MT=0x5), 128-bit per packet
  // Following ktmidi convention for unmapped meta events:
  // ManufID=0x00, DevID=0x00, SubID1=0x00, SubID2=0x00,
  // then 0xFF 0xFF 0xFF marker, then meta type byte, then data

  // For simplicity, we'll encode the text as multiple 128-bit SysEx8 packets
  // Each packet can hold up to 13 bytes of data (14 bytes - 1 for status)

  const uint8_t META_TEXT_TYPE = 0x01;  // Text event
  size_t textLen = text.size();
  size_t offset = 0;

  // First packet includes header bytes
  // Word 0: [MT=5:4][Group:4][Status:4][NumBytes:4][StreamID:8][ManufID:8]
  // Word 1: [ManufID2:8][DevID:8][SubID1:8][SubID2:8]
  // Word 2: [0xFF:8][0xFF:8][0xFF:8][MetaType:8]
  // Word 3: [Data bytes...]

  while (offset < textLen || offset == 0) {
    // Calculate how many data bytes fit in this packet
    size_t headerBytes = (offset == 0) ? 10 : 0;  // First packet has header
    size_t maxDataBytes = 14 - headerBytes;
    size_t dataBytes = std::min(maxDataBytes, textLen - offset);
    size_t totalBytes = headerBytes + dataBytes;

    // Status: 0x0 = start, 0x1 = continue, 0x2 = end, 0x3 = complete
    uint8_t status;
    if (textLen <= maxDataBytes && offset == 0) {
      status = 0x0;  // Complete in single message (start and end)
    } else if (offset == 0) {
      status = 0x1;  // Start
    } else if (offset + dataBytes >= textLen) {
      status = 0x3;  // End
    } else {
      status = 0x2;  // Continue
    }

    uint32_t word0 = (0x5 << 28) | ((group & 0x0F) << 24) | (status << 20) |
                     ((totalBytes & 0x0F) << 16) | (0x00 << 8) | 0x00;

    writeUint32BE(buf, word0);

    if (offset == 0) {
      // First packet - write header
      uint32_t word1 = (0x00 << 24) | (0x00 << 16) | (0x00 << 8) | 0x00;
      uint32_t word2 = (0xFF << 24) | (0xFF << 16) | (0xFF << 8) | META_TEXT_TYPE;
      writeUint32BE(buf, word1);
      writeUint32BE(buf, word2);

      // Word 3: up to 4 data bytes
      uint32_t word3 = 0;
      for (size_t i = 0; i < std::min(dataBytes, size_t(4)); i++) {
        word3 |= (static_cast<uint32_t>(static_cast<uint8_t>(text[offset + i])) << (24 - i * 8));
      }
      writeUint32BE(buf, word3);
      offset += std::min(dataBytes, size_t(4));
    } else {
      // Continuation packets - up to 12 bytes of data across 3 words
      for (int w = 0; w < 3; w++) {
        uint32_t word = 0;
        for (int b = 0; b < 4 && offset < textLen; b++) {
          word |= (static_cast<uint32_t>(static_cast<uint8_t>(text[offset++])) << (24 - b * 8));
        }
        writeUint32BE(buf, word);
      }
    }

    if (offset >= textLen) break;
  }
}

}  // namespace ump
}  // namespace midisketch
