/**
 * @file coordinator.h
 * @brief Central coordinator for song generation.
 *
 * Coordinator is the single source of truth for:
 * - Song structure (BPM, chord progression, sections)
 * - Generation order (track priorities based on paradigm)
 * - Cross-track adjustments (motif application, hook sharing)
 * - Parameter validation
 *
 * Design principles:
 * 1. Generation order = priority (later tracks avoid earlier tracks)
 * 2. Pre-computed safety candidates per beat
 * 3. Physical model constraints enforced
 * 4. Tension selection remains track responsibility
 * 5. Drums are pitch-independent (no collision check)
 */

#ifndef MIDISKETCH_CORE_COORDINATOR_H
#define MIDISKETCH_CORE_COORDINATOR_H

#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "core/arrangement.h"
#include "core/i_harmony_coordinator.h"
#include "core/i_track_base.h"
#include "core/production_blueprint.h"
#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {

// Forward declarations
class Song;
struct ChordProgression;

/// @brief Validation result for parameters.
struct ValidationResult {
  bool valid = true;                  ///< True if all parameters are valid
  std::vector<std::string> warnings;  ///< Non-fatal issues
  std::vector<std::string> errors;    ///< Fatal issues

  /// @brief Add a warning.
  void addWarning(const std::string& msg) { warnings.push_back(msg); }

  /// @brief Add an error (sets valid = false).
  void addError(const std::string& msg) {
    errors.push_back(msg);
    valid = false;
  }

  /// @brief Check if there are any issues.
  bool hasIssues() const { return !warnings.empty() || !errors.empty(); }
};

/// @brief Central coordinator for song generation.
///
/// Owns and manages:
/// - Song structure (BPM, chord progression, arrangement)
/// - Generation paradigm and riff policy
/// - Track generation order
/// - Cross-track coordination
class Coordinator {
 public:
  Coordinator();
  ~Coordinator();

  // =========================================================================
  // Initialization
  // =========================================================================

  /// @brief Initialize the coordinator with generation parameters.
  /// @param params Generation parameters
  void initialize(const GeneratorParams& params);

  /// @brief Initialize the coordinator with external dependencies.
  ///
  /// Used by Generator to share its harmony context and arrangement.
  /// This avoids duplicating state between Generator and Coordinator.
  ///
  /// @param params Generation parameters
  /// @param arrangement Pre-built arrangement (with density progression, etc.)
  /// @param rng Reference to external RNG (shared state)
  /// @param harmony Pointer to external harmony coordinator
  void initialize(const GeneratorParams& params,
                  const Arrangement& arrangement,
                  std::mt19937& rng,
                  IHarmonyCoordinator* harmony);

  /// @brief Validate all parameters for musical correctness.
  /// @return Validation result with warnings/errors
  ValidationResult validateParams() const;

  // =========================================================================
  // Song Structure (Coordinator-owned)
  // =========================================================================

  /// @brief Get the BPM.
  uint16_t getBpm() const { return bpm_; }

  /// @brief Get the chord progression.
  const ChordProgression& getChordProgression() const;

  /// @brief Get the arrangement.
  const Arrangement& getArrangement() const { return arrangement_; }

  /// @brief Get the generation paradigm.
  GenerationParadigm getParadigm() const { return paradigm_; }

  /// @brief Get the riff policy.
  RiffPolicy getRiffPolicy() const { return riff_policy_; }

  /// @brief Get the resolved blueprint.
  const ProductionBlueprint* getBlueprint() const { return blueprint_; }

  /// @brief Get the resolved blueprint ID.
  uint8_t getBlueprintId() const { return blueprint_id_; }

  // =========================================================================
  // Generation Control
  // =========================================================================

  /// @brief Get the track generation order for the current paradigm.
  ///
  /// Order determines priority: earlier tracks have higher priority,
  /// later tracks must avoid them.
  ///
  /// Traditional:  Vocal → Aux → Motif → Bass → Chord → Arpeggio
  /// RhythmSync:   Motif → Vocal → Aux → Bass → Chord → Arpeggio
  /// MelodyDriven: Vocal → Aux → Bass → Chord → Motif → Arpeggio
  ///
  /// @return Vector of track roles in generation order
  std::vector<TrackRole> getGenerationOrder() const;

  /// @brief Get the priority for a track role.
  /// @param role Track role
  /// @return Priority level
  TrackPriority getTrackPriority(TrackRole role) const;

  /// @brief Check if rhythm lock is active (RhythmSync + Locked policy).
  bool isRhythmLockActive() const;

  /// @brief Generate all tracks in priority order.
  /// @param song Song to populate
  void generateAllTracks(Song& song);

  /// @brief Regenerate a specific track.
  /// @param role Track to regenerate
  /// @param song Song to update
  void regenerateTrack(TrackRole role, Song& song);

  // =========================================================================
  // Cross-Track Coordination
  // =========================================================================

  /// @brief Apply a motif pattern across sections.
  /// @param pattern Motif note pattern
  /// @param track Target track
  void applyMotifAcrossSections(const std::vector<NoteEvent>& pattern,
                                 MidiTrack& track);

  /// @brief Apply a hook to target section types.
  /// @param hook Hook note pattern
  /// @param targets Section types to apply to
  /// @param track Target track
  void applyHookToSections(const std::vector<NoteEvent>& hook,
                           const std::vector<SectionType>& targets,
                           MidiTrack& track);

  // =========================================================================
  // Accessors
  // =========================================================================

  /// @brief Get the harmony coordinator.
  IHarmonyCoordinator& harmony() { return *harmony_; }

  /// @brief Get the harmony coordinator (const).
  const IHarmonyCoordinator& harmony() const { return *harmony_; }

  /// @brief Get the active RNG (external if set, otherwise owned).
  std::mt19937& rng() { return getActiveRng(); }

  /// @brief Get the current parameters.
  const GeneratorParams& getParams() const { return params_; }

  /// @brief Get accumulated warnings.
  const std::vector<std::string>& getWarnings() const { return warnings_; }

  /// @brief Clear accumulated warnings.
  void clearWarnings() { warnings_.clear(); }

  /// @brief Get a track generator by role.
  /// @param role Track role
  /// @return Pointer to generator, or nullptr if not registered
  ITrackBase* getTrackGenerator(TrackRole role);

  /// @brief Get a track generator by role (const).
  /// @param role Track role
  /// @return Pointer to generator, or nullptr if not registered
  const ITrackBase* getTrackGenerator(TrackRole role) const;

 private:
  // =========================================================================
  // Song Structure (Coordinator-owned)
  // =========================================================================

  GeneratorParams params_;          ///< Current parameters
  uint16_t bpm_ = 120;              ///< Resolved BPM
  uint8_t chord_id_ = 0;            ///< Chord progression ID
  Arrangement arrangement_;         ///< Song arrangement
  GenerationParadigm paradigm_ = GenerationParadigm::Traditional;
  RiffPolicy riff_policy_ = RiffPolicy::Free;
  uint8_t blueprint_id_ = 0;        ///< Resolved blueprint ID
  const ProductionBlueprint* blueprint_ = nullptr;  ///< Blueprint pointer

  // =========================================================================
  // Generation Engine
  // =========================================================================

  std::unique_ptr<IHarmonyCoordinator> harmony_;  ///< Harmony coordinator (owned)
  IHarmonyCoordinator* external_harmony_ = nullptr;  ///< External harmony (not owned)
  std::map<TrackRole, std::unique_ptr<ITrackBase>> track_generators_;  ///< Track generators
  std::mt19937 rng_;                ///< Random number generator (owned)
  std::mt19937* external_rng_ = nullptr;  ///< External RNG (not owned)

  /// @brief Get the active RNG (external if set, otherwise owned).
  std::mt19937& getActiveRng() { return external_rng_ ? *external_rng_ : rng_; }

  /// @brief Get the active harmony coordinator (external if set, otherwise owned).
  IHarmonyCoordinator& getActiveHarmony() {
    return external_harmony_ ? *external_harmony_ : *harmony_;
  }

  // =========================================================================
  // State
  // =========================================================================

  std::vector<std::string> warnings_;  ///< Accumulated warnings
  std::map<TrackRole, TrackPriority> priorities_;  ///< Track priorities
  DrumGrid drum_grid_;  ///< Drum grid for RhythmSync paradigm

  // =========================================================================
  // Initialization Helpers
  // =========================================================================

  /// @brief Initialize blueprint from params.
  void initializeBlueprint();

  /// @brief Initialize track priorities based on paradigm.
  void initializePriorities();

  /// @brief Build the song arrangement.
  void buildArrangement();

  /// @brief Validate BPM for the current paradigm.
  void validateBpm();

  /// @brief Register track generators.
  void registerTrackGenerators();

  /// @brief Determine whether a track should be skipped during generation.
  ///
  /// Consolidates all skip conditions: disabled flags, composition style,
  /// paradigm constraints, mood sentinel checks, and blueprint requirements.
  ///
  /// @param role Track role to check
  /// @param song Song reference (needed to check if Motif already exists)
  /// @return true if the track should be skipped
  bool shouldSkipTrack(TrackRole role, const Song& song) const;

  /// @brief Register guide chord phantom notes for collision detection.
  ///
  /// Registers Root + 3rd + 7th as phantom notes in the Vocal-adapted register
  /// for each bar, using the effective chord degree (including secondary dominants).
  /// Called after secondary dominant planning, before track generation.
  ///
  /// @param harmony Harmony coordinator to register phantom notes with
  void registerGuideChord(IHarmonyCoordinator& harmony);

  /// @brief Apply max_moving_voices constraint by freezing low-priority tracks.
  /// @param song The song with generated tracks
  /// @param sections The section list
  void applyVoiceLimit(Song& song, const std::vector<Section>& sections);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_COORDINATOR_H
