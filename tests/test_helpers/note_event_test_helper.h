/**
 * @file note_event_test_helper.h
 * @brief Test helper for creating NoteEvent objects in unit tests.
 *
 * NoteEvent constructors are private to enforce use of NoteFactory for
 * dissonance checking. This helper class is a friend of NoteEvent and
 * provides test-only construction methods.
 */

#ifndef MIDISKETCH_TESTS_NOTE_EVENT_TEST_HELPER_H
#define MIDISKETCH_TESTS_NOTE_EVENT_TEST_HELPER_H

#include "core/basic_types.h"

namespace midisketch {

/// @brief Test helper for creating NoteEvent objects without NoteFactory.
///
/// This class is a friend of NoteEvent and provides methods for creating
/// NoteEvent objects in unit tests where NoteFactory is not appropriate.
///
/// @note This class should ONLY be used in tests. Production code should
/// use NoteFactory to ensure proper dissonance checking.
class NoteEventTestHelper {
 public:
  /// @brief Create a NoteEvent with specified parameters.
  /// @param start Start tick
  /// @param dur Duration in ticks
  /// @param note MIDI note number (0-127)
  /// @param vel Velocity (0-127)
  /// @return NoteEvent with the specified parameters
  static NoteEvent create(Tick start, Tick dur, uint8_t note, uint8_t vel) {
    return NoteEvent(start, dur, note, vel);
  }

  /// @brief Create a default-initialized NoteEvent.
  /// @return Default-initialized NoteEvent
  static NoteEvent createDefault() { return NoteEvent(); }
};

}  // namespace midisketch

#endif  // MIDISKETCH_TESTS_NOTE_EVENT_TEST_HELPER_H
