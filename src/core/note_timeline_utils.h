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
 * @brief Check if a note overlaps with a specific tick.
 *
 * @param note_start Start tick of the note
 * @param note_end End tick of the note
 * @param tick Tick to check
 * @return true if tick falls within the note's duration
 */
inline bool containsTick(Tick note_start, Tick note_end, Tick tick) {
  return note_start <= tick && tick < note_end;
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
 * @brief Trim a note to not extend past a boundary.
 *
 * If the note extends past the boundary, its duration is reduced.
 * A small gap (articulation) is optionally preserved.
 *
 * @param note Note to modify (in-place)
 * @param boundary Tick boundary to not exceed
 * @param gap_ticks Gap to leave before boundary (default: 10)
 * @return New duration, or 0 if note was entirely past boundary
 */
Tick trimToBoundary(NoteEvent& note, Tick boundary, Tick gap_ticks = 10);

/**
 * @brief Merge adjacent notes with the same pitch.
 *
 * Notes are considered adjacent if the end of one note equals
 * the start of the next. Merged notes combine their durations.
 *
 * @param notes Vector of notes (modified in-place, should be sorted by start_tick)
 */
void mergeAdjacentSamePitch(std::vector<NoteEvent>& notes);

/**
 * @brief Sort notes by start tick.
 *
 * @param notes Vector of notes to sort (modified in-place)
 */
void sortByStartTick(std::vector<NoteEvent>& notes);

/**
 * @brief Calculate the overlap amount between a note and a boundary.
 *
 * @param note_start Start tick of the note
 * @param note_end End tick of the note
 * @param boundary Boundary tick to check against
 * @return Amount of overlap in ticks, or 0 if no overlap
 */
inline Tick overlapAmount(Tick note_start, Tick note_end, Tick boundary) {
  if (boundary > note_start && boundary < note_end) {
    return note_end - boundary;
  }
  return 0;
}

/**
 * @brief Check if a note would be too short after trimming.
 *
 * @param note_start Note start tick
 * @param boundary Boundary to trim to
 * @param min_duration Minimum acceptable duration
 * @return true if the resulting duration would be less than min_duration
 */
inline bool wouldBeTooShort(Tick note_start, Tick boundary, Tick min_duration) {
  if (boundary <= note_start) return true;
  return (boundary - note_start) < min_duration;
}

}  // namespace NoteTimeline

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_NOTE_TIMELINE_UTILS_H
