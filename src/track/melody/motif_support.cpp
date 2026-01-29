/**
 * @file motif_support.cpp
 * @brief Implementation of GlobalMotif extraction and evaluation.
 */

#include "track/melody/motif_support.h"

#include <algorithm>
#include <cmath>

namespace midisketch {
namespace melody {

GlobalMotif extractGlobalMotifImpl(const std::vector<NoteEvent>& notes) {
  GlobalMotif motif;

  if (notes.size() < 2) {
    return motif;  // Not enough notes for meaningful analysis
  }

  // Extract interval signature (relative pitch changes)
  size_t interval_limit = std::min(notes.size() - 1, static_cast<size_t>(8));
  for (size_t i = 0; i < interval_limit; ++i) {
    int interval = static_cast<int>(notes[i + 1].note) - static_cast<int>(notes[i].note);
    motif.interval_signature[i] = static_cast<int8_t>(std::clamp(interval, -12, 12));
  }
  motif.interval_count = static_cast<uint8_t>(interval_limit);

  // Extract rhythm signature (relative durations)
  Tick max_duration = 0;
  for (size_t i = 0; i < std::min(notes.size(), static_cast<size_t>(8)); ++i) {
    if (notes[i].duration > max_duration) {
      max_duration = notes[i].duration;
    }
  }
  if (max_duration > 0) {
    for (size_t i = 0; i < std::min(notes.size(), static_cast<size_t>(8)); ++i) {
      // Normalize to 0-8 scale (8 = longest note)
      uint8_t ratio = static_cast<uint8_t>((notes[i].duration * 8) / max_duration);
      motif.rhythm_signature[i] =
          std::clamp(ratio, static_cast<uint8_t>(1), static_cast<uint8_t>(8));
    }
    motif.rhythm_count = static_cast<uint8_t>(std::min(notes.size(), static_cast<size_t>(8)));
  }

  // Analyze contour type
  if (motif.interval_count >= 2) {
    int first_half_sum = 0, second_half_sum = 0;
    size_t mid = motif.interval_count / 2;

    for (size_t i = 0; i < mid; ++i) {
      first_half_sum += motif.interval_signature[i];
    }
    for (size_t i = mid; i < motif.interval_count; ++i) {
      second_half_sum += motif.interval_signature[i];
    }

    // Determine contour based on directional changes
    int total_movement = first_half_sum + second_half_sum;
    bool first_rising = first_half_sum > 0;
    bool first_falling = first_half_sum < 0;
    bool second_rising = second_half_sum > 0;
    bool second_falling = second_half_sum < 0;

    // Peak/Valley: significant direction reversal
    if (first_rising && second_falling && std::abs(first_half_sum) >= 3) {
      motif.contour_type = ContourType::Peak;
    } else if (first_falling && second_rising && std::abs(first_half_sum) >= 3) {
      motif.contour_type = ContourType::Valley;
    } else if (std::abs(first_half_sum) < 3 && std::abs(second_half_sum) < 3) {
      // Both halves have little movement = plateau
      motif.contour_type = ContourType::Plateau;
    } else if (total_movement > 0) {
      motif.contour_type = ContourType::Ascending;
    } else {
      motif.contour_type = ContourType::Descending;
    }
  }

  return motif;
}

float evaluateWithGlobalMotifImpl(const std::vector<NoteEvent>& candidate,
                                  const GlobalMotif& global_motif) {
  if (!global_motif.isValid() || candidate.size() < 2) {
    return 0.0f;
  }

  float bonus = 0.0f;

  // Extract candidate's contour
  GlobalMotif candidate_motif = extractGlobalMotifImpl(candidate);

  // Contour similarity bonus (0.0-0.10)
  // Increased from 0.05 to strengthen melodic coherence across sections.
  if (candidate_motif.contour_type == global_motif.contour_type) {
    bonus += 0.10f;
  }

  // Interval pattern similarity bonus (0.0-0.05)
  if (candidate_motif.interval_count > 0 && global_motif.interval_count > 0) {
    size_t compare_count = std::min(static_cast<size_t>(candidate_motif.interval_count),
                                    static_cast<size_t>(global_motif.interval_count));

    int similarity_score = 0;
    for (size_t i = 0; i < compare_count; ++i) {
      int diff =
          std::abs(candidate_motif.interval_signature[i] - global_motif.interval_signature[i]);
      // Award points for similar intervals (within 2 semitones)
      if (diff <= 2) {
        similarity_score += (3 - diff);  // 3 for exact, 2 for 1 off, 1 for 2 off
      }
    }

    // Normalize to 0.0-0.05 range
    float max_score = static_cast<float>(compare_count * 3);
    if (max_score > 0.0f) {
      bonus += (static_cast<float>(similarity_score) / max_score) * 0.05f;
    }
  }

  // Contour direction matching bonus (0.0-0.05)
  // Rewards candidates whose individual interval directions match the DNA pattern.
  // If the DNA goes up at position N, ascending intervals at that position get a bonus.
  if (candidate_motif.interval_count > 0 && global_motif.interval_count > 0) {
    size_t compare_count = std::min(static_cast<size_t>(candidate_motif.interval_count),
                                    static_cast<size_t>(global_motif.interval_count));
    int direction_matches = 0;
    for (size_t idx = 0; idx < compare_count; ++idx) {
      int cand_dir = (candidate_motif.interval_signature[idx] > 0)
                         ? 1
                         : (candidate_motif.interval_signature[idx] < 0 ? -1 : 0);
      int motif_dir = (global_motif.interval_signature[idx] > 0)
                          ? 1
                          : (global_motif.interval_signature[idx] < 0 ? -1 : 0);
      if (cand_dir == motif_dir && cand_dir != 0) {
        direction_matches++;
      }
    }
    // Normalize: each matching direction contributes proportionally
    if (compare_count > 0) {
      bonus +=
          (static_cast<float>(direction_matches) / static_cast<float>(compare_count)) * 0.05f;
    }
  }

  // Interval consistency bonus (0.0-0.05)
  // Rewards candidates that preserve the step-vs-leap character of the DNA.
  // Steps (1-2 semitones) matching steps, and leaps (3+) matching leaps.
  if (candidate_motif.interval_count > 0 && global_motif.interval_count > 0) {
    size_t compare_count = std::min(static_cast<size_t>(candidate_motif.interval_count),
                                    static_cast<size_t>(global_motif.interval_count));
    int consistency_matches = 0;
    for (size_t idx = 0; idx < compare_count; ++idx) {
      int cand_abs = std::abs(candidate_motif.interval_signature[idx]);
      int motif_abs = std::abs(global_motif.interval_signature[idx]);
      bool cand_is_step = (cand_abs >= 1 && cand_abs <= 2);
      bool motif_is_step = (motif_abs >= 1 && motif_abs <= 2);
      // Both steps or both leaps (3+ semitones)
      if (cand_is_step == motif_is_step && (cand_abs > 0 || motif_abs > 0)) {
        consistency_matches++;
      }
    }
    if (compare_count > 0) {
      bonus +=
          (static_cast<float>(consistency_matches) / static_cast<float>(compare_count)) * 0.05f;
    }
  }

  return bonus;
}

}  // namespace melody
}  // namespace midisketch
