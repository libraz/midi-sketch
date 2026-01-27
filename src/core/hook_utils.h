/**
 * @file hook_utils.h
 * @brief Hook generation utilities using HookSkeleton and HookBetrayal.
 *
 * Implements the "select, not create" philosophy for memorable hooks.
 * Hooks are selected from predefined patterns, not randomly generated.
 */

#ifndef MIDISKETCH_CORE_HOOK_UTILS_H
#define MIDISKETCH_CORE_HOOK_UTILS_H

#include <array>
#include <random>
#include <vector>

#include "core/melody_types.h"
#include "core/pitch_utils.h"
#include "core/types.h"

namespace midisketch {

/// @brief Maximum intervals in a hook skeleton.
constexpr size_t kMaxHookIntervals = 5;

/// @brief Relative interval pattern for a hook skeleton.
struct SkeletonPattern {
  std::array<int8_t, kMaxHookIntervals> intervals;  ///< Relative scale degrees
  uint8_t length;                                   ///< Number of notes
};

/// @brief Convert scale degree offset to semitones (major scale).
/// @param degree Scale degree offset (0-7)
/// @returns Semitone offset
inline int scaleDegreesToSemitones(int degree) {
  // Major scale intervals: 0, 2, 4, 5, 7, 9, 11, 12
  constexpr int kMajorScaleSemitones[] = {0, 2, 4, 5, 7, 9, 11, 12};
  if (degree < 0) {
    // Negative degrees: mirror around 0
    return -scaleDegreesToSemitones(-degree);
  }
  if (degree >= 8) {
    // Octave + remainder
    return 12 + scaleDegreesToSemitones(degree - 7);
  }
  return kMajorScaleSemitones[degree];
}

/// @brief Get interval pattern for a hook skeleton.
/// @param skeleton The hook skeleton type
/// @returns Pattern of relative intervals (scale degrees)
inline SkeletonPattern getSkeletonPattern(HookSkeleton skeleton) {
  switch (skeleton) {
    case HookSkeleton::Repeat:
      // X X X - same pitch repetition, most memorable
      return {{0, 0, 0, 0, 0}, 3};

    case HookSkeleton::Ascending:
      // X X+1 X+2 - stepwise rise, builds energy
      return {{0, 1, 2, 0, 0}, 3};

    case HookSkeleton::AscendDrop:
      // X X+2 X+4 X+3 - rise then slight fall, creates arc
      return {{0, 2, 4, 3, 0}, 4};

    case HookSkeleton::LeapReturn:
      // X X+4 X+1 - jump up then resolve down, dramatic
      return {{0, 4, 1, 0, 0}, 3};

    case HookSkeleton::RhythmRepeat:
      // X _ X _ X - with rests, rhythmic emphasis (-128 = rest marker)
      return {{0, -128, 0, -128, 0}, 5};

    case HookSkeleton::PeakDrop:
      // X X+3 X+5 X+2 X - Peak then descend back to start
      return {{0, 3, 5, 2, 0}, 5};

    case HookSkeleton::Pendulum:
      // X X+3 X-1 X+2 X - Swing motion
      return {{0, 3, -1, 2, 0}, 5};

    case HookSkeleton::DescentResolve:
      // X X-1 X-2 X-1 - Descend then resolve up
      return {{0, -1, -2, -1, 0}, 4};

    case HookSkeleton::CallResponse:
      // X X+2 X X+3 - Question-answer pattern
      return {{0, 2, 0, 3, 0}, 4};

    case HookSkeleton::Syncopated:
      // X _ X+1 X _ - Rhythmic with rests (-128 = rest)
      return {{0, -128, 1, 0, -128}, 5};

    case HookSkeleton::ChromaticSlide:
      // X X X+1 X+1 - Repeat then half-step up
      return {{0, 0, 1, 1, 0}, 4};

    case HookSkeleton::DoubleAscend:
      // X X+1 X X+2 X - Two-step rise with anchoring
      return {{0, 1, 0, 2, 0}, 5};

    case HookSkeleton::Staircase:
      // X X+2 X+1 X+3 X+2 - Ascending staircase pattern
      return {{0, 2, 1, 3, 2}, 5};

    case HookSkeleton::TripleHit:
      // X X X Y - Same note emphasis then resolution
      return {{0, 0, 0, 2, 0}, 4};

    case HookSkeleton::WideArch:
      // X X+4 X+7 X+4 X - Wide arch contour
      return {{0, 4, 7, 4, 0}, 5};

    case HookSkeleton::NarrowPendulum:
      // X X+1 X-1 X - Narrow swing motion
      return {{0, 1, -1, 0, 0}, 4};

    case HookSkeleton::QuestionMark:
      // X X+2 X+4 X+5 - Ascending question (unresolved)
      return {{0, 2, 4, 5, 0}, 4};

    // Phase 3: New patterns for addictiveness improvement
    case HookSkeleton::StepwiseDescent:
      // X X-1 X-2 X-3 - Gradual descent creates melancholic resolution
      return {{0, -1, -2, -3, 0}, 4};

    case HookSkeleton::OctaveLeap:
      // X X+7 X+4 - Octave jump is dramatic and memorable
      return {{0, 7, 4, 0, 0}, 3};

    case HookSkeleton::SuspendResolve:
      // X X+1 X+1 X - Sus4-like tension then release
      return {{0, 1, 1, 0, 0}, 4};

    case HookSkeleton::SymmetricArch:
      // X X+2 X+4 X+2 X - Mirror/arch pattern for balanced beauty
      return {{0, 2, 4, 2, 0}, 5};

    case HookSkeleton::AnticipationBuild:
      // X X X+2 X+4 - Buildup pattern before climax
      return {{0, 0, 2, 4, 0}, 4};

    case HookSkeleton::EchoPhrasing:
      // X _ X-1 X - Echo with rest and variation (-128 = rest)
      return {{0, -128, -1, 0, 0}, 4};

    case HookSkeleton::StutterRepeat:
      // X X _ X X - Rhythmic stutter for modern/edgy feel (-128 = rest)
      return {{0, 0, -128, 0, 0}, 5};
  }
  return {{0, 0, 0, 0, 0}, 3};  // Default: repeat
}

/// @brief Weight map for hook skeleton selection.
struct SkeletonWeights {
  float repeat;           ///< Weight for Repeat skeleton
  float ascending;        ///< Weight for Ascending skeleton
  float ascend_drop;      ///< Weight for AscendDrop skeleton
  float leap_return;      ///< Weight for LeapReturn skeleton
  float rhythm_repeat;    ///< Weight for RhythmRepeat skeleton
  float peak_drop = 0.0f;        ///< Weight for PeakDrop skeleton
  float pendulum = 0.0f;         ///< Weight for Pendulum skeleton
  float descent_resolve = 0.0f;  ///< Weight for DescentResolve skeleton
  float call_response = 0.0f;    ///< Weight for CallResponse skeleton
  float syncopated = 0.0f;       ///< Weight for Syncopated skeleton
  float chromatic_slide = 0.0f;  ///< Weight for ChromaticSlide skeleton
  // Extended patterns
  float double_ascend = 0.0f;     ///< Weight for DoubleAscend skeleton
  float staircase = 0.0f;         ///< Weight for Staircase skeleton
  float triple_hit = 0.0f;        ///< Weight for TripleHit skeleton
  float wide_arch = 0.0f;         ///< Weight for WideArch skeleton
  float narrow_pendulum = 0.0f;   ///< Weight for NarrowPendulum skeleton
  float question_mark = 0.0f;     ///< Weight for QuestionMark skeleton
  // Phase 3: New patterns for addictiveness
  float stepwise_descent = 0.0f;   ///< Weight for StepwiseDescent skeleton
  float octave_leap = 0.0f;        ///< Weight for OctaveLeap skeleton
  float suspend_resolve = 0.0f;    ///< Weight for SuspendResolve skeleton
  float symmetric_arch = 0.0f;     ///< Weight for SymmetricArch skeleton
  float anticipation_build = 0.0f; ///< Weight for AnticipationBuild skeleton
  float echo_phrasing = 0.0f;      ///< Weight for EchoPhrasing skeleton
  float stutter_repeat = 0.0f;     ///< Weight for StutterRepeat skeleton
};

/// @brief Default weights for Chorus sections (memorability focused).
/// Order: repeat, ascending, ascend_drop, leap_return, rhythm_repeat
constexpr SkeletonWeights kChorusSkeletonWeights = {
    1.5f,  // repeat - Most memorable
    1.3f,  // ascending - Energy building
    1.0f,  // ascend_drop - Natural arc
    0.7f,  // leap_return - Less common
    1.2f,  // rhythm_repeat - Catchy rhythm
    0.9f,  // peak_drop
    0.6f,  // pendulum
    0.5f,  // descent_resolve
    0.8f,  // call_response
    0.7f,  // syncopated
    0.4f,  // chromatic_slide
    // Extended patterns
    1.1f,  // double_ascend - Good for chorus build
    0.7f,  // staircase - Interesting variety
    1.3f,  // triple_hit - Strong emphasis (catchy)
    0.8f,  // wide_arch - Dramatic contour
    0.5f,  // narrow_pendulum - Subtle motion
    0.6f,  // question_mark - Creates tension
    // Phase 3: New patterns
    1.0f,  // stepwise_descent - Melancholic resolution (effective in chorus)
    0.8f,  // octave_leap - Dramatic impact
    0.7f,  // suspend_resolve - Tension release
    0.9f,  // symmetric_arch - Balanced beauty
    0.6f,  // anticipation_build - Pre-climax
    0.5f,  // echo_phrasing - Rhythmic interest
    0.7f,  // stutter_repeat - Modern feel
};

/// @brief Default weights for non-Chorus sections.
constexpr SkeletonWeights kDefaultSkeletonWeights = {
    1.0f,  // repeat
    1.0f,  // ascending
    1.0f,  // ascend_drop
    0.8f,  // leap_return
    0.9f,  // rhythm_repeat
    0.8f,  // peak_drop
    0.7f,  // pendulum
    0.7f,  // descent_resolve
    0.9f,  // call_response
    0.8f,  // syncopated
    0.5f,  // chromatic_slide
    // Extended patterns
    0.9f,  // double_ascend - Good for verse development
    0.8f,  // staircase - Adds variety
    0.7f,  // triple_hit - Can be too repetitive for verse
    0.6f,  // wide_arch - Save drama for chorus
    0.8f,  // narrow_pendulum - Works well in verses
    0.7f,  // question_mark - Good for pre-chorus
    // Phase 3: New patterns
    0.8f,  // stepwise_descent - Good for verse resolution
    0.5f,  // octave_leap - Save impact for chorus
    0.7f,  // suspend_resolve - Works in pre-chorus
    0.6f,  // symmetric_arch - Moderate use in verse
    0.8f,  // anticipation_build - Good for pre-chorus
    0.6f,  // echo_phrasing - Adds variety
    0.5f,  // stutter_repeat - Modern sections
};

/// @brief Apply HookIntensity multiplier to skeleton weights.
///
/// HookIntensity affects the selection probability of different skeletons:
/// - Off: Reduces Repeat, favors variety
/// - Light: Default weights (no modification)
/// - Normal: Boosts Repeat and RhythmRepeat (catchy patterns)
/// - Strong: Greatly boosts Repeat and AscendDrop (most memorable)
///
/// @param base Base weights from section type
/// @param intensity Hook intensity level
/// @returns Modified weights
inline SkeletonWeights applyHookIntensityToWeights(const SkeletonWeights& base,
                                                   HookIntensity intensity) {
  SkeletonWeights result = base;

  switch (intensity) {
    case HookIntensity::Off:
      // Reduce repetitive patterns, favor variety
      result.repeat *= 0.5f;
      result.rhythm_repeat *= 0.6f;
      result.ascending *= 1.2f;
      result.leap_return *= 1.3f;
      result.peak_drop *= 1.1f;
      result.pendulum *= 1.2f;
      result.descent_resolve *= 1.1f;
      result.call_response *= 1.0f;
      result.syncopated *= 1.1f;
      result.chromatic_slide *= 0.8f;
      // Extended patterns: favor variety
      result.double_ascend *= 1.2f;
      result.staircase *= 1.3f;
      result.triple_hit *= 0.6f;  // Less repetitive
      result.wide_arch *= 1.2f;
      result.narrow_pendulum *= 1.1f;
      result.question_mark *= 1.2f;
      // Phase 3 patterns: favor variety
      result.stepwise_descent *= 1.2f;
      result.octave_leap *= 1.3f;
      result.suspend_resolve *= 1.1f;
      result.symmetric_arch *= 1.2f;
      result.anticipation_build *= 1.1f;
      result.echo_phrasing *= 1.2f;
      result.stutter_repeat *= 0.6f;  // Less repetitive
      break;

    case HookIntensity::Light:
      // Default weights - no modification
      break;

    case HookIntensity::Normal:
      // Boost catchy patterns
      result.repeat *= 1.3f;
      result.rhythm_repeat *= 1.4f;
      result.ascend_drop *= 1.1f;
      result.peak_drop *= 1.1f;
      result.call_response *= 1.2f;
      result.syncopated *= 1.1f;
      // Extended patterns: boost catchy ones
      result.double_ascend *= 1.2f;
      result.triple_hit *= 1.4f;  // Emphasis is catchy
      result.wide_arch *= 1.1f;
      // Phase 3 patterns: moderate boost
      result.stepwise_descent *= 1.2f;
      result.octave_leap *= 1.1f;
      result.suspend_resolve *= 1.1f;
      result.symmetric_arch *= 1.2f;
      result.anticipation_build *= 1.1f;
      result.stutter_repeat *= 1.3f;  // Catchy stutter
      break;

    case HookIntensity::Strong:
      // Maximum memorability
      result.repeat *= 1.8f;
      result.ascend_drop *= 1.5f;
      result.rhythm_repeat *= 1.6f;
      result.ascending *= 1.2f;
      result.peak_drop *= 1.3f;
      result.call_response *= 1.4f;
      result.chromatic_slide *= 1.2f;
      // Extended patterns: maximize catchiness
      result.double_ascend *= 1.4f;
      result.triple_hit *= 1.7f;  // Very catchy
      result.wide_arch *= 1.3f;
      result.staircase *= 1.1f;
      // Phase 3 patterns: maximize memorability
      result.stepwise_descent *= 1.5f;  // Strong emotional impact
      result.octave_leap *= 1.4f;       // Dramatic
      result.suspend_resolve *= 1.3f;
      result.symmetric_arch *= 1.4f;    // Satisfying balance
      result.anticipation_build *= 1.2f;
      result.stutter_repeat *= 1.6f;    // Very catchy
      break;

    case HookIntensity::Maximum:
      // Behavioral Loop: extreme repetition, simple patterns only
      // Heavily boost simple repetitive patterns
      result.repeat *= 3.0f;
      result.triple_hit *= 2.5f;
      result.call_response *= 2.5f;
      result.rhythm_repeat *= 2.5f;

      // Suppress complex patterns (these create variety, not addiction)
      result.pendulum *= 0.2f;
      result.staircase *= 0.2f;
      result.wide_arch *= 0.3f;
      result.question_mark *= 0.2f;
      result.chromatic_slide *= 0.3f;
      result.descent_resolve *= 0.4f;

      // Moderate patterns: some boost but not extreme
      result.ascending *= 1.5f;
      result.ascend_drop *= 1.3f;
      result.double_ascend *= 1.2f;
      result.narrow_pendulum *= 0.5f;
      result.peak_drop *= 0.6f;
      result.syncopated *= 0.5f;
      result.leap_return *= 0.4f;

      // Phase 3 patterns: addictive ones boosted, complex suppressed
      result.stepwise_descent *= 1.5f;    // Simple, effective
      result.octave_leap *= 0.4f;         // Too dramatic for loops
      result.suspend_resolve *= 1.3f;     // Simple tension-release
      result.symmetric_arch *= 0.5f;      // Too complex
      result.anticipation_build *= 0.4f;  // Not loopable
      result.echo_phrasing *= 1.2f;       // Good for repetition
      result.stutter_repeat *= 2.0f;      // Very addictive
      break;
  }

  return result;
}

/// @brief Select a hook skeleton based on section type and hook intensity.
/// @param type Section type
/// @param rng Random number generator
/// @param intensity Hook intensity (default: Normal)
/// @returns Selected HookSkeleton
inline HookSkeleton selectHookSkeleton(SectionType type, std::mt19937& rng,
                                       HookIntensity intensity = HookIntensity::Normal) {
  // Get base weights from section type
  const SkeletonWeights& base_weights =
      (type == SectionType::Chorus) ? kChorusSkeletonWeights : kDefaultSkeletonWeights;

  // Apply HookIntensity modifier
  SkeletonWeights weights = applyHookIntensityToWeights(base_weights, intensity);

  float total = weights.repeat + weights.ascending + weights.ascend_drop + weights.leap_return +
                weights.rhythm_repeat + weights.peak_drop + weights.pendulum +
                weights.descent_resolve + weights.call_response + weights.syncopated +
                weights.chromatic_slide + weights.double_ascend + weights.staircase +
                weights.triple_hit + weights.wide_arch + weights.narrow_pendulum +
                weights.question_mark +
                // Phase 3: New patterns
                weights.stepwise_descent + weights.octave_leap + weights.suspend_resolve +
                weights.symmetric_arch + weights.anticipation_build + weights.echo_phrasing +
                weights.stutter_repeat;

  std::uniform_real_distribution<float> dist(0.0f, total);
  float roll = dist(rng);

  float cumulative = 0.0f;
  cumulative += weights.repeat;
  if (roll < cumulative) return HookSkeleton::Repeat;

  cumulative += weights.ascending;
  if (roll < cumulative) return HookSkeleton::Ascending;

  cumulative += weights.ascend_drop;
  if (roll < cumulative) return HookSkeleton::AscendDrop;

  cumulative += weights.leap_return;
  if (roll < cumulative) return HookSkeleton::LeapReturn;

  cumulative += weights.rhythm_repeat;
  if (roll < cumulative) return HookSkeleton::RhythmRepeat;

  cumulative += weights.peak_drop;
  if (roll < cumulative) return HookSkeleton::PeakDrop;

  cumulative += weights.pendulum;
  if (roll < cumulative) return HookSkeleton::Pendulum;

  cumulative += weights.descent_resolve;
  if (roll < cumulative) return HookSkeleton::DescentResolve;

  cumulative += weights.call_response;
  if (roll < cumulative) return HookSkeleton::CallResponse;

  cumulative += weights.syncopated;
  if (roll < cumulative) return HookSkeleton::Syncopated;

  cumulative += weights.chromatic_slide;
  if (roll < cumulative) return HookSkeleton::ChromaticSlide;

  cumulative += weights.double_ascend;
  if (roll < cumulative) return HookSkeleton::DoubleAscend;

  cumulative += weights.staircase;
  if (roll < cumulative) return HookSkeleton::Staircase;

  cumulative += weights.triple_hit;
  if (roll < cumulative) return HookSkeleton::TripleHit;

  cumulative += weights.wide_arch;
  if (roll < cumulative) return HookSkeleton::WideArch;

  cumulative += weights.narrow_pendulum;
  if (roll < cumulative) return HookSkeleton::NarrowPendulum;

  cumulative += weights.question_mark;
  if (roll < cumulative) return HookSkeleton::QuestionMark;

  // Phase 3: New patterns
  cumulative += weights.stepwise_descent;
  if (roll < cumulative) return HookSkeleton::StepwiseDescent;

  cumulative += weights.octave_leap;
  if (roll < cumulative) return HookSkeleton::OctaveLeap;

  cumulative += weights.suspend_resolve;
  if (roll < cumulative) return HookSkeleton::SuspendResolve;

  cumulative += weights.symmetric_arch;
  if (roll < cumulative) return HookSkeleton::SymmetricArch;

  cumulative += weights.anticipation_build;
  if (roll < cumulative) return HookSkeleton::AnticipationBuild;

  cumulative += weights.echo_phrasing;
  if (roll < cumulative) return HookSkeleton::EchoPhrasing;

  return HookSkeleton::StutterRepeat;
}

/// @brief Select a betrayal type for hook variation.
///
/// First occurrence should use None. Later repetitions apply betrayal.
///
/// @param repetition_index Which repetition (0 = first, no betrayal)
/// @param rng Random number generator
/// @returns Selected HookBetrayal
inline HookBetrayal selectBetrayal(int repetition_index, std::mt19937& rng) {
  // First occurrence: exact repetition
  if (repetition_index == 0) {
    return HookBetrayal::None;
  }

  // Later repetitions: weighted selection of betrayals
  constexpr float kLastPitchWeight = 1.5f;   // Most common
  constexpr float kExtendOneWeight = 1.2f;   // Dramatic
  constexpr float kSingleRestWeight = 0.8f;  // Breathing
  constexpr float kSingleLeapWeight = 0.5f;  // Less common

  float total = kLastPitchWeight + kExtendOneWeight + kSingleRestWeight + kSingleLeapWeight;

  std::uniform_real_distribution<float> dist(0.0f, total);
  float roll = dist(rng);

  float cumulative = 0.0f;
  cumulative += kLastPitchWeight;
  if (roll < cumulative) return HookBetrayal::LastPitch;

  cumulative += kExtendOneWeight;
  if (roll < cumulative) return HookBetrayal::ExtendOne;

  cumulative += kSingleRestWeight;
  if (roll < cumulative) return HookBetrayal::SingleRest;

  return HookBetrayal::SingleLeap;
}

/// @brief Expand a hook skeleton to actual MIDI pitches.
/// @param skeleton The hook skeleton pattern
/// @param base_pitch Base MIDI pitch (will be on chord tone)
/// @param vocal_low Minimum vocal pitch
/// @param vocal_high Maximum vocal pitch
/// @returns Vector of MIDI pitches (-1 = rest)
inline std::vector<int8_t> expandSkeletonToPitches(HookSkeleton skeleton, int base_pitch,
                                                   uint8_t vocal_low, uint8_t vocal_high) {
  SkeletonPattern pattern = getSkeletonPattern(skeleton);
  std::vector<int8_t> pitches;
  pitches.reserve(pattern.length);

  for (size_t i = 0; i < pattern.length; ++i) {
    int8_t interval = pattern.intervals[i];

    if (interval == -128) {
      // Rest marker
      pitches.push_back(-1);
      continue;
    }

    // Convert scale degree offset to semitones
    int semitones = scaleDegreesToSemitones(interval);
    int pitch = base_pitch + semitones;

    // Clamp to vocal range
    pitch = std::clamp(pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));

    pitches.push_back(static_cast<int8_t>(pitch));
  }

  return pitches;
}

/// @brief Apply a betrayal to a note sequence.
/// @param pitches Pitch sequence to modify (in-place)
/// @param durations Duration sequence to modify (in-place)
/// @param betrayal Betrayal type to apply
/// @param rng Random number generator
inline void applyBetrayal(std::vector<int8_t>& pitches, std::vector<Tick>& durations,
                          HookBetrayal betrayal, std::mt19937& rng) {
  if (pitches.empty() || betrayal == HookBetrayal::None) {
    return;
  }

  switch (betrayal) {
    case HookBetrayal::LastPitch: {
      // Modify final pitch by ±1-2 scale degrees
      size_t last = pitches.size() - 1;
      while (last > 0 && pitches[last] < 0) --last;  // Skip rests
      if (pitches[last] >= 0) {
        int shift = (rng() % 2 == 0) ? -2 : 2;  // ±2 semitones
        pitches[last] = static_cast<int8_t>(pitches[last] + shift);
      }
      break;
    }

    case HookBetrayal::ExtendOne: {
      // Extend last note by 50%
      if (!durations.empty()) {
        durations.back() = durations.back() * 3 / 2;
      }
      break;
    }

    case HookBetrayal::SingleRest: {
      // Insert rest before last note (shorten second-to-last)
      if (pitches.size() >= 2 && durations.size() >= 2) {
        size_t idx = pitches.size() - 2;
        durations[idx] = durations[idx] * 2 / 3;
        // Gap will be handled by caller
      }
      break;
    }

    case HookBetrayal::SingleLeap: {
      // Add unexpected leap to one middle note
      if (pitches.size() >= 3) {
        size_t mid = pitches.size() / 2;
        if (pitches[mid] >= 0) {
          int leap = (rng() % 2 == 0) ? -5 : 5;  // 4th leap
          pitches[mid] = static_cast<int8_t>(pitches[mid] + leap);
        }
      }
      break;
    }

    default:
      break;
  }
}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_HOOK_UTILS_H
