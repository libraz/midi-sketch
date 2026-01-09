/**
 * @file midisketch.h
 * @brief High-level API for MIDI generation.
 */

#ifndef MIDISKETCH_H
#define MIDISKETCH_H

#include "core/generator.h"
#include "core/song.h"
#include "core/types.h"
#include "midi/midi_writer.h"

namespace midisketch {

/// @brief High-level API wrapping Generator and MidiWriter.
class MidiSketch {
 public:
  MidiSketch();
  ~MidiSketch();

  /**
   * @brief Generate MIDI with the given parameters.
   * @param params Generation parameters
   */
  void generate(const GeneratorParams& params);

  /**
   * @brief Generate MIDI from a SongConfig.
   * @param config Song configuration
   */
  void generateFromConfig(const SongConfig& config);

  /**
   * @brief Regenerate only the melody with a new seed.
   * @param new_seed Random seed (0 = auto)
   */
  void regenerateMelody(uint32_t new_seed = 0);

  /**
   * @brief Regenerate melody with full parameter control.
   *
   * Updates vocal range, attitude, and composition style.
   * Other tracks (chord, bass, drums, arpeggio) remain unchanged.
   * @param params MelodyRegenerateParams with all required fields
   */
  void regenerateMelody(const MelodyRegenerateParams& params);

  /**
   * @brief Regenerate vocal track with updated VocalAttitude.
   * @param config SongConfig containing the new VocalAttitude
   * @param new_seed Random seed (0 = keep current seed)
   */
  void regenerateVocalFromConfig(const SongConfig& config, uint32_t new_seed = 0);

  /**
   * @brief Get current melody data (seed + notes).
   * @return MelodyData for saving/comparing candidates
   */
  MelodyData getMelody() const;

  /**
   * @brief Set melody from saved MelodyData.
   * @param melody MelodyData to apply to vocal track
   */
  void setMelody(const MelodyData& melody);

  /**
   * @brief Set MIDI output format.
   * @param format MidiFormat::SMF1 or MidiFormat::SMF2
   */
  void setMidiFormat(MidiFormat format);

  /**
   * @brief Get current MIDI format.
   * @return Current MidiFormat
   */
  MidiFormat getMidiFormat() const;

  /**
   * @brief Get MIDI data as byte vector.
   * @return MIDI binary data
   */
  std::vector<uint8_t> getMidi() const;

  /**
   * @brief Get event data as JSON string.
   * @return JSON string for playback/display
   */
  std::string getEventsJson() const;

  /**
   * @brief Get generated song.
   * @return Reference to Song
   */
  const Song& getSong() const;

  /**
   * @brief Get generation parameters.
   * @return Reference to GeneratorParams
   */
  const GeneratorParams& getParams() const;

  /**
   * @brief Get harmony context for piano roll safety API.
   * @return Reference to HarmonyContext
   */
  const HarmonyContext& getHarmonyContext() const;

  /**
   * @brief Get library version string.
   * @return Version string (e.g., "0.1.0")
   */
  static const char* version();

 private:
  Generator generator_;
  MidiWriter midi_writer_;
  MidiFormat midi_format_ = kDefaultMidiFormat;
};

}  // namespace midisketch

#endif  // MIDISKETCH_H
