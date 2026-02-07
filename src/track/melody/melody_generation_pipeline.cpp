/**
 * @file melody_generation_pipeline.cpp
 * @brief Implementation of unified melody generation pipeline.
 */

#include "track/melody/melody_generation_pipeline.h"

#include <algorithm>
#include <cmath>

#include "core/i_harmony_context.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "track/melody/constraint_pipeline.h"
#include "track/melody/contour_direction.h"
#include "track/melody/isolated_note_resolver.h"
#include "track/melody/leap_resolution.h"
#include "track/melody/melody_utils.h"
#include "track/melody/note_constraints.h"
#include "track/melody/pitch_constraints.h"
#include "track/melody/pitch_resolver.h"
#include "track/melody/rhythm_generator.h"

namespace midisketch {
namespace melody {

// ============================================================================
// Rhythm Generation
// ============================================================================

std::vector<RhythmNote> MelodyGenerationPipeline::generateRhythm(
    const MelodyTemplate& tmpl, const RhythmGenerationContext& ctx, std::mt19937& rng) const {
  return melody::generatePhraseRhythm(tmpl, ctx.phrase_beats, ctx.density_modifier,
                                      ctx.thirtysecond_ratio, rng, ctx.paradigm,
                                      ctx.syncopation_weight, ctx.section_type);
}

// ============================================================================
// Pitch Resolution
// ============================================================================

PitchChoice MelodyGenerationPipeline::selectPitchChoice(
    const MelodyTemplate& tmpl, float phrase_pos, const PitchGenerationContext& ctx,
    std::optional<ContourType> forced_contour, std::mt19937& rng) const {
  return melody::selectPitchChoice(tmpl, phrase_pos, ctx.target_pitch >= 0, ctx.section_type, rng,
                                   ctx.note_eighths, forced_contour);
}

PitchChoice MelodyGenerationPipeline::applyDirectionInertia(PitchChoice choice,
                                                            const PhraseGenerationState& state,
                                                            const MelodyTemplate& tmpl,
                                                            std::mt19937& rng) const {
  return melody::applyDirectionInertia(choice, state.direction_inertia, tmpl, rng);
}

int MelodyGenerationPipeline::applyPitchChoice(PitchChoice choice,
                                               const PitchGenerationContext& ctx) const {
  return melody::applyPitchChoice(choice, ctx.current_pitch, ctx.target_pitch, ctx.chord_degree,
                                  ctx.key_offset, ctx.vocal_low, ctx.vocal_high, ctx.attitude,
                                  ctx.disable_singability, ctx.note_eighths);
}

int MelodyGenerationPipeline::calculateTargetPitch(const MelodyTemplate& tmpl,
                                                   const PitchGenerationContext& ctx,
                                                   Tick section_start,
                                                   const IHarmonyContext& harmony) const {
  return melody::calculateTargetPitch(tmpl, ctx.tessitura.center, tmpl.tessitura_range, ctx.vocal_low,
                                      ctx.vocal_high, section_start, harmony);
}

// ============================================================================
// Constraint Application
// ============================================================================

int MelodyGenerationPipeline::applyAllPitchConstraints(
    int pitch, Tick note_start, const PitchGenerationContext& ctx, PhraseGenerationState& state,
    [[maybe_unused]] const IHarmonyContext& harmony, std::mt19937& rng) const {
  int new_pitch = pitch;

  // 1. Consecutive same note reduction with J-POP style probability curve
  ConsecutiveSameNoteTracker tracker;
  tracker.count = state.consecutive_same_count;

  int max_interval = getEffectiveMaxInterval(ctx.section_type, ctx.max_leap_semitones);
  applyConsecutiveSameNoteConstraint(new_pitch, tracker, state.prev_pitch, ctx.chord_degree,
                                     ctx.vocal_low, ctx.vocal_high, max_interval, rng);
  state.consecutive_same_count = tracker.count;

  // 2. Maximum interval constraint
  if (state.prev_pitch >= 0) {
    int interval = std::abs(new_pitch - state.prev_pitch);
    if (interval > max_interval) {
      new_pitch = nearestChordToneWithinInterval(new_pitch, state.prev_pitch, ctx.chord_degree,
                                                 max_interval, ctx.vocal_low, ctx.vocal_high,
                                                 &ctx.tessitura);
    }
  }

  // 3. Leap preparation constraint (limit leaps after short notes)
  if (state.prev_pitch >= 0) {
    new_pitch = applyLeapPreparationConstraint(new_pitch, state.prev_pitch, state.prev_note_duration,
                                               ctx.chord_degree, ctx.vocal_low, ctx.vocal_high,
                                               &ctx.tessitura);
  }

  // 4. Leap encouragement (encourage movement after long notes)
  if (state.prev_pitch >= 0) {
    new_pitch = encourageLeapAfterLongNote(new_pitch, state.prev_pitch, state.prev_note_duration,
                                           ctx.chord_degree, ctx.vocal_low, ctx.vocal_high, rng);
  }

  // 5. Avoid note constraint (no tritone/minor 2nd with chord tones)
  new_pitch =
      enforceAvoidNoteConstraint(new_pitch, ctx.chord_degree, ctx.vocal_low, ctx.vocal_high);

  // 6. Downbeat chord-tone constraint
  new_pitch = enforceDownbeatChordTone(new_pitch, note_start, ctx.chord_degree, state.prev_pitch,
                                       ctx.vocal_low, ctx.vocal_high, ctx.disable_singability);

  // 7. Final max interval re-check after all adjustments
  if (state.prev_pitch >= 0) {
    int final_interval = std::abs(new_pitch - state.prev_pitch);
    if (final_interval > max_interval) {
      new_pitch = nearestChordToneWithinInterval(new_pitch, state.prev_pitch, ctx.chord_degree,
                                                 max_interval, ctx.vocal_low, ctx.vocal_high,
                                                 &ctx.tessitura);
    }
  }

  return new_pitch;
}

// ============================================================================
// Duration/Gate Processing
// ============================================================================

Tick MelodyGenerationPipeline::applyDurationConstraints(Tick note_start, Tick duration,
                                                        const IHarmonyContext& harmony,
                                                        Tick phrase_end, bool is_phrase_end,
                                                        bool is_phrase_start,
                                                        int interval_from_prev,
                                                        uint8_t pitch) const {
  GateContext gate_ctx;
  gate_ctx.is_phrase_end = is_phrase_end;
  gate_ctx.is_phrase_start = is_phrase_start;
  gate_ctx.note_duration = duration;
  gate_ctx.interval_from_prev = interval_from_prev;

  return applyAllDurationConstraints(note_start, duration, harmony, phrase_end, gate_ctx, pitch);
}

// ============================================================================
// Utility Functions
// ============================================================================

Tick MelodyGenerationPipeline::getBaseBreathDuration(SectionType section_type, Mood mood) const {
  return melody::getBaseBreathDuration(section_type, mood);
}

Tick MelodyGenerationPipeline::getBreathDuration(SectionType section_type, Mood mood,
                                                 float phrase_density, uint8_t phrase_high,
                                                 [[maybe_unused]] const void* breath_ctx,
                                                 VocalStylePreset vocal_style) const {
  return melody::getBreathDuration(section_type, mood, phrase_density, phrase_high, nullptr,
                                   vocal_style);
}

Tick MelodyGenerationPipeline::getRhythmUnit(RhythmGrid grid, bool is_eighth) const {
  return melody::getRhythmUnit(grid, is_eighth);
}

int MelodyGenerationPipeline::getEffectiveMaxInterval(SectionType section_type,
                                                      uint8_t ctx_max_leap) const {
  return melody::getEffectiveMaxInterval(section_type, ctx_max_leap);
}

float MelodyGenerationPipeline::getMotifWeightForSection(SectionType section_type) const {
  return melody::getMotifWeightForSection(section_type);
}

// ============================================================================
// Post-processing
// ============================================================================

void MelodyGenerationPipeline::resolveIsolatedNotes(std::vector<NoteEvent>& notes,
                                                    const IHarmonyContext& harmony,
                                                    uint8_t vocal_low, uint8_t vocal_high) const {
  melody::resolveIsolatedNotes(notes, harmony, vocal_low, vocal_high);
}

}  // namespace melody
}  // namespace midisketch
