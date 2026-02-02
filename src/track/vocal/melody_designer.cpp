/**
 * @file melody_designer.cpp
 * @brief Implementation of MelodyDesigner track generation.
 */

#include "track/vocal/melody_designer.h"

#include <algorithm>
#include <cmath>

#include "core/harmonic_rhythm.h"
#include "core/hook_utils.h"
#include "core/i_harmony_context.h"
#include "core/melody_embellishment.h"
#include "core/motif_transform.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/phrase_patterns.h"
#include "core/pitch_utils.h"
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
#include "track/melody/note_constraints.h"
#include "track/melody/pitch_constraints.h"
#include "track/melody/pitch_resolver.h"
#include "track/melody/rhythm_generator.h"
#include "track/melody/motif_support.h"

namespace midisketch {

// Import melody submodule symbols
using melody::getBaseBreathDuration;
using melody::getBreathDuration;
using melody::getMotifWeightForSection;
using melody::getRhythmUnit;
using melody::LeapResolutionState;
using melody::applyPitchChoiceImpl;
using melody::calculateTargetPitchImpl;
using melody::generatePhraseRhythmImpl;
using melody::selectPitchForLockedRhythmImpl;
using melody::extractGlobalMotifImpl;
using melody::evaluateWithGlobalMotifImpl;
using melody::getDirectionBiasForContour;
using melody::selectPitchChoiceImpl;
using melody::applyDirectionInertiaImpl;
using melody::getEffectivePlateauRatioImpl;
using melody::shouldLeapImpl;
using melody::getStabilizeStepImpl;
using melody::isInSameVowelSectionImpl;
using melody::getMaxStepInVowelSectionImpl;

namespace {

// Default velocity for melody notes
constexpr uint8_t DEFAULT_VELOCITY = 100;

// Local wrapper - uses submodule function
int getEffectiveMaxInterval(SectionType section_type, uint8_t ctx_max_leap) {
  return melody::getEffectiveMaxInterval(section_type, ctx_max_leap);
}

// Import additional submodule functions
using melody::getBassRootPitchClass;
using melody::isAvoidNoteWithChord;
using melody::isAvoidNoteWithRoot;
using melody::getNearestSafeChordTone;
using melody::getAnchorTonePitch;
using melody::calculatePhraseCount;
using melody::applySequentialTransposition;
using melody::HookRhythmPattern;
using melody::getHookRhythmPatterns;
using melody::getHookRhythmPatternCount;
using melody::selectHookRhythmPatternIndex;

}  // namespace

std::vector<NoteEvent> MelodyDesigner::generateSection(const MelodyTemplate& tmpl,
                                                       const SectionContext& ctx,
                                                       const IHarmonyContext& harmony,
                                                       std::mt19937& rng) {
  std::vector<NoteEvent> result;

  // Calculate phrase structure aligned with harmonic rhythm
  uint8_t phrase_beats = tmpl.max_phrase_beats;

  // Get harmonic rhythm for this section to align phrases with chord changes
  HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(ctx.section_type, ctx.mood);

  // Determine chord change interval in beats
  // Slow: 8 beats (2 bars), Normal: 4 beats (1 bar), Dense: 4 beats minimum
  uint8_t chord_unit_beats = (harmonic.density == HarmonicDensity::Slow) ? 8 : 4;

  // Align phrase length to chord boundaries
  // This prevents melodies from sustaining across chord changes
  if (phrase_beats > chord_unit_beats) {
    phrase_beats = chord_unit_beats;
  }

  uint8_t phrase_bars = (phrase_beats + 3) / 4;  // Convert to bars
  uint8_t phrase_count = calculatePhraseCount(ctx.section_bars, phrase_bars);

  int prev_pitch = -1;
  int direction_inertia = 0;

  Tick current_tick = ctx.section_start;

  for (uint8_t i = 0; i < phrase_count; ++i) {
    // Calculate actual phrase length for this iteration
    Tick remaining = ctx.section_end - current_tick;
    uint8_t actual_beats = std::min(static_cast<uint8_t>(phrase_beats),
                                    static_cast<uint8_t>(remaining / TICKS_PER_BEAT));

    if (actual_beats < 2) break;  // Too short for a phrase

    // Apply anticipation rest before phrases (except first phrase of section)
    // This creates "tame" (溜め) effect common in J-POP for building anticipation
    // Skip for UltraVocaloid (high thirtysecond_ratio) which needs continuous machine-gun passages
    if (i > 0 && ctx.anticipation_rest != AnticipationRestMode::Off && ctx.thirtysecond_ratio < 0.8f) {
      Tick anticipation_duration = 0;
      switch (ctx.anticipation_rest) {
        case AnticipationRestMode::Subtle:
          anticipation_duration = TICK_SIXTEENTH;
          break;
        case AnticipationRestMode::Moderate:
          anticipation_duration = TICK_EIGHTH;
          break;
        case AnticipationRestMode::Pronounced:
          anticipation_duration = TICK_QUARTER;
          break;
        default:
          break;
      }
      current_tick += anticipation_duration;
      // Recalculate remaining time after adding anticipation rest
      remaining = ctx.section_end - current_tick;
      actual_beats = std::min(actual_beats, static_cast<uint8_t>(remaining / TICKS_PER_BEAT));
      if (actual_beats < 2) break;
    }

    // Generate hook for chorus at specific positions
    // Skip hook for UltraVocaloid (high thirtysecond_ratio) - needs continuous machine-gun passages
    bool is_hook_position =
        (ctx.section_type == SectionType::Chorus) && (i == 0 || (i == 2 && phrase_count > 3));
    bool use_hook = is_hook_position && tmpl.hook_note_count > 0 && ctx.thirtysecond_ratio < 0.8f;

    PhraseResult phrase_result;
    if (use_hook) {
      phrase_result = generateHook(tmpl, current_tick, ctx, prev_pitch, harmony, rng);
    } else {
      phrase_result = generateMelodyPhrase(tmpl, current_tick, actual_beats, ctx, prev_pitch,
                                           direction_inertia, harmony, rng);
    }

    // Apply sequential transposition for B sections (Zekvenz effect)
    // Creates ascending sequence: each phrase rises by 2-4-5 semitones
    applySequentialTransposition(phrase_result.notes, i, ctx.section_type, ctx.key_offset,
                                 ctx.vocal_low, ctx.vocal_high);

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
          adjusted_note.note = static_cast<uint8_t>(nearestChordToneWithinInterval(
              adjusted_note.note, prev_note_pitch, note_chord_degree, MAX_PHRASE_INTERVAL,
              ctx.vocal_low, ctx.vocal_high, &ctx.tessitura));
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
      adjusted_note.note = static_cast<uint8_t>(
          std::clamp(snapped, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high)));
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

    // Update state for next phrase
    // Use actual last pitch after transposition and adjustment (not original)
    if (!result.empty()) {
      prev_pitch = result.back().note;
    } else {
      prev_pitch = phrase_result.last_pitch;
    }
    direction_inertia = phrase_result.direction_inertia;

    // Move to next phrase position
    // For hooks, calculate actual duration from generated notes to avoid overlap
    // when hook spans multiple phrase lengths (e.g., Idol style with 4 repeats)
    if (is_hook_position && !phrase_result.notes.empty()) {
      // Find the end tick of the last generated note
      Tick last_note_end = 0;
      for (const auto& note : phrase_result.notes) {
        Tick note_end = note.start_tick + note.duration;
        if (note_end > last_note_end) {
          last_note_end = note_end;
        }
      }
      // Advance current_tick to after the hook (with small gap)
      if (last_note_end > current_tick) {
        current_tick = last_note_end;
      } else {
        current_tick += actual_beats * TICKS_PER_BEAT;
      }
    } else {
      current_tick += actual_beats * TICKS_PER_BEAT;
    }

    // Add rest between phrases (breathing) - skip if breathing gaps disabled
    // Breath duration varies by section type, mood, and phrase characteristics
    if (i < phrase_count - 1 && !ctx.disable_breathing_gaps) {
      // Calculate phrase characteristics for context-aware breathing
      float phrase_density = 0.0f;
      uint8_t phrase_high_pitch = 60;  // Default: middle C
      if (!phrase_result.notes.empty() && actual_beats > 0) {
        phrase_density = static_cast<float>(phrase_result.notes.size()) / actual_beats;
        for (const auto& note : phrase_result.notes) {
          if (note.note > phrase_high_pitch) {
            phrase_high_pitch = note.note;
          }
        }
      }
      current_tick += getBreathDuration(ctx.section_type, ctx.mood, phrase_density,
                                        phrase_high_pitch, nullptr, ctx.vocal_style);
    }

    // Snap to next half-bar boundary (phrase_beats/2 * TICKS_PER_BEAT grid)
    // Using half-bar intervals reduces gaps while respecting harmonic rhythm
    // This ensures phrases start at musically sensible points without large silences
    Tick half_phrase_beats = std::max(static_cast<Tick>(phrase_beats / 2), static_cast<Tick>(2));
    Tick snap_interval = half_phrase_beats * TICKS_PER_BEAT;
    Tick relative_tick = current_tick - ctx.section_start;
    Tick next_boundary = ((relative_tick + snap_interval - 1) / snap_interval) * snap_interval;
    current_tick = ctx.section_start + next_boundary;

    // Ensure we don't exceed section end
    if (current_tick >= ctx.section_end) break;
  }

  // Apply melodic embellishment (non-chord tones) if enabled
  if (ctx.enable_embellishment && !result.empty()) {
    EmbellishmentConfig emb_config = MelodicEmbellisher::getConfigForMood(ctx.mood);
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
    Tick bar_pos = note.start_tick % TICKS_PER_BAR;
    bool is_downbeat = bar_pos < TICKS_PER_BEAT / 4;
    if (is_downbeat) {
      int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);
      std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);
      int pitch_pc = note.note % 12;
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
        if (isScaleTone(pitch_pc, static_cast<uint8_t>(ctx.key_offset)) && note_idx + 1 < result.size()) {
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
#ifdef MIDISKETCH_NOTE_PROVENANCE
          uint8_t old_pitch = note.note;
#endif
          int new_pitch;
          int max_interval = getMaxMelodicIntervalForSection(ctx.section_type);
          if (prev_final_pitch >= 0) {
            new_pitch = nearestChordToneWithinInterval(note.note, prev_final_pitch, chord_degree,
                                                       max_interval, ctx.vocal_low,
                                                       ctx.vocal_high, &ctx.tessitura);
          } else {
            new_pitch = nearestChordTonePitch(note.note, chord_degree);
          }
          // Defensive clamp to ensure vocal range is respected
          new_pitch = std::clamp(new_pitch, static_cast<int>(ctx.vocal_low),
                                 static_cast<int>(ctx.vocal_high));
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
        int constrained_pitch = nearestChordToneWithinInterval(
            note.note, prev_final_pitch, chord_degree, max_interval, ctx.vocal_low,
            ctx.vocal_high, &ctx.tessitura);
        // Defensive clamp to ensure vocal range is respected
        constrained_pitch = std::clamp(constrained_pitch, static_cast<int>(ctx.vocal_low),
                                       static_cast<int>(ctx.vocal_high));
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
    std::uniform_real_distribution<float> dist(0.0f, total_weight);
    float roll = dist(rng);
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
  float syncopation_weight = getSyncopationWeight(ctx.vocal_groove, ctx.section_type, ctx.drive_feel);

  // Generate rhythm pattern with section density modifier and 32nd note ratio
  std::vector<RhythmNote> rhythm = generatePhraseRhythm(
      tmpl, phrase_beats, ctx.density_modifier, ctx.thirtysecond_ratio, rng, ctx.paradigm,
      syncopation_weight, ctx.section_type);

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
    std::uniform_int_distribution<size_t> count_dist(2, std::min(static_cast<size_t>(4),
                                                                  static_cast<size_t>(motif.interval_count)));
    size_t fragment_length = count_dist(rng);

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

  // Generate notes for each rhythm position
  for (size_t i = 0; i < rhythm.size(); ++i) {
    const RhythmNote& rn = rhythm[i];
    float phrase_pos = static_cast<float>(i) / rhythm.size();

    // Calculate note timing first to get correct chord degree
    Tick note_start = phrase_start + static_cast<Tick>(rn.beat * TICKS_PER_BEAT);
    int8_t note_chord_degree = harmony.getChordDegreeAt(note_start);

    // Select pitch movement (with rhythm-melody coupling and optional contour template)
    PitchChoice choice = selectPitchChoice(tmpl, phrase_pos, target_pitch >= 0, ctx.section_type, rng,
                                           rn.eighths, ctx.forced_contour);

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
      choice = applyDirectionInertia(choice, result.direction_inertia, tmpl, rng);
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
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            if (dist(rng) < 0.5f) {
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
      new_pitch = applyPitchChoice(choice, current_pitch, target_pitch, note_chord_degree,
                                   ctx.key_offset, ctx.vocal_low, ctx.vocal_high,
                                   ctx.vocal_attitude, ctx.disable_vowel_constraints,
                                   rn.eighths);
    }

    // Apply consecutive same note reduction with J-POP style probability curve
    melody::applyConsecutiveSameNoteConstraint(
        new_pitch, consecutive_tracker, current_pitch, note_chord_degree,
        ctx.vocal_low, ctx.vocal_high, 0, rng);

    // Enforce maximum interval constraint (section-adaptive + blueprint constraint)
    // Use nearestChordToneWithinInterval to stay on chord tones
    // getEffectiveMaxInterval considers both section type and blueprint limits
    int max_interval = getEffectiveMaxInterval(ctx.section_type, ctx.max_leap_semitones);
    int interval = std::abs(new_pitch - current_pitch);
    if (interval > max_interval) {
      new_pitch = nearestChordToneWithinInterval(new_pitch, current_pitch, note_chord_degree,
                                                 max_interval, ctx.vocal_low, ctx.vocal_high,
                                                 &ctx.tessitura);
    }

    // Multi-note leap resolution tracking
    int actual_interval = new_pitch - current_pitch;

    // Check if pending resolution should override the selected pitch
    if (leap_state.shouldApplyStep() && !result.notes.empty()) {
      float step_probability = ctx.prefer_stepwise ? 1.0f : 0.80f;
      std::uniform_real_distribution<float> step_dist(0.0f, 1.0f);

      if (step_dist(rng) < step_probability) {
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
      new_pitch = melody::applyLeapPreparationConstraint(new_pitch, current_pitch, prev_note_duration,
                                                          note_chord_degree, ctx.vocal_low,
                                                          ctx.vocal_high, &ctx.tessitura);
    }

    // Leap encouragement: encourage movement after long notes
    if (i > 0) {
      new_pitch = melody::encourageLeapAfterLongNote(new_pitch, current_pitch, prev_note_duration,
                                                      note_chord_degree, ctx.vocal_low,
                                                      ctx.vocal_high, rng);
    }

    // Avoid note check: melody should not form tritone/minor2nd with chord tones
    new_pitch = melody::enforceAvoidNoteConstraint(new_pitch, note_chord_degree,
                                                    ctx.vocal_low, ctx.vocal_high);

    // Downbeat chord-tone constraint: beat 1 requires chord tones for harmonic clarity
    new_pitch = melody::enforceDownbeatChordTone(new_pitch, note_start, note_chord_degree,
                                                  current_pitch, ctx.vocal_low, ctx.vocal_high,
                                                  ctx.disable_vowel_constraints);

    // Leap-after-reversal rule: prefer step motion in opposite direction after leaps
    if (i > 0 && !result.notes.empty()) {
      int prev_note = result.notes.back().note;
      int prev_interval = current_pitch - prev_note;
      std::vector<int> chord_tones = getChordTonePitchClasses(note_chord_degree);
      new_pitch = melody::applyLeapReversalRule(new_pitch, current_pitch, prev_interval,
                                                 chord_tones, ctx.vocal_low, ctx.vocal_high,
                                                 ctx.prefer_stepwise, rng);
    }

    // FINAL SAFETY CHECK: Re-enforce max interval after all adjustments
    // Previous adjustments (avoid note, downbeat snapping) might have created
    // large intervals. This final check ensures singability is maintained.
    {
      // Section-adaptive max interval + blueprint constraint
      int effective_max_interval = getEffectiveMaxInterval(ctx.section_type, ctx.max_leap_semitones);
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

    // Phrase ending: ensure minimum quarter note duration for proper cadence
    if (is_phrase_end) {
      note_duration = std::max(note_duration, static_cast<Tick>(TICK_QUARTER));
    }

    // Build gate context for constraint pipeline
    melody::GateContext gate_ctx;
    gate_ctx.is_phrase_end = is_phrase_end;
    gate_ctx.is_phrase_start = is_phrase_start;
    gate_ctx.note_duration = note_duration;
    gate_ctx.interval_from_prev = result.notes.empty() ? 0
                                  : std::abs(new_pitch - result.notes.back().note);

    // Apply all duration constraints using the pipeline
    Tick phrase_end = phrase_start + phrase_beats * TICKS_PER_BEAT;
    note_duration = melody::applyAllDurationConstraints(
        note_start, note_duration, harmony, phrase_end, gate_ctx);

    // =========================================================================
    // PHRASE END RESOLUTION: Enforce chord tone landing for singable cadences
    // =========================================================================
    // When phrase_end_resolution > 0, final notes should resolve to chord tones.
    // For Chorus sections, prefer root note for maximum stability and memorability.
    // This creates natural phrase endings that singers instinctively expect.
    if (is_phrase_end && tmpl.phrase_end_resolution > 0.0f) {
      std::uniform_real_distribution<float> resolve_dist(0.0f, 1.0f);
      if (resolve_dist(rng) < tmpl.phrase_end_resolution) {
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
        // For Chorus sections, prefer root note resolution for strong cadence
        if (ctx.section_type == SectionType::Chorus && resolve_dist(rng) < 0.6f) {
          // Find root of current chord
          int root_pc = chord_tones.empty() ? 0 : chord_tones[0];
          // Find nearest root in vocal range
          int octave = new_pitch / 12;
          int root_pitch = octave * 12 + root_pc;
          if (root_pitch < static_cast<int>(ctx.vocal_low)) root_pitch += 12;
          if (root_pitch > static_cast<int>(ctx.vocal_high)) root_pitch -= 12;
          if (root_pitch >= static_cast<int>(ctx.vocal_low) &&
              root_pitch <= static_cast<int>(ctx.vocal_high)) {
            new_pitch = root_pitch;
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
    ContourType contour_for_curve =
        ctx.forced_contour.value_or(ContourType::Plateau);
    float phrase_curve = getPhraseNoteVelocityCurve(
        static_cast<int>(i), static_cast<int>(rhythm.size()), contour_for_curve);
    velocity = vel::clamp(static_cast<int>(velocity * phrase_curve));

    // Final clamp to ensure pitch is within vocal range
    // ABSOLUTE CONSTRAINT: Ensure pitch is on scale (prevents chromatic notes)
    new_pitch = snapToNearestScaleTone(new_pitch, ctx.key_offset);
    new_pitch =
        std::clamp(new_pitch, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high));

    // Apply pitch safety check to avoid collisions with other tracks (e.g., Motif tritone)
    // Use getSafePitchCandidates for unified collision resolution
    auto candidates = getSafePitchCandidates(harmony, static_cast<uint8_t>(new_pitch), note_start,
                                              note_duration, TrackRole::Vocal, ctx.vocal_low,
                                              ctx.vocal_high);
    if (candidates.empty()) {
      continue;  // No safe pitch available
    }

    // Select best candidate considering melodic context
    PitchSelectionHints hints;
    hints.prev_pitch = static_cast<int8_t>(current_pitch);
    hints.note_duration = note_duration;
    hints.phrase_position = static_cast<float>(i) / rhythm.size();
    hints.tessitura_center = ctx.tessitura.center;
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

    current_pitch = new_pitch;
    prev_note_duration = note_duration;  // Track for leap preparation
  }

  // POST-GENERATION: Resolve melodically isolated notes before returning
  melody::resolveIsolatedNotes(result.notes, harmony, ctx.vocal_low, ctx.vocal_high);

  // Re-check chord boundaries after isolated note resolution (pitch may have changed)
  for (auto& note : result.notes) {
    note.duration = melody::clampToChordBoundary(note.start_tick, note.duration, harmony,
                                                  note.note);
  }

  result.last_pitch = current_pitch;
  return result;
}

MelodyDesigner::PhraseResult MelodyDesigner::generateHook(const MelodyTemplate& tmpl,
                                                          Tick hook_start,
                                                          const SectionContext& ctx, int prev_pitch,
                                                          const IHarmonyContext& harmony,
                                                          std::mt19937& rng) {
  PhraseResult result;
  result.notes.clear();

  // Get chord degree at hook start position
  int8_t start_chord_degree = harmony.getChordDegreeAt(hook_start);

  // Initialize base pitch using chord at hook position
  int base_pitch;
  if (prev_pitch < 0) {
    base_pitch = ctx.tessitura.center;
    base_pitch = nearestChordTonePitch(base_pitch, start_chord_degree);
  } else {
    // Snap prev_pitch to current chord's chord tone
    base_pitch = nearestChordTonePitch(prev_pitch, start_chord_degree);
  }
  // Clamp base_pitch to vocal range
  base_pitch =
      std::clamp(base_pitch, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high));

  // Song-level hook fixation: generate and cache hook motif once
  // "Variation is the enemy, Exact is justice" - use the same hook throughout the song
  if (!hook_cache_.chorus_hook.has_value()) {
    StyleMelodyParams hook_params{};
    hook_params.hook_repetition = true;  // Use catchy repetitive style
    hook_cache_.chorus_hook = designChorusHook(hook_params, rng);
  }

  Motif hook = *hook_cache_.chorus_hook;

  // Hybrid approach: blend HookSkeleton contour hint with existing Motif
  // HookSkeleton provides melodic DNA, Motif provides rhythm (maintained)
  // HookIntensity influences skeleton selection: Strong → more Repeat/AscendDrop
  //
  // Hook density gradient: use stronger intensity in first half of sections
  // This makes the beginning more catchy while allowing variety later.
  // Two cached skeletons: one for first half (boosted), one for second half (base)
  uint8_t bar_in_section = static_cast<uint8_t>(
      (hook_start - ctx.section_start) / TICKS_PER_BAR);
  bool is_first_half = (bar_in_section < ctx.section_bars / 2);

  HookSkeleton selected_skeleton;
  if (is_first_half) {
    // First half: use boosted intensity skeleton
    if (!hook_cache_.skeleton.has_value()) {
      HookIntensity boosted = getPositionAwareIntensity(
          ctx.hook_intensity, bar_in_section, ctx.section_bars);
      hook_cache_.skeleton = selectHookSkeleton(ctx.section_type, rng, boosted);
    }
    selected_skeleton = *hook_cache_.skeleton;
  } else {
    // Second half: use base intensity skeleton (more variety)
    if (!hook_cache_.skeleton_later.has_value()) {
      hook_cache_.skeleton_later = selectHookSkeleton(ctx.section_type, rng, ctx.hook_intensity);
    }
    selected_skeleton = *hook_cache_.skeleton_later;
  }
  SkeletonPattern skeleton_contour = getSkeletonPattern(selected_skeleton);

  // Blend skeleton contour with Motif contour (Motif 80%, Skeleton 20%)
  // This preserves the existing system while adding HookSkeleton influence
  for (size_t i = 0; i < hook.contour_degrees.size() && i < skeleton_contour.length; ++i) {
    int8_t skeleton_hint = skeleton_contour.intervals[i % skeleton_contour.length];
    if (skeleton_hint != -128) {  // Skip rest markers
      // Blend: 80% existing Motif, 20% HookSkeleton hint
      int blended = static_cast<int>(hook.contour_degrees[i] * 0.8f + skeleton_hint * 0.2f);
      hook.contour_degrees[i] = static_cast<int8_t>(blended);
    }
  }

  // Select rhythm pattern based on template style (cached per song for consistency)
  if (hook_cache_.rhythm_pattern_idx == SIZE_MAX) {
    hook_cache_.rhythm_pattern_idx = selectHookRhythmPatternIndex(tmpl, rng);
  }
  const HookRhythmPattern& rhythm_pattern = getHookRhythmPatterns()[hook_cache_.rhythm_pattern_idx];

  // Use template settings for repetition count
  uint8_t repeat_count =
      std::clamp(tmpl.hook_repeat_count, static_cast<uint8_t>(2), static_cast<uint8_t>(4));

  // =========================================================================
  // HOOK BETRAYAL: Apply variation based on template threshold
  // =========================================================================
  // Track hook repetitions across the song. Apply betrayal when threshold is
  // reached. Threshold is template-specific:
  //   - threshold=3 (YOASOBI/TikTok): early variation, faster evolution
  //   - threshold=4 (default): standard "3 times same, 4th different" rule
  //   - threshold=5 (ballad): late variation, more consistency
  //   - threshold=0: no betrayal (exact repetition)
  ++hook_cache_.repetition_count;
  HookBetrayal betrayal = HookBetrayal::None;
  uint8_t threshold = tmpl.betrayal_threshold > 0 ? tmpl.betrayal_threshold : 4;
  if (tmpl.betrayal_threshold > 0 && hook_cache_.repetition_count >= threshold &&
      (hook_cache_.repetition_count % threshold) == 0) {
    // Select betrayal type at threshold (and multiples thereof)
    betrayal = selectBetrayal(1, rng);  // 1 = non-first occurrence
  }

  Tick current_tick = hook_start;
  // kMaxMelodicInterval from pitch_utils.h (Major 6th - singable leap limit)

  // Generate hook notes with chord-aware pitch selection
  int prev_hook_pitch = base_pitch;

  // =========================================================================
  // SABI HEAD RESTORATION: Apply cached first 4 pitches for chorus consistency
  // =========================================================================
  // If we have cached sabi pitches, use them for the first 4 notes.
  // This ensures the chorus "hook head" remains consistent across the song.
  bool use_cached_sabi = (hook_cache_.pitches_cached && ctx.section_type == SectionType::Chorus);

  // Track consecutive same notes for J-POP style probability curve
  melody::ConsecutiveSameNoteTracker consecutive_tracker;

  // Track previous note duration for leap preparation
  Tick prev_note_duration = TICKS_PER_BEAT;  // Default to quarter note

  // Use rhythm pattern's note count, but limit by available contour degrees
  size_t contour_limit =
      std::min(static_cast<size_t>(rhythm_pattern.note_count), hook.contour_degrees.size());

  // Track overall note index across all repetitions for sabi caching
  size_t total_note_idx = 0;

  for (uint8_t rep = 0; rep < repeat_count; ++rep) {
    for (size_t i = 0; i < contour_limit; ++i, ++total_note_idx) {
      // Get chord at this note's position
      int8_t note_chord_degree = harmony.getChordDegreeAt(current_tick);

      // Calculate pitch from contour, then snap to current chord
      int pitch = base_pitch + hook.contour_degrees[i % hook.contour_degrees.size()];

      // Apply cached sabi pitches for first 8 notes (if available)
      // This ensures the chorus hook head is consistent across the song
      bool use_cached_rhythm_for_note = false;
      if (use_cached_sabi && total_note_idx < 8) {
        pitch = static_cast<int>(hook_cache_.sabi_pitches[total_note_idx]);
        use_cached_rhythm_for_note = hook_cache_.rhythm_cached;
      }

      // Find nearest chord tone within vocal range and interval constraint
      int max_interval = getMaxMelodicIntervalForSection(ctx.section_type);
      pitch = nearestChordToneWithinInterval(pitch, prev_hook_pitch, note_chord_degree,
                                             max_interval, ctx.vocal_low, ctx.vocal_high,
                                             &ctx.tessitura);

      // Leap preparation principle: constrain leaps after very short notes
      // Same threshold as generateMelodyPhrase for consistency
      constexpr Tick VERY_SHORT_THRESHOLD = TICKS_PER_BEAT / 2;  // 0.5 beats
      constexpr int MAX_LEAP_AFTER_SHORT = 5;                    // Perfect 4th
      if ((rep > 0 || i > 0) && prev_note_duration < VERY_SHORT_THRESHOLD) {
        int leap = std::abs(pitch - prev_hook_pitch);
        if (leap > MAX_LEAP_AFTER_SHORT) {
          pitch = nearestChordToneWithinInterval(pitch, prev_hook_pitch, note_chord_degree,
                                                 MAX_LEAP_AFTER_SHORT, ctx.vocal_low,
                                                 ctx.vocal_high, &ctx.tessitura);
        }
      }

      // Avoid note check: melody should not form tritone/minor2nd with chord tones
      pitch = melody::enforceAvoidNoteConstraint(pitch, note_chord_degree, ctx.vocal_low, ctx.vocal_high);

      // Downbeat chord-tone constraint for hooks
      pitch = melody::enforceDownbeatChordTone(pitch, current_tick, note_chord_degree,
                                                prev_hook_pitch, ctx.vocal_low, ctx.vocal_high,
                                                true);  // disable_singability=true for hooks

      // Apply consecutive same note limit with J-POP probability curve
      melody::applyConsecutiveSameNoteConstraint(
          pitch, consecutive_tracker, prev_hook_pitch, note_chord_degree,
          ctx.vocal_low, ctx.vocal_high, kMaxMelodicInterval, rng);

      // Get duration from rhythm pattern (in eighths, convert to ticks)
      // Use rhythm grid from template for triplet support
      uint8_t eighths = rhythm_pattern.durations[i];
      Tick eighth_unit = getRhythmUnit(tmpl.rhythm_grid, true);
      Tick note_duration = static_cast<Tick>(eighths) * eighth_unit;

      uint8_t velocity = DEFAULT_VELOCITY;
      // Accent based on note position in pattern
      // First note and longer notes get accent (pop music theory: strong-weak alignment)
      if (i == 0 || eighths >= 2) {
        velocity += 10;
      }

      // Vocal-friendly gate processing for hooks using constraint_pipeline
      bool is_pattern_end = (i == contour_limit - 1);
      bool is_repeat_end = (rep == repeat_count - 1) && is_pattern_end;

      // Build gate context for hooks
      melody::GateContext gate_ctx;
      gate_ctx.is_phrase_end = is_repeat_end;
      gate_ctx.is_phrase_start = (i == 0 && rep == 0);
      gate_ctx.note_duration = note_duration;
      gate_ctx.interval_from_prev = std::abs(pitch - prev_hook_pitch);

      // Apply gate ratio
      Tick actual_duration = melody::applyGateRatio(note_duration, gate_ctx);

      // ABSOLUTE CONSTRAINT: Ensure pitch is on scale (prevents chromatic notes)
      pitch = snapToNearestScaleTone(pitch, ctx.key_offset);
      pitch = std::clamp(pitch, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high));

      // FINAL SAFETY CHECK: Re-enforce max interval after all adjustments
      // Downbeat snapping and avoid note checks might have created large intervals
      {
        int section_max_interval = getMaxMelodicIntervalForSection(ctx.section_type);
        int final_interval = std::abs(pitch - prev_hook_pitch);
        if (final_interval > section_max_interval) {
          pitch = nearestChordToneWithinInterval(pitch, prev_hook_pitch, note_chord_degree,
                                                 section_max_interval, ctx.vocal_low, ctx.vocal_high,
                                                 &ctx.tessitura);
          // Defensive clamp to ensure vocal range is respected
          pitch = std::clamp(pitch, static_cast<int>(ctx.vocal_low),
                             static_cast<int>(ctx.vocal_high));
        }
      }

      // Apply cached rhythm (duration/velocity) for sabi consistency
      Tick final_duration = actual_duration;
      uint8_t final_velocity = velocity;
      Tick tick_advance = note_duration;  // Default: advance by pattern duration
      if (use_cached_rhythm_for_note) {
        final_duration = std::max(hook_cache_.sabi_durations[total_note_idx], TICK_SIXTEENTH);
        final_velocity = hook_cache_.sabi_velocities[total_note_idx];
        // For tick advancement, use cached duration to maintain timing consistency
        tick_advance = hook_cache_.sabi_durations[total_note_idx];
      }

      // Apply pitch safety check to avoid collisions with other tracks (e.g., Motif tritone)
      // Use getSafePitchCandidates for unified collision resolution
      auto candidates = getSafePitchCandidates(harmony, static_cast<uint8_t>(pitch), current_tick,
                                                final_duration, TrackRole::Vocal, ctx.vocal_low,
                                                ctx.vocal_high);
      if (candidates.empty()) {
        current_tick += tick_advance;
        continue;  // No safe pitch available
      }

      // Select best candidate preserving hook shape
      PitchSelectionHints hints;
      hints.prev_pitch = static_cast<int8_t>(prev_hook_pitch);
      hints.note_duration = final_duration;
      hints.tessitura_center = ctx.tessitura.center;
      pitch = selectBestCandidate(candidates, static_cast<uint8_t>(pitch), hints);

      NoteEvent hook_note = createNoteWithoutHarmony(
          current_tick, final_duration, static_cast<uint8_t>(pitch), final_velocity);
#ifdef MIDISKETCH_NOTE_PROVENANCE
      hook_note.prov_source = static_cast<uint8_t>(NoteSource::Hook);
      hook_note.prov_chord_degree = note_chord_degree;
      hook_note.prov_lookup_tick = current_tick;
      hook_note.prov_original_pitch = static_cast<uint8_t>(pitch);
#endif
      result.notes.push_back(hook_note);

      prev_hook_pitch = pitch;
      prev_note_duration = final_duration;  // Track for leap preparation
      current_tick += tick_advance;
    }

    // Add gap after pattern (varies by pattern for natural breathing)
    current_tick += rhythm_pattern.gap_after;
  }

  // =========================================================================
  // APPLY HOOK BETRAYAL: Modify pitches/durations for the 4th occurrence
  // =========================================================================
  // Apply betrayal modifications to the generated notes.
  // This adds subtle variation while maintaining hook recognizability.
  if (betrayal != HookBetrayal::None && !result.notes.empty()) {
    std::vector<int8_t> pitches;
    std::vector<Tick> durations;
    pitches.reserve(result.notes.size());
    durations.reserve(result.notes.size());
    for (const auto& note : result.notes) {
      pitches.push_back(static_cast<int8_t>(note.note));
      durations.push_back(note.duration);
    }

    applyBetrayal(pitches, durations, betrayal, rng);

    // Apply modifications back to notes (clamped to vocal range)
    // Also add pitch safety check and record original pitch in provenance
    for (size_t i = 0; i < result.notes.size() && i < pitches.size(); ++i) {
      [[maybe_unused]] uint8_t old_pitch = result.notes[i].note;
      int new_pitch = std::clamp(static_cast<int>(pitches[i]),
                                 static_cast<int>(ctx.vocal_low),
                                 static_cast<int>(ctx.vocal_high));

      // Apply pitch safety check for modified pitch
      // Use getSafePitchCandidates for unified collision resolution
      auto candidates = getSafePitchCandidates(harmony, static_cast<uint8_t>(new_pitch),
                                                result.notes[i].start_tick,
                                                result.notes[i].duration,
                                                TrackRole::Vocal, ctx.vocal_low, ctx.vocal_high);
      if (!candidates.empty()) {
        // Select best candidate considering neighboring notes for melodic continuity
        PitchSelectionHints hints;
        if (i > 0) {
          hints.prev_pitch = static_cast<int8_t>(result.notes[i - 1].note);
        }
        hints.note_duration = result.notes[i].duration;
        hints.tessitura_center = ctx.tessitura.center;
        result.notes[i].note = selectBestCandidate(candidates, static_cast<uint8_t>(new_pitch), hints);
      }
      // If candidates is empty, keep the original clamped pitch (new_pitch)

#ifdef MIDISKETCH_NOTE_PROVENANCE
      // Record original pitch before betrayal modification
      if (old_pitch != result.notes[i].note) {
        result.notes[i].prov_original_pitch = old_pitch;
        result.notes[i].addTransformStep(TransformStepType::MotionAdjust, old_pitch,
                                          result.notes[i].note, 0, 0);
      }
#endif

      if (i < durations.size()) {
        result.notes[i].duration = durations[i];
      }

      // Re-check chord boundary after betrayal pitch/duration changes
      auto boundary_info = harmony.analyzeChordBoundary(
          result.notes[i].note, result.notes[i].start_tick, result.notes[i].duration);
      if (boundary_info.boundary_tick > 0 &&
          boundary_info.overlap_ticks >= TICK_EIGHTH &&
          (boundary_info.safety == CrossBoundarySafety::NonChordTone ||
           boundary_info.safety == CrossBoundarySafety::AvoidNote) &&
          boundary_info.safe_duration >= TICK_SIXTEENTH) {
        result.notes[i].duration = boundary_info.safe_duration;
      }
    }
  }

  // =========================================================================
  // SABI (CHORUS) HEAD CACHING: Store first 8 pitches, durations, velocities
  // =========================================================================
  // Cache the first 8 notes of the chorus hook for reuse in subsequent
  // chorus sections. This ensures the "sabi" (hook head) is memorable.
  // Rhythm (duration + velocity) is also cached for complete consistency.
  if (!hook_cache_.pitches_cached && ctx.section_type == SectionType::Chorus &&
      result.notes.size() >= 8) {
    for (size_t i = 0; i < 8 && i < result.notes.size(); ++i) {
      hook_cache_.sabi_pitches[i] = result.notes[i].note;
      hook_cache_.sabi_durations[i] = result.notes[i].duration;
      hook_cache_.sabi_velocities[i] = result.notes[i].velocity;
    }
    hook_cache_.pitches_cached = true;
    hook_cache_.rhythm_cached = true;
  }

  // Return last pitch for smooth transition to next phrase
  result.last_pitch = prev_hook_pitch;
  result.direction_inertia = 0;  // Reset inertia after hook

  return result;
}

// getDirectionBiasForContour moved to contour_direction.cpp

PitchChoice MelodyDesigner::selectPitchChoice(const MelodyTemplate& tmpl, float phrase_pos,
                                              bool has_target, SectionType section_type,
                                              std::mt19937& rng, float note_eighths,
                                              std::optional<ContourType> forced_contour) {
  return selectPitchChoiceImpl(tmpl, phrase_pos, has_target, section_type, rng, note_eighths,
                               forced_contour);
}

PitchChoice MelodyDesigner::applyDirectionInertia(PitchChoice choice, int inertia,
                                                  const MelodyTemplate& tmpl, std::mt19937& rng) {
  return applyDirectionInertiaImpl(choice, inertia, tmpl, rng);
}

float MelodyDesigner::getEffectivePlateauRatio(const MelodyTemplate& tmpl, int current_pitch,
                                               const TessituraRange& tessitura) {
  return getEffectivePlateauRatioImpl(tmpl, current_pitch, tessitura);
}

bool MelodyDesigner::shouldLeap(LeapTrigger trigger, float phrase_pos, float section_pos) {
  return shouldLeapImpl(trigger, phrase_pos, section_pos);
}

int MelodyDesigner::getStabilizeStep(int leap_direction, int max_step) {
  return getStabilizeStepImpl(leap_direction, max_step);
}

bool MelodyDesigner::isInSameVowelSection(float pos1, float pos2, uint8_t phrase_length) {
  return isInSameVowelSectionImpl(pos1, pos2, phrase_length);
}

int8_t MelodyDesigner::getMaxStepInVowelSection(bool in_same_vowel) {
  return getMaxStepInVowelSectionImpl(in_same_vowel);
}

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

#ifdef MIDISKETCH_NOTE_PROVENANCE
    uint8_t old_pitch = note.note;
#endif

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

  // Maximum allowed interval (major 6th = 9 semitones)
  // kMaxMelodicInterval from pitch_utils.h

  // Find the last note
  auto& last_note = notes.back();

  // Leading tone: one semitone below the expected first note of next section
  // In C major, this is typically B (11) leading to C (0)
  // We approximate by using a semitone below the current tessitura center
  int leading_pitch = ctx.tessitura.center - 1;

  // Ensure it's within range
  if (leading_pitch < static_cast<int>(ctx.vocal_low)) {
    leading_pitch = ctx.vocal_low;
  }
  if (leading_pitch > static_cast<int>(ctx.vocal_high)) {
    leading_pitch = ctx.vocal_high;
  }

  // Check interval constraint with last note
  int interval = std::abs(leading_pitch - static_cast<int>(last_note.note));
  if (interval > kMaxMelodicInterval) {
    // Skip inserting leading tone if interval is too large
    return;
  }

  // Insert a short leading tone just before section end
  // Only if there's space and the last note ends before section end
  Tick last_note_end = last_note.start_tick + last_note.duration;
  Tick leading_tone_start = ctx.section_end - TICKS_PER_BEAT / 4;  // 16th note before end

  // Skip if gap is too large - leading tone needs melodic context
  // An isolated note after a long gap sounds unnatural
  constexpr Tick MAX_GAP = TICKS_PER_BEAT / 2;  // Half beat (8th note gap max)
  if (leading_tone_start > last_note_end && leading_tone_start - last_note_end > MAX_GAP) {
    return;
  }

  if (last_note_end <= leading_tone_start) {
    // Check pitch safety before adding leading tone
    Tick leading_duration = TICKS_PER_BEAT / 4;
    if (!harmony.isPitchSafe(static_cast<uint8_t>(leading_pitch), leading_tone_start,
                             leading_duration, TrackRole::Vocal)) {
      return;  // Skip leading tone if it would cause dissonance
    }

    uint8_t velocity = static_cast<uint8_t>(
        std::min(127, static_cast<int>(last_note.velocity) + 10));  // Slightly louder

    NoteEvent leading_note = createNoteWithoutHarmony(leading_tone_start, leading_duration,
                                                       static_cast<uint8_t>(leading_pitch), velocity);
#ifdef MIDISKETCH_NOTE_PROVENANCE
    leading_note.prov_source = static_cast<uint8_t>(NoteSource::MelodyPhrase);
    leading_note.prov_chord_degree = harmony.getChordDegreeAt(leading_tone_start);
    leading_note.prov_lookup_tick = leading_tone_start;
    leading_note.prov_original_pitch = static_cast<uint8_t>(leading_pitch);
#endif
    notes.push_back(leading_note);
  }
}

int MelodyDesigner::applyPitchChoice(PitchChoice choice, int current_pitch, int target_pitch,
                                     int8_t chord_degree, int key_offset, uint8_t vocal_low,
                                     uint8_t vocal_high, VocalAttitude attitude,
                                     bool disable_singability, float note_eighths) {
  return applyPitchChoiceImpl(choice, current_pitch, target_pitch, chord_degree, key_offset,
                              vocal_low, vocal_high, attitude, disable_singability, note_eighths);
}

int MelodyDesigner::calculateTargetPitch(const MelodyTemplate& tmpl, const SectionContext& ctx,
                                         [[maybe_unused]] int current_pitch,
                                         const IHarmonyContext& harmony,
                                         [[maybe_unused]] std::mt19937& rng) {
  return calculateTargetPitchImpl(tmpl, ctx.tessitura.center, tmpl.tessitura_range, ctx.vocal_low,
                                  ctx.vocal_high, ctx.section_start, harmony);
}

std::vector<RhythmNote> MelodyDesigner::generatePhraseRhythm(
    const MelodyTemplate& tmpl, uint8_t phrase_beats, float density_modifier,
    float thirtysecond_ratio, std::mt19937& rng, GenerationParadigm paradigm,
    float syncopation_weight, SectionType section_type) {
  return generatePhraseRhythmImpl(tmpl, phrase_beats, density_modifier, thirtysecond_ratio, rng,
                                  paradigm, syncopation_weight, section_type);
}

// === Locked Rhythm Pitch Selection ===

uint8_t MelodyDesigner::selectPitchForLockedRhythm(uint8_t prev_pitch, int8_t chord_degree,
                                                   uint8_t vocal_low, uint8_t vocal_high,
                                                   std::mt19937& rng) {
  return selectPitchForLockedRhythmImpl(prev_pitch, chord_degree, vocal_low, vocal_high, rng);
}

// === GlobalMotif Support ===

GlobalMotif MelodyDesigner::extractGlobalMotif(const std::vector<NoteEvent>& notes) {
  return extractGlobalMotifImpl(notes);
}

float MelodyDesigner::evaluateWithGlobalMotif(const std::vector<NoteEvent>& candidate,
                                              const GlobalMotif& global_motif) {
  return evaluateWithGlobalMotifImpl(candidate, global_motif);
}

// ============================================================================
// Section-Specific Motif Variants
// ============================================================================

void MelodyDesigner::prepareMotifVariants(const GlobalMotif& source) {
  motif_variants_.clear();

  if (!source.isValid()) {
    return;
  }

  // Chorus: use original motif (strongest recognition)
  motif_variants_[SectionType::Chorus] = source;

  // A section: diminished rhythm (slightly faster feel for verses)
  motif_variants_[SectionType::A] =
      transformGlobalMotif(source, GlobalMotifTransform::Diminish);

  // B section: sequenced up (building tension toward chorus)
  motif_variants_[SectionType::B] =
      transformGlobalMotif(source, GlobalMotifTransform::Sequence, 2);

  // Bridge: inverted contour (maximum contrast)
  motif_variants_[SectionType::Bridge] =
      transformGlobalMotif(source, GlobalMotifTransform::Invert);

  // Outro: fragmented (winding down, partial recall)
  motif_variants_[SectionType::Outro] =
      transformGlobalMotif(source, GlobalMotifTransform::Fragment);

  // Intro/Interlude: retrograde (instrumental interest)
  motif_variants_[SectionType::Intro] =
      transformGlobalMotif(source, GlobalMotifTransform::Retrograde);
  motif_variants_[SectionType::Interlude] =
      transformGlobalMotif(source, GlobalMotifTransform::Retrograde);

  // Chant/MixBreak: augmented rhythm (emphasized, slower feel)
  motif_variants_[SectionType::Chant] =
      transformGlobalMotif(source, GlobalMotifTransform::Augment);
  motif_variants_[SectionType::MixBreak] =
      transformGlobalMotif(source, GlobalMotifTransform::Augment);
}

const GlobalMotif& MelodyDesigner::getMotifForSection(SectionType section_type) const {
  auto it = motif_variants_.find(section_type);
  if (it != motif_variants_.end()) {
    return it->second;
  }

  // Fallback to original motif if variant not found
  if (cached_global_motif_.has_value()) {
    return cached_global_motif_.value();
  }

  // Return a static empty motif if nothing available
  static const GlobalMotif empty_motif;
  return empty_motif;
}

}  // namespace midisketch
