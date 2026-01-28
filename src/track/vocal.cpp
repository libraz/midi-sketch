/**
 * @file vocal.cpp
 * @brief Vocal melody track generation with phrase caching and variation.
 *
 * Phrase-based approach: each section generates/reuses cached phrases with
 * subtle variations for varied repetition (scale degrees, singability, cadences).
 */

#include "track/vocal.h"

#include <algorithm>
#include <unordered_map>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/melody_embellishment.h"
#include "core/melody_templates.h"
#include "core/pitch_bend_curves.h"
#include "core/pitch_utils.h"
#include "core/production_blueprint.h"
#include "core/velocity.h"
#include "track/melody_designer.h"
#include "track/phrase_cache.h"
#include "track/phrase_variation.h"
#include "track/vocal_helpers.h"

namespace midisketch {

// ============================================================================
// Motif Collision Avoidance Constants
// ============================================================================
// When BackgroundMotif mode, avoid vocal range collision with motif track.
// These MIDI note numbers define register boundaries for range separation.
constexpr uint8_t kMotifHighRegisterThreshold = 72;  // C5 - motif considered "high" if above this
constexpr uint8_t kMotifLowRegisterThreshold = 60;   // C4 - motif considered "low" if below this
constexpr uint8_t kVocalAvoidHighLimit = 72;         // Limit vocal high when motif is high
constexpr uint8_t kVocalAvoidLowLimit = 65;          // Limit vocal low when motif is low
constexpr uint8_t kMinVocalOctaveRange = 12;         // Minimum 1 octave range required
constexpr uint8_t kVocalRangeFloor = 48;             // C3 - absolute minimum for vocal
constexpr uint8_t kVocalRangeCeiling = 96;           // C7 - absolute maximum for vocal

// ============================================================================
// Rhythm Lock Support
// ============================================================================

bool shouldLockVocalRhythm(const GeneratorParams& params) {
  // Rhythm lock is used for Orangestar style:
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
bool shouldUsePerSectionTypeRhythmLock(const GeneratorParams& params) {
  return params.vocal_style == VocalStylePreset::UltraVocaloid;
}

/**
 * @brief Generate notes using locked rhythm pattern with new pitches.
 * @param rhythm Locked rhythm pattern to use
 * @param section Current section
 * @param designer Melody designer for pitch selection
 * @param harmony Harmony context
 * @param ctx Section context
 * @param rng Random number generator
 * @return Generated notes with locked rhythm and new pitches
 */
static std::vector<NoteEvent> generateWithLockedRhythm(
    const CachedRhythmPattern& rhythm, const Section& section, MelodyDesigner& designer,
    const IHarmonyContext& harmony, const MelodyDesigner::SectionContext& ctx, std::mt19937& rng) {
  std::vector<NoteEvent> notes;
  uint8_t section_beats = section.bars * 4;

  // Get scaled onsets and durations for this section's length
  auto onsets = rhythm.getScaledOnsets(section_beats);
  auto durations = rhythm.getScaledDurations(section_beats);

  if (onsets.empty()) {
    return notes;
  }

  // Ensure durations matches onsets size
  while (durations.size() < onsets.size()) {
    durations.push_back(0.5f);  // Default half-beat duration
  }

  uint8_t prev_pitch = (ctx.vocal_low + ctx.vocal_high) / 2;  // Start at center

  for (size_t i = 0; i < onsets.size(); ++i) {
    float beat = onsets[i];
    float dur = durations[i];

    Tick tick = section.start_tick + static_cast<Tick>(beat * TICKS_PER_BEAT);
    Tick duration = static_cast<Tick>(dur * TICKS_PER_BEAT);

    // Get chord at this position
    int8_t chord_degree = harmony.getChordDegreeAt(tick);

    // Select pitch using melody designer's pitch selection logic
    // Use chord tones primarily for consonance
    uint8_t pitch = designer.selectPitchForLockedRhythm(prev_pitch, chord_degree, ctx.vocal_low,
                                                        ctx.vocal_high, rng);

    // Calculate velocity based on beat position
    float beat_in_bar = std::fmod(beat, 4.0f);
    uint8_t velocity = 80;
    if (beat_in_bar < 0.1f || std::abs(beat_in_bar - 2.0f) < 0.1f) {
      velocity = 95;  // Strong beats
    } else if (std::abs(beat_in_bar - 1.0f) < 0.1f || std::abs(beat_in_bar - 3.0f) < 0.1f) {
      velocity = 85;  // Medium beats
    }

    notes.push_back({tick, duration, pitch, velocity});
    prev_pitch = pitch;
  }

  return notes;
}

void generateVocalTrack(MidiTrack& track, Song& song, const GeneratorParams& params,
                        std::mt19937& rng, const MidiTrack* motif_track,
                        const IHarmonyContext& harmony, bool skip_collision_avoidance,
                        const DrumGrid* drum_grid, CachedRhythmPattern* rhythm_lock) {
  // Determine effective vocal range
  uint8_t effective_vocal_low = params.vocal_low;
  uint8_t effective_vocal_high = params.vocal_high;

  // Apply blueprint max_pitch constraint (e.g., IdolKawaii limits to G5=79)
  if (params.blueprint_ref != nullptr) {
    const auto& constraints = params.blueprint_ref->constraints;
    if (constraints.max_pitch < effective_vocal_high) {
      effective_vocal_high = constraints.max_pitch;
    }
  }

  // Adjust vocal_high to account for modulation
  // After modulation, notes will be transposed up by modulationAmount semitones.
  // To ensure the final pitch stays within vocal_high, reduce effective_vocal_high.
  // NOTE: Use effective_vocal_high (which already has blueprint constraint applied)
  // to ensure modulation doesn't override blueprint limits.
  int8_t mod_amount = song.modulationAmount();
  if (mod_amount > 0) {
    int adjusted_high = static_cast<int>(effective_vocal_high) - mod_amount;
    // Ensure at least 1 octave (12 semitones) range remains
    int min_high = static_cast<int>(effective_vocal_low) + 12;
    effective_vocal_high = static_cast<uint8_t>(std::max(min_high, adjusted_high));
  }

  // Adjust range for BackgroundMotif to avoid collision with motif
  if (params.composition_style == CompositionStyle::BackgroundMotif && motif_track != nullptr &&
      !motif_track->empty()) {
    auto [motif_low, motif_high] = motif_track->analyzeRange();

    if (motif_high > kMotifHighRegisterThreshold) {  // Motif in high register
      effective_vocal_high = std::min(effective_vocal_high, kVocalAvoidHighLimit);
      if (effective_vocal_high - effective_vocal_low < kMinVocalOctaveRange) {
        effective_vocal_low = std::max(
            kVocalRangeFloor, static_cast<uint8_t>(effective_vocal_high - kMinVocalOctaveRange));
      }
    } else if (motif_low < kMotifLowRegisterThreshold) {  // Motif in low register
      effective_vocal_low = std::max(effective_vocal_low, kVocalAvoidLowLimit);
      if (effective_vocal_high - effective_vocal_low < kMinVocalOctaveRange) {
        effective_vocal_high = std::min(
            kVocalRangeCeiling, static_cast<uint8_t>(effective_vocal_low + kMinVocalOctaveRange));
      }
    }
  }

  // Get chord progression
  const auto& progression = getChordProgression(params.chord_id);

  // Velocity scale for composition style
  float velocity_scale = 1.0f;
  if (params.composition_style == CompositionStyle::BackgroundMotif) {
    velocity_scale = 0.7f;
  } else if (params.composition_style == CompositionStyle::SynthDriven) {
    velocity_scale = 0.75f;
  }

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
  CachedRhythmPattern* active_rhythm_lock = rhythm_lock ? rhythm_lock : &local_rhythm_lock;
  // Per-section-type rhythm lock map (for UltraVocaloid)
  std::unordered_map<SectionType, CachedRhythmPattern> section_type_rhythm_locks;

  // Clear existing phrase boundaries for fresh generation
  song.clearPhraseBoundaries();

  // Track section type occurrences for progressive tessitura shift
  // J-POP practice: later choruses are often sung higher for emotional build-up
  std::unordered_map<SectionType, int> section_occurrence_count;

  // Process each section
  for (const auto& section : song.arrangement().sections()) {
    // Skip sections without vocals (by type)
    if (!sectionHasVocals(section.type)) {
      continue;
    }
    // Skip sections where vocal is disabled by track_mask
    if (!hasTrack(section.track_mask, TrackMask::Vocal)) {
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
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;

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
                   static_cast<int>(effective_vocal_high) + climax_extension));

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

      // Select variation based on reuse count (80% Exact, 20% variation)
      PhraseVariation variation = selectPhraseVariation(cached.reuse_count, rng);
      cached.reuse_count++;

      // Shift timing to current section start
      section_notes = shiftTiming(cached.notes, section_start);

      // Apply subtle variation for interest while maintaining recognizability
      applyPhraseVariation(section_notes, variation, rng);

      // Adjust pitch range if different
      section_notes = adjustPitchRange(section_notes, cached.vocal_low, cached.vocal_high,
                                       section_vocal_low, section_vocal_high);

      // Re-apply collision avoidance (chord context may differ)
      if (!skip_collision_avoidance) {
        applyCollisionAvoidanceWithIntervalConstraint(section_notes, harmony, section_vocal_low,
                                                      section_vocal_high);
      }
    } else {
      // Cache miss: generate new melody
      MelodyDesigner::SectionContext ctx;
      ctx.section_type = section.type;
      ctx.section_start = section_start;
      ctx.section_end = section_end;
      ctx.section_bars = section.bars;
      ctx.chord_degree = chord_degree;
      ctx.key_offset = 0;  // Always C major internally
      ctx.tessitura = section_tessitura;
      ctx.vocal_low = section_vocal_low;
      ctx.vocal_high = section_vocal_high;
      ctx.mood = params.mood;  // For harmonic rhythm alignment
      // Apply section's density_percent to density modifier (with SectionModifier)
      float base_density = getDensityModifier(section.type, params.melody_params);
      uint8_t effective_density = section.getModifiedDensity(section.density_percent);
      float density_factor = effective_density / 100.0f;
      ctx.density_modifier = base_density * density_factor;
      ctx.thirtysecond_ratio = getThirtysecondRatio(section.type, params.melody_params);
      ctx.consecutive_same_note_prob =
          getConsecutiveSameNoteProb(section.type, params.melody_params);
      ctx.disable_vowel_constraints = params.melody_params.disable_vowel_constraints;
      ctx.disable_breathing_gaps = params.melody_params.disable_breathing_gaps;
      ctx.vocal_attitude = params.vocal_attitude;
      ctx.hook_intensity = params.hook_intensity;  // For HookSkeleton selection
      // RhythmSync support
      ctx.paradigm = params.paradigm;
      ctx.drum_grid = drum_grid;
      // Behavioral Loop support
      ctx.addictive_mode = params.addictive_mode;
      // Vocal groove feel for syncopation control
      ctx.vocal_groove = params.vocal_groove;
      // Drive feel for timing and syncopation modulation
      ctx.drive_feel = params.drive_feel;

      // Vocal style for physics parameters (breath, timing, pitch bend)
      ctx.vocal_style = params.vocal_style;

      // Apply blueprint constraints for melodic leap and stepwise preference
      if (params.blueprint_ref != nullptr) {
        ctx.max_leap_semitones = params.blueprint_ref->constraints.max_leap_semitones;
        ctx.prefer_stepwise = params.blueprint_ref->constraints.prefer_stepwise;
      }

      // Set anticipation rest mode based on groove feel and drive
      // Driving/Syncopated grooves benefit from anticipation rests for "tame" effect
      // Higher drive_feel increases anticipation intensity
      if (params.vocal_groove == VocalGrooveFeel::Driving16th) {
        ctx.anticipation_rest = (params.drive_feel >= 70) ? AnticipationRestMode::Moderate
                                                          : AnticipationRestMode::Subtle;
      } else if (params.vocal_groove == VocalGrooveFeel::Syncopated) {
        ctx.anticipation_rest = AnticipationRestMode::Moderate;
      } else if (params.drive_feel >= 80) {
        // High drive with any groove gets subtle anticipation
        ctx.anticipation_rest = AnticipationRestMode::Subtle;
      }

      // Set phrase contour template based on section type
      // Common J-POP practice:
      // - Chorus: Peak (arch shape) for memorable hook contour
      // - A (Verse): Ascending for storytelling build
      // - B (Pre-chorus): Ascending to build tension before chorus
      // - Bridge: Descending for contrast
      switch (section.type) {
        case SectionType::Chorus:
          ctx.forced_contour = ContourType::Peak;
          break;
        case SectionType::A:
          ctx.forced_contour = ContourType::Ascending;
          break;
        case SectionType::B:
          ctx.forced_contour = ContourType::Ascending;
          break;
        case SectionType::Bridge:
          ctx.forced_contour = ContourType::Descending;
          break;
        default:
          ctx.forced_contour = std::nullopt;  // Use default section-aware bias
          break;
      }

      // Set transition info for next section (if any)
      const auto& sections = song.arrangement().sections();
      for (size_t i = 0; i < sections.size(); ++i) {
        if (&sections[i] == &section && i + 1 < sections.size()) {
          ctx.transition_to_next = getTransition(section.type, sections[i + 1].type);
          break;
        }
      }

      // Check for rhythm lock
      // UltraVocaloid uses per-section-type rhythm lock (Verse->Verse, Chorus->Chorus)
      // Other styles use global rhythm lock (first section's rhythm for all)
      CachedRhythmPattern* current_rhythm_lock = nullptr;
      if (use_rhythm_lock) {
        if (use_per_section_type_lock) {
          // Per-section-type lock: look up by section type
          auto it = section_type_rhythm_locks.find(section.type);
          if (it != section_type_rhythm_locks.end() && it->second.isValid()) {
            current_rhythm_lock = &it->second;
          }
        } else if (active_rhythm_lock->isValid()) {
          // Global lock: use single rhythm pattern
          current_rhythm_lock = active_rhythm_lock;
        }
      }

      if (current_rhythm_lock != nullptr) {
        // Use locked rhythm pattern with new pitches
        section_notes =
            generateWithLockedRhythm(*current_rhythm_lock, section, designer, harmony, ctx, rng);
      } else {
        // Generate melody with evaluation (candidate count varies by section importance)
        int candidate_count = MelodyDesigner::getCandidateCountForSection(section.type);
        section_notes = designer.generateSectionWithEvaluation(
            section_tmpl, ctx, harmony, rng, params.vocal_style, params.melodic_complexity,
            candidate_count);

        // Cache rhythm pattern for subsequent sections
        if (use_rhythm_lock && !section_notes.empty()) {
          if (use_per_section_type_lock) {
            // Cache per section type
            section_type_rhythm_locks[section.type] =
                extractRhythmPattern(section_notes, section_start, section.bars * 4);
          } else if (!active_rhythm_lock->isValid()) {
            // Cache globally
            *active_rhythm_lock =
                extractRhythmPattern(section_notes, section_start, section.bars * 4);
          }
        }
      }

      // Apply transition approach if transition info was set
      if (ctx.transition_to_next) {
        designer.applyTransitionApproach(section_notes, ctx, harmony);
      }

      // Apply HarmonyContext collision avoidance with interval constraint
      if (!skip_collision_avoidance) {
        applyCollisionAvoidanceWithIntervalConstraint(section_notes, harmony, section_vocal_low,
                                                      section_vocal_high);
      }

      // Extract GlobalMotif from first Chorus for song-wide melodic unity
      // Subsequent sections will receive bonus for similar contour/intervals
      if (section.type == SectionType::Chorus && !designer.getCachedGlobalMotif().has_value()) {
        GlobalMotif motif = MelodyDesigner::extractGlobalMotif(section_notes);
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
        int new_pitch = nearestChordToneWithinInterval(
            first_note, prev_note, first_note_chord_degree, kMaxMelodicInterval, section_vocal_low,
            section_vocal_high, nullptr);
        section_notes.front().note = static_cast<uint8_t>(new_pitch);
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
          Tick pos_in_bar = note.start_tick % TICKS_PER_BAR;
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
        int snapped = snapToNearestScaleTone(note.note, 0);  // Always C major internally
        note.note = static_cast<uint8_t>(std::clamp(snapped, static_cast<int>(section_vocal_low),
                                                    static_cast<int>(section_vocal_high)));
      } else {
        // Clamp to range even for chromatic tones
        note.note = static_cast<uint8_t>(std::clamp(static_cast<int>(note.note),
                                                    static_cast<int>(section_vocal_low),
                                                    static_cast<int>(section_vocal_high)));
      }
      all_notes.push_back(note);
    }
  }

  // NOTE: Modulation is NOT applied internally.
  // MidiWriter applies modulation to all tracks when generating MIDI bytes.
  // This ensures consistent behavior and avoids double-modulation.

  // Apply groove feel timing adjustments
  applyGrooveFeel(all_notes, params.vocal_groove);

  // Remove overlapping notes
  // UltraVocaloid allows 32nd notes (60 ticks), standard vocals need 16th notes (120 ticks)
  Tick min_note_duration =
      (params.vocal_style == VocalStylePreset::UltraVocaloid) ? TICK_32ND : TICK_SIXTEENTH;
  removeOverlaps(all_notes, min_note_duration);

  // Vocal-friendly post-processing:
  // Merge same-pitch notes only with very short gaps (64th note = ~30 ticks).
  // Larger gaps preserve intentional articulation (staccato, rhythmic patterns).
  constexpr Tick kMergeMaxGap = 30;
  mergeSamePitchNotes(all_notes, kMergeMaxGap);

  // NOTE: resolveIsolatedShortNotes() removed - short notes are often
  // intentional articulation (staccato bursts, rhythmic motifs).

  // Apply velocity scale
  applyVelocityBalance(all_notes, velocity_scale);

  // FINAL INTERVAL ENFORCEMENT: Ensure no consecutive notes exceed kMaxMelodicInterval
  // This catches any intervals that slipped through earlier processing
  // kMaxMelodicInterval from pitch_utils.h (Major 6th)
  for (size_t i = 1; i < all_notes.size(); ++i) {
    int prev_pitch = all_notes[i - 1].note;
    int curr_pitch = all_notes[i].note;
    int interval = std::abs(curr_pitch - prev_pitch);
    if (interval > kMaxMelodicInterval) {
      // Use nearestChordToneWithinInterval to fix
      int8_t chord_degree = harmony.getChordDegreeAt(all_notes[i].start_tick);
      int fixed_pitch =
          nearestChordToneWithinInterval(curr_pitch, prev_pitch, chord_degree, kMaxMelodicInterval,
                                         params.vocal_low, params.vocal_high, nullptr);
      all_notes[i].note = static_cast<uint8_t>(fixed_pitch);
    }
  }

  // FINAL SCALE ENFORCEMENT: Ensure all notes are diatonic after interval fixing
  // The interval enforcement may have introduced non-diatonic notes
  for (auto& note : all_notes) {
    int snapped = snapToNearestScaleTone(note.note, 0);  // Always C major internally
    if (snapped != note.note) {
      note.note = static_cast<uint8_t>(
          std::clamp(snapped, static_cast<int>(params.vocal_low), static_cast<int>(params.vocal_high)));
    }
  }

  // Add notes to track (preserving provenance)
  for (const auto& note : all_notes) {
    track.addNote(note);
  }

  // Apply pitch bend expressions based on vocal attitude and vocal physics
  // Expressive and Raw attitudes get scoop-up and fall-off effects
  // pitch_bend_scale from VocalPhysicsParams controls the depth
  VocalPhysicsParams physics = getVocalPhysicsParams(params.vocal_style);

  // Skip pitch bend entirely if scale is 0 (UltraVocaloid)
  if (params.vocal_attitude >= VocalAttitude::Expressive && physics.pitch_bend_scale > 0.0f) {
    std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);

    // Track phrase boundaries for determining phrase starts/ends
    // Notes after a rest of 1 beat or more are considered phrase starts
    constexpr Tick kPhraseGapThreshold = TICKS_PER_BEAT;

    for (size_t note_idx = 0; note_idx < all_notes.size(); ++note_idx) {
      const auto& note = all_notes[note_idx];

      // Determine if this is a phrase start (first note or after significant gap)
      bool is_phrase_start = (note_idx == 0);
      if (note_idx > 0) {
        Tick prev_note_end = all_notes[note_idx - 1].start_tick + all_notes[note_idx - 1].duration;
        if (note.start_tick - prev_note_end >= kPhraseGapThreshold) {
          is_phrase_start = true;
        }
      }

      // Determine if this is a phrase end (last note or before significant gap)
      bool is_phrase_end = (note_idx == all_notes.size() - 1);
      if (note_idx + 1 < all_notes.size()) {
        Tick next_note_start = all_notes[note_idx + 1].start_tick;
        Tick this_note_end = note.start_tick + note.duration;
        if (next_note_start - this_note_end >= kPhraseGapThreshold) {
          is_phrase_end = true;
        }
      }

      // Scoop probability based on attitude (scaled by pitch_bend_scale)
      float scoop_prob = (params.vocal_attitude == VocalAttitude::Raw) ? 0.8f : 0.5f;
      float fall_prob = (params.vocal_attitude == VocalAttitude::Raw) ? 0.7f : 0.4f;
      // Reduce probability for Vocaloid-style (partial physics)
      scoop_prob *= physics.pitch_bend_scale;
      fall_prob *= physics.pitch_bend_scale;

      // Apply attack bend (scoop-up) at phrase starts
      // Only on notes long enough to accommodate the bend
      if (is_phrase_start && note.duration >= TICK_EIGHTH && prob_dist(rng) < scoop_prob) {
        // Scoop depth varies by attitude (-20 to -40 cents), scaled by pitch_bend_scale
        int base_depth = (params.vocal_attitude == VocalAttitude::Raw) ? -40 : -25;
        int depth = static_cast<int>(base_depth * physics.pitch_bend_scale);
        if (depth != 0) {
          auto bends = PitchBendCurves::generateAttackBend(note.start_tick, depth, TICK_SIXTEENTH);
          for (const auto& bend : bends) {
            track.addPitchBend(bend.tick, bend.value);
          }
        }
      }

      // Apply fall-off at phrase ends on long notes
      if (is_phrase_end && note.duration >= TICK_HALF && prob_dist(rng) < fall_prob) {
        // Fall depth varies by attitude (-60 to -100 cents), scaled by pitch_bend_scale
        int base_depth = (params.vocal_attitude == VocalAttitude::Raw) ? -100 : -60;
        int depth = static_cast<int>(base_depth * physics.pitch_bend_scale);
        if (depth != 0) {
          Tick note_end = note.start_tick + note.duration;
          auto bends = PitchBendCurves::generateFallOff(note_end, depth, TICK_EIGHTH);
          for (const auto& bend : bends) {
            track.addPitchBend(bend.tick, bend.value);
          }
          // Add reset bend after fall-off to prepare for next note
          track.addPitchBend(note_end + TICK_SIXTEENTH, PitchBend::kCenter);
        }
      }
    }
  }
}

}  // namespace midisketch
