#include "core/melody_evaluator.h"
#include "core/harmony_context.h"
#include "core/types.h"
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
                                          const HarmonyContext& harmony) {
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
                                       const HarmonyContext& harmony) {
  MelodyScore score;
  score.singability = calcSingability(notes);
  score.chord_tone_ratio = calcChordToneRatio(notes, harmony);
  score.contour_shape = calcContourShape(notes);
  score.surprise_element = calcSurpriseElement(notes);
  score.aaab_pattern = calcAaabPattern(notes);
  return score;
}

const EvaluatorConfig& MelodyEvaluator::getEvaluatorConfig(VocalStylePreset style) {
  switch (style) {
    case VocalStylePreset::Idol:
    case VocalStylePreset::BrightKira:
    case VocalStylePreset::CuteAffected:
      return kIdolConfig;

    case VocalStylePreset::Vocaloid:
    case VocalStylePreset::UltraVocaloid:
    case VocalStylePreset::CoolSynth:
      return kVocaloidConfig;

    case VocalStylePreset::Ballad:
      return kBalladConfig;

    case VocalStylePreset::Anime:
      return kYoasobiConfig;

    case VocalStylePreset::Standard:
    case VocalStylePreset::Rock:
    case VocalStylePreset::PowerfulShout:
    case VocalStylePreset::CityPop:
    case VocalStylePreset::Auto:
    default:
      return kStandardConfig;
  }
}

}  // namespace midisketch
