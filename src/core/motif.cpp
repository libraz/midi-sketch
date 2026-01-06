#include "core/motif.h"
#include <algorithm>

namespace midisketch {

Motif applyVariation(const Motif& original, MotifVariation variation,
                     int8_t param, std::mt19937& rng) {
  Motif result = original;

  switch (variation) {
    case MotifVariation::Exact:
      // No changes needed
      break;

    case MotifVariation::Transposed:
      // Shift all contour degrees by param
      for (auto& degree : result.contour_degrees) {
        degree += param;
      }
      break;

    case MotifVariation::Inverted:
      // Invert contour around the first note
      if (!result.contour_degrees.empty()) {
        int8_t pivot = result.contour_degrees[0];
        for (auto& degree : result.contour_degrees) {
          degree = pivot - (degree - pivot);
        }
      }
      break;

    case MotifVariation::Augmented:
      // Double all durations
      for (auto& rn : result.rhythm) {
        rn.eighths *= 2;
        rn.beat *= 2.0f;
      }
      result.length_beats *= 2;
      break;

    case MotifVariation::Diminished:
      // Halve all durations
      for (auto& rn : result.rhythm) {
        rn.eighths = std::max(1, rn.eighths / 2);
        rn.beat /= 2.0f;
      }
      result.length_beats /= 2;
      break;

    case MotifVariation::Fragmented:
      // Use only the first half
      if (result.rhythm.size() > 2) {
        size_t half = result.rhythm.size() / 2;
        result.rhythm.resize(half);
        if (result.contour_degrees.size() > half) {
          result.contour_degrees.resize(half);
        }
        result.length_beats /= 2;
      }
      break;

    case MotifVariation::Sequenced:
      // Apply sequential transposition (each repetition shifts by param)
      // This version just applies a single step
      for (size_t i = 0; i < result.contour_degrees.size(); ++i) {
        result.contour_degrees[i] += static_cast<int8_t>(i) * param / 4;
      }
      break;

    case MotifVariation::Embellished:
      // Add passing tones (simplified: just add some variation to degrees)
      {
        std::uniform_int_distribution<int> dist(-1, 1);
        for (size_t i = 1; i < result.contour_degrees.size() - 1; ++i) {
          if (!result.rhythm[i].strong) {
            result.contour_degrees[i] += static_cast<int8_t>(dist(rng));
          }
        }
      }
      break;
  }

  return result;
}

Motif designChorusHook(const StyleMelodyParams& params, std::mt19937& rng) {
  Motif hook;
  hook.length_beats = 8;  // 2-bar hook
  hook.ends_on_chord_tone = true;

  if (params.hook_repetition) {
    // Idol/Anime style: catchy, repetitive rhythm
    hook.rhythm = {
        {0.0f, 4, true},   // Beat 1: half note
        {2.0f, 2, true},   // Beat 3: quarter note
        {3.0f, 2, false},  // Beat 4: quarter note
        {4.0f, 4, true},   // Beat 1 (bar 2): half note - climax
        {6.0f, 2, true},   // Beat 3: quarter note
        {7.0f, 2, false},  // Beat 4: quarter note
    };
    hook.climax_index = 3;  // Fourth note is the climax

    // Ascending-then-descending contour for memorable hook
    hook.contour_degrees = {0, 2, 4, 4, 7, 4, 0};
  } else {
    // Standard style: gradual arch contour
    hook.rhythm = {
        {0.0f, 2, true},   // Beat 1: quarter note
        {1.0f, 2, false},  // Beat 2: quarter note
        {2.0f, 2, true},   // Beat 3: quarter note
        {3.0f, 2, false},  // Beat 4: quarter note
        {4.0f, 3, true},   // Beat 1 (bar 2): dotted quarter - climax
        {5.5f, 2, false},  // Beat 2.5: quarter note
        {6.5f, 3, true},   // Beat 3.5: dotted quarter
    };
    hook.climax_index = 4;  // Fifth note is the climax

    // Smooth arch contour
    hook.contour_degrees = {0, 2, 4, 5, 7, 5, 4, 2, 0};
  }

  // Adjust contour size to match rhythm size
  while (hook.contour_degrees.size() < hook.rhythm.size()) {
    hook.contour_degrees.push_back(0);
  }
  if (hook.contour_degrees.size() > hook.rhythm.size()) {
    hook.contour_degrees.resize(hook.rhythm.size());
  }

  // Add some randomization based on density
  if (params.note_density >= 1.0f) {
    // High density: add some variation
    std::uniform_int_distribution<int> var_dist(-1, 1);
    for (size_t i = 1; i < hook.contour_degrees.size() - 1; ++i) {
      if (!hook.rhythm[i].strong) {
        hook.contour_degrees[i] += static_cast<int8_t>(var_dist(rng));
      }
    }
  }

  return hook;
}

}  // namespace midisketch
