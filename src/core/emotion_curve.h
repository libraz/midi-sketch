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
 * These values guide note selection, velocity, and density across tracks.
 */
struct SectionEmotion {
  float tension;          ///< Tension level 0.0-1.0 (0=relaxed, 1=maximum tension)
  float energy;           ///< Energy level 0.0-1.0 (0=calm, 1=explosive)
  float resolution_need;  ///< Need for resolution 0.0-1.0 (0=stable, 1=desperate for resolution)
  int8_t pitch_tendency;  ///< Pitch direction tendency -3..+3 (-=down, +=up)
  float density_factor;   ///< Density multiplier 0.5-1.5 (affects note count)
};

/**
 * @brief Hints for handling section transitions.
 */
struct TransitionHint {
  bool crescendo;           ///< Should crescendo into next section
  bool use_fill;            ///< Should add drum fill at boundary
  int8_t approach_pitch;    ///< Pitch approach direction (-1=down, 0=any, +1=up)
  float velocity_ramp;      ///< Velocity change rate (>1 = increase, <1 = decrease)
  bool use_leading_tone;    ///< Insert leading tone before next section
};

/**
 * @brief Plans and tracks the emotional arc of a song.
 *
 * Usage:
 * @code
 * EmotionCurve curve;
 * curve.plan(sections, Mood::ModernPop);
 *
 * // During generation:
 * const auto& emotion = curve.getEmotion(section_index);
 * // Use emotion.tension, emotion.energy, etc. to guide generation
 *
 * // At section boundaries:
 * auto hint = curve.getTransitionHint(from_index);
 * // Use hint to guide transition handling
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
