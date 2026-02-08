/**
 * @file note_timeline_utils.h
 * @brief Utilities for note timing and overlap handling.
 *
 * Consolidates common note timeline operations from aux_track.cpp
 * and post_processor.cpp.
 */

#ifndef MIDISKETCH_CORE_NOTE_TIMELINE_UTILS_H
#define MIDISKETCH_CORE_NOTE_TIMELINE_UTILS_H

#include <vector>

#include "core/basic_types.h"

namespace midisketch {

/**
 * @brief Utilities for note timing and overlap operations.
 */
namespace NoteTimeline {

/**
 * @brief Check if two note time ranges overlap.
 *
 * @param start1 Start tick of first note
 * @param end1 End tick of first note (start + duration)
 * @param start2 Start tick of second note
 * @param end2 End tick of second note
 * @return true if the time ranges overlap
 */
inline bool overlaps(Tick start1, Tick end1, Tick start2, Tick end2) {
  return start1 < end2 && start2 < end1;
}

/**
 * @brief Fix overlapping notes by trimming earlier notes.
 *
 * For consecutive notes, trims the earlier note's duration so it
 * ends at or before the next note starts. Handles same-start-tick
 * cases by using minimum duration.
 *
 * @param notes Vector of notes (modified in-place, should be sorted by start_tick)
 */
void fixOverlaps(std::vector<NoteEvent>& notes);

/**
 * @brief Fix overlaps with minimum duration enforcement.
 *
 * Extended version of fixOverlaps that:
 * 1. Sorts notes by start tick
 * 2. Ensures notes meet minimum duration (while respecting next note)
 * 3. Resolves any remaining overlaps by trimming
 *
 * @param notes Vector of notes (modified in-place)
 * @param min_duration Minimum duration to enforce for each note
 */
void fixOverlapsWithMinDuration(std::vector<NoteEvent>& notes, Tick min_duration);

/**
 * @brief Sort notes by start tick.
 *
 * @param notes Vector of notes to sort (modified in-place)
 */
void sortByStartTick(std::vector<NoteEvent>& notes);

}  // namespace NoteTimeline

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_NOTE_TIMELINE_UTILS_H
