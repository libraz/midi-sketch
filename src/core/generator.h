#ifndef MIDISKETCH_CORE_GENERATOR_H
#define MIDISKETCH_CORE_GENERATOR_H

#include "core/song.h"
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

  // Sets the melody from saved MelodyData.
  // Replaces the current vocal track with the saved melody.
  // @param melody MelodyData to apply
  void setMelody(const MelodyData& melody);

  // Regenerates only the motif track with a new seed.
  // Other tracks remain unchanged.
  // @param new_seed New random seed (0 = auto)
  void regenerateMotif(uint32_t new_seed = 0);

  // Returns current motif data for saving.
  // @returns MotifData containing seed and pattern
  MotifData getMotif() const;

  // Sets the motif from saved MotifData.
  // Replaces the current motif track with the saved motif.
  // @param motif MotifData to apply
  void setMotif(const MotifData& motif);

  // Returns the generated song.
  // @returns Reference to Song
  const Song& getSong() const { return song_; }

  // Returns the current generation parameters.
  // @returns Reference to GeneratorParams
  const GeneratorParams& getParams() const { return params_; }

 private:
  GeneratorParams params_;
  Song song_;
  std::mt19937 rng_;

  void generateVocal();
  void generateChord();
  void generateBass();
  void generateDrums();
  void generateSE();
  void generateMotif();

  // Rebuilds motif track from stored pattern.
  void rebuildMotifFromPattern();

  // Calculates modulation point and amount based on structure and mood.
  void calculateModulation();

  // Resolves seed value (0 = generate from clock).
  // @param seed Input seed
  // @returns Resolved seed value
  uint32_t resolveSeed(uint32_t seed);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_GENERATOR_H
