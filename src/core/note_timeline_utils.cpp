/**
 * @file note_timeline_utils.cpp
 * @brief Implementation of note timeline utilities.
 */

#include "core/note_timeline_utils.h"

#include <algorithm>

namespace midisketch {
namespace NoteTimeline {

void fixOverlaps(std::vector<NoteEvent>& notes) {
  if (notes.size() <= 1) {
    return;
  }

  // Ensure notes are sorted
  sortByStartTick(notes);

  for (size_t i = 0; i + 1 < notes.size(); ++i) {
    Tick end_tick = notes[i].start_tick + notes[i].duration;
    Tick next_start = notes[i + 1].start_tick;

    // Ensure no overlap: end of current note <= start of next note
    if (end_tick > next_start) {
      // Guard against underflow: if same start_tick, use minimum duration
      Tick max_duration = (next_start > notes[i].start_tick) ? (next_start - notes[i].start_tick) : 1;
      notes[i].duration = max_duration;

      // If still overlapping (same start_tick case), shift next note
      if (notes[i].start_tick + notes[i].duration > next_start) {
        notes[i + 1].start_tick = notes[i].start_tick + notes[i].duration;
      }
    }
  }
}

void fixOverlapsWithMinDuration(std::vector<NoteEvent>& notes, Tick min_duration) {
  if (notes.size() < 2) return;

  // Sort by start tick
  sortByStartTick(notes);

  // First pass: ensure minimum duration, respecting space to next note
  for (size_t i = 0; i < notes.size(); ++i) {
    if (notes[i].duration < min_duration) {
      Tick max_safe = min_duration;
      if (i + 1 < notes.size() && notes[i + 1].start_tick > notes[i].start_tick) {
        Tick space = notes[i + 1].start_tick - notes[i].start_tick;
        if (space < min_duration) {
          max_safe = space;
        }
      }
      notes[i].duration = std::max(notes[i].duration, max_safe);
    }
  }

  // Second pass: resolve any remaining overlaps by truncating duration
  for (size_t i = 0; i + 1 < notes.size(); ++i) {
    Tick end_tick = notes[i].start_tick + notes[i].duration;
    Tick next_start = notes[i + 1].start_tick;

    if (end_tick > next_start) {
      // Truncate current note to end at next note's start
      if (next_start > notes[i].start_tick) {
        notes[i].duration = next_start - notes[i].start_tick;
      } else {
        // Same start tick: shift next note forward
        notes[i + 1].start_tick = notes[i].start_tick + notes[i].duration;
      }
    }
  }

  // Re-sort in case shifts changed order, then final overlap check
  sortByStartTick(notes);

  // Final pass: ensure no overlaps remain after re-sort
  for (size_t i = 0; i + 1 < notes.size(); ++i) {
    Tick end_tick = notes[i].start_tick + notes[i].duration;
    if (end_tick > notes[i + 1].start_tick) {
      if (notes[i + 1].start_tick > notes[i].start_tick) {
        notes[i].duration = notes[i + 1].start_tick - notes[i].start_tick;
      } else {
        // Last resort: set minimal duration
        notes[i].duration = 1;
      }
    }
  }
}

Tick trimToBoundary(NoteEvent& note, Tick boundary, Tick gap_ticks) {
  Tick note_end = note.start_tick + note.duration;

  // Note doesn't extend past boundary
  if (note_end <= boundary) {
    return note.duration;
  }

  // Note starts at or after boundary
  if (note.start_tick >= boundary) {
    note.duration = 0;
    return 0;
  }

  // Calculate new duration with gap
  Tick time_to_boundary = boundary - note.start_tick;
  if (time_to_boundary > gap_ticks) {
    note.duration = time_to_boundary - gap_ticks;
  } else {
    // Not enough room for gap, use minimum duration
    note.duration = (time_to_boundary > 0) ? time_to_boundary : 1;
  }

  return note.duration;
}

void mergeAdjacentSamePitch(std::vector<NoteEvent>& notes) {
  if (notes.size() <= 1) {
    return;
  }

  // Ensure notes are sorted
  sortByStartTick(notes);

  std::vector<NoteEvent> merged;
  merged.reserve(notes.size());
  merged.push_back(notes[0]);

  for (size_t i = 1; i < notes.size(); ++i) {
    NoteEvent& prev = merged.back();
    const NoteEvent& curr = notes[i];

    Tick prev_end = prev.start_tick + prev.duration;

    // Check if same pitch and adjacent (end of prev == start of curr)
    if (prev.note == curr.note && prev_end == curr.start_tick) {
      // Merge: extend previous note's duration
      prev.duration += curr.duration;
      // Keep velocity of first note (or could average)
    } else {
      merged.push_back(curr);
    }
  }

  notes = std::move(merged);
}

void sortByStartTick(std::vector<NoteEvent>& notes) {
  std::sort(notes.begin(), notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });
}

}  // namespace NoteTimeline
}  // namespace midisketch
