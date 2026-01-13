/**
 * @file chord_progression_tracker.cpp
 * @brief Implementation of chord progression tracking.
 */

#include "core/chord_progression_tracker.h"
#include "core/arrangement.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/harmonic_rhythm.h"
#include <algorithm>

namespace midisketch {

void ChordProgressionTracker::initialize(const Arrangement& arrangement,
                                          const ChordProgression& progression,
                                          Mood mood) {
  chords_.clear();

  const auto& sections = arrangement.sections();

  for (const auto& section : sections) {
    HarmonicRhythmInfo harmonic =
        HarmonicRhythmInfo::forSection(section.type, mood);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      // Calculate chord index based on harmonic rhythm
      int chord_idx;
      if (harmonic.density == HarmonicDensity::Slow) {
        // Slow: chord changes every 2 bars
        chord_idx = (bar / 2) % progression.length;
      } else {
        // Normal/Dense: chord changes every bar
        chord_idx = bar % progression.length;
      }

      int8_t degree = progression.degrees[chord_idx];

      // Check if this bar should split for phrase-end anticipation (Dense
      // rhythm) Uses same logic as chord_track for synchronization
      bool should_split =
          shouldSplitPhraseEnd(bar, section.bars, progression.length, harmonic,
                               section.type, mood);

      if (should_split) {
        // Dense harmonic rhythm: split bar into two chord entries
        // First half: current chord
        Tick half_bar = TICKS_PER_BAR / 2;
        chords_.push_back({bar_start, bar_start + half_bar, degree});

        // Second half: next chord (anticipation)
        int next_chord_idx = (chord_idx + 1) % progression.length;
        int8_t next_degree = progression.degrees[next_chord_idx];
        chords_.push_back(
            {bar_start + half_bar, bar_start + TICKS_PER_BAR, next_degree});
      } else {
        // Normal: one chord for the whole bar
        chords_.push_back({bar_start, bar_start + TICKS_PER_BAR, degree});
      }
    }
  }
}

int8_t ChordProgressionTracker::getChordDegreeAt(Tick tick) const {
  if (chords_.empty()) {
    return 0;  // Fallback: return I chord
  }

  // Binary search: find first chord whose start > tick
  auto it = std::upper_bound(
      chords_.begin(), chords_.end(), tick,
      [](Tick t, const ChordInfo& c) { return t < c.start; });

  // If it's not the first element, check the previous chord
  if (it != chords_.begin()) {
    --it;
    if (tick >= it->start && tick < it->end) {
      return it->degree;
    }
  }

  // Fallback: return I chord
  return 0;
}

Tick ChordProgressionTracker::getNextChordChangeTick(Tick after) const {
  if (chords_.empty()) {
    return 0;
  }

  // Find the chord that contains 'after'
  for (size_t i = 0; i < chords_.size(); ++i) {
    if (after >= chords_[i].start && after < chords_[i].end) {
      // Check if next chord exists and has different degree
      if (i + 1 < chords_.size() &&
          chords_[i + 1].degree != chords_[i].degree) {
        return chords_[i + 1].start;
      }
      // Same degree continues, keep looking
      for (size_t j = i + 1; j < chords_.size(); ++j) {
        if (chords_[j].degree != chords_[i].degree) {
          return chords_[j].start;
        }
      }
      break;
    }
  }

  return 0;  // No chord change found
}

std::vector<int> ChordProgressionTracker::getChordTonesAt(Tick tick) const {
  int8_t degree = getChordDegreeAt(tick);
  return getChordTonePitchClasses(degree);
}

void ChordProgressionTracker::clear() { chords_.clear(); }

}  // namespace midisketch
