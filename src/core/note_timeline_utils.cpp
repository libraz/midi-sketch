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

void sortByStartTick(std::vector<NoteEvent>& notes) {
  std::sort(notes.begin(), notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });
}

}  // namespace NoteTimeline
}  // namespace midisketch
