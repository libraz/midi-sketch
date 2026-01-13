/**
 * @file melody_designer.cpp
 * @brief Implementation of MelodyDesigner track generation.
 */

#include "track/melody_designer.h"
#include "core/harmonic_rhythm.h"
#include "core/hook_utils.h"
#include "core/i_harmony_context.h"
#include "core/melody_embellishment.h"
#include "core/note_factory.h"
#include "core/phrase_patterns.h"
#include "core/timing_constants.h"
#include "core/vocal_style_profile.h"
#include <algorithm>
#include <cmath>

namespace midisketch {

namespace {

// Default velocity for melody notes
constexpr uint8_t DEFAULT_VELOCITY = 100;

// Get the bass root pitch class for a given chord degree.
// This is the note that bass will play as the harmonic foundation.
// @param chord_degree Chord degree (0-6 for diatonic)
// @return Pitch class of the bass root (0-11)
int getBassRootPitchClass(int8_t chord_degree) {
  // In diatonic system, bass plays the root of each chord
  // Degree 0 = I (root = 0/C), Degree 1 = ii (root = 2/D), etc.
  // Map: 0->0, 1->2, 2->4, 3->5, 4->7, 5->9, 6->11
  constexpr int DEGREE_TO_ROOT[] = {0, 2, 4, 5, 7, 9, 11};
  int normalized = ((chord_degree % 7) + 7) % 7;
  return DEGREE_TO_ROOT[normalized];
}

// Check if a pitch creates an "avoid note" relationship with ANY chord tone.
// Based on music theory: avoid notes are pitches that create:
// - Minor 2nd (1 semitone) with any chord tone (creates harsh beating)
// - Tritone (6 semitones) with chord root only (makes non-dominant chords sound dominant)
// Examples where this matters:
// - CMaj7: F (4th) is avoid because it's a minor 2nd from E (major 7th)
// - Am: F# would be avoid because it's a minor 2nd from G (if G were a chord tone)
// @param pitch_pc Pitch class of the melody note (0-11)
// @param chord_tones Vector of chord tone pitch classes
// @param root_pc Pitch class of the chord root (0-11) for tritone check
// @return true if the pitch should be avoided on strong beats
bool isAvoidNoteWithChord(int pitch_pc, const std::vector<int>& chord_tones, int root_pc) {
  // Check minor 2nd (1 semitone) against ALL chord tones
  for (int ct : chord_tones) {
    int interval = std::abs(pitch_pc - ct);
    if (interval > 6) interval = 12 - interval;
    if (interval == 1) {
      return true;  // Minor 2nd with any chord tone is avoid
    }
  }

  // Check tritone (6 semitones) against ROOT only
  // (Tritone with 5th is common in jazz voicings and often acceptable)
  int root_interval = std::abs(pitch_pc - root_pc);
  if (root_interval > 6) root_interval = 12 - root_interval;
  if (root_interval == 6) {
    return true;  // Tritone with root makes chord sound dominant
  }

  return false;
}

// Simplified version for backward compatibility - checks only against root
// Use isAvoidNoteWithChord for more accurate checking when chord tones are available
bool isAvoidNoteWithRoot(int pitch_pc, int root_pc) {
  int interval = std::abs(pitch_pc - root_pc);
  if (interval > 6) interval = 12 - interval;  // Normalize to 0-6
  // Minor 2nd (1) = half step above root, creates harsh dissonance
  // Tritone (6) = augmented 4th, makes chord sound like dominant
  return interval == 1 || interval == 6;
}

// Get the nearest chord tone that is NOT an avoid note with the root.
// @param current_pitch Current pitch to adjust
// @param chord_degree Chord degree for getting chord tones
// @param root_pc Root pitch class
// @param vocal_low Minimum allowed pitch
// @param vocal_high Maximum allowed pitch
// @return Adjusted pitch (nearest safe chord tone)
int getNearestSafeChordTone(int current_pitch, int8_t chord_degree, int root_pc,
                            uint8_t vocal_low, uint8_t vocal_high) {
  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);
  if (chord_tones.empty()) {
    return std::clamp(current_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
  }

  // Initialize best_pitch to clamped current_pitch (fallback if no candidates found)
  int best_pitch = std::clamp(current_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
  int best_distance = 100;

  // Search in multiple octaves
  for (int pc : chord_tones) {
    // Skip if this pitch class is an avoid note with root
    if (isAvoidNoteWithRoot(pc, root_pc)) continue;

    for (int oct = 3; oct <= 6; ++oct) {
      int candidate = oct * 12 + pc;
      if (candidate < static_cast<int>(vocal_low) ||
          candidate > static_cast<int>(vocal_high)) {
        continue;
      }
      int dist = std::abs(candidate - current_pitch);
      if (dist < best_distance) {
        best_distance = dist;
        best_pitch = candidate;
      }
    }
  }

  return best_pitch;
}

// Anchor tone pitch classes for Chorus/B sections (C major scale)
// (Also known as "structural tones" or "arrival tones" in music theory)
//
// These pitch classes establish stable harmonic anchors for phrase starts:
//   - C (0): Tonic - maximum stability, "home" feeling
//   - G (7): Dominant - creates tension/expectation, strong pull to tonic
//   - A (9): Relative minor (vi) - adds emotional color while staying diatonic
//
// Rationale for I, V, vi selection:
//   - These are the most structurally important pitches in major key harmony
//   - Using them at phrase starts creates memorable, singable hooks
//   - The cycling pattern (I→V→vi) provides variety while maintaining coherence
constexpr int8_t ANCHOR_TONE_PCS[] = {0, 7, 9};  // I(C), V(G), vi(A)

// Get anchor tone (structural tone) for Chorus/B section phrase starts.
// This creates a consistent starting point that makes hooks memorable.
// @param chord_degree Current chord degree
// @param tessitura_center Center of comfortable singing range
// @param vocal_low Minimum pitch
// @param vocal_high Maximum pitch
// @returns A pitch that serves as a stable anchor for the phrase
int getAnchorTonePitch(int8_t chord_degree, int tessitura_center,
                       uint8_t vocal_low, uint8_t vocal_high) {
  // Select target pitch class based on chord (cycles through I, V, vi)
  int target_pc = ANCHOR_TONE_PCS[std::abs(chord_degree) % 3];
  // Find the target pitch class in the octave containing tessitura center
  int base = (tessitura_center / 12) * 12 + target_pc;
  // Adjust to fit within vocal range
  if (base < static_cast<int>(vocal_low)) base += 12;
  if (base > static_cast<int>(vocal_high)) base -= 12;
  return std::clamp(base, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
}

// Calculate number of phrases in a section
uint8_t calculatePhraseCount(uint8_t section_bars, uint8_t phrase_length_bars) {
  if (phrase_length_bars == 0) phrase_length_bars = 2;
  return (section_bars + phrase_length_bars - 1) / phrase_length_bars;
}

// ============================================================================
// Pop Hook Rhythm Patterns
// ============================================================================
//
// Based on pop music theory, hook phrases use specific rhythm patterns that
// create catchiness through:
// 1. Strong-weak beat alignment: longer notes on downbeats
// 2. Syncopation: off-beat accents for groove
// 3. Breath points: natural gaps for singability
// 4. Motif closure: ending with longer note for resolution
//
// Duration values in eighths (1 = 8th note, 2 = quarter, 4 = half)
struct HookRhythmPattern {
  uint8_t durations[6];   // Note durations in eighths (0 = end marker)
  uint8_t note_count;     // Number of notes in pattern
  Tick gap_after;         // Gap after pattern (in ticks)
  const char* name;       // Pattern name for debugging
};

// Common pop hook rhythm patterns
// Each pattern is designed to be catchy and singable
constexpr HookRhythmPattern kHookRhythmPatterns[] = {
  // Pattern 1: "Ta-Ta-Taa" (8-8-4) - Classic buildup to resolution
  // Example: "Bad Guy" chorus, most K-pop hooks
  {{1, 1, 2, 0, 0, 0}, 3, TICK_EIGHTH, "buildup"},

  // Pattern 2: "Taa-Ta-Ta" (4-8-8) - Syncopated start
  // Example: "Shape of You" hook style
  {{2, 1, 1, 0, 0, 0}, 3, TICK_EIGHTH, "syncopated"},

  // Pattern 3: "Ta-Ta-Ta-Taa" (8-8-8-4) - Four-note energy
  // Example: "Dynamite" style, high energy J-pop
  {{1, 1, 1, 2, 0, 0}, 4, TICK_EIGHTH, "four-note"},

  // Pattern 4: "Taa-Taa" (4-4) - Simple and powerful
  // Example: "We Will Rock You" style, stadium anthems
  {{2, 2, 0, 0, 0, 0}, 2, TICK_QUARTER, "powerful"},

  // Pattern 5: "Ta-Taa-Ta" (8-4-8) - Dotted rhythm feel
  // Example: Swing-influenced pop hooks
  {{1, 2, 1, 0, 0, 0}, 3, TICK_EIGHTH, "dotted"},

  // Pattern 6: "Taa-Ta-Ta-Ta" (4-8-8-8) - Descending energy
  // Example: Call-and-response style hooks
  {{2, 1, 1, 1, 0, 0}, 4, TICK_SIXTEENTH, "call-response"},
};

constexpr size_t kHookRhythmPatternCount =
    sizeof(kHookRhythmPatterns) / sizeof(kHookRhythmPatterns[0]);

// Select a hook rhythm pattern index based on template characteristics
size_t selectHookRhythmPatternIndex(
    const MelodyTemplate& tmpl, std::mt19937& rng) {
  // Weight patterns based on template style
  std::vector<size_t> candidates;

  if (tmpl.rhythm_driven) {
    // Rhythm-driven: prefer energetic patterns
    candidates = {0, 2, 5};  // buildup, four-note, call-response
  } else if (tmpl.long_note_ratio > 0.3f) {
    // Sparse style: prefer simpler patterns
    candidates = {3, 1, 4};  // powerful, syncopated, dotted
  } else {
    // Balanced: use all patterns
    for (size_t i = 0; i < kHookRhythmPatternCount; ++i) {
      candidates.push_back(i);
    }
  }

  std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
  return candidates[dist(rng)];
}

}  // namespace

std::vector<NoteEvent> MelodyDesigner::generateSection(
    const MelodyTemplate& tmpl,
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
    uint8_t actual_beats = std::min(
        static_cast<uint8_t>(phrase_beats),
        static_cast<uint8_t>(remaining / TICKS_PER_BEAT));

    if (actual_beats < 2) break;  // Too short for a phrase

    // Generate hook for chorus at specific positions
    bool is_hook_position = (ctx.section_type == SectionType::Chorus) &&
                            (i == 0 || (i == 2 && phrase_count > 3));

    PhraseResult phrase_result;
    if (is_hook_position && tmpl.hook_note_count > 0) {
      phrase_result = generateHook(tmpl, current_tick, ctx, prev_pitch,
                                   harmony, rng);
    } else {
      phrase_result = generateMelodyPhrase(tmpl, current_tick, actual_beats,
                                           ctx, prev_pitch, direction_inertia,
                                           harmony, rng);
    }

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
          // Use nearestChordToneWithinInterval to stay on chord tones
          adjusted_note.note = static_cast<uint8_t>(
              nearestChordToneWithinInterval(adjusted_note.note, prev_note_pitch,
                                             note_chord_degree, MAX_PHRASE_INTERVAL,
                                             ctx.vocal_low, ctx.vocal_high,
                                             &ctx.tessitura));
        }
      }
      // ABSOLUTE CONSTRAINT: Ensure pitch is on scale (prevents chromatic notes)
      int snapped = snapToNearestScaleTone(adjusted_note.note, ctx.key_offset);
      adjusted_note.note = static_cast<uint8_t>(
          std::clamp(snapped, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high)));
      result.push_back(adjusted_note);
    }

    // Update state for next phrase
    prev_pitch = phrase_result.last_pitch;
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
    if (i < phrase_count - 1 && !ctx.disable_breathing_gaps) {
      current_tick += TICK_EIGHTH;  // Short breath
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

  // Final downbeat chord-tone enforcement
  // Ensures all notes on beat 1 are chord tones, even after embellishment
  for (auto& note : result) {
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
        int new_pitch = nearestChordTonePitch(note.note, chord_degree);
        new_pitch = std::clamp(new_pitch,
                               static_cast<int>(ctx.vocal_low),
                               static_cast<int>(ctx.vocal_high));
        note.note = static_cast<uint8_t>(new_pitch);
      }
    }
  }

  return result;
}

std::vector<NoteEvent> MelodyDesigner::generateSectionWithEvaluation(
    const MelodyTemplate& tmpl,
    const SectionContext& ctx,
    const IHarmonyContext& harmony,
    std::mt19937& rng,
    VocalStylePreset vocal_style,
    MelodicComplexity melodic_complexity,
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
    Tick phrase_duration = ctx.section_end - ctx.section_start;
    float culling_score = MelodyEvaluator::evaluateForCulling(melody, harmony, phrase_duration);

    // StyleBias evaluation: interval pattern preferences
    float bias_score = 1.0f;
    if (melody.size() >= 2) {
      int stepwise_count = 0, skip_count = 0, leap_count = 0;
      int same_pitch_count = 0;
      for (size_t j = 1; j < melody.size(); ++j) {
        int interval = std::abs(static_cast<int>(melody[j].note) -
                                static_cast<int>(melody[j - 1].note));
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
        bias_score = stepwise_ratio * bias.stepwise_weight +
                     skip_ratio * bias.skip_weight +
                     leap_ratio * bias.leap_weight +
                     same_ratio * bias.same_pitch_weight;
        // Normalize to ~1.0 range
        bias_score = bias_score / (bias.stepwise_weight + bias.skip_weight +
                                   bias.leap_weight + bias.same_pitch_weight) * 4.0f;
        bias_score = std::clamp(bias_score, 0.5f, 1.5f);
      }
    }

    // Combined score: 40% style, 40% culling, 20% bias
    float combined_score = style_total * 0.4f + culling_score * 0.4f + bias_score * 0.2f;

    // GlobalMotif bonus: light reward for similar contour/intervals (0.0-0.1)
    // Only applied if GlobalMotif has been extracted from chorus
    if (cached_global_motif_.has_value() && cached_global_motif_->isValid()) {
      float motif_bonus = evaluateWithGlobalMotif(melody, *cached_global_motif_);
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
    const MelodyTemplate& tmpl,
    Tick phrase_start,
    uint8_t phrase_beats,
    const SectionContext& ctx,
    int prev_pitch,
    int direction_inertia,
    const IHarmonyContext& harmony,
    std::mt19937& rng) {

  PhraseResult result;
  result.notes.clear();
  result.direction_inertia = direction_inertia;

  // Create NoteFactory for provenance tracking
  NoteFactory factory(harmony);

  // Generate rhythm pattern with section density modifier and 32nd note ratio
  std::vector<RhythmNote> rhythm = generatePhraseRhythm(tmpl, phrase_beats, ctx.density_modifier, ctx.thirtysecond_ratio, rng);

  // Get chord degree at phrase start
  int8_t start_chord_degree = harmony.getChordDegreeAt(phrase_start);

  // Calculate initial pitch if none provided
  int current_pitch;
  if (prev_pitch < 0) {
    // For Chorus/B sections, use anchor tone for memorable melodic anchoring
    if (ctx.section_type == SectionType::Chorus || ctx.section_type == SectionType::B) {
      current_pitch = getAnchorTonePitch(start_chord_degree, ctx.tessitura.center,
                                         ctx.vocal_low, ctx.vocal_high);
    } else {
      // Start near tessitura center for other sections
      current_pitch = ctx.tessitura.center;
      // Adjust to chord tone at phrase start
      current_pitch = nearestChordTonePitch(current_pitch, start_chord_degree);
      current_pitch = std::clamp(current_pitch,
                                 static_cast<int>(ctx.vocal_low),
                                 static_cast<int>(ctx.vocal_high));
    }
  } else {
    current_pitch = prev_pitch;
  }

  // Calculate target pitch if template has target
  int target_pitch = -1;
  if (tmpl.has_target_pitch) {
    target_pitch = calculateTargetPitch(tmpl, ctx, current_pitch, harmony, rng);
  }

  // Track consecutive same notes for J-POP style probability curve
  int consecutive_same_count = 0;

  // Track previous note duration for leap preparation principle
  // Pop vocal theory: large leaps need preparation time (longer preceding note)
  Tick prev_note_duration = TICKS_PER_BEAT;  // Default to quarter note

  // Generate notes for each rhythm position
  for (size_t i = 0; i < rhythm.size(); ++i) {
    const RhythmNote& rn = rhythm[i];
    float phrase_pos = static_cast<float>(i) / rhythm.size();

    // Calculate note timing first to get correct chord degree
    Tick note_start = phrase_start + static_cast<Tick>(rn.beat * TICKS_PER_BEAT);
    int8_t note_chord_degree = harmony.getChordDegreeAt(note_start);

    // Select pitch movement
    PitchChoice choice = selectPitchChoice(tmpl, phrase_pos,
                                           target_pitch >= 0, rng);

    // Apply direction inertia
    choice = applyDirectionInertia(choice, result.direction_inertia, tmpl, rng);

    // Check vowel section constraint (skip if vowel constraints disabled)
    if (tmpl.vowel_constraint && i > 0 && !ctx.disable_vowel_constraints) {
      bool same_vowel = isInSameVowelSection(
          rhythm[i-1].beat, rn.beat, phrase_beats);
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
    int new_pitch = applyPitchChoice(choice, current_pitch, target_pitch,
                                     note_chord_degree, ctx.key_offset,
                                     ctx.vocal_low, ctx.vocal_high,
                                     ctx.vocal_attitude);

    // Apply consecutive same note reduction with J-POP style probability curve
    // J-POP theory: rhythmic repetition is common but should taper off naturally
    //   2 notes: 85% allow (very common in hooks)
    //   3 notes: 50% allow (rhythmic emphasis)
    //   4 notes: 25% allow (occasional effect)
    //   5+ notes: 5% allow (rare, intentional)
    bool force_move = false;
    if (new_pitch == current_pitch) {
      consecutive_same_count++;

      // Calculate allow probability based on repetition count
      float allow_prob;
      switch (consecutive_same_count) {
        case 1: allow_prob = 1.0f; break;   // First note always OK
        case 2: allow_prob = 0.85f; break;  // 2nd repetition: 85%
        case 3: allow_prob = 0.50f; break;  // 3rd repetition: 50%
        case 4: allow_prob = 0.25f; break;  // 4th repetition: 25%
        default: allow_prob = 0.05f; break; // 5+: 5%
      }

      std::uniform_real_distribution<float> same_dist(0.0f, 1.0f);
      if (same_dist(rng) > allow_prob) {
        force_move = true;
      }
    } else {
      consecutive_same_count = 0;  // Reset counter when pitch changes
    }

    if (force_move) {
      // Force movement to a different chord tone (using note's chord degree)
      std::vector<int> chord_tones = getChordTonePitchClasses(note_chord_degree);
      std::vector<int> candidates;
      for (int pc : chord_tones) {
        for (int oct = 4; oct <= 6; ++oct) {
          int candidate = oct * 12 + pc;
          if (candidate >= ctx.vocal_low && candidate <= ctx.vocal_high &&
              candidate != current_pitch) {
            candidates.push_back(candidate);
          }
        }
      }
      if (!candidates.empty()) {
        // Pick closest different chord tone
        int best = candidates[0];
        int best_dist = std::abs(best - current_pitch);
        for (int c : candidates) {
          int dist = std::abs(c - current_pitch);
          if (dist > 0 && dist < best_dist) {
            best = c;
            best_dist = dist;
          }
        }
        new_pitch = best;
        consecutive_same_count = 0;  // Reset after forced movement
      }
    }

    // Enforce maximum interval constraint (major 6th = 9 semitones)
    // Use nearestChordToneWithinInterval to stay on chord tones
    constexpr int MAX_INTERVAL = 9;
    int interval = std::abs(new_pitch - current_pitch);
    if (interval > MAX_INTERVAL) {
      new_pitch = nearestChordToneWithinInterval(
          new_pitch, current_pitch, note_chord_degree, MAX_INTERVAL,
          ctx.vocal_low, ctx.vocal_high, &ctx.tessitura);
    }

    // Leap preparation principle (pop vocal theory):
    // Large leaps after very short notes are difficult to sing.
    // Singers need time to prepare for large pitch jumps.
    // After very short notes (< 0.5 beats), limit to perfect 4th (5 semitones).
    // This prevents awkward jumps while still allowing common pop intervals (3rds, 4ths).
    constexpr Tick VERY_SHORT_THRESHOLD = TICKS_PER_BEAT / 2;  // 0.5 beats (8th note)
    constexpr int MAX_LEAP_AFTER_SHORT = 5;  // Perfect 4th
    if (i > 0 && prev_note_duration < VERY_SHORT_THRESHOLD) {
      int leap = std::abs(new_pitch - current_pitch);
      if (leap > MAX_LEAP_AFTER_SHORT) {
        new_pitch = nearestChordToneWithinInterval(
            new_pitch, current_pitch, note_chord_degree, MAX_LEAP_AFTER_SHORT,
            ctx.vocal_low, ctx.vocal_high, &ctx.tessitura);
      }
    }

    // Avoid note check: melody should not form tritone/minor2nd with chord tones
    // This applies to ALL notes, not just strong beats, because bass establishes
    // the harmonic foundation throughout the bar.
    // Based on pop music theory: the bass root defines the chord's identity.
    // We check against ALL chord tones (not just root) to catch clashes like
    // F against E in CMaj7 (minor 2nd with the 7th).
    {
      int bass_root_pc = getBassRootPitchClass(note_chord_degree);
      std::vector<int> chord_tones = getChordTonePitchClasses(note_chord_degree);
      int pitch_pc = new_pitch % 12;
      if (isAvoidNoteWithChord(pitch_pc, chord_tones, bass_root_pc)) {
        // Adjust to nearest safe chord tone
        new_pitch = getNearestSafeChordTone(new_pitch, note_chord_degree, bass_root_pc,
                                             ctx.vocal_low, ctx.vocal_high);
      }
    }

    // Downbeat chord-tone constraint: beat 1 of each bar requires chord tones.
    // In pop music theory, the downbeat (beat 1) is the strongest metric position
    // and must establish clear harmonic grounding. Non-chord tones (tensions like
    // 9th, 11th, 13th) on beat 1 create harmonic ambiguity and weaken the phrase.
    // This is a fundamental principle in pop arranging - save tensions for weak
    // beats and passing tones, use chord tones (1, 3, 5) on strong beats.
    //
    // Music theory exceptions (not currently implemented):
    // - Appoggiatura: Non-chord tone on strong beat that resolves down by step
    //   Creates emotional tension (common in classical and ballads)
    // - Jazz: Tensions on downbeat are stylistically appropriate (9th, 11th, 13th)
    //
    // Current behavior: Strict enforcement for pop-style clarity.
    // Future enhancement: Genre parameter to relax this constraint for jazz/classical.
    {
      Tick bar_pos = note_start % TICKS_PER_BAR;
      bool is_downbeat = bar_pos < TICKS_PER_BEAT / 4;
      if (is_downbeat) {
        std::vector<int> chord_tones = getChordTonePitchClasses(note_chord_degree);
        int new_pc = new_pitch % 12;
        bool is_chord_tone = false;
        for (int ct : chord_tones) {
          if (new_pc == ct) {
            is_chord_tone = true;
            break;
          }
        }
        if (!is_chord_tone) {
          // Snap to nearest chord tone for strong harmonic grounding
          new_pitch = nearestChordTonePitch(new_pitch, note_chord_degree);
          new_pitch = std::clamp(new_pitch,
                                 static_cast<int>(ctx.vocal_low),
                                 static_cast<int>(ctx.vocal_high));
        }
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

    // Calculate duration based on next note's position or use eighths field
    // (note_start already calculated above)
    Tick note_duration;
    if (i + 1 < rhythm.size()) {
      // Duration until next note
      float beat_duration = rhythm[i + 1].beat - rn.beat;
      note_duration = static_cast<Tick>(beat_duration * TICKS_PER_BEAT);
    } else {
      // Last note: use eighths field
      note_duration = rn.eighths * TICK_EIGHTH;
    }

    // Vocal-friendly gate processing based on pop vocal theory:
    // - Phrase endings need breath preparation (shorter)
    // - Same pitch = legato (no gap, or tie)
    // - Step motion = smooth connection
    // - Skip/Leap = slight articulation, leap needs preparation time
    // - Long notes (quarter+) don't need gating
    bool is_phrase_end = (i == rhythm.size() - 1);
    bool is_phrase_start = (i == 0);
    float gate_ratio = 1.0f;

    if (is_phrase_end) {
      // Phrase ending: breath preparation (85%)
      // Minimum duration is quarter note for proper cadence
      note_duration = std::max(note_duration, static_cast<Tick>(TICK_QUARTER));
      gate_ratio = 0.85f;
    } else if (is_phrase_start) {
      // Phrase start: clear attack, no gate
      gate_ratio = 1.0f;
    } else if (note_duration >= TICK_QUARTER) {
      // Long notes (quarter+): no gate needed for natural sustain
      gate_ratio = 1.0f;
    } else if (!result.notes.empty()) {
      // Interior notes: gate based on interval to previous note
      int prev_pitch = result.notes.back().note;
      int interval = std::abs(new_pitch - prev_pitch);

      if (interval == 0) {
        // Same pitch: legato connection (100%)
        // Rhythmic repetition needs slight articulation but NOT staccato
        gate_ratio = 1.0f;
      } else if (interval <= 2) {
        // Step motion (1-2 semitones): smooth legato (98%)
        gate_ratio = 0.98f;
      } else if (interval <= 5) {
        // Skip (3-5 semitones): slight articulation (95%)
        gate_ratio = 0.95f;
      } else {
        // Leap (6+ semitones): preparation time needed
        // Shorten previous note slightly for jump preparation
        gate_ratio = 0.92f;
      }
    }

    // Apply gate ratio
    note_duration = static_cast<Tick>(note_duration * gate_ratio);

    // Clamp note duration to chord change boundary (prevents dissonance when chord changes)
    Tick chord_change = harmony.getNextChordChangeTick(note_start);
    if (chord_change > 0 && chord_change > note_start &&
        note_start + note_duration > chord_change) {
      // Shorten note to end just before chord change
      Tick new_duration = chord_change - note_start - 10;  // Small gap before chord change
      if (new_duration >= TICK_SIXTEENTH) {
        note_duration = new_duration;
      }
    }

    // Also clamp to phrase boundary for phrase ending
    Tick phrase_end = phrase_start + phrase_beats * TICKS_PER_BEAT;
    if (note_start + note_duration > phrase_end) {
      note_duration = phrase_end - note_start;
      // Ensure minimum duration (16th note) for musical validity
      if (note_duration < TICK_SIXTEENTH) {
        note_duration = TICK_SIXTEENTH;
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

    // Final clamp to ensure pitch is within vocal range
    // ABSOLUTE CONSTRAINT: Ensure pitch is on scale (prevents chromatic notes)
    new_pitch = snapToNearestScaleTone(new_pitch, ctx.key_offset);
    new_pitch = std::clamp(new_pitch, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high));

    // Add note with provenance tracking
    result.notes.push_back(factory.create(
        note_start, note_duration, static_cast<uint8_t>(new_pitch), velocity,
        NoteSource::MelodyPhrase));

    current_pitch = new_pitch;
    prev_note_duration = note_duration;  // Track for leap preparation
  }

  result.last_pitch = current_pitch;
  return result;
}

MelodyDesigner::PhraseResult MelodyDesigner::generateHook(
    const MelodyTemplate& tmpl,
    Tick hook_start,
    const SectionContext& ctx,
    int prev_pitch,
    const IHarmonyContext& harmony,
    std::mt19937& rng) {

  PhraseResult result;
  result.notes.clear();

  // Create NoteFactory for provenance tracking
  NoteFactory factory(harmony);

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
  base_pitch = std::clamp(base_pitch, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high));

  // Song-level hook fixation: generate and cache hook motif once
  // "Variation is the enemy, Exact is justice" - use the same hook throughout the song
  if (!cached_chorus_hook_.has_value()) {
    StyleMelodyParams hook_params{};
    hook_params.hook_repetition = true;  // Use catchy repetitive style
    cached_chorus_hook_ = designChorusHook(hook_params, rng);
  }

  Motif hook = *cached_chorus_hook_;

  // Hybrid approach: blend HookSkeleton contour hint with existing Motif
  // HookSkeleton provides melodic DNA, Motif provides rhythm (maintained)
  // HookIntensity influences skeleton selection: Strong → more Repeat/AscendDrop
  if (!cached_hook_skeleton_.has_value()) {
    cached_hook_skeleton_ = selectHookSkeleton(ctx.section_type, rng, ctx.hook_intensity);
  }
  SkeletonPattern skeleton_contour = getSkeletonPattern(*cached_hook_skeleton_);

  // Blend skeleton contour with Motif contour (Motif 80%, Skeleton 20%)
  // This preserves the existing system while adding HookSkeleton influence
  for (size_t i = 0; i < hook.contour_degrees.size() && i < skeleton_contour.length; ++i) {
    int8_t skeleton_hint = skeleton_contour.intervals[i % skeleton_contour.length];
    if (skeleton_hint != -128) {  // Skip rest markers
      // Blend: 80% existing Motif, 20% HookSkeleton hint
      int blended = static_cast<int>(hook.contour_degrees[i] * 0.8f +
                                     skeleton_hint * 0.2f);
      hook.contour_degrees[i] = static_cast<int8_t>(blended);
    }
  }

  // Select rhythm pattern based on template style (cached per song for consistency)
  if (cached_hook_rhythm_pattern_idx_ == SIZE_MAX) {
    cached_hook_rhythm_pattern_idx_ = selectHookRhythmPatternIndex(tmpl, rng);
  }
  const HookRhythmPattern& rhythm_pattern = kHookRhythmPatterns[cached_hook_rhythm_pattern_idx_];

  // Use template settings for repetition count
  uint8_t repeat_count = std::clamp(tmpl.hook_repeat_count,
                                    static_cast<uint8_t>(2),
                                    static_cast<uint8_t>(4));

  Tick current_tick = hook_start;
  constexpr int MAX_INTERVAL = 9;  // Major 6th - singable leap limit

  // Generate hook notes with chord-aware pitch selection
  int prev_hook_pitch = base_pitch;

  // Track consecutive same notes for J-POP style probability curve
  int consecutive_same_count = 0;

  // Track previous note duration for leap preparation
  Tick prev_note_duration = TICKS_PER_BEAT;  // Default to quarter note

  // Use rhythm pattern's note count, but limit by available contour degrees
  size_t contour_limit = std::min(
      static_cast<size_t>(rhythm_pattern.note_count),
      hook.contour_degrees.size());

  for (uint8_t rep = 0; rep < repeat_count; ++rep) {
    for (size_t i = 0; i < contour_limit; ++i) {
      // Get chord at this note's position
      int8_t note_chord_degree = harmony.getChordDegreeAt(current_tick);

      // Calculate pitch from contour, then snap to current chord
      int pitch = base_pitch + hook.contour_degrees[i % hook.contour_degrees.size()];

      // Find nearest chord tone within vocal range and interval constraint
      pitch = nearestChordToneWithinInterval(
          pitch, prev_hook_pitch, note_chord_degree, MAX_INTERVAL,
          ctx.vocal_low, ctx.vocal_high, &ctx.tessitura);

      // Leap preparation principle: constrain leaps after very short notes
      // Same threshold as generateMelodyPhrase for consistency
      constexpr Tick VERY_SHORT_THRESHOLD = TICKS_PER_BEAT / 2;  // 0.5 beats
      constexpr int MAX_LEAP_AFTER_SHORT = 5;  // Perfect 4th
      if ((rep > 0 || i > 0) && prev_note_duration < VERY_SHORT_THRESHOLD) {
        int leap = std::abs(pitch - prev_hook_pitch);
        if (leap > MAX_LEAP_AFTER_SHORT) {
          pitch = nearestChordToneWithinInterval(
              pitch, prev_hook_pitch, note_chord_degree, MAX_LEAP_AFTER_SHORT,
              ctx.vocal_low, ctx.vocal_high, &ctx.tessitura);
        }
      }

      // Avoid note check: melody should not form tritone/minor2nd with chord tones
      {
        int bass_root_pc = getBassRootPitchClass(note_chord_degree);
        std::vector<int> chord_tones = getChordTonePitchClasses(note_chord_degree);
        int pitch_pc = pitch % 12;
        if (isAvoidNoteWithChord(pitch_pc, chord_tones, bass_root_pc)) {
          pitch = getNearestSafeChordTone(pitch, note_chord_degree, bass_root_pc,
                                           ctx.vocal_low, ctx.vocal_high);
        }
      }

      // Downbeat chord-tone constraint for hooks
      {
        Tick bar_pos = current_tick % TICKS_PER_BAR;
        bool is_downbeat = bar_pos < TICKS_PER_BEAT / 4;
        if (is_downbeat) {
          std::vector<int> chord_tones = getChordTonePitchClasses(note_chord_degree);
          int pitch_pc = pitch % 12;
          bool is_chord_tone = false;
          for (int ct : chord_tones) {
            if (pitch_pc == ct) {
              is_chord_tone = true;
              break;
            }
          }
          if (!is_chord_tone) {
            pitch = nearestChordTonePitch(pitch, note_chord_degree);
            pitch = std::clamp(pitch,
                               static_cast<int>(ctx.vocal_low),
                               static_cast<int>(ctx.vocal_high));
          }
        }
      }

      // Apply consecutive same note limit with J-POP probability curve
      // Same curve as generateMelodyPhrase for consistency
      bool force_move = false;
      if (pitch == prev_hook_pitch) {
        consecutive_same_count++;

        float allow_prob;
        switch (consecutive_same_count) {
          case 1: allow_prob = 1.0f; break;
          case 2: allow_prob = 0.85f; break;
          case 3: allow_prob = 0.50f; break;
          case 4: allow_prob = 0.25f; break;
          default: allow_prob = 0.05f; break;
        }

        std::uniform_real_distribution<float> same_dist(0.0f, 1.0f);
        if (same_dist(rng) > allow_prob) {
          force_move = true;
        }
      } else {
        consecutive_same_count = 0;
      }

      if (force_move) {
        // Force movement to different chord tone
        std::vector<int> chord_tones = getChordTonePitchClasses(note_chord_degree);
        std::vector<int> candidates;
        for (int pc : chord_tones) {
          for (int oct = 4; oct <= 6; ++oct) {
            int candidate = oct * 12 + pc;
            if (candidate >= ctx.vocal_low && candidate <= ctx.vocal_high &&
                candidate != prev_hook_pitch &&
                std::abs(candidate - prev_hook_pitch) <= MAX_INTERVAL) {
              candidates.push_back(candidate);
            }
          }
        }
        if (!candidates.empty()) {
          int best = candidates[0];
          int best_dist = std::abs(best - prev_hook_pitch);
          for (int c : candidates) {
            int dist = std::abs(c - prev_hook_pitch);
            if (dist > 0 && dist < best_dist) {
              best = c;
              best_dist = dist;
            }
          }
          pitch = best;
          consecutive_same_count = 0;
        }
      }

      // Get duration from rhythm pattern (in eighths, convert to ticks)
      uint8_t eighths = rhythm_pattern.durations[i];
      Tick note_duration = static_cast<Tick>(eighths) * TICK_EIGHTH;

      uint8_t velocity = DEFAULT_VELOCITY;
      // Accent based on note position in pattern
      // First note and longer notes get accent (pop music theory: strong-weak alignment)
      if (i == 0 || eighths >= 2) {
        velocity += 10;
      }

      // Vocal-friendly gate processing for hooks
      // Hooks are catchy phrases that need clear articulation but NOT staccato
      float gate_ratio = 1.0f;
      bool is_pattern_end = (i == contour_limit - 1);
      bool is_repeat_end = (rep == repeat_count - 1) && is_pattern_end;

      if (is_repeat_end) {
        // Final note of hook: breath preparation
        gate_ratio = 0.85f;
      } else if (i == 0 && rep == 0) {
        // First note of hook: clear attack
        gate_ratio = 1.0f;
      } else if (note_duration >= TICK_QUARTER) {
        // Long notes: no gate needed
        gate_ratio = 1.0f;
      } else {
        // Interior hook notes: gate based on interval
        int interval = std::abs(pitch - prev_hook_pitch);
        if (interval == 0) {
          // Same pitch in hook: legato (100%)
          gate_ratio = 1.0f;
        } else if (interval <= 2) {
          // Step: smooth (98%)
          gate_ratio = 0.98f;
        } else if (interval <= 5) {
          // Skip: slight articulation (95%)
          gate_ratio = 0.95f;
        } else {
          // Leap: preparation (92%)
          gate_ratio = 0.92f;
        }
      }

      Tick actual_duration = static_cast<Tick>(note_duration * gate_ratio);

      // Clamp duration to chord change boundary
      Tick chord_change = harmony.getNextChordChangeTick(current_tick);
      if (chord_change > 0 && chord_change > current_tick &&
          current_tick + actual_duration > chord_change) {
        Tick new_duration = chord_change - current_tick - 10;
        if (new_duration >= TICK_SIXTEENTH) {
          actual_duration = new_duration;
        }
      }

      // ABSOLUTE CONSTRAINT: Ensure pitch is on scale (prevents chromatic notes)
      pitch = snapToNearestScaleTone(pitch, ctx.key_offset);
      pitch = std::clamp(pitch, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high));

      result.notes.push_back(factory.create(
          current_tick,
          actual_duration,
          static_cast<uint8_t>(pitch),
          velocity,
          NoteSource::Hook));

      prev_hook_pitch = pitch;
      prev_note_duration = actual_duration;  // Track for leap preparation
      current_tick += note_duration;
    }

    // Add gap after pattern (varies by pattern for natural breathing)
    current_tick += rhythm_pattern.gap_after;
  }

  // Return last pitch for smooth transition to next phrase
  result.last_pitch = prev_hook_pitch;
  result.direction_inertia = 0;  // Reset inertia after hook

  return result;
}

PitchChoice MelodyDesigner::selectPitchChoice(
    const MelodyTemplate& tmpl,
    float phrase_pos,
    bool has_target,
    std::mt19937& rng) {

  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  // Step 1: Check for same pitch (plateau)
  if (dist(rng) < tmpl.plateau_ratio) {
    return PitchChoice::Same;
  }

  // Step 2: Target attraction (if applicable)
  if (has_target && tmpl.has_target_pitch) {
    if (phrase_pos >= tmpl.target_attraction_start) {
      if (dist(rng) < tmpl.target_attraction_strength) {
        return PitchChoice::TargetStep;
      }
    }
  }

  // Step 3: Random step direction
  return (dist(rng) < 0.5f) ? PitchChoice::StepUp : PitchChoice::StepDown;
}

PitchChoice MelodyDesigner::applyDirectionInertia(
    PitchChoice choice,
    int inertia,
    [[maybe_unused]] const MelodyTemplate& tmpl,
    std::mt19937& rng) {

  // Same pitch or target step - don't modify
  if (choice == PitchChoice::Same || choice == PitchChoice::TargetStep) {
    return choice;
  }

  // Strong inertia can override random direction
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  float inertia_strength = std::abs(inertia) / 3.0f;

  if (dist(rng) < inertia_strength * 0.5f) {
    // Follow inertia direction
    if (inertia > 0) {
      return PitchChoice::StepUp;
    } else if (inertia < 0) {
      return PitchChoice::StepDown;
    }
  }

  return choice;
}

float MelodyDesigner::getEffectivePlateauRatio(
    const MelodyTemplate& tmpl,
    int current_pitch,
    const TessituraRange& tessitura) {

  float base_ratio = tmpl.plateau_ratio;

  // Boost plateau ratio in high register for stability
  if (current_pitch > tessitura.high) {
    base_ratio += tmpl.high_register_plateau_boost;
  }

  // Also boost slightly near tessitura boundaries
  if (current_pitch <= tessitura.low + 2 || current_pitch >= tessitura.high - 2) {
    base_ratio += 0.1f;
  }

  return std::min(base_ratio, 0.9f);  // Cap at 90%
}

bool MelodyDesigner::shouldLeap(
    LeapTrigger trigger,
    float phrase_pos,
    float section_pos) {

  switch (trigger) {
    case LeapTrigger::None:
      return false;

    case LeapTrigger::PhraseStart:
      return phrase_pos < 0.1f;

    case LeapTrigger::EmotionalPeak:
      // Emotional peak typically around 60-80% of section
      return section_pos >= 0.6f && section_pos <= 0.8f;

    case LeapTrigger::SectionBoundary:
      return section_pos < 0.05f || section_pos > 0.95f;
  }

  return false;
}

int MelodyDesigner::getStabilizeStep(int leap_direction, int max_step) {
  // Return opposite direction, smaller magnitude
  int stabilize = -leap_direction;
  int magnitude = std::max(1, max_step / 2);
  return stabilize * magnitude;
}

bool MelodyDesigner::isInSameVowelSection(
    float pos1, float pos2, [[maybe_unused]] uint8_t phrase_length) {

  // Simple vowel section model: divide phrase into 2-beat sections
  constexpr float VOWEL_SECTION_BEATS = 2.0f;

  int section1 = static_cast<int>(pos1 / VOWEL_SECTION_BEATS);
  int section2 = static_cast<int>(pos2 / VOWEL_SECTION_BEATS);

  return section1 == section2;
}

int8_t MelodyDesigner::getMaxStepInVowelSection(bool in_same_vowel) {
  return in_same_vowel ? 2 : 4;
}

void MelodyDesigner::applyTransitionApproach(
    std::vector<NoteEvent>& notes,
    const SectionContext& ctx,
    const IHarmonyContext& harmony) {

  if (!ctx.transition_to_next || notes.empty()) return;

  const auto& trans = *ctx.transition_to_next;
  Tick approach_start = ctx.section_end - trans.approach_beats * TICKS_PER_BEAT;

  // Maximum allowed interval (major 6th = 9 semitones)
  constexpr int MAX_INTERVAL = 9;

  int prev_pitch = -1;

  for (auto& note : notes) {
    if (note.start_tick < approach_start) {
      prev_pitch = note.note;
      continue;
    }

    // 1. Apply pitch tendency (creating "run-up" to next section)
    float progress = static_cast<float>(note.start_tick - approach_start) /
                     static_cast<float>(ctx.section_end - approach_start);
    int8_t pitch_shift = static_cast<int8_t>(trans.pitch_tendency * progress);

    // Move toward chord tone while shifting
    int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);
    int new_pitch = nearestChordTonePitch(
        note.note + pitch_shift, chord_degree);

    // Constrain to vocal range
    new_pitch = std::clamp(
        new_pitch, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high));

    // Ensure interval constraint with previous note
    if (prev_pitch >= 0) {
      int interval = std::abs(new_pitch - prev_pitch);
      if (interval > MAX_INTERVAL) {
        // Reduce the shift to stay within interval constraint
        if (new_pitch > prev_pitch) {
          new_pitch = prev_pitch + MAX_INTERVAL;
        } else {
          new_pitch = prev_pitch - MAX_INTERVAL;
        }
        // Snap to scale to prevent chromatic notes
        new_pitch = snapToNearestScaleTone(new_pitch, ctx.key_offset);
        // Re-constrain to vocal range
        new_pitch = std::clamp(
            new_pitch, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high));
      }
    }

    note.note = static_cast<uint8_t>(new_pitch);
    prev_pitch = new_pitch;

    // 2. Apply velocity gradient (crescendo/decrescendo)
    float vel_factor = 1.0f + (trans.velocity_growth - 1.0f) * progress;
    note.velocity = static_cast<uint8_t>(
        std::clamp(static_cast<float>(note.velocity) * vel_factor, 1.0f, 127.0f));
  }

  // 3. Insert leading tone if requested (skip if it would create large interval)
  if (trans.use_leading_tone && !notes.empty()) {
    int last_pitch = notes.back().note;
    int leading_pitch = ctx.tessitura.center - 1;
    if (std::abs(leading_pitch - last_pitch) <= MAX_INTERVAL) {
      insertLeadingTone(notes, ctx, harmony);
    }
  }
}

void MelodyDesigner::insertLeadingTone(
    std::vector<NoteEvent>& notes,
    const SectionContext& ctx,
    const IHarmonyContext& harmony) {

  if (notes.empty()) return;

  // Create NoteFactory for provenance tracking
  NoteFactory factory(harmony);

  // Maximum allowed interval (major 6th = 9 semitones)
  constexpr int MAX_INTERVAL = 9;

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
  if (interval > MAX_INTERVAL) {
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
  if (leading_tone_start > last_note_end &&
      leading_tone_start - last_note_end > MAX_GAP) {
    return;
  }

  if (last_note_end <= leading_tone_start) {
    uint8_t velocity = static_cast<uint8_t>(
        std::min(127, static_cast<int>(last_note.velocity) + 10));  // Slightly louder

    notes.push_back(factory.create(
        leading_tone_start,
        TICKS_PER_BEAT / 4,  // 16th note duration
        static_cast<uint8_t>(leading_pitch),
        velocity,
        NoteSource::PostProcess));  // Leading tone is a post-processing addition
  }
}

int MelodyDesigner::applyPitchChoice(
    PitchChoice choice,
    int current_pitch,
    int target_pitch,
    int8_t chord_degree,
    int key_offset,
    uint8_t vocal_low,
    uint8_t vocal_high,
    VocalAttitude attitude) {

  // VocalAttitude affects candidate pitch selection:
  //   Clean: chord tones only (1, 3, 5)
  //   Expressive: chord tones + tensions (7, 9)
  //   Raw: all scale tones (more freedom)

  // Get chord tones for current chord
  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);

  // Build candidate pitch classes based on VocalAttitude
  std::vector<int> candidate_pcs;
  switch (attitude) {
    case VocalAttitude::Clean:
      // Chord tones only (safe, consonant)
      candidate_pcs = chord_tones;
      break;

    case VocalAttitude::Expressive:
      // Chord tones + tensions (7th, 9th = 2nd)
      candidate_pcs = chord_tones;
      // Add 7th (11 semitones from root for major, 10 for minor/dominant)
      {
        int root_pc = chord_tones.empty() ? 0 : chord_tones[0];
        int seventh = (root_pc + 11) % 12;  // Major 7th
        int ninth = (root_pc + 2) % 12;     // 9th = 2nd
        candidate_pcs.push_back(seventh);
        candidate_pcs.push_back(ninth);
      }
      break;

    case VocalAttitude::Raw:
      // All scale tones (C major: 0, 2, 4, 5, 7, 9, 11)
      candidate_pcs = {0, 2, 4, 5, 7, 9, 11};
      break;
  }

  // Build candidate pitches within vocal range
  std::vector<int> candidates;
  for (int pc : candidate_pcs) {
    // ABSOLUTE CONSTRAINT: Only allow scale tones
    // This prevents chromatic notes from VocalAttitude::Expressive tensions
    // that fall outside the scale (e.g., G# from Am7 in C major)
    if (!isScaleTone(pc, static_cast<uint8_t>(key_offset))) {
      continue;
    }

    // Check multiple octaves (4-6 covers typical vocal range)
    for (int oct = 4; oct <= 6; ++oct) {
      int candidate = oct * 12 + pc;
      if (candidate >= vocal_low && candidate <= vocal_high) {
        candidates.push_back(candidate);
      }
    }
  }

  // Sort candidates for easier searching
  std::sort(candidates.begin(), candidates.end());

  if (candidates.empty()) {
    // Fallback: use nearest chord tone to current pitch
    return std::clamp(nearestChordTonePitch(current_pitch, chord_degree),
                      static_cast<int>(vocal_low),
                      static_cast<int>(vocal_high));
  }

  int new_pitch = current_pitch;

  switch (choice) {
    case PitchChoice::Same:
      // Stay on nearest chord tone to current pitch
      new_pitch = nearestChordTonePitch(current_pitch, chord_degree);
      break;

    case PitchChoice::StepUp:
      // Find smallest chord tone above current pitch
      {
        int best = -1;
        for (int c : candidates) {
          if (c > current_pitch) {
            best = c;
            break;  // Already sorted, first one above is smallest
          }
        }
        if (best < 0) {
          // No chord tone above, use nearest
          best = nearestChordTonePitch(current_pitch, chord_degree);
        }
        new_pitch = best;
      }
      break;

    case PitchChoice::StepDown:
      // Find largest chord tone below current pitch
      {
        int best = -1;
        for (int i = static_cast<int>(candidates.size()) - 1; i >= 0; --i) {
          if (candidates[i] < current_pitch) {
            best = candidates[i];
            break;  // Reverse sorted search, first one below is largest
          }
        }
        if (best < 0) {
          best = nearestChordTonePitch(current_pitch, chord_degree);
        }
        new_pitch = best;
      }
      break;

    case PitchChoice::TargetStep:
      // Move toward target, using nearest chord tone in that direction
      if (target_pitch >= 0) {
        if (target_pitch > current_pitch) {
          // Going up toward target: find first chord tone above current
          for (int c : candidates) {
            if (c > current_pitch && c <= target_pitch) {
              new_pitch = c;
              break;
            }
          }
          if (new_pitch == current_pitch) {
            // No suitable chord tone found, use nearest above
            for (int c : candidates) {
              if (c > current_pitch) {
                new_pitch = c;
                break;
              }
            }
          }
        } else if (target_pitch < current_pitch) {
          // Going down toward target: find first chord tone below current
          for (int i = static_cast<int>(candidates.size()) - 1; i >= 0; --i) {
            if (candidates[i] < current_pitch && candidates[i] >= target_pitch) {
              new_pitch = candidates[i];
              break;
            }
          }
          if (new_pitch == current_pitch) {
            // No suitable chord tone found, use nearest below
            for (int i = static_cast<int>(candidates.size()) - 1; i >= 0; --i) {
              if (candidates[i] < current_pitch) {
                new_pitch = candidates[i];
                break;
              }
            }
          }
        } else {
          // Already at target
          new_pitch = nearestChordTonePitch(current_pitch, chord_degree);
        }
      } else {
        new_pitch = nearestChordTonePitch(current_pitch, chord_degree);
      }
      break;
  }

  // Clamp to vocal range
  new_pitch = std::clamp(new_pitch,
                         static_cast<int>(vocal_low),
                         static_cast<int>(vocal_high));

  return new_pitch;
}

int MelodyDesigner::calculateTargetPitch(
    const MelodyTemplate& tmpl,
    const SectionContext& ctx,
    [[maybe_unused]] int current_pitch,
    const IHarmonyContext& harmony,
    [[maybe_unused]] std::mt19937& rng) {

  // Target is typically a chord tone in the upper part of tessitura
  std::vector<int> chord_tones = harmony.getChordTonesAt(ctx.section_start);

  if (chord_tones.empty()) {
    return ctx.tessitura.center;
  }

  // Find chord tone nearest to upper tessitura
  int target_area = ctx.tessitura.center + tmpl.tessitura_range / 2;
  int best_pitch = target_area;
  int best_dist = 100;

  for (int pc : chord_tones) {
    // Check multiple octaves
    for (int oct = 4; oct <= 6; ++oct) {
      int candidate = oct * 12 + pc;
      if (candidate < ctx.vocal_low || candidate > ctx.vocal_high) continue;

      int dist = std::abs(candidate - target_area);
      if (dist < best_dist) {
        best_dist = dist;
        best_pitch = candidate;
      }
    }
  }

  return best_pitch;
}

std::vector<RhythmNote> MelodyDesigner::generatePhraseRhythm(
    const MelodyTemplate& tmpl,
    uint8_t phrase_beats,
    float density_modifier,
    float thirtysecond_ratio,
    std::mt19937& rng) {

  std::vector<RhythmNote> rhythm;
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  float current_beat = 0.0f;
  float end_beat = static_cast<float>(phrase_beats);

  // Apply section density modifier to sixteenth density
  float effective_sixteenth_density = tmpl.sixteenth_density * density_modifier;
  // Clamp to valid range [0.0, 0.95]
  effective_sixteenth_density = std::min(effective_sixteenth_density, 0.95f);

  // Reserve space for final phrase-ending note (at least 1 beat before end)
  // This ensures proper cadence with a longer final note on a strong beat
  float phrase_body_end = end_beat - 1.0f;

  // Track consecutive short notes to prevent breath-difficult passages
  // Pop vocal principle: limit rapid-fire notes to maintain singability
  int consecutive_short_count = 0;
  constexpr int MAX_CONSECUTIVE_SHORT = 3;

  while (current_beat < phrase_body_end) {
    // Check if current position is on a strong beat (integer beat: 0.0, 1.0, 2.0, 3.0)
    // Pop music principle: strong beats should have longer, more stable notes
    float frac = current_beat - std::floor(current_beat);
    bool is_on_beat = frac < 0.01f;

    // Determine note duration (in eighths, float to support 32nds)
    float eighths;

    if (is_on_beat && !tmpl.rhythm_driven) {
      // Strong beat (non-rhythm-driven styles): prioritize longer notes for natural vocal phrasing
      // Avoid 16th/32nd notes on downbeats - they break the rhythmic anchor
      // Exception: rhythm_driven templates (like UltraVocaloid) maintain fast rhythms
      if (dist(rng) < tmpl.long_note_ratio * 2.0f) {
        eighths = 4.0f;  // Half note (doubled probability on strong beat)
      } else {
        eighths = 2.0f;  // Quarter note (default for strong beats)
      }
      consecutive_short_count = 0;  // Reset counter on strong beat
    } else {
      // Weak beat: use existing logic for rhythmic variety
      if (thirtysecond_ratio > 0.0f && dist(rng) < thirtysecond_ratio) {
        eighths = 0.5f;  // 32nd note (0.25 eighth = 0.125 beats)
      } else if (tmpl.rhythm_driven && dist(rng) < effective_sixteenth_density) {
        eighths = 1.0f;  // 16th note (0.5 eighth)
      } else if (dist(rng) < tmpl.long_note_ratio) {
        eighths = 4.0f;  // Half note
      } else {
        eighths = 2.0f;  // Quarter note (most common)
      }
    }

    // Enforce consecutive short note limit for singability
    // Vocal physiology: too many rapid notes without breath points causes strain
    if (eighths <= 1.0f) {
      consecutive_short_count++;
      if (consecutive_short_count >= MAX_CONSECUTIVE_SHORT) {
        eighths = 2.0f;  // Force quarter note for breathing room
        consecutive_short_count = 0;
      }
    } else {
      consecutive_short_count = 0;
    }

    // Check if strong beat
    bool strong = (static_cast<int>(current_beat) % 2 == 0);

    // For RhythmNote, convert float eighths back to int (32nd = 0.5 -> 1 for special handling)
    int rhythm_eighths = static_cast<int>(eighths);
    if (eighths < 1.0f) {
      // 32nd note: store as 1 to indicate shortest note (16th-equivalent for now)
      rhythm_eighths = 1;
    }
    rhythm.push_back({current_beat, rhythm_eighths, strong});

    current_beat += eighths * 0.5f;  // Convert eighths to beats
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
    // Final note is at least a quarter note (2 eighths), extending to phrase end
    int final_eighths = static_cast<int>((end_beat - final_beat) * 2.0f);
    final_eighths = std::max(final_eighths, 2);  // At least quarter note

    rhythm.push_back({final_beat, final_eighths, true});  // Strong beat
  }

  return rhythm;
}

// === GlobalMotif Support ===

GlobalMotif MelodyDesigner::extractGlobalMotif(const std::vector<NoteEvent>& notes) {
  GlobalMotif motif;

  if (notes.size() < 2) {
    return motif;  // Not enough notes for meaningful analysis
  }

  // Extract interval signature (relative pitch changes)
  size_t interval_limit = std::min(notes.size() - 1, static_cast<size_t>(8));
  for (size_t i = 0; i < interval_limit; ++i) {
    int interval = static_cast<int>(notes[i + 1].note) -
                   static_cast<int>(notes[i].note);
    motif.interval_signature[i] = static_cast<int8_t>(std::clamp(interval, -12, 12));
  }
  motif.interval_count = static_cast<uint8_t>(interval_limit);

  // Extract rhythm signature (relative durations)
  Tick max_duration = 0;
  for (size_t i = 0; i < std::min(notes.size(), static_cast<size_t>(8)); ++i) {
    if (notes[i].duration > max_duration) {
      max_duration = notes[i].duration;
    }
  }
  if (max_duration > 0) {
    for (size_t i = 0; i < std::min(notes.size(), static_cast<size_t>(8)); ++i) {
      // Normalize to 0-8 scale (8 = longest note)
      uint8_t ratio = static_cast<uint8_t>(
          (notes[i].duration * 8) / max_duration);
      motif.rhythm_signature[i] = std::clamp(ratio, static_cast<uint8_t>(1),
                                             static_cast<uint8_t>(8));
    }
    motif.rhythm_count = static_cast<uint8_t>(
        std::min(notes.size(), static_cast<size_t>(8)));
  }

  // Analyze contour type
  if (motif.interval_count >= 2) {
    int first_half_sum = 0, second_half_sum = 0;
    size_t mid = motif.interval_count / 2;

    for (size_t i = 0; i < mid; ++i) {
      first_half_sum += motif.interval_signature[i];
    }
    for (size_t i = mid; i < motif.interval_count; ++i) {
      second_half_sum += motif.interval_signature[i];
    }

    // Determine contour based on directional changes
    int total_movement = first_half_sum + second_half_sum;
    bool first_rising = first_half_sum > 0;
    bool first_falling = first_half_sum < 0;
    bool second_rising = second_half_sum > 0;
    bool second_falling = second_half_sum < 0;

    // Peak/Valley: significant direction reversal
    if (first_rising && second_falling && std::abs(first_half_sum) >= 3) {
      motif.contour_type = ContourType::Peak;
    } else if (first_falling && second_rising && std::abs(first_half_sum) >= 3) {
      motif.contour_type = ContourType::Valley;
    } else if (std::abs(first_half_sum) < 3 && std::abs(second_half_sum) < 3) {
      // Both halves have little movement = plateau
      motif.contour_type = ContourType::Plateau;
    } else if (total_movement > 0) {
      motif.contour_type = ContourType::Ascending;
    } else {
      motif.contour_type = ContourType::Descending;
    }
  }

  return motif;
}

float MelodyDesigner::evaluateWithGlobalMotif(
    const std::vector<NoteEvent>& candidate,
    const GlobalMotif& global_motif) {

  if (!global_motif.isValid() || candidate.size() < 2) {
    return 0.0f;
  }

  float bonus = 0.0f;

  // Extract candidate's contour
  GlobalMotif candidate_motif = extractGlobalMotif(candidate);

  // Contour similarity bonus (0.0-0.05)
  if (candidate_motif.contour_type == global_motif.contour_type) {
    bonus += 0.05f;
  }

  // Interval pattern similarity bonus (0.0-0.05)
  if (candidate_motif.interval_count > 0 && global_motif.interval_count > 0) {
    size_t compare_count = std::min(
        static_cast<size_t>(candidate_motif.interval_count),
        static_cast<size_t>(global_motif.interval_count));

    int similarity_score = 0;
    for (size_t i = 0; i < compare_count; ++i) {
      int diff = std::abs(candidate_motif.interval_signature[i] -
                          global_motif.interval_signature[i]);
      // Award points for similar intervals (within 2 semitones)
      if (diff <= 2) {
        similarity_score += (3 - diff);  // 3 for exact, 2 for 1 off, 1 for 2 off
      }
    }

    // Normalize to 0.0-0.05 range
    float max_score = static_cast<float>(compare_count * 3);
    if (max_score > 0.0f) {
      bonus += (static_cast<float>(similarity_score) / max_score) * 0.05f;
    }
  }

  return bonus;
}

}  // namespace midisketch
