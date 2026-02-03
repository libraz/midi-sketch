/**
 * @file rhythm_generator.cpp
 * @brief Implementation of rhythm generation for melody phrases.
 */

#include "track/melody/rhythm_generator.h"

#include <algorithm>
#include <cmath>

#include "core/chord_utils.h"
#include "core/velocity.h"

namespace midisketch {
namespace melody {

std::vector<RhythmNote> generatePhraseRhythmImpl(const MelodyTemplate& tmpl, uint8_t phrase_beats,
                                                  float density_modifier, float thirtysecond_ratio,
                                                  std::mt19937& rng, GenerationParadigm paradigm,
                                                  float syncopation_weight,
                                                  SectionType section_type) {
  std::vector<RhythmNote> rhythm;
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  float current_beat = 0.0f;
  float end_beat = static_cast<float>(phrase_beats);

  // Apply section density modifier to sixteenth density
  float effective_sixteenth_density = tmpl.sixteenth_density * density_modifier;
  // Clamp to valid range [0.0, 0.95]
  effective_sixteenth_density = std::min(effective_sixteenth_density, 0.95f);

  // Reserve space for final phrase-ending note
  // UltraVocaloid: shorter reservation to maximize machine-gun notes
  // Standard: 0.5 beat reservation (reduced from 1.0 to increase density)
  float phrase_body_end = (thirtysecond_ratio >= 0.8f) ? end_beat - 0.25f : end_beat - 0.5f;

  // Track consecutive short notes to prevent breath-difficult passages
  // Pop vocal principle: limit rapid-fire notes to maintain singability
  // UltraVocaloid: allow machine-gun bursts (32+ consecutive short notes)
  int consecutive_short_count = 0;
  int max_consecutive_short = (thirtysecond_ratio >= 0.8f) ? 32 : 3;

  // Track previous note duration for "溜め→爆発" (hold→burst) pattern
  // After a long note (>=half note), boost density to create energy release
  float prev_note_eighths = 0.0f;
  constexpr float kLongNoteThreshold = 4.0f;       // Half note (4 eighths)
  constexpr float kPostLongNoteDensityBoost = 1.3f;  // 30% density increase

  // UltraVocaloid: Random start pattern for natural variation
  // 0 = immediate 32nd notes, 1 = quarter accent first, 2 = gradual acceleration
  int ultra_start_pattern = 0;
  if (thirtysecond_ratio >= 0.8f) {
    float r = dist(rng);
    if (r < 0.5f) {
      ultra_start_pattern = 0;  // 50%: immediate machine-gun
    } else if (r < 0.8f) {
      ultra_start_pattern = 1;  // 30%: quarter note accent first
    } else {
      ultra_start_pattern = 2;  // 20%: gradual acceleration
    }
  }

  while (current_beat < phrase_body_end) {
    // Check if current position is on a strong beat (integer beat: 0.0, 1.0, 2.0, 3.0)
    // Pop music principle: strong beats should have longer, more stable notes
    float frac = current_beat - std::floor(current_beat);
    bool is_on_beat = frac < 0.01f;

    // Syncopation: with probability based on syncopation_weight, shift off-beat
    // This creates rhythmic interest by placing notes on upbeats (8th note offset)
    // Only apply at the start of each beat (not within an off-beat already)
    // Guard: don't syncopate if it would push us past the phrase body end
    //
    // Note: Only consume RNG when syncopation is possible to avoid changing
    // downstream generation for the default case (syncopation_weight = 0).
    bool apply_syncopation = false;
    if (is_on_beat && syncopation_weight > 0.0f && current_beat + 0.5f < phrase_body_end) {
      // Calculate context-aware syncopation weight
      // Phrase progress: 0.0 at start, 1.0 at end
      float phrase_progress = current_beat / end_beat;
      int beat_in_bar = static_cast<int>(current_beat) % 4;
      float contextual_weight =
          getContextualSyncopationWeight(syncopation_weight, phrase_progress, beat_in_bar, section_type);

      float synco_roll = dist(rng);
      if (synco_roll < contextual_weight) {
        // Skip this strong beat, advance to the off-beat (8th note = 0.5 beats)
        apply_syncopation = true;
        current_beat += 0.5f;
        frac = 0.5f;
        is_on_beat = false;
      }
    }
    (void)apply_syncopation;  // Suppress unused variable warning

    // Determine note duration (in eighths, float to support 32nds)
    float eighths;

    // UltraVocaloid (thirtysecond_ratio >= 0.8): allow fast notes even on strong beats
    bool force_long_on_beat = is_on_beat && !tmpl.rhythm_driven && thirtysecond_ratio < 0.8f;

    // UltraVocaloid: Insert phrase-ending long note at the end of each phrase
    // Creates natural breathing points in machine-gun passages
    // Triggers when we're in the last 1 beat of the phrase
    bool ultra_phrase_boundary = false;
    if (thirtysecond_ratio >= 0.8f) {
      float beats_remaining = phrase_body_end - current_beat;
      // If we're in the last 1 beat of the phrase, insert a long note
      if (beats_remaining <= 1.0f && beats_remaining > 0.1f) {
        ultra_phrase_boundary = true;
      }
    }

    // UltraVocaloid: Handle start pattern variations
    bool ultra_start_zone = (thirtysecond_ratio >= 0.8f && current_beat < 2.0f);

    if (ultra_phrase_boundary) {
      // Phrase boundary: insert long note that extends to the 2-bar boundary
      // Duration fills the remaining time until the boundary (quarter note)
      eighths = 2.0f;  // Quarter note
      consecutive_short_count = 0;
    } else if (ultra_start_zone && ultra_start_pattern > 0) {
      // Start pattern variations
      if (ultra_start_pattern == 1) {
        // Pattern 1: Quarter note accent on beat 0, then machine-gun
        if (current_beat < 0.01f) {
          eighths = 2.0f;  // Quarter note accent
        } else {
          eighths = 0.25f;  // Then 32nd notes
        }
      } else {
        // Pattern 2: Gradual acceleration (quarter -> 8th -> 16th -> 32nd)
        if (current_beat < 0.5f) {
          eighths = 2.0f;  // Quarter note
        } else if (current_beat < 1.0f) {
          eighths = 1.0f;  // 8th note
        } else if (current_beat < 1.5f) {
          eighths = 0.5f;  // 16th note
        } else {
          eighths = 0.25f;  // 32nd note
        }
      }
    } else if (force_long_on_beat) {
      // Strong beat (non-UltraVocaloid): allow shorter notes for denser melodies
      // Base 30% chance for 8th notes on strong beats, plus density bonus
      // This creates J-POP/K-POP conversational feel with more rhythmic activity
      float eighth_prob = 0.30f + effective_sixteenth_density * 0.3f;  // 30-60% for 8th
      float half_prob = tmpl.long_note_ratio * 0.8f;  // Reduced half note probability
      float roll = dist(rng);
      if (roll < eighth_prob) {
        eighths = 1.0f;  // 8th note
      } else if (roll < eighth_prob + half_prob) {
        eighths = 4.0f;  // Half note
      } else {
        eighths = 2.0f;  // Quarter note
      }
      consecutive_short_count = 0;  // Reset counter on strong beat
    } else {
      // Weak beat: favor shorter notes for density
      // Apply "溜め→爆発" (hold→burst) pattern: boost density after long notes
      float local_density_boost = 1.0f;
      if (prev_note_eighths >= kLongNoteThreshold) {
        local_density_boost = kPostLongNoteDensityBoost;
      }

      if (thirtysecond_ratio > 0.0f && dist(rng) < thirtysecond_ratio * local_density_boost) {
        eighths = 0.25f;  // 32nd note (0.25 eighth = 60 ticks)
      } else if (dist(rng) < (0.35f + effective_sixteenth_density) * local_density_boost) {
        // 35% base + density bonus for 8th notes (removed rhythm_driven requirement)
        eighths = 1.0f;  // 8th note
      } else if (dist(rng) < tmpl.long_note_ratio * 0.5f / local_density_boost) {
        eighths = 4.0f;  // Half note (reduced probability)
      } else {
        eighths = 2.0f;  // Quarter note
      }
    }

    // Enforce consecutive short note limit for singability
    // Vocal physiology: too many rapid notes without breath points causes strain
    // UltraVocaloid: relaxed limit (32) allows machine-gun passages
    if (eighths <= 1.0f) {
      consecutive_short_count++;
      if (consecutive_short_count >= max_consecutive_short) {
        eighths = 2.0f;  // Force quarter note for breathing room
        consecutive_short_count = 0;
      }
    } else {
      consecutive_short_count = 0;
    }

    // Check if strong beat
    bool strong = (static_cast<int>(current_beat) % 2 == 0);

    // Store actual eighths value as float to preserve short note durations
    rhythm.push_back({current_beat, eighths, strong});

    // Track previous note for "溜め→爆発" pattern
    prev_note_eighths = eighths;

    current_beat += eighths * 0.5f;  // Convert eighths to beats

    // Quantize to grid based on paradigm and style
    // UltraVocaloid 32nd grid takes priority (explicit vocal style choice)
    // RhythmSync uses 16th note grid for tighter rhythm sync
    if (ultra_phrase_boundary) {
      // After phrase boundary note, skip to phrase body end (exit the while loop)
      current_beat = phrase_body_end;
    } else if (thirtysecond_ratio >= 0.8f) {
      // UltraVocaloid: 32nd note grid for machine-gun bursts
      // Beat positions: 0, 0.125, 0.25, 0.375, 0.5, ...
      current_beat = std::ceil(current_beat * 8.0f) / 8.0f;
    } else if (paradigm == GenerationParadigm::RhythmSync) {
      // 16th note grid: 0, 0.25, 0.5, 0.75, 1.0, 1.25, ...
      current_beat = std::ceil(current_beat * 4.0f) / 4.0f;
    } else {
      // Traditional: 8th note grid for natural pop vocal rhythm
      // Standard pop vocal beat positions: 0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5
      current_beat = std::ceil(current_beat * 2.0f) / 2.0f;
    }
  }

  // Add final phrase-ending note on a strong beat
  // In pop music, phrases should end on strong beats (1, 2, 3, 4) with longer notes
  if (phrase_beats >= 2) {
    // Snap to nearest integer beat for the final note
    float final_beat = std::floor(current_beat);
    if (final_beat < current_beat) {
      final_beat = std::ceil(current_beat);
    }
    // Ensure we don't exceed the phrase
    if (final_beat >= end_beat) {
      final_beat = end_beat - 1.0f;
    }
    // Final note duration: UltraVocaloid uses quarter note for phrase ending
    float final_eighths = (end_beat - final_beat) * 2.0f;
    if (thirtysecond_ratio >= 0.8f) {
      final_eighths = std::max(final_eighths, 2.0f);  // At least quarter note for UltraVocaloid
    } else {
      final_eighths = std::max(final_eighths, 2.0f);  // At least quarter note
    }

    rhythm.push_back({final_beat, final_eighths, true});  // Strong beat
  }

  return rhythm;
}

// ============================================================================
// Enhanced Locked Rhythm Pitch Selection
// ============================================================================
// Addresses the melodic quality issues in RhythmSync paradigm:
// 1. Direction bias based on phrase position (ascending start, resolving end)
// 2. Direction inertia to maintain melodic momentum
// 3. GlobalMotif interval pattern reference for song-wide unity

/// @brief Section-specific direction bias thresholds.
/// @return {ascending_end, descending_start} for phrase position.
static std::pair<float, float> getDirectionBiasThresholds(SectionType type) {
  switch (type) {
    case SectionType::Chorus:
      return {0.25f, 0.75f};  // Stronger arch shape for memorable melody
    case SectionType::A:
      return {0.40f, 0.60f};  // Flatter for storytelling
    case SectionType::Bridge:
      return {0.50f, 0.50f};  // Symmetric for contrast
    default:
      return {0.30f, 0.70f};  // Default
  }
}

/// @brief Section-specific maximum direction inertia.
/// Verse sections have lower inertia for more restrained movement.
static int getMaxInertia(SectionType type) {
  switch (type) {
    case SectionType::Chorus:
      return 3;  // Dynamic melodic movement
    case SectionType::A:
      return 2;  // Restrained for storytelling
    case SectionType::Bridge:
      return 2;  // Contrast with chorus
    default:
      return 3;
  }
}

uint8_t selectPitchForLockedRhythmEnhancedImpl(
    uint8_t prev_pitch, int8_t chord_degree, uint8_t vocal_low, uint8_t vocal_high,
    const LockedRhythmContext& ctx, std::mt19937& rng) {
  // Build candidate pitch classes based on VocalAttitude
  std::vector<int> candidate_pcs;

  if (ctx.vocal_attitude == VocalAttitude::Raw) {
    // Raw: All diatonic scale tones allowed (rule-breaking)
    candidate_pcs = {0, 2, 4, 5, 7, 9, 11};  // C major diatonic
  } else {
    // Start with chord tones
    candidate_pcs = getChordTonePitchClasses(chord_degree);

    // Expressive: Add tensions (9th, 13th) for colorful harmonies
    if (ctx.vocal_attitude >= VocalAttitude::Expressive && !candidate_pcs.empty()) {
      int root = candidate_pcs[0];
      candidate_pcs.push_back((root + 2) % 12);   // 9th = root + 2
      candidate_pcs.push_back((root + 9) % 12);   // 13th = root + 9
    }
  }

  // Collect candidate pitches within vocal range
  std::vector<uint8_t> candidates;
  for (int pc : candidate_pcs) {
    for (int octave = 3; octave <= 7; ++octave) {
      int pitch = pc + (octave * 12);
      if (pitch >= vocal_low && pitch <= vocal_high) {
        candidates.push_back(static_cast<uint8_t>(pitch));
      }
    }
  }

  if (candidates.empty()) {
    // Fallback: use diatonic scale tones
    for (int p = vocal_low; p <= vocal_high; ++p) {
      int pc = p % 12;
      // C major diatonic: 0, 2, 4, 5, 7, 9, 11
      if (pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11) {
        candidates.push_back(static_cast<uint8_t>(p));
      }
    }
  }

  if (candidates.empty()) {
    return prev_pitch;  // Safety fallback
  }

  // =========================================================================
  // Phase 1: Apply direction bias based on phrase position
  // =========================================================================
  // Section-specific thresholds for melodic arch shape
  auto [ascending_end, descending_start] = getDirectionBiasThresholds(ctx.section_type);
  int direction_bias = 0;  // -1 = prefer down, 0 = neutral, +1 = prefer up
  if (ctx.phrase_position < ascending_end) {
    direction_bias = 1;   // Ascending bias at start
  } else if (ctx.phrase_position > descending_start) {
    direction_bias = -1;  // Descending bias at end (resolution)
  }

  // =========================================================================
  // Phase 2: Apply direction inertia
  // =========================================================================
  // Direction inertia creates melodic momentum - once moving up/down,
  // continue that direction to create smooth phrases
  // Section-specific maximum inertia (Verse is more restrained)
  int max_inertia = getMaxInertia(ctx.section_type);
  int clamped_inertia = std::clamp(ctx.direction_inertia, -max_inertia, max_inertia);
  if (clamped_inertia > 1) {
    direction_bias = std::max(direction_bias, 1);  // Strong upward momentum
  } else if (clamped_inertia < -1) {
    direction_bias = std::min(direction_bias, -1); // Strong downward momentum
  }

  // =========================================================================
  // Phase 3: Check GlobalMotif interval pattern
  // =========================================================================
  // If we have a cached GlobalMotif, try to follow its interval pattern
  // This creates song-wide melodic unity
  // Use modulo to cycle through motif when note_index exceeds motif length
  int motif_target = -1;
  if (ctx.motif_intervals != nullptr && ctx.motif_interval_count > 0) {
    size_t motif_idx = ctx.note_index % ctx.motif_interval_count;
    int8_t interval = ctx.motif_intervals[motif_idx];
    int target = static_cast<int>(prev_pitch) + interval;
    // Clamp to vocal range
    target = std::clamp(target, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
    motif_target = target;
  }

  // =========================================================================
  // Phase 4: Score and select best candidate
  // =========================================================================
  std::vector<std::pair<uint8_t, float>> scored_candidates;
  scored_candidates.reserve(candidates.size());

  for (uint8_t pitch : candidates) {
    float score = 1.0f;
    int movement = static_cast<int>(pitch) - static_cast<int>(prev_pitch);
    int abs_movement = std::abs(movement);

    // 4.1: Stepwise preference (most important for singability)
    // Prefer small intervals (1-2 semitones = step, 3-4 = small skip)
    // P5 (7 semitones) is common in J-POP melodies and should not be penalized
    if (abs_movement <= 2) {
      score += 0.4f;  // Strong bonus for stepwise
    } else if (abs_movement <= 4) {
      score += 0.2f;  // Moderate bonus for small skip
    } else if (abs_movement >= 9) {
      score -= 0.3f;  // Penalty for large leaps (>= M6)
    }

    // 4.2: Direction bias alignment
    if (direction_bias != 0) {
      if ((movement > 0 && direction_bias > 0) || (movement < 0 && direction_bias < 0)) {
        score += 0.25f;  // Bonus for matching direction preference
      } else if ((movement > 0 && direction_bias < 0) || (movement < 0 && direction_bias > 0)) {
        score -= 0.15f;  // Penalty for opposing direction
      }
    }

    // 4.3: GlobalMotif target alignment
    if (motif_target >= 0) {
      int dist_to_motif = std::abs(static_cast<int>(pitch) - motif_target);
      if (dist_to_motif == 0) {
        score += 0.3f;  // Exact match with motif target
      } else if (dist_to_motif <= 2) {
        score += 0.15f; // Close to motif target
      }
    }

    // 4.4: Tessitura center preference (comfortable singing range)
    int dist_to_center = std::abs(static_cast<int>(pitch) - static_cast<int>(ctx.tessitura_center));
    if (dist_to_center <= 6) {
      score += 0.1f;   // Bonus for staying near tessitura center
    } else if (dist_to_center > 12) {
      score -= 0.1f;   // Penalty for straying far from center
    }

    // 4.5: Progressive penalty for consecutive same-pitch notes
    // Music theory: 1-2 consecutive same notes = OK (rhythmic figure)
    //               3 consecutive = moderate penalty
    //               4+ consecutive = very strong penalty (monotonous)
    // Penalty must be strong enough to overcome stepwise bonus (+0.4f)
    // RhythmSync compatibility: candidates may be limited due to Motif collision,
    // so penalty must be high enough to force movement when alternatives exist.
    if (movement == 0) {
      if (ctx.same_pitch_streak >= 3) {
        score -= 5.0f;  // 4th+ note: extreme penalty (force movement)
      } else if (ctx.same_pitch_streak >= 2) {
        score -= 2.0f;  // 3rd note: very strong penalty
      } else if (ctx.same_pitch_streak >= 1) {
        score -= 0.5f;  // 2nd note: moderate penalty
      }
      // 1st note (streak=0): no penalty - first occurrence is fine
    }

    scored_candidates.emplace_back(pitch, score);
  }

  // Sort by score (highest first)
  std::sort(scored_candidates.begin(), scored_candidates.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  // Weighted probabilistic selection from top candidates
  // This maintains some variety while preferring better options
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  float roll = dist(rng);

  // Top candidate: 55%, Second: 25%, Third: 15%, Fourth+: 5%
  if (roll < 0.55f || scored_candidates.size() == 1) {
    return scored_candidates[0].first;
  } else if (roll < 0.80f && scored_candidates.size() > 1) {
    return scored_candidates[1].first;
  } else if (roll < 0.95f && scored_candidates.size() > 2) {
    return scored_candidates[2].first;
  } else if (scored_candidates.size() > 3) {
    // Random from remaining top candidates
    size_t max_idx = std::min(static_cast<size_t>(6), scored_candidates.size());
    std::uniform_int_distribution<size_t> idx_dist(3, max_idx - 1);
    return scored_candidates[idx_dist(rng)].first;
  }

  return scored_candidates[0].first;
}

}  // namespace melody
}  // namespace midisketch
