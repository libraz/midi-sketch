/**
 * @file test_helpers.h
 * @brief Shared helper functions for tests.
 *
 * Common utility functions used across multiple test files.
 */

#ifndef MIDISKETCH_TEST_TEST_HELPERS_H
#define MIDISKETCH_TEST_TEST_HELPERS_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "core/midi_track.h"
#include "core/timing_constants.h"
#include "core/types.h"

namespace midisketch {
namespace test {

/**
 * @brief Count notes from a track within a section's tick range.
 * @param track The MIDI track to count notes in
 * @param section The section defining the time range
 * @return Number of notes whose start_tick falls within the section
 */
inline int countNotesInSection(const MidiTrack& track, const Section& section) {
  Tick section_start = section.start_tick;
  Tick section_end = section_start + section.bars * TICKS_PER_BAR;
  int count = 0;
  for (const auto& note : track.notes()) {
    if (note.start_tick >= section_start && note.start_tick < section_end) {
      ++count;
    }
  }
  return count;
}

/**
 * @brief Count notes from a note vector within a tick range.
 * @param notes The notes to count
 * @param section_start Start tick of the range
 * @param section_end End tick of the range
 * @return Number of notes whose start_tick falls within [section_start, section_end)
 */
inline size_t countNotesInRange(const std::vector<NoteEvent>& notes, Tick section_start,
                                Tick section_end) {
  size_t count = 0;
  for (const auto& note : notes) {
    if (note.start_tick >= section_start && note.start_tick < section_end) {
      ++count;
    }
  }
  return count;
}

}  // namespace test
}  // namespace midisketch

#endif  // MIDISKETCH_TEST_TEST_HELPERS_H
