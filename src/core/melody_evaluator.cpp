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
  stepwise_score = std::min(1.0f, static_cast<float>(max_run) /
                                      static_cast<float>(notes.size() / 2));

  // === 2. Rhythm pattern consistency ===
  // Check for (duration, beat_position) patterns, not just duration frequency.
  // This prevents short scattered notes from scoring high.
  constexpr Tick kQuantize = TICKS_PER_BEAT / 2;  // 8th note
  constexpr Tick kBeatQuantize = TICKS_PER_BEAT;

  // Create rhythm signature: (quantized_duration, beat_offset)
  std::vector<std::pair<int, int>> rhythm_patterns;
  rhythm_patterns.reserve(notes.size());
  for (const auto& note : notes) {
    int dur_idx = static_cast<int>(std::min(note.duration / kQuantize,
                                            static_cast<Tick>(7)));
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
  rhythm_score = static_cast<float>(max_pattern_count) /
                 static_cast<float>(notes.size());

  // === 3. Cell repetition: 3-gram (2 intervals + 2 durations) ===
  // A "cell" is: (interval1, interval2, dur_ratio1, dur_ratio2)
  // This captures melodic+rhythmic motifs, not just pitch direction.
  struct Cell {
    int8_t int1, int2;   // Two consecutive intervals
    int8_t dur1, dur2;   // Duration ratios (quantized)

    bool operator==(const Cell& other) const {
      return int1 == other.int1 && int2 == other.int2 &&
             dur1 == other.dur1 && dur2 == other.dur2;
    }
  };

  std::vector<Cell> cells;
  cells.reserve(notes.size() > 2 ? notes.size() - 2 : 0);

  for (size_t i = 0; i + 2 < notes.size(); ++i) {
    Cell c;
    c.int1 = static_cast<int8_t>(std::clamp(
        notes[i + 1].note - notes[i].note, -12, 12));
    c.int2 = static_cast<int8_t>(std::clamp(
        notes[i + 2].note - notes[i + 1].note, -12, 12));
    c.dur1 = static_cast<int8_t>(std::min(
        notes[i].duration / kQuantize, static_cast<Tick>(7)));
    c.dur2 = static_cast<int8_t>(std::min(
        notes[i + 1].duration / kQuantize, static_cast<Tick>(7)));
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
                     ? static_cast<float>(max_cell_count) /
                           static_cast<float>(cells.size())
                     : 0.0f;
  }

  // Weighted combination: stepwise run is most important for cohesion
  return stepwise_score * 0.5f + rhythm_score * 0.25f + cell_score * 0.25f;
}

float MelodyEvaluator::calcGapRatio(const std::vector<NoteEvent>& notes,
                                     Tick phrase_duration) {
  if (notes.empty() || phrase_duration == 0) return 1.0f;  // All gap = worst
  if (notes.size() == 1) {
    // Single note: gap = phrase - note duration
    Tick note_coverage = notes[0].duration;
    return 1.0f - static_cast<float>(note_coverage) /
                      static_cast<float>(phrase_duration);
  }

  // Calculate total sounding time
  Tick total_sounding = 0;
  for (const auto& note : notes) {
    total_sounding += note.duration;
  }

  // Calculate gaps between consecutive notes
  Tick total_gaps = 0;
  for (size_t i = 1; i < notes.size(); ++i) {
    Tick gap = notes[i].start_tick -
               (notes[i - 1].start_tick + notes[i - 1].duration);
    if (gap > 0) {
      total_gaps += gap;
    }
  }

  // Also account for gap at the start and end of phrase
  // (assuming notes are within phrase_start to phrase_start + phrase_duration)
  // For simplicity, we use the gap-to-phrase ratio
  float gap_ratio = static_cast<float>(total_gaps) /
                    static_cast<float>(phrase_duration);

  // Also penalize low note density (notes not filling the phrase)
  float coverage = static_cast<float>(total_sounding) /
                   static_cast<float>(phrase_duration);
  float coverage_penalty = std::max(0.0f, 1.0f - coverage);

  // Combine: direct gaps + low coverage
  return std::min(1.0f, gap_ratio * 0.6f + coverage_penalty * 0.4f);
}

float MelodyEvaluator::evaluateForCulling(const std::vector<NoteEvent>& notes,
                                           const IHarmonyContext& harmony,
                                           Tick phrase_duration) {
  if (notes.empty()) return 0.0f;  // Empty = reject

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

  // === Phrase Cohesion Gate (penalty for low cohesion) ===
  // Convert cohesion from bonus to penalty: if below threshold, penalize.
  // This is the primary gate for "scattered note" problems.
  float cohesion = calcPhraseCohesionBonus(notes);
  constexpr float kCohesionThreshold = 0.45f;
  if (cohesion < kCohesionThreshold) {
    // Penalize lack of cohesion: max ~0.16 penalty when cohesion = 0
    score -= (kCohesionThreshold - cohesion) * 0.35f;
  }

  // === Gap Ratio Penalty (primary fix for scattered notes) ===
  // High gap ratio = notes floating in isolation = bad melody
  float gap_ratio = calcGapRatio(notes, phrase_duration);
  constexpr float kGapThreshold = 0.4f;  // Allow up to 40% silence
  if (gap_ratio > kGapThreshold) {
    // Strong penalty for scattered melodies: max ~0.3 penalty when gap = 1.0
    score -= (gap_ratio - kGapThreshold) * 0.5f;
  }

  // === Bonuses ===
  score += calcClearPeakBonus(notes);
  score += calcMotifRepeatBonus(notes);

  // Note: calcContourShape removed from here (duplicate with style_total).
  // Contour evaluation should be done via MelodyScore::total() with EvaluatorConfig.

  return std::clamp(score, 0.0f, 1.0f);
}

}  // namespace midisketch
