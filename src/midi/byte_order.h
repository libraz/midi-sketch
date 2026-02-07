/**
 * @file byte_order.h
 * @brief Big-endian byte order read/write utilities for MIDI binary data.
 */

#ifndef MIDISKETCH_MIDI_BYTE_ORDER_H
#define MIDISKETCH_MIDI_BYTE_ORDER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace midisketch {

/**
 * @brief Read a big-endian uint16 from a byte buffer.
 * @param data Pointer to at least 2 bytes of data
 * @return Decoded 16-bit value
 */
inline uint16_t readUint16BE(const uint8_t* data) {
  return (static_cast<uint16_t>(data[0]) << 8) | data[1];
}

/**
 * @brief Read a big-endian uint32 from a byte buffer.
 * @param data Pointer to at least 4 bytes of data
 * @return Decoded 32-bit value
 */
inline uint32_t readUint32BE(const uint8_t* data) {
  return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) | data[3];
}

/**
 * @brief Write a big-endian uint16 to a byte buffer.
 * @param buf Output buffer to append to
 * @param value 16-bit value to write
 */
inline void writeUint16BE(std::vector<uint8_t>& buf, uint16_t value) {
  buf.push_back((value >> 8) & 0xFF);
  buf.push_back(value & 0xFF);
}

/**
 * @brief Write a big-endian uint32 to a byte buffer.
 * @param buf Output buffer to append to
 * @param value 32-bit value to write
 */
inline void writeUint32BE(std::vector<uint8_t>& buf, uint32_t value) {
  buf.push_back((value >> 24) & 0xFF);
  buf.push_back((value >> 16) & 0xFF);
  buf.push_back((value >> 8) & 0xFF);
  buf.push_back(value & 0xFF);
}

/**
 * @brief Read a MIDI variable-length quantity (VLQ).
 *
 * VLQ encoding uses 7 data bits per byte, with the high bit indicating
 * continuation. Maximum 4 bytes (28 bits of data).
 *
 * @param data Byte buffer to read from
 * @param offset Current read position (updated on return)
 * @param max_size Maximum valid offset (buffer size)
 * @param value Output: decoded VLQ value
 * @return true if read succeeded, false if data is truncated or malformed
 */
inline bool readVariableLength(const uint8_t* data, size_t& offset, size_t max_size,
                               uint32_t& value) {
  value = 0;
  size_t count = 0;

  do {
    if (offset >= max_size || count > 4) {  // NOLINT: 4 is max VLQ byte count
      return false;
    }
    uint8_t byte = data[offset++];
    value = (value << 7) | (byte & 0x7F);  // NOLINT: bit operations for VLQ decoding
    if (!(byte & 0x80)) break;              // NOLINT: bit check for continuation
    count++;
  } while (true);

  return true;
}

}  // namespace midisketch

#endif  // MIDISKETCH_MIDI_BYTE_ORDER_H
