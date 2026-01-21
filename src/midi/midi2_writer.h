/**
 * @file midi2_writer.h
 * @brief MIDI 2.0 Clip and Container file writer using UMP messages.
 */

#ifndef MIDISKETCH_MIDI2_WRITER_H
#define MIDISKETCH_MIDI2_WRITER_H

#include <string>
#include <vector>

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"

namespace midisketch {

// MIDI 2.0 Clip/Container File Writer
// Supports:
// - SMF2 Clip File (single track, "SMF2CLIP" header)
// - ktmidi Container File (multi-track, "AAAAAAAAEEEEEEEE" header)
class Midi2Writer {
 public:
  Midi2Writer();

  // Build a single-track clip file from MidiTrack
  // Output format: SMF2CLIP
  void buildClip(const MidiTrack& track, const std::string& name, uint8_t channel, uint8_t program,
                 uint16_t bpm, Key key, Tick mod_tick = 0, int8_t mod_amount = 0);

  // Build a multi-track container file from Song
  // Output format: ktmidi container (AAAAAAAAEEEEEEEE header)
  void buildContainer(const Song& song, Key key, const std::string& metadata = "");

  // Returns the MIDI 2.0 data as a byte vector
  std::vector<uint8_t> toBytes() const;

  // Writes MIDI 2.0 data to a file
  bool writeToFile(const std::string& path) const;

 private:
  std::vector<uint8_t> data_;

  // Write ktmidi container header
  void writeContainerHeader(uint16_t numTracks, uint16_t ticksPerQuarter);

  // Write SMF2CLIP header
  void writeClipHeader();

  // Write clip configuration (DCTPQ, tempo, time sig, Start of Clip)
  void writeClipConfig(uint16_t ticksPerQuarter, uint16_t bpm);

  // Write track data as UMP messages with delta clockstamps
  void writeTrackData(const MidiTrack& track, uint8_t group, uint8_t channel, uint8_t program,
                      Key key, Tick mod_tick, int8_t mod_amount);

  // Write marker/text events (for SE track)
  void writeMarkerData(const MidiTrack& track, uint8_t group, uint16_t bpm,
                       const std::string& metadata);
};

}  // namespace midisketch

#endif  // MIDISKETCH_MIDI2_WRITER_H
