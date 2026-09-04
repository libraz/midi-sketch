/**
 * @file emotion_curve.h
 * @brief Emotion curve system for planning the emotional arc of a song.
 *
 * Implements the "story arc" approach to composition where each section
 * has specific emotional characteristics that create a coherent journey:
 * - Intro: Anticipation
 * - A melody: Expectation
 * - B melody: Tension build
 * - Chorus: Release/resolution
 * - Bridge: Reflection
 * - Outro: Closure
 */

#ifndef MIDISKETCH_CORE_EMOTION_CURVE_H
#define MIDISKETCH_CORE_EMOTION_CURVE_H

#include <cstdint>
#include <vector>

#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {

/**
 * @brief Emotion parameters for a single section.
 *
 * The curve is planned after the arrangement is fixed and consumed by the
 * post-processing pipeline, so these values shape the dynamics of notes that
 * already exist: energy sets both the level and the ceiling above it. They do
 * not reach note selection or note count, which are decided during generation,
 * before the curve is planned.
 */
struct SectionEmotion {
  /// @brief Tension level 0.0-1.0 (0=relaxed, 1=maximum tension), not read by any track.
  ///
  /// Harmonic unrest, which a pop chorus resolves at its loudest point. That is
  /// why the velocity ceiling asks energy instead: the two point opposite ways
  /// at exactly the section the arrangement made the peak.
  float tension;
  float energy;  ///< Energy level 0.0-1.0 (0=calm, 1=explosive)
  /// @brief Need for resolution 0.0-1.0 (0=stable, 1=desperate for resolution).
  ///
  /// Carried by the curve's own rules and not read by any track.
  /// getChordTonePreferenceBoost() converts it into a chord-tone bias, but
  /// nothing in the generation path calls that function.
  float resolution_need;
  /// @brief Pitch direction tendency -3..+3 (-=down, +=up), not read by any track.
  ///
  /// The run-up into the next section is driven by SectionTransition, which
  /// carries a field of the same name that the vocal transition pass does read.
  int8_t pitch_tendency;
  /// @brief Planned density weight 0.5-1.5, carried by the curve's own rules.
  ///
  /// Section note counts are set by Section::density_percent during generation.
  /// This value records the arc's density intent alongside it and is not applied
  /// to any track.
  float density_factor;
};

/**
 * @brief Hints for handling section transitions.
 *
 * Two of these reach the song: use_fill marks the next section for a drum fill
 * while the arrangement is still being built, and velocity_ramp shapes the last
 * two beats of the section during post-processing. The rest are intermediate
 * values of the curve's own rules -- crescendo is what velocity_ramp is derived
 * from, and the two pitch hints have no reader.
 */
struct TransitionHint {
  bool crescendo;         ///< Energy is rising into the next section; sets velocity_ramp
  bool use_fill;          ///< Should add drum fill at boundary
  int8_t approach_pitch;  ///< Pitch approach direction (-1=down, 0=any, +1=up), not read
  float velocity_ramp;    ///< Velocity change rate (>1 = increase, <1 = decrease)
  bool use_leading_tone;  ///< Insert leading tone before next section, not read
};

/**
 * @brief Plans and tracks the emotional arc of a song.
 *
 * The curve is planned once the arrangement is fixed, so it cannot guide
 * generation: every note it shapes already exists by the time it is read.
 *
 * Usage:
 * @code
 * EmotionCurve curve;
 * curve.plan(sections, Mood::ModernPop);
 *
 * // During post-processing, to shape the dynamics of existing notes:
 * const auto& emotion = curve.getEmotion(section_index);
 * // emotion.energy sets the level and the ceiling above it
 *
 * // At section boundaries:
 * auto hint = curve.getTransitionHint(from_index);
 * // hint.use_fill and hint.velocity_ramp are the two the song reads
 * @endcode
 */
class EmotionCurve {
 public:
  EmotionCurve() = default;

  /**
   * @brief Plan the emotional curve for a song structure.
   * @param sections Vector of sections defining song structure
   * @param mood Overall mood affecting intensity scaling
   */
  void plan(const std::vector<Section>& sections, Mood mood);

  /**
   * @brief Get emotion parameters for a section.
   * @param section_index Index into the sections vector
   * @return SectionEmotion for the specified section
   */
  const SectionEmotion& getEmotion(size_t section_index) const;

  /**
   * @brief Get transition hint from one section to the next.
   * @param from_index Index of the source section
   * @return TransitionHint for the transition
   */
  TransitionHint getTransitionHint(size_t from_index) const;

  /**
   * @brief Check if curve has been planned.
   * @return true if plan() has been called
   */
  bool isPlanned() const { return !emotions_.empty(); }

  /**
   * @brief Get the number of sections in the curve.
   * @return Number of planned sections
   */
  size_t size() const { return emotions_.size(); }

  /**
   * @brief Get mood intensity multiplier.
   * @param mood The mood to get intensity for
   * @return Intensity multiplier (0.7-1.3)
   */
  static float getMoodIntensity(Mood mood);

 private:
  std::vector<SectionEmotion> emotions_;
  std::vector<Section> sections_;
  Mood mood_ = Mood::ModernPop;

  /**
   * @brief Estimate base emotion for a section type.
   * @param type The section type
   * @return Base emotion values for this section type
   */
  static SectionEmotion estimateBaseEmotion(SectionType type);

  /**
   * @brief Adjust emotions based on surrounding context.
   *
   * Implements rules like:
   * - B before Chorus gets higher tension
   * - Bridge after Chorus gets lower energy
   * - Repeated sections get progressive intensity
   */
  void adjustForContext();

  /**
   * @brief Apply mood-based scaling to all emotions.
   */
  void applyMoodScaling();

  /// Default emotion for out-of-bounds access
  static const SectionEmotion kDefaultEmotion;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_EMOTION_CURVE_H
