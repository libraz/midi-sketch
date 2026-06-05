/**
 * @file melody_designer.cpp
 * @brief Implementation of MelodyDesigner track generation.
 */

#include "track/vocal/melody_designer.h"

#include <algorithm>
#include <cmath>

#include "core/chord_utils.h"
#include "core/harmonic_rhythm.h"
#include "core/hook_utils.h"
#include "core/i_harmony_context.h"
#include "core/melody_embellishment.h"
#include "core/motif_transform.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/phrase_patterns.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "core/velocity_helper.h"
#include "core/vocal_style_profile.h"
#include "track/melody/constraint_pipeline.h"
#include "track/melody/contour_direction.h"
#include "track/melody/hook_rhythm_patterns.h"
#include "track/melody/isolated_note_resolver.h"
#include "track/melody/leap_resolution.h"
#include "track/melody/melody_utils.h"
#include "track/melody/motif_support.h"
#include "track/melody/note_constraints.h"
#include "track/melody/pitch_constraints.h"
#include "track/melody/pitch_resolver.h"
#include "track/melody/rhythm_generator.h"
#include "track/vocal/phrase_planner.h"
#include "track/vocal/vocal_helpers.h"

namespace midisketch {

// Import melody submodule symbols
using melody::getBaseBreathDuration;
using melody::getBreathDuration;
using melody::getDirectionBiasForContour;
using melody::getMotifWeightForSection;
using melody::getRhythmUnit;
using melody::LeapResolutionState;
using melody::LockedRhythmContext;
// Free functions (formerly wrapped by MelodyDesigner methods)
using melody::applyDirectionInertia;
using melody::applyPitchChoice;
using melody::calculateTargetPitch;
using melody::evaluateWithGlobalMotif;
using melody::extractGlobalMotif;
using melody::generatePhraseRhythm;
using melody::getEffectivePlateauRatio;
using melody::getMaxStepInVowelSection;
using melody::getStabilizeStep;
using melody::isInSameVowelSection;
using melody::selectPitchChoice;
using melody::selectPitchForLockedRhythmEnhanced;
using melody::shouldLeap;

namespace {

// Default velocity for melody notes
constexpr uint8_t DEFAULT_VELOCITY = 100;

// Local wrapper - uses submodule function
int getEffectiveMaxInterval(SectionType section_type, uint8_t ctx_max_leap) {
  return melody::getEffectiveMaxInterval(section_type, ctx_max_leap);
}

// Import additional submodule functions
using melody::applySequentialTransposition;
using melody::calculatePhraseCount;
using melody::getAnchorTonePitch;
using melody::getBassRootPitchClass;
using melody::getHookRhythmPatternCount;
using melody::getHookRhythmPatterns;
using melody::getNearestSafeChordTone;
using melody::HookRhythmPattern;
using melody::isAvoidNoteWithChord;
using melody::isAvoidNoteWithRoot;
using melody::selectHookRhythmPatternIndex;

/// Calculate effective subdivision ratio considering BPM and mora mode.
///
/// BPM scaling:
///   - Low BPM (<=80): long notes are natural → ×0.5
///   - Mid BPM (80-120): linear interpolation → 0.5→1.0
///   - High BPM (120-160): remaining long notes stand out → 1.0→1.3
///   - Very high (>160): cap → ×1.3
///
/// Mora scaling:
///   - MoraTimed active: rhythm generator already reflects mora density → ×0.5
///   - Standard (stress-timed): stress timing leaves long notes → ×1.0
float calcEffectiveSubRatio(float base_ratio, uint16_t bpm, bool is_mora_timed) {
  float bpm_factor;
  if (bpm <= 80) {
    bpm_factor = 0.5f;
  } else if (bpm <= 120) {
    bpm_factor = 0.5f + static_cast<float>(bpm - 80) * 0.0125f;  // 0.5→1.0
  } else if (bpm <= 160) {
    bpm_factor = 1.0f + static_cast<float>(bpm - 120) * 0.0075f;  // 1.0→1.3
  } else {
    bpm_factor = 1.3f;
  }

  float mora_factor = is_mora_timed ? 0.5f : 1.0f;

  return std::min(0.5f, base_ratio * bpm_factor * mora_factor);
}

/// Syllabic subdivision: split long notes into repeated same-pitch notes.
/// @param notes      Input melody notes
/// @param ratio      Probability of subdividing each eligible note (0.0-0.5)
/// @param bpm        Beats per minute (for singability check)
/// @param min_ms     Minimum singable note duration in milliseconds
/// @param rng        Random number generator
std::vector<NoteEvent> subdivideSyllabic(const std::vector<NoteEvent>& notes, float ratio,
                                         uint16_t bpm, float min_ms, std::mt19937& rng) {
  if (notes.empty() || ratio <= 0.0f) {
    return notes;
  }

  // Calculate minimum singable duration in ticks
  Tick min_ticks = static_cast<Tick>(min_ms / 1000.0f * (bpm / 60.0f) * TICKS_PER_BEAT);
  if (min_ticks < TICK_SIXTEENTH) {
    min_ticks = TICK_SIXTEENTH;
  }

  std::vector<NoteEvent> result;
  result.reserve(notes.size() * 2);  // Estimate

  for (size_t i = 0; i < notes.size(); ++i) {
    const auto& note = notes[i];

    // Skip short notes (< quarter note)
    if (note.duration < TICK_QUARTER) {
      result.push_back(note);
      continue;
    }

    // Skip last note in phrase (phrase ending should sustain)
    if (i + 1 >= notes.size()) {
      result.push_back(note);
      continue;
    }

    // Skip if there's a gap before next note (rest before = sustain)
    Tick note_end = note.start_tick + note.duration;
    if (note_end < notes[i + 1].start_tick) {
      result.push_back(note);
      continue;
    }

    // Probability check
    if (!rng_util::rollProbability(rng, ratio)) {
      result.push_back(note);
      continue;
    }

    // Determine split count
    int split_count = 2;
    if (note.duration >= min_ticks * 4) {
      // 30% chance of 4-split for long notes
      split_count = rng_util::rollProbability(rng, 0.3f) ? 4 : 2;
    } else if (note.duration < min_ticks * 2) {
      // Too short to split
      result.push_back(note);
      continue;
    }

    // Calculate split duration, quantized to 16th note grid
    Tick raw_dur = note.duration / split_count;
    Tick split_dur = (raw_dur / TICK_SIXTEENTH) * TICK_SIXTEENTH;
    if (split_dur < min_ticks) {
      // Fallback: try 2-split if 4-split was too small
      if (split_count == 4) {
        split_count = 2;
        raw_dur = note.duration / 2;
        split_dur = (raw_dur / TICK_SIXTEENTH) * TICK_SIXTEENTH;
      }
      if (split_dur < min_ticks) {
        result.push_back(note);
        continue;
      }
    }

    // Generate subdivided notes
    std::uniform_int_distribution<int> vel_dist(-4, 4);
    Tick current_tick = note.start_tick;
    for (int s = 0; s < split_count; ++s) {
      NoteEvent sub_note = note;
      sub_note.start_tick = current_tick;
      // Last segment gets remaining duration
      if (s == split_count - 1) {
        sub_note.duration = note.start_tick + note.duration - current_tick;
      } else {
        sub_note.duration = split_dur;
      }
      // Velocity micro-variation
      int vel_delta = vel_dist(rng);
      sub_note.velocity =
          static_cast<uint8_t>(std::clamp(static_cast<int>(note.velocity) + vel_delta, 1, 127));
#ifdef MIDISKETCH_NOTE_PROVENANCE
      sub_note.prov_source = static_cast<uint8_t>(NoteSource::SyllabicSub);
#endif
      result.push_back(sub_note);
      current_tick += split_dur;
    }
  }

  return result;
}

/// @brief Apply cadence constraint based on phrase pair role.
/// Antecedent phrases should end on non-root (3rd/5th) for tension.
/// Consequent phrases should resolve to root for resolution.
void applyPhrasePairCadence(std::vector<NoteEvent>& notes, PhrasePairRole pair_role,
                            const IHarmonyContext& harmony, uint8_t vocal_low, uint8_t vocal_high) {
  if (notes.empty() || pair_role == PhrasePairRole::Independent) {
    return;
  }

  NoteEvent& last_note = notes.back();
  int8_t chord_degree = harmony.getChordDegreeAt(last_note.start_tick);
  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);
  int pitch_class = getPitchClass(last_note.note);

  // Root pitch class for the current chord degree
  int root_pc = chord_tones.empty() ? 0 : chord_tones[0];
  bool is_root = (pitch_class == root_pc);

  if (pair_role == PhrasePairRole::Antecedent && is_root && chord_tones.size() >= 2) {
    // Antecedent ends on root -> move to nearest 3rd or 5th
    int target_pc = chord_tones[1];  // 3rd is usually chord_tones[1]
    int current = static_cast<int>(last_note.note);
    // Find nearest note with target pitch class within vocal range
    for (int offset = 0; offset <= 6; ++offset) {
      int up_pitch = current + offset;
      int down_pitch = current - offset;
      if (up_pitch <= vocal_high && up_pitch >= vocal_low && (up_pitch % 12) == target_pc) {
        auto new_pitch = static_cast<uint8_t>(up_pitch);
        if (harmony.isConsonantWithOtherTracks(new_pitch, last_note.start_tick, last_note.duration,
                                               TrackRole::Vocal)) {
          last_note.note = new_pitch;
          return;
        }
      }
      if (down_pitch >= vocal_low && down_pitch <= vocal_high && (down_pitch % 12) == target_pc) {
        auto new_pitch = static_cast<uint8_t>(down_pitch);
        if (harmony.isConsonantWithOtherTracks(new_pitch, last_note.start_tick, last_note.duration,
                                               TrackRole::Vocal)) {
          last_note.note = new_pitch;
          return;
        }
      }
    }
  } else if (pair_role == PhrasePairRole::Consequent && !is_root) {
    // Consequent not on root -> move to nearest root within vocal range
    int current = static_cast<int>(last_note.note);
    for (int offset = 0; offset <= 6; ++offset) {
      int up_pitch = current + offset;
      int down_pitch = current - offset;
      if (up_pitch <= vocal_high && up_pitch >= vocal_low && (up_pitch % 12) == root_pc) {
        auto new_pitch = static_cast<uint8_t>(up_pitch);
        if (harmony.isConsonantWithOtherTracks(new_pitch, last_note.start_tick, last_note.duration,
                                               TrackRole::Vocal)) {
          last_note.note = new_pitch;
          return;
        }
      }
      if (down_pitch >= vocal_low && down_pitch <= vocal_high && (down_pitch % 12) == root_pc) {
        auto new_pitch = static_cast<uint8_t>(down_pitch);
        if (harmony.isConsonantWithOtherTracks(new_pitch, last_note.start_tick, last_note.duration,
                                               TrackRole::Vocal)) {
          last_note.note = new_pitch;
          return;
        }
      }
    }
  }
}

}  // namespace

std::vector<NoteEvent> MelodyDesigner::generateSection(const MelodyTemplate& tmpl,
                                                       const SectionContext& ctx,
                                                       const IHarmonyContext& harmony,
                                                       std::mt19937& rng) {
  std::vector<NoteEvent> result;

  // Build phrase plan - replaces manual phrase count, timing, contour, and density calculation
  PhrasePlan plan =
      PhrasePlanner::buildPlan(ctx.section_type, ctx.section_start, ctx.section_end,
                               ctx.section_bars, ctx.mood, ctx.vocal_style, nullptr, ctx.bpm);

  int prev_pitch = -1;
  int direction_inertia = 0;

  for (const auto& planned : plan.phrases) {
    // Calculate actual beats for this phrase
    uint8_t actual_beats = planned.beats;
    if (actual_beats < 2) continue;

    // Set up phrase context from plan
    SectionContext phrase_ctx = ctx;
    phrase_ctx.sub_phrase_index = planned.arc_stage;
    phrase_ctx.forced_contour = planned.contour;
    phrase_ctx.density_modifier *= planned.density_modifier;

    Tick phrase_start = planned.start_tick;
    // TODO(anacrusis): Future pickup note implementation should place notes
    // BEFORE phrase_start (in the previous phrase's tail guard zone),
    // not delay phrase_start forward. See AnticipationRestMode.

    // Generate hook or melody phrase
    // Skip hook for UltraVocaloid (high thirtysecond_ratio) - needs continuous machine-gun passages
    bool use_hook = planned.is_hook_position && tmpl.hook_note_count > 0 &&
                    phrase_ctx.thirtysecond_ratio < 0.8f;

    PhraseResult phrase_result;
    if (use_hook) {
      Tick phrase_end = planned.end_tick - planned.breath_after;
      phrase_result =
          generateHook(tmpl, phrase_start, phrase_end, phrase_ctx, prev_pitch, harmony, rng);
    } else {
      phrase_result = generateMelodyPhrase(tmpl, phrase_start, actual_beats, phrase_ctx, prev_pitch,
                                           direction_inertia, harmony, rng);
    }

    // Apply sequential transposition for B sections (Zekvenz effect)
    // Creates ascending sequence: each phrase rises by 2-4-5 semitones
    applySequentialTransposition(phrase_result.notes, planned.phrase_index, ctx.section_type,
                                 ctx.key_offset, ctx.vocal_low, ctx.vocal_high);

    // Apply cadence constraint based on antecedent-consequent pair role
    applyPhrasePairCadence(phrase_result.notes, planned.pair_role, harmony, ctx.vocal_low,
                           ctx.vocal_high);

    // Append notes to result, enforcing interval constraint between phrases
    constexpr int MAX_PHRASE_INTERVAL = 9;  // Major 6th
    for (const auto& note : phrase_result.notes) {
      NoteEvent adjusted_note = note;
      // Check interval with previous note in result
      if (!result.empty()) {
        int prev_note_pitch = result.back().note;
        int interval = std::abs(static_cast<int>(adjusted_note.note) - prev_note_pitch);
        if (interval > MAX_PHRASE_INTERVAL) {
          // Get chord degree at this note's position for chord tone snapping
          int8_t note_chord_degree = harmony.getChordDegreeAt(adjusted_note.start_tick);
#ifdef MIDISKETCH_NOTE_PROVENANCE
          uint8_t old_pitch = adjusted_note.note;
#endif
          // Use nearestChordToneWithinInterval to stay on chord tones
          uint8_t interval_fixed = static_cast<uint8_t>(nearestChordToneWithinInterval(
              adjusted_note.note, prev_note_pitch, note_chord_degree, MAX_PHRASE_INTERVAL,
              ctx.vocal_low, ctx.vocal_high, &ctx.tessitura));
          // Re-verify collision safety after interval fix
          if (harmony.isConsonantWithOtherTracks(interval_fixed, adjusted_note.start_tick,
                                                 adjusted_note.duration, TrackRole::Vocal)) {
            adjusted_note.note = interval_fixed;
          }
#ifdef MIDISKETCH_NOTE_PROVENANCE
          if (old_pitch != adjusted_note.note) {
            adjusted_note.prov_original_pitch = old_pitch;
            adjusted_note.addTransformStep(TransformStepType::IntervalFix, old_pitch,
                                           adjusted_note.note, 0, 0);
          }
#endif
        }
      }
      // ABSOLUTE CONSTRAINT: Ensure pitch is on scale (prevents chromatic notes)
#ifdef MIDISKETCH_NOTE_PROVENANCE
      uint8_t pre_snap_pitch = adjusted_note.note;
#endif
      int snapped = snapToNearestScaleTone(adjusted_note.note, ctx.key_offset);
      uint8_t snapped_clamped = static_cast<uint8_t>(
          std::clamp(snapped, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high)));
      // Re-verify collision safety after scale snap
      if (snapped_clamped != adjusted_note.note &&
          !harmony.isConsonantWithOtherTracks(snapped_clamped, adjusted_note.start_tick,
                                              adjusted_note.duration, TrackRole::Vocal)) {
        snapped_clamped = adjusted_note.note;  // Keep original if snap introduces collision
      }
      adjusted_note.note = snapped_clamped;
#ifdef MIDISKETCH_NOTE_PROVENANCE
      if (pre_snap_pitch != adjusted_note.note) {
        if (adjusted_note.prov_original_pitch == 0) {
          adjusted_note.prov_original_pitch = pre_snap_pitch;
        }
        adjusted_note.addTransformStep(TransformStepType::ScaleSnap, pre_snap_pitch,
                                       adjusted_note.note, 0, 0);
      }
#endif
      result.push_back(adjusted_note);
    }

    // Insert an inter-phrase breath gap by shortening the last note of this
    // phrase so it ends at singable_end (= end_tick - breath_after). Without
    // this, consecutive phrases produce one continuous, un-breathable stream
    // that runs for an entire section. The breath gap must be at least a
    // half-beat (TICK_EIGHTH) for the phrase boundary to register as a real
    // breath; otherwise it is perceived as continuous singing.
    if (planned.breath_after > 0 && !phrase_result.notes.empty() && !result.empty()) {
      Tick breath = std::max<Tick>(planned.breath_after, TICK_EIGHTH);
      Tick singable_end = (planned.end_tick > breath) ? (planned.end_tick - breath) : 0;
      NoteEvent& last = result.back();
      Tick last_end = last.start_tick + last.duration;
      if (last_end > singable_end && singable_end > last.start_tick + TICK_SIXTEENTH) {
        last.duration = singable_end - last.start_tick;
      }
    }

    // Update state for next phrase
    // Use actual last pitch after transposition and adjustment (not original)
    if (!result.empty()) {
      prev_pitch = result.back().note;
    } else {
      prev_pitch = phrase_result.last_pitch;
    }
    direction_inertia = phrase_result.direction_inertia;
  }

  // Apply melodic embellishment (non-chord tones) if enabled
  if (ctx.enable_embellishment && !result.empty()) {
    EmbellishmentConfig emb_config = MelodicEmbellisher::getConfigForMood(ctx.mood);
    // Scale NCT density for later occurrences (2nd chorus gets 1.2x, 3rd+ gets 1.4x)
    emb_config.adjustForOccurrence(ctx.section_occurrence);
    result = MelodicEmbellisher::embellish(result, emb_config, harmony, ctx.key_offset, rng);
  }

  // Final downbeat chord-tone enforcement with interval constraint
  // Ensures all notes on beat 1 are chord tones, even after embellishment
  // Also enforces kMaxMelodicInterval between consecutive notes
  // Use shared constant from pitch_utils.h
  //
  // APPOGGIATURA EXCEPTION: Preserve non-chord tones on downbeats that resolve
  // down by step (1-2 semitones) to the next note. Appoggiaturas create emotional
  // tension common in expressive pop and ballad vocals.
  int prev_final_pitch = -1;
  for (size_t note_idx = 0; note_idx < result.size(); ++note_idx) {
    auto& note = result[note_idx];
    Tick bar_pos = positionInBar(note.start_tick);
    bool is_downbeat = bar_pos < TICKS_PER_BEAT / 4;
    if (is_downbeat) {
      int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);
      std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);
      int pitch_pc = getPitchClass(note.note);
      bool is_chord_tone = false;
      for (int ct : chord_tones) {
        if (pitch_pc == ct) {
          is_chord_tone = true;
          break;
        }
      }
      if (!is_chord_tone) {
        // Check for valid appoggiatura: resolves down by step (1-2 semitones)
        // to the next note, which should be a chord tone
        // IMPORTANT: The appoggiatura itself must be diatonic (on scale)
        bool is_valid_appoggiatura = false;
        // First verify the potential appoggiatura is diatonic
        if (isScaleTone(pitch_pc, static_cast<uint8_t>(ctx.key_offset)) &&
            note_idx + 1 < result.size()) {
          int next_pitch = result[note_idx + 1].note;
          int resolution_interval = static_cast<int>(note.note) - next_pitch;
          // Must resolve DOWN by half-step or whole-step (1-2 semitones)
          if (resolution_interval >= 1 && resolution_interval <= 2) {
            // Verify next note is a chord tone (proper resolution target)
            int8_t next_chord_degree = harmony.getChordDegreeAt(result[note_idx + 1].start_tick);
            std::vector<int> next_chord_tones = getChordTonePitchClasses(next_chord_degree);
            int next_pc = next_pitch % 12;
            for (int ct : next_chord_tones) {
              if (next_pc == ct) {
                is_valid_appoggiatura = true;
                break;
              }
            }
          }
        }

        // Only enforce chord tone if NOT a valid appoggiatura
        if (!is_valid_appoggiatura) {
          // Use interval-aware snapping to preserve melodic contour
          uint8_t old_pitch = note.note;
          int new_pitch;
          int max_interval = getMaxMelodicIntervalForSection(ctx.section_type);
          if (prev_final_pitch >= 0) {
            new_pitch = nearestChordToneWithinInterval(note.note, prev_final_pitch, chord_degree,
                                                       max_interval, ctx.vocal_low, ctx.vocal_high,
                                                       &ctx.tessitura);
          } else {
            new_pitch = nearestChordTonePitch(note.note, chord_degree);
          }
          // Defensive clamp to ensure vocal range is respected
          new_pitch = std::clamp(new_pitch, static_cast<int>(ctx.vocal_low),
                                 static_cast<int>(ctx.vocal_high));
          // Re-verify collision safety after chord tone snap
          if (!harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(new_pitch), note.start_tick,
                                                  note.duration, TrackRole::Vocal)) {
            new_pitch = old_pitch;  // Keep original if snap introduces collision
          }
          note.note = static_cast<uint8_t>(new_pitch);
#ifdef MIDISKETCH_NOTE_PROVENANCE
          if (old_pitch != note.note) {
            note.prov_original_pitch = old_pitch;
            note.addTransformStep(TransformStepType::ChordToneSnap, old_pitch, note.note, 0, 0);
          }
#endif
        }
      }
    }
    // Enforce interval constraint between all consecutive notes
    if (prev_final_pitch >= 0) {
      int interval = std::abs(static_cast<int>(note.note) - prev_final_pitch);
      int max_interval = getMaxMelodicIntervalForSection(ctx.section_type);
      if (interval > max_interval) {
        int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);
#ifdef MIDISKETCH_NOTE_PROVENANCE
        uint8_t old_pitch = note.note;
#endif
        int constrained_pitch =
            nearestChordToneWithinInterval(note.note, prev_final_pitch, chord_degree, max_interval,
                                           ctx.vocal_low, ctx.vocal_high, &ctx.tessitura);
        // Defensive clamp to ensure vocal range is respected
        constrained_pitch = std::clamp(constrained_pitch, static_cast<int>(ctx.vocal_low),
                                       static_cast<int>(ctx.vocal_high));
        // Re-verify collision safety after interval fix
        if (!harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(constrained_pitch),
                                                note.start_tick, note.duration, TrackRole::Vocal)) {
          constrained_pitch = note.note;  // Keep original if fix introduces collision
        }
        note.note = static_cast<uint8_t>(constrained_pitch);
#ifdef MIDISKETCH_NOTE_PROVENANCE
        if (old_pitch != note.note) {
          note.prov_original_pitch = old_pitch;
          note.addTransformStep(TransformStepType::IntervalFix, old_pitch, note.note, 0, 0);
        }
#endif
      }
    }
    prev_final_pitch = note.note;
  }

  // Syllabic subdivision: split long same-pitch notes for lyric syllables
  if (ctx.syllabic_sub_ratio > 0.0f && !result.empty()) {
    float min_ms = isHighEnergyVocalStyle(ctx.vocal_style) ? 80.0f : 120.0f;
    float effective_ratio =
        calcEffectiveSubRatio(ctx.syllabic_sub_ratio, ctx.bpm, ctx.is_mora_timed);
    if (effective_ratio > 0.0f) {
      result = subdivideSyllabic(result, effective_ratio, ctx.bpm, min_ms, rng);
    }
  }

  // Remove duplicate notes at same tick (can happen from phrase boundary edge cases)
  if (result.size() > 1) {
    std::vector<NoteEvent> deduplicated;
    deduplicated.reserve(result.size());
    deduplicated.push_back(result[0]);
    for (size_t i = 1; i < result.size(); ++i) {
      if (result[i].start_tick > deduplicated.back().start_tick) {
        deduplicated.push_back(result[i]);
      }
      // If same tick, keep only the first note (discard duplicate)
    }
    result = std::move(deduplicated);
  }

  return result;
}

std::vector<NoteEvent> MelodyDesigner::generateSectionWithEvaluation(
    const MelodyTemplate& tmpl, const SectionContext& ctx, const IHarmonyContext& harmony,
    std::mt19937& rng, VocalStylePreset vocal_style, MelodicComplexity melodic_complexity,
    int candidate_count) {
  // Generate multiple candidates
  std::vector<std::pair<std::vector<NoteEvent>, float>> candidates;
  candidates.reserve(static_cast<size_t>(candidate_count));

  // Get unified style profile for consistent bias and evaluation
  const VocalStyleProfile& profile = getVocalStyleProfile(vocal_style);
  StyleBias bias = adjustBiasForComplexity(profile.bias, melodic_complexity);
  const EvaluatorConfig& config = profile.evaluator;

  for (int i = 0; i < candidate_count; ++i) {
    // Generate a candidate melody
    std::vector<NoteEvent> melody = generateSection(tmpl, ctx, harmony, rng);

    // Combine style-specific evaluation with penalty-based culling
    // Style evaluation: positive features (contour, pattern, surprise)
    MelodyScore style_score = MelodyEvaluator::evaluate(melody, harmony);
    float style_total = style_score.total(config);

    // Culling evaluation: penalty-based (singing difficulty, monotony, gaps)
    // Pass vocal_style for style-specific gap thresholds and breathless penalty
    Tick phrase_duration = ctx.section_end - ctx.section_start;
    float culling_score =
        MelodyEvaluator::evaluateForCulling(melody, harmony, phrase_duration, vocal_style);

    // StyleBias evaluation: interval pattern preferences
    float bias_score = 1.0f;
    if (melody.size() >= 2) {
      int stepwise_count = 0, skip_count = 0, leap_count = 0;
      int same_pitch_count = 0;
      for (size_t j = 1; j < melody.size(); ++j) {
        int interval =
            std::abs(static_cast<int>(melody[j].note) - static_cast<int>(melody[j - 1].note));
        if (interval == 0) {
          same_pitch_count++;
        } else if (interval <= 2) {
          stepwise_count++;
        } else if (interval <= 4) {
          skip_count++;
        } else {
          leap_count++;
        }
      }
      int total_intervals = static_cast<int>(melody.size()) - 1;
      if (total_intervals > 0) {
        // Calculate weighted interval score based on StyleBias
        float stepwise_ratio = static_cast<float>(stepwise_count) / total_intervals;
        float skip_ratio = static_cast<float>(skip_count) / total_intervals;
        float leap_ratio = static_cast<float>(leap_count) / total_intervals;
        float same_ratio = static_cast<float>(same_pitch_count) / total_intervals;

        // Bias score: weight each interval type by style preference
        bias_score = stepwise_ratio * bias.stepwise_weight + skip_ratio * bias.skip_weight +
                     leap_ratio * bias.leap_weight + same_ratio * bias.same_pitch_weight;
        // Normalize to ~1.0 range
        bias_score =
            bias_score /
            (bias.stepwise_weight + bias.skip_weight + bias.leap_weight + bias.same_pitch_weight) *
            4.0f;
        bias_score = std::clamp(bias_score, 0.5f, 1.5f);
      }
    }

    // Combined score: 40% style, 40% culling, 20% bias
    float combined_score = style_total * 0.4f + culling_score * 0.4f + bias_score * 0.2f;

    // GlobalMotif bonus: reward for similar contour/intervals
    // Weight scaled by section type (Task 5-1: section-specific importance):
    // - Chorus: 0.35 (maximum hook recognition)
    // - A (1st): 0.15 (introduce motif fragments)
    // - B: 0.22 (strong tension building)
    // - A (2nd+): 0.25 (reinforce recognition)
    // - Bridge: 0.05 (contrast, deliberately different)
    // Uses section-specific variant for appropriate transformation:
    // - Chorus: original motif (strongest recognition)
    // - A section: diminished (faster feel)
    // - B section: sequenced (building tension)
    // - Bridge: inverted (contrast)
    // - Outro: fragmented (winding down)
    if (cached_global_motif_.has_value() && cached_global_motif_->isValid()) {
      const GlobalMotif& motif_variant = getMotifForSection(ctx.section_type);
      float raw_motif_bonus = evaluateWithGlobalMotif(melody, motif_variant);
      // Apply section-specific weight multiplier
      float section_weight = getMotifWeightForSection(ctx.section_type);
      float motif_bonus = raw_motif_bonus * (section_weight / 0.35f);  // Normalize to max 0.35
      combined_score += motif_bonus;
    }

    candidates.emplace_back(std::move(melody), combined_score);
  }

  // Sort by score (highest first)
  std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  // Cull bottom 50%: only keep top half
  size_t keep_count = std::max(static_cast<size_t>(1), candidates.size() / 2);

  // Select probabilistically from top candidates (maintains diversity)
  // Use weighted selection: higher scores have higher probability
  float total_weight = 0.0f;
  for (size_t i = 0; i < keep_count; ++i) {
    total_weight += candidates[i].second;
  }

  if (total_weight > 0.0f) {
    float roll = rng_util::rollFloat(rng, 0.0f, total_weight);
    float cumulative = 0.0f;
    for (size_t i = 0; i < keep_count; ++i) {
      cumulative += candidates[i].second;
      if (roll <= cumulative) {
        return std::move(candidates[i].first);
      }
    }
  }

  // Fallback: return best candidate
  return std::move(candidates[0].first);
}

MelodyDesigner::PhraseResult MelodyDesigner::generateMelodyPhrase(
    const MelodyTemplate& tmpl, Tick phrase_start, uint8_t phrase_beats, const SectionContext& ctx,
    int prev_pitch, int direction_inertia, const IHarmonyContext& harmony, std::mt19937& rng) {
  PhraseResult result;
  result.notes.clear();
  result.direction_inertia = direction_inertia;

  // Calculate syncopation weight based on vocal groove, section type, and drive_feel
  // drive_feel modulates syncopation: laid-back = less, aggressive = more
  // When enable_syncopation is false, force weight to 0 (no syncopation effects)
  // ctx.syncopation_prob scales the result (StyleMelodyParams override)
  float syncopation_weight =
      ctx.enable_syncopation
          ? getSyncopationWeight(ctx.vocal_groove, ctx.section_type, ctx.drive_feel) *
                (ctx.syncopation_prob / 0.15f)  // Normalize: 0.15 = default, scale proportionally
          : 0.0f;
  syncopation_weight = std::min(syncopation_weight, 0.5f);  // Cap at 0.5

  // Apply long_note_ratio override from SectionContext if user explicitly set it
  MelodyTemplate effective_tmpl = tmpl;
  if (ctx.long_note_ratio_override >= 0.0f) {
    effective_tmpl.long_note_ratio = ctx.long_note_ratio_override;
  }

  // Generate rhythm pattern with section density modifier and 32nd note ratio
  std::vector<RhythmNote> rhythm = generatePhraseRhythm(
      effective_tmpl, phrase_beats, ctx.density_modifier, ctx.thirtysecond_ratio, rng, ctx.paradigm,
      syncopation_weight, ctx.section_type, ctx.bpm, ctx.vocal_style);

  // RhythmSync density boost: if output is too sparse, regenerate with higher density
  // Target: at least 2 notes per beat (phrase_beats * 2) for RhythmSync paradigm
  // This ensures RhythmSync-style locked rhythms maintain their characteristic density
  if (ctx.paradigm == GenerationParadigm::RhythmSync &&
      rhythm.size() < static_cast<size_t>(phrase_beats * 2)) {
    float boost = std::max(1.5f, static_cast<float>(phrase_beats * 2) / rhythm.size());
    float boosted_density = ctx.density_modifier * boost;
    rhythm = generatePhraseRhythm(effective_tmpl, phrase_beats, boosted_density,
                                  ctx.thirtysecond_ratio, rng, ctx.paradigm, syncopation_weight,
                                  ctx.section_type, ctx.bpm, ctx.vocal_style);
  }

  // =========================================================================
  // STYLE PARAM POST-PROCESSING: Apply zombie params to generated rhythm
  // =========================================================================

  // chorus_long_tones: Extend short notes to create sustained melody in Chorus.
  // Converts eighth-note durations to quarter notes for a more open, singable feel.
  // At BPM >= 145 with high-energy idol styles, disable to preserve rapid-fire density.
  bool apply_long_tones =
      ctx.chorus_long_tones && !(ctx.bpm >= 145 && isHighEnergyVocalStyle(ctx.vocal_style));
  if (ctx.section_type == SectionType::Chorus && apply_long_tones) {
    for (auto& rn : rhythm) {
      // Extend eighth notes (1.0 eighths) to quarter notes (2.0 eighths)
      if (rn.eighths >= 0.5f && rn.eighths < 2.0f) {
        rn.eighths = 2.0f;
      }
    }
  }

  // min_note_division: Filter notes shorter than the minimum allowed division.
  // Division value maps: 4=quarter, 8=eighth, 16=sixteenth, 32=thirty-second.
  // Expressed in eighths: quarter=2.0, eighth=1.0, sixteenth=0.5, thirty-second=0.25.
  if (ctx.min_note_division > 0) {
    float min_eighths = 8.0f / static_cast<float>(ctx.min_note_division);
    for (auto& rn : rhythm) {
      if (rn.eighths < min_eighths) {
        rn.eighths = min_eighths;
      }
    }
  }

  // allow_bar_crossing: Clip notes at bar boundaries when crossing is not allowed.
  // Prevents notes from sustaining across barlines for tighter phrasing.
  if (!ctx.allow_bar_crossing) {
    constexpr float kBeatsPerBar = 4.0f;  // 4/4 time
    for (auto& rn : rhythm) {
      float note_end_beat = rn.beat + rn.eighths * 0.5f;  // Convert eighths to beats
      float bar_start = std::floor(rn.beat / kBeatsPerBar) * kBeatsPerBar;
      float bar_end = bar_start + kBeatsPerBar;
      if (note_end_beat > bar_end) {
        float max_beats = bar_end - rn.beat;
        rn.eighths = std::max(0.25f, max_beats * 2.0f);  // Convert beats back to eighths
      }
    }
  }

  // Get chord degree at phrase start
  int8_t start_chord_degree = harmony.getChordDegreeAt(phrase_start);

  // =========================================================================
  // MOTIF FRAGMENT ENFORCEMENT: Inject chorus motif fragments in A/B sections
  // =========================================================================
  // When enforce_motif_fragments is true and we have a cached GlobalMotif,
  // use 2-4 notes from the motif's interval_signature at phrase beginning.
  // This creates song-wide melodic unity by echoing chorus motif in verses.
  std::vector<int8_t> motif_fragment_intervals;
  if (ctx.enforce_motif_fragments && cached_global_motif_.has_value() &&
      cached_global_motif_->isValid()) {
    // Get appropriate variant for this section type
    const GlobalMotif& motif = getMotifForSection(ctx.section_type);

    // Extract 2-4 intervals from the motif signature (random count for variety)
    size_t fragment_length = static_cast<size_t>(
        rng_util::rollRange(rng, 2,
                            static_cast<int>(std::min(static_cast<size_t>(4),
                                                      static_cast<size_t>(motif.interval_count)))));

    for (size_t i = 0; i < fragment_length && i < motif.interval_count; ++i) {
      motif_fragment_intervals.push_back(motif.interval_signature[i]);
    }
  }

  // Calculate initial pitch if none provided
  int current_pitch;
  if (prev_pitch < 0) {
    // For Chorus/B sections, use anchor tone for memorable melodic anchoring
    if (ctx.section_type == SectionType::Chorus || ctx.section_type == SectionType::B) {
      current_pitch = getAnchorTonePitch(start_chord_degree, ctx.tessitura.center, ctx.vocal_low,
                                         ctx.vocal_high);
    } else {
      // Start near tessitura center for other sections
      current_pitch = ctx.tessitura.center;
      // Adjust to chord tone at phrase start
      current_pitch = nearestChordTonePitch(current_pitch, start_chord_degree);
      current_pitch = std::clamp(current_pitch, static_cast<int>(ctx.vocal_low),
                                 static_cast<int>(ctx.vocal_high));
    }
  } else {
    // Start from previous phrase's last pitch, but ensure smooth connection
    // If the interval from prev_pitch to a good starting pitch is too large,
    // find a chord tone that connects better
    current_pitch = prev_pitch;

    // Check if starting on prev_pitch would create melodic isolation
    // (phrase boundaries often have different chord contexts)
    int chord_tone = nearestChordTonePitch(current_pitch, start_chord_degree);
    int interval_to_chord = std::abs(chord_tone - prev_pitch);

    // If prev_pitch is far from current chord context, use chord tone instead
    // This prevents isolated notes at phrase boundaries
    constexpr int kMaxPhraseConnectionInterval = 7;  // Perfect 5th
    if (interval_to_chord <= kMaxPhraseConnectionInterval) {
      // Chord tone is reachable - prefer it for harmonic grounding
      current_pitch = chord_tone;
    } else {
      // Chord tone is too far - find intermediate step
      // Move toward chord tone by at most kMaxPhraseConnectionInterval
      int direction = (chord_tone > prev_pitch) ? 1 : -1;
      int stepped_pitch = prev_pitch + direction * kMaxPhraseConnectionInterval;
      current_pitch = nearestChordTonePitch(stepped_pitch, start_chord_degree);
      current_pitch = std::clamp(current_pitch, static_cast<int>(ctx.vocal_low),
                                 static_cast<int>(ctx.vocal_high));
    }
  }

  // Calculate target pitch if template has target
  int target_pitch = -1;
  if (tmpl.has_target_pitch) {
    target_pitch = calculateTargetPitch(tmpl, ctx, current_pitch, harmony, rng);
  }

  // Track consecutive same notes for J-POP style probability curve
  melody::ConsecutiveSameNoteTracker consecutive_tracker;

  // Track previous note duration for leap preparation principle
  // Pop vocal theory: large leaps need preparation time (longer preceding note)
  Tick prev_note_duration = TICKS_PER_BEAT;  // Default to quarter note

  // Phase 4: Track leap resolution state for multi-note stepwise resolution
  LeapResolutionState leap_state;

  // Same-direction leap chain state (hard limit: no 3+ consecutive
  // same-direction leaps, which outline arpeggios instead of vocal lines)
  int leap_chain_len = 0;
  int leap_chain_dir = 0;

  // Generate notes for each rhythm position
  for (size_t i = 0; i < rhythm.size(); ++i) {
    const RhythmNote& rn = rhythm[i];
    float phrase_pos = static_cast<float>(i) / rhythm.size();

    // Calculate note timing first to get correct chord degree
    Tick note_start = phrase_start + static_cast<Tick>(rn.beat * TICKS_PER_BEAT);
    int8_t note_chord_degree = harmony.getChordDegreeAt(note_start);

    // Select pitch movement (with rhythm-melody coupling and optional contour template)
    PitchChoice choice = selectPitchChoice(tmpl, phrase_pos, target_pitch >= 0, ctx.section_type,
                                           rng, rn.eighths, ctx.forced_contour);

    // =========================================================================
    // MOTIF FRAGMENT APPLICATION: Override pitch choice for first few notes
    // =========================================================================
    // When motif fragments are active, use the interval_signature to guide pitch.
    // This creates a subtle echo of the chorus motif in verse/pre-chorus sections.
    bool using_motif_fragment = false;
    int motif_target_pitch = -1;
    if (!motif_fragment_intervals.empty() && i > 0 && i <= motif_fragment_intervals.size()) {
      // Get the interval for this note (i-1 because first note is base)
      int8_t interval = motif_fragment_intervals[i - 1];
      // Calculate target pitch from previous pitch plus interval (in semitones)
      motif_target_pitch = current_pitch + interval;
      // Snap to nearest chord tone for harmonic safety
      motif_target_pitch = nearestChordTonePitch(motif_target_pitch, note_chord_degree);
      motif_target_pitch = std::clamp(motif_target_pitch, static_cast<int>(ctx.vocal_low),
                                      static_cast<int>(ctx.vocal_high));
      using_motif_fragment = true;
    }

    // Apply direction inertia (only if not using motif fragment)
    if (!using_motif_fragment) {
      choice = applyDirectionInertia(choice, result.direction_inertia, rng);
    }

    // Check vowel section constraint (skip if vowel constraints disabled)
    if (tmpl.vowel_constraint && i > 0 && !ctx.disable_vowel_constraints) {
      bool same_vowel = isInSameVowelSection(rhythm[i - 1].beat, rn.beat, phrase_beats);
      if (same_vowel) {
        int8_t max_step = getMaxStepInVowelSection(true);
        if (tmpl.max_step > max_step) {
          // Constrain to smaller steps within vowel section
          if (choice != PitchChoice::Same) {
            // Force step movement only
            if (rng_util::rollProbability(rng, 0.5f)) {
              choice = PitchChoice::Same;
            }
          }
        }
      }
    }

    // Apply pitch choice - now generates chord tones directly (chord-tone-first approach)
    // Use note_chord_degree (chord at this note's position) instead of ctx.chord_degree
    // Pass note_eighths for rhythm-melody coupling
    int new_pitch;
    if (using_motif_fragment && motif_target_pitch >= 0) {
      // Use motif-guided pitch for fragment notes
      new_pitch = motif_target_pitch;
    } else {
      new_pitch =
          applyPitchChoice(choice, current_pitch, target_pitch, note_chord_degree, ctx.key_offset,
                           ctx.vocal_low, ctx.vocal_high, ctx.vocal_attitude,
                           ctx.disable_vowel_constraints, rn.eighths, ctx.tension_usage);
    }

    // Apply consecutive same note reduction with J-POP style probability curve
    melody::applyConsecutiveSameNoteConstraint(new_pitch, consecutive_tracker, current_pitch,
                                               note_chord_degree, ctx.key_offset, ctx.vocal_low,
                                               ctx.vocal_high, 0, rng);

    // Enforce maximum interval constraint (section-adaptive + blueprint constraint)
    // Use nearestChordToneWithinInterval to stay on chord tones
    // getEffectiveMaxInterval considers both section type and blueprint limits
    int max_interval = getEffectiveMaxInterval(ctx.section_type, ctx.max_leap_semitones);
    int interval = std::abs(new_pitch - current_pitch);
    if (interval > max_interval) {
      new_pitch =
          nearestChordToneWithinInterval(new_pitch, current_pitch, note_chord_degree, max_interval,
                                         ctx.vocal_low, ctx.vocal_high, &ctx.tessitura);
    }

    // Multi-note leap resolution tracking
    int actual_interval = new_pitch - current_pitch;

    // Check if pending resolution should override the selected pitch
    if (leap_state.shouldApplyStep() && !result.notes.empty()) {
      float step_probability = ctx.prefer_stepwise ? 1.0f : 0.80f;

      if (rng_util::rollProbability(rng, step_probability)) {
        std::vector<int> chord_tones = getChordTonePitchClasses(note_chord_degree);
        int best_step_pitch = melody::findStepwiseResolutionPitch(
            current_pitch, chord_tones, leap_state.direction, ctx.vocal_low, ctx.vocal_high);
        if (best_step_pitch >= 0) {
          new_pitch = best_step_pitch;
          actual_interval = new_pitch - current_pitch;
        }
      }
    }

    // Detect new leaps and start resolution tracking
    if (std::abs(actual_interval) >= melody::kLeapThreshold) {
      leap_state.startResolution(actual_interval);
    }

    // Leap preparation principle: limit leaps after short notes
    if (i > 0) {
      new_pitch = melody::applyLeapPreparationConstraint(
          new_pitch, current_pitch, prev_note_duration, note_chord_degree, ctx.vocal_low,
          ctx.vocal_high, &ctx.tessitura);
    }

    // Movement encouragement: avoid static repeats after long notes
    if (i > 0) {
      new_pitch = melody::encourageMovementAfterLongNote(
          new_pitch, current_pitch, prev_note_duration, note_chord_degree, ctx.key_offset,
          ctx.vocal_low, ctx.vocal_high, rng);
    }

    // Avoid note check: melody should not form tritone/minor2nd with chord tones
    // (short weak-beat notes pass through as passing/neighbor tones)
    new_pitch = melody::enforceAvoidNoteConstraint(new_pitch, note_chord_degree, ctx.vocal_low,
                                                   ctx.vocal_high, note_start,
                                                   static_cast<Tick>(rn.eighths * TICK_EIGHTH));

    // Downbeat chord-tone constraint: beat 1 requires chord tones for harmonic
    // clarity (short stepwise approaches pass as appoggiaturas)
    new_pitch = melody::enforceDownbeatChordTone(
        new_pitch, note_start, note_chord_degree, current_pitch, ctx.vocal_low, ctx.vocal_high,
        ctx.disable_vowel_constraints, static_cast<Tick>(rn.eighths * TICK_EIGHTH));

    // Guide tone priority: on strong beats, bias toward 3rd/7th at configured rate
    if (ctx.guide_tone_rate > 0 && ctx.vocal_attitude != VocalAttitude::Raw) {
      new_pitch = melody::enforceGuideToneOnDownbeat(new_pitch, note_start, note_chord_degree,
                                                     ctx.vocal_low, ctx.vocal_high,
                                                     ctx.guide_tone_rate, rng, current_pitch);
    }

    // Leap-after-reversal rule: prefer step motion in opposite direction after leaps
    if (i > 0 && !result.notes.empty()) {
      int prev_note = result.notes.back().note;
      int prev_interval = current_pitch - prev_note;
      std::vector<int> chord_tones = getChordTonePitchClasses(note_chord_degree);
      float phrase_pos = static_cast<float>(i) / rhythm.size();
      new_pitch = melody::applyLeapReversalRule(
          new_pitch, current_pitch, prev_interval, chord_tones, ctx.vocal_low, ctx.vocal_high,
          ctx.prefer_stepwise, rng, static_cast<int8_t>(ctx.section_type), phrase_pos);
    }

    // FINAL SAFETY CHECK: Re-enforce max interval after all adjustments
    // Previous adjustments (avoid note, downbeat snapping) might have created
    // large intervals. This final check ensures singability is maintained.
    {
      // Section-adaptive max interval + blueprint constraint
      int effective_max_interval =
          getEffectiveMaxInterval(ctx.section_type, ctx.max_leap_semitones);
      int final_interval = std::abs(new_pitch - current_pitch);
      if (final_interval > effective_max_interval) {
        new_pitch = nearestChordToneWithinInterval(new_pitch, current_pitch, note_chord_degree,
                                                   effective_max_interval, ctx.vocal_low,
                                                   ctx.vocal_high, &ctx.tessitura);
      }
    }

    // Update direction inertia
    int movement = new_pitch - current_pitch;
    if (movement > 0) {
      result.direction_inertia = std::min(result.direction_inertia + 1, 3);
    } else if (movement < 0) {
      result.direction_inertia = std::max(result.direction_inertia - 1, -3);
    } else {
      // Same pitch - decay inertia
      if (result.direction_inertia > 0) result.direction_inertia--;
      if (result.direction_inertia < 0) result.direction_inertia++;
    }

    // Calculate duration from rhythm eighths field
    // This preserves short note durations regardless of quantized positions
    // (note_start already calculated above)
    // Use rhythm grid from template for triplet support
    Tick eighth_unit = getRhythmUnit(tmpl.rhythm_grid, true);
    Tick note_duration = static_cast<Tick>(rn.eighths * eighth_unit);

    // Cap to gap if next note is closer to prevent overlap
    if (i + 1 < rhythm.size()) {
      float beat_duration = rhythm[i + 1].beat - rn.beat;
      Tick gap_duration = static_cast<Tick>(beat_duration * TICKS_PER_BEAT);
      note_duration = std::min(note_duration, gap_duration);
    }

    // Vocal-friendly gate processing using constraint_pipeline
    bool is_phrase_end = (i == rhythm.size() - 1);
    bool is_phrase_start = (i == 0);

    // Phrase ending: ensure minimum duration for proper cadence
    // At fast tempos, use BPM-scaled minimum (~400ms) instead of fixed quarter note
    if (is_phrase_end) {
      constexpr float kMinPhraseEndSeconds = 0.4f;
      Tick bpm_phrase_end_min =
          static_cast<Tick>(kMinPhraseEndSeconds * ctx.bpm * TICKS_PER_BEAT / 60.0f);
      Tick phrase_end_min = std::max(static_cast<Tick>(TICK_QUARTER), bpm_phrase_end_min);
      note_duration = std::max(note_duration, phrase_end_min);
    }

    // Build gate context for constraint pipeline
    melody::GateContext gate_ctx;
    gate_ctx.is_phrase_end = is_phrase_end;
    gate_ctx.is_phrase_start = is_phrase_start;
    gate_ctx.note_duration = note_duration;
    gate_ctx.interval_from_prev =
        result.notes.empty() ? 0 : std::abs(new_pitch - result.notes.back().note);

    // Apply all duration constraints using the pipeline
    Tick phrase_end = phrase_start + phrase_beats * TICKS_PER_BEAT;
    note_duration = melody::applyAllDurationConstraints(note_start, note_duration, harmony,
                                                        phrase_end, gate_ctx);

    // =========================================================================
    // PHRASE END RESOLUTION: Enforce chord tone landing for singable cadences
    // =========================================================================
    // When phrase_end_resolution > 0, final notes should resolve to chord tones.
    // For Chorus sections, prefer root note for maximum stability and memorability.
    // This creates natural phrase endings that singers instinctively expect.
    if (is_phrase_end && tmpl.phrase_end_resolution > 0.0f) {
      if (rng_util::rollProbability(rng, tmpl.phrase_end_resolution)) {
        std::vector<int> chord_tones = getChordTonePitchClasses(note_chord_degree);
        int pitch_pc = new_pitch % 12;
        bool is_chord_tone = false;
        for (int ct : chord_tones) {
          if (pitch_pc == ct) {
            is_chord_tone = true;
            break;
          }
        }
        if (!is_chord_tone) {
          // Snap to nearest chord tone
          new_pitch = nearestChordTonePitch(new_pitch, note_chord_degree);
          new_pitch = std::clamp(new_pitch, static_cast<int>(ctx.vocal_low),
                                 static_cast<int>(ctx.vocal_high));
        }
        // For Chorus/Drop sections, prefer root note for strong cadence (75%)
        // For B/Bridge sections, prefer root or 5th for moderate resolution (50%)
        float root_prob = 0.0f;
        bool allow_fifth = false;
        if (ctx.section_type == SectionType::Chorus || ctx.section_type == SectionType::Drop) {
          root_prob = 0.75f;
        } else if (ctx.section_type == SectionType::B || ctx.section_type == SectionType::Bridge) {
          root_prob = 0.50f;
          allow_fifth = true;
        }
        if (root_prob > 0.0f && rng_util::rollProbability(rng, root_prob)) {
          int root_pc = chord_tones.empty() ? 0 : chord_tones[0];
          int octave = new_pitch / 12;
          int root_pitch = octave * 12 + root_pc;
          if (root_pitch < static_cast<int>(ctx.vocal_low)) root_pitch += 12;
          if (root_pitch > static_cast<int>(ctx.vocal_high)) root_pitch -= 12;
          bool resolved = (root_pitch >= static_cast<int>(ctx.vocal_low) &&
                           root_pitch <= static_cast<int>(ctx.vocal_high));
          if (resolved) {
            new_pitch = root_pitch;
          } else if (allow_fifth && chord_tones.size() >= 3) {
            // Fall back to 5th for B/Bridge when root is out of range
            int fifth_pc = chord_tones[2];  // root(0), 3rd(1), 5th(2)
            int fifth_pitch = octave * 12 + fifth_pc;
            if (fifth_pitch < static_cast<int>(ctx.vocal_low)) fifth_pitch += 12;
            if (fifth_pitch > static_cast<int>(ctx.vocal_high)) fifth_pitch -= 12;
            if (fifth_pitch >= static_cast<int>(ctx.vocal_low) &&
                fifth_pitch <= static_cast<int>(ctx.vocal_high)) {
              new_pitch = fifth_pitch;
            }
          }
        }
      }
    }

    // Calculate velocity
    uint8_t velocity = DEFAULT_VELOCITY;
    if (rn.strong) {
      velocity = std::min(127, velocity + 10);
    }
    if (is_phrase_end) {
      velocity = static_cast<uint8_t>(velocity * 0.85f);
    }

    // Apply phrase-internal velocity curve for natural crescendo/decrescendo
    // This creates expression within the phrase beyond bar-level dynamics
    ContourType contour_for_curve = ctx.forced_contour.value_or(ContourType::Plateau);
    float phrase_curve = getPhraseNoteVelocityCurve(
        static_cast<int>(i), static_cast<int>(rhythm.size()), contour_for_curve);
    velocity = vel::clamp(static_cast<int>(velocity * phrase_curve));

    // Final clamp to ensure pitch is within vocal range
    // ABSOLUTE CONSTRAINT: Ensure pitch is on scale (prevents chromatic notes)
    new_pitch = snapToNearestScaleTone(new_pitch, ctx.key_offset);
    new_pitch =
        std::clamp(new_pitch, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high));

    // HARD CONSTRAINT: Never extend a same-direction leap chain to 3
    new_pitch =
        melody::enforceLeapChainLimit(new_pitch, current_pitch, leap_chain_len, leap_chain_dir,
                                      ctx.key_offset, ctx.vocal_low, ctx.vocal_high);

    // Apply pitch safety check to avoid collisions with other tracks (e.g., Motif tritone)
    // Use getSafePitchCandidates for unified collision resolution
    auto candidates =
        getSafePitchCandidates(harmony, static_cast<uint8_t>(new_pitch), note_start, note_duration,
                               TrackRole::Vocal, ctx.vocal_low, ctx.vocal_high);
    if (candidates.empty()) {
      continue;  // No safe pitch available
    }

    // Select best candidate considering melodic context
    PitchSelectionHints hints;
    hints.prev_pitch = static_cast<int8_t>(current_pitch);
    hints.note_duration = note_duration;
    hints.phrase_position = static_cast<float>(i) / rhythm.size();
    hints.tessitura_center = ctx.tessitura.center;
    hints.section_type = static_cast<int8_t>(ctx.section_type);
    hints.sub_phrase_index = static_cast<int8_t>(ctx.sub_phrase_index);
    // Propagate contour direction from accumulated direction inertia
    if (result.direction_inertia > 0)
      hints.contour_direction = 1;
    else if (result.direction_inertia < 0)
      hints.contour_direction = -1;
    new_pitch = selectBestCandidate(candidates, static_cast<uint8_t>(new_pitch), hints);

    // Add note (registration handled by VocalGenerator)
    NoteEvent note = createNoteWithoutHarmony(note_start, note_duration,
                                              static_cast<uint8_t>(new_pitch), velocity);
#ifdef MIDISKETCH_NOTE_PROVENANCE
    note.prov_source = static_cast<uint8_t>(NoteSource::MelodyPhrase);
    note.prov_chord_degree = note_chord_degree;
    note.prov_lookup_tick = note_start;
    note.prov_original_pitch = static_cast<uint8_t>(new_pitch);
#endif
    result.notes.push_back(note);

    // Update leap chain state from the final pitch (collision resolution may
    // have shifted it after the chain guard ran)
    melody::updateLeapChainState(new_pitch - current_pitch, leap_chain_len, leap_chain_dir);

    current_pitch = new_pitch;
    prev_note_duration = note_duration;  // Track for leap preparation
  }

  // POST-GENERATION: Resolve melodically isolated notes before returning
  melody::resolveIsolatedNotes(result.notes, harmony, ctx.vocal_low, ctx.vocal_high);

  // Re-check chord boundaries after isolated note resolution (pitch may have changed)
  for (auto& note : result.notes) {
    note.duration =
        melody::clampToChordBoundary(note.start_tick, note.duration, harmony, note.note);
  }

  // Absorb short final note into previous note when chord-boundary clipping
  // creates a very short phrase ending (< quarter note). A 230t fragment at
  // phrase end sounds choppy; extending the previous note is more singable.
  if (result.notes.size() >= 2) {
    auto& last = result.notes.back();
    if (last.duration < TICK_QUARTER) {
      auto& prev = result.notes[result.notes.size() - 2];
      Tick target_end = last.start_tick + last.duration;
      Tick extended_dur = target_end - prev.start_tick;
      // Verify the extension is safe at chord boundaries for the previous note's pitch
      Tick safe_dur =
          melody::clampToChordBoundary(prev.start_tick, extended_dur, harmony, prev.note);
      if (safe_dur > prev.duration) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
        Tick old_dur = prev.duration;
        prev.addTransformStep(TransformStepType::ChordBoundaryClip,
                              static_cast<uint8_t>(old_dur >> 8),
                              static_cast<uint8_t>(safe_dur >> 8), 0, 0);
#endif
        prev.duration = safe_dur;
        result.notes.pop_back();
        current_pitch = result.notes.back().note;
      } else {
        // Can't extend prev across chord boundary.
        // Remove the short trailing note — phrase resolves on the previous sustained note.
        result.notes.pop_back();
        current_pitch = result.notes.empty() ? current_pitch : result.notes.back().note;
      }
    }
  }

  result.last_pitch = current_pitch;
  return result;
}

// generateHook moved to melody_hook.cpp

// selectPitchChoice, applyDirectionInertia, getEffectivePlateauRatio, shouldLeap,
// getStabilizeStep, isInSameVowelSection, getMaxStepInVowelSection
// moved to contour_direction.h/cpp as free functions in melody:: namespace.

// applyTransitionApproach and insertLeadingTone moved to melody_transition.cpp

// applyPitchChoice moved to pitch_resolver.h/cpp as free function in melody:: namespace.

// (old generateHook body removed — now in melody_hook.cpp)
int MelodyDesigner::calculateTargetPitch(const MelodyTemplate& tmpl, const SectionContext& ctx,
                                         [[maybe_unused]] int current_pitch,
                                         const IHarmonyContext& harmony,
                                         [[maybe_unused]] std::mt19937& rng) {
  return melody::calculateTargetPitch(ctx.tessitura.center, tmpl.tessitura_range, ctx.vocal_low,
                                      ctx.vocal_high, ctx.section_start, harmony);
}

// generatePhraseRhythm moved to rhythm_generator.h/cpp as free function in melody:: namespace.

// === Locked Rhythm Pitch Selection ===

uint8_t MelodyDesigner::selectPitchForLockedRhythmEnhanced(
    uint8_t prev_pitch, int8_t chord_degree, uint8_t vocal_low, uint8_t vocal_high,
    float phrase_position, int direction_inertia, size_t note_index, std::mt19937& rng,
    SectionType section_type, VocalAttitude vocal_attitude, int same_pitch_streak) {
  // Build context for enhanced selection
  LockedRhythmContext ctx;
  ctx.phrase_position = phrase_position;
  ctx.direction_inertia = direction_inertia;
  ctx.note_index = note_index;
  ctx.tessitura_center = (vocal_low + vocal_high) / 2;
  ctx.section_type = section_type;
  ctx.vocal_attitude = vocal_attitude;
  ctx.same_pitch_streak = same_pitch_streak;

  // Use GlobalMotif if available
  if (cached_global_motif_.has_value() && cached_global_motif_->isValid()) {
    ctx.motif_intervals = cached_global_motif_->interval_signature;
    ctx.motif_interval_count = cached_global_motif_->interval_count;
  } else {
    ctx.motif_intervals = nullptr;
    ctx.motif_interval_count = 0;
  }

  return melody::selectPitchForLockedRhythmEnhanced(prev_pitch, chord_degree, vocal_low, vocal_high,
                                                    ctx, rng);
}

// === GlobalMotif Support ===

// extractGlobalMotif and evaluateWithGlobalMotif moved to motif_support.h/cpp
// as free functions in melody:: namespace.

// ============================================================================
// Section-Specific Motif Variants
// ============================================================================

}  // namespace midisketch
