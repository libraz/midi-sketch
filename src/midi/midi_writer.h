#ifndef MIDISKETCH_MIDI_WRITER_H
#define MIDISKETCH_MIDI_WRITER_H

#include "core/types.h"
#include <string>
#include <vector>

namespace midisketch {

// Writes MIDI data in SMF Type 1 format.
class MidiWriter {
 public:
  MidiWriter();

  // Builds MIDI data from a generation result.
  // @param result Generation result containing all tracks
  // @param key Output key for transposition
  void build(const GenerationResult& result, Key key);

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
  // @param num_tracks Number of tracks
  // @param division Ticks per quarter note
  void writeHeader(uint16_t num_tracks, uint16_t division);

  // Writes a single track chunk.
  // @param track Track data
  // @param name Track name
  // @param bpm Tempo in BPM
  // @param key Key for transposition
  // @param is_first_track true if this is the first track (includes tempo)
  // @param mod_tick Tick where modulation starts (0 = no modulation)
  // @param mod_amount Modulation amount in semitones
  void writeTrack(const TrackData& track, const std::string& name,
                  uint16_t bpm, Key key, bool is_first_track,
                  Tick mod_tick = 0, int8_t mod_amount = 0);

  // Writes a variable-length quantity to buffer.
  // @param buf Output buffer
  // @param value Value to encode
  static void writeVariableLength(std::vector<uint8_t>& buf, uint32_t value);

  // Transposes a pitch by the given key.
  // @param pitch Original MIDI pitch
  // @param key Key offset
  // @returns Transposed pitch (clamped to 0-127)
  static uint8_t transposePitch(uint8_t pitch, Key key);

  // Writes a marker track containing text events.
  // @param markers Vector of text events
  // @param bpm Tempo in BPM
  void writeMarkerTrack(const std::vector<TextEvent>& markers, uint16_t bpm);
};

}  // namespace midisketch

#endif  // MIDISKETCH_MIDI_WRITER_H
