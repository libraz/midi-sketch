/**
 * @file locked_rhythm_generator.cpp
 * @brief Locked rhythm generation for vocal track (RhythmSync paradigm).
 *
 * Extracted from vocal.cpp. Generates vocal pitch sequences over a
 * pre-determined (locked) rhythm pattern with candidate evaluation.
 */

#include "track/vocal/locked_rhythm_generator.h"

#include <algorithm>
#include <set>

#include "core/i_harmony_context.h"
#include "core/melody_evaluator.h"
#include "core/mood_utils.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/rng_util.h"
#include "track/melody/motif_support.h"
#include "track/vocal/phrase_cache.h"
#include "track/vocal/phrase_planner.h"
#include "track/vocal/rhythm_lock_evaluator.h"
#include "track/vocal/vocal_pitch_hints.h"

namespace midisketch {

float computeGateRatio(SectionType section_type) {
  switch (section_type) {
    case SectionType::Chorus:
    case SectionType::Drop:
      return 0.96f;
    case SectionType::B:
      return 0.94f;
    case SectionType::Bridge:
      return 0.96f;
    case SectionType::A:
    default:
      return 0.90f;
  }
}

Tick computePhraseEndMinDuration(SectionType section_type, uint16_t bpm) {
  constexpr float kMinPhraseEndSeconds = 0.5f;
  Tick bpm_phrase_end_min = static_cast<Tick>(
      kMinPhraseEndSeconds * bpm * TICKS_PER_BEAT / 60.0f);

  switch (section_type) {
    case SectionType::Chorus:
    case SectionType::Drop:
      return std::max(TICK_HALF, bpm_phrase_end_min);
    case SectionType::B:
    case SectionType::Bridge:
      return std::max(TICK_QUARTER + TICK_EIGHTH, bpm_phrase_end_min);
    default:
      return std::max(TICK_QUARTER, bpm_phrase_end_min);
  }
}

void updateMelodicState(LockedRhythmMelodicState& state, uint8_t new_pitch) {
  int movement = static_cast<int>(new_pitch) - static_cast<int>(state.prev_pitch);
  if (movement > 0) {
    state.direction_inertia = std::min(state.direction_inertia + 1, 3);
    state.same_pitch_streak = 0;
  } else if (movement < 0) {
    state.direction_inertia = std::max(state.direction_inertia - 1, -3);
    state.same_pitch_streak = 0;
  } else {
    if (state.direction_inertia > 0) state.direction_inertia--;
    if (state.direction_inertia < 0) state.direction_inertia++;
    state.same_pitch_streak++;
  }
  state.prev_pitch = new_pitch;
}

uint8_t selectPitchForOnset(const std::vector<PitchCandidate>& candidates,
                            const LockedRhythmMelodicState& state,
                            Tick hint_duration,
                            const MelodyDesigner::SectionContext& ctx,
                            const PhrasePlan* phrase_plan, size_t onset_idx,
                            const std::vector<OnsetContourInfo>& onset_contours,
                            std::mt19937& rng) {
  // Force movement after 3 consecutive same pitches
  if (state.same_pitch_streak >= 3 && candidates.size() > 1) {
    std::vector<uint8_t> different_pitches;
    for (const auto& c : candidates) {
      if (c.pitch != state.prev_pitch) {
        different_pitches.push_back(c.pitch);
      }
    }
    if (!different_pitches.empty()) {
      return rng_util::selectRandom(rng, different_pitches);
    }
    // All candidates are same pitch - fall through to best candidate selection
    PitchSelectionHints hints = buildPitchHints(state, hint_duration, ctx, phrase_plan,
                                                 onset_idx, onset_contours);
    return selectBestCandidate(candidates, state.prev_pitch, hints);
  }

  PitchSelectionHints hints = buildPitchHints(state, hint_duration, ctx, phrase_plan,
                                               onset_idx, onset_contours);

  // Add randomness: 70% best candidate, 30% random from top 3
  if (candidates.size() >= 3 && rng_util::rollProbability(rng, 0.3f)) {
    size_t rand_idx = rng_util::rollRange(rng, 0,
        static_cast<int>(std::min(static_cast<size_t>(2), candidates.size() - 1)));
    return candidates[rand_idx].pitch;
  }
  return selectBestCandidate(candidates, state.prev_pitch, hints);
}

Tick computeNoteDuration(bool is_last_note, bool is_phrase_end, Tick tick,
                         Tick section_end, Tick next_onset, Tick available_span,
                         Tick breath_duration, Tick phrase_end_min,
                         float gate_ratio, uint8_t safe_pitch, uint8_t prev_pitch) {
  Tick duration;
  if (is_last_note) {
    duration = section_end - tick;
  } else if (is_phrase_end) {
    // Phrase-end note: sustain with breath gap before next phrase
    Tick breath_gap = breath_duration;
    if (available_span > breath_gap + TICK_SIXTEENTH) {
      duration = available_span - breath_gap;
    } else {
      // Very short span: use full span with gate ratio, no room for breath
      duration = static_cast<Tick>(available_span * gate_ratio);
    }
    duration = std::max(duration, phrase_end_min);
    if (tick + duration > next_onset) {
      duration = next_onset - tick;
    }
  } else {
    // Same pitch as previous: legato (no gap) to avoid unnatural micro-splits.
    // In singing, consecutive same-pitch notes are sustained as one long note.
    if (safe_pitch == prev_pitch) {
      duration = available_span;
    } else {
      duration = static_cast<Tick>(available_span * gate_ratio);
    }
    duration = std::max(duration, TICK_SIXTEENTH);
    if (tick + duration > next_onset) {
      duration = next_onset - tick;
    }
  }
  return duration;
}

std::vector<NoteEvent> generateLockedRhythmCandidate(
    const CachedRhythmPattern& rhythm, const Section& section, MelodyDesigner& /*designer*/,
    const IHarmonyContext& harmony, const MelodyDesigner::SectionContext& ctx, std::mt19937& rng,
    const PhrasePlan* phrase_plan) {
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

  // Pre-compute section-level parameters
  std::set<float> boundary_set = buildPhraseBoundarySet(phrase_plan, rhythm, section);
  std::set<float> phrase_start_beats = buildPhraseStartBeats(phrase_plan, section);

  bool is_ballad = MoodClassification::isBallad(ctx.mood);
  Tick breath_duration = getBreathDuration(section.type, is_ballad, false, ctx.bpm);
  float gate_ratio = computeGateRatio(section.type);
  Tick phrase_end_min = computePhraseEndMinDuration(section.type, ctx.bpm);
  std::vector<OnsetContourInfo> onset_contours = buildOnsetContourMap(phrase_plan, onsets, section);

  LockedRhythmMelodicState state;
  state.prev_pitch = (ctx.vocal_low + ctx.vocal_high) / 2;  // Start at center

  // Whether run-based onset map is active: breath gaps and density thinning
  // are already handled by buildRunBasedOnsetMap() in this mode.
  bool run_based_active =
      (phrase_plan != nullptr && !phrase_plan->phrases.empty() &&
       ctx.paradigm == GenerationParadigm::RhythmSync &&
       ctx.motif_params != nullptr &&
       ctx.motif_params->rhythm_template != MotifRhythmTemplate::None &&
       ctx.vocal_style != VocalStylePreset::UltraVocaloid);

  size_t i = 0;
  while (i < onsets.size()) {
    float beat = onsets[i];

    // Insert breath at phrase boundaries by shortening previous note.
    // When run-based onset map is active, breath gaps are already handled by
    // buildRunBasedOnsetMap(), so skip retroactive breath insertion.
    bool breath_handled_by_plan = run_based_active;
    if (i > 0 && boundary_set.count(beat) > 0 && !notes.empty() && !breath_handled_by_plan) {
      Tick min_duration = TICK_SIXTEENTH;
      if (notes.back().duration > breath_duration + min_duration) {
        notes.back().duration -= breath_duration;
      }
    }

    Tick tick = section.start_tick + static_cast<Tick>(beat * TICKS_PER_BEAT);
    Tick section_end = section.endTick();

    // Compute base available_span (to next onset)
    size_t next_idx = i + 1;
    Tick immediate_next = (next_idx < onsets.size())
        ? section.start_tick + static_cast<Tick>(onsets[next_idx] * TICKS_PER_BEAT)
        : section_end;
    Tick base_span = (immediate_next > tick) ? (immediate_next - tick) : TICK_SIXTEENTH;

    Tick base_duration = static_cast<Tick>(base_span * gate_ratio);
    base_duration = std::max(base_duration, TICK_SIXTEENTH);

    // Evaluate long-note desire BEFORE pitch selection.
    // When buildRunBasedOnsetMap has already controlled density (RhythmSync
    // with PhrasePlan), skip evaluateLongNoteDesire to prevent double-thinning.
    bool onset_pre_thinned = run_based_active;

    LongNoteDesire desire{0, 0.0f};
    if (!onset_pre_thinned) {
      desire = evaluateLongNoteDesire(i, onsets, section, boundary_set, state.onsets_since_long,
                                       ctx.bpm, phrase_start_beats);
    }

    // For likely-long notes, compute extended duration for pitch selection
    // so the chosen pitch is guaranteed safe for the full extension.
    Tick candidate_duration = base_duration;
    bool using_extended_candidates = false;
    if (desire.max_skip > 0 && desire.probability >= 0.3f) {
      size_t ext_active = std::min(i + 1 + static_cast<size_t>(desire.max_skip),
                                   onsets.size());
      Tick ext_onset = (ext_active < onsets.size())
          ? section.start_tick + static_cast<Tick>(onsets[ext_active] * TICKS_PER_BEAT)
          : section_end;
      if (ext_onset > tick) {
        candidate_duration = ext_onset - tick;
        using_extended_candidates = true;
      }
    }

    // Get chord at this position for provenance tracking
    [[maybe_unused]] int8_t chord_degree = harmony.getChordDegreeAt(tick);

    // Fetch pitch candidates with collision safety check.
    // When using extended candidates, fetch with the longer duration so the
    // selected pitch is safe across the full extension.
    auto candidates = getSafePitchCandidates(harmony, state.prev_pitch, tick, candidate_duration,
                                              TrackRole::Vocal, ctx.vocal_low, ctx.vocal_high,
                                              PitchPreference::Default, 10);

    // Fallback: if extended search yields no candidates, try with base duration
    if (candidates.empty() && using_extended_candidates) {
      candidates = getSafePitchCandidates(harmony, state.prev_pitch, tick, base_duration,
                                          TrackRole::Vocal, ctx.vocal_low, ctx.vocal_high,
                                          PitchPreference::Default, 10);
      desire.max_skip = 0;
      using_extended_candidates = false;
    }

    // Prefer diatonic (scale tone) candidates for vocal track.
    {
      auto it = std::remove_if(candidates.begin(), candidates.end(),
                               [](const PitchCandidate& c) { return !c.is_scale_tone; });
      if (it != candidates.begin()) {
        candidates.erase(it, candidates.end());
      }
    }

    if (candidates.empty()) {
      ++i;
      state.onsets_since_long++;
      continue;
    }

    // Select pitch
    Tick hint_duration = using_extended_candidates ? candidate_duration : base_duration;
    uint8_t safe_pitch = selectPitchForOnset(candidates, state, hint_duration, ctx, phrase_plan,
                                              i, onset_contours, rng);

    // Compute actual skips with the chosen pitch
    int actual_skips = 0;
    if (desire.max_skip > 0 && rng_util::rollProbability(rng, desire.probability)) {
      actual_skips = computeSafeSkipCount(
          safe_pitch, tick, onsets, i, desire.max_skip, section, harmony);
    }

    // Compute actual next_onset and available_span based on skips
    size_t next_active = i + 1 + static_cast<size_t>(actual_skips);
    Tick next_onset;
    bool is_last_note;
    if (next_active < onsets.size()) {
      next_onset = section.start_tick +
                   static_cast<Tick>(onsets[next_active] * TICKS_PER_BEAT);
      is_last_note = false;
    } else {
      next_onset = section_end;
      is_last_note = true;
    }
    Tick available_span = (next_onset > tick) ? (next_onset - tick) : TICK_SIXTEENTH;

    // Determine phrase-end and compute final duration
    bool is_phrase_end = isPhraseEndOnset(i, next_active, onsets, boundary_set,
                                          section_beats, is_last_note);

    // Note: Track collision clip (getMaxSafeEnd) is intentionally omitted for
    // the final duration. In RhythmSync, the Motif plays dense 8th-note patterns
    // and brief passing dissonance with a sustained vocal note is musically normal.
    // Extension safety is handled by computeSafeSkipCount() which checks both
    // chord boundary AND inter-track collision before allowing note extension.
    Tick duration = computeNoteDuration(is_last_note, is_phrase_end, tick, section_end,
                                        next_onset, available_span, breath_duration,
                                        phrase_end_min, gate_ratio, safe_pitch, state.prev_pitch);

    // Update melodic state (direction inertia, same-pitch streak)
    updateMelodicState(state, safe_pitch);

    // Compute velocity from accent pattern or beat position
    uint8_t velocity = computeOnsetVelocity(beat, ctx);

    NoteEvent note = createNoteWithoutHarmony(tick, duration, safe_pitch, velocity);
#ifdef MIDISKETCH_NOTE_PROVENANCE
    note.prov_source = static_cast<uint8_t>(NoteSource::MelodyPhrase);
    note.prov_chord_degree = chord_degree;
    note.prov_lookup_tick = tick;
    note.prov_original_pitch = safe_pitch;
#endif
    notes.push_back(note);

    // Advance: skip consumed onsets
    state.onsets_since_long = (actual_skips > 0) ? 0 : state.onsets_since_long + 1;
    i += 1 + static_cast<size_t>(actual_skips);
  }

  // Post-process: ensure phrase-end resolution by merging short tail notes
  postProcessPhraseEndResolution(notes, gate_ratio);

  return notes;
}

std::vector<NoteEvent> generateLockedRhythmWithEvaluation(
    const CachedRhythmPattern& rhythm, const Section& section, MelodyDesigner& designer,
    const IHarmonyContext& harmony, const MelodyDesigner::SectionContext& ctx, std::mt19937& rng,
    const PhrasePlan* phrase_plan) {

  constexpr int kCandidateCount = 20;  // 1/5 of normal mode (100) for performance

  // Generate multiple candidates
  std::vector<std::pair<std::vector<NoteEvent>, float>> candidates;
  candidates.reserve(static_cast<size_t>(kCandidateCount));

  for (int i = 0; i < kCandidateCount; ++i) {
    std::vector<NoteEvent> melody = generateLockedRhythmCandidate(
        rhythm, section, designer, harmony, ctx, rng, phrase_plan);

    if (melody.empty()) {
      continue;
    }

    // Evaluate the candidate
    // Style evaluation: positive features
    MelodyScore style_score = MelodyEvaluator::evaluate(melody, harmony);
    float style_total = style_score.total();  // Use simple average

    // Culling evaluation: penalty-based
    Tick phrase_duration = section.endTick() - section.start_tick;
    float culling_score = MelodyEvaluator::evaluateForCulling(
        melody, harmony, phrase_duration, ctx.vocal_style);

    // GlobalMotif bonus if available
    float motif_bonus = 0.0f;
    if (designer.getCachedGlobalMotif().has_value() &&
        designer.getCachedGlobalMotif()->isValid()) {
      motif_bonus = melody::evaluateWithGlobalMotif(
          melody, *designer.getCachedGlobalMotif());
    }

    // Combined score: 35% style, 40% culling, 25% motif
    // Higher motif weight strengthens RhythmSync "riff addiction" quality
    float combined_score = style_total * 0.35f + culling_score * 0.40f + motif_bonus * 0.25f;

    candidates.emplace_back(std::move(melody), combined_score);
  }

  if (candidates.empty()) {
    // Fallback: generate single candidate without evaluation
    return generateLockedRhythmCandidate(rhythm, section, designer, harmony, ctx, rng, phrase_plan);
  }

  // Sort by score (highest first)
  std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  // Keep top half
  size_t keep_count = std::max(static_cast<size_t>(1), candidates.size() / 2);

  // Weighted probabilistic selection from top candidates
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

}  // namespace midisketch
