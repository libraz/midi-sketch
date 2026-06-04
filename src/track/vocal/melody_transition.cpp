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

  // Maximum allowed interval (major 6th = 9 semitones)
  // kMaxMelodicInterval from pitch_utils.h

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
    int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);
    int new_pitch = nearestChordTonePitch(note.note + pitch_shift, chord_degree);

    // Constrain to vocal range
    new_pitch =
        std::clamp(new_pitch, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high));

    // Ensure interval constraint with previous note
    if (prev_pitch >= 0) {
      int max_interval = getMaxMelodicIntervalForSection(ctx.section_type);
      int interval = std::abs(new_pitch - prev_pitch);
      if (interval > max_interval) {
        // Reduce the shift to stay within interval constraint
        if (new_pitch > prev_pitch) {
          new_pitch = prev_pitch + max_interval;
        } else {
          new_pitch = prev_pitch - max_interval;
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

  // 3. Insert leading tone if requested (skip if it would create large interval)
  if (trans.use_leading_tone && !notes.empty()) {
    int last_pitch = notes.back().note;
    int leading_pitch = ctx.tessitura.center - 1;
    if (std::abs(leading_pitch - last_pitch) <= kMaxMelodicInterval) {
      insertLeadingTone(notes, ctx, harmony);
    }
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

  // Use chord tone at the pickup position for harmonically correct approach note.
  // The old approach (tessitura.center - 1) produced chromatic pitches (e.g. F# in C major)
  // that are non-diatonic. Chord tones are always diatonic and create natural pickup motion.
  int8_t degree = harmony.getChordDegreeAt(leading_tone_start);
  ChordToneHelper helper(degree);
  int leading_pitch = helper.nearestChordTone(static_cast<uint8_t>(
      std::clamp(static_cast<int>(ctx.tessitura.center) - 1, static_cast<int>(ctx.vocal_low),
                 static_cast<int>(ctx.vocal_high))));

  // Ensure it's within range
  leading_pitch =
      std::clamp(leading_pitch, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high));

  // Check interval constraint with last note
  int interval = std::abs(leading_pitch - static_cast<int>(last_note.note));
  if (interval > kMaxMelodicInterval) {
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
