#include "core/rewrite_pitch_guard.h"

#include <algorithm>

#include "core/chord.h"
#include "core/i_harmony_context.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"

namespace midisketch {

uint8_t clampScalePitch(int pitch, uint8_t low, uint8_t high) {
  pitch = std::clamp(pitch, static_cast<int>(low), static_cast<int>(high));
  pitch = snapToNearestScaleTone(pitch, 0);
  if (pitch < static_cast<int>(low)) {
    for (int candidate = low; candidate <= static_cast<int>(high); ++candidate) {
      if (isScaleTone(candidate % 12, 0)) {
        return static_cast<uint8_t>(candidate);
      }
    }
  }
  if (pitch > static_cast<int>(high)) {
    for (int candidate = high; candidate >= static_cast<int>(low); --candidate) {
      if (isScaleTone(candidate % 12, 0)) {
        return static_cast<uint8_t>(candidate);
      }
    }
  }
  pitch = std::clamp(pitch, static_cast<int>(low), static_cast<int>(high));
  return static_cast<uint8_t>(pitch);
}

uint8_t clampScalePitchAvoidingChord(int pitch, Tick tick, const IHarmonyContext& harmony,
                                     uint8_t low, uint8_t high) {
  int8_t degree = harmony.getChordDegreeAt(tick);
  uint8_t chord_root = degreeToRoot(degree, Key::C);
  Chord chord = getChordNotes(degree);
  bool is_minor = (chord.intervals[1] == 3);

  uint8_t initial = clampScalePitch(pitch, low, high);
  if (!isAvoidNoteWithContext(initial, chord_root, is_minor, degree)) {
    return initial;
  }

  // Walk candidates from the CLAMPED pitch, not the raw input: when the input
  // sits far outside [low, high] (e.g. a register shift from a much lower
  // octave), every raw-input offset is out of range, the walk finds nothing,
  // and the avoid note from the initial clamp leaks through.
  for (int offset = 1; offset <= 12; ++offset) {
    for (int direction : {-1, 1}) {
      int candidate_pitch = static_cast<int>(initial) + direction * offset;
      if (candidate_pitch < static_cast<int>(low) || candidate_pitch > static_cast<int>(high)) {
        continue;
      }
      uint8_t candidate = clampScalePitch(candidate_pitch, low, high);
      if (!isAvoidNoteWithContext(candidate, chord_root, is_minor, degree)) {
        return candidate;
      }
    }
  }

  return initial;
}

// Check that a pitch is not an avoid note for ANY chord sounding during
// [tick, tick + duration). A note checked only at its start tick can sustain
// across a chord boundary into an avoid relationship (e.g. a DNA-stamped B
// held from V into IV forms a tritone with the new F root).
bool isAvoidNoteOverSpan(uint8_t pitch, Tick tick, Tick duration, const IHarmonyContext& harmony) {
  Tick end = tick + std::max<Tick>(duration, 1);
  int8_t last_degree = -100;
  for (Tick t = tick; t < end; t += TICKS_PER_BEAT) {
    int8_t degree = harmony.getChordDegreeAt(t);
    if (degree == last_degree) continue;
    last_degree = degree;
    uint8_t chord_root = degreeToRoot(degree, Key::C);
    Chord chord = getChordNotes(degree);
    bool is_minor = (chord.intervals[1] == 3);
    if (isAvoidNoteWithContext(pitch, chord_root, is_minor, degree)) {
      return true;
    }
  }
  return false;
}

}  // namespace midisketch
