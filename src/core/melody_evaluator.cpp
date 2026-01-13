/**
 * @file melody_evaluator.cpp
 * @brief Implementation of melody quality scoring.
 */

#include "core/melody_evaluator.h"
#include "core/i_harmony_context.h"
#include "core/pitch_utils.h"
#include "core/types.h"
#include "core/vocal_style_profile.h"
#include <algorithm>
#include <cmath>

namespace midisketch {

float MelodyEvaluator::calcSingability(const std::vector<NoteEvent>& notes) {
  if (notes.size() < 2) return 0.5f;

  float total_interval = 0.0f;
  int count = 0;

  for (size_t i = 1; i < notes.size(); ++i) {
    int interval = std::abs(notes[i].note - notes[i - 1].note);
    total_interval += static_cast<float>(interval);
    ++count;
  }

  if (count == 0) return 0.5f;

  float avg_interval = total_interval / static_cast<float>(count);

  // Ideal: 2-4 semitones average -> 1.0
  // 0-1 semitones: slightly low (0.7-0.9)
  // 5-6 semitones: acceptable (0.7-0.9)
  // 7+ semitones: too jumpy (0.3-0.6)
  if (avg_interval >= 2.0f && avg_interval <= 4.0f) {
    return 1.0f;
  } else if (avg_interval < 2.0f) {
    // Too static, but not terrible
    return 0.7f + (avg_interval / 2.0f) * 0.2f;
  } else if (avg_interval <= 6.0f) {
    // Acceptable range
    return 1.0f - (avg_interval - 4.0f) * 0.15f;
  } else {
    // Too jumpy
    return std::max(0.3f, 0.7f - (avg_interval - 6.0f) * 0.1f);
  }
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

  return static_cast<float>(chord_tone_hits) /
         static_cast<float>(strong_beat_notes);
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
      static_cast<float>(up_count_first + down_count_second) /
      static_cast<float>(contour.size());

  // Check for Wave shape (multiple direction changes)
  int direction_changes = 0;
  for (size_t i = 1; i < contour.size(); ++i) {
    if (contour[i] != 0 && contour[i - 1] != 0 &&
        contour[i] != contour[i - 1]) {
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
  float descend_score =
      static_cast<float>(descend_count) / static_cast<float>(contour.size());

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
      pattern.push_back(
          static_cast<int8_t>(notes[i + 1].note - notes[i].note));
    }
    return pattern;
  };

  // Calculate similarity between two patterns
  auto similarity = [](const std::vector<int8_t>& a,
                       const std::vector<int8_t>& b) {
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

MelodyScore MelodyEvaluator::evaluate(const std::vector<NoteEvent>& notes,
                                       const IHarmonyContext& harmony) {
  MelodyScore score;
  score.singability = calcSingability(notes);
  score.chord_tone_ratio = calcChordToneRatio(notes, harmony);
  score.contour_shape = calcContourShape(notes);
  score.surprise_element = calcSurpriseElement(notes);
  score.aaab_pattern = calcAaabPattern(notes);
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
    Tick total_duration = notes.back().start_tick + notes.back().duration -
                          notes.front().start_tick;
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
  if (notes.size() < 4) return 0.5f;

  float stepwise_score = 0.0f;
  float rhythm_score = 0.0f;
  float cell_score = 0.0f;

  // === 1. Stepwise motion ratio ===
  // Count intervals that are stepwise (0-2 semitones)
  int stepwise_count = 0;
  for (size_t i = 1; i < notes.size(); ++i) {
    int interval = std::abs(notes[i].note - notes[i - 1].note);
    if (interval <= 2) {  // Unison, minor 2nd, or major 2nd
      stepwise_count++;
    }
  }
  stepwise_score = static_cast<float>(stepwise_count) /
                   static_cast<float>(notes.size() - 1);

  // === 2. Rhythm consistency ===
  // Check if note durations cluster into similar values
  std::vector<Tick> durations;
  durations.reserve(notes.size());
  for (const auto& note : notes) {
    durations.push_back(note.duration);
  }

  // Find most common duration (quantized to 8th note)
  constexpr Tick kQuantize = TICKS_PER_BEAT / 2;  // 8th note
  std::vector<int> duration_counts(8, 0);  // 0-7 = 8th note multiples
  for (Tick dur : durations) {
    int idx = static_cast<int>(std::min(dur / kQuantize, static_cast<Tick>(7)));
    duration_counts[static_cast<size_t>(idx)]++;
  }
  int max_count = *std::max_element(duration_counts.begin(), duration_counts.end());
  rhythm_score = static_cast<float>(max_count) / static_cast<float>(notes.size());

  // === 3. Short cell repetition ===
  // Look for 2-3 note patterns that repeat
  auto getIntervalCell = [&](size_t start, size_t len) {
    std::vector<int8_t> cell;
    for (size_t i = start; i < start + len && i + 1 < notes.size(); ++i) {
      cell.push_back(static_cast<int8_t>(notes[i + 1].note - notes[i].note));
    }
    return cell;
  };

  int cell_matches = 0;
  int cell_checks = 0;
  // Check for 2-note cell repetition
  for (size_t i = 0; i + 3 < notes.size(); i += 2) {
    auto cell1 = getIntervalCell(i, 2);
    auto cell2 = getIntervalCell(i + 2, 2);
    if (cell1.size() >= 1 && cell2.size() >= 1 && cell1[0] == cell2[0]) {
      cell_matches++;
    }
    cell_checks++;
  }
  if (cell_checks > 0) {
    cell_score = static_cast<float>(cell_matches) / static_cast<float>(cell_checks);
  }

  // Weighted combination: stepwise is most important
  return stepwise_score * 0.5f + rhythm_score * 0.25f + cell_score * 0.25f;
}

float MelodyEvaluator::evaluateForCulling(const std::vector<NoteEvent>& notes,
                                           const IHarmonyContext& harmony) {
  if (notes.empty()) return 0.5f;

  float score = 1.0f;

  // === Singing Difficulty Penalties ===
  score -= calcHighRegisterPenalty(notes);
  score -= calcLeapAfterHighPenalty(notes);
  score -= calcRapidDirectionChangePenalty(notes);

  // === Music Theory Penalties ===
  // Non-chord tones on strong beats (reuse existing)
  float chord_tone_ratio = calcChordToneRatio(notes, harmony);
  if (chord_tone_ratio < 0.5f) {
    score -= (0.5f - chord_tone_ratio) * 0.4f;  // Up to 0.2 penalty
  }

  // === Boring Melody Penalties ===
  score -= calcMonotonyPenalty(notes);

  // === Bonuses ===
  score += calcClearPeakBonus(notes);
  score += calcMotifRepeatBonus(notes);

  // === Shape Selection Bonuses ===
  // These actively select "good shapes" rather than just penalizing bad ones.
  // Without these, melodies that avoid penalties but lack coherence survive.
  score += calcPhraseCohesionBonus(notes) * 0.1f;  // Phrase unity
  score += calcContourShape(notes) * 0.1f;         // Directional clarity

  return std::clamp(score, 0.0f, 1.0f);
}

}  // namespace midisketch
