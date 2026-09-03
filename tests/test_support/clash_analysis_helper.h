/**
 * @file clash_analysis_helper.h
 * @brief Shared clash analysis utilities for dissonance tests.
 *
 * Every judgement here comes from analyzeDissonance(), the same detector the
 * CLI and the C API report with. A second, hand-rolled interval rule in test
 * code would drift from it, and a test-side rule that is even slightly more
 * permissive lets through exactly the clashes the shipped analysis reports.
 * These helpers only select and reshape what that detector returned.
 */

#ifndef MIDISKETCH_TEST_CLASH_ANALYSIS_HELPER_H
#define MIDISKETCH_TEST_CLASH_ANALYSIS_HELPER_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "analysis/dissonance.h"
#include "core/basic_types.h"
#include "core/i_chord_lookup.h"
#include "core/i_harmony_context.h"
#include "core/midi_track.h"
#include "core/pitch_utils.h"
#include "core/song.h"
#include "core/track_collision_detector.h"
#include "core/types.h"

namespace midisketch {
namespace test {

struct ClashInfo {
  std::string track_a;
  std::string track_b;
  uint8_t pitch_a;
  uint8_t pitch_b;
  Tick tick;
  int interval;
};

/**
 * @brief Every simultaneous clash the shipped analysis reports for a song.
 * @param song The Song to analyze
 * @param params Generation params the song was produced with
 * @param harmony Generation-time harmony timeline (registered extensions intact)
 * @return One ClashInfo per reported clash, in report order
 */
inline std::vector<ClashInfo> analyzeAllTrackPairs(const Song& song, const GeneratorParams& params,
                                                   const IChordLookup& harmony) {
  std::vector<ClashInfo> all_clashes;
  const DissonanceReport report = analyzeDissonance(song, params, harmony);

  for (const auto& issue : report.issues) {
    if (issue.type != DissonanceType::SimultaneousClash) continue;
    if (issue.notes.size() < 2) continue;
    all_clashes.push_back({issue.notes[0].track_name, issue.notes[1].track_name,
                           issue.notes[0].pitch, issue.notes[1].pitch, issue.tick,
                           static_cast<int>(issue.interval_semitones)});
  }

  return all_clashes;
}

/**
 * @brief The reported clashes that involve one specific pair of tracks.
 * @param song The Song to analyze
 * @param params Generation params the song was produced with
 * @param harmony Generation-time harmony timeline
 * @param role_a First track role
 * @param role_b Second track role
 * @return ClashInfo for each reported clash between those two tracks
 */
inline std::vector<ClashInfo> findClashes(const Song& song, const GeneratorParams& params,
                                          const IChordLookup& harmony, TrackRole role_a,
                                          TrackRole role_b) {
  const std::string name_a = trackRoleToString(role_a);
  const std::string name_b = trackRoleToString(role_b);

  std::vector<ClashInfo> clashes;
  for (const auto& clash : analyzeAllTrackPairs(song, params, harmony)) {
    const bool forward = clash.track_a == name_a && clash.track_b == name_b;
    const bool reverse = clash.track_a == name_b && clash.track_b == name_a;
    if (forward || reverse) {
      clashes.push_back(clash);
    }
  }
  return clashes;
}

}  // namespace test
}  // namespace midisketch

#endif  // MIDISKETCH_TEST_CLASH_ANALYSIS_HELPER_H
