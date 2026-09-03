/**
 * @file melody_transition.cpp
 * @brief MelodyDesigner::applyTransitionApproach() and insertLeadingTone() implementations.
 *
 * Extracted from melody_designer.cpp for modularity.
 */

#include <algorithm>
#include <cmath>

#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "core/velocity_helper.h"
#include "track/melody/melody_utils.h"
#include "track/vocal/melody_designer.h"

namespace midisketch {

void MelodyDesigner::applyTransitionApproach(std::vector<NoteEvent>& notes,
                                             const SectionContext& ctx,
                                             const IHarmonyContext& harmony) {
  if (!ctx.transition_to_next || notes.empty()) return;

  const auto& trans = *ctx.transition_to_next;
  Tick approach_start = ctx.section_end - trans.approach_beats * TICKS_PER_BEAT;

  int effective_max_interval =
      melody::getEffectiveMaxInterval(ctx.section_type, ctx.max_leap_semitones);
  int prev_pitch = -1;

  for (auto& note : notes) {
    if (note.start_tick < approach_start) {
      prev_pitch = note.note;
      continue;
    }

    uint8_t old_pitch = note.note;

    // 1. Apply pitch tendency (creating "run-up" to next section)
    float progress = static_cast<float>(note.start_tick - approach_start) /
                     static_cast<float>(ctx.section_end - approach_start);
    int8_t pitch_shift = static_cast<int8_t>(trans.pitch_tendency * progress);

    // Move toward chord tone while shifting
    int new_pitch = melody::nearestPitchInSet(melody::vocalSnapTonesAt(harmony, note.start_tick),
                                              note.note + pitch_shift, 0, 127);

    // Constrain to vocal range
    new_pitch =
        std::clamp(new_pitch, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high));

    // Ensure interval constraint with previous note
    if (prev_pitch >= 0) {
      int interval = std::abs(new_pitch - prev_pitch);
      if (interval > effective_max_interval) {
        // Reduce the shift to stay within interval constraint
        if (new_pitch > prev_pitch) {
          new_pitch = prev_pitch + effective_max_interval;
        } else {
          new_pitch = prev_pitch - effective_max_interval;
        }
        // Snap to scale to prevent chromatic notes
        new_pitch = snapToNearestScaleTone(new_pitch, ctx.key_offset);
        // Re-constrain to vocal range
        new_pitch = std::clamp(new_pitch, static_cast<int>(ctx.vocal_low),
                               static_cast<int>(ctx.vocal_high));
      }
    }

    // Re-verify collision safety after transition approach pitch modification
    if (static_cast<uint8_t>(new_pitch) != old_pitch &&
        !harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(new_pitch), note.start_tick,
                                            note.duration, TrackRole::Vocal)) {
      new_pitch = old_pitch;  // Keep original if transition introduces collision
    }
    note.note = static_cast<uint8_t>(new_pitch);
#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (old_pitch != note.note) {
      note.prov_original_pitch = old_pitch;
      note.addTransformStep(TransformStepType::ScaleSnap, old_pitch, note.note, 0, 0);
    }
#endif
    prev_pitch = new_pitch;

    // 2. Apply velocity gradient (crescendo/decrescendo)
    float vel_factor = 1.0f + (trans.velocity_growth - 1.0f) * progress;
    note.velocity = vel::scale(note.velocity, vel_factor);
  }

  // 3. Insert leading tone if requested. insertLeadingTone() applies the same
  // interval bound to the pitch it actually chooses, so screening here against
  // a differently-derived candidate would reject usable pickups.
  if (trans.use_leading_tone && !notes.empty()) {
    insertLeadingTone(notes, ctx, harmony);
  }
}

void MelodyDesigner::insertLeadingTone(std::vector<NoteEvent>& notes, const SectionContext& ctx,
                                       const IHarmonyContext& harmony) {
  if (notes.empty()) return;

  // Find the last note
  auto& last_note = notes.back();

  // Insert a short pickup note just before section end
  Tick last_note_end = last_note.start_tick + last_note.duration;
  Tick leading_tone_start = ctx.section_end - TICKS_PER_BEAT / 4;  // 16th note before end

  // Use a chord tone at the pickup position so the approach note is
  // harmonically correct and diatonic, aimed just under the tessitura centre.
  int leading_pitch = melody::nearestPitchInSet(
      melody::vocalSnapTonesAt(harmony, leading_tone_start),
      std::clamp(static_cast<int>(ctx.tessitura.center) - 1, static_cast<int>(ctx.vocal_low),
                 static_cast<int>(ctx.vocal_high)),
      0, 127);

  // Ensure it's within range
  leading_pitch =
      std::clamp(leading_pitch, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high));

  // Check interval constraint with last note
  int interval = std::abs(leading_pitch - static_cast<int>(last_note.note));
  int max_interval = melody::getEffectiveMaxInterval(ctx.section_type, ctx.max_leap_semitones);
  if (interval > max_interval) {
    return;
  }

  // Skip if gap is too large - leading tone needs melodic context
  // An isolated note after a long gap sounds unnatural
  constexpr Tick MAX_GAP = TICKS_PER_BEAT / 2;  // Half beat (8th note gap max)
  if (leading_tone_start > last_note_end && leading_tone_start - last_note_end > MAX_GAP) {
    return;
  }

  if (last_note_end <= leading_tone_start) {
    // Check pitch safety before adding leading tone
    Tick leading_duration = TICKS_PER_BEAT / 4;
    if (!harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(leading_pitch), leading_tone_start,
                                            leading_duration, TrackRole::Vocal)) {
      return;  // Skip leading tone if it would cause dissonance
    }

    uint8_t velocity = static_cast<uint8_t>(
        std::min(127, static_cast<int>(last_note.velocity) + 10));  // Slightly louder

    NoteEvent leading_note = createNoteWithoutHarmony(
        leading_tone_start, leading_duration, static_cast<uint8_t>(leading_pitch), velocity);
#ifdef MIDISKETCH_NOTE_PROVENANCE
    leading_note.prov_source = static_cast<uint8_t>(NoteSource::MelodyPhrase);
    leading_note.prov_chord_degree = harmony.getChordDegreeAt(leading_tone_start);
    leading_note.prov_lookup_tick = leading_tone_start;
    leading_note.prov_original_pitch = static_cast<uint8_t>(leading_pitch);
#endif
    notes.push_back(leading_note);
  }
}

}  // namespace midisketch
