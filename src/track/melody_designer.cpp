/**
 * @file melody_designer.cpp
 * @brief Implementation of MelodyDesigner track generation.
 */

#include "track/melody_designer.h"

#include <algorithm>
#include <cmath>

#include "core/harmonic_rhythm.h"
#include "core/hook_utils.h"
#include "core/i_harmony_context.h"
#include "core/melody_embellishment.h"
#include "core/motif_transform.h"
#include "core/note_factory.h"
#include "core/phrase_patterns.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "core/vocal_style_profile.h"

namespace midisketch {

namespace {

// Default velocity for melody notes
constexpr uint8_t DEFAULT_VELOCITY = 100;

// ============================================================================
// GlobalMotif Weight by Section Type (Task 5-1)
// ============================================================================
// Adjust motif_similarity_bonus weight by section progression:
// - A (1st): 0.10 (subtle, fragments OK)
// - B: 0.15 (building tension, motif development)
// - Chorus: 0.25 (maximum, hook recognition)
// - A (2nd+): 0.20 (listener knows motif now)
// - Bridge: 0.05 (contrast, deliberately different)

/// @brief Get GlobalMotif weight multiplier for section type.
/// @param section Section type
/// @param section_occurrence How many times this section type has appeared (1-based)
/// @return Weight multiplier (0.05 - 0.35)
///
/// Enhanced weights for stronger motivic consistency:
/// - Chorus/Drop: 0.35 (maximum hook recognition)
/// - B/MixBreak: 0.22 (strong tension building)
/// - A (1st): 0.15 (introduce motif fragments)
/// - A (2nd+): 0.25 (reinforce recognition)
float getMotifWeightForSection(SectionType section, int section_occurrence = 1) {
  switch (section) {
    case SectionType::Chorus:
    case SectionType::Drop:
      // Maximum weight: hook recognition is paramount
      return 0.35f;
    case SectionType::B:
    case SectionType::MixBreak:
      // Building tension: stronger motif development
      return 0.22f;
    case SectionType::A:
      // Verse: depends on occurrence
      if (section_occurrence == 1) {
        return 0.15f;  // First verse: introduce motif fragments
      } else {
        return 0.25f;  // Subsequent verses: reinforce recognition
      }
    case SectionType::Bridge:
      // Contrast: deliberately different, minimal motif reference
      return 0.05f;
    case SectionType::Intro:
    case SectionType::Interlude:
      // Instrumental: subtle motif reference
      return 0.10f;
    case SectionType::Outro:
      // Outro: recall motif for closure
      return 0.20f;
    case SectionType::Chant:
      // Chant: minimal melodic content
      return 0.05f;
  }
  return 0.12f;  // Default
}

// Calculate base breath duration based on section type and mood.
// Ballads and sentimental moods get longer breaths for emotional phrasing.
// Chorus sections get shorter breaths for drive and energy.
// @param section Section type (Verse, Chorus, etc.)
// @param mood Current mood setting
// @return Base breath duration in ticks
Tick getBaseBreathDuration(SectionType section, Mood mood) {
  // Ballad/Sentimental: longer breath (quarter note) for emotional phrasing
  if (mood == Mood::Ballad || mood == Mood::Sentimental) {
    return TICK_QUARTER;
  }
  // Chorus: shorter breath (16th note) for drive and momentum
  if (section == SectionType::Chorus) {
    return TICK_SIXTEENTH;
  }
  // Default: 8th note breath
  return TICK_EIGHTH;
}

// Calculate breath duration with phrase context awareness.
// Considers phrase density and pitch to allow appropriate recovery time.
// High density phrases and phrases reaching high pitches get longer breaths.
// @param section Section type (Verse, Chorus, etc.)
// @param mood Current mood setting
// @param phrase_density Note density of the phrase (notes per beat, 0.0-2.0+)
// @param phrase_high_pitch Highest pitch reached in the phrase (MIDI note number)
// @return Adjusted breath duration in ticks
Tick getBreathDuration(SectionType section, Mood mood, float phrase_density = 0.0f,
                       uint8_t phrase_high_pitch = 60) {
  Tick base = getBaseBreathDuration(section, mood);

  // High density phrases need more recovery time
  // Density > 1.0 means more than one note per beat on average
  if (phrase_density > 1.0f) {
    base = static_cast<Tick>(base * 1.3f);
  } else if (phrase_density > 0.7f) {
    base = static_cast<Tick>(base * 1.15f);
  }

  // High pitch phrases (C5=72 or above) need more breath
  // This mimics natural singing where high notes require more breath recovery
  if (phrase_high_pitch >= 72) {  // C5 or higher
    base = static_cast<Tick>(base * 1.2f);
  }

  // Cap at half note to avoid excessive gaps
  return std::min(base, TICK_HALF);
}

// Get rhythm unit based on grid type.
// Binary grid uses 8th/16th notes, Ternary uses triplets.
// @param grid Rhythm grid type
// @param is_eighth Whether to use 8th note base (vs quarter)
// @return Tick duration for the rhythm unit
Tick getRhythmUnit(RhythmGrid grid, bool is_eighth) {
  switch (grid) {
    case RhythmGrid::Ternary:
      return is_eighth ? TICK_EIGHTH_TRIPLET : TICK_QUARTER_TRIPLET;
    case RhythmGrid::Hybrid:
      // Hybrid uses binary as base but allows triplets in specific contexts
      // (handled separately where needed)
      return is_eighth ? TICK_EIGHTH : TICK_QUARTER;
    case RhythmGrid::Binary:
    default:
      return is_eighth ? TICK_EIGHTH : TICK_QUARTER;
  }
}

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
int getNearestSafeChordTone(int current_pitch, int8_t chord_degree, int root_pc, uint8_t vocal_low,
                            uint8_t vocal_high) {
  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);
  if (chord_tones.empty()) {
    return std::clamp(current_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
  }

  // Initialize best_pitch to clamped current_pitch (fallback if no candidates found)
  int best_pitch =
      std::clamp(current_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
  int best_distance = 100;

  // Search in multiple octaves
  for (int pc : chord_tones) {
    // Skip if this pitch class is an avoid note with root
    if (isAvoidNoteWithRoot(pc, root_pc)) continue;

    for (int oct = 3; oct <= 6; ++oct) {
      int candidate = oct * 12 + pc;
      if (candidate < static_cast<int>(vocal_low) || candidate > static_cast<int>(vocal_high)) {
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
int getAnchorTonePitch(int8_t chord_degree, int tessitura_center, uint8_t vocal_low,
                       uint8_t vocal_high) {
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

/// @brief Apply sequential transposition to B section phrases (Zekvenz effect).
/// Creates ascending sequence pattern to build tension before chorus.
/// @param notes Phrase notes to transpose (modified in-place)
/// @param phrase_index Index of the phrase within the section (0-based)
/// @param section_type Section type (only applies to B sections)
/// @param key_offset Key offset for scale snapping
/// @param vocal_low Minimum vocal pitch
/// @param vocal_high Maximum vocal pitch
void applySequentialTransposition(std::vector<NoteEvent>& notes, uint8_t phrase_index,
                                  SectionType section_type, int key_offset, uint8_t vocal_low,
                                  uint8_t vocal_high) {
  // Only apply to B sections and non-first phrases
  if (section_type != SectionType::B || phrase_index == 0 || notes.empty()) {
    return;
  }

  // Sequential intervals: ascending by scale-like amounts
  // phrase 1: +2 semitones, phrase 2: +4 semitones, phrase 3+: +5 semitones
  constexpr int8_t kSequenceIntervals[] = {0, 2, 4, 5};
  int transpose = (phrase_index < 4) ? kSequenceIntervals[phrase_index] : 5;

  for (auto& note : notes) {
    int new_pitch = note.note + transpose;
    // Snap to scale to avoid chromatic notes
    new_pitch = snapToNearestScaleTone(new_pitch, key_offset);
    // Constrain to vocal range
    new_pitch =
        std::clamp(new_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
    note.note = static_cast<uint8_t>(new_pitch);
  }
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
  uint8_t durations[6];  // Note durations in eighths (0 = end marker)
  uint8_t note_count;    // Number of notes in pattern
  Tick gap_after;        // Gap after pattern (in ticks)
  const char* name;      // Pattern name for debugging
};

// Common pop hook rhythm patterns
// Each pattern is designed to be catchy and singable
// ENHANCED: Added killer rhythm patterns for more variety and catchiness
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

    // =========================================================================
    // NEW KILLER RHYTHM PATTERNS for enhanced catchiness
    // =========================================================================

    // Pattern 7: Syncopated burst (R-8-8-16-16)
    // Example: Funk-influenced pop, off-beat energy
    // The rest at the start creates anticipation, 16th notes add drive
    {{1, 1, 1, 1, 0, 0}, 4, TICK_SIXTEENTH, "synco-burst"},

    // Pattern 8: Staccato syncopation (16-R-16-R-8-8)
    // Example: Dance-pop hooks with gaps for impact
    // The gaps between short notes create rhythmic tension
    {{1, 1, 1, 2, 0, 0}, 4, TICK_SIXTEENTH, "staccato-synco"},

    // Pattern 9: Anticipation pattern (4-16-16-4)
    // Example: Leaning into the beat style
    // Long-short-short-long creates urgency
    {{2, 1, 1, 2, 0, 0}, 4, TICK_EIGHTH, "anticipation"},

    // Pattern 10: Drill pattern (16-16-16-16-16-8)
    // Example: Rapid-fire hooks, Vocaloid style
    // Machine-gun notes ending in resolution
    {{1, 1, 1, 1, 1, 2}, 6, TICK_SIXTEENTH, "drill"},

    // Pattern 11: 2-mora ending (8-4)
    // Example: Classic J-pop phrase endings
    // Simple but effective for syllable-based lyrics
    {{1, 2, 0, 0, 0, 0}, 2, TICK_EIGHTH, "mora-2"},

    // Pattern 12: 3-mora pattern (8-8-4)
    // Example: Standard J-pop hook rhythm
    // Maps well to 3-syllable words
    {{1, 1, 2, 0, 0, 0}, 3, TICK_EIGHTH, "mora-3"},

    // Pattern 13: 3-mora start emphasis (4-8-8)
    // Example: Emphasis on first syllable
    // Creates forward momentum
    {{2, 1, 1, 0, 0, 0}, 3, TICK_EIGHTH, "mora-3-start"},

    // Pattern 14: 4-mora pattern (8-8-8-4)
    // Example: Maps to 4-syllable words
    // Builds to resolution
    {{1, 1, 1, 2, 0, 0}, 4, TICK_EIGHTH, "mora-4"},
};

constexpr size_t kHookRhythmPatternCount =
    sizeof(kHookRhythmPatterns) / sizeof(kHookRhythmPatterns[0]);

// Select a hook rhythm pattern index based on template characteristics
size_t selectHookRhythmPatternIndex(const MelodyTemplate& tmpl, std::mt19937& rng) {
  // Weight patterns based on template style
  // ENHANCED: Include new killer patterns based on style
  std::vector<size_t> candidates;

  if (tmpl.rhythm_driven) {
    // Rhythm-driven: prefer energetic patterns including new syncopated ones
    candidates = {0, 2, 5, 6, 7, 8, 9};  // buildup, four-note, call-response, synco-burst, staccato, anticipation, drill
  } else if (tmpl.long_note_ratio > 0.3f) {
    // Sparse style: prefer simpler patterns and mora patterns
    candidates = {3, 1, 4, 10, 12};  // powerful, syncopated, dotted, mora-2, mora-3-start
  } else if (tmpl.sixteenth_density > 0.3f) {
    // High density (Vocaloid style): include drill and syncopated patterns
    candidates = {2, 5, 6, 7, 9, 13};  // four-note, call-response, synco-burst, staccato, drill, mora-4
  } else {
    // Balanced: use all patterns with slight preference for classic ones
    for (size_t i = 0; i < kHookRhythmPatternCount; ++i) {
      candidates.push_back(i);
    }
  }

  std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
  return candidates[dist(rng)];
}

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
          // Use nearestChordToneWithinInterval to stay on chord tones
          adjusted_note.note = static_cast<uint8_t>(nearestChordToneWithinInterval(
              adjusted_note.note, prev_note_pitch, note_chord_degree, MAX_PHRASE_INTERVAL,
              ctx.vocal_low, ctx.vocal_high, &ctx.tessitura));
        }
      }
      // ABSOLUTE CONSTRAINT: Ensure pitch is on scale (prevents chromatic notes)
      int snapped = snapToNearestScaleTone(adjusted_note.note, ctx.key_offset);
      adjusted_note.note = static_cast<uint8_t>(
          std::clamp(snapped, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high)));
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
      current_tick += getBreathDuration(ctx.section_type, ctx.mood, phrase_density, phrase_high_pitch);
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
  int prev_final_pitch = -1;
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
        // Use interval-aware snapping to preserve melodic contour
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
      }
    }
    // Enforce interval constraint between all consecutive notes
    if (prev_final_pitch >= 0) {
      int interval = std::abs(static_cast<int>(note.note) - prev_final_pitch);
      int max_interval = getMaxMelodicIntervalForSection(ctx.section_type);
      if (interval > max_interval) {
        int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);
        int constrained_pitch = nearestChordToneWithinInterval(
            note.note, prev_final_pitch, chord_degree, max_interval, ctx.vocal_low,
            ctx.vocal_high, &ctx.tessitura);
        // Defensive clamp to ensure vocal range is respected
        constrained_pitch = std::clamp(constrained_pitch, static_cast<int>(ctx.vocal_low),
                                       static_cast<int>(ctx.vocal_high));
        note.note = static_cast<uint8_t>(constrained_pitch);
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

  // Create NoteFactory for provenance tracking
  NoteFactory factory(harmony);

  // Calculate syncopation weight based on vocal groove, section type, and drive_feel
  // drive_feel modulates syncopation: laid-back = less, aggressive = more
  float syncopation_weight = getSyncopationWeight(ctx.vocal_groove, ctx.section_type, ctx.drive_feel);

  // Generate rhythm pattern with section density modifier and 32nd note ratio
  std::vector<RhythmNote> rhythm = generatePhraseRhythm(
      tmpl, phrase_beats, ctx.density_modifier, ctx.thirtysecond_ratio, rng, ctx.paradigm,
      syncopation_weight);

  // Get chord degree at phrase start
  int8_t start_chord_degree = harmony.getChordDegreeAt(phrase_start);

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
    PitchChoice choice = selectPitchChoice(tmpl, phrase_pos, target_pitch >= 0, ctx.section_type, rng);

    // Apply direction inertia
    choice = applyDirectionInertia(choice, result.direction_inertia, tmpl, rng);

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
    int new_pitch = applyPitchChoice(choice, current_pitch, target_pitch, note_chord_degree,
                                     ctx.key_offset, ctx.vocal_low, ctx.vocal_high,
                                     ctx.vocal_attitude, ctx.disable_vowel_constraints);

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
        case 1:
          allow_prob = 1.0f;
          break;  // First note always OK
        case 2:
          allow_prob = 0.85f;
          break;  // 2nd repetition: 85%
        case 3:
          allow_prob = 0.50f;
          break;  // 3rd repetition: 50%
        case 4:
          allow_prob = 0.25f;
          break;  // 4th repetition: 25%
        default:
          allow_prob = 0.05f;
          break;  // 5+: 5%
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

    // Enforce maximum interval constraint (section-adaptive)
    // Use nearestChordToneWithinInterval to stay on chord tones
    // getMaxMelodicIntervalForSection allows wider leaps in chorus/bridge
    int max_interval = getMaxMelodicIntervalForSection(ctx.section_type);
    int interval = std::abs(new_pitch - current_pitch);
    if (interval > max_interval) {
      new_pitch = nearestChordToneWithinInterval(new_pitch, current_pitch, note_chord_degree,
                                                 max_interval, ctx.vocal_low, ctx.vocal_high,
                                                 &ctx.tessitura);
    }

    // Leap preparation principle (pop vocal theory):
    // Large leaps after very short notes are difficult to sing.
    // Singers need time to prepare for large pitch jumps.
    // After very short notes (< 0.5 beats), limit to perfect 4th (5 semitones).
    // This prevents awkward jumps while still allowing common pop intervals (3rds, 4ths).
    constexpr Tick VERY_SHORT_THRESHOLD = TICKS_PER_BEAT / 2;  // 0.5 beats (8th note)
    constexpr int MAX_LEAP_AFTER_SHORT = 5;                    // Perfect 4th
    if (i > 0 && prev_note_duration < VERY_SHORT_THRESHOLD) {
      int leap = std::abs(new_pitch - current_pitch);
      if (leap > MAX_LEAP_AFTER_SHORT) {
        new_pitch = nearestChordToneWithinInterval(new_pitch, current_pitch, note_chord_degree,
                                                   MAX_LEAP_AFTER_SHORT, ctx.vocal_low,
                                                   ctx.vocal_high, &ctx.tessitura);
      }
    }

    // Leap encouragement for long notes (pop vocal theory):
    // After long notes (>= 1 beat), listeners expect melodic movement.
    // Static pitches or small steps after held notes can feel anticlimactic.
    // Encourage larger intervals (major 3rd or more) to create melodic interest.
    // 60% chance to encourage leap to avoid deterministic behavior.
    constexpr Tick LONG_NOTE_THRESHOLD = TICKS_PER_BEAT;  // 1 beat (quarter note)
    constexpr int PREFERRED_LEAP_AFTER_LONG = 4;         // Major 3rd minimum
    if (i > 0 && prev_note_duration >= LONG_NOTE_THRESHOLD) {
      int current_interval = std::abs(new_pitch - current_pitch);
      // If staying on same pitch or small step after long note, consider encouraging movement
      if (current_interval < PREFERRED_LEAP_AFTER_LONG) {
        std::uniform_real_distribution<float> leap_dist(0.0f, 1.0f);
        if (leap_dist(rng) < 0.6f) {  // 60% chance to encourage leap
          // Find chord tones at least PREFERRED_LEAP_AFTER_LONG semitones away
          std::vector<int> chord_tones = getChordTonePitchClasses(note_chord_degree);
          std::vector<int> leap_candidates;
          for (int pc : chord_tones) {
            for (int oct = 4; oct <= 6; ++oct) {
              int candidate = oct * 12 + pc;
              int interval = std::abs(candidate - current_pitch);
              if (candidate >= ctx.vocal_low && candidate <= ctx.vocal_high &&
                  interval >= PREFERRED_LEAP_AFTER_LONG && interval <= kMaxMelodicInterval) {
                leap_candidates.push_back(candidate);
              }
            }
          }
          if (!leap_candidates.empty()) {
            // Pick random leap candidate
            std::uniform_int_distribution<size_t> idx_dist(0, leap_candidates.size() - 1);
            new_pitch = leap_candidates[idx_dist(rng)];
          }
        }
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
          // For machine-style vocals (UltraVocaloid), use simple nearest chord tone
          // to preserve rapid-fire articulation patterns
          if (ctx.disable_vowel_constraints) {
            new_pitch = nearestChordTonePitch(new_pitch, note_chord_degree);
            new_pitch = std::clamp(new_pitch, static_cast<int>(ctx.vocal_low),
                                   static_cast<int>(ctx.vocal_high));
          } else {
            // SINGABILITY: Snap to chord tone that minimizes interval from previous pitch
            // This preserves melodic contour better than snapping to nearest chord tone
            // Music theory: preserve intended direction when avoiding same pitch
            bool intended_movement = (new_pitch != current_pitch);
            int intended_direction = (new_pitch > current_pitch) ? 1 : -1;

            // Find best chord tone (closest to current_pitch)
            int best_pitch = new_pitch;
            int best_interval = 127;

            // Also track best chord tone in the intended direction
            int best_directional_pitch = -1;
            int best_directional_interval = 127;

            for (int ct : chord_tones) {
              // Check multiple octaves around current pitch
              for (int oct = 3; oct <= 7; ++oct) {
                int candidate = oct * 12 + ct;
                if (candidate < ctx.vocal_low || candidate > ctx.vocal_high) continue;
                int interval = std::abs(candidate - current_pitch);

                // Track absolute best
                if (interval < best_interval) {
                  best_interval = interval;
                  best_pitch = candidate;
                }

                // Track best in intended direction (excluding same pitch)
                if (candidate != current_pitch) {
                  int direction = (candidate > current_pitch) ? 1 : -1;
                  if (direction == intended_direction && interval < best_directional_interval) {
                    best_directional_interval = interval;
                    best_directional_pitch = candidate;
                  }
                }
              }
            }

            // Decision: if movement was intended but best is same pitch,
            // use directional best (preserves melodic contour)
            if (intended_movement && best_pitch == current_pitch && best_directional_pitch >= 0 &&
                best_directional_interval <= 5) {  // Allow up to P4 (5 semitones)
              new_pitch = best_directional_pitch;
            } else {
              new_pitch = best_pitch;
            }
          }
        }
      }
    }

    // =========================================================================
    // LEAP-AFTER-REVERSAL RULE (singability improvement):
    // After a large leap (4+ semitones), prefer step motion in opposite direction.
    // This is a fundamental vocal principle: singers need to "recover" after jumps.
    // Exception: Skip if we just chose Same pitch or are at phrase boundaries.
    // =========================================================================
    if (i > 0 && !result.notes.empty()) {
      int prev_note = result.notes.back().note;
      int prev_interval = current_pitch - prev_note;  // Signed: positive=up, negative=down
      constexpr int kLeapThreshold = 4;               // Major 3rd or larger

      if (std::abs(prev_interval) >= kLeapThreshold && new_pitch != current_pitch) {
        // Previous move was a leap; prefer opposite direction step
        int current_interval = new_pitch - current_pitch;
        bool is_same_direction =
            (prev_interval > 0 && current_interval > 0) || (prev_interval < 0 && current_interval < 0);

        if (is_same_direction) {
          // Try to find a chord tone in the opposite direction (step motion)
          int preferred_direction = (prev_interval > 0) ? -1 : 1;  // Opposite of leap
          std::vector<int> chord_tones = getChordTonePitchClasses(note_chord_degree);

          int best_reversal_pitch = -1;
          int best_reversal_interval = 127;

          for (int ct : chord_tones) {
            for (int oct = 4; oct <= 6; ++oct) {
              int candidate = oct * 12 + ct;
              if (candidate < ctx.vocal_low || candidate > ctx.vocal_high) continue;

              int interval_from_current = candidate - current_pitch;
              int direction = (interval_from_current > 0) ? 1 : (interval_from_current < 0) ? -1 : 0;

              // Must be in preferred direction and be a step (1-3 semitones)
              if (direction == preferred_direction) {
                int abs_interval = std::abs(interval_from_current);
                if (abs_interval >= 1 && abs_interval <= 3 && abs_interval < best_reversal_interval) {
                  best_reversal_interval = abs_interval;
                  best_reversal_pitch = candidate;
                }
              }
            }
          }

          // Apply reversal if found a good candidate (60% probability to allow some flexibility)
          if (best_reversal_pitch >= 0) {
            std::uniform_real_distribution<float> rev_dist(0.0f, 1.0f);
            if (rev_dist(rng) < 0.6f) {
              new_pitch = best_reversal_pitch;
            }
          }
        }
      }
    }

    // FINAL SAFETY CHECK: Re-enforce max interval after all adjustments
    // Previous adjustments (avoid note, downbeat snapping) might have created
    // large intervals. This final check ensures singability is maintained.
    {
      // Section-adaptive max interval from pitch_utils.h
      int section_max_interval = getMaxMelodicIntervalForSection(ctx.section_type);
      int final_interval = std::abs(new_pitch - current_pitch);
      if (final_interval > section_max_interval) {
        new_pitch = nearestChordToneWithinInterval(new_pitch, current_pitch, note_chord_degree,
                                                   section_max_interval, ctx.vocal_low,
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
      // Guard: ensure chord_change - note_start > 10 to avoid underflow
      if (chord_change > note_start + 10) {
        Tick new_duration = chord_change - note_start - 10;  // Small gap before chord change
        if (new_duration >= TICK_SIXTEENTH) {
          note_duration = new_duration;
        }
      }
    }

    // Also clamp to phrase boundary for phrase ending
    Tick phrase_end = phrase_start + phrase_beats * TICKS_PER_BEAT;
    if (note_start + note_duration > phrase_end) {
      // Guard: ensure phrase_end > note_start to avoid underflow
      if (phrase_end > note_start) {
        note_duration = phrase_end - note_start;
      }
      // Ensure minimum duration (16th note) for musical validity
      if (note_duration < TICK_SIXTEENTH) {
        note_duration = TICK_SIXTEENTH;
      }
    }

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

    // Final clamp to ensure pitch is within vocal range
    // ABSOLUTE CONSTRAINT: Ensure pitch is on scale (prevents chromatic notes)
    new_pitch = snapToNearestScaleTone(new_pitch, ctx.key_offset);
    new_pitch =
        std::clamp(new_pitch, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high));

    // Add note with provenance tracking
    result.notes.push_back(factory.create(note_start, note_duration,
                                          static_cast<uint8_t>(new_pitch), velocity,
                                          NoteSource::MelodyPhrase));

    current_pitch = new_pitch;
    prev_note_duration = note_duration;  // Track for leap preparation
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
  base_pitch =
      std::clamp(base_pitch, static_cast<int>(ctx.vocal_low), static_cast<int>(ctx.vocal_high));

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
      int blended = static_cast<int>(hook.contour_degrees[i] * 0.8f + skeleton_hint * 0.2f);
      hook.contour_degrees[i] = static_cast<int8_t>(blended);
    }
  }

  // Select rhythm pattern based on template style (cached per song for consistency)
  if (cached_hook_rhythm_pattern_idx_ == SIZE_MAX) {
    cached_hook_rhythm_pattern_idx_ = selectHookRhythmPatternIndex(tmpl, rng);
  }
  const HookRhythmPattern& rhythm_pattern = kHookRhythmPatterns[cached_hook_rhythm_pattern_idx_];

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
  ++hook_repetition_count_;
  HookBetrayal betrayal = HookBetrayal::None;
  uint8_t threshold = tmpl.betrayal_threshold > 0 ? tmpl.betrayal_threshold : 4;
  if (tmpl.betrayal_threshold > 0 && hook_repetition_count_ >= threshold &&
      (hook_repetition_count_ % threshold) == 0) {
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
  bool use_cached_sabi = (sabi_pitches_cached_ && ctx.section_type == SectionType::Chorus);

  // Track consecutive same notes for J-POP style probability curve
  int consecutive_same_count = 0;

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
        pitch = static_cast<int>(cached_sabi_pitches_[total_note_idx]);
        use_cached_rhythm_for_note = sabi_rhythm_cached_;
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
      {
        int bass_root_pc = getBassRootPitchClass(note_chord_degree);
        std::vector<int> chord_tones = getChordTonePitchClasses(note_chord_degree);
        int pitch_pc = pitch % 12;
        if (isAvoidNoteWithChord(pitch_pc, chord_tones, bass_root_pc)) {
          pitch = getNearestSafeChordTone(pitch, note_chord_degree, bass_root_pc, ctx.vocal_low,
                                          ctx.vocal_high);
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
            pitch = std::clamp(pitch, static_cast<int>(ctx.vocal_low),
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
          case 1:
            allow_prob = 1.0f;
            break;
          case 2:
            allow_prob = 0.85f;
            break;
          case 3:
            allow_prob = 0.50f;
            break;
          case 4:
            allow_prob = 0.25f;
            break;
          default:
            allow_prob = 0.05f;
            break;
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
                std::abs(candidate - prev_hook_pitch) <= kMaxMelodicInterval) {
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
        // Guard: ensure chord_change - current_tick > 10 to avoid underflow
        if (chord_change > current_tick + 10) {
          Tick new_duration = chord_change - current_tick - 10;
          if (new_duration >= TICK_SIXTEENTH) {
            actual_duration = new_duration;
          }
        }
      }

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
        final_duration = cached_sabi_durations_[total_note_idx];
        final_velocity = cached_sabi_velocities_[total_note_idx];
        // For tick advancement, use cached duration to maintain timing consistency
        tick_advance = cached_sabi_durations_[total_note_idx];
      }

      result.notes.push_back(factory.create(
          current_tick, final_duration, static_cast<uint8_t>(pitch), final_velocity, NoteSource::Hook));

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
    for (size_t i = 0; i < result.notes.size() && i < pitches.size(); ++i) {
      int new_pitch = std::clamp(static_cast<int>(pitches[i]),
                                 static_cast<int>(ctx.vocal_low),
                                 static_cast<int>(ctx.vocal_high));
      result.notes[i].note = static_cast<uint8_t>(new_pitch);
      if (i < durations.size()) {
        result.notes[i].duration = durations[i];
      }
    }
  }

  // =========================================================================
  // SABI (CHORUS) HEAD CACHING: Store first 8 pitches, durations, velocities
  // =========================================================================
  // Cache the first 8 notes of the chorus hook for reuse in subsequent
  // chorus sections. This ensures the "sabi" (hook head) is memorable.
  // Rhythm (duration + velocity) is also cached for complete consistency.
  if (!sabi_pitches_cached_ && ctx.section_type == SectionType::Chorus &&
      result.notes.size() >= 8) {
    for (size_t i = 0; i < 8 && i < result.notes.size(); ++i) {
      cached_sabi_pitches_[i] = result.notes[i].note;
      cached_sabi_durations_[i] = result.notes[i].duration;
      cached_sabi_velocities_[i] = result.notes[i].velocity;
    }
    sabi_pitches_cached_ = true;
    sabi_rhythm_cached_ = true;
  }

  // Return last pitch for smooth transition to next phrase
  result.last_pitch = prev_hook_pitch;
  result.direction_inertia = 0;  // Reset inertia after hook

  return result;
}

PitchChoice MelodyDesigner::selectPitchChoice(const MelodyTemplate& tmpl, float phrase_pos,
                                              bool has_target, SectionType section_type,
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

  // Step 3: Section-aware directional bias
  // Different sections have different melodic direction tendencies:
  // - A (Verse): slightly ascending for storytelling momentum
  // - B (Pre-chorus): ascending more strongly in second half for tension building
  // - Chorus: balanced for hook memorability
  // - Bridge: slightly descending for contrast
  float upward_bias;
  switch (section_type) {
    case SectionType::A:
      upward_bias = 0.55f;  // Slight upward tendency
      break;
    case SectionType::B:
      upward_bias = phrase_pos > 0.5f ? 0.65f : 0.55f;  // Strong rise in second half
      break;
    case SectionType::Chorus:
      upward_bias = 0.50f;  // Balanced
      break;
    case SectionType::Bridge:
      upward_bias = 0.45f;  // Slight downward for contrast
      break;
    default:
      upward_bias = 0.50f;  // Balanced for other sections
      break;
  }
  return (dist(rng) < upward_bias) ? PitchChoice::StepUp : PitchChoice::StepDown;
}

PitchChoice MelodyDesigner::applyDirectionInertia(PitchChoice choice, int inertia,
                                                  [[maybe_unused]] const MelodyTemplate& tmpl,
                                                  std::mt19937& rng) {
  // Same pitch or target step - don't modify
  if (choice == PitchChoice::Same || choice == PitchChoice::TargetStep) {
    return choice;
  }

  // Strong inertia can override random direction
  // Coefficient 0.7 for better melodic continuity (was 0.5)
  constexpr float kInertiaCoefficient = 0.7f;

  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  int abs_inertia = std::abs(inertia);

  // Decay after 3 consecutive same-direction moves to prevent monotony
  float decay_factor = 1.0f;
  if (abs_inertia > 3) {
    decay_factor = std::pow(0.8f, static_cast<float>(abs_inertia - 3));
  }

  float inertia_strength = (static_cast<float>(abs_inertia) / 3.0f) * decay_factor;

  if (dist(rng) < inertia_strength * kInertiaCoefficient) {
    // Follow inertia direction
    if (inertia > 0) {
      return PitchChoice::StepUp;
    } else if (inertia < 0) {
      return PitchChoice::StepDown;
    }
  }

  return choice;
}

float MelodyDesigner::getEffectivePlateauRatio(const MelodyTemplate& tmpl, int current_pitch,
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

bool MelodyDesigner::shouldLeap(LeapTrigger trigger, float phrase_pos, float section_pos) {
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

bool MelodyDesigner::isInSameVowelSection(float pos1, float pos2,
                                          [[maybe_unused]] uint8_t phrase_length) {
  // Simple vowel section model: divide phrase into 2-beat sections
  constexpr float VOWEL_SECTION_BEATS = 2.0f;

  int section1 = static_cast<int>(pos1 / VOWEL_SECTION_BEATS);
  int section2 = static_cast<int>(pos2 / VOWEL_SECTION_BEATS);

  return section1 == section2;
}

int8_t MelodyDesigner::getMaxStepInVowelSection(bool in_same_vowel) {
  return in_same_vowel ? 2 : 4;
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
    if (std::abs(leading_pitch - last_pitch) <= kMaxMelodicInterval) {
      insertLeadingTone(notes, ctx, harmony);
    }
  }
}

void MelodyDesigner::insertLeadingTone(std::vector<NoteEvent>& notes, const SectionContext& ctx,
                                       const IHarmonyContext& harmony) {
  if (notes.empty()) return;

  // Create NoteFactory for provenance tracking
  NoteFactory factory(harmony);

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
    uint8_t velocity = static_cast<uint8_t>(
        std::min(127, static_cast<int>(last_note.velocity) + 10));  // Slightly louder

    notes.push_back(
        factory.create(leading_tone_start,
                       TICKS_PER_BEAT / 4,  // 16th note duration
                       static_cast<uint8_t>(leading_pitch), velocity,
                       NoteSource::PostProcess));  // Leading tone is a post-processing addition
  }
}

int MelodyDesigner::applyPitchChoice(PitchChoice choice, int current_pitch, int target_pitch,
                                     int8_t chord_degree, int key_offset, uint8_t vocal_low,
                                     uint8_t vocal_high, VocalAttitude attitude,
                                     bool disable_singability) {
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
                      static_cast<int>(vocal_low), static_cast<int>(vocal_high));
  }

  int new_pitch = current_pitch;

  switch (choice) {
    case PitchChoice::Same:
      // Stay on nearest chord tone to current pitch
      new_pitch = nearestChordTonePitch(current_pitch, chord_degree);
      break;

    case PitchChoice::StepUp: {
      int best = -1;
      // For machine-style vocals (UltraVocaloid), use chord-tone-first approach
      // to preserve rapid articulation patterns
      if (disable_singability) {
        // Find smallest chord tone above current pitch
        for (int c : candidates) {
          if (c > current_pitch) {
            best = c;
            break;  // Already sorted, first one above is smallest
          }
        }
      } else {
        // SINGABILITY: Prefer step motion while maintaining harmonic awareness
        // Priority order:
        //   1) Scale tone step (whole step > half step for consonance)
        //   2) Chord tone within small interval (M3 = 4 semitones)
        //   3) Any chord tone (fallback)
        // Note: Downbeat chord-tone constraint ensures strong beats are harmonically correct

        // Priority 1: Scale tone step (prefer whole step for more consonant motion)
        for (int step = 2; step >= 1; --step) {
          int candidate = current_pitch + step;
          if (candidate <= vocal_high &&
              isScaleTone(candidate % 12, static_cast<uint8_t>(key_offset))) {
            best = candidate;
            break;
          }
        }

        // Priority 2: Chord tone within small interval
        if (best < 0) {
          for (int c : candidates) {
            if (c > current_pitch && c - current_pitch <= 4) {
              best = c;
              break;
            }
          }
        }

        // Priority 3: Any chord tone (fallback)
        if (best < 0) {
          for (int c : candidates) {
            if (c > current_pitch) {
              best = c;
              break;
            }
          }
        }
      }
      if (best < 0) {
        // No chord tone above, use nearest
        best = nearestChordTonePitch(current_pitch, chord_degree);
      }
      // SINGABILITY: Enforce maximum interval constraint (major 6th = 9 semitones)
      // Large leaps are difficult to sing and sound unnatural in pop melodies
      constexpr int kMaxMelodicInterval = 9;
      if (best >= 0 && std::abs(best - current_pitch) > kMaxMelodicInterval) {
        // Find closest chord tone within max interval
        int closest = -1;
        int closest_dist = 127;
        for (int c : candidates) {
          int dist = std::abs(c - current_pitch);
          if (dist <= kMaxMelodicInterval && dist < closest_dist) {
            closest_dist = dist;
            closest = c;
          }
        }
        if (closest >= 0) {
          best = closest;
        } else {
          // No chord tone within range, stay on current or use nearest
          best = nearestChordTonePitch(current_pitch, chord_degree);
        }
      }
      new_pitch = best;
    } break;

    case PitchChoice::StepDown: {
      int best = -1;
      // For machine-style vocals (UltraVocaloid), use chord-tone-first approach
      if (disable_singability) {
        // Find largest chord tone below current pitch
        for (int i = static_cast<int>(candidates.size()) - 1; i >= 0; --i) {
          if (candidates[i] < current_pitch) {
            best = candidates[i];
            break;
          }
        }
      } else {
        // SINGABILITY: Prefer step motion while maintaining harmonic awareness
        // Same priority order as StepUp

        // Priority 1: Scale tone step (prefer whole step for more consonant motion)
        for (int step = 2; step >= 1; --step) {
          int candidate = current_pitch - step;
          if (candidate >= vocal_low &&
              isScaleTone(candidate % 12, static_cast<uint8_t>(key_offset))) {
            best = candidate;
            break;
          }
        }

        // Priority 2: Chord tone within small interval
        if (best < 0) {
          for (int i = static_cast<int>(candidates.size()) - 1; i >= 0; --i) {
            if (candidates[i] < current_pitch && current_pitch - candidates[i] <= 4) {
              best = candidates[i];
              break;
            }
          }
        }

        // Priority 3: Any chord tone (fallback)
        if (best < 0) {
          for (int i = static_cast<int>(candidates.size()) - 1; i >= 0; --i) {
            if (candidates[i] < current_pitch) {
              best = candidates[i];
              break;
            }
          }
        }
      }
      if (best < 0) {
        best = nearestChordTonePitch(current_pitch, chord_degree);
      }
      // SINGABILITY: Enforce maximum interval constraint (major 6th = 9 semitones)
      constexpr int kMaxMelodicInterval = 9;
      if (best >= 0 && std::abs(best - current_pitch) > kMaxMelodicInterval) {
        // Find closest chord tone within max interval
        int closest = -1;
        int closest_dist = 127;
        for (int c : candidates) {
          int dist = std::abs(c - current_pitch);
          if (dist <= kMaxMelodicInterval && dist < closest_dist) {
            closest_dist = dist;
            closest = c;
          }
        }
        if (closest >= 0) {
          best = closest;
        } else {
          best = nearestChordTonePitch(current_pitch, chord_degree);
        }
      }
      new_pitch = best;
    } break;

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
  new_pitch = std::clamp(new_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));

  return new_pitch;
}

int MelodyDesigner::calculateTargetPitch(const MelodyTemplate& tmpl, const SectionContext& ctx,
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
    const MelodyTemplate& tmpl, uint8_t phrase_beats, float density_modifier,
    float thirtysecond_ratio, std::mt19937& rng, GenerationParadigm paradigm,
    float syncopation_weight) {
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
    if (is_on_beat && syncopation_weight > 0.0f &&
        current_beat + 0.5f < phrase_body_end) {
      float synco_roll = dist(rng);
      if (synco_roll < syncopation_weight) {
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
      if (thirtysecond_ratio > 0.0f && dist(rng) < thirtysecond_ratio) {
        eighths = 0.25f;  // 32nd note (0.25 eighth = 60 ticks)
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

// === Locked Rhythm Pitch Selection ===

uint8_t MelodyDesigner::selectPitchForLockedRhythm(uint8_t prev_pitch, int8_t chord_degree,
                                                   uint8_t vocal_low, uint8_t vocal_high,
                                                   std::mt19937& rng) {
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

// === GlobalMotif Support ===

GlobalMotif MelodyDesigner::extractGlobalMotif(const std::vector<NoteEvent>& notes) {
  GlobalMotif motif;

  if (notes.size() < 2) {
    return motif;  // Not enough notes for meaningful analysis
  }

  // Extract interval signature (relative pitch changes)
  size_t interval_limit = std::min(notes.size() - 1, static_cast<size_t>(8));
  for (size_t i = 0; i < interval_limit; ++i) {
    int interval = static_cast<int>(notes[i + 1].note) - static_cast<int>(notes[i].note);
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
      uint8_t ratio = static_cast<uint8_t>((notes[i].duration * 8) / max_duration);
      motif.rhythm_signature[i] =
          std::clamp(ratio, static_cast<uint8_t>(1), static_cast<uint8_t>(8));
    }
    motif.rhythm_count = static_cast<uint8_t>(std::min(notes.size(), static_cast<size_t>(8)));
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

float MelodyDesigner::evaluateWithGlobalMotif(const std::vector<NoteEvent>& candidate,
                                              const GlobalMotif& global_motif) {
  if (!global_motif.isValid() || candidate.size() < 2) {
    return 0.0f;
  }

  float bonus = 0.0f;

  // Extract candidate's contour
  GlobalMotif candidate_motif = extractGlobalMotif(candidate);

  // Contour similarity bonus (0.0-0.10)
  // Increased from 0.05 to strengthen melodic coherence across sections.
  if (candidate_motif.contour_type == global_motif.contour_type) {
    bonus += 0.10f;
  }

  // Interval pattern similarity bonus (0.0-0.05)
  if (candidate_motif.interval_count > 0 && global_motif.interval_count > 0) {
    size_t compare_count = std::min(static_cast<size_t>(candidate_motif.interval_count),
                                    static_cast<size_t>(global_motif.interval_count));

    int similarity_score = 0;
    for (size_t i = 0; i < compare_count; ++i) {
      int diff =
          std::abs(candidate_motif.interval_signature[i] - global_motif.interval_signature[i]);
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

  // Contour direction matching bonus (0.0-0.05)
  // Rewards candidates whose individual interval directions match the DNA pattern.
  // If the DNA goes up at position N, ascending intervals at that position get a bonus.
  if (candidate_motif.interval_count > 0 && global_motif.interval_count > 0) {
    size_t compare_count = std::min(static_cast<size_t>(candidate_motif.interval_count),
                                    static_cast<size_t>(global_motif.interval_count));
    int direction_matches = 0;
    for (size_t idx = 0; idx < compare_count; ++idx) {
      int cand_dir = (candidate_motif.interval_signature[idx] > 0)
                         ? 1
                         : (candidate_motif.interval_signature[idx] < 0 ? -1 : 0);
      int motif_dir = (global_motif.interval_signature[idx] > 0)
                          ? 1
                          : (global_motif.interval_signature[idx] < 0 ? -1 : 0);
      if (cand_dir == motif_dir && cand_dir != 0) {
        direction_matches++;
      }
    }
    // Normalize: each matching direction contributes proportionally
    if (compare_count > 0) {
      bonus +=
          (static_cast<float>(direction_matches) / static_cast<float>(compare_count)) * 0.05f;
    }
  }

  // Interval consistency bonus (0.0-0.05)
  // Rewards candidates that preserve the step-vs-leap character of the DNA.
  // Steps (1-2 semitones) matching steps, and leaps (3+) matching leaps.
  if (candidate_motif.interval_count > 0 && global_motif.interval_count > 0) {
    size_t compare_count = std::min(static_cast<size_t>(candidate_motif.interval_count),
                                    static_cast<size_t>(global_motif.interval_count));
    int consistency_matches = 0;
    for (size_t idx = 0; idx < compare_count; ++idx) {
      int cand_abs = std::abs(candidate_motif.interval_signature[idx]);
      int motif_abs = std::abs(global_motif.interval_signature[idx]);
      bool cand_is_step = (cand_abs >= 1 && cand_abs <= 2);
      bool motif_is_step = (motif_abs >= 1 && motif_abs <= 2);
      // Both steps or both leaps (3+ semitones)
      if (cand_is_step == motif_is_step && (cand_abs > 0 || motif_abs > 0)) {
        consistency_matches++;
      }
    }
    if (compare_count > 0) {
      bonus +=
          (static_cast<float>(consistency_matches) / static_cast<float>(compare_count)) * 0.05f;
    }
  }

  return bonus;
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
