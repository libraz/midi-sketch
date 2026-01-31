/**
 * @file melody_evaluator.cpp
 * @brief Implementation of melody quality scoring.
 */

#include "core/melody_evaluator.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "core/i_harmony_context.h"
#include "core/pitch_utils.h"
#include "core/types.h"
#include "core/vocal_style_profile.h"

namespace midisketch {

// ============================================================================
// Singability vs Culling Penalties: Role Distinction for Large Leaps
// ============================================================================
//
// calcSingability (below):
//   Role:     "Tendency evaluation" - measures interval DISTRIBUTION quality
//   Target:   5-10% large leaps is ideal for pop vocals
//   Effect:   Soft penalty when ratio exceeds 10%
//   Purpose:  Guide melody toward singable contours (macro-level quality)
//
// evaluateForCulling penalties (calcLeapAfterHighPenalty, etc.):
//   Role:     "Accident prevention" - catches specific DANGEROUS patterns
//   Target:   Absolute violations (large leap TO high register, etc.)
//   Effect:   Direct point deduction for risky combinations
//   Purpose:  Hard gate against physically difficult passages (micro-level danger)
//
// Both affect large leaps, but serve different purposes:
// - Singability = statistical preference (overall balance)
// - Culling penalties = safety filter (specific dangerous patterns)
//
// ============================================================================

float MelodyEvaluator::calcSingability(const std::vector<NoteEvent>& notes) {
  if (notes.size() < 2) return 0.5f;

  // Count intervals by category for detailed scoring
  int same_count = 0;        // 0 semitones
  int step_count = 0;        // 1-2 semitones (true step motion)
  int small_leap_count = 0;  // 3-4 semitones (small leaps)
  int large_leap_count = 0;  // 5+ semitones

  for (size_t i = 1; i < notes.size(); ++i) {
    int interval = std::abs(notes[i].note - notes[i - 1].note);
    if (interval == 0) {
      same_count++;
    } else if (interval <= 2) {
      step_count++;
    } else if (interval <= 4) {
      small_leap_count++;
    } else {
      large_leap_count++;
    }
  }

  int total = same_count + step_count + small_leap_count + large_leap_count;
  if (total == 0) return 0.5f;

  // Calculate ratios
  float same_ratio = static_cast<float>(same_count) / total;
  float step_ratio = static_cast<float>(step_count) / total;
  float small_leap_ratio = static_cast<float>(small_leap_count) / total;
  float large_leap_ratio = static_cast<float>(large_leap_count) / total;

  // Singability scoring (pop vocal theory):
  // - Step motion (1-2 semitones): most singable, highest score
  // - Same pitch: good for hooks/repetition, neutral-positive
  // - Small leaps (3-4 semitones): acceptable but less singable
  // - Large leaps (5+ semitones): difficult, penalized
  //
  // Target: Step 40-50%, Same 20-30%, SmallLeap 15-25%, LargeLeap 5-10%
  float score = 0.0f;

  // Step motion: strongest positive contribution (target 40-50%)
  // Score peaks at 45%, drops off at extremes
  float step_score = 1.0f - std::abs(step_ratio - 0.45f) * 2.0f;
  step_score = std::max(0.0f, step_score);
  score += step_score * 0.40f;  // 40% weight

  // Same pitch: moderate positive (target 20-30%)
  float same_score = 1.0f - std::abs(same_ratio - 0.25f) * 3.0f;
  same_score = std::max(0.0f, same_score);
  score += same_score * 0.20f;  // 20% weight

  // Small leaps: slight penalty if too many (target 15-25%)
  float small_leap_score = 1.0f - std::max(0.0f, small_leap_ratio - 0.25f) * 3.0f;
  small_leap_score = std::max(0.0f, small_leap_score);
  score += small_leap_score * 0.25f;  // 25% weight

  // Large leaps: penalty (target 5-10%)
  float large_leap_score = 1.0f - std::max(0.0f, large_leap_ratio - 0.10f) * 5.0f;
  large_leap_score = std::max(0.0f, large_leap_score);
  score += large_leap_score * 0.15f;  // 15% weight

  return std::clamp(score, 0.0f, 1.0f);
}

float MelodyEvaluator::calcChordToneRatio(const std::vector<NoteEvent>& notes,
                                          const IHarmonyContext& harmony) {
  if (notes.empty()) return 0.5f;

  int strong_beat_notes = 0;
  int chord_tone_hits = 0;

  for (const auto& note : notes) {
    // Strong beat: tick % (TICKS_PER_BEAT * 2) == 0 (beat 1 and 3)
    if (note.start_tick % (TICKS_PER_BEAT * 2) == 0) {
      ++strong_beat_notes;

      // Check if note is a chord tone
      std::vector<int> chord_tones = harmony.getChordTonesAt(note.start_tick);
      int pitch_class = note.note % 12;

      for (int tone : chord_tones) {
        if (pitch_class == tone) {
          ++chord_tone_hits;
          break;
        }
      }
    }
  }

  if (strong_beat_notes == 0) return 0.5f;

  return static_cast<float>(chord_tone_hits) / static_cast<float>(strong_beat_notes);
}

float MelodyEvaluator::calcContourShape(const std::vector<NoteEvent>& notes) {
  if (notes.size() < 4) return 0.5f;

  // Extract contour (direction changes)
  std::vector<int8_t> contour;
  contour.reserve(notes.size() - 1);
  for (size_t i = 1; i < notes.size(); ++i) {
    int diff = notes[i].note - notes[i - 1].note;
    if (diff > 0)
      contour.push_back(1);
    else if (diff < 0)
      contour.push_back(-1);
    else
      contour.push_back(0);
  }

  // Check for Arch shape (up then down)
  size_t midpoint = contour.size() / 2;
  int up_count_first = 0, down_count_second = 0;
  for (size_t i = 0; i < midpoint; ++i) {
    if (contour[i] > 0) ++up_count_first;
  }
  for (size_t i = midpoint; i < contour.size(); ++i) {
    if (contour[i] < 0) ++down_count_second;
  }
  float arch_score =
      static_cast<float>(up_count_first + down_count_second) / static_cast<float>(contour.size());

  // Check for Wave shape (multiple direction changes)
  int direction_changes = 0;
  for (size_t i = 1; i < contour.size(); ++i) {
    if (contour[i] != 0 && contour[i - 1] != 0 && contour[i] != contour[i - 1]) {
      ++direction_changes;
    }
  }
  // 2-3 direction changes is ideal for wave
  float wave_score = 0.0f;
  if (direction_changes >= 2 && direction_changes <= 4) {
    wave_score = 1.0f;
  } else if (direction_changes == 1 || direction_changes == 5) {
    wave_score = 0.7f;
  } else {
    wave_score = std::min(1.0f, static_cast<float>(direction_changes) / 4.0f);
  }

  // Check for Descending shape
  int descend_count = 0;
  for (const auto& dir : contour) {
    if (dir < 0) ++descend_count;
  }
  float descend_score = static_cast<float>(descend_count) / static_cast<float>(contour.size());

  // Return best matching contour
  return std::max({arch_score, wave_score, descend_score * 0.8f});
}

float MelodyEvaluator::calcSurpriseElement(const std::vector<NoteEvent>& notes) {
  if (notes.size() < 2) return 0.5f;

  int large_leaps = 0;
  for (size_t i = 1; i < notes.size(); ++i) {
    int interval = std::abs(notes[i].note - notes[i - 1].note);
    if (interval >= 5) {  // 5+ semitones is a large leap
      ++large_leaps;
    }
  }

  // 1-2 large leaps is ideal (memorable hook)
  if (large_leaps == 1 || large_leaps == 2) {
    return 1.0f;
  } else if (large_leaps == 0) {
    return 0.7f;  // Too predictable
  } else if (large_leaps == 3) {
    return 0.6f;  // Slightly too many
  } else {
    return std::max(0.3f, 0.5f - (large_leaps - 3) * 0.1f);  // Too jumpy
  }
}

float MelodyEvaluator::calcAaabPattern(const std::vector<NoteEvent>& notes) {
  if (notes.size() < 8) return 0.5f;

  // Divide into 4 quarters
  size_t quarter = notes.size() / 4;
  if (quarter < 2) return 0.5f;

  // Extract interval patterns for each quarter
  auto getPattern = [&](size_t start, size_t len) {
    std::vector<int8_t> pattern;
    for (size_t i = start; i < start + len && i + 1 < notes.size(); ++i) {
      pattern.push_back(static_cast<int8_t>(notes[i + 1].note - notes[i].note));
    }
    return pattern;
  };

  // Calculate similarity between two patterns
  auto similarity = [](const std::vector<int8_t>& a, const std::vector<int8_t>& b) {
    if (a.empty() || b.empty()) return 0.0f;
    size_t len = std::min(a.size(), b.size());
    int match = 0;
    for (size_t i = 0; i < len; ++i) {
      if (a[i] == b[i]) ++match;
    }
    return static_cast<float>(match) / static_cast<float>(len);
  };

  auto p1 = getPattern(0, quarter);
  auto p2 = getPattern(quarter, quarter);
  auto p3 = getPattern(quarter * 2, quarter);
  auto p4 = getPattern(quarter * 3, quarter);

  // AAA similarity (first three should be similar)
  float aaa_sim = (similarity(p1, p2) + similarity(p2, p3) + similarity(p1, p3)) / 3.0f;

  // B difference (fourth should be different)
  float b_diff = 1.0f - (similarity(p1, p4) + similarity(p2, p4) + similarity(p3, p4)) / 3.0f;

  // Weighted combination: AAA similarity is more important
  return aaa_sim * 0.7f + b_diff * 0.3f;
}

float MelodyEvaluator::calcRhythmIntervalCorrelation(const std::vector<NoteEvent>& notes) {
  // Rhythm-interval correlation: long notes should precede leaps, short notes should use steps.
  // Based on pop vocal theory: singers need time to prepare for large pitch jumps.
  // "Long note + leap" and "short note + step" are good correlations.
  // "Short note + leap" is difficult to sing and should be penalized.
  if (notes.size() < 2) return 0.5f;

  int good_correlations = 0;  // long note + leap OR short note + step
  int bad_correlations = 0;   // short note + leap
  int total_pairs = 0;

  for (size_t i = 1; i < notes.size(); ++i) {
    Tick prev_duration = notes[i - 1].duration;
    int interval = std::abs(notes[i].note - notes[i - 1].note);

    bool is_long = prev_duration >= TICKS_PER_BEAT;       // Quarter note or longer
    bool is_short = prev_duration < TICKS_PER_BEAT / 2;   // Less than 8th note
    bool is_leap = interval >= 5;                          // Perfect 4th or larger
    bool is_step = interval <= 2;                          // Major 2nd or smaller

    if ((is_long && is_leap) || (is_short && is_step)) {
      good_correlations++;  // Ideal combinations for singability
    } else if (is_short && is_leap) {
      bad_correlations++;   // Difficult to sing: no time to prepare for jump
    }
    total_pairs++;
  }

  if (total_pairs == 0) return 0.5f;

  // Score calculation: good ratio - bad ratio, centered at 0.5
  float good_ratio = static_cast<float>(good_correlations) / static_cast<float>(total_pairs);
  float bad_ratio = static_cast<float>(bad_correlations) / static_cast<float>(total_pairs);

  return std::clamp(0.5f + (good_ratio - bad_ratio) * 0.5f, 0.0f, 1.0f);
}

float MelodyEvaluator::calcCatchiness(const std::vector<NoteEvent>& notes) {
  // Catchiness evaluates hook memorability through four factors:
  // 1. 2-3 note pitch pattern repetition (30%)
  // 2. Rhythmic pattern consistency (25%)
  // 3. Simple interval usage (25%)
  // 4. Hook contour recognition (20%)

  if (notes.size() < 4) return 0.5f;

  float pattern_score = 0.0f;
  float rhythm_score = 0.0f;
  float simple_interval_score = 0.0f;
  float contour_score = 0.0f;

  // === 1. 2-3 note pitch pattern repetition (30%) ===
  // Count repeated 2-note and 3-note pitch patterns
  int pattern_matches = 0;
  int total_patterns = 0;

  // 2-note patterns (intervals)
  for (size_t i = 0; i + 1 < notes.size(); ++i) {
    int8_t interval1 = static_cast<int8_t>(notes[i + 1].note - notes[i].note);
    for (size_t j = i + 2; j + 1 < notes.size(); ++j) {
      int8_t interval2 = static_cast<int8_t>(notes[j + 1].note - notes[j].note);
      if (interval1 == interval2) {
        pattern_matches++;
      }
      total_patterns++;
    }
  }

  // 3-note patterns (two consecutive intervals)
  for (size_t i = 0; i + 2 < notes.size(); ++i) {
    int8_t int1a = static_cast<int8_t>(notes[i + 1].note - notes[i].note);
    int8_t int1b = static_cast<int8_t>(notes[i + 2].note - notes[i + 1].note);
    for (size_t j = i + 3; j + 2 < notes.size(); ++j) {
      int8_t int2a = static_cast<int8_t>(notes[j + 1].note - notes[j].note);
      int8_t int2b = static_cast<int8_t>(notes[j + 2].note - notes[j + 1].note);
      if (int1a == int2a && int1b == int2b) {
        pattern_matches += 2;  // 3-note matches count more
      }
      total_patterns++;
    }
  }

  if (total_patterns > 0) {
    pattern_score = std::min(1.0f, static_cast<float>(pattern_matches) /
                                       static_cast<float>(total_patterns) * 2.0f);
  }

  // === 1b. High repetition bonus: same interval appearing 4+ times ===
  // Count frequency of each interval
  std::unordered_map<int8_t, int> interval_freq;
  for (size_t i = 0; i + 1 < notes.size(); ++i) {
    int8_t interval = static_cast<int8_t>(notes[i + 1].note - notes[i].note);
    interval_freq[interval]++;
  }
  int max_interval_freq = 0;
  for (const auto& kv : interval_freq) {
    max_interval_freq = std::max(max_interval_freq, kv.second);
  }

  float high_rep_bonus = 0.0f;
  if (max_interval_freq >= 6) {
    high_rep_bonus = 0.25f;
  } else if (max_interval_freq >= 5) {
    high_rep_bonus = 0.15f;
  } else if (max_interval_freq >= 4) {
    high_rep_bonus = 0.08f;
  }
  pattern_score = std::min(1.0f, pattern_score + high_rep_bonus);

  // === 2. Rhythmic pattern consistency (25%) ===
  // Check for repeated duration patterns
  constexpr Tick kDurQuantize = TICKS_PER_BEAT / 4;  // 16th note quantization
  int rhythm_matches = 0;
  int rhythm_total = 0;

  for (size_t i = 0; i < notes.size(); ++i) {
    int dur_idx = static_cast<int>(notes[i].duration / kDurQuantize);
    for (size_t j = i + 1; j < notes.size(); ++j) {
      int other_dur = static_cast<int>(notes[j].duration / kDurQuantize);
      if (dur_idx == other_dur) {
        rhythm_matches++;
      }
      rhythm_total++;
    }
  }

  if (rhythm_total > 0) {
    rhythm_score = static_cast<float>(rhythm_matches) / static_cast<float>(rhythm_total);
  }

  // === 3. Simple interval usage (25%) ===
  // Count intervals that are easy to sing/remember (unison, 2nd, 3rd = 0-4 semitones)
  int simple_intervals = 0;
  int total_intervals = 0;

  for (size_t i = 1; i < notes.size(); ++i) {
    int interval = std::abs(notes[i].note - notes[i - 1].note);
    if (interval <= 4) {  // Unison through major 3rd
      simple_intervals++;
    }
    total_intervals++;
  }

  if (total_intervals > 0) {
    simple_interval_score = static_cast<float>(simple_intervals) / static_cast<float>(total_intervals);
  }

  // === 4. Hook contour recognition (20%) ===
  // Check for classic hook contours: Repeat, AscendDrop, PeakDrop
  // These are patterns that tend to be memorable in pop music

  // Check for pitch repetition (Repeat pattern)
  // Graduated bonus: longer consecutive same-pitch runs = higher catchiness (Ice Cream style)
  int consecutive_same = 0;
  int max_consecutive_same = 0;
  for (size_t i = 1; i < notes.size(); ++i) {
    if (notes[i].note == notes[i - 1].note) {
      consecutive_same++;
      max_consecutive_same = std::max(max_consecutive_same, consecutive_same);
    } else {
      consecutive_same = 0;
    }
  }
  // Graduated repeat bonus: 2音:0.2, 3音:0.4, 4音:0.6, 5+音:1.0
  float repeat_bonus = 0.0f;
  if (max_consecutive_same >= 5) {
    repeat_bonus = 1.0f;
  } else if (max_consecutive_same >= 4) {
    repeat_bonus = 0.6f;
  } else if (max_consecutive_same >= 3) {
    repeat_bonus = 0.4f;
  } else if (max_consecutive_same >= 2) {
    repeat_bonus = 0.2f;
  }

  // Check for AscendDrop (rising then falling)
  bool has_ascend_drop = false;
  if (notes.size() >= 4) {
    size_t mid = notes.size() / 2;
    int first_half_direction = 0;
    int second_half_direction = 0;

    for (size_t i = 1; i <= mid && i < notes.size(); ++i) {
      first_half_direction += (notes[i].note > notes[i - 1].note) ? 1 : -1;
    }
    for (size_t i = mid + 1; i < notes.size(); ++i) {
      second_half_direction += (notes[i].note > notes[i - 1].note) ? 1 : -1;
    }

    // AscendDrop: first half mostly ascending, second half mostly descending
    has_ascend_drop = (first_half_direction > 0 && second_half_direction < 0);
  }
  float ascend_drop_bonus = has_ascend_drop ? 0.5f : 0.0f;

  contour_score = repeat_bonus + ascend_drop_bonus;
  contour_score = std::min(1.0f, contour_score);

  // === Combine scores with weights ===
  float total = pattern_score * 0.30f + rhythm_score * 0.25f + simple_interval_score * 0.25f +
                contour_score * 0.20f;

  return std::clamp(total, 0.0f, 1.0f);
}

MelodyScore MelodyEvaluator::evaluate(const std::vector<NoteEvent>& notes,
                                      const IHarmonyContext& harmony) {
  MelodyScore score;
  score.singability = calcSingability(notes);
  score.chord_tone_ratio = calcChordToneRatio(notes, harmony);
  score.contour_shape = calcContourShape(notes);
  score.surprise_element = calcSurpriseElement(notes);
  score.aaab_pattern = calcAaabPattern(notes);
  score.rhythm_interval_correlation = calcRhythmIntervalCorrelation(notes);
  score.catchiness = calcCatchiness(notes);
  return score;
}

// ============================================================================
// VocalStylePreset → EvaluatorConfig Mapping
// ============================================================================
//
// Now delegated to VocalStyleProfile for unified management.
// See vocal_style_profile.h for the consolidated style definitions.
//

const EvaluatorConfig& MelodyEvaluator::getEvaluatorConfig(VocalStylePreset style) {
  return getVocalStyleProfile(style).evaluator;
}

// ============================================================================
// Penalty-based Evaluation
// ============================================================================

float MelodyEvaluator::calcHighRegisterPenalty(const std::vector<NoteEvent>& notes,
                                               uint8_t high_threshold) {
  // Default threshold (D5=74) matches vocal_helpers.h kHighRegisterThreshold.
  // Above passaggio (E4-B4), singers need more effort. D5 and above is demanding.
  if (notes.size() < 2) return 0.0f;

  float penalty = 0.0f;
  int consecutive_high = 0;
  Tick high_duration = 0;

  for (const auto& note : notes) {
    if (note.note >= high_threshold) {
      consecutive_high++;
      high_duration += note.duration;

      // Long high notes are harder to sing
      if (note.duration > TICKS_PER_BEAT * 2) {
        penalty += 0.1f;
      }
    } else {
      // Penalize long consecutive high passages
      if (consecutive_high > 3) {
        penalty += 0.05f * static_cast<float>(consecutive_high - 3);
      }
      consecutive_high = 0;
    }
  }

  // Overall high register density penalty
  if (!notes.empty()) {
    Tick total_duration =
        notes.back().start_tick + notes.back().duration - notes.front().start_tick;
    if (total_duration > 0 && high_duration > total_duration / 2) {
      penalty += 0.1f;
    }
  }

  return std::min(penalty, 0.5f);
}

float MelodyEvaluator::calcLeapAfterHighPenalty(const std::vector<NoteEvent>& notes) {
  if (notes.size() < 2) return 0.0f;

  constexpr uint8_t kHighThreshold = 74;  // D5
  constexpr int kLargeLeap = 7;           // 5th or more

  float penalty = 0.0f;

  for (size_t i = 1; i < notes.size(); ++i) {
    int interval = std::abs(notes[i].note - notes[i - 1].note);
    bool is_high = notes[i].note >= kHighThreshold;

    // Large leap landing on high note is difficult
    if (interval >= kLargeLeap && is_high) {
      penalty += 0.15f;
    }
  }

  return std::min(penalty, 0.4f);
}

float MelodyEvaluator::calcRapidDirectionChangePenalty(const std::vector<NoteEvent>& notes) {
  if (notes.size() < 4) return 0.0f;

  int rapid_changes = 0;
  int prev_direction = 0;  // -1 down, 0 same, 1 up

  for (size_t i = 1; i < notes.size(); ++i) {
    int diff = notes[i].note - notes[i - 1].note;
    int direction = (diff > 0) ? 1 : (diff < 0) ? -1 : 0;

    if (direction != 0 && prev_direction != 0 && direction != prev_direction) {
      // Check if notes are close together (rapid)
      Tick gap = notes[i].start_tick - notes[i - 1].start_tick;
      if (gap < TICKS_PER_BEAT / 2) {  // 8th note or faster
        rapid_changes++;
      }
    }
    if (direction != 0) {
      prev_direction = direction;
    }
  }

  // 2-3 changes = OK, 4+ = increasingly bad
  if (rapid_changes <= 3) return 0.0f;
  return std::min(0.05f * static_cast<float>(rapid_changes - 3), 0.3f);
}

float MelodyEvaluator::calcIsolatedNotePenalty(const std::vector<NoteEvent>& notes,
                                               int prev_section_last_pitch,
                                               int threshold) {
  if (notes.size() < 2) return 0.0f;

  int isolated_count = 0;

  // Check first note against previous section's last note
  if (prev_section_last_pitch >= 0 && notes.size() >= 2) {
    int interval_before = std::abs(static_cast<int>(notes[0].note) - prev_section_last_pitch);
    int interval_after = std::abs(static_cast<int>(notes[1].note) - static_cast<int>(notes[0].note));
    if (interval_before >= threshold && interval_after >= threshold) {
      isolated_count++;
    }
  }

  // Check internal notes
  for (size_t i = 1; i + 1 < notes.size(); ++i) {
    int interval_before = std::abs(static_cast<int>(notes[i].note) - static_cast<int>(notes[i - 1].note));
    int interval_after = std::abs(static_cast<int>(notes[i + 1].note) - static_cast<int>(notes[i].note));
    if (interval_before >= threshold && interval_after >= threshold) {
      isolated_count++;
    }
  }

  // Each isolated note contributes ~0.1 penalty, max 0.3
  return std::min(0.1f * static_cast<float>(isolated_count), 0.3f);
}

float MelodyEvaluator::calcMonotonyPenalty(const std::vector<NoteEvent>& notes) {
  if (notes.size() < 4) return 0.0f;

  // Count unique pitches
  std::vector<uint8_t> pitches;
  pitches.reserve(notes.size());
  for (const auto& note : notes) {
    pitches.push_back(note.note);
  }
  std::sort(pitches.begin(), pitches.end());
  auto last = std::unique(pitches.begin(), pitches.end());
  size_t unique_count = static_cast<size_t>(std::distance(pitches.begin(), last));

  // Very few unique pitches = monotonous
  float ratio = static_cast<float>(unique_count) / static_cast<float>(notes.size());
  if (ratio < 0.3f) {
    return 0.2f;  // Severe monotony
  } else if (ratio < 0.5f) {
    return 0.1f;  // Moderate monotony
  }
  return 0.0f;
}

float MelodyEvaluator::calcClearPeakBonus(const std::vector<NoteEvent>& notes) {
  if (notes.size() < 4) return 0.0f;

  // Find highest pitch
  uint8_t max_pitch = 0;
  size_t max_idx = 0;
  for (size_t i = 0; i < notes.size(); ++i) {
    if (notes[i].note > max_pitch) {
      max_pitch = notes[i].note;
      max_idx = i;
    }
  }

  // Count how many times max pitch appears
  int peak_count = 0;
  for (const auto& note : notes) {
    if (note.note == max_pitch) peak_count++;
  }

  // Single clear peak in middle of phrase = good
  float position = static_cast<float>(max_idx) / static_cast<float>(notes.size());
  bool in_middle = position > 0.25f && position < 0.85f;

  if (peak_count == 1 && in_middle) {
    return 0.15f;
  } else if (peak_count <= 2 && in_middle) {
    return 0.08f;
  }
  return 0.0f;
}

float MelodyEvaluator::calcMotifRepeatBonus(const std::vector<NoteEvent>& notes) {
  // Reuse existing AAAB calculation
  float aaab_score = calcAaabPattern(notes);
  // Convert to bonus (0-0.2 range)
  return aaab_score * 0.2f;
}

float MelodyEvaluator::calcPhraseCohesionBonus(const std::vector<NoteEvent>& notes) {
  // Short phrases can't be evaluated for cohesion - return no bonus
  if (notes.size() < 4) return 0.0f;

  float stepwise_score = 0.0f;
  float rhythm_score = 0.0f;
  float cell_score = 0.0f;

  // === 1. Stepwise motion: consecutive run length ===
  // Instead of simple ratio, measure the longest consecutive stepwise run.
  // This distinguishes "connected melody" from "scattered stepwise fragments".
  int max_run = 0;
  int current_run = 0;
  for (size_t i = 1; i < notes.size(); ++i) {
    int interval = std::abs(notes[i].note - notes[i - 1].note);
    if (interval <= 2) {  // Unison, minor 2nd, or major 2nd
      current_run++;
      max_run = std::max(max_run, current_run);
    } else {
      current_run = 0;
    }
  }
  // Score based on max run relative to phrase length
  // Ideal: at least half the notes are in one connected run
  stepwise_score =
      std::min(1.0f, static_cast<float>(max_run) / static_cast<float>(notes.size() / 2));

  // === 2. Rhythm pattern consistency ===
  // Check for (duration, beat_position) patterns, not just duration frequency.
  // This prevents short scattered notes from scoring high.
  constexpr Tick kQuantize = TICKS_PER_BEAT / 2;  // 8th note
  constexpr Tick kBeatQuantize = TICKS_PER_BEAT;

  // Create rhythm signature: (quantized_duration, beat_offset)
  std::vector<std::pair<int, int>> rhythm_patterns;
  rhythm_patterns.reserve(notes.size());
  for (const auto& note : notes) {
    int dur_idx = static_cast<int>(std::min(note.duration / kQuantize, static_cast<Tick>(7)));
    int beat_offset = static_cast<int>((note.start_tick % kBeatQuantize) /
                                       (kBeatQuantize / 4));  // 0-3 within beat
    rhythm_patterns.push_back({dur_idx, beat_offset});
  }

  // Count most frequent pattern
  int max_pattern_count = 0;
  for (size_t i = 0; i < rhythm_patterns.size(); ++i) {
    int count = 0;
    for (size_t j = 0; j < rhythm_patterns.size(); ++j) {
      if (rhythm_patterns[i] == rhythm_patterns[j]) count++;
    }
    max_pattern_count = std::max(max_pattern_count, count);
  }
  rhythm_score = static_cast<float>(max_pattern_count) / static_cast<float>(notes.size());

  // === 3. Cell repetition: 3-gram (2 intervals + 2 durations) ===
  // A "cell" is: (interval1, interval2, dur_ratio1, dur_ratio2)
  // This captures melodic+rhythmic motifs, not just pitch direction.
  struct Cell {
    int8_t int1, int2;  // Two consecutive intervals
    int8_t dur1, dur2;  // Duration ratios (quantized)

    bool operator==(const Cell& other) const {
      return int1 == other.int1 && int2 == other.int2 && dur1 == other.dur1 && dur2 == other.dur2;
    }
  };

  std::vector<Cell> cells;
  cells.reserve(notes.size() > 2 ? notes.size() - 2 : 0);

  for (size_t i = 0; i + 2 < notes.size(); ++i) {
    Cell c;
    c.int1 = static_cast<int8_t>(std::clamp(notes[i + 1].note - notes[i].note, -12, 12));
    c.int2 = static_cast<int8_t>(std::clamp(notes[i + 2].note - notes[i + 1].note, -12, 12));
    c.dur1 = static_cast<int8_t>(std::min(notes[i].duration / kQuantize, static_cast<Tick>(7)));
    c.dur2 = static_cast<int8_t>(std::min(notes[i + 1].duration / kQuantize, static_cast<Tick>(7)));
    cells.push_back(c);
  }

  // Find most frequent 3-gram cell
  int max_cell_count = 0;
  for (size_t i = 0; i < cells.size(); ++i) {
    int count = 0;
    for (size_t j = 0; j < cells.size(); ++j) {
      if (cells[i] == cells[j]) count++;
    }
    max_cell_count = std::max(max_cell_count, count);
  }
  if (!cells.empty()) {
    // Need at least 2 occurrences for it to be a "repetition"
    cell_score = (max_cell_count >= 2)
                     ? static_cast<float>(max_cell_count) / static_cast<float>(cells.size())
                     : 0.0f;
  }

  // Weighted combination: stepwise run is most important for cohesion
  return stepwise_score * 0.5f + rhythm_score * 0.25f + cell_score * 0.25f;
}

float MelodyEvaluator::calcGapRatio(const std::vector<NoteEvent>& notes, Tick phrase_duration) {
  if (notes.empty() || phrase_duration == 0) return 1.0f;  // All gap = worst
  if (notes.size() == 1) {
    // Single note: gap = phrase - note duration
    Tick note_coverage = notes[0].duration;
    return 1.0f - static_cast<float>(note_coverage) / static_cast<float>(phrase_duration);
  }

  // Calculate total sounding time
  Tick total_sounding = 0;
  for (const auto& note : notes) {
    total_sounding += note.duration;
  }

  // Calculate gaps between consecutive notes
  Tick total_gaps = 0;
  for (size_t i = 1; i < notes.size(); ++i) {
    Tick gap = notes[i].start_tick - (notes[i - 1].start_tick + notes[i - 1].duration);
    if (gap > 0) {
      total_gaps += gap;
    }
  }

  // Also account for gap at the start and end of phrase
  // (assuming notes are within phrase_start to phrase_start + phrase_duration)
  // For simplicity, we use the gap-to-phrase ratio
  float gap_ratio = static_cast<float>(total_gaps) / static_cast<float>(phrase_duration);

  // Also penalize low note density (notes not filling the phrase)
  float coverage = static_cast<float>(total_sounding) / static_cast<float>(phrase_duration);
  float coverage_penalty = std::max(0.0f, 1.0f - coverage);

  // Combine: direct gaps + low coverage
  return std::min(1.0f, gap_ratio * 0.6f + coverage_penalty * 0.4f);
}

float MelodyEvaluator::calcBreathlessPenalty(const std::vector<NoteEvent>& notes) {
  if (notes.size() < 4) return 0.0f;

  // Count consecutive short notes without breathing room
  constexpr Tick kShortNoteThreshold = TICKS_PER_BEAT / 4;     // 16th note or shorter
  constexpr Tick kBreathingGapThreshold = TICKS_PER_BEAT / 2;  // 8th note gap minimum

  int consecutive_short = 0;
  int max_consecutive_short = 0;

  for (size_t i = 0; i < notes.size(); ++i) {
    bool is_short = notes[i].duration <= kShortNoteThreshold;

    if (is_short) {
      consecutive_short++;

      // Check if there's a breathing gap after this note
      if (i + 1 < notes.size()) {
        Tick gap = notes[i + 1].start_tick - (notes[i].start_tick + notes[i].duration);
        if (gap >= kBreathingGapThreshold) {
          // Breathing opportunity found, reset
          max_consecutive_short = std::max(max_consecutive_short, consecutive_short);
          consecutive_short = 0;
        }
      }
    } else {
      max_consecutive_short = std::max(max_consecutive_short, consecutive_short);
      consecutive_short = 0;
    }
  }
  max_consecutive_short = std::max(max_consecutive_short, consecutive_short);

  // Penalty based on max consecutive short notes
  // 4-5 consecutive 16th notes = OK (one beat)
  // 6-8 = getting hard
  // 9+ = very breathless
  float penalty = 0.0f;
  if (max_consecutive_short > 8) {
    penalty = 0.25f;
  } else if (max_consecutive_short > 5) {
    penalty = 0.1f + 0.05f * (max_consecutive_short - 5);
  }

  return std::min(penalty, 0.3f);
}

float MelodyEvaluator::getCohesionThreshold(VocalStylePreset style) {
  switch (style) {
    // High cohesion required - traditional melodic styles
    // These styles expect smooth, connected melodic lines
    case VocalStylePreset::Ballad:
    case VocalStylePreset::CityPop:
      return 0.50f;

    // Low cohesion acceptable - rhythmic/mechanical styles
    // These styles tolerate more disconnected, angular melodies
    case VocalStylePreset::Vocaloid:
    case VocalStylePreset::UltraVocaloid:
    case VocalStylePreset::Rock:
    case VocalStylePreset::PowerfulShout:
      return 0.35f;

    // Standard cohesion
    default:
      return 0.45f;
  }
}

float MelodyEvaluator::getGapThreshold(VocalStylePreset style) {
  switch (style) {
    // High density styles - less silence allowed
    case VocalStylePreset::Idol:
    case VocalStylePreset::BrightKira:
    case VocalStylePreset::CuteAffected:
    case VocalStylePreset::Rock:
    case VocalStylePreset::PowerfulShout:
      return 0.30f;

    // Vocaloid styles - machine-like, very high density
    case VocalStylePreset::Vocaloid:
    case VocalStylePreset::UltraVocaloid:
      return 0.25f;

    // Ballad - more silence is natural
    case VocalStylePreset::Ballad:
      return 0.50f;

    // City Pop - jazzy, some space is good
    case VocalStylePreset::CityPop:
      return 0.45f;

    // Anime - dramatic, varied density
    case VocalStylePreset::Anime:
      return 0.35f;

    // Standard and others
    default:
      return 0.40f;
  }
}

float MelodyEvaluator::evaluateForCulling(const std::vector<NoteEvent>& notes,
                                          const IHarmonyContext& harmony, Tick phrase_duration,
                                          VocalStylePreset style,
                                          int prev_section_last_pitch) {
  if (notes.empty()) return 0.0f;  // Empty = reject

  float score = 1.0f;

  // === Singing Difficulty Penalties ===
  score -= calcHighRegisterPenalty(notes);
  score -= calcLeapAfterHighPenalty(notes);
  score -= calcRapidDirectionChangePenalty(notes);

  // === Isolated Note Penalty ===
  // Notes with large intervals both before AND after feel disconnected
  score -= calcIsolatedNotePenalty(notes, prev_section_last_pitch);

  // === Breathless Penalty (style-dependent) ===
  // Vocaloid styles tolerate more consecutive short notes
  bool is_vocaloid_style =
      (style == VocalStylePreset::Vocaloid || style == VocalStylePreset::UltraVocaloid);
  if (!is_vocaloid_style) {
    score -= calcBreathlessPenalty(notes);
  }

  // === Music Theory Penalties ===
  // Non-chord tones on strong beats (reuse existing)
  float chord_tone_ratio = calcChordToneRatio(notes, harmony);
  if (chord_tone_ratio < 0.5f) {
    score -= (0.5f - chord_tone_ratio) * 0.4f;  // Up to 0.2 penalty
  }

  // === Boring Melody Penalties ===
  score -= calcMonotonyPenalty(notes);

  // === Phrase Cohesion Gate (penalty for low cohesion) ===
  // Convert cohesion from bonus to penalty: if below threshold, penalize.
  // This is the primary gate for "scattered note" problems.
  // Threshold varies by style: Ballad/CityPop need high cohesion, Vocaloid/Rock tolerate less.
  float cohesion = calcPhraseCohesionBonus(notes);
  float cohesion_threshold = getCohesionThreshold(style);
  if (cohesion < cohesion_threshold) {
    // Penalize lack of cohesion: max ~0.16-0.18 penalty when cohesion = 0
    score -= (cohesion_threshold - cohesion) * 0.35f;
  }

  // === Gap Ratio Penalty (style-dependent threshold) ===
  // High gap ratio = notes floating in isolation = bad melody
  float gap_ratio = calcGapRatio(notes, phrase_duration);
  float gap_threshold = getGapThreshold(style);
  if (gap_ratio > gap_threshold) {
    // Strong penalty for scattered melodies: max ~0.3 penalty when gap = 1.0
    score -= (gap_ratio - gap_threshold) * 0.5f;
  }

  // === Bonuses ===
  score += calcClearPeakBonus(notes);
  score += calcMotifRepeatBonus(notes);

  // Note: calcContourShape removed from here (duplicate with style_total).
  // Contour evaluation should be done via MelodyScore::total() with EvaluatorConfig.

  return std::clamp(score, 0.0f, 1.0f);
}

}  // namespace midisketch
