/**
 * @file melody_generation_pipeline.h
 * @brief Unified interface for melody generation, consolidating scattered helper functions.
 *
 * This class reduces the deep call chain (7-10 levels) in MelodyDesigner by providing
 * a unified facade over the melody submodule functions. Instead of 20+ using directives
 * and scattered function calls, callers use this single pipeline object.
 *
 * Refactoring benefits:
 * - Reduced call chain depth: 7-10 → 3-4 levels
 * - Improved traceability: all melody operations go through one interface
 * - Clearer dependencies: state is explicitly passed via context objects
 */

#ifndef MIDISKETCH_TRACK_MELODY_GENERATION_PIPELINE_H
#define MIDISKETCH_TRACK_MELODY_GENERATION_PIPELINE_H

#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "core/melody_templates.h"
#include "core/melody_types.h"
#include "core/pitch_utils.h"  // for TessituraRange
#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {

class IHarmonyContext;

namespace melody {

/// @brief Context for pitch generation operations.
struct PitchGenerationContext {
  int current_pitch = 60;
  int target_pitch = -1;
  int8_t chord_degree = 0;
  int key_offset = 0;
  uint8_t vocal_low = 48;
  uint8_t vocal_high = 84;
  VocalAttitude attitude = VocalAttitude::Expressive;
  bool disable_singability = false;
  float note_eighths = 2.0f;
  SectionType section_type = SectionType::A;
  TessituraRange tessitura{60, 72, 66, 48, 84};  // low=60, high=72, center=66, vocal_low=48, vocal_high=84
  uint8_t max_leap_semitones = 12;
};

/// @brief Context for rhythm generation operations.
struct RhythmGenerationContext {
  uint8_t phrase_beats = 4;
  float density_modifier = 1.0f;
  float thirtysecond_ratio = 0.0f;
  GenerationParadigm paradigm = GenerationParadigm::Traditional;
  float syncopation_weight = 0.15f;
  SectionType section_type = SectionType::A;
  RhythmGrid rhythm_grid = RhythmGrid::Binary;
};

/// @brief State tracking for phrase generation.
struct PhraseGenerationState {
  int direction_inertia = 0;
  int prev_pitch = -1;
  Tick prev_note_duration = 480;  // Quarter note default
  int consecutive_same_count = 0;
};

/// @brief Unified pipeline for melody generation operations.
///
/// Consolidates 20+ helper functions from melody/ submodules into a single
/// coherent interface. This reduces call chain depth and improves traceability.
///
/// Usage:
/// @code
/// MelodyGenerationPipeline pipeline;
///
/// // Generate rhythm pattern
/// RhythmGenerationContext rhythm_ctx{...};
/// auto rhythm = pipeline.generateRhythm(tmpl, rhythm_ctx, rng);
///
/// // Resolve pitch for each note
/// PitchGenerationContext pitch_ctx{...};
/// PhraseGenerationState state{};
/// int pitch = pipeline.resolvePitch(tmpl, pitch_ctx, state, rng);
/// @endcode
class MelodyGenerationPipeline {
 public:
  MelodyGenerationPipeline() = default;

  // ============================================================================
  // Rhythm Generation
  // ============================================================================

  /// @brief Generate rhythm pattern for a phrase.
  /// @param tmpl Melody template with rhythm parameters
  /// @param ctx Rhythm generation context
  /// @param rng Random number generator
  /// @return Vector of rhythm positions for the phrase
  std::vector<RhythmNote> generateRhythm(const MelodyTemplate& tmpl,
                                         const RhythmGenerationContext& ctx,
                                         std::mt19937& rng) const;

  // ============================================================================
  // Pitch Resolution
  // ============================================================================

  /// @brief Select pitch choice (direction) based on template and position.
  /// @param tmpl Melody template
  /// @param phrase_pos Position within phrase (0.0-1.0)
  /// @param ctx Pitch generation context
  /// @param forced_contour Optional forced contour override
  /// @param rng Random number generator
  /// @return Selected pitch choice
  PitchChoice selectPitchChoice(const MelodyTemplate& tmpl, float phrase_pos,
                                const PitchGenerationContext& ctx,
                                std::optional<ContourType> forced_contour,
                                std::mt19937& rng) const;

  /// @brief Apply direction inertia to modify pitch choice.
  /// @param choice Initial pitch choice
  /// @param state Current phrase state (contains inertia)
  /// @param tmpl Melody template
  /// @param rng Random number generator
  /// @return Modified pitch choice
  PitchChoice applyDirectionInertia(PitchChoice choice,
                                    const PhraseGenerationState& state,
                                    const MelodyTemplate& tmpl,
                                    std::mt19937& rng) const;

  /// @brief Resolve final pitch from pitch choice and context.
  /// @param choice Pitch choice (direction)
  /// @param ctx Pitch generation context
  /// @return Resolved MIDI pitch
  int applyPitchChoice(PitchChoice choice, const PitchGenerationContext& ctx) const;

  /// @brief Calculate target pitch for phrase.
  /// @param tmpl Melody template
  /// @param ctx Pitch generation context
  /// @param section_start Section start tick for harmony lookup
  /// @param harmony Harmony context
  /// @return Target pitch
  int calculateTargetPitch(const MelodyTemplate& tmpl, const PitchGenerationContext& ctx,
                           Tick section_start, const IHarmonyContext& harmony) const;

  // ============================================================================
  // Constraint Application (combines multiple constraint checks)
  // ============================================================================

  /// @brief Apply all melodic constraints to a pitch.
  ///
  /// Combines multiple constraint checks in a single call:
  /// - Consecutive same note limit
  /// - Maximum interval constraint
  /// - Leap preparation (after short notes)
  /// - Leap encouragement (after long notes)
  /// - Avoid note constraint
  /// - Downbeat chord-tone constraint
  /// - Leap reversal rule
  ///
  /// @param pitch Initial pitch to constrain
  /// @param note_start Note start tick
  /// @param ctx Pitch generation context
  /// @param state Phrase generation state (modified)
  /// @param harmony Harmony context
  /// @param rng Random number generator
  /// @return Constrained pitch
  int applyAllPitchConstraints(int pitch, Tick note_start,
                               const PitchGenerationContext& ctx,
                               PhraseGenerationState& state,
                               const IHarmonyContext& harmony,
                               std::mt19937& rng) const;

  // ============================================================================
  // Duration/Gate Processing
  // ============================================================================

  /// @brief Apply all duration constraints.
  /// @param note_start Note start tick
  /// @param duration Initial duration
  /// @param harmony Harmony context
  /// @param phrase_end Phrase end tick
  /// @param is_phrase_end Whether this is the last note in phrase
  /// @param is_phrase_start Whether this is the first note in phrase
  /// @param interval_from_prev Interval from previous note
  /// @return Constrained duration
  Tick applyDurationConstraints(Tick note_start, Tick duration,
                                const IHarmonyContext& harmony,
                                Tick phrase_end, bool is_phrase_end,
                                bool is_phrase_start, int interval_from_prev,
                                uint8_t pitch = 0) const;

  // ============================================================================
  // Utility Functions
  // ============================================================================

  /// @brief Get base breath duration between phrases.
  /// @param section_type Section type
  /// @param mood Current mood
  /// @return Base breath duration in ticks
  Tick getBaseBreathDuration(SectionType section_type, Mood mood) const;

  /// @brief Get contextual breath duration.
  /// @param section_type Section type
  /// @param mood Current mood
  /// @param phrase_density Note density of previous phrase
  /// @param phrase_high Highest note in previous phrase
  /// @param breath_ctx Additional breath context (optional)
  /// @param vocal_style Vocal style preset
  /// @return Breath duration in ticks
  Tick getBreathDuration(SectionType section_type, Mood mood,
                         float phrase_density, uint8_t phrase_high,
                         const void* breath_ctx,
                         VocalStylePreset vocal_style) const;

  /// @brief Get rhythm unit based on grid type.
  /// @param grid Rhythm grid (Binary, Ternary, Shuffle)
  /// @param is_eighth Whether to return eighth note unit
  /// @return Duration in ticks
  Tick getRhythmUnit(RhythmGrid grid, bool is_eighth) const;

  /// @brief Get effective max interval for section type.
  /// @param section_type Section type
  /// @param ctx_max_leap Context maximum leap
  /// @return Effective maximum interval
  int getEffectiveMaxInterval(SectionType section_type, uint8_t ctx_max_leap) const;

  /// @brief Get motif weight for section type.
  /// @param section_type Section type
  /// @return Weight multiplier (0.0-1.0)
  float getMotifWeightForSection(SectionType section_type) const;

  // ============================================================================
  // Post-processing
  // ============================================================================

  /// @brief Resolve isolated notes in the phrase.
  /// @param notes Notes to process (modified in place)
  /// @param harmony Harmony context
  /// @param vocal_low Minimum pitch
  /// @param vocal_high Maximum pitch
  void resolveIsolatedNotes(std::vector<NoteEvent>& notes,
                            const IHarmonyContext& harmony,
                            uint8_t vocal_low, uint8_t vocal_high) const;

 private:
  // Internal helpers delegate to submodule functions
};

}  // namespace melody
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_GENERATION_PIPELINE_H
