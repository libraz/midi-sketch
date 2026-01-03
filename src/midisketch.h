#ifndef MIDISKETCH_H
#define MIDISKETCH_H

#include "core/types.h"
#include "core/generator.h"
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

  // Regenerates only the melody with a new seed.
  // @param new_seed New random seed (0 = auto)
  void regenerateMelody(uint32_t new_seed = 0);

  // Returns the MIDI data as a byte vector.
  // @returns MIDI binary data (SMF Type 1)
  std::vector<uint8_t> getMidi() const;

  // Returns the event data as a JSON string.
  // @returns JSON string for playback/display
  std::string getEventsJson() const;

  // Returns the generation result.
  // @returns Reference to GenerationResult
  const GenerationResult& getResult() const;

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
