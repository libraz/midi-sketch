/**
 * @file isolated_note_resolver.cpp
 * @brief Implementation of isolated note resolution for melody generation.
 */

#include "track/melody/isolated_note_resolver.h"

#include <algorithm>
#include <cmath>

#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/pitch_utils.h"

namespace midisketch {
namespace melody {

bool isIsolatedNote(int prev_pitch, int curr_pitch, int next_pitch) {
  int interval_before = std::abs(curr_pitch - prev_pitch);
  int interval_after = std::abs(next_pitch - curr_pitch);
  return interval_before >= kIsolationThreshold && interval_after >= kIsolationThreshold;
}

int findConnectingPitch(int prev_pitch, int next_pitch, int8_t chord_degree,
                        uint8_t vocal_low, uint8_t vocal_high) {
  // Aim for midpoint between neighbors
  int midpoint = (prev_pitch + next_pitch) / 2;
  // Snap to nearest chord tone
  int fixed_pitch = nearestChordTonePitch(midpoint, chord_degree);
  // Ensure within vocal range
  return std::clamp(fixed_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
}

bool doesFixImproveConnectivity(int prev_pitch, int curr_pitch, int next_pitch, int fixed_pitch) {
  int interval_before = std::abs(curr_pitch - prev_pitch);
  int interval_after = std::abs(next_pitch - curr_pitch);

  int new_interval_before = std::abs(fixed_pitch - prev_pitch);
  int new_interval_after = std::abs(next_pitch - fixed_pitch);

  // Accept if: any interval improves, OR both intervals stay same/better and note changes
  bool improves = (new_interval_before < interval_before || new_interval_after < interval_after);
  bool no_worse = (new_interval_before <= interval_before && new_interval_after <= interval_after);

  return improves || (no_worse && fixed_pitch != curr_pitch);
}

void resolveIsolatedNotes(std::vector<NoteEvent>& notes, const IHarmonyContext& harmony,
                          uint8_t vocal_low, uint8_t vocal_high) {
  // Need at least 3 notes to have a middle note with neighbors
  if (notes.size() < 3) {
    return;
  }

  for (size_t i = 1; i + 1 < notes.size(); ++i) {
    int prev_pitch = notes[i - 1].note;
    int curr_pitch = notes[i].note;
    int next_pitch = notes[i + 1].note;

    if (!isIsolatedNote(prev_pitch, curr_pitch, next_pitch)) {
      continue;
    }

    // Find a better connecting pitch
    int8_t chord_degree = harmony.getChordDegreeAt(notes[i].start_tick);
    int fixed_pitch = findConnectingPitch(prev_pitch, next_pitch, chord_degree,
                                           vocal_low, vocal_high);

    // Apply fix only if it improves connectivity and doesn't introduce collision
    if (doesFixImproveConnectivity(prev_pitch, curr_pitch, next_pitch, fixed_pitch) &&
        harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(fixed_pitch), notes[i].start_tick,
                                            notes[i].duration, TrackRole::Vocal)) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
      uint8_t old_pitch = notes[i].note;
#endif
      notes[i].note = static_cast<uint8_t>(fixed_pitch);
#ifdef MIDISKETCH_NOTE_PROVENANCE
      if (old_pitch != notes[i].note) {
        notes[i].prov_original_pitch = old_pitch;
        notes[i].addTransformStep(TransformStepType::ChordToneSnap, old_pitch,
                                  notes[i].note, 0, 0);
      }
#endif
    }
  }
}

}  // namespace melody
}  // namespace midisketch
