/**
 * @file motif_transform.cpp
 * @brief Implementation of GlobalMotif transformation functions.
 */

#include "core/motif_transform.h"

#include <algorithm>
#include <cmath>

#include "core/section_types.h"

namespace midisketch {

GlobalMotif transformGlobalMotif(const GlobalMotif& source, GlobalMotifTransform transform,
                                 int8_t param) {
  switch (transform) {
    case GlobalMotifTransform::None:
      return source;
    case GlobalMotifTransform::Invert:
      return invertMotif(source);
    case GlobalMotifTransform::Augment:
      return augmentMotif(source);
    case GlobalMotifTransform::Diminish:
      return diminishMotif(source);
    case GlobalMotifTransform::Fragment:
      return fragmentMotif(source);
    case GlobalMotifTransform::Sequence:
      return sequenceMotif(source, param);
    case GlobalMotifTransform::Retrograde:
      return retrogradeMotif(source);
    default:
      return source;
  }
}

GlobalMotif invertMotif(const GlobalMotif& source) {
  GlobalMotif result = source;

  // Invert interval directions
  for (uint8_t i = 0; i < result.interval_count && i < 8; ++i) {
    result.interval_signature[i] = -result.interval_signature[i];
  }

  // Invert contour type
  switch (source.contour_type) {
    case ContourType::Ascending:
      result.contour_type = ContourType::Descending;
      break;
    case ContourType::Descending:
      result.contour_type = ContourType::Ascending;
      break;
    case ContourType::Peak:
      result.contour_type = ContourType::Valley;
      break;
    case ContourType::Valley:
      result.contour_type = ContourType::Peak;
      break;
    case ContourType::Plateau:
      // Plateau remains plateau
      break;
  }

  return result;
}

GlobalMotif augmentMotif(const GlobalMotif& source) {
  GlobalMotif result = source;

  // Double rhythm values (capped at 255)
  for (uint8_t i = 0; i < result.rhythm_count && i < 8; ++i) {
    uint16_t doubled = static_cast<uint16_t>(result.rhythm_signature[i]) * 2;
    result.rhythm_signature[i] = static_cast<uint8_t>(std::min<uint16_t>(doubled, 255));
  }

  return result;
}

GlobalMotif diminishMotif(const GlobalMotif& source) {
  GlobalMotif result = source;

  // Halve rhythm values (minimum 1)
  for (uint8_t i = 0; i < result.rhythm_count && i < 8; ++i) {
    result.rhythm_signature[i] = std::max<uint8_t>(result.rhythm_signature[i] / 2, 1);
  }

  return result;
}

GlobalMotif fragmentMotif(const GlobalMotif& source) {
  GlobalMotif result = source;

  // Take first half of intervals
  uint8_t half_intervals = (source.interval_count + 1) / 2;
  result.interval_count = half_intervals;

  // Clear the rest
  for (uint8_t i = half_intervals; i < 8; ++i) {
    result.interval_signature[i] = 0;
  }

  // Take first half of rhythm
  uint8_t half_rhythm = (source.rhythm_count + 1) / 2;
  result.rhythm_count = half_rhythm;

  // Clear the rest
  for (uint8_t i = half_rhythm; i < 8; ++i) {
    result.rhythm_signature[i] = 0;
  }

  return result;
}

GlobalMotif sequenceMotif(const GlobalMotif& source, int8_t degree_shift) {
  GlobalMotif result = source;

  // Apply degree_shift to all intervals in the signature.
  // This creates a melodic sequence: same contour pattern with
  // wider or narrower intervals, preserving rhythm and contour type.
  for (uint8_t idx = 0; idx < result.interval_count && idx < 8; ++idx) {
    int16_t shifted =
        static_cast<int16_t>(result.interval_signature[idx]) + degree_shift;
    result.interval_signature[idx] = static_cast<int8_t>(
        std::clamp(shifted, static_cast<int16_t>(-12),
                   static_cast<int16_t>(12)));
  }

  return result;
}

GlobalMotif retrogradeMotif(const GlobalMotif& source) {
  GlobalMotif result = source;

  // Reverse interval sequence
  if (result.interval_count > 1) {
    for (uint8_t i = 0; i < result.interval_count / 2; ++i) {
      std::swap(result.interval_signature[i],
                result.interval_signature[result.interval_count - 1 - i]);
    }
  }

  // Reverse rhythm sequence
  if (result.rhythm_count > 1) {
    for (uint8_t i = 0; i < result.rhythm_count / 2; ++i) {
      std::swap(result.rhythm_signature[i], result.rhythm_signature[result.rhythm_count - 1 - i]);
    }
  }

  // Contour is reversed
  switch (source.contour_type) {
    case ContourType::Ascending:
      result.contour_type = ContourType::Descending;
      break;
    case ContourType::Descending:
      result.contour_type = ContourType::Ascending;
      break;
    default:
      // Peak, Valley, and Plateau remain the same in retrograde
      break;
  }

  return result;
}

float calculateMotifSimilarity(const GlobalMotif& a, const GlobalMotif& b) {
  if (!a.isValid() || !b.isValid()) {
    return 0.0f;
  }

  float score = 0.0f;

  // Contour similarity (weight: 0.3)
  if (a.contour_type == b.contour_type) {
    score += 0.3f;
  } else {
    // Partial credit for related contours
    if ((a.contour_type == ContourType::Ascending &&
         b.contour_type == ContourType::Peak) ||
        (a.contour_type == ContourType::Peak &&
         b.contour_type == ContourType::Ascending) ||
        (a.contour_type == ContourType::Descending &&
         b.contour_type == ContourType::Valley) ||
        (a.contour_type == ContourType::Valley &&
         b.contour_type == ContourType::Descending)) {
      score += 0.15f;
    }
  }

  // Interval signature similarity (weight: 0.5)
  uint8_t min_intervals = std::min(a.interval_count, b.interval_count);
  if (min_intervals > 0) {
    float interval_match = 0.0f;
    for (uint8_t i = 0; i < min_intervals; ++i) {
      int diff = std::abs(a.interval_signature[i] - b.interval_signature[i]);
      if (diff == 0) {
        interval_match += 1.0f;
      } else if (diff <= 2) {
        interval_match += 0.5f;  // Close match
      } else if (diff <= 4) {
        interval_match += 0.25f;  // Similar direction
      }
    }
    score += 0.5f * (interval_match / min_intervals);
  }

  // Rhythm similarity (weight: 0.2)
  uint8_t min_rhythm = std::min(a.rhythm_count, b.rhythm_count);
  if (min_rhythm > 0) {
    float rhythm_match = 0.0f;
    for (uint8_t i = 0; i < min_rhythm; ++i) {
      uint8_t ratio_a = a.rhythm_signature[i];
      uint8_t ratio_b = b.rhythm_signature[i];
      // Allow some tolerance in rhythm ratios
      int diff = std::abs(static_cast<int>(ratio_a) - static_cast<int>(ratio_b));
      if (diff == 0) {
        rhythm_match += 1.0f;
      } else if (diff <= 1) {
        rhythm_match += 0.7f;
      } else if (diff <= 2) {
        rhythm_match += 0.3f;
      }
    }
    score += 0.2f * (rhythm_match / min_rhythm);
  }

  return std::clamp(score, 0.0f, 1.0f);
}

// Recommended motif transformation for each section type.
// Indexed by SectionType enum value (0-9).
// clang-format off
constexpr GlobalMotifTransform kSectionMotifTransform[10] = {
    GlobalMotifTransform::Retrograde,  // 0: Intro - instrumental interest
    GlobalMotifTransform::Diminish,    // 1: A - slightly lower energy
    GlobalMotifTransform::Sequence,    // 2: B - building tension
    GlobalMotifTransform::None,        // 3: Chorus - original (strongest recognition)
    GlobalMotifTransform::Invert,      // 4: Bridge - contrast
    GlobalMotifTransform::Retrograde,  // 5: Interlude - instrumental interest
    GlobalMotifTransform::Fragment,    // 6: Outro - winding down
    GlobalMotifTransform::Augment,     // 7: Chant - emphasis
    GlobalMotifTransform::Augment,     // 8: MixBreak - emphasis
    GlobalMotifTransform::None,        // 9: Drop - original energy
};
// clang-format on

GlobalMotifTransform getRecommendedTransformForSection(SectionType section_type) {
  uint8_t idx = static_cast<uint8_t>(section_type);
  if (idx < sizeof(kSectionMotifTransform) / sizeof(kSectionMotifTransform[0])) {
    return kSectionMotifTransform[idx];
  }
  return GlobalMotifTransform::None;  // fallback
}

}  // namespace midisketch
