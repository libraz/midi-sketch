/**
 * @file phrase_note_generator.cpp
 * @brief Implementation of note generation logic for melody phrases.
 */

#include "track/melody/phrase_note_generator.h"

#include <algorithm>
#include <cmath>

#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "core/velocity_helper.h"
#include "track/melody/constraint_pipeline.h"
#include "track/melody/leap_resolution.h"
#include "track/melody/melody_utils.h"
#include "track/melody/note_constraints.h"
#include "track/melody/pitch_constraints.h"

namespace midisketch {
namespace melody {

int selectInitialPhrasePitch(int prev_pitch, int8_t chord_degree, SectionType section_type,
                              const TessituraRange& tessitura, uint8_t vocal_low, uint8_t vocal_high) {
  if (prev_pitch < 0) {
    // No previous pitch - select based on section type
    if (section_type == SectionType::Chorus || section_type == SectionType::B) {
      // Use anchor tone for memorable melodic anchoring
      return getAnchorTonePitch(chord_degree, tessitura.center, vocal_low, vocal_high);
    } else {
      // Start near tessitura center for other sections
      int pitch = tessitura.center;
      pitch = nearestChordTonePitch(pitch, chord_degree);
      return std::clamp(pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
    }
  }

  // Have previous pitch - ensure smooth connection
  int chord_tone = nearestChordTonePitch(prev_pitch, chord_degree);
  int interval_to_chord = std::abs(chord_tone - prev_pitch);

  // If prev_pitch is far from current chord context, use chord tone instead
  constexpr int kMaxPhraseConnectionInterval = 7;  // Perfect 5th
  if (interval_to_chord <= kMaxPhraseConnectionInterval) {
    // Chord tone is reachable - prefer it for harmonic grounding
    return chord_tone;
  }

  // Chord tone is too far - find intermediate step
  int direction = (chord_tone > prev_pitch) ? 1 : -1;
  int stepped_pitch = prev_pitch + direction * kMaxPhraseConnectionInterval;
  int result = nearestChordTonePitch(stepped_pitch, chord_degree);
  return std::clamp(result, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
}

int applyMotifFragment(int current_pitch, size_t note_index,
                       const std::vector<int8_t>& motif_intervals,
                       int8_t chord_degree, uint8_t vocal_low, uint8_t vocal_high) {
  // Motif fragments apply to notes 1 through N (not the first note)
  if (note_index == 0 || note_index > motif_intervals.size()) {
    return -1;  // Not applicable
  }

  // Get the interval for this note (note_index - 1 because first note is base)
  int8_t interval = motif_intervals[note_index - 1];
  // Calculate target pitch from previous pitch plus interval (in semitones)
  int target = current_pitch + interval;
  // Snap to nearest chord tone for harmonic safety
  target = nearestChordTonePitch(target, chord_degree);
  return std::clamp(target, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
}

int applyAllPitchConstraints(int pitch, const NoteGenerationContext& ctx,
                              const PhraseNoteParams& params,
                              LeapResolutionState& leap_state,
                              const std::vector<int>& chord_tones,
                              std::mt19937& rng) {
  int new_pitch = pitch;

  // 1. Maximum interval constraint
  int max_interval = getEffectiveMaxInterval(params.section_type, params.max_leap_semitones);
  int interval = std::abs(new_pitch - ctx.current_pitch);
  if (interval > max_interval) {
    new_pitch = nearestChordToneWithinInterval(new_pitch, ctx.current_pitch, ctx.chord_degree,
                                                max_interval, params.vocal_low, params.vocal_high,
                                                params.tessitura);
  }

  // 2. Multi-note leap resolution tracking
  int actual_interval = new_pitch - ctx.current_pitch;  // Signed for direction

  // Check if pending resolution should override the selected pitch
  if (leap_state.shouldApplyStep() && ctx.prev_note_pitch >= 0) {
    float step_probability = params.prefer_stepwise ? 1.0f : 0.80f;
    std::uniform_real_distribution<float> step_dist(0.0f, 1.0f);

    if (step_dist(rng) < step_probability) {
      int best_step = findStepwiseResolutionPitch(ctx.current_pitch, chord_tones,
                                                   leap_state.direction,
                                                   params.vocal_low, params.vocal_high);
      if (best_step >= 0) {
        new_pitch = best_step;
        actual_interval = new_pitch - ctx.current_pitch;
      }
    }
  }

  // Detect new leaps and start resolution tracking
  if (std::abs(actual_interval) >= kLeapThreshold) {
    leap_state.startResolution(actual_interval);
  }

  // 3. Leap preparation constraint (limit leaps after short notes)
  if (ctx.note_index > 0) {
    new_pitch = applyLeapPreparationConstraint(new_pitch, ctx.current_pitch, ctx.prev_duration,
                                                ctx.chord_degree, params.vocal_low,
                                                params.vocal_high, params.tessitura);
  }

  // 4. Leap encouragement after long notes
  if (ctx.note_index > 0) {
    new_pitch = encourageLeapAfterLongNote(new_pitch, ctx.current_pitch, ctx.prev_duration,
                                            ctx.chord_degree, params.vocal_low,
                                            params.vocal_high, rng);
  }

  // 5. Avoid note constraint
  new_pitch = enforceAvoidNoteConstraint(new_pitch, ctx.chord_degree,
                                          params.vocal_low, params.vocal_high);

  // 6. Downbeat chord-tone constraint
  new_pitch = enforceDownbeatChordTone(new_pitch, ctx.note_start, ctx.chord_degree,
                                        ctx.current_pitch, params.vocal_low, params.vocal_high,
                                        params.disable_vowel_constraints);

  // 6b. Guide tone priority: on strong beats, bias toward 3rd/7th
  if (params.guide_tone_rate > 0 && params.vocal_attitude != VocalAttitude::Raw) {
    new_pitch = enforceGuideToneOnDownbeat(new_pitch, ctx.note_start, ctx.chord_degree,
                                            params.vocal_low, params.vocal_high,
                                            params.guide_tone_rate, rng);
  }

  // 7. Leap-after-reversal rule
  if (ctx.note_index > 0 && ctx.prev_note_pitch >= 0) {
    new_pitch = applyLeapReversalRule(new_pitch, ctx.current_pitch, ctx.prev_interval,
                                       chord_tones, params.vocal_low, params.vocal_high,
                                       params.prefer_stepwise, rng,
                                       static_cast<int8_t>(params.section_type),
                                       ctx.phrase_pos);
  }

  // 8. Final interval enforcement (re-check after all adjustments)
  int effective_max_interval = getEffectiveMaxInterval(params.section_type, params.max_leap_semitones);
  int final_interval = std::abs(new_pitch - ctx.current_pitch);
  if (final_interval > effective_max_interval) {
    new_pitch = nearestChordToneWithinInterval(new_pitch, ctx.current_pitch, ctx.chord_degree,
                                                effective_max_interval, params.vocal_low,
                                                params.vocal_high, params.tessitura);
  }

  return new_pitch;
}

Tick calculateNoteDuration(float eighths, RhythmGrid rhythm_grid, float beat, float next_beat) {
  Tick eighth_unit = getRhythmUnit(rhythm_grid, true);
  Tick duration = static_cast<Tick>(eighths * eighth_unit);

  // Cap to gap if next note is closer to prevent overlap
  if (next_beat >= 0) {
    float beat_duration = next_beat - beat;
    Tick gap_duration = static_cast<Tick>(beat_duration * TICKS_PER_BEAT);
    duration = std::min(duration, gap_duration);
  }

  return duration;
}

uint8_t calculateNoteVelocity(bool strong, bool is_phrase_end, size_t note_index,
                              size_t total_notes, ContourType contour) {
  constexpr uint8_t DEFAULT_VELOCITY = 100;
  uint8_t velocity = DEFAULT_VELOCITY;

  if (strong) {
    velocity = std::min(127, velocity + 10);
  }
  if (is_phrase_end) {
    velocity = static_cast<uint8_t>(velocity * 0.85f);
  }

  // Apply phrase-internal velocity curve for natural crescendo/decrescendo
  float phrase_curve = getPhraseNoteVelocityCurve(
      static_cast<int>(note_index), static_cast<int>(total_notes), contour);
  return vel::clamp(static_cast<int>(velocity * phrase_curve));
}

int applyPhraseEndResolution(int pitch, int8_t chord_degree, SectionType section_type,
                              float phrase_end_resolution, uint8_t vocal_low, uint8_t vocal_high,
                              std::mt19937& rng) {
  if (phrase_end_resolution <= 0.0f) {
    return pitch;
  }

  std::uniform_real_distribution<float> resolve_dist(0.0f, 1.0f);
  if (resolve_dist(rng) >= phrase_end_resolution) {
    return pitch;
  }

  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);
  int pitch_pc = getPitchClass(static_cast<uint8_t>(pitch));
  bool is_chord_tone = false;
  for (int ct : chord_tones) {
    if (pitch_pc == ct) {
      is_chord_tone = true;
      break;
    }
  }

  int new_pitch = pitch;
  if (!is_chord_tone) {
    // Snap to nearest chord tone
    new_pitch = nearestChordTonePitch(pitch, chord_degree);
    new_pitch = std::clamp(new_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
  }

  // For Chorus sections, prefer root note resolution for strong cadence
  if (section_type == SectionType::Chorus && resolve_dist(rng) < 0.6f) {
    int root_pc = chord_tones.empty() ? 0 : chord_tones[0];
    int octave = new_pitch / 12;
    int root_pitch = octave * 12 + root_pc;
    if (root_pitch < static_cast<int>(vocal_low)) root_pitch += 12;
    if (root_pitch > static_cast<int>(vocal_high)) root_pitch -= 12;
    if (root_pitch >= static_cast<int>(vocal_low) &&
        root_pitch <= static_cast<int>(vocal_high)) {
      new_pitch = root_pitch;
    }
  }

  return new_pitch;
}

int applyFinalPitchSafety(int pitch, Tick note_start, Tick note_duration, int key_offset,
                          uint8_t vocal_low, uint8_t vocal_high, const IHarmonyContext& harmony,
                          int prev_pitch) {
  // Snap to nearest scale tone (prevents chromatic notes)
  int safe_pitch = snapToNearestScaleTone(pitch, key_offset);
  safe_pitch = std::clamp(safe_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));

  // Apply pitch safety check to avoid collisions with other tracks
  auto candidates = getSafePitchCandidates(harmony, static_cast<uint8_t>(safe_pitch), note_start,
                                            note_duration, TrackRole::Vocal, vocal_low, vocal_high);
  if (candidates.empty()) {
    return -1;  // No safe pitch available
  }

  // Select best candidate considering melodic context
  PitchSelectionHints hints;
  hints.prev_pitch = static_cast<int8_t>(prev_pitch);
  hints.note_duration = note_duration;
  hints.tessitura_center = (vocal_low + vocal_high) / 2;
  return selectBestCandidate(candidates, static_cast<uint8_t>(safe_pitch), hints);
}

}  // namespace melody
}  // namespace midisketch
