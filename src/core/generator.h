/**
 * @file generator.h
 * @brief Main MIDI generator class for complete song creation.
 */

#ifndef MIDISKETCH_CORE_GENERATOR_H
#define MIDISKETCH_CORE_GENERATOR_H

#include "core/i_harmony_context.h"
#include "core/motif.h"
#include "core/production_blueprint.h"
#include "core/section_types.h"
#include "core/song.h"
#include "core/types.h"
#include <memory>
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

  /**
   * @brief Construct with injected HarmonyContext (for testing).
   * @param harmony_context Custom harmony context implementation
   */
  explicit Generator(std::unique_ptr<IHarmonyContext> harmony_context);

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

  /** @brief Get the generated song (const). */
  const Song& getSong() const { return song_; }

  /** @brief Get the generated song (mutable, for Strategy pattern). */
  Song& getSong() { return song_; }

  /** @brief Get current generation parameters. */
  const GeneratorParams& getParams() const { return params_; }

  /** @brief Get harmony context (for external collision checking). */
  const IHarmonyContext& getHarmonyContext() const { return *harmony_context_; }

  /** @brief Get harmony context (mutable, for Strategy pattern). */
  IHarmonyContext& getHarmonyContext() { return *harmony_context_; }

  /** @brief Get random number generator (mutable, for Strategy pattern). */
  std::mt19937& getRng() { return rng_; }

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
  /// @name Strategy Invocation Methods
  /// Used by CompositionStrategy to invoke track generation
  /// @{

  /** @brief Invoke vocal track generation. */
  void invokeGenerateVocal() { generateVocal(); }

  /** @brief Invoke bass track generation. */
  void invokeGenerateBass() { generateBass(); }

  /** @brief Invoke chord track generation. */
  void invokeGenerateChord() { generateChord(); }

  /** @brief Invoke motif track generation. */
  void invokeGenerateMotif() { generateMotif(); }

  /** @brief Invoke aux track generation. */
  void invokeGenerateAux() { generateAux(); }

  /** @brief Get resolved blueprint ID after generation.
   *  @return Blueprint ID (0-3), or 0 if not generated */
  uint8_t resolvedBlueprintId() const { return resolved_blueprint_id_; }

  /** @brief Get pre-computed drum grid for RhythmSync paradigm.
   *  @return Pointer to DrumGrid, or nullptr if not RhythmSync */
  const DrumGrid* getDrumGrid() const {
    return drum_grid_.has_value() ? &drum_grid_.value() : nullptr;
  }

  // ============================================================================
  // Rhythm Lock Support (Orangestar style)
  // ============================================================================

  /**
   * @brief Generate Motif track and extract rhythm as coordinate axis.
   *
   * For Orangestar style (RhythmSync + Locked), Motif provides the
   * rhythmic "coordinate axis" that Vocal will follow.
   * Call this BEFORE generateVocal() when using rhythm lock.
   */
  void generateMotifAsAxis();

  /**
   * @brief Check if rhythm lock is active and should be used.
   * @return True if RhythmSync paradigm with Locked policy
   */
  bool shouldUseRhythmLock() const;

  /**
   * @brief Apply density progression to sections for Orangestar style.
   *
   * For RhythmSync paradigm, increases density_percent for each
   * occurrence of the same section type (e.g., 2nd Chorus is denser
   * than 1st Chorus). Creates "Peak is a temporal event" effect.
   */
  void applyDensityProgression();

  /// @}

 private:
  /// @name State
  /// @{
  GeneratorParams params_;           ///< Current generation parameters
  Song song_;                        ///< Generated song data
  std::mt19937 rng_;                 ///< Random number generator (Mersenne Twister)
  std::unique_ptr<IHarmonyContext> harmony_context_;  ///< Tracks notes for collision avoidance

  /// Blueprint state
  uint8_t resolved_blueprint_id_ = 0;           ///< Resolved blueprint ID after selection
  const ProductionBlueprint* blueprint_ = nullptr;  ///< Pointer to selected blueprint

  /// RhythmSync state
  std::optional<DrumGrid> drum_grid_;  ///< Drum grid for RhythmSync (pre-computed)

  /// Rhythm lock state (Orangestar style)
  bool rhythm_lock_active_ = false;  ///< True when Motif rhythm is used as axis
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

  /// @name RhythmSync Methods
  /// @{

  /** @brief Pre-compute drum grid for RhythmSync paradigm.
   *  Called before vocal generation, does NOT generate drum notes. */
  void computeDrumGrid();

  /// @}

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
