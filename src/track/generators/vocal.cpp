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
  // RhythmSync support
  sctx.paradigm = params.paradigm;
  sctx.drum_grid = drum_grid;
  // Motif template for accent-linked velocity (RhythmSync)
  if (params.paradigm == GenerationParadigm::RhythmSync) {
    sctx.motif_params = &params.motif;
  }
  // Behavioral Loop support
  sctx.addictive_mode = params.addictive_mode;
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
  if (params.melody_max_leap_override) {
    sctx.max_leap_semitones = params.melody_params.max_leap_interval;
  } else if (params.blueprint_ref != nullptr) {
    sctx.max_leap_semitones = params.blueprint_ref->constraints.max_leap_semitones;
  }
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

void VocalGenerator::postProcessVocalNotes(std::vector<NoteEvent>& all_notes, MidiTrack& track,
                                           const Song& song, const GeneratorParams& params,
                                           IHarmonyContext& harmony, std::mt19937& rng,
                                           float velocity_scale, uint8_t effective_vocal_low,
                                           uint8_t effective_vocal_high) const {
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
  // within individual sections that exceed max_phrase_bars.
  VocalPhysicsParams physics = getVocalPhysicsParams(params.vocal_style);
  if (physics.requires_breath && physics.max_phrase_bars < 255) {
    melody::enforceMaxPhraseDuration(all_notes, physics.max_phrase_bars, TICK_EIGHTH);
  }

  // Vocal-friendly post-processing:
  // Merge same-pitch notes with BPM-aware gap threshold.
  // At fast tempos, gate_ratio creates larger tick gaps that should still be merged.
  // SKIP for UltraVocaloid: same-pitch rapid-fire is intentional (machine-gun style)
  if (params.vocal_style != VocalStylePreset::UltraVocaloid) {
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
  enforceVocalPitchConstraints(all_notes, params, harmony);

  // Break up excessive consecutive same-pitch notes (RhythmSync compatibility)
  // This addresses monotonous melody issues in RhythmSync paradigm where
  // collision avoidance can cause long runs of the same pitch.
  // max_consecutive=3 means 4th note onwards gets alternated for melodic interest.
  breakConsecutiveSamePitch(all_notes, harmony, effective_vocal_low, effective_vocal_high, 3);

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
    // Climax Range Expansion (Task 3.11)
    // For the last Chorus (peak_level=Max): allow vocal_high + 2 semitones
    // This gives the vocalist room to "break out" at the climax
    // ========================================================================
    int climax_extension = 0;
    if (section.type == SectionType::Chorus && section.peak_level == PeakLevel::Max) {
      climax_extension = 2;  // +2 semitones for climax
    }

    // Register shift adjusts the preferred center but must not exceed original range
    // (except for climax extension which allows exceeding the range)
    uint8_t section_vocal_low = static_cast<uint8_t>(
        std::clamp(static_cast<int>(effective_vocal_low) + register_shift,
                   static_cast<int>(effective_vocal_low),
                   static_cast<int>(effective_vocal_high) - 6));  // At least 6 semitone range
    uint8_t section_vocal_high = static_cast<uint8_t>(
        std::clamp(static_cast<int>(effective_vocal_high) + register_shift + climax_extension,
                   static_cast<int>(effective_vocal_low) + 6,
                   std::min(static_cast<int>(effective_vocal_high) + climax_extension, 127)));

    // Apply vocal_range_span constraint
    if (section.vocal_range_span > 0) {
      int span = section.vocal_range_span;
      if (static_cast<int>(section_vocal_high) - static_cast<int>(section_vocal_low) > span) {
        section_vocal_high = static_cast<uint8_t>(section_vocal_low + span);
      }
    }

    // Verse/Bridge ceiling: prevent non-Chorus sections from reaching Chorus highs.
    // This ensures the Chorus climax is perceptually distinct.
    constexpr int kVerseCeilingMarginSt = 3;
    if (section.type == SectionType::A || section.type == SectionType::Bridge) {
      int chorus_ceiling =
          static_cast<int>(effective_vocal_high) + params.melody_params.chorus_register_shift;
      int ceiling = chorus_ceiling - kVerseCeilingMarginSt;
      if (static_cast<int>(section_vocal_high) > ceiling) {
        section_vocal_high =
            static_cast<uint8_t>(std::max(ceiling, static_cast<int>(section_vocal_low) + 6));
      }
    }

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

      // Adjust pitch range if different
      section_notes = adjustPitchRange(section_notes, cached.vocal_low, cached.vocal_high,
                                       section_vocal_low, section_vocal_high);

      // Re-apply collision avoidance (chord context may differ)
      applyCollisionAvoidanceWithIntervalConstraint(section_notes, harmony, section_vocal_low,
                                                    section_vocal_high);
    } else {
      // Cache miss: generate new melody
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
      PhrasePlan phrase_plan = PhrasePlanner::buildPlan(
          section.type, section_start, section_end, section.bars, params.mood, params.vocal_style,
          current_rhythm_lock, params.bpm);

      // Mark first chorus phrase as hold-burst entry if previous section was B
      if (section.type == SectionType::Chorus && !phrase_plan.phrases.empty()) {
        const auto& sections = song.arrangement().sections();
        for (size_t si = 0; si < sections.size(); ++si) {
          if (&sections[si] == &section && si > 0 && sections[si - 1].type == SectionType::B) {
            phrase_plan.phrases[0].is_hold_burst_entry = true;
            phrase_plan.phrases[0].density_modifier *= 1.3f;
            break;
          }
        }
      }

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
                                                    section_vocal_high);

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
      // kMaxMelodicInterval from pitch_utils.h
      int prev_note = all_notes.back().note;
      int first_note = section_notes.front().note;
      int interval = std::abs(first_note - prev_note);
      if (interval > kMaxMelodicInterval) {
        // Get chord degree at first note's position
        int8_t first_note_chord_degree = harmony.getChordDegreeAt(section_notes.front().start_tick);
        // Use nearestChordToneWithinInterval to stay on chord tones
#ifdef MIDISKETCH_NOTE_PROVENANCE
        uint8_t old_pitch = section_notes.front().note;
#endif
        int new_pitch = nearestChordToneWithinInterval(
            first_note, prev_note, first_note_chord_degree, kMaxMelodicInterval, section_vocal_low,
            section_vocal_high, nullptr);
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
                        effective_vocal_low, effective_vocal_high);
}

}  // namespace midisketch
