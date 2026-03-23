/**
 * @file melody_hook.cpp
 * @brief MelodyDesigner::generateHook() implementation.
 *
 * Extracted from melody_designer.cpp for modularity.
 */

#include "track/vocal/melody_designer.h"

#include <algorithm>
#include <cmath>

#include "core/chord_utils.h"
#include "core/hook_utils.h"
#include "core/i_harmony_context.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "track/melody/constraint_pipeline.h"
#include "track/melody/hook_rhythm_patterns.h"
#include "track/melody/melody_utils.h"
#include "track/melody/note_constraints.h"
#include "track/melody/pitch_constraints.h"
#include "track/melody/rhythm_generator.h"

namespace midisketch {

using melody::getRhythmUnit;
using melody::HookRhythmPattern;
using melody::getHookRhythmPatterns;
using melody::selectHookRhythmPatternIndex;

namespace {

// Default velocity for melody notes (must match melody_designer.cpp)
constexpr uint8_t DEFAULT_VELOCITY = 100;

}  // namespace

MelodyDesigner::PhraseResult MelodyDesigner::generateHook(const MelodyTemplate& tmpl,
                                                          Tick hook_start, Tick phrase_end,
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
      tickToBar(hook_start - ctx.section_start));
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
    if (current_tick >= phrase_end) break;
    for (size_t i = 0; i < contour_limit; ++i, ++total_note_idx) {
      if (current_tick >= phrase_end) break;
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

      // NOTE: applyConsecutiveSameNoteConstraint moved after selectBestCandidate
      // to ensure the final pitch (after collision avoidance) is checked

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
      Tick tick_advance = note_duration;  // Default: advance by pattern duration (pre-gate)
      if (use_cached_rhythm_for_note) {
        final_duration = std::max(hook_cache_.sabi_durations[total_note_idx], TICK_SIXTEENTH);
        final_velocity = hook_cache_.sabi_velocities[total_note_idx];
        // For tick advancement, use pre-gate duration to maintain grid-aligned timing
        tick_advance = hook_cache_.sabi_tick_advances[total_note_idx];
      }

      // Clamp duration so hook notes never exceed phrase boundary
      if (current_tick + final_duration > phrase_end) {
        final_duration = phrase_end - current_tick;
        if (final_duration < TICK_SIXTEENTH) break;  // Too short to be singable
      }

      // Apply pitch safety check to avoid collisions with other tracks (e.g., Motif tritone)
      // Use getSafePitchCandidates for unified collision resolution
      auto candidates = getSafePitchCandidates(harmony, static_cast<uint8_t>(pitch), current_tick,
                                                final_duration, TrackRole::Vocal, ctx.vocal_low,
                                                ctx.vocal_high);
      if (candidates.empty()) {
        // First note of hook MUST exist — try chord tones as fallback
        if (i == 0 && rep == 0) {
          uint8_t fallback_pitch = static_cast<uint8_t>(
              nearestChordTonePitch(pitch, note_chord_degree));
          fallback_pitch = static_cast<uint8_t>(std::clamp(
              static_cast<int>(fallback_pitch),
              static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high)));
          PitchCandidate fallback;
          fallback.pitch = fallback_pitch;
          fallback.max_safe_duration = final_duration;
          fallback.is_chord_tone = true;
          candidates.push_back(fallback);
        } else {
          current_tick += tick_advance;
          continue;  // No safe pitch available
        }
      }

      // Select best candidate preserving hook shape
      PitchSelectionHints hints;
      hints.prev_pitch = static_cast<int8_t>(prev_hook_pitch);
      hints.note_duration = final_duration;
      hints.tessitura_center = ctx.tessitura.center;
      hints.section_type = static_cast<int8_t>(ctx.section_type);
      hints.sub_phrase_index = static_cast<int8_t>(ctx.sub_phrase_index);
      hints.same_pitch_streak = static_cast<int8_t>(consecutive_tracker.count);  // Pass streak
      pitch = selectBestCandidate(candidates, static_cast<uint8_t>(pitch), hints);

      // Apply consecutive same note limit AFTER final pitch selection
      // This ensures we catch cases where collision avoidance re-selected the same pitch
      melody::applyConsecutiveSameNoteConstraint(
          pitch, consecutive_tracker, prev_hook_pitch, note_chord_degree,
          ctx.vocal_low, ctx.vocal_high, kMaxMelodicInterval, rng);

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
        hints.section_type = static_cast<int8_t>(ctx.section_type);
        hints.sub_phrase_index = static_cast<int8_t>(ctx.sub_phrase_index);
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
  // NOTE: sabi_tick_advances stores pre-gate durations for grid-aligned timing.
  if (!hook_cache_.pitches_cached && ctx.section_type == SectionType::Chorus &&
      result.notes.size() >= 8) {
    Tick eighth_unit = getRhythmUnit(tmpl.rhythm_grid, true);
    for (size_t i = 0; i < 8 && i < result.notes.size(); ++i) {
      hook_cache_.sabi_pitches[i] = result.notes[i].note;
      hook_cache_.sabi_durations[i] = result.notes[i].duration;
      hook_cache_.sabi_velocities[i] = result.notes[i].velocity;
      // Store pre-gate duration (from rhythm pattern) for grid-aligned tick advancement
      size_t pattern_idx = i % contour_limit;
      hook_cache_.sabi_tick_advances[i] =
          static_cast<Tick>(rhythm_pattern.durations[pattern_idx]) * eighth_unit;
    }
    hook_cache_.pitches_cached = true;
    hook_cache_.rhythm_cached = true;
  }

  // Absorb short final note into previous note when chord-boundary clipping
  // or gate ratio creates a very short hook ending (< quarter note).
  if (result.notes.size() >= 2) {
    auto& last = result.notes.back();
    if (last.duration < TICK_QUARTER) {
      auto& prev = result.notes[result.notes.size() - 2];
      Tick target_end = last.start_tick + last.duration;
      Tick extended_dur = target_end - prev.start_tick;
      Tick safe_dur = melody::clampToChordBoundary(prev.start_tick, extended_dur, harmony,
                                                    prev.note);
      if (safe_dur > prev.duration) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
        Tick old_dur = prev.duration;
        prev.addTransformStep(TransformStepType::ChordBoundaryClip,
                              static_cast<uint8_t>(old_dur >> 8),
                              static_cast<uint8_t>(safe_dur >> 8), 0, 0);
#endif
        prev.duration = safe_dur;
        result.notes.pop_back();
        prev_hook_pitch = result.notes.back().note;
      } else {
        // Can't extend prev across chord boundary.
        // Remove the short trailing note — phrase resolves on the previous sustained note.
        result.notes.pop_back();
        prev_hook_pitch =
            result.notes.empty() ? prev_hook_pitch : result.notes.back().note;
      }
    }
  }

  // Return last pitch for smooth transition to next phrase
  result.last_pitch = prev_hook_pitch;
  result.direction_inertia = 0;  // Reset inertia after hook

  return result;
}

}  // namespace midisketch
