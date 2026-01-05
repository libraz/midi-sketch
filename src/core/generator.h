#ifndef MIDISKETCH_CORE_GENERATOR_H
#define MIDISKETCH_CORE_GENERATOR_H

#include "core/harmony_context.h"
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

  // Generates all tracks from a SongConfig (new API).
  // Converts SongConfig to GeneratorParams and generates.
  // @param config Song configuration
  void generateFromConfig(const SongConfig& config);

  // Regenerates only the melody track with a new seed.
  // Other tracks remain unchanged.
  // @param new_seed New random seed (0 = auto)
  void regenerateMelody(uint32_t new_seed = 0);

  // Regenerates only the melody track with full parameter control.
  // Updates vocal range, attitude, and composition style before regenerating.
  // Other tracks (chord, bass, drums, arpeggio) remain unchanged.
  // @param params MelodyRegenerateParams with all required fields
  void regenerateMelody(const MelodyRegenerateParams& params);

  // Regenerates only the vocal track with updated VocalAttitude.
  // Other tracks remain unchanged.
  // Uses the VocalAttitude and StyleMelodyParams from the config.
  // @param config SongConfig containing the new VocalAttitude
  // @param new_seed New random seed (0 = keep current seed)
  void regenerateVocalFromConfig(const SongConfig& config, uint32_t new_seed = 0);

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

  // Sets modulation timing (for use before calling generate()).
  // @param timing ModulationTiming value
  // @param semitones Semitones to modulate (1-4, default 2)
  void setModulationTiming(ModulationTiming timing, int8_t semitones = 2) {
    modulation_timing_ = timing;
    modulation_semitones_ = semitones;
  }

 private:
  GeneratorParams params_;
  Song song_;
  std::mt19937 rng_;
  HarmonyContext harmony_context_;

  // Call system settings (stored from SongConfig)
  bool call_enabled_ = false;
  bool call_notes_enabled_ = true;
  IntroChant intro_chant_ = IntroChant::None;
  MixPattern mix_pattern_ = MixPattern::None;
  CallDensity call_density_ = CallDensity::Standard;

  // Modulation settings (stored from SongConfig)
  ModulationTiming modulation_timing_ = ModulationTiming::None;
  int8_t modulation_semitones_ = 2;

  void generateVocal();
  void generateChord();
  void generateBass();
  void generateDrums();
  void generateSE();
  void generateMotif();
  void generateArpeggio();

  // Rebuilds motif track from stored pattern.
  void rebuildMotifFromPattern();

  // Calculates modulation point and amount based on structure and mood.
  void calculateModulation();

  // Applies transition dynamics (crescendo/decrescendo) to all melodic tracks.
  void applyTransitionDynamics();

  // Applies humanization (timing/velocity variation) to all melodic tracks.
  void applyHumanization();

  // Resolves seed value (0 = generate from clock).
  // @param seed Input seed
  // @returns Resolved seed value
  uint32_t resolveSeed(uint32_t seed);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_GENERATOR_H
