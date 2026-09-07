/**
 * @file post_processing_pipeline.h
 * @brief Post-processing pipeline for velocity shaping, dynamics, and expression.
 *
 * Extracted from Generator to encapsulate post-processing phases:
 * 1. Velocity shaping (contour, accent, bar curves, micro-dynamics)
 * 2. Transition effects (section transitions, exit patterns, emotion dynamics)
 * 3. Final adjustments (chord boundary clipping, panning, expression)
 * 4. Humanization (velocity/timing variation)
 * 5. Expression curves (CC11, CC1, CC7, CC74, CC64)
 */

#ifndef MIDISKETCH_CORE_POST_PROCESSING_PIPELINE_H
#define MIDISKETCH_CORE_POST_PROCESSING_PIPELINE_H

#include <random>
#include <vector>

#include "core/emotion_curve.h"
#include "core/i_harmony_context.h"
#include "core/i_harmony_coordinator.h"
#include "core/production_blueprint.h"
#include "core/section_types.h"
#include "core/song.h"
#include "core/types.h"

namespace midisketch {

/// @brief Smallest departure from 1.0 the transition velocity ramp acts on.
///
/// Below this the ramp would move a velocity by less than a step over the two
/// beats it covers, so the pass skips the transition rather than walking every
/// note to no effect. Whether a planned ramp clears it is the difference
/// between the crescendo reaching the notes and being computed and discarded,
/// which is why the threshold is named here rather than written at the one
/// place that applies it.
constexpr float kMinTransitionRampDeparture = 0.05f;

/**
 * @brief Orchestrates post-processing phases for generated tracks.
 *
 * Handles velocity shaping, transition effects, final adjustments,
 * humanization, staggered entry, and expression curve generation.
 */
class PostProcessingPipeline {
 public:
  /**
   * @brief Context containing all external dependencies for post-processing.
   *
   * All references/pointers must remain valid for the duration of run().
   */
  struct Context {
    Song& song;
    const GeneratorParams& params;
    IHarmonyCoordinator& harmony;
    std::mt19937& rng;
    const ProductionBlueprint* blueprint;
    const EmotionCurve& emotion_curve;
  };

  /**
   * @brief Run the full post-processing pipeline.
   *
   * Executes in order:
   * 1. Staggered entry for intro sections
   * 2. Post-processing pipeline (velocity, transitions, final adjustments)
   * 3. Expression curves (CC11, CC1, CC7, CC74, CC64)
   * 4. Humanization (if enabled)
   *
   * Note: applyLayerSchedule() and planTempoMap() are NOT included here;
   * they remain in Generator as they are structural, not dynamics-related.
   *
   * @param ctx Context with all required dependencies
   */
  void run(const Context& ctx);

 private:
  /// @name Pipeline Phases
  /// @{

  /** @brief Apply staggered entry to all qualifying intro sections. */
  void applyStaggeredEntryToSections(const Context& ctx);

  /** @brief Apply staggered entry to a single intro section. */
  void applyStaggeredEntry(const Context& ctx, const Section& section,
                           const StaggeredEntryConfig& config);

  /** @brief Orchestrate the three-phase post-processing pipeline. */
  void applyPostProcessingPipeline(const Context& ctx);

  /** @brief Velocity shaping (contour, accent, bar curves, micro-dynamics). */
  void applyVelocityShaping(const Context& ctx, std::vector<MidiTrack*>& tracks);

  /** @brief Transition effects (section transitions, exit patterns, emotion). */
  void applyTransitionEffects(const Context& ctx, std::vector<MidiTrack*>& tracks,
                              const std::vector<TrackRole>& track_roles);

  /** @brief Final adjustments (chord boundary clipping, panning, expression). */
  void applyFinalAdjustments(const Context& ctx);

  /** @brief Apply EmotionCurve-based velocity adjustments for section transitions. */
  void applyEmotionBasedDynamics(const Context& ctx, std::vector<MidiTrack*>& tracks,
                                 const std::vector<Section>& sections);

  /** @brief Apply emotion-based velocity adjustment to a single note. */
  uint8_t applyEmotionToVelocity(const Context& ctx, uint8_t base_velocity,
                                 const SectionEmotion& emotion);

  /** @brief Apply random velocity humanization to all melodic tracks. */
  void applyVelocityHumanization(const Context& ctx);

  /**
   * @brief Apply the drum-style groove pocket and the bass-kick alignment.
   *
   * Deterministic micro-timing, shaped by drum_style and drive_feel. Separate
   * from velocity humanization because it is a groove decision rather than a
   * human-imperfection one, and it is reached whenever the caller asked for
   * timing at all.
   *
   * @param ctx Context with all required dependencies
   * @param timing_amount How much of the pocket to apply (0.0-1.0)
   */
  void applyGrooveTiming(const Context& ctx, float timing_amount);

  /** @brief Generate CC11 Expression curves for melodic tracks. */
  void generateExpressionCurves(const Context& ctx);

  /// @}
  /// @name Utilities
  /// @{

  /** @brief Find which section a tick belongs to. */
  size_t findSectionIndex(const std::vector<Section>& sections, Tick tick) const;

  /// @}
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_POST_PROCESSING_PIPELINE_H
