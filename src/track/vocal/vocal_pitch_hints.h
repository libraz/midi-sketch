/**
 * @file vocal_pitch_hints.h
 * @brief Pitch hint construction, onset velocity, phrase-end detection, and
 *        phrase-end resolution post-processing for vocal generation.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_VOCAL_PITCH_HINTS_H_
#define MIDISKETCH_TRACK_VOCAL_VOCAL_PITCH_HINTS_H_

#include <cstdint>
#include <set>
#include <vector>

#include "core/basic_types.h"
#include "core/melody_types.h"
#include "core/note_creator.h"
#include "core/timing_constants.h"
#include "track/vocal/melody_designer.h"

namespace midisketch {

struct PhrasePlan;

/// @brief Onset contour info for phrase-aware pitch selection.
struct OnsetContourInfo {
  ContourType contour = ContourType::Ascending;
  float phrase_position = 0.0f;  // 0.0-1.0
};

/// @brief Mutable melodic state tracked across onsets in the main loop.
struct LockedRhythmMelodicState {
  uint8_t prev_pitch;
  int direction_inertia = 0;
  int same_pitch_streak = 0;
  int onsets_since_long = 100;
};

/// @brief Apply phrase contour direction to pitch selection hints.
void applyContourToHints(const OnsetContourInfo& ci, PitchSelectionHints& hints);

/// @brief Build PitchSelectionHints from current melodic state and contour info.
PitchSelectionHints buildPitchHints(const LockedRhythmMelodicState& state,
                                    Tick hint_duration,
                                    const MelodyDesigner::SectionContext& ctx,
                                    const PhrasePlan* phrase_plan, size_t onset_idx,
                                    const std::vector<OnsetContourInfo>& onset_contours);

/// @brief Compute velocity for an onset using motif accent pattern or beat position.
uint8_t computeOnsetVelocity(float beat, const MelodyDesigner::SectionContext& ctx);

/// @brief Determine if this onset is a phrase-end note using range-based boundary check.
bool isPhraseEndOnset(size_t onset_idx, size_t next_active,
                      const std::vector<float>& onsets,
                      const std::set<float>& boundary_set,
                      uint8_t section_beats, bool is_last_note);

/// @brief Post-process notes to ensure phrase-end resolution by merging short tail notes.
/// Scans for phrase boundaries (gap >= TICK_EIGHTH). If the tail (last 2 beats) lacks
/// a sustained note (>= 1 beat), merges 2-3 adjacent notes into one longer note.
void postProcessPhraseEndResolution(std::vector<NoteEvent>& notes, float gate_ratio);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_VOCAL_PITCH_HINTS_H_
