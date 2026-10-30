/**
 * @file chord_timeline_helper.h
 * @brief Walks the harmony timeline as chords a listener hears.
 */

#ifndef MIDISKETCH_TEST_CHORD_TIMELINE_HELPER_H
#define MIDISKETCH_TEST_CHORD_TIMELINE_HELPER_H

#include <vector>

#include "core/i_harmony_context.h"
#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {
namespace test {

/// One chord as heard: back-to-back timeline entries naming the same chord merged.
struct ChordRun {
  Tick start = 0;
  Tick end = 0;
  int8_t degree = 0;
  ChordExtension extension = ChordExtension::None;
  bool secondary_dominant = false;
};

/**
 * @brief Split [0, song_end) into chord runs.
 *
 * Consecutive entries with the same degree, extension and secondary-dominant flag
 * (an anticipation slice and the bar it leads into) form a single run.
 */
inline std::vector<ChordRun> chordRuns(const IHarmonyContext& harmony, Tick song_end) {
  std::vector<ChordRun> runs;
  for (Tick start = 0; start < song_end;) {
    ChordRun run;
    run.start = start;
    run.degree = harmony.getChordDegreeAt(start);
    run.extension = harmony.getChordExtensionAt(start);
    run.secondary_dominant = harmony.isSecondaryDominantAt(start);
    Tick end = start;
    do {
      const Tick next = harmony.getNextChordEntryTick(end);
      end = (next <= end || next > song_end) ? song_end : next;
    } while (end < song_end && harmony.getChordDegreeAt(end) == run.degree &&
             harmony.getChordExtensionAt(end) == run.extension &&
             harmony.isSecondaryDominantAt(end) == run.secondary_dominant);
    run.end = end;
    runs.push_back(run);
    start = end;
  }
  return runs;
}

}  // namespace test
}  // namespace midisketch

#endif  // MIDISKETCH_TEST_CHORD_TIMELINE_HELPER_H
