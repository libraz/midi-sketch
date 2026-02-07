/**
 * @file ump.h
 * @brief Universal MIDI Packet (UMP) message builders for MIDI 2.0.
 */

#ifndef MIDISKETCH_MIDI_UMP_H
#define MIDISKETCH_MIDI_UMP_H

#include <cstdint>
#include <string>
#include <vector>

#include "midi/byte_order.h"

namespace midisketch {
namespace ump {

// UMP Message Types (MT) - upper 4 bits of first word
enum class MessageType : uint8_t {
  Utility = 0x0,            // 32-bit: JR Timestamp, Delta Clockstamp
  System = 0x1,             // 32-bit: System Common/Real Time
  Midi1ChannelVoice = 0x2,  // 32-bit: MIDI 1.0 Channel Voice
  Data64 = 0x3,             // 64-bit: SysEx7
  Midi2ChannelVoice = 0x4,  // 64-bit: MIDI 2.0 Channel Voice
  Data128 = 0x5,            // 128-bit: SysEx8
  FlexData = 0xD,           // 128-bit: Flex Data (tempo, time sig, metadata)
  UmpStream = 0xF           // 128-bit: UMP Stream (Start/End of Clip)
};

// UMP Stream Status codes (for MT=0xF)
enum class StreamStatus : uint8_t {
  EndpointDiscovery = 0x00,
  EndpointInfoNotify = 0x01,
  DeviceIdentityNotify = 0x02,
  EndpointNameNotify = 0x03,
  ProductInstanceIdNotify = 0x04,
  StreamConfigRequest = 0x05,
  StreamConfigNotify = 0x06,
  FunctionBlockDiscovery = 0x10,
  FunctionBlockInfoNotify = 0x11,
  FunctionBlockNameNotify = 0x12,
  StartOfClip = 0x20,
  EndOfClip = 0x21,
  DCTPQ = 0x00  // Delta Clockstamp Ticks Per Quarter (format=0x00)
};

// Write 32-bit word in big-endian to buffer (delegates to midisketch::writeUint32BE)
inline void writeUint32BE(std::vector<uint8_t>& buf, uint32_t value) {
  midisketch::writeUint32BE(buf, value);
}

// Write 16-bit word in big-endian to buffer (delegates to midisketch::writeUint16BE)
inline void writeUint16BE(std::vector<uint8_t>& buf, uint16_t value) {
  midisketch::writeUint16BE(buf, value);
}

// Build MIDI 1.0 Channel Voice Note On message (32-bit UMP)
// Returns: [MT=2][Group][Status=9][Channel][Note][Velocity]
uint32_t makeNoteOn(uint8_t group, uint8_t channel, uint8_t note, uint8_t velocity);

// Build MIDI 1.0 Channel Voice Note Off message (32-bit UMP)
// Returns: [MT=2][Group][Status=8][Channel][Note][Velocity]
uint32_t makeNoteOff(uint8_t group, uint8_t channel, uint8_t note, uint8_t velocity = 0);

// Build MIDI 1.0 Channel Voice Program Change message (32-bit UMP)
// Returns: [MT=2][Group][Status=C][Channel][Program][0]
uint32_t makeProgramChange(uint8_t group, uint8_t channel, uint8_t program);

// Build MIDI 1.0 Channel Voice Control Change message (32-bit UMP)
// Returns: [MT=2][Group][Status=B][Channel][CC#][Value]
uint32_t makeControlChange(uint8_t group, uint8_t channel, uint8_t cc, uint8_t value);

// Build Delta Clockstamp message (32-bit UMP, utility message)
// Returns: [MT=0][Group][Status=4][0][Ticks:16]
// For ticks > 0xFFFF, use writeDeltaClockstampLarge
uint32_t makeDeltaClockstamp(uint8_t group, uint16_t ticks);

// Write Delta Clockstamp for large tick values (up to 20 bits)
// Uses two 32-bit words if needed
void writeDeltaClockstamp(std::vector<uint8_t>& buf, uint8_t group, uint32_t ticks);

// Write DCTPQ (Delta Clockstamp Ticks Per Quarter Note) message
// 128-bit UMP Stream message
void writeDCTPQ(std::vector<uint8_t>& buf, uint16_t ticksPerQuarter);

// Write Start of Clip message (128-bit UMP Stream)
void writeStartOfClip(std::vector<uint8_t>& buf);

// Write End of Clip message (128-bit UMP Stream)
void writeEndOfClip(std::vector<uint8_t>& buf);

// Write Tempo message (Flex Data, 128-bit)
// microsPerQuarter: microseconds per quarter note (60000000 / BPM)
void writeTempo(std::vector<uint8_t>& buf, uint8_t group, uint32_t microsPerQuarter);

// Write Time Signature message (Flex Data, 128-bit)
void writeTimeSignature(std::vector<uint8_t>& buf, uint8_t group, uint8_t numerator,
                        uint8_t denominator);

// Write metadata as SysEx8 message (for MIDISKETCH: prefix data)
// Uses the convention: ManufID=0x00, DevID=0x00, SubID=0x00, 0xFFFFFF + meta
void writeMetadataText(std::vector<uint8_t>& buf, uint8_t group, const std::string& text);

}  // namespace ump
}  // namespace midisketch

#endif  // MIDISKETCH_MIDI_UMP_H
