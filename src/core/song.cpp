/**
 * @file song.cpp
 * @brief Implementation of Song container operations.
 */

#include "core/song.h"

namespace midisketch {

void Song::clearAll() {
  for (auto& track : tracks_) {
    track.clear();
  }
  motif_pattern_.clear();
  phrase_boundaries_.clear();
}

void Song::setArrangement(const Arrangement& arrangement) { arrangement_ = arrangement; }

}  // namespace midisketch
