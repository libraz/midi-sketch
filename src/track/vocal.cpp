/**
 * @file vocal.cpp
 * @brief Vocal melody track generation with phrase caching and variation.
 *
 * Phrase-based approach: each section generates/reuses cached phrases with
 * subtle variations for varied repetition (scale degrees, singability, cadences).
 */

#include "track/vocal.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/melody_templates.h"
#include "core/pitch_utils.h"
#include "core/velocity.h"
#include "track/melody_designer.h"
#include "track/phrase_cache.h"
#include "track/phrase_variation.h"
#include "track/vocal_helpers.h"
#include <algorithm>
#include <unordered_map>

namespace midisketch {

void generateVocalTrack(MidiTrack& track, Song& song,
                        const GeneratorParams& params, std::mt19937& rng,
                        const MidiTrack* motif_track,
                        const IHarmonyContext& harmony,
                        bool skip_collision_avoidance) {

  // Determine effective vocal range
  uint8_t effective_vocal_low = params.vocal_low;
  uint8_t effective_vocal_high = params.vocal_high;

  // Adjust vocal_high to account for modulation
  // After modulation, notes will be transposed up by modulationAmount semitones.
  // To ensure the final pitch stays within vocal_high, reduce effective_vocal_high.
  int8_t mod_amount = song.modulationAmount();
  if (mod_amount > 0) {
    int adjusted_high = static_cast<int>(params.vocal_high) - mod_amount;
    // Ensure at least 1 octave (12 semitones) range remains
    int min_high = static_cast<int>(effective_vocal_low) + 12;
    effective_vocal_high = static_cast<uint8_t>(std::max(min_high, adjusted_high));
  }

  // Adjust range for BackgroundMotif to avoid collision with motif
  if (params.composition_style == CompositionStyle::BackgroundMotif &&
      motif_track != nullptr && !motif_track->empty()) {
    auto [motif_low, motif_high] = motif_track->analyzeRange();

    if (motif_high > 72) {  // Motif in high register
      effective_vocal_high = std::min(effective_vocal_high, static_cast<uint8_t>(72));
      if (effective_vocal_high - effective_vocal_low < 12) {
        effective_vocal_low = std::max(static_cast<uint8_t>(48),
                                        static_cast<uint8_t>(effective_vocal_high - 12));
      }
    } else if (motif_low < 60) {  // Motif in low register
      effective_vocal_low = std::max(effective_vocal_low, static_cast<uint8_t>(65));
      if (effective_vocal_high - effective_vocal_low < 12) {
        effective_vocal_high = std::min(static_cast<uint8_t>(96),
                                         static_cast<uint8_t>(effective_vocal_low + 12));
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

  // Clear existing phrase boundaries for fresh generation
  song.clearPhraseBoundaries();

  // Process each section
  for (const auto& section : song.arrangement().sections()) {
    // Skip sections without vocals
    if (!sectionHasVocals(section.type)) {
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

    // Apply register shift for section (clamped to original range)
    int8_t register_shift = getRegisterShift(section.type, params.melody_params);
    // Register shift adjusts the preferred center but must not exceed original range
    uint8_t section_vocal_low = static_cast<uint8_t>(
        std::clamp(static_cast<int>(effective_vocal_low) + register_shift,
                   static_cast<int>(effective_vocal_low),
                   static_cast<int>(effective_vocal_high) - 6));  // At least 6 semitone range
    uint8_t section_vocal_high = static_cast<uint8_t>(
        std::clamp(static_cast<int>(effective_vocal_high) + register_shift,
                   static_cast<int>(effective_vocal_low) + 6,
                   static_cast<int>(effective_vocal_high)));  // Stay within original high

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
      section_notes = adjustPitchRange(section_notes,
                                        cached.vocal_low, cached.vocal_high,
                                        section_vocal_low, section_vocal_high);

      // Re-apply collision avoidance (chord context may differ)
      if (!skip_collision_avoidance) {
        applyCollisionAvoidanceWithIntervalConstraint(
            section_notes, harmony, section_vocal_low, section_vocal_high);
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
      ctx.density_modifier = getDensityModifier(section.type, params.melody_params);
      ctx.thirtysecond_ratio = getThirtysecondRatio(section.type, params.melody_params);
      ctx.consecutive_same_note_prob = getConsecutiveSameNoteProb(section.type, params.melody_params);
      ctx.disable_vowel_constraints = params.melody_params.disable_vowel_constraints;
      ctx.disable_breathing_gaps = params.melody_params.disable_breathing_gaps;
      ctx.vocal_attitude = params.vocal_attitude;
      ctx.hook_intensity = params.hook_intensity;  // For HookSkeleton selection

      // Set transition info for next section (if any)
      const auto& sections = song.arrangement().sections();
      for (size_t i = 0; i < sections.size(); ++i) {
        if (&sections[i] == &section && i + 1 < sections.size()) {
          ctx.transition_to_next = getTransition(section.type, sections[i + 1].type);
          break;
        }
      }

      // Generate melody with evaluation (candidate count varies by section importance)
      int candidate_count = MelodyDesigner::getCandidateCountForSection(section.type);
      section_notes = designer.generateSectionWithEvaluation(
          section_tmpl, ctx, harmony, rng, params.vocal_style,
          params.melodic_complexity, candidate_count);

      // Apply transition approach if transition info was set
      if (ctx.transition_to_next) {
        designer.applyTransitionApproach(section_notes, ctx, harmony);
      }

      // Apply HarmonyContext collision avoidance with interval constraint
      if (!skip_collision_avoidance) {
        applyCollisionAvoidanceWithIntervalConstraint(
            section_notes, harmony, section_vocal_low, section_vocal_high);
      }

      // Extract GlobalMotif from first Chorus for song-wide melodic unity
      // Subsequent sections will receive bonus for similar contour/intervals
      if (section.type == SectionType::Chorus &&
          !designer.getCachedGlobalMotif().has_value()) {
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
            first_note, prev_note, first_note_chord_degree, kMaxMelodicInterval,
            section_vocal_low, section_vocal_high, nullptr);
        section_notes.front().note = static_cast<uint8_t>(new_pitch);
      }
    }
    for (auto& note : section_notes) {
      // ABSOLUTE CONSTRAINT: Ensure pitch is on scale (prevents chromatic notes)
      int snapped = snapToNearestScaleTone(note.note, 0);  // Always C major internally
      note.note = static_cast<uint8_t>(
          std::clamp(snapped,
                     static_cast<int>(section_vocal_low),
                     static_cast<int>(section_vocal_high)));
      all_notes.push_back(note);
    }
  }

  // NOTE: Modulation is NOT applied internally.
  // MidiWriter applies modulation to all tracks when generating MIDI bytes.
  // This ensures consistent behavior and avoids double-modulation.

  // Apply groove feel timing adjustments
  applyGrooveFeel(all_notes, params.vocal_groove);

  // Remove overlapping notes
  removeOverlaps(all_notes);

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
      int fixed_pitch = nearestChordToneWithinInterval(
          curr_pitch, prev_pitch, chord_degree, kMaxMelodicInterval,
          params.vocal_low, params.vocal_high, nullptr);
      all_notes[i].note = static_cast<uint8_t>(fixed_pitch);
    }
  }

  // Add notes to track (preserving provenance)
  for (const auto& note : all_notes) {
    track.addNote(note);
  }
}

}  // namespace midisketch
