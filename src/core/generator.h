#ifndef MIDISKETCH_CORE_GENERATOR_H
#define MIDISKETCH_CORE_GENERATOR_H

#include "core/types.h"
#include <random>

namespace midisketch {

// Main generator class for MIDI content creation.
// Generates all tracks (vocal, chord, bass, drums, SE) based on input parameters.
class Generator {
 public:
  Generator();

  // Generates all tracks with the given parameters.
  // @param params Generation parameters
  void generate(const GeneratorParams& params);

  // Regenerates only the melody track with a new seed.
  // Other tracks remain unchanged.
  // @param new_seed New random seed (0 = auto)
  void regenerateMelody(uint32_t new_seed = 0);

  // Returns the current generation result.
  // @returns Reference to GenerationResult
  const GenerationResult& getResult() const { return result_; }

  // Returns the current generation parameters.
  // @returns Reference to GeneratorParams
  const GeneratorParams& getParams() const { return params_; }

 private:
  GeneratorParams params_;
  GenerationResult result_;
  std::mt19937 rng_;

  void generateVocal();
  void generateChord();
  void generateBass();
  void generateDrums();
  void generateSE();

  // Calculates modulation point and amount based on structure and mood.
  void calculateModulation();

  // Resolves seed value (0 = generate from clock).
  // @param seed Input seed
  // @returns Resolved seed value
  uint32_t resolveSeed(uint32_t seed);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_GENERATOR_H
