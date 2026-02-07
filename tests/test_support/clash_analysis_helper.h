/**
 * @file clash_analysis_helper.h
 * @brief Shared clash analysis utilities for dissonance tests.
 *
 * Provides ClashInfo struct, findClashes(), and analyzeAllTrackPairs()
 * used by both dissonance_integration_test.cpp and dissonance_diagnostic_test.cpp.
 */

#ifndef MIDISKETCH_TEST_CLASH_ANALYSIS_HELPER_H
#define MIDISKETCH_TEST_CLASH_ANALYSIS_HELPER_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/basic_types.h"
#include "core/i_harmony_context.h"
#include "core/midi_track.h"
#include "core/pitch_utils.h"
#include "core/song.h"

namespace midisketch {
namespace test {

/// Maximum allowed register separation for clash detection (2 octaves)
constexpr int kMaxClashSeparation = 24;

struct ClashInfo {
  std::string track_a;
  std::string track_b;
  uint8_t pitch_a;
  uint8_t pitch_b;
  Tick tick;
  int interval;
};

/**
 * @brief Get track name for reporting.
 * @param track Pointer to a MidiTrack
 * @param song The Song containing the tracks
 * @return Human-readable track name
 */
inline std::string getTrackName(const MidiTrack* track, const Song& song) {
  if (track == &song.vocal()) return "Vocal";
  if (track == &song.bass()) return "Bass";
  if (track == &song.chord()) return "Chord";
  if (track == &song.motif()) return "Motif";
  if (track == &song.aux()) return "Aux";
  return "Unknown";
}

/**
 * @brief Find all dissonant clashes between two tracks using chord context.
 * @param track_a First track
 * @param name_a Name of first track (for reporting)
 * @param track_b Second track
 * @param name_b Name of second track (for reporting)
 * @param harmony Harmony context for chord-degree-aware dissonance detection
 * @return Vector of ClashInfo for each dissonant pair found
 */
inline std::vector<ClashInfo> findClashes(const MidiTrack& track_a, const std::string& name_a,
                                          const MidiTrack& track_b, const std::string& name_b,
                                          const IHarmonyContext& harmony) {
  std::vector<ClashInfo> clashes;

  for (const auto& note_a : track_a.notes()) {
    Tick start_a = note_a.start_tick;
    Tick end_a = start_a + note_a.duration;

    for (const auto& note_b : track_b.notes()) {
      Tick start_b = note_b.start_tick;
      Tick end_b = start_b + note_b.duration;

      // Check temporal overlap
      bool overlap = (start_a < end_b) && (start_b < end_a);
      if (!overlap) continue;

      // Calculate actual interval
      int actual_interval =
          std::abs(static_cast<int>(note_a.note) - static_cast<int>(note_b.note));

      // Skip wide separations (perceptually not clashing)
      if (actual_interval >= kMaxClashSeparation) continue;

      // Check dissonance using unified logic from pitch_utils
      Tick overlap_tick = std::max(start_a, start_b);
      int8_t chord_degree = harmony.getChordDegreeAt(overlap_tick);

      if (isDissonantActualInterval(actual_interval, chord_degree)) {
        clashes.push_back(
            {name_a, name_b, note_a.note, note_b.note, overlap_tick, actual_interval});
      }
    }
  }

  return clashes;
}

/**
 * @brief Analyze all track pairs in a song for dissonances using chord context.
 * @param song The Song to analyze
 * @param harmony Harmony context for chord-degree-aware dissonance detection
 * @return Vector of all ClashInfo found across all track pairs
 */
inline std::vector<ClashInfo> analyzeAllTrackPairs(const Song& song,
                                                    const IHarmonyContext& harmony) {
  std::vector<ClashInfo> all_clashes;

  // Get all melodic tracks (skip drums and SE)
  std::vector<std::pair<const MidiTrack*, std::string>> tracks;
  if (!song.vocal().empty()) tracks.push_back({&song.vocal(), "Vocal"});
  if (!song.bass().empty()) tracks.push_back({&song.bass(), "Bass"});
  if (!song.chord().empty()) tracks.push_back({&song.chord(), "Chord"});
  if (!song.motif().empty()) tracks.push_back({&song.motif(), "Motif"});
  if (!song.aux().empty()) tracks.push_back({&song.aux(), "Aux"});

  // Check all unique pairs
  for (size_t idx = 0; idx < tracks.size(); ++idx) {
    for (size_t jdx = idx + 1; jdx < tracks.size(); ++jdx) {
      auto clashes = findClashes(*tracks[idx].first, tracks[idx].second, *tracks[jdx].first,
                                 tracks[jdx].second, harmony);
      all_clashes.insert(all_clashes.end(), clashes.begin(), clashes.end());
    }
  }

  return all_clashes;
}

}  // namespace test
}  // namespace midisketch

#endif  // MIDISKETCH_TEST_CLASH_ANALYSIS_HELPER_H
