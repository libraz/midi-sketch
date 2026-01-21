/**
 * @file midi2_reader.h
 * @brief MIDI 2.0 Clip and Container file reader.
 */

#ifndef MIDISKETCH_MIDI2_READER_H
#define MIDISKETCH_MIDI2_READER_H

#include <cstdint>
#include <string>
#include <vector>

namespace midisketch {

/**
 * @brief Parsed MIDI 2.0 container data.
 */
struct ParsedMidi2 {
  uint16_t num_tracks = 0;  ///< Number of tracks
  uint16_t division = 480;  ///< Ticks per quarter note
  uint16_t bpm = 120;       ///< Tempo
  std::string metadata;     ///< MIDISKETCH metadata (JSON) if present

  bool hasMidiSketchMetadata() const { return !metadata.empty(); }
};

/**
 * @brief Reader for MIDI 2.0 Clip and Container files.
 *
 * Supports:
 * - ktmidi Container File (multi-track, "AAAAAAAAEEEEEEEE" header)
 * - SMF2 Clip File (single track, "SMF2CLIP" header)
 */
class Midi2Reader {
 public:
  /**
   * @brief Read a MIDI 2.0 file from disk.
   * @param path Path to the file
   * @return true on success, false on error
   */
  bool read(const std::string& path);

  /**
   * @brief Read from raw bytes.
   * @param data Pointer to data
   * @param size Size in bytes
   * @return true on success, false on error
   */
  bool read(const uint8_t* data, size_t size);

  /**
   * @brief Get the parsed MIDI 2.0 data.
   * @return Reference to ParsedMidi2
   */
  const ParsedMidi2& getParsedMidi() const { return midi_; }

  /**
   * @brief Get error message if read() failed.
   * @return Error string
   */
  const std::string& getError() const { return error_; }

  /**
   * @brief Check if data looks like MIDI 2.0 format.
   * @param data Pointer to data
   * @param size Size in bytes
   * @return true if ktmidi container or SMF2CLIP
   */
  static bool isMidi2Format(const uint8_t* data, size_t size);

 private:
  bool parseContainer(const uint8_t* data, size_t size);
  bool parseClip(const uint8_t* data, size_t size);
  void parseUmpMessages(const uint8_t* data, size_t size, size_t offset);
  void extractMetadataFromSysEx8(const uint8_t* data, size_t len);

  ParsedMidi2 midi_;
  std::string error_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_MIDI2_READER_H
