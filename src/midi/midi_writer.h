#ifndef MIDISKETCH_MIDI_WRITER_H
#define MIDISKETCH_MIDI_WRITER_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"
#include <string>
#include <vector>

namespace midisketch {

// Writes MIDI data in SMF Type 1 format.
// This class is intentionally "dumb" - it only handles byte-level output.
class MidiWriter {
 public:
  MidiWriter();

  // Builds MIDI data from a Song.
  // @param song Song containing all tracks
  // @param key Output key for transposition
  void build(const Song& song, Key key);

  // Returns the MIDI data as a byte vector.
  // @returns MIDI binary data
  std::vector<uint8_t> toBytes() const;

  // Writes MIDI data to a file.
  // @param path Output file path
  // @returns true on success, false on failure
  bool writeToFile(const std::string& path) const;

 private:
  std::vector<uint8_t> data_;

  // Writes the MIDI file header chunk.
  void writeHeader(uint16_t num_tracks, uint16_t division);

  // Writes a single track chunk from MidiTrack.
  void writeTrack(const MidiTrack& track, const std::string& name,
                  uint8_t channel, uint8_t program, uint16_t bpm, Key key,
                  bool is_first_track, Tick mod_tick = 0, int8_t mod_amount = 0);

  // Writes a marker/SE track containing text events.
  void writeMarkerTrack(const MidiTrack& track, uint16_t bpm);

  // Writes a variable-length quantity to buffer.
  static void writeVariableLength(std::vector<uint8_t>& buf, uint32_t value);

  // Transposes a pitch by the given key.
  static uint8_t transposePitch(uint8_t pitch, Key key);
};

}  // namespace midisketch

#endif  // MIDISKETCH_MIDI_WRITER_H
