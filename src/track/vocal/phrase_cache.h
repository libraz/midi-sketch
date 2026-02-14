/**
 * @file phrase_cache.h
 * @brief Phrase caching structures for vocal melody generation.
 *
 * Provides structures for caching and reusing vocal phrases across sections.
 * Enables "varied repetition" where Chorus 1 and 2 share melodic content.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_PHRASE_CACHE_H
#define MIDISKETCH_TRACK_VOCAL_PHRASE_CACHE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "core/section_types.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "track/generators/motif.h"
#include "track/vocal/phrase_plan.h"

namespace midisketch {

/**
 * @brief Cached phrase for section repetition.
 *
 * Chorus 1 & 2 share melody with subtle variations for musical interest.
 */
struct CachedPhrase {
  std::vector<NoteEvent> notes;  ///< Notes with timing relative to section start
  uint8_t bars;                  ///< Section length when cached
  uint8_t vocal_low;             ///< Vocal range when cached
  uint8_t vocal_high;
  int reuse_count = 0;  ///< How many times this phrase has been reused
};

/**
 * @brief Extended cache key for phrase lookup.
 *
 * Phrases are cached not just by section type, but also by length and
 * starting chord. This ensures that a 4-bar chorus starting on I chord
 * is cached separately from an 8-bar chorus starting on IV chord.
 */
struct PhraseCacheKey {
  SectionType section_type;  ///< Section type (Verse, Chorus, etc.)
  uint8_t bars;              ///< Section length in bars
  int8_t chord_degree;       ///< Starting chord degree (affects melodic choices)

  bool operator==(const PhraseCacheKey& other) const {
    return section_type == other.section_type && bars == other.bars &&
           chord_degree == other.chord_degree;
  }
};

/**
 * @brief Hash function for PhraseCacheKey enabling use in unordered_map.
 */
struct PhraseCacheKeyHash {
  size_t operator()(const PhraseCacheKey& key) const {
    return std::hash<uint8_t>()(static_cast<uint8_t>(key.section_type)) ^
           (std::hash<uint8_t>()(key.bars) << 4) ^ (std::hash<int8_t>()(key.chord_degree) << 8);
  }
};

// ============================================================================
// Rhythm Lock for Orangestar-style generation
// ============================================================================

/**
 * @brief Cached rhythm pattern for Orangestar-style "coordinate axis" locking.
 *
 * Stores onset positions (in beats) for reuse across sections.
 * The rhythm pattern becomes the fixed "coordinate axis" while pitch can vary.
 * This creates the addictive repeating riff characteristic of Orangestar style.
 */
struct CachedRhythmPattern {
  std::vector<float> onset_beats;  ///< Onset positions in beats (0.0, 0.25, 0.5, ...)
  std::vector<float> durations;    ///< Duration of each note in beats
  uint8_t phrase_beats = 0;        ///< Original phrase length in beats
  bool is_locked = false;          ///< True after first phrase is generated

  /**
   * @brief Scale rhythm pattern to a different phrase length.
   * @param target_beats Target phrase length in beats
   * @return Scaled onset positions
   *
   * Used when applying a 2-bar pattern to a 4-bar section, etc.
   */
  std::vector<float> getScaledOnsets(uint8_t target_beats) const {
    if (phrase_beats == 0 || phrase_beats == target_beats || onset_beats.empty()) {
      return onset_beats;
    }
    float scale = static_cast<float>(target_beats) / phrase_beats;
    std::vector<float> scaled;
    scaled.reserve(onset_beats.size());
    for (float onset : onset_beats) {
      scaled.push_back(onset * scale);
    }
    return scaled;
  }

  /**
   * @brief Scale durations to a different phrase length.
   * @param target_beats Target phrase length in beats
   * @return Scaled durations
   */
  std::vector<float> getScaledDurations(uint8_t target_beats) const {
    if (phrase_beats == 0 || phrase_beats == target_beats || durations.empty()) {
      return durations;
    }
    float scale = static_cast<float>(target_beats) / phrase_beats;
    std::vector<float> scaled;
    scaled.reserve(durations.size());
    for (float dur : durations) {
      scaled.push_back(dur * scale);
    }
    return scaled;
  }

  /**
   * @brief Check if the pattern is valid and can be used.
   * @return True if pattern has onsets and is locked
   */
  bool isValid() const { return is_locked && !onset_beats.empty() && phrase_beats > 0; }

  /**
   * @brief Clear the cached pattern.
   */
  void clear() {
    onset_beats.clear();
    durations.clear();
    phrase_beats = 0;
    is_locked = false;
  }
};

/**
 * @brief Calculate pattern density (notes per bar) for RhythmLock validation.
 * @param pattern Rhythm pattern to evaluate
 * @return Density as notes per bar (4 beats)
 *
 * Used to validate that a rhythm pattern has sufficient density before locking.
 * Minimum density of 3.0 notes/bar ensures RhythmLock doesn't propagate sparse patterns.
 */
inline float calculatePatternDensity(const CachedRhythmPattern& pattern) {
  if (pattern.phrase_beats == 0 || pattern.onset_beats.empty()) {
    return 0.0f;
  }
  // Convert to notes per bar (4 beats)
  float bars = static_cast<float>(pattern.phrase_beats) / 4.0f;
  if (bars <= 0.0f) return 0.0f;
  return static_cast<float>(pattern.onset_beats.size()) / bars;
}

/**
 * @brief Extract rhythm pattern from generated notes.
 * @param notes Source notes (absolute timing)
 * @param section_start Section start tick
 * @param phrase_beats Phrase length in beats
 * @return Extracted rhythm pattern
 */
inline CachedRhythmPattern extractRhythmPattern(const std::vector<NoteEvent>& notes,
                                                Tick section_start, uint8_t phrase_beats) {
  CachedRhythmPattern pattern;
  pattern.phrase_beats = phrase_beats;

  for (const auto& note : notes) {
    float beat = static_cast<float>(note.start_tick - section_start) / TICKS_PER_BEAT;
    float duration = static_cast<float>(note.duration) / TICKS_PER_BEAT;
    pattern.onset_beats.push_back(beat);
    pattern.durations.push_back(duration);
  }

  pattern.is_locked = true;
  return pattern;
}

/**
 * @brief Extract rhythm pattern from a MidiTrack for a specific section.
 * @param track Source track (e.g., Motif track)
 * @param section_start Section start tick
 * @param section_end Section end tick
 * @return Extracted rhythm pattern for the section
 *
 * Used in RhythmSync paradigm to extract Motif's rhythm pattern for Vocal synchronization.
 * The Motif acts as the "coordinate axis" and Vocal should follow its rhythm.
 */
inline CachedRhythmPattern extractRhythmPatternFromTrack(const std::vector<NoteEvent>& track_notes,
                                                          Tick section_start, Tick section_end) {
  CachedRhythmPattern pattern;
  uint8_t section_beats = static_cast<uint8_t>((section_end - section_start) / TICKS_PER_BEAT);
  pattern.phrase_beats = section_beats;

  for (const auto& note : track_notes) {
    // Only include notes within this section
    if (note.start_tick >= section_start && note.start_tick < section_end) {
      float beat = static_cast<float>(note.start_tick - section_start) / TICKS_PER_BEAT;
      float duration = static_cast<float>(note.duration) / TICKS_PER_BEAT;
      pattern.onset_beats.push_back(beat);
      pattern.durations.push_back(duration);
    }
  }

  pattern.is_locked = !pattern.onset_beats.empty();
  return pattern;
}

// ============================================================================
// Phrase Boundary Detection for Breath Insertion
// ============================================================================

/**
 * @brief Get max notes per phrase based on section type.
 *
 * Section-type dependent phrasing length:
 * - Chorus: 12 (shorter phrases for open, breathable feel)
 * - Verse: 16 (longer phrases for storytelling density)
 * - Bridge: 8 (open, spacious phrasing)
 * - Others: 12 (reasonable default)
 *
 * @param section_type Section type
 * @return Maximum notes before forced breath
 */
inline int getMaxNotesPerPhrase(SectionType section_type) {
  switch (section_type) {
    case SectionType::A:
      return 16;
    case SectionType::B:
      return 12;
    case SectionType::Chorus:
    case SectionType::Drop:
      return 12;
    case SectionType::Bridge:
      return 8;
    default:
      return 12;
  }
}

/**
 * @brief Detect phrase boundaries from rhythm pattern for breath insertion.
 *
 * Analyzes gaps between notes to find natural breathing points.
 * Ensures vocally singable passages by enforcing maximum phrase length.
 * Section-type aware: Chorus uses shorter phrases, Bridge is more open.
 * Barline positions (beat 4 boundaries) are preferred as breath candidates.
 *
 * @param pattern Rhythm pattern to analyze
 * @param section_type Section type for phrase length control (default: A)
 * @return Vector of phrase boundary beats (positions where breath can occur)
 */
inline std::vector<float> detectPhraseBoundariesFromRhythm(
    const CachedRhythmPattern& pattern, SectionType section_type = SectionType::A) {
  std::vector<float> boundaries;
  if (pattern.onset_beats.size() <= 1) {
    return boundaries;
  }

  constexpr float kMinGapForBreath = 0.5f;  // Half-beat gap = natural breath point
  int max_notes = getMaxNotesPerPhrase(section_type);

  int notes_since_boundary = 0;
  for (size_t i = 1; i < pattern.onset_beats.size(); ++i) {
    float prev_end = pattern.onset_beats[i - 1] + pattern.durations[i - 1];
    float gap = pattern.onset_beats[i] - prev_end;
    notes_since_boundary++;

    // Check if this onset is near a barline (beat 0 of a bar)
    float beat_in_bar = std::fmod(pattern.onset_beats[i], 4.0f);
    bool is_barline = (beat_in_bar < 0.25f);

    // Phrase boundary when:
    // 1. Sufficient gap between notes (natural breath point)
    // 2. Too many consecutive notes (forced breath)
    // 3. Barline position when approaching max (prefer musically natural break)
    bool force_breath = notes_since_boundary >= max_notes;
    bool near_limit_at_barline = is_barline && notes_since_boundary >= (max_notes * 3 / 4);

    if (gap >= kMinGapForBreath || force_breath || near_limit_at_barline) {
      boundaries.push_back(pattern.onset_beats[i]);
      notes_since_boundary = 0;
    }
  }
  return boundaries;
}

/**
 * @brief Get breath duration based on section type and context.
 *
 * Context-dependent breath durations:
 * - Sub-phrase (within a phrase): minimal gap (32nd note)
 * - Phrase boundary (same section): standard gap (8th note)
 * - Section boundary: larger gap (quarter note) for dramatic pause
 * - Ballad: longer breaths for expressiveness
 *
 * @param section_type Current section type
 * @param is_ballad True if slow/ballad mood
 * @param is_section_boundary True if this is a section boundary (default: false)
 * @return Breath duration in ticks
 */
inline Tick getBreathDuration(SectionType section_type, bool is_ballad,
                               bool is_section_boundary = false,
                               uint16_t bpm = 120) {
  Tick base;
  if (is_section_boundary) {
    // Section boundary: larger breath for dramatic pause
    base = TICK_QUARTER;  // 480 ticks
  } else {
    // Phrase boundary: standard breath
    switch (section_type) {
      case SectionType::Chorus:
      case SectionType::Drop:
        base = TICK_SIXTEENTH;  // 120 ticks - minimal breath for energy
        break;
      case SectionType::Bridge:
        base = TICK_EIGHTH;     // 240 ticks - spacious
        break;
      default:
        base = TICK_SIXTEENTH;  // 120 ticks
        break;
    }
  }

  // Ballads: longer breaths for expressiveness
  if (is_ballad) {
    base = static_cast<Tick>(base * 1.5f);
  }

  // BPM compensation: singers need ~150ms minimum for a breath regardless of tempo.
  // At fast tempos, fewer ticks correspond to the same real time, so we need more ticks.
  constexpr float kMinBreathSeconds = 0.15f;
  Tick min_breath_ticks = static_cast<Tick>(
      kMinBreathSeconds * bpm * TICKS_PER_BEAT / 60.0f);
  base = std::max(base, min_breath_ticks);

  return base;
}

// ============================================================================
// Run-Based Onset Selection for RhythmSync Vocal
// ============================================================================

/// @brief Minimum vocal onset interval based on vocal physiology (~200ms).
constexpr float kMinVocalOnsetSeconds = 0.2f;

/// @brief Calculate BPM-dependent minimum onset interval in ticks.
inline Tick calcMinOnsetInterval(uint16_t bpm) {
  return static_cast<Tick>(
      std::ceil(kMinVocalOnsetSeconds * bpm * TICKS_PER_BEAT / 60.0f));
}

/// @brief Position bonus constants for onset scoring.
constexpr float kFirstRunBonus = 0.3f;
constexpr float kLastRunBonus = 0.2f;
constexpr float kStrongBeatBonus = 0.1f;

/// @brief Build run-based filtered onset map for RhythmSync vocal.
///
/// Groups Motif onsets into "runs" (contiguous onset clusters), then
/// selectively keeps/trims runs to match PhrasePlan target_note_count.
/// This ensures rhythmic coherence: short notes only appear in dense
/// runs, never in isolation.
///
/// @param pattern Full Motif rhythm pattern for the section
/// @param phrase_plan PhrasePlan with per-phrase target_note_count
/// @param tmpl_config Motif rhythm template config (for accent weights)
/// @param bpm Current BPM
/// @param section_start Section start tick (absolute)
/// @return Filtered CachedRhythmPattern (onset_beats only, durations empty)
inline CachedRhythmPattern buildRunBasedOnsetMap(
    const CachedRhythmPattern& pattern,
    const PhrasePlan& phrase_plan,
    const motif_detail::MotifRhythmTemplateConfig& tmpl_config,
    uint16_t bpm,
    Tick section_start) {

  CachedRhythmPattern result;
  result.phrase_beats = pattern.phrase_beats;
  result.is_locked = pattern.is_locked;

  if (pattern.onset_beats.empty() || phrase_plan.phrases.empty()) {
    result.onset_beats = pattern.onset_beats;
    return result;
  }

  Tick min_interval_ticks = calcMinOnsetInterval(bpm);
  float min_interval_beats = static_cast<float>(min_interval_ticks) / TICKS_PER_BEAT;

  // Collect accent weights from template (beat_position -> weight mapping)
  // Build a lookup: for each onset beat, find closest template accent weight
  auto getAccentWeight = [&](float beat) -> float {
    float beat_in_bar = std::fmod(beat, 4.0f);
    if (beat_in_bar < 0.0f) beat_in_bar += 4.0f;
    float best_dist = 100.0f;
    float best_weight = 0.5f;
    for (uint8_t ti = 0; ti < tmpl_config.note_count; ++ti) {
      if (tmpl_config.beat_positions[ti] < 0) break;
      float dist = std::abs(beat_in_bar - tmpl_config.beat_positions[ti]);
      if (dist < best_dist) {
        best_dist = dist;
        best_weight = tmpl_config.accent_weights[ti];
      }
    }
    return best_weight;
  };

  // Process each phrase independently
  for (const auto& phrase : phrase_plan.phrases) {
    // Convert phrase boundaries to beat-relative (from section start)
    float phrase_start_beat =
        static_cast<float>(phrase.start_tick - section_start) / TICKS_PER_BEAT;
    // Use singable_end (pre-computed: end_tick - breath_after)
    float phrase_end_beat =
        static_cast<float>(phrase.singable_end - section_start) / TICKS_PER_BEAT;

    // Collect onsets within this phrase's singable region
    std::vector<size_t> phrase_onset_indices;
    for (size_t i = 0; i < pattern.onset_beats.size(); ++i) {
      float b = pattern.onset_beats[i];
      if (b >= phrase_start_beat && b < phrase_end_beat) {
        phrase_onset_indices.push_back(i);
      }
    }

    if (phrase_onset_indices.empty()) continue;

    int target = static_cast<int>(phrase.target_note_count * phrase.density_modifier);
    target = std::max(target, 2);  // At least 2 notes per phrase

    int total_onsets = static_cast<int>(phrase_onset_indices.size());

    if (total_onsets <= target) {
      // All onsets fit within target — keep all
      for (size_t idx : phrase_onset_indices) {
        result.onset_beats.push_back(pattern.onset_beats[idx]);
      }
    } else {
      // ================================================================
      // Phase 3: Select target onsets from all phrase onsets
      // ================================================================
      // Strategy: score each onset by accent weight + position bonus,
      // then keep the top `target` onsets.

      struct ScoredOnset {
        size_t pattern_index;  // Index into pattern.onset_beats
        float score;
        float beat;
      };

      std::vector<ScoredOnset> scored;
      scored.reserve(phrase_onset_indices.size());

      for (size_t idx : phrase_onset_indices) {
        float b = pattern.onset_beats[idx];
        float s = getAccentWeight(b);

        // Strong beat bonus
        float beat_in_bar = std::fmod(b, 4.0f);
        if (beat_in_bar < 0.0f) beat_in_bar += 4.0f;
        if (beat_in_bar < 0.1f || std::abs(beat_in_bar - 2.0f) < 0.1f) {
          s += kStrongBeatBonus;
        }

        // Phrase boundary bonus (first/last onset)
        if (idx == phrase_onset_indices.front()) s += kFirstRunBonus;
        if (idx == phrase_onset_indices.back()) s += kLastRunBonus;

        scored.push_back({idx, s, b});
      }

      // Sort by score descending (keep highest scored)
      std::sort(scored.begin(), scored.end(),
                [](const ScoredOnset& a, const ScoredOnset& b) {
                  return a.score > b.score;
                });

      // Keep top `target` onsets
      size_t keep_count = static_cast<size_t>(std::max(target, 2));
      if (keep_count > scored.size()) keep_count = scored.size();

      std::vector<float> kept_beats;
      kept_beats.reserve(keep_count);
      for (size_t k = 0; k < keep_count; ++k) {
        kept_beats.push_back(scored[k].beat);
      }

      // Sort by beat position (chronological order)
      std::sort(kept_beats.begin(), kept_beats.end());

      // ================================================================
      // Phase 5: min_interval check
      // ================================================================
      std::vector<float> final_beats;
      if (!kept_beats.empty()) {
        final_beats.push_back(kept_beats[0]);
        for (size_t k = 1; k < kept_beats.size(); ++k) {
          float gap_beats = kept_beats[k] - final_beats.back();
          if (gap_beats >= min_interval_beats) {
            final_beats.push_back(kept_beats[k]);
          }
        }
      }

      for (float b : final_beats) {
        result.onset_beats.push_back(b);
      }
    }
  }

  // Sort to ensure monotonic order
  std::sort(result.onset_beats.begin(), result.onset_beats.end());
  return result;
}

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_PHRASE_CACHE_H
