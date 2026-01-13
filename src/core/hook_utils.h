/**
 * @file hook_utils.h
 * @brief Hook generation utilities using HookSkeleton and HookBetrayal.
 *
 * Implements the "select, not create" philosophy for memorable hooks.
 * Hooks are selected from predefined patterns, not randomly generated.
 */

#ifndef MIDISKETCH_CORE_HOOK_UTILS_H
#define MIDISKETCH_CORE_HOOK_UTILS_H

#include "core/melody_types.h"
#include "core/pitch_utils.h"
#include "core/types.h"
#include <array>
#include <random>
#include <vector>

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
  }
  return {{0, 0, 0, 0, 0}, 3};  // Default: repeat
}

/// @brief Weight map for hook skeleton selection.
struct SkeletonWeights {
  float repeat;       ///< Weight for Repeat skeleton
  float ascending;    ///< Weight for Ascending skeleton
  float ascend_drop;  ///< Weight for AscendDrop skeleton
  float leap_return;  ///< Weight for LeapReturn skeleton
  float rhythm_repeat;///< Weight for RhythmRepeat skeleton
};

/// @brief Default weights for Chorus sections (memorability focused).
/// Order: repeat, ascending, ascend_drop, leap_return, rhythm_repeat
constexpr SkeletonWeights kChorusSkeletonWeights = {
    1.5f,   // repeat - Most memorable
    1.3f,   // ascending - Energy building
    1.0f,   // ascend_drop - Natural arc
    0.7f,   // leap_return - Less common
    1.2f,   // rhythm_repeat - Catchy rhythm
};

/// @brief Default weights for non-Chorus sections.
constexpr SkeletonWeights kDefaultSkeletonWeights = {
    1.0f,   // repeat
    1.0f,   // ascending
    1.0f,   // ascend_drop
    0.8f,   // leap_return
    0.9f,   // rhythm_repeat
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
inline SkeletonWeights applyHookIntensityToWeights(
    const SkeletonWeights& base,
    HookIntensity intensity) {

  SkeletonWeights result = base;

  switch (intensity) {
    case HookIntensity::Off:
      // Reduce repetitive patterns, favor variety
      result.repeat *= 0.5f;
      result.rhythm_repeat *= 0.6f;
      result.ascending *= 1.2f;
      result.leap_return *= 1.3f;
      break;

    case HookIntensity::Light:
      // Default weights - no modification
      break;

    case HookIntensity::Normal:
      // Boost catchy patterns
      result.repeat *= 1.3f;
      result.rhythm_repeat *= 1.4f;
      result.ascend_drop *= 1.1f;
      break;

    case HookIntensity::Strong:
      // Maximum memorability
      result.repeat *= 1.8f;
      result.ascend_drop *= 1.5f;
      result.rhythm_repeat *= 1.6f;
      result.ascending *= 1.2f;
      break;
  }

  return result;
}

/// @brief Select a hook skeleton based on section type and hook intensity.
/// @param type Section type
/// @param rng Random number generator
/// @param intensity Hook intensity (default: Normal)
/// @returns Selected HookSkeleton
inline HookSkeleton selectHookSkeleton(
    SectionType type,
    std::mt19937& rng,
    HookIntensity intensity = HookIntensity::Normal) {

  // Get base weights from section type
  const SkeletonWeights& base_weights =
      (type == SectionType::Chorus)
          ? kChorusSkeletonWeights
          : kDefaultSkeletonWeights;

  // Apply HookIntensity modifier
  SkeletonWeights weights = applyHookIntensityToWeights(base_weights, intensity);

  float total = weights.repeat + weights.ascending + weights.ascend_drop +
                weights.leap_return + weights.rhythm_repeat;

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

  return HookSkeleton::RhythmRepeat;
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

  float total = kLastPitchWeight + kExtendOneWeight +
                kSingleRestWeight + kSingleLeapWeight;

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
inline std::vector<int8_t> expandSkeletonToPitches(
    HookSkeleton skeleton,
    int base_pitch,
    uint8_t vocal_low,
    uint8_t vocal_high) {

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
    pitch = std::clamp(pitch, static_cast<int>(vocal_low),
                       static_cast<int>(vocal_high));

    pitches.push_back(static_cast<int8_t>(pitch));
  }

  return pitches;
}

/// @brief Apply a betrayal to a note sequence.
/// @param pitches Pitch sequence to modify (in-place)
/// @param durations Duration sequence to modify (in-place)
/// @param betrayal Betrayal type to apply
/// @param rng Random number generator
inline void applyBetrayal(std::vector<int8_t>& pitches,
                          std::vector<Tick>& durations,
                          HookBetrayal betrayal,
                          std::mt19937& rng) {
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
