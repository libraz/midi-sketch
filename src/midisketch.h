#ifndef MIDISKETCH_H
#define MIDISKETCH_H

#include "core/generator.h"
#include "core/song.h"
#include "core/types.h"
#include "midi/midi_writer.h"

namespace midisketch {

// High-level API for MIDI generation.
// Wraps Generator and MidiWriter for convenient usage.
class MidiSketch {
 public:
  MidiSketch();
  ~MidiSketch();

  // Generates MIDI with the given parameters.
  // @param params Generation parameters
  void generate(const GeneratorParams& params);

  // Generates MIDI from a SongConfig (new API).
  // @param config Song configuration
  void generateFromConfig(const SongConfig& config);

  // Regenerates only the melody with a new seed.
  // @param new_seed New random seed (0 = auto)
  void regenerateMelody(uint32_t new_seed = 0);

  // Regenerates only the melody with full parameter control.
  // Updates vocal range, attitude, and composition style before regenerating.
  // Other tracks (chord, bass, drums, arpeggio) remain unchanged.
  // @param params MelodyRegenerateParams with all required fields
  void regenerateMelody(const MelodyRegenerateParams& params);

  // Regenerates the vocal track with updated VocalAttitude.
  // @param config SongConfig containing the new VocalAttitude
  // @param new_seed New random seed (0 = keep current seed)
  void regenerateVocalFromConfig(const SongConfig& config, uint32_t new_seed = 0);

  // Returns the current melody data (seed + notes).
  // Use this to save melody candidates for later comparison.
  // @returns MelodyData containing seed and notes
  MelodyData getMelody() const;

  // Sets the melody from saved MelodyData.
  // Replaces the current vocal track with the saved melody.
  // @param melody MelodyData to apply
  void setMelody(const MelodyData& melody);

  // Returns the MIDI data as a byte vector.
  // @returns MIDI binary data (SMF Type 1)
  std::vector<uint8_t> getMidi() const;

  // Returns the event data as a JSON string.
  // @returns JSON string for playback/display
  std::string getEventsJson() const;

  // Returns the generated song.
  // @returns Reference to Song
  const Song& getSong() const;

  // Returns the generation parameters.
  // @returns Reference to GeneratorParams
  const GeneratorParams& getParams() const;

  // Returns the library version string.
  // @returns Version string (e.g., "0.1.0")
  static const char* version();

 private:
  Generator generator_;
  MidiWriter midi_writer_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_H
