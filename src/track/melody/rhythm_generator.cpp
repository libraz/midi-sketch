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
  // Standard: 1 beat reservation for proper cadence
  float phrase_body_end = (thirtysecond_ratio >= 0.8f) ? end_beat - 0.5f : end_beat - 1.0f;

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
      // Strong beat (non-UltraVocaloid): prioritize longer notes for natural vocal phrasing
      // Avoid 16th/32nd notes on downbeats - they break the rhythmic anchor
      if (dist(rng) < tmpl.long_note_ratio * 2.0f) {
        eighths = 4.0f;  // Half note (doubled probability on strong beat)
      } else {
        eighths = 2.0f;  // Quarter note (default for strong beats)
      }
      consecutive_short_count = 0;  // Reset counter on strong beat
    } else {
      // Weak beat: use existing logic for rhythmic variety
      // Apply "溜め→爆発" (hold→burst) pattern: boost density after long notes
      float local_density_boost = 1.0f;
      if (prev_note_eighths >= kLongNoteThreshold) {
        local_density_boost = kPostLongNoteDensityBoost;
      }

      if (thirtysecond_ratio > 0.0f && dist(rng) < thirtysecond_ratio * local_density_boost) {
        eighths = 0.25f;  // 32nd note (0.25 eighth = 60 ticks)
      } else if (tmpl.rhythm_driven &&
                 dist(rng) < effective_sixteenth_density * local_density_boost) {
        eighths = 1.0f;  // 16th note (0.5 eighth)
      } else if (dist(rng) < tmpl.long_note_ratio / local_density_boost) {
        eighths = 4.0f;  // Half note (less likely after long note)
      } else {
        eighths = 2.0f;  // Quarter note (most common)
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

uint8_t selectPitchForLockedRhythmImpl(uint8_t prev_pitch, int8_t chord_degree, uint8_t vocal_low,
                                       uint8_t vocal_high, std::mt19937& rng) {
  // Get chord tone pitch classes (0-11) for the current chord (prioritize consonance)
  std::vector<int> chord_tone_pcs = getChordTonePitchClasses(chord_degree);

  // Collect candidate pitches within vocal range
  std::vector<uint8_t> candidates;

  // Add chord tones in range (primary candidates)
  // pitch_class is the pitch modulo 12 (e.g., C=0, C#=1, ..., B=11)
  for (int pc : chord_tone_pcs) {
    for (int octave = 3; octave <= 7; ++octave) {
      int pitch = pc + (octave * 12);
      if (pitch >= vocal_low && pitch <= vocal_high) {
        candidates.push_back(static_cast<uint8_t>(pitch));
      }
    }
  }

  if (candidates.empty()) {
    // Fallback: use any pitch in range
    for (int p = vocal_low; p <= vocal_high; ++p) {
      candidates.push_back(static_cast<uint8_t>(p));
    }
  }

  if (candidates.empty()) {
    return prev_pitch;  // Safety fallback
  }

  // Sort candidates by distance from prev_pitch (prefer stepwise motion)
  std::sort(candidates.begin(), candidates.end(), [prev_pitch](uint8_t a, uint8_t b) {
    return std::abs(static_cast<int>(a) - prev_pitch) < std::abs(static_cast<int>(b) - prev_pitch);
  });

  // Weight selection: prefer close pitches but allow some variety
  // 60% chance: closest pitch (stepwise)
  // 30% chance: second closest
  // 10% chance: random from top 4
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  float roll = dist(rng);

  size_t idx = 0;
  if (roll < 0.6f) {
    idx = 0;  // Closest
  } else if (roll < 0.9f && candidates.size() > 1) {
    idx = 1;  // Second closest
  } else if (candidates.size() > 2) {
    // Random from top 4
    size_t max_idx = std::min(static_cast<size_t>(4), candidates.size());
    std::uniform_int_distribution<size_t> idx_dist(0, max_idx - 1);
    idx = idx_dist(rng);
  }

  return candidates[idx];
}

}  // namespace melody
}  // namespace midisketch
