/**
 * @file phrase_cache.h
 * @brief Phrase caching structures for vocal melody generation.
 *
 * Provides structures for caching and reusing vocal phrases across sections.
 * Enables "varied repetition" where Chorus 1 and 2 share melodic content.
 */

#ifndef MIDISKETCH_TRACK_PHRASE_CACHE_H
#define MIDISKETCH_TRACK_PHRASE_CACHE_H

#include <cstdint>
#include <functional>
#include <vector>

#include "core/section_types.h"
#include "core/types.h"

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

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_PHRASE_CACHE_H
