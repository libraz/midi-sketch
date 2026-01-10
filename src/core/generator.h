/**
 * @file generator.h
 * @brief Main MIDI generator class for complete song creation.
 */

#ifndef MIDISKETCH_CORE_GENERATOR_H
#define MIDISKETCH_CORE_GENERATOR_H

#include "core/harmony_context.h"
#include "core/motif.h"
#include "core/song.h"
#include "core/types.h"
#include <optional>
#include <random>

namespace midisketch {

/**
 * @brief Main generator class for MIDI content creation.
 *
 * Creates 8 tracks: Vocal, Chord, Bass, Drums, Motif, Arpeggio, Aux, SE.
 *
 * Workflows:
 * - generate(): Standard (Chord→Bass→Vocal)
 * - generateWithVocal(): Vocal-first (Vocal→Bass→Chord)
 * - generateVocal() + generateAccompanimentForVocal(): Trial-and-error
 */
class Generator {
 public:
  Generator();

  /// @name Standard Generation
  /// @{

  /**
   * @brief Generate all tracks with the given parameters.
   * @param params Generation parameters (key, tempo, mood, etc.)
   */
  void generate(const GeneratorParams& params);

  /**
   * @brief Generate all tracks from a SongConfig.
   * @param config Song configuration with all settings
   */
  void generateFromConfig(const SongConfig& config);

  /// @}
  /// @name Vocal-First Generation (Trial-and-Error Workflow)
  /// @{

  /**
   * @brief Generate only the vocal track without accompaniment.
   * @param params Generation parameters
   */
  void generateVocal(const GeneratorParams& params);

  /**
   * @brief Generate accompaniment tracks for existing vocal.
   * @pre Must be called after generateVocal() or generateWithVocal()
   */
  void generateAccompanimentForVocal();

  /**
   * @brief Generate accompaniment tracks with configuration.
   *
   * Generates: Aux → Bass → Chord → Drums → Arpeggio → Motif → SE
   * Accompaniment adapts to existing vocal.
   *
   * @param config Accompaniment configuration
   * @pre Must have existing vocal (call generateVocal() first)
   */
  void generateAccompanimentForVocal(const AccompanimentConfig& config);

  /**
   * @brief Regenerate vocal track with a new seed.
   * @param new_seed New random seed (0 = auto-generate from clock)
   */
  void regenerateVocal(uint32_t new_seed = 0);

  /**
   * @brief Regenerate vocal track with new configuration.
   * @param config Vocal configuration with all parameters
   */
  void regenerateVocal(const VocalConfig& config);

  /**
   * @brief Generate all tracks with vocal-first priority.
   *
   * Vocal→Bass→Chord order. Accompaniment adapts to melody.
   *
   * @param params Generation parameters
   */
  void generateWithVocal(const GeneratorParams& params);

  /**
   * @brief Set melody from saved MelodyData.
   * @param melody Saved melody data to restore
   */
  void setMelody(const MelodyData& melody);

  /**
   * @brief Set custom vocal notes for accompaniment generation.
   *
   * Initializes the song structure and chord progression from params,
   * then replaces the vocal track with the provided notes.
   * Call generateAccompanimentForVocal() after this.
   *
   * @param params Generation parameters (for structure/chord setup)
   * @param notes Vector of NoteEvent representing the custom vocal
   */
  void setVocalNotes(const GeneratorParams& params, const std::vector<NoteEvent>& notes);

  /**
   * @brief Regenerate accompaniment tracks with a new seed.
   *
   * Keeps current vocal, clears and regenerates all accompaniment tracks
   * (Aux, Bass, Chord, Drums, Arpeggio, Motif, SE) with the specified seed.
   *
   * @param new_seed New random seed for accompaniment (0 = auto-generate)
   * @pre Must have existing vocal (call generateVocal() first)
   */
  void regenerateAccompaniment(uint32_t new_seed = 0);

  /**
   * @brief Regenerate accompaniment tracks with configuration.
   *
   * Keeps current vocal, clears and regenerates all accompaniment tracks
   * with the specified configuration.
   *
   * @param config Accompaniment configuration
   * @pre Must have existing vocal (call generateVocal() first)
   */
  void regenerateAccompaniment(const AccompanimentConfig& config);

  /// @}
  /// @name Motif Control
  /// @{

  /**
   * @brief Regenerate motif track with a new seed.
   * @param new_seed New random seed (0 = auto)
   */
  void regenerateMotif(uint32_t new_seed = 0);

  /** @brief Get current motif data for saving. */
  MotifData getMotif() const;

  /** @brief Set motif from saved MotifData. */
  void setMotif(const MotifData& motif);

  /// @}
  /// @name Accessors
  /// @{

  /** @brief Get the generated song. */
  const Song& getSong() const { return song_; }

  /** @brief Get current generation parameters. */
  const GeneratorParams& getParams() const { return params_; }

  /** @brief Get harmony context (for external collision checking). */
  const HarmonyContext& getHarmonyContext() const { return harmony_context_; }

  /**
   * @brief Set modulation timing for key change.
   * @param timing When to modulate (None, LastChorus, AfterBridge)
   * @param semitones Steps to modulate up (1-4, typically 2)
   */
  void setModulationTiming(ModulationTiming timing, int8_t semitones = 2) {
    modulation_timing_ = timing;
    modulation_semitones_ = semitones;
  }

  /// @}

 private:
  /// @name State
  /// @{
  GeneratorParams params_;           ///< Current generation parameters
  Song song_;                        ///< Generated song data
  std::mt19937 rng_;                 ///< Random number generator (Mersenne Twister)
  HarmonyContext harmony_context_;   ///< Tracks notes for collision avoidance
  /// @}

  /// @name Call/SE System Settings
  /// Stored from SongConfig for SE track generation
  /// @{
  bool se_enabled_ = true;           ///< Enable SE (sound effect) track
  bool call_enabled_ = false;        ///< Enable call-and-response patterns
  bool call_notes_enabled_ = true;   ///< Include pitched notes in calls
  IntroChant intro_chant_ = IntroChant::None;  ///< Intro chant style
  MixPattern mix_pattern_ = MixPattern::None;  ///< Mix breakdown pattern
  CallDensity call_density_ = CallDensity::Standard;  ///< Call frequency
  /// @}

  /// @name Modulation Settings
  /// @{
  ModulationTiming modulation_timing_ = ModulationTiming::None;
  int8_t modulation_semitones_ = 2;  ///< Key change amount (1-4 semitones)
  /// @}

  /// Cached chorus motif for intro placement (enables "teaser" riffs)
  std::optional<Motif> cached_chorus_motif_;

  /// @name Track Generation Methods
  /// Each generates a single track and registers notes with HarmonyContext
  /// @{
  void generateVocal();     ///< Main melody track
  void generateChord();     ///< Chord voicing track
  void generateBass();      ///< Bass line track
  void generateDrums();     ///< Drum pattern track
  void generateSE();        ///< Sound effects (calls, chants)
  void generateMotif();     ///< Background motif track
  void generateArpeggio();  ///< Arpeggio pattern track
  void generateAux();       ///< Auxiliary melody track
  /// @}

  /// @name Post-Processing Methods
  /// @{

  /** @brief Resolve chord-arpeggio clashes for BGM-only mode. */
  void resolveArpeggioChordClashes();

  /** @brief Rebuild motif track from stored pattern data. */
  void rebuildMotifFromPattern();

  /** @brief Calculate modulation point and amount from structure/mood. */
  void calculateModulation();

  /** @brief Apply transition dynamics to melodic tracks. */
  void applyTransitionDynamics();

  /** @brief Apply humanization to all melodic tracks. */
  void applyHumanization();

  /**
   * @brief Resolve seed value (0 = generate from system clock).
   * @param seed Input seed (0 for auto)
   * @return Resolved seed value
   */
  uint32_t resolveSeed(uint32_t seed);

  /// @}
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_GENERATOR_H
