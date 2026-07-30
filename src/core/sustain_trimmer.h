/**
 * @file sustain_trimmer.h
 * @brief Trim sustained melodic notes that become chromatic at a chord change.
 */

#ifndef MIDISKETCH_CORE_SUSTAIN_TRIMMER_H
#define MIDISKETCH_CORE_SUSTAIN_TRIMMER_H

#include <algorithm>
#include <cmath>

#include "core/i_harmony_context.h"
#include "core/midi_track.h"
#include "core/timing_constants.h"

namespace midisketch {

/// Trim a long note before a later chord creates a semitone rub with it.
/// Benign non-chord tones remain sustained; only chromatic m2/M7 conflicts
/// trigger the release gap.
inline void trimSustainsAtDissonantChordChanges(MidiTrack& track, const IHarmonyContext& harmony) {
  for (auto& note : track.notes()) {
    if (note.duration <= TICK_QUARTER) continue;

    const Tick note_end = note.start_tick + note.duration;
    int8_t previous_degree = harmony.getChordDegreeAt(note.start_tick);
    for (Tick tick = note.start_tick + TICK_SIXTEENTH; tick < note_end; tick += TICK_SIXTEENTH) {
      const int8_t degree = harmony.getChordDegreeAt(tick);
      if (degree == previous_degree) continue;
      previous_degree = degree;

      const int pitch_class = note.note % 12;
      bool is_chord_tone = false;
      bool half_step_conflict = false;
      for (int chord_pitch_class : harmony.getChordTonesAt(tick)) {
        if (chord_pitch_class == pitch_class) {
          is_chord_tone = true;
          break;
        }
        const int distance = std::abs(chord_pitch_class - pitch_class);
        half_step_conflict |= std::min(distance, 12 - distance) == 1;
      }
      if (is_chord_tone || !half_step_conflict) continue;

      constexpr Tick kReleaseGap = 30;
      constexpr Tick kMinRemaining = TICK_EIGHTH;
      if (tick > note.start_tick + kMinRemaining + kReleaseGap) {
        note.duration = tick - note.start_tick - kReleaseGap;
#ifdef MIDISKETCH_NOTE_PROVENANCE
        note.addTransformStep(TransformStepType::PostProcessDuration, 0, 0, -1, 0);
#endif
      }
      break;
    }
  }
}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_SUSTAIN_TRIMMER_H
