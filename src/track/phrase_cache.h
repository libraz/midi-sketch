/**
 * @file phrase_cache.h
 * @brief Phrase caching structures for vocal melody generation.
 *
 * Provides structures for caching and reusing vocal phrases across sections.
 * Enables "varied repetition" where Chorus 1 and 2 share melodic content.
 */

#ifndef MIDISKETCH_TRACK_PHRASE_CACHE_H
#define MIDISKETCH_TRACK_PHRASE_CACHE_H

#include "core/section_types.h"
#include "core/types.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace midisketch {

/**
 * @brief Cached phrase for section repetition.
 *
 * Chorus 1 & 2 share melody with subtle variations for musical interest.
 */
struct CachedPhrase {
  std::vector<NoteEvent> notes;  ///< Notes with timing relative to section start
  uint8_t bars;                   ///< Section length when cached
  uint8_t vocal_low;              ///< Vocal range when cached
  uint8_t vocal_high;
  int reuse_count = 0;            ///< How many times this phrase has been reused
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
    return section_type == other.section_type &&
           bars == other.bars &&
           chord_degree == other.chord_degree;
  }
};

/**
 * @brief Hash function for PhraseCacheKey enabling use in unordered_map.
 */
struct PhraseCacheKeyHash {
  size_t operator()(const PhraseCacheKey& key) const {
    return std::hash<uint8_t>()(static_cast<uint8_t>(key.section_type)) ^
           (std::hash<uint8_t>()(key.bars) << 4) ^
           (std::hash<int8_t>()(key.chord_degree) << 8);
  }
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_PHRASE_CACHE_H
