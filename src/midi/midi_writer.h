/**
 * @file midi_writer.h
 * @brief SMF Type 1 and MIDI 2.0 file writer.
 */

#ifndef MIDISKETCH_MIDI_WRITER_H
#define MIDISKETCH_MIDI_WRITER_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"
#include <memory>
#include <string>
#include <vector>

namespace midisketch {

class HarmonyContext;

#ifndef MIDISKETCH_WASM
class Midi2Writer;
#endif

/// @brief MIDI file writer (SMF Type 1 and MIDI 2.0).
class MidiWriter {
 public:
  MidiWriter();
  ~MidiWriter();

  /**
   * @brief Build MIDI data from a Song.
   * @param song Song containing all tracks
   * @param key Output key for transposition
   * @param metadata Optional JSON metadata to embed
   * @param format MIDI format (SMF1 or SMF2)
   */
  void build(const Song& song, Key key, const std::string& metadata = "",
             MidiFormat format = kDefaultMidiFormat);

  /**
   * @brief Build vocal preview MIDI (vocal + root bass only).
   * @param song Song containing vocal track
   * @param harmony HarmonyContext for chord root extraction
   * @param key Output key for transposition
   */
  void buildVocalPreview(const Song& song, const HarmonyContext& harmony,
                         Key key);

  /** @brief Get MIDI data as byte vector. @return Binary MIDI data */
  std::vector<uint8_t> toBytes() const;

  /** @brief Write MIDI to file. @param path Output path @return Success */
  bool writeToFile(const std::string& path) const;

 private:
  std::vector<uint8_t> data_;
#ifndef MIDISKETCH_WASM
  std::unique_ptr<Midi2Writer> midi2_writer_;
#endif

  void buildSMF1(const Song& song, Key key, const std::string& metadata);
#ifndef MIDISKETCH_WASM
  void buildSMF2(const Song& song, Key key, const std::string& metadata);
#endif
  void writeHeader(uint16_t num_tracks, uint16_t division);
  void writeTrack(const MidiTrack& track, const std::string& name,
                  uint8_t channel, uint8_t program, uint16_t bpm, Key key,
                  bool is_first_track, Tick mod_tick = 0, int8_t mod_amount = 0);
  void writeMarkerTrack(const MidiTrack& track, uint16_t bpm,
                        const std::string& metadata = "");
  static void writeVariableLength(std::vector<uint8_t>& buf, uint32_t value);
  static uint8_t transposePitch(uint8_t pitch, Key key);
};

}  // namespace midisketch

#endif  // MIDISKETCH_MIDI_WRITER_H
