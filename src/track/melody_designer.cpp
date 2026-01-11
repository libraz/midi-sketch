/**
 * @file melody_designer.cpp
 * @brief Implementation of MelodyDesigner track generation.
 */

#include "track/melody_designer.h"
#include "core/harmonic_rhythm.h"
#include "core/harmony_context.h"
#include "core/melody_embellishment.h"
#include "core/note_factory.h"
#include "core/timing_constants.h"
#include <algorithm>
#include <cmath>

namespace midisketch {

namespace {

// Default velocity for melody notes
constexpr uint8_t DEFAULT_VELOCITY = 100;

// Check if a tick position is on a strong beat (Beat 1 or Beat 3).
// Strong beats are critical for harmonic establishment in pop music.
// @param tick Absolute tick position
// @return true if on Beat 1 or Beat 3 of any bar
bool isStrongBeat(Tick tick) {
  Tick pos_in_bar = tick % TICKS_PER_BAR;
  // Beat 1: 0 to TICKS_PER_BEAT (with tolerance for slight offsets)
  // Beat 3: 2*TICKS_PER_BEAT to 3*TICKS_PER_BEAT
  constexpr Tick TOLERANCE = TICKS_PER_BEAT / 8;  // 1/32 note tolerance
  bool is_beat_1 = pos_in_bar < TOLERANCE;
  bool is_beat_3 = (pos_in_bar >= 2 * TICKS_PER_BEAT - TOLERANCE) &&
                   (pos_in_bar < 2 * TICKS_PER_BEAT + TOLERANCE);
  return is_beat_1 || is_beat_3;
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

// Check if a pitch creates an "avoid note" relationship with the chord root.
// Based on music theory: avoid notes are pitches that create:
// - Minor 9th (1 semitone) with chord tones
// - Tritone (6 semitones) with chord root (makes non-dominant chords sound dominant)
// @param pitch_pc Pitch class of the melody note (0-11)
// @param root_pc Pitch class of the chord root (0-11)
// @return true if the pitch should be avoided on strong beats
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
  if (chord_tones.empty()) return current_pitch;

  int best_pitch = current_pitch;
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

// Responsibility pitch classes for Chorus/B sections (C major scale)
// These create a consistent "anchor" feeling at phrase starts
constexpr int8_t RESPONSIBILITY_PCS[] = {0, 7, 9};  // I(C), V(G), vi(A)

// Get responsibility pitch for Chorus/B section phrase starts.
// This creates a consistent starting point that makes hooks memorable.
// @param chord_degree Current chord degree
// @param tessitura_center Center of comfortable singing range
// @param vocal_low Minimum pitch
// @param vocal_high Maximum pitch
// @returns A pitch that serves as a stable "anchor" for the phrase
int getResponsibilityPitch(int8_t chord_degree, int tessitura_center,
                           uint8_t vocal_low, uint8_t vocal_high) {
  // Select target pitch class based on chord (cycles through I, V, vi)
  int target_pc = RESPONSIBILITY_PCS[std::abs(chord_degree) % 3];
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

}  // namespace

std::vector<NoteEvent> MelodyDesigner::generateSection(
    const MelodyTemplate& tmpl,
    const SectionContext& ctx,
    const HarmonyContext& harmony,
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

    // Snap to next chord boundary (phrase_beats * TICKS_PER_BEAT grid)
    // This ensures each phrase starts at a chord change, preventing sustain issues
    Tick chord_interval = phrase_beats * TICKS_PER_BEAT;
    Tick relative_tick = current_tick - ctx.section_start;
    Tick next_boundary = ((relative_tick + chord_interval - 1) / chord_interval) * chord_interval;
    current_tick = ctx.section_start + next_boundary;
  }

  // Apply melodic embellishment (non-chord tones) if enabled
  if (ctx.enable_embellishment && !result.empty()) {
    EmbellishmentConfig emb_config = MelodicEmbellisher::getConfigForMood(ctx.mood);
    result = MelodicEmbellisher::embellish(result, emb_config, harmony, ctx.key_offset, rng);
  }

  return result;
}

std::vector<NoteEvent> MelodyDesigner::generateSectionWithEvaluation(
    const MelodyTemplate& tmpl,
    const SectionContext& ctx,
    const HarmonyContext& harmony,
    std::mt19937& rng,
    VocalStylePreset vocal_style,
    int candidate_count) {

  // Get evaluation config for the vocal style
  const EvaluatorConfig& config = MelodyEvaluator::getEvaluatorConfig(vocal_style);

  // Generate multiple candidates
  std::vector<std::pair<std::vector<NoteEvent>, float>> candidates;
  candidates.reserve(static_cast<size_t>(candidate_count));

  for (int i = 0; i < candidate_count; ++i) {
    // Generate a candidate melody
    std::vector<NoteEvent> melody = generateSection(tmpl, ctx, harmony, rng);

    // Evaluate it
    MelodyScore score = MelodyEvaluator::evaluate(melody, harmony);
    float total_score = score.total(config);

    candidates.emplace_back(std::move(melody), total_score);
  }

  // Find the best candidate
  auto best_it = std::max_element(
      candidates.begin(), candidates.end(),
      [](const auto& a, const auto& b) { return a.second < b.second; });

  return std::move(best_it->first);
}

MelodyDesigner::PhraseResult MelodyDesigner::generateMelodyPhrase(
    const MelodyTemplate& tmpl,
    Tick phrase_start,
    uint8_t phrase_beats,
    const SectionContext& ctx,
    int prev_pitch,
    int direction_inertia,
    const HarmonyContext& harmony,
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
    // For Chorus/B sections, use responsibility pitch for memorable anchoring
    if (ctx.section_type == SectionType::Chorus || ctx.section_type == SectionType::B) {
      current_pitch = getResponsibilityPitch(start_chord_degree, ctx.tessitura.center,
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

    // Apply consecutive same note reduction (move to different chord tone)
    if (new_pitch == current_pitch && ctx.consecutive_same_note_prob < 1.0f) {
      std::uniform_real_distribution<float> same_dist(0.0f, 1.0f);
      if (same_dist(rng) > ctx.consecutive_same_note_prob) {
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
        }
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

    // Avoid note check: melody should not form tritone/minor2nd with bass root
    // This applies to ALL notes, not just strong beats, because bass establishes
    // the harmonic foundation throughout the bar.
    // Based on pop music theory: the bass root defines the chord's identity.
    {
      int bass_root_pc = getBassRootPitchClass(note_chord_degree);
      int pitch_pc = new_pitch % 12;
      if (isAvoidNoteWithRoot(pitch_pc, bass_root_pc)) {
        // Adjust to nearest safe chord tone
        new_pitch = getNearestSafeChordTone(new_pitch, note_chord_degree, bass_root_pc,
                                             ctx.vocal_low, ctx.vocal_high);
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

    // Apply gate for phrase ending
    bool is_phrase_end = (i == rhythm.size() - 1);
    float gate = is_phrase_end ? tmpl.phrase_end_resolution * 0.8f : 0.9f;
    note_duration = static_cast<Tick>(note_duration * gate);

    // Clamp note duration to phrase boundary (prevents sustain over chord change)
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

    // Add note with provenance tracking
    result.notes.push_back(factory.create(
        note_start, note_duration, static_cast<uint8_t>(new_pitch), velocity,
        NoteSource::MelodyPhrase));

    current_pitch = new_pitch;
  }

  result.last_pitch = current_pitch;
  return result;
}

MelodyDesigner::PhraseResult MelodyDesigner::generateHook(
    const MelodyTemplate& tmpl,
    Tick hook_start,
    const SectionContext& ctx,
    int prev_pitch,
    const HarmonyContext& harmony,
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

  // Song-level hook fixation: generate and cache hook motif once
  // "Variation is the enemy, Exact is justice" - use the same hook throughout the song
  if (!cached_chorus_hook_.has_value()) {
    StyleMelodyParams hook_params{};
    hook_params.hook_repetition = true;  // Use catchy repetitive style
    cached_chorus_hook_ = designChorusHook(hook_params, rng);
  }

  const Motif& hook = *cached_chorus_hook_;

  // Use template settings for timing control
  uint8_t repeat_count = std::clamp(tmpl.hook_repeat_count,
                                    static_cast<uint8_t>(2),
                                    static_cast<uint8_t>(4));

  // Calculate timing for hook notes
  // Use quarter notes for hooks to maintain singability and avoid overlaps
  Tick note_duration = TICK_QUARTER;  // Quarter notes for catchy hooks
  if (tmpl.rhythm_driven && tmpl.sixteenth_density > 0.3f) {
    note_duration = TICK_EIGHTH;  // Eighth notes for rhythm-driven styles
  }

  Tick current_tick = hook_start;
  constexpr int MAX_INTERVAL = 9;  // Major 6th - singable leap limit

  // Generate hook notes with chord-aware pitch selection
  // Rebuild hook pitches for each repetition to follow chord changes
  int prev_hook_pitch = base_pitch;
  size_t contour_limit = std::min(hook.contour_degrees.size(), static_cast<size_t>(3));

  for (uint8_t rep = 0; rep < repeat_count; ++rep) {
    for (size_t i = 0; i < contour_limit; ++i) {
      // Get chord at this note's position
      int8_t note_chord_degree = harmony.getChordDegreeAt(current_tick);

      // Calculate pitch from contour, then snap to current chord
      int pitch = base_pitch + hook.contour_degrees[i];

      // Find nearest chord tone within vocal range and interval constraint
      // This ensures the pitch is both a chord tone AND within bounds
      pitch = nearestChordToneWithinInterval(
          pitch, prev_hook_pitch, note_chord_degree, MAX_INTERVAL,
          ctx.vocal_low, ctx.vocal_high, &ctx.tessitura);

      // Avoid note check: melody should not form tritone/minor2nd with bass root
      // This applies to ALL notes because bass establishes harmonic foundation.
      {
        int bass_root_pc = getBassRootPitchClass(note_chord_degree);
        int pitch_pc = pitch % 12;
        if (isAvoidNoteWithRoot(pitch_pc, bass_root_pc)) {
          pitch = getNearestSafeChordTone(pitch, note_chord_degree, bass_root_pc,
                                           ctx.vocal_low, ctx.vocal_high);
        }
      }

      uint8_t velocity = DEFAULT_VELOCITY;
      if (i == 0) velocity += 10;  // Accent first note of each repetition

      result.notes.push_back(factory.create(
          current_tick,
          static_cast<Tick>(note_duration * 0.85f),  // Gate with room for humanize
          static_cast<uint8_t>(pitch),
          velocity,
          NoteSource::Hook));

      prev_hook_pitch = pitch;
      current_tick += note_duration;
    }

    // Quarter note gap between repetitions for breathing
    current_tick += TICK_QUARTER;
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
    const HarmonyContext& harmony) {

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
    const HarmonyContext& harmony) {

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
    [[maybe_unused]] int key_offset,
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
    const HarmonyContext& harmony,
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

  // Use smaller margin when 32nd notes are enabled, otherwise keep original
  float end_margin = (thirtysecond_ratio > 0.0f) ? 0.125f : 0.25f;

  while (current_beat < end_beat - end_margin) {
    // Determine note duration (in eighths, float to support 32nds)
    float eighths;
    if (thirtysecond_ratio > 0.0f && dist(rng) < thirtysecond_ratio) {
      eighths = 0.5f;  // 32nd note (0.25 eighth = 0.125 beats)
    } else if (tmpl.rhythm_driven && dist(rng) < effective_sixteenth_density) {
      eighths = 1.0f;  // 16th note (0.5 eighth)
    } else if (dist(rng) < tmpl.long_note_ratio) {
      eighths = 4.0f;  // Half note
    } else {
      eighths = 2.0f;  // Quarter note (most common)
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

  return rhythm;
}

}  // namespace midisketch
