/**
 * @file vocal.cpp
 * @brief Vocal melody track generation with phrase caching and variation.
 *
 * Phrase-based approach: each section generates/reuses cached phrases with
 * subtle variations for varied repetition (scale degrees, singability, cadences).
 */

#include "track/generators/vocal.h"

#include <algorithm>
#include <set>
#include <unordered_map>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/melody_embellishment.h"
#include "core/melody_evaluator.h"
#include "core/melody_templates.h"
#include "core/mood_utils.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/note_timeline_utils.h"
#include "core/pitch_bend_curves.h"
#include "core/pitch_utils.h"
#include "core/production_blueprint.h"
#include "core/rng_util.h"
#include "core/song.h"
#include "core/velocity.h"
#include "track/generators/motif.h"
#include "track/melody/melody_utils.h"
#include "track/melody/motif_support.h"
#include "track/melody/rhythm_generator.h"
#include "track/vocal/locked_rhythm_generator.h"
#include "track/vocal/melody_designer.h"
#include "track/vocal/phrase_cache.h"
#include "track/vocal/phrase_planner.h"
#include "track/vocal/phrase_variation.h"
#include "track/vocal/rhythm_lock_evaluator.h"
#include "track/vocal/vocal_helpers.h"
#include "track/vocal/vocal_pitch_hints.h"
#include "track/vocal/vocal_post_process.h"
#include "track/vocal/vocal_range.h"

namespace midisketch {

// Note: enforceVocalPitchConstraints, breakConsecutiveSamePitch,
// breakSameDirectionLeapChains,
// and applyVocalPitchBendExpressions are in track/vocal/vocal_post_process.cpp

// ============================================================================
// Rhythm Lock Support
// ============================================================================

bool shouldLockVocalRhythm(const GeneratorParams& params) {
  // Rhythm lock is used for RhythmSync style:
  // - RhythmSync paradigm (vocal syncs to drum grid)
  // - Locked riff policy (same rhythm throughout)
  if (params.paradigm != GenerationParadigm::RhythmSync) {
    return false;
  }
  // RiffPolicy::Locked is an alias for LockedContour, so check the underlying values
  uint8_t policy_value = static_cast<uint8_t>(params.riff_policy);
  // LockedContour=1, LockedPitch=2, LockedAll=3
  return policy_value >= 1 && policy_value <= 3;
}

// Check if rhythm lock should be per-section-type (for UltraVocaloid)
// UltraVocaloid needs different rhythms per section type (ballad verse + machine-gun chorus)
// but still wants consistency within the same section type
static bool shouldUsePerSectionTypeRhythmLock(const GeneratorParams& params) {
  return params.vocal_style == VocalStylePreset::UltraVocaloid;
}

// =============================================================================
// Extracted sub-methods for doGenerateFullTrack decomposition
// =============================================================================

MelodyDesigner::SectionContext VocalGenerator::buildSectionContext(
    const Section& section, const GeneratorParams& params, const Song& song,
    const TessituraRange& tessitura, uint8_t vocal_low, uint8_t vocal_high, int8_t chord_degree,
    int occurrence, const DrumGrid* drum_grid, const MelodyDesigner& designer) const {
  MelodyDesigner::SectionContext sctx;
  sctx.section_type = section.type;
  sctx.section_start = section.start_tick;
  sctx.section_end = section.endTick();
  sctx.section_bars = section.bars;
  sctx.chord_degree = chord_degree;
  sctx.key_offset = 0;  // Always C major internally
  sctx.tessitura = tessitura;
  sctx.vocal_low = vocal_low;
  sctx.vocal_high = vocal_high;
  sctx.mood = params.mood;  // For harmonic rhythm alignment
  sctx.bpm = params.bpm;
  // Apply section's density_percent to density modifier (with SectionModifier)
  float base_density = getDensityModifier(section.type, params.melody_params);
  uint8_t effective_density = section.getModifiedDensity(section.density_percent);
  float density_factor = effective_density / 100.0f;
  sctx.density_modifier = base_density * density_factor;
  sctx.thirtysecond_ratio = getThirtysecondRatio(section.type, params.melody_params);
  sctx.consecutive_same_note_prob = getConsecutiveSameNoteProb(section.type, params.melody_params);
  sctx.disable_vowel_constraints = params.melody_params.disable_vowel_constraints;
  sctx.disable_breathing_gaps = params.melody_params.disable_breathing_gaps;
  // Wire StyleMelodyParams zombie parameters to SectionContext
  sctx.chorus_long_tones = params.melody_params.chorus_long_tones;
  sctx.allow_bar_crossing = params.melody_params.allow_bar_crossing;
  sctx.min_note_division = params.melody_params.min_note_division;
  // High-energy idol at fast BPM: allow 16th notes (enables run_window effect)
  // min_note_division: higher = finer allowed (8=eighth, 16=sixteenth, 32=thirty-second)
  if (params.bpm >= 145 && isHighEnergyVocalStyle(params.vocal_style) &&
      sctx.min_note_division > 0 && sctx.min_note_division < 16) {
    sctx.min_note_division = 16;
  }
  sctx.tension_usage = params.melody_params.tension_usage;
  sctx.syncopation_prob = params.melody_params.syncopation_prob;
  if (params.melody_long_note_ratio_override) {
    sctx.long_note_ratio_override = params.melody_params.long_note_ratio;
  }
  sctx.phrase_length_bars = params.melody_params.phrase_length_bars;
  // allow_unison_repeat: when false, hard-disable consecutive same notes
  if (!params.melody_params.allow_unison_repeat) {
    sctx.consecutive_same_note_prob = 0.0f;
  }
  // note_density: apply as additional multiplier to density_modifier
  sctx.density_modifier *= params.melody_params.note_density;
  sctx.vocal_attitude = params.vocal_attitude;
  sctx.hook_intensity = params.hook_intensity;  // For HookSkeleton selection
  sctx.hook_repetition = params.melody_params.hook_repetition;
  // RhythmSync support
  sctx.paradigm = params.paradigm;
  sctx.drum_grid = drum_grid;
  // Motif template for accent-linked velocity (RhythmSync)
  if (params.paradigm == GenerationParadigm::RhythmSync) {
    sctx.motif_params = &params.motif;
  }
  // Vocal groove feel for syncopation control
  sctx.vocal_groove = params.vocal_groove;
  // Syncopation enable flag
  sctx.enable_syncopation = params.enable_syncopation;
  // Drive feel for timing and syncopation modulation
  sctx.drive_feel = params.drive_feel;

  // Vocal style for physics parameters (breath, timing, pitch bend)
  sctx.vocal_style = params.vocal_style;

  // Syllabic subdivision parameters
  sctx.syllabic_sub_ratio = getSubdivisionRatio(section.type, params.melody_params);
  auto resolved_mora =
      melody::resolveMoraMode(params.melody_params.mora_rhythm_mode, params.vocal_style);
  sctx.is_mora_timed = (resolved_mora == MoraRhythmMode::MoraTimed);

  // Occurrence count for occurrence-dependent embellishment density
  sctx.section_occurrence = occurrence;

  // Apply melodic leap constraint: user override > blueprint > default
  sctx.max_leap_semitones = melody::resolveContextMaxLeap(params);
  if (params.blueprint_ref != nullptr) {
    sctx.prefer_stepwise = params.blueprint_ref->constraints.prefer_stepwise;
  }

  // Wire guide tone rate from section
  sctx.guide_tone_rate = section.guide_tone_rate;

  // Set anticipation rest mode based on groove feel and drive
  // Driving/Syncopated grooves benefit from anticipation rests for "tame" effect
  // Higher drive_feel increases anticipation intensity
  if (params.vocal_groove == VocalGrooveFeel::Driving16th) {
    sctx.anticipation_rest =
        (params.drive_feel >= 70) ? AnticipationRestMode::Moderate : AnticipationRestMode::Subtle;
  } else if (params.vocal_groove == VocalGrooveFeel::Syncopated) {
    sctx.anticipation_rest = AnticipationRestMode::Moderate;
  } else if (params.drive_feel >= 80) {
    // High drive with any groove gets subtle anticipation
    sctx.anticipation_rest = AnticipationRestMode::Subtle;
  }

  // Set phrase contour template based on section type
  // Common J-POP practice:
  // - Chorus: Peak (arch shape) for memorable hook contour
  // - A (Verse): Ascending for storytelling build
  // - B (Pre-chorus): Ascending to build tension before chorus
  // - Bridge: Descending for contrast
  switch (section.type) {
    case SectionType::Chorus:
      sctx.forced_contour = ContourType::Peak;
      break;
    case SectionType::A:
      sctx.forced_contour = ContourType::Ascending;
      break;
    case SectionType::B:
      sctx.forced_contour = ContourType::Ascending;
      break;
    case SectionType::Bridge:
      sctx.forced_contour = ContourType::Descending;
      break;
    default:
      sctx.forced_contour = std::nullopt;  // Use default section-aware bias
      break;
  }

  // Enable motif fragment enforcement for A/B sections after first chorus
  // This creates song-wide melodic unity by echoing chorus motif fragments
  if (designer.getCachedGlobalMotif().has_value() &&
      (section.type == SectionType::A || section.type == SectionType::B)) {
    sctx.enforce_motif_fragments = true;
  }

  // Set transition info for next section (if any)
  const auto& sections = song.arrangement().sections();
  for (size_t idx = 0; idx < sections.size(); ++idx) {
    if (&sections[idx] == &section && idx + 1 < sections.size()) {
      sctx.transition_to_next = getTransition(section.type, sections[idx + 1].type);
      break;
    }
  }

  return sctx;
}

CachedRhythmPattern* VocalGenerator::resolveRhythmLock(
    const Section& section, const GeneratorParams& params, const Song& song,
    const FullTrackContext& ctx, CachedRhythmPattern& motif_storage, bool use_per_section_type_lock,
    std::unordered_map<SectionType, CachedRhythmPattern>& section_type_locks,
    CachedRhythmPattern* active_rhythm_lock, Tick section_start, Tick section_end) const {
  CachedRhythmPattern* current_rhythm_lock = nullptr;

  // RhythmSync paradigm: extract rhythm from Motif track (coordinate axis)
  // Try ctx.motif_track first (from Coordinator), then fall back to motif_track_ member
  const MidiTrack* motif_ref = ctx.motif_track;
  if (motif_ref == nullptr) {
    motif_ref = motif_track_;
  }
  if (params.paradigm == GenerationParadigm::RhythmSync && motif_ref != nullptr &&
      !motif_ref->empty()) {
    // Extract Motif's rhythm pattern for this section
    motif_storage = extractRhythmPatternFromTrack(motif_ref->notes(), section_start, section_end);
    if (motif_storage.isValid()) {
      current_rhythm_lock = &motif_storage;
    }
  }

  // Fallback: use stored Motif base pattern (available even when Motif is muted)
  if (current_rhythm_lock == nullptr && params.paradigm == GenerationParadigm::RhythmSync) {
    const auto& base_pattern = song.motifPattern();
    if (!base_pattern.empty()) {
      uint8_t pattern_beats = static_cast<uint8_t>(
          (base_pattern.back().start_tick + base_pattern.back().duration + TICKS_PER_BEAT - 1) /
          TICKS_PER_BEAT);
      if (pattern_beats > 0) {
        motif_storage = extractRhythmPattern(base_pattern, 0, pattern_beats);
        if (motif_storage.isValid()) {
          current_rhythm_lock = &motif_storage;
        }
      }
    }
  }

  // Fallback: use cached Vocal rhythm if Motif pattern not available
  if (current_rhythm_lock == nullptr) {
    if (use_per_section_type_lock) {
      // Per-section-type lock: look up by section type
      auto iter = section_type_locks.find(section.type);
      if (iter != section_type_locks.end() && iter->second.isValid()) {
        current_rhythm_lock = &iter->second;
      }
    } else if (active_rhythm_lock->isValid()) {
      // Global lock: use single rhythm pattern
      current_rhythm_lock = active_rhythm_lock;
    }
  }

  return current_rhythm_lock;
}

void VocalGenerator::postProcessVocalNotes(
    std::vector<NoteEvent>& all_notes, MidiTrack& track, const Song& song,
    const GeneratorParams& params, IHarmonyContext& harmony, std::mt19937& rng,
    float velocity_scale, uint8_t effective_vocal_low, uint8_t effective_vocal_high,
    const std::vector<SectionCeiling>& section_ceilings) const {
  // Apply section-end sustain - extend final notes of each section
  applySectionEndSustain(all_notes, song.arrangement().sections(), harmony);

  // Apply groove feel timing adjustments
  applyGrooveFeel(all_notes, params.vocal_groove);

  // Remove overlapping notes
  // UltraVocaloid allows 32nd notes (60 ticks), standard vocals need 16th notes (120 ticks)
  Tick min_note_duration =
      (params.vocal_style == VocalStylePreset::UltraVocaloid) ? TICK_32ND : TICK_SIXTEENTH;
  NoteTimeline::fixOverlapsWithMinDuration(all_notes, min_note_duration);

  // Safety net: enforce maximum phrase duration for very long sections.
  // Inter-section breaths are handled during generation; this catches edge cases
  // (e.g. RhythmSync seeds whose onsets do not align with planned phrase
  // boundaries) where notes run on for a whole section without a breath.
  // The cap is tightened to 6 bars so the continuous-singing span stays within
  // the breathability budget (the analyzer flags spans over ~24 beats / 6 bars),
  // while never exceeding the style's physical max_phrase_bars.
  VocalPhysicsParams physics = getVocalPhysicsParams(params.vocal_style);
  if (physics.requires_breath && physics.max_phrase_bars < 255) {
    constexpr uint8_t kBreathabilityMaxBars = 6;
    uint8_t safety_bars = std::min(physics.max_phrase_bars, kBreathabilityMaxBars);
    melody::enforceMaxPhraseDuration(all_notes, safety_bars, TICK_EIGHTH);
  }

  // Vocal-friendly post-processing:
  // Merge same-pitch notes with BPM-aware gap threshold.
  // At fast tempos, gate_ratio creates larger tick gaps that should still be merged.
  // SKIP for UltraVocaloid: same-pitch rapid-fire is intentional (machine-gun style)
  // SKIP for RhythmSync locked-rhythm leads: the chanted same-pitch runs ARE the
  // style (vocaloid/anison references sing 4.87-10.34 notes/bar); merging them
  // collapsed the lead to ~2 notes/bar.
  bool keep_same_pitch_runs = params.vocal_style == VocalStylePreset::UltraVocaloid ||
                              (params.paradigm == GenerationParadigm::RhythmSync &&
                               params.motif.rhythm_template != MotifRhythmTemplate::None);
  if (!keep_same_pitch_runs) {
    // BPM-aware merge gap: ~50ms in real time, minimum 30 ticks
    Tick merge_gap =
        static_cast<Tick>(std::max(30.0f, 0.05f * params.bpm * TICKS_PER_BEAT / 60.0f));
    mergeSamePitchNotes(all_notes, merge_gap);
  }

  // NOTE: resolveIsolatedShortNotes() removed - short notes are often
  // intentional articulation (staccato bursts, rhythmic motifs).

  // Apply velocity scale
  applyVelocityBalance(all_notes, velocity_scale);

  // Enforce pitch constraints (interval limits and scale enforcement)
  enforceVocalPitchConstraints(all_notes, params, harmony, &song.arrangement().sections());

  // Break up excessive consecutive same-pitch notes (RhythmSync compatibility)
  // This addresses monotonous melody issues in RhythmSync paradigm where
  // collision avoidance can cause long runs of the same pitch.
  // max_consecutive=3 means 4th note onwards gets alternated for melodic interest.
  uint8_t post_process_max_leap = melody::resolveContextMaxLeap(params);
  breakConsecutiveSamePitch(all_notes, harmony, effective_vocal_low, effective_vocal_high, 3,
                            &song.arrangement().sections(), post_process_max_leap);
  breakSameDirectionLeapChains(all_notes, harmony, effective_vocal_low, effective_vocal_high);

  // Re-enforce per-section ceilings AFTER all song-wide pitch passes. Earlier
  // passes (enforceVocalPitchConstraints, breakConsecutiveSamePitch) operate on
  // the global effective range and can lift non-Chorus notes back above their
  // section ceiling, which would let the global melodic peak escape the Chorus.
  // This pass is the final authority on non-Chorus ceilings: it clamps each note
  // in place using its owning section's bounds (no reordering, no aliasing).
  for (auto& note : all_notes) {
    for (const auto& sc : section_ceilings) {
      if (sc.is_chorus) continue;
      if (note.start_tick < sc.start_tick || note.start_tick >= sc.end_tick) continue;
      if (note.note <= sc.high) break;
      // Single-note slice keeps enforceSectionCeiling's octave-drop + scale-snap
      // + collision-safety logic as the single source of truth for the clamp.
      std::vector<NoteEvent> one{note};
      enforceSectionCeiling(one, harmony, sc.low, sc.high);
      note.note = one.front().note;
#ifdef MIDISKETCH_NOTE_PROVENANCE
      note.prov_original_pitch = one.front().prov_original_pitch;
#endif
      break;
    }
  }

  // Guarantee the global melodic peak lands in a Chorus. The Chorus headroom
  // in the register ladder makes this likely but not certain: a conjunct Chorus
  // melody may never reach its lifted ceiling while a Verse/Pre-chorus note
  // touches the base ceiling and ties (or beats) the actual Chorus peak.
  // Cap non-Chorus notes strictly below the realized Chorus peak.
  uint8_t chorus_peak = 0;
  for (const auto& note : all_notes) {
    for (const auto& sc : section_ceilings) {
      if (!sc.is_chorus) continue;
      if (note.start_tick >= sc.start_tick && note.start_tick < sc.end_tick) {
        chorus_peak = std::max(chorus_peak, note.note);
        break;
      }
    }
  }
  if (chorus_peak > 0) {
    for (auto& note : all_notes) {
      if (note.note < chorus_peak) continue;
      const SectionCeiling* owner = nullptr;
      for (const auto& sc : section_ceilings) {
        if (note.start_tick >= sc.start_tick && note.start_tick < sc.end_tick) {
          owner = &sc;
          break;
        }
      }
      if (owner == nullptr || owner->is_chorus) continue;
      uint8_t cap = std::max<uint8_t>(static_cast<uint8_t>(chorus_peak - 1), owner->low);
      std::vector<NoteEvent> one{note};
      enforceSectionCeiling(one, harmony, owner->low, cap);
      note.note = one.front().note;
#ifdef MIDISKETCH_NOTE_PROVENANCE
      note.prov_original_pitch = one.front().prov_original_pitch;
#endif
    }
  }

  // Develop repeated Chorus heads after the global constraint passes. Cached
  // and rhythm-locked material can otherwise absorb its occurrence register
  // shift through range clamping, leaving a later climax at the same or even a
  // lower average pitch. Raise the lowest safe scale tones in the first 12
  // onsets until each later Chorus has an audible (> 0.5 semitone) head lift.
  std::vector<const Section*> choruses;
  for (const auto& section : song.arrangement().sections()) {
    if (section.type == SectionType::Chorus) choruses.push_back(&section);
  }
  auto headIndices = [&all_notes](const Section& section) {
    std::vector<size_t> indices;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (all_notes[idx].start_tick >= section.start_tick &&
          all_notes[idx].start_tick < section.endTick()) {
        indices.push_back(idx);
        if (indices.size() == 12) break;
      }
    }
    return indices;
  };
  auto pitchAverage = [&all_notes](const std::vector<size_t>& indices) {
    if (indices.empty()) return 0.0;
    int sum = 0;
    for (size_t idx : indices) sum += all_notes[idx].note;
    return static_cast<double>(sum) / static_cast<double>(indices.size());
  };
  if (choruses.size() > 1) {
    const auto first_head = headIndices(*choruses.front());
    const double first_average = pitchAverage(first_head);
    for (size_t occurrence = 1; occurrence < choruses.size(); ++occurrence) {
      auto head = headIndices(*choruses[occurrence]);
      if (head.empty()) continue;
      constexpr double kMinimumHeadLift = 0.5;
      for (size_t attempt = 0;
           attempt < head.size() * 2 && pitchAverage(head) <= first_average + kMinimumHeadLift;
           ++attempt) {
        size_t best_idx = all_notes.size();
        uint8_t best_pitch = 127;
        uint8_t best_candidate = 0;
        for (size_t head_pos = 0; head_pos < head.size(); ++head_pos) {
          const size_t note_idx = head[head_pos];
          const auto& note = all_notes[note_idx];
          int candidate = static_cast<int>(note.note) + 1;
          while (candidate <= effective_vocal_high &&
                 !isScaleTone(getPitchClass(static_cast<uint8_t>(candidate)), 0)) {
            ++candidate;
          }
          if (candidate > effective_vocal_high) continue;
          if (head_pos > 0 &&
              std::abs(candidate - static_cast<int>(all_notes[head[head_pos - 1]].note)) >
                  post_process_max_leap) {
            continue;
          }
          if (head_pos + 1 < head.size() &&
              std::abs(candidate - static_cast<int>(all_notes[head[head_pos + 1]].note)) >
                  post_process_max_leap) {
            continue;
          }
          if (!harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(candidate), note.start_tick,
                                                  note.duration, TrackRole::Vocal)) {
            continue;
          }
          if (note.note < best_pitch) {
            best_idx = note_idx;
            best_pitch = note.note;
            best_candidate = static_cast<uint8_t>(candidate);
          }
        }
        if (best_idx == all_notes.size()) break;
#ifdef MIDISKETCH_NOTE_PROVENANCE
        const uint8_t original = all_notes[best_idx].note;
        all_notes[best_idx].prov_original_pitch = original;
        all_notes[best_idx].addTransformStep(TransformStepType::ScaleSnap, original, best_candidate,
                                             0, 0);
#endif
        all_notes[best_idx].note = best_candidate;
      }
    }
  }

  // FINAL RANGE AUTHORITY.
  //
  // Every pass above works within a section's own bounds, but the chorus-head
  // lift and the develop-repeat passes raise pitches, so the configured range
  // is re-asserted here on the notes that are actually emitted. Nothing the
  // generator emits may fall outside [vocal_low, vocal_high]: the piano-roll
  // safety API reports the same bound, and a note outside it makes the two
  // surfaces contradict each other for the same handle and the same params.
  enforceSectionCeiling(all_notes, harmony, effective_vocal_low, effective_vocal_high);
  for (auto& note : all_notes) {
    if (note.note >= effective_vocal_low) continue;
#ifdef MIDISKETCH_NOTE_PROVENANCE
    const uint8_t below_range = note.note;
#endif
    int lifted = note.note;
    while (lifted < static_cast<int>(effective_vocal_low)) lifted += 12;
    note.note = static_cast<uint8_t>(std::min(lifted, static_cast<int>(effective_vocal_high)));
#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (below_range != note.note) {
      note.prov_original_pitch = below_range;
      note.addTransformStep(TransformStepType::RangeClamp, below_range, note.note, 0, 0);
    }
#endif
  }

  // FINAL INTERVAL AUTHORITY.
  //
  // enforceVocalPitchConstraints above is not the last word: the section-ceiling
  // passes, the chorus-peak cap and the chorus-head lift all move pitches after
  // it, and lowering one note widens the leap into the next. The invariant has
  // to hold on the notes that are actually emitted, so it is re-established
  // here, after every pass that can move a pitch.
  //
  // The replacement is bounded by the note's OWN section range rather than the
  // song-wide one, so this sweep cannot undo a ceiling the passes above just
  // enforced; the two constraints therefore cannot fight each other. A note with
  // no admissible pitch keeps the one it has -- a ceiling and a consonant pitch
  // both outrank a singable interval.
  //
  // The replacement is chosen by melody::resolveLeapWithinBound, which searches
  // every diatonic pitch the bound allows and admits it on the same terms as
  // every other vocal pass. Searching only the chord tones left the sweep with
  // as few as two candidates under a secondary dominant, and a bound it could
  // not close stayed open.
  {
    const uint8_t final_ctx_max_leap = melody::resolveContextMaxLeap(params);
    for (size_t i = 1; i < all_notes.size(); ++i) {
      const SectionCeiling* owner = nullptr;
      for (const auto& sc : section_ceilings) {
        if (all_notes[i].start_tick >= sc.start_tick && all_notes[i].start_tick < sc.end_tick) {
          owner = &sc;
          break;
        }
      }
      SectionType owner_type = SectionType::A;
      for (const auto& section : song.arrangement().sections()) {
        if (all_notes[i].start_tick >= section.start_tick &&
            all_notes[i].start_tick < section.endTick()) {
          owner_type = section.type;
          break;
        }
      }
      const int max_interval = melody::getEffectiveMaxInterval(owner_type, final_ctx_max_leap);
      const int prev_pitch = all_notes[i - 1].note;
      const int curr_pitch = all_notes[i].note;
      if (std::abs(curr_pitch - prev_pitch) <= max_interval) continue;

      const uint8_t bound_low = owner != nullptr ? owner->low : effective_vocal_low;
      const uint8_t bound_high = owner != nullptr ? owner->high : effective_vocal_high;

      melody::MelodicNeighborhood neighborhood;
      neighborhood.start = all_notes[i].start_tick;
      neighborhood.duration = all_notes[i].duration;
      neighborhood.prev_pitch = prev_pitch;
      if (i + 1 < all_notes.size()) {
        const Tick cur_end = all_notes[i].start_tick + all_notes[i].duration;
        neighborhood.next_pitch = all_notes[i + 1].note;
        neighborhood.next_start = all_notes[i + 1].start_tick;
        neighborhood.gap_to_next =
            all_notes[i + 1].start_tick > cur_end ? all_notes[i + 1].start_tick - cur_end : 0;
      }

      const int fixed = melody::resolveLeapWithinBound(harmony, neighborhood, curr_pitch,
                                                       max_interval, bound_low, bound_high);
      if (fixed == curr_pitch) continue;
#ifdef MIDISKETCH_NOTE_PROVENANCE
      const uint8_t before = all_notes[i].note;
#endif
      all_notes[i].note = static_cast<uint8_t>(fixed);
#ifdef MIDISKETCH_NOTE_PROVENANCE
      if (before != all_notes[i].note) {
        all_notes[i].prov_original_pitch = before;
        all_notes[i].addTransformStep(TransformStepType::IntervalFix, before, all_notes[i].note, 0,
                                      0);
      }
#endif
    }
  }

  // Final overlap check - ensures no overlaps after all processing
  NoteTimeline::fixOverlapsWithMinDuration(all_notes, min_note_duration);

  // Add notes to track
  // Note: Registration with HarmonyContext is handled by Coordinator after generateFullTrack()
  // to avoid double registration and ensure MidiTrack and HarmonyContext are in sync
  for (const auto& note : all_notes) {
    track.addNote(note);
  }

  // Apply pitch bend expressions (scoop-up, fall-off, vibrato, portamento)
  const auto* sections_ptr = &song.arrangement().sections();
  applyVocalPitchBendExpressions(track, all_notes, params, rng, sections_ptr);
}

void VocalGenerator::doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  Song& song = *ctx.song;
  const GeneratorParams& params = *ctx.params;
  std::mt19937& rng = *ctx.rng;
  IHarmonyContext& harmony = *ctx.harmony;
  const DrumGrid* drum_grid = ctx.drum_grid;

  // Calculate effective vocal range (extracted helper)
  VocalRangeResult range = calculateEffectiveVocalRange(params, song, motif_track_);
  uint8_t effective_vocal_low = range.effective_low;
  uint8_t effective_vocal_high = range.effective_high;
  float velocity_scale = range.velocity_scale;

  // Get chord progression
  const auto& progression = getChordProgression(params.chord_id);

  // Create MelodyDesigner
  MelodyDesigner designer;

  // Collect all notes
  std::vector<NoteEvent> all_notes;

  // Collect per-section pitch ceilings for the final, song-wide ceiling pass.
  std::vector<SectionCeiling> section_ceilings;

  // Phrase cache for section repetition (V2: extended key with bars + chord_degree)
  std::unordered_map<PhraseCacheKey, CachedPhrase, PhraseCacheKeyHash> phrase_cache;

  // Check if rhythm lock should be used
  bool use_rhythm_lock = shouldLockVocalRhythm(params);
  bool use_per_section_type_lock = shouldUsePerSectionTypeRhythmLock(params);
  // Local rhythm lock cache if none provided externally
  CachedRhythmPattern local_rhythm_lock;
  CachedRhythmPattern* active_rhythm_lock = &local_rhythm_lock;
  // Per-section-type rhythm lock map (for UltraVocaloid)
  std::unordered_map<SectionType, CachedRhythmPattern> section_type_rhythm_locks;

  // Clear existing phrase boundaries for fresh generation
  song.clearPhraseBoundaries();

  // Track section type occurrences for progressive tessitura shift
  // J-POP practice: later choruses are often sung higher for emotional build-up
  std::unordered_map<SectionType, int> section_occurrence_count;

  // Track whether previous section had vocal notes (for inter-section breath)
  bool has_prev_vocal_section = false;
  SectionType prev_vocal_section_type = SectionType::Intro;

  // Process each section
  for (const auto& section : song.arrangement().sections()) {
    // Skip sections without vocals (by type)
    if (!sectionHasVocals(section.type)) {
      continue;
    }
    // Skip sections where vocal is disabled by track_mask
    if (shouldSkipSection(section)) {
      continue;
    }

    // Get template: use explicit template if specified, otherwise auto-select by style/section
    MelodyTemplateId section_template_id =
        (params.melody_template != MelodyTemplateId::Auto)
            ? params.melody_template
            : getDefaultTemplateForStyle(params.vocal_style, section.type);
    const MelodyTemplate& section_tmpl = getTemplate(section_template_id);

    // Calculate section boundaries
    Tick section_start = section.start_tick;
    Tick section_end = section.endTick();

    // Get chord for this section
    int chord_idx = section.start_bar % progression.length;
    int8_t chord_degree = progression.at(chord_idx);

    // Track occurrence count for this section type (1-based)
    int occurrence = ++section_occurrence_count[section.type];

    // Apply register shift for section (clamped to original range)
    // Includes progressive tessitura shift for later occurrences
    int8_t register_shift = getRegisterShift(section.type, params.melody_params, occurrence);

    // ========================================================================
    // Section ceiling.
    //
    // effective_vocal_high is a hard bound, not a soft target. It is derived
    // from the configured vocal_high (a singer's real top note) and is the same
    // bound the piano-roll safety API reports against, so a Chorus given
    // headroom ABOVE it produces notes the engine's own safety surface calls
    // out of range for the same handle and the same params.
    //
    // The global melodic peak is kept inside a Chorus by the realized-peak cap
    // in postProcessVocalNotes, which measures what the Chorus actually sang;
    // an anticipatory ceiling excursion is neither necessary for that nor
    // permitted by the configured range.
    // ========================================================================
    int section_ceiling = static_cast<int>(effective_vocal_high);
    // RhythmSync exception: the locked-rhythm Chorus is pitch-constrained by the
    // motif grid and rarely reaches its ceiling, so the Pre-chorus (B) gives up
    // one semitone to keep the peak from tying it.
    if (params.paradigm == GenerationParadigm::RhythmSync && section.type == SectionType::B) {
      section_ceiling -= 1;
    }

    // Register shift adjusts the preferred center but must not exceed the
    // section's ceiling.
    int low_lift = 0;
    uint8_t section_vocal_low = static_cast<uint8_t>(
        std::clamp(static_cast<int>(effective_vocal_low) + register_shift + low_lift,
                   static_cast<int>(effective_vocal_low),
                   static_cast<int>(effective_vocal_high) - 6));  // At least 6 semitone range
    uint8_t section_vocal_high = static_cast<uint8_t>(std::clamp(
        section_ceiling + register_shift,
        std::min(static_cast<int>(effective_vocal_low) + 6, section_ceiling), section_ceiling));

    // Apply vocal_range_span constraint
    if (section.vocal_range_span > 0) {
      int span = section.vocal_range_span;
      if (static_cast<int>(section_vocal_high) - static_cast<int>(section_vocal_low) > span) {
        section_vocal_high = static_cast<uint8_t>(section_vocal_low + span);
      }
    }

    // Record this section's ceiling for the final song-wide ceiling pass.
    section_ceilings.push_back(
        SectionCeiling{section_start, section_end, section_vocal_low, section_vocal_high,
                       section.type == SectionType::Chorus || section.type == SectionType::Drop});

    // Recalculate tessitura for section
    TessituraRange section_tessitura = calculateTessitura(section_vocal_low, section_vocal_high);

    std::vector<NoteEvent> section_notes;

    // V2: Create extended cache key
    PhraseCacheKey cache_key{section.type, section.bars, chord_degree};

    // Check phrase cache for repeated sections (V2: extended key)
    auto cache_it = phrase_cache.find(cache_key);
    if (cache_it != phrase_cache.end()) {
      // Cache hit: reuse cached phrase with timing adjustment and optional variation
      CachedPhrase& cached = cache_it->second;

      // Select variation based on reuse count and occurrence
      // (later choruses get progressively more variation)
      PhraseVariation variation = selectPhraseVariation(cached.reuse_count, occurrence, rng);
      cached.reuse_count++;

      // Shift timing to current section start
      section_notes = shiftTiming(cached.notes, section_start);

      // Apply subtle variation for interest while maintaining recognizability
      applyPhraseVariation(section_notes, variation, rng);

      // A cached phrase already contains the first occurrence's embellishment.
      // Later occurrences still need their own development pass; otherwise a
      // cache hit bypasses the occurrence-aware NCT density and every repeated
      // section becomes only a shifted copy. The embellisher preserves the
      // existing skeleton while adding the later-occurrence detail.
      if (occurrence > 1 && !section_notes.empty()) {
        EmbellishmentConfig occurrence_config = MelodicEmbellisher::getConfigForMood(params.mood);
        occurrence_config.adjustForOccurrence(occurrence);
        section_notes =
            MelodicEmbellisher::embellish(section_notes, occurrence_config, harmony, 0, rng);
      }

      // Adjust pitch range if different
      section_notes = adjustPitchRange(section_notes, cached.vocal_low, cached.vocal_high,
                                       section_vocal_low, section_vocal_high);

      // A replayed hook is still a hook the listener hears, so it counts toward
      // the template's betrayal threshold. Counting only generated hooks left
      // the counter below every threshold in exactly the songs the mechanism
      // exists for -- the ones whose choruses repeat.
      if (cached.contains_hook) {
        designer.replayHookOccurrence(section_tmpl, section_notes, harmony, rng, section_vocal_low,
                                      section_vocal_high);
      }

      // Re-apply collision avoidance (chord context may differ)
      applyCollisionAvoidanceWithIntervalConstraint(section_notes, harmony, section_vocal_low,
                                                    section_vocal_high, section.type,
                                                    melody::resolveContextMaxLeap(params));

      // Enforce non-Chorus ceiling so the global peak stays in the Chorus
      // (see detailed rationale in the cache-miss branch below).
      if (section.type != SectionType::Chorus && section.type != SectionType::Drop) {
        enforceSectionCeiling(section_notes, harmony, section_vocal_low, section_vocal_high);
      }
    } else {
      // Cache miss: generate new melody
      const uint8_t hook_count_before_section = designer.hookRepetitionCount();
      MelodyDesigner::SectionContext sctx =
          buildSectionContext(section, params, song, section_tessitura, section_vocal_low,
                              section_vocal_high, chord_degree, occurrence, drum_grid, designer);

      // Resolve rhythm lock for this section
      CachedRhythmPattern motif_rhythm_pattern;  // Local storage for Motif-derived pattern
      CachedRhythmPattern* current_rhythm_lock = nullptr;
      if (use_rhythm_lock) {
        current_rhythm_lock = resolveRhythmLock(
            section, params, song, ctx, motif_rhythm_pattern, use_per_section_type_lock,
            section_type_rhythm_locks, active_rhythm_lock, section_start, section_end);
      }

      // Build phrase plan for this section (uses rhythm lock if available)
      PhrasePlan phrase_plan =
          PhrasePlanner::buildPlan(section.type, section_start, section_end, section.bars,
                                   params.mood, params.vocal_style, current_rhythm_lock, params.bpm,
                                   params.melody_params.phrase_length_bars, sctx.anticipation_rest);

      // A Chorus entered straight out of a Pre-chorus bursts from that section's
      // hold, which PhrasePlanner cannot see: it plans one section at a time.
      if (section.type == SectionType::Chorus && !phrase_plan.phrases.empty()) {
        const auto& sections = song.arrangement().sections();
        for (size_t si = 0; si < sections.size(); ++si) {
          if (&sections[si] == &section && si > 0 && sections[si - 1].type == SectionType::B) {
            PhrasePlanner::markHoldBurstEntry(phrase_plan.phrases[0], section.type);
            break;
          }
        }
      }

      // Hand the plan to the designer so the rhythm-lock reconciliation and the
      // density surge above reach generation instead of being re-derived there.
      sctx.phrase_plan = &phrase_plan;

      // Run-based onset selection for RhythmSync (skip for UltraVocaloid)
      CachedRhythmPattern run_filtered_pattern;
      if (current_rhythm_lock != nullptr && params.paradigm == GenerationParadigm::RhythmSync &&
          params.motif.rhythm_template != MotifRhythmTemplate::None &&
          params.vocal_style != VocalStylePreset::UltraVocaloid) {
        const auto& tmpl = motif_detail::getTemplateConfig(params.motif.rhythm_template);
        run_filtered_pattern = buildRunBasedOnsetMap(*current_rhythm_lock, phrase_plan, tmpl,
                                                     params.bpm, section_start);
        current_rhythm_lock = &run_filtered_pattern;
      }

      if (current_rhythm_lock != nullptr) {
        // Use locked rhythm pattern with evaluation-based pitch selection
        section_notes = generateLockedRhythmWithEvaluation(*current_rhythm_lock, section, designer,
                                                           harmony, sctx, rng, &phrase_plan);
      } else {
        // Generate melody with evaluation (candidate count varies by section importance)
        int candidate_count = MelodyDesigner::getCandidateCountForSection(section.type);
        section_notes = designer.generateSectionWithEvaluation(
            section_tmpl, sctx, harmony, rng, params.vocal_style, params.melodic_complexity,
            candidate_count);

        // Cache rhythm pattern for subsequent sections
        // Validate density before locking to prevent sparse patterns from propagating
        constexpr float kMinRhythmLockDensity = 3.0f;  // Minimum notes per bar
        if (use_rhythm_lock && !section_notes.empty()) {
          CachedRhythmPattern candidate =
              extractRhythmPattern(section_notes, section_start, section.bars * 4);
          float density = calculatePatternDensity(candidate);

          if (density >= kMinRhythmLockDensity) {
            if (use_per_section_type_lock) {
              // Cache per section type
              section_type_rhythm_locks[section.type] = std::move(candidate);
            } else if (!active_rhythm_lock->isValid()) {
              // Cache globally
              *active_rhythm_lock = std::move(candidate);
            }
          }
          // If density is too low, don't lock - let subsequent sections generate fresh
        }
      }

      // Apply transition approach if transition info was set
      if (sctx.transition_to_next) {
        designer.applyTransitionApproach(section_notes, sctx, harmony);
      }

      // Apply HarmonyContext collision avoidance with interval constraint
      applyCollisionAvoidanceWithIntervalConstraint(section_notes, harmony, section_vocal_low,
                                                    section_vocal_high, section.type,
                                                    melody::resolveContextMaxLeap(params));

      // Enforce non-Chorus ceiling AFTER all pitch transforms. Earlier pitch
      // resolution (collision avoidance, interval fixes) can push individual
      // notes above section_vocal_high; left unchecked, the global melodic peak
      // can land in a Verse/Pre-chorus/Bridge instead of the Chorus. Any note
      // above the ceiling is dropped an octave and snapped to a safe scale tone
      // so the Chorus remains the clear melodic climax.
      if (section.type != SectionType::Chorus && section.type != SectionType::Drop) {
        enforceSectionCeiling(section_notes, harmony, section_vocal_low, section_vocal_high);
      }

      // Extract GlobalMotif from first Chorus for song-wide melodic unity
      // Subsequent sections will receive bonus for similar contour/intervals
      if (section.type == SectionType::Chorus && !designer.getCachedGlobalMotif().has_value()) {
        GlobalMotif motif = melody::extractGlobalMotif(section_notes);
        if (motif.isValid()) {
          designer.setGlobalMotif(motif);
        }
      }

      // Apply hook intensity effects at hook points (Chorus, B section)
      applyHookIntensity(section_notes, section.type, params.hook_intensity, section_start);

      // Cache the phrase (with relative timing)
      CachedPhrase cache_entry;
      cache_entry.notes = toRelativeTiming(section_notes, section_start);
      cache_entry.bars = section.bars;
      cache_entry.vocal_low = section_vocal_low;
      cache_entry.vocal_high = section_vocal_high;
      cache_entry.contains_hook = designer.hookRepetitionCount() > hook_count_before_section;
      phrase_cache[cache_key] = std::move(cache_entry);
    }

    // V5: Generate phrase boundary at section end
    if (!section_notes.empty()) {
      CadenceType cadence = detectCadenceType(section_notes, chord_degree);
      bool is_section_end = true;
      bool is_breath = true;  // Breath at every section end

      PhraseBoundary boundary;
      boundary.tick = section_end;
      boundary.is_breath = is_breath;
      boundary.is_section_end = is_section_end;
      boundary.cadence = cadence;
      song.addPhraseBoundary(boundary);
    }

    // Ensure inter-section breath gap by shortening previous section's last note
    if (has_prev_vocal_section && !all_notes.empty() && !section_notes.empty()) {
      Tick prev_end = all_notes.back().start_tick + all_notes.back().duration;
      Tick next_start = section_notes.front().start_tick;
      Tick current_gap = (next_start > prev_end) ? (next_start - prev_end) : 0;

      // Calculate desired breath using existing BreathContext
      BreathContext breath_ctx;
      breath_ctx.is_section_boundary = true;
      breath_ctx.next_section = section.type;
      // Compute prev_phrase_high from last ~8 notes in all_notes
      uint8_t prev_high = 60;
      for (int k = static_cast<int>(all_notes.size()) - 1;
           k >= 0 && k >= static_cast<int>(all_notes.size()) - 8; --k) {
        prev_high = std::max(prev_high, all_notes[k].note);
      }
      breath_ctx.prev_phrase_high = prev_high;

      Tick desired_breath =
          melody::getBreathDuration(prev_vocal_section_type, params.mood, 0.5f, prev_high,
                                    &breath_ctx, params.vocal_style, params.bpm);
      desired_breath = std::max(desired_breath, TICK_EIGHTH);  // floor: 240 ticks

      if (current_gap < desired_breath) {
        Tick shorten_by = desired_breath - current_gap;
        NoteEvent& last = all_notes.back();
        // Duration floor: never shorten below TICK_SIXTEENTH (120 ticks)
        Tick min_remaining = TICK_SIXTEENTH;
        if (last.duration > shorten_by + min_remaining) {
          last.duration -= shorten_by;
        } else if (last.duration > min_remaining) {
          last.duration = min_remaining;
        }
        // else: note is already very short, don't touch it
      }
    }

    // Add to collected notes
    // Check interval between last note of previous section and first note of this section
    if (!all_notes.empty() && !section_notes.empty()) {
      // The bound belongs to the section being entered: a Chorus is allowed the
      // octave that makes its entry a hook, and this is the very note that
      // carries it. Using the standing major 6th here silently flattened every
      // section entry in the song.
      const int boundary_max_interval =
          melody::getEffectiveMaxInterval(section.type, melody::resolveContextMaxLeap(params));
      int prev_note = all_notes.back().note;
      int first_note = section_notes.front().note;
      int interval = std::abs(first_note - prev_note);
      if (interval > boundary_max_interval) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
        uint8_t old_pitch = section_notes.front().note;
#endif
        // Pull the boundary leap back onto a tone of the chord sounding there
        int new_pitch = melody::nearestPitchInSetWithinInterval(
            melody::vocalSnapTonesAt(harmony, section_notes.front().start_tick), first_note,
            prev_note, boundary_max_interval, section_vocal_low, section_vocal_high);
        // Re-verify collision safety after interval fix
        if (!harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(new_pitch),
                                                section_notes.front().start_tick,
                                                section_notes.front().duration, TrackRole::Vocal)) {
          new_pitch = first_note;  // Keep original if fix introduces collision
        }
        section_notes.front().note = static_cast<uint8_t>(new_pitch);
#ifdef MIDISKETCH_NOTE_PROVENANCE
        if (old_pitch != section_notes.front().note) {
          section_notes.front().prov_original_pitch = old_pitch;
          section_notes.front().addTransformStep(TransformStepType::IntervalFix, old_pitch,
                                                 section_notes.front().note, 0, 0);
        }
#endif
      }
    }
    // Determine if chromatic approach is enabled for this mood
    EmbellishmentConfig emb_config = MelodicEmbellisher::getConfigForMood(params.mood);
    bool allow_chromatic = emb_config.chromatic_approach;

    for (size_t ni = 0; ni < section_notes.size(); ++ni) {
      auto& note = section_notes[ni];

      // Check if this note qualifies as a chromatic passing tone that should be preserved
      bool preserve_chromatic = false;
      if (allow_chromatic) {
        int snapped_check = snapToNearestScaleTone(note.note, 0);
        bool is_chromatic = (snapped_check != note.note);

        if (is_chromatic) {
          // Preserve if on a weak beat (not beats 1 or 3) and resolves by half-step
          // to the next note (which should be a scale tone)
          Tick pos_in_bar = positionInBar(note.start_tick);
          bool is_weak = (pos_in_bar >= TICKS_PER_BEAT / 2) &&
                         !(pos_in_bar >= 2 * TICKS_PER_BEAT &&
                           pos_in_bar < 2 * TICKS_PER_BEAT + TICKS_PER_BEAT / 2);

          if (is_weak && ni + 1 < section_notes.size()) {
            int next_pitch = section_notes[ni + 1].note;
            int interval = std::abs(static_cast<int>(note.note) - next_pitch);
            // Half-step resolution to a diatonic note
            if (interval <= 2 && snapToNearestScaleTone(next_pitch, 0) == next_pitch) {
              preserve_chromatic = true;
            }
          }
        }
      }

      if (!preserve_chromatic) {
        // ABSOLUTE CONSTRAINT: Ensure pitch is on scale (prevents chromatic notes)
#ifdef MIDISKETCH_NOTE_PROVENANCE
        uint8_t old_pitch = note.note;
#endif
        int snapped = snapToNearestScaleTone(note.note, 0);  // Always C major internally
        uint8_t snapped_clamped = static_cast<uint8_t>(std::clamp(
            snapped, static_cast<int>(section_vocal_low), static_cast<int>(section_vocal_high)));
        // Re-verify collision safety after scale snap
        if (snapped_clamped != note.note &&
            !harmony.isConsonantWithOtherTracks(snapped_clamped, note.start_tick, note.duration,
                                                TrackRole::Vocal)) {
          // Scale snap would introduce collision - keep original pitch
          snapped_clamped = note.note;
        }
        note.note = snapped_clamped;
#ifdef MIDISKETCH_NOTE_PROVENANCE
        if (old_pitch != note.note) {
          note.prov_original_pitch = old_pitch;
          note.addTransformStep(TransformStepType::ScaleSnap, old_pitch, note.note, 0, 0);
        }
#endif
      } else {
        // Clamp to range even for chromatic tones
#ifdef MIDISKETCH_NOTE_PROVENANCE
        uint8_t old_pitch = note.note;
#endif
        uint8_t clamped = static_cast<uint8_t>(std::clamp(static_cast<int>(note.note),
                                                          static_cast<int>(section_vocal_low),
                                                          static_cast<int>(section_vocal_high)));
        // Re-verify collision safety after range clamp
        if (clamped != note.note &&
            !harmony.isConsonantWithOtherTracks(clamped, note.start_tick, note.duration,
                                                TrackRole::Vocal)) {
          // Clamp would introduce collision - keep original pitch
          clamped = note.note;
        }
        note.note = clamped;
#ifdef MIDISKETCH_NOTE_PROVENANCE
        if (old_pitch != note.note) {
          note.prov_original_pitch = old_pitch;
          note.addTransformStep(TransformStepType::RangeClamp, old_pitch, note.note,
                                static_cast<int8_t>(section_vocal_low),
                                static_cast<int8_t>(section_vocal_high));
        }
#endif
      }
      // Section boundary invariant: a vocal note must start inside its own
      // section. Phrase windows can overshoot the section end (uneven bar
      // splits, transition pickups), and a note starting at section_end lands
      // in the NEXT section — including call sections (Chant/MixBreak) that
      // must stay vocal-free.
      if (note.start_tick >= section_end) {
        continue;
      }
      all_notes.push_back(note);
    }

    // Update inter-section breath tracking
    if (!section_notes.empty()) {
      has_prev_vocal_section = true;
      prev_vocal_section_type = section.type;
    }
  }

  // NOTE: Modulation is NOT applied internally.
  // MidiWriter applies modulation to all tracks when generating MIDI bytes.
  // This ensures consistent behavior and avoids double-modulation.

  // Final post-processing: sustain, groove, overlaps, breath, merge, velocity,
  // pitch constraints, same-pitch breaks, and pitch bend expressions
  postProcessVocalNotes(all_notes, track, song, params, harmony, rng, velocity_scale,
                        effective_vocal_low, effective_vocal_high, section_ceilings);
}

}  // namespace midisketch
