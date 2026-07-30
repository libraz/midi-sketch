/**
 * @file overlap_note_filter.h
 * @brief Efficient removal of notes matching an overlapping reference note.
 */

#ifndef MIDISKETCH_CORE_OVERLAP_NOTE_FILTER_H
#define MIDISKETCH_CORE_OVERLAP_NOTE_FILTER_H

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <vector>

#include "core/basic_types.h"

namespace midisketch {

/// Remove target notes for which @p should_remove returns true for any
/// overlapping reference note.  Both source vectors may be chronologically
/// unsorted; index vectors preserve their original order while the sweep keeps
/// only references whose duration can still overlap the current target.
///
/// The predicate receives only genuinely overlapping pairs.  This avoids the
/// repeated full reference-track scans used by post-processing cleanup passes,
/// while a single erase-remove compaction avoids memmove per deleted note.
template <typename Predicate>
void eraseNotesMatchingOverlappingReference(std::vector<NoteEvent>& target_notes,
                                            const std::vector<NoteEvent>& reference_notes,
                                            Predicate should_remove) {
  if (target_notes.empty() || reference_notes.empty()) return;

  std::vector<size_t> target_order(target_notes.size());
  std::iota(target_order.begin(), target_order.end(), 0);
  std::sort(target_order.begin(), target_order.end(), [&](size_t lhs, size_t rhs) {
    return target_notes[lhs].start_tick < target_notes[rhs].start_tick;
  });

  std::vector<size_t> reference_order(reference_notes.size());
  std::iota(reference_order.begin(), reference_order.end(), 0);
  std::sort(reference_order.begin(), reference_order.end(), [&](size_t lhs, size_t rhs) {
    return reference_notes[lhs].start_tick < reference_notes[rhs].start_tick;
  });

  std::vector<size_t> active_references;
  active_references.reserve(reference_notes.size());
  std::vector<char> remove(target_notes.size(), false);
  size_t next_reference = 0;
  Tick furthest_target_end = 0;

  for (size_t target_index : target_order) {
    const NoteEvent& target = target_notes[target_index];
    const Tick target_end = target.start_tick + target.duration;

    // A long earlier target may have already required us to add references
    // that start after this target ends.  Keep them active but reject them in
    // the exact overlap check below.
    if (target_end > furthest_target_end) {
      furthest_target_end = target_end;
      while (next_reference < reference_order.size() &&
             reference_notes[reference_order[next_reference]].start_tick < furthest_target_end) {
        active_references.push_back(reference_order[next_reference++]);
      }
    }

    active_references.erase(
        std::remove_if(active_references.begin(), active_references.end(),
                       [&](size_t ref_index) {
                         const NoteEvent& reference = reference_notes[ref_index];
                         return reference.start_tick + reference.duration <= target.start_tick;
                       }),
        active_references.end());

    for (size_t ref_index : active_references) {
      const NoteEvent& reference = reference_notes[ref_index];
      const Tick reference_end = reference.start_tick + reference.duration;
      if (target.start_tick >= reference_end || target_end <= reference.start_tick) continue;
      if (should_remove(target, reference)) {
        remove[target_index] = true;
        break;
      }
    }
  }

  size_t target_index = 0;
  target_notes.erase(std::remove_if(target_notes.begin(), target_notes.end(),
                                    [&](const NoteEvent&) { return remove[target_index++]; }),
                     target_notes.end());
}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_OVERLAP_NOTE_FILTER_H
