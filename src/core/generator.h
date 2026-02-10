/**
 * @file generator.h
 * @brief Main MIDI generator class for complete song creation.
 */

#ifndef MIDISKETCH_CORE_GENERATOR_H
#define MIDISKETCH_CORE_GENERATOR_H

#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "core/coordinator.h"
#include "core/emotion_curve.h"
#include "core/i_harmony_context.h"
#include "core/motif.h"
#include "core/production_blueprint.h"
#include "core/section_types.h"
#include "core/song.h"
#include "core/types.h"

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
   * @brief Construct with injected HarmonyCoordinator (for testing).
   * @param harmony_context Custom harmony coordinator implementation
   */
  explicit Generator(std::unique_ptr<IHarmonyCoordinator> harmony_context);

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

  /** @brief Get emotion curve (for track generation guidance). */
  const EmotionCurve& getEmotionCurve() const { return emotion_curve_; }

  /**
   * @brief Set modulation timing for key change.
   * @param timing When to modulate (None, LastChorus, AfterBridge)
   * @param semitones Steps to modulate up (1-4, typically 2)
   */
  void setModulationTiming(ModulationTiming timing, int8_t semitones = 2) {
    params_.modulation_timing = timing;
    params_.modulation_semitones = semitones;
  }

  /** @brief Get warnings generated during last generation.
   *  @return Vector of warning messages */
  const std::vector<std::string>& getWarnings() const { return warnings_; }

  /** @brief Check if there are any warnings. */
  bool hasWarnings() const { return !warnings_.empty(); }

  /** @brief Clear all warnings. */
  void clearWarnings() { warnings_.clear(); }

  /// @}
  /// @name Accessors
  /// @{

  /** @brief Get resolved blueprint ID after generation.
   *  @return Blueprint ID (0-3), or 0 if not generated */
  uint8_t resolvedBlueprintId() const { return resolved_blueprint_id_; }

  /** @brief Get pre-computed drum grid for RhythmSync paradigm.
   *  @return Pointer to DrumGrid, or nullptr if not RhythmSync */
  const DrumGrid* getDrumGrid() const {
    return drum_grid_.has_value() ? &drum_grid_.value() : nullptr;
  }

  /// @}
  /// @name Vocal-First Refinement
  /// @{

  /**
   * @brief Represents a clash between vocal and accompaniment tracks.
   *
   * Used by refineVocalForAccompaniment() to identify and resolve conflicts.
   */
  struct VocalClash {
    Tick tick;               ///< Position of the clash
    uint8_t vocal_pitch;     ///< Current vocal pitch
    uint8_t clashing_pitch;  ///< Pitch from accompaniment track
    TrackRole clashing_track;  ///< Which track is clashing
    uint8_t suggested_pitch;   ///< Suggested safe pitch
  };

  /**
   * @brief Refine vocal track after accompaniment generation.
   *
   * Detects clashes between vocal and accompaniment (minor 2nd, major 7th),
   * and adjusts vocal pitches to minimize dissonance while preserving melody.
   * Called automatically at the end of generateWithVocal().
   *
   * @param max_iterations Maximum refinement passes (default 2)
   * @return Number of notes adjusted
   */
  int refineVocalForAccompaniment(int max_iterations = 2);

  /// @}

 private:
  /// @name State
  /// @{
  GeneratorParams params_;  ///< Current generation parameters
  Song song_;               ///< Generated song data
  std::mt19937 rng_;        ///< Random number generator (Mersenne Twister)
  std::unique_ptr<IHarmonyCoordinator> harmony_context_;  ///< Harmony coordinator for collision avoidance

  /// Blueprint state
  uint8_t resolved_blueprint_id_ = 0;               ///< Resolved blueprint ID after selection
  const ProductionBlueprint* blueprint_ = nullptr;  ///< Pointer to selected blueprint

  /// RhythmSync state
  std::optional<DrumGrid> drum_grid_;  ///< Drum grid for RhythmSync (pre-computed)

  /// Bass-Kick sync state
  std::optional<KickPatternCache> kick_cache_;  ///< Pre-computed kick positions for bass sync

  /// Emotion curve for song-wide emotional planning
  EmotionCurve emotion_curve_;  ///< Planned emotional arc

  /// Coordinator for track generation
  std::unique_ptr<Coordinator> coordinator_;
  /// @}

  // Call/SE and Modulation settings are stored directly in params_.
  // See GeneratorParams::se_enabled, call_enabled, call_notes_enabled,
  // intro_chant, mix_pattern, call_density, modulation_timing, modulation_semitones.

  /// @name Warnings
  /// @{
  std::vector<std::string> warnings_;  ///< Accumulated warnings during generation
  /// @}

  /// @name Generation Phase Helpers
  /// @{

  /** @brief Accept incoming params while preserving pre-set state.
   *
   *  Preserves modulation timing set via setModulationTiming() if the
   *  incoming params has default modulation values.
   *
   *  @param params Incoming generation parameters */
  void acceptParams(const GeneratorParams& params);

  /** @brief Initialize all generation state (seed, blueprint, BPM, structure).
   *  @return Resolved BPM value */
  uint16_t initializeGenerationState();

  /** @brief Generate all tracks via Coordinator.
   *  Prepares params and calls coordinator_->generateAllTracks(). */
  void generateAllTracksViaCoordinator();

  /** @brief Apply all post-processing effects.
   *  Staggered entry, layer schedule, dynamics, expression, humanization. */
  void applyPostProcessingEffects();

  /// @}
  /// @name Initialization Helpers (reduce code duplication)
  /// @{

  /** @brief Initialize blueprint from seed.
   *  Selects blueprint, copies settings to params_, forces drums if required. */
  void initializeBlueprint(uint32_t seed);

  /** @brief Configure motif parameters for RhythmSync paradigm.
   *  Sets rhythm_density=Driving, note_count=8, length=1bar. */
  void configureRhythmSyncMotif();

  /** @brief Configure motif parameters for Behavioral Loop mode.
   *  Sets rhythm_density=Driving, note_count=8, length=1bar for tight loops. */
  void configureAddictiveMotif();

  /** @brief Validate and normalize vocal range parameters.
   *  Swaps low/high if inverted, clamps to valid MIDI range. */
  void validateVocalRange();

  /** @brief Resolve BPM from params and clamp for paradigm constraints.
   *
   *  Resolves BPM=0 to mood default, clamps for RhythmSync paradigm,
   *  stores result in params_.bpm and song_.setBpm().
   *
   *  @return Resolved and clamped BPM value */
  uint16_t resolveAndClampBpm();

  /** @brief Apply AccompanimentConfig to params_ and internal state.
   *  @param config Accompaniment configuration to apply */
  void applyAccompanimentConfig(const AccompanimentConfig& config);

  /** @brief Clear all accompaniment tracks and re-register vocal.
   *  Clears: Aux, Bass, Chord, Drums, Arpeggio, Motif, SE */
  void clearAccompanimentTracks();

  /** @brief Build song structure from params and blueprint.
   *  Priority: target_duration > form_explicit > blueprint > pattern.
   *  @param bpm Resolved BPM value
   *  @return Vector of sections */
  std::vector<Section> buildSongStructure(uint16_t bpm);

  /// @}
  /// @name RhythmSync Methods
  /// @{

  /** @brief Pre-compute drum grid for RhythmSync paradigm.
   *  Called before vocal generation, does NOT generate drum notes. */
  void computeDrumGrid();

  /// @}

  /// @brief Build a FullTrackContext pre-filled with common fields.
  ///
  /// Sets song, params, rng, harmony to reduce boilerplate across the
  /// multiple context construction sites in track generation methods.
  /// Callers may then set track-specific fields before passing to generators.
  ///
  /// @return Pre-filled FullTrackContext
  FullTrackContext buildBaseContext();

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

  /** @brief Apply staggered entry to intro sections.
   *
   * Implements gradual instrument participation by:
   * 1. Removing notes before each track's entry bar
   * 2. Applying velocity fade-in for smooth entry
   *
   * @param section The intro section to process
   * @param config Staggered entry configuration
   */
  void applyStaggeredEntry(const Section& section, const StaggeredEntryConfig& config);

  /** @brief Apply staggered entry to all qualifying sections.
   *  Called automatically after track generation if EntryPattern::Stagger is used.
   */
  void applyStaggeredEntryToSections();

  /**
   * @brief Detect clashes between vocal and accompaniment tracks.
   * @return Vector of VocalClash describing each clash with suggested fixes
   */
  std::vector<VocalClash> detectVocalAccompanimentClashes() const;

  /**
   * @brief Adjust vocal pitch at a specific tick.
   * @param tick Position to adjust
   * @param new_pitch New pitch value
   * @return true if adjustment was made
   */
  bool adjustVocalPitchAt(Tick tick, uint8_t new_pitch);

  /** @brief Apply layer schedule to remove notes from inactive bars.
   *
   *  For sections with layer_events, removes notes from bars where the
   *  corresponding track is not yet active (or has been removed).
   *  Works as post-processing after all tracks are generated.
   */
  void applyLayerSchedule();

  /** @brief Resolve chord-arpeggio clashes for BGM-only mode. */
  void resolveArpeggioChordClashes();

  /** @brief Rebuild motif track from stored pattern data. */
  void rebuildMotifFromPattern();

  /** @brief Calculate modulation point and amount from structure/mood. */
  void calculateModulation();

  /// Plan tempo map for ritardando in outro sections.
  void planTempoMap();

  /** @brief Apply post-processing pipeline to melodic tracks.
   *
   *  Orchestrates three sub-phases:
   *  1. Velocity shaping (contour, accent, bar curves, micro-dynamics)
   *  2. Transition effects (section transitions, exit patterns, clash fixes)
   *  3. Final adjustments (chord boundary clipping, panning, expression) */
  void applyPostProcessingPipeline();

  /** @brief Phase 1: Apply velocity shaping to melodic tracks.
   *
   *  Applies melody contour velocity, accent patterns, bar-level velocity
   *  curves, beat-level micro-dynamics, and phrase-end decay. */
  void applyVelocityShaping(std::vector<MidiTrack*>& tracks);

  /** @brief Phase 2: Apply transition effects for section boundaries.
   *
   *  Applies section transition dynamics, entry patterns, exit patterns,
   *  chorus drop, ritardando, motif-vocal clash fixes, enhanced final hit,
   *  emotion-based dynamics, and blueprint constraints. */
  void applyTransitionEffects(std::vector<MidiTrack*>& tracks,
                              const std::vector<TrackRole>& track_roles);

  /** @brief Phase 3: Apply final adjustments after all dynamics processing.
   *
   *  Clips vocal notes at chord boundaries, creates arrangement holes,
   *  applies stereo panning, and generates expression curves. */
  void applyFinalAdjustments();

  /** @brief Apply EmotionCurve-based velocity adjustments for section transitions. */
  void applyEmotionBasedDynamics(std::vector<MidiTrack*>& tracks,
                                  const std::vector<Section>& sections);

  /** @brief Apply emotion-based velocity adjustment to a single note.
   *  @param base_velocity Original velocity
   *  @param emotion SectionEmotion for the note's section
   *  @return Adjusted velocity (clamped 30-127) */
  uint8_t applyEmotionToVelocity(uint8_t base_velocity, const SectionEmotion& emotion);

  /** @brief Find which section a tick belongs to.
   *  @param sections Song sections
   *  @param tick Tick position to look up
   *  @return Section index (or sections.size() if not found) */
  size_t findSectionIndex(const std::vector<Section>& sections, Tick tick) const;

  /** @brief Apply humanization to all melodic tracks. */
  void applyHumanization();

  /** @brief Generate CC11 Expression curves for melodic tracks.
   *  Adds section-based expression curves to vocal, bass, and chord tracks. */
  void generateExpressionCurves();

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
