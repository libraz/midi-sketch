/**
 * @file harmony_timeline_planner.h
 * @brief Every harmony decision the song makes before a single note exists.
 *
 * The progression a song is planned from is not the harmony it plays. Secondary
 * dominants, cadence replacements, passing diminished chords, tritone
 * substitutions and the chord extensions are all decided here and written onto
 * the shared timeline, and every track then voices against the timeline rather
 * than against the progression array. A track that reads the array instead
 * states a chord the rest of the band has already replaced.
 *
 * Planning it all in one place, before generation, is what lets the vocal-first
 * preview and full generation agree: the melody is designed against the same
 * harmony the accompaniment will later be built on, rather than against a
 * simpler one that the accompaniment then contradicts.
 */

#ifndef MIDISKETCH_CORE_HARMONY_TIMELINE_PLANNER_H
#define MIDISKETCH_CORE_HARMONY_TIMELINE_PLANNER_H

#include <cstdint>
#include <vector>

namespace midisketch {

class Arrangement;
class IHarmonyCoordinator;
struct GeneratorParams;
struct ChordProgression;
struct Section;
enum class Mood : uint8_t;

/// @brief Register every generation-time harmony decision on the timeline.
///
/// The planners run in a fixed order and it is load-bearing: secondary
/// dominants go first and each later planner writes around what is already
/// there, which is what stops two devices from cancelling each other out.
///
/// @param arrangement The song's sections
/// @param params Generation parameters, including the seed the planners derive from
/// @param progression The planned progression the decisions are made against
/// @param harmony Harmony coordinator, left holding the timeline every track will read
void registerPlannedHarmonyTimeline(const Arrangement& arrangement, const GeneratorParams& params,
                                    const ChordProgression& progression,
                                    IHarmonyCoordinator& harmony);

/// @brief The bars of a chorus that state the same place in its loop as @p bar, @p bar included.
///
/// A chorus restates its loop up to the two bars it closes on, and the planner
/// writes every restated bar's harmony from the first statement. A decision a
/// track makes later on one of these bars has to be one it makes on all of
/// them, or the restatement comes apart again. Empty when @p bar is not in a
/// restated loop.
std::vector<uint8_t> restatedLoopBars(const Section& section, uint8_t bar,
                                      const ChordProgression& progression, Mood mood);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_HARMONY_TIMELINE_PLANNER_H
