#include "track/melody_designer.h"
#include "core/harmony_context.h"
#include <algorithm>
#include <cmath>

namespace midisketch {

namespace {

// Duration in ticks for different note values
constexpr Tick TICK_SIXTEENTH = TICKS_PER_BEAT / 4;  // 120
constexpr Tick TICK_EIGHTH = TICKS_PER_BEAT / 2;     // 240
constexpr Tick TICK_QUARTER = TICKS_PER_BEAT;        // 480

// Default velocity for melody notes
constexpr uint8_t DEFAULT_VELOCITY = 100;

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

  // Calculate phrase structure
  uint8_t phrase_beats = tmpl.max_phrase_beats;
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

    // Append notes to result
    for (const auto& note : phrase_result.notes) {
      result.push_back(note);
    }

    // Update state for next phrase
    prev_pitch = phrase_result.last_pitch;
    direction_inertia = phrase_result.direction_inertia;

    // Move to next phrase position
    current_tick += actual_beats * TICKS_PER_BEAT;

    // Add rest between phrases (breathing)
    if (i < phrase_count - 1) {
      current_tick += TICK_EIGHTH;  // Short breath
    }
  }

  return result;
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

  // Generate rhythm pattern with section density modifier
  std::vector<RhythmNote> rhythm = generatePhraseRhythm(tmpl, phrase_beats, ctx.density_modifier, rng);

  // Calculate initial pitch if none provided
  int current_pitch;
  if (prev_pitch < 0) {
    // Start near tessitura center
    current_pitch = ctx.tessitura.center;
    // Adjust to chord tone
    current_pitch = nearestChordTonePitch(current_pitch, ctx.chord_degree);
    current_pitch = std::clamp(current_pitch,
                               static_cast<int>(ctx.vocal_low),
                               static_cast<int>(ctx.vocal_high));
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

    // Select pitch movement
    PitchChoice choice = selectPitchChoice(tmpl, phrase_pos,
                                           target_pitch >= 0, rng);

    // Apply direction inertia
    choice = applyDirectionInertia(choice, result.direction_inertia, tmpl, rng);

    // Check vowel section constraint
    if (tmpl.vowel_constraint && i > 0) {
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

    // Apply pitch choice
    int new_pitch = applyPitchChoice(choice, current_pitch, target_pitch,
                                     ctx.chord_degree, ctx.key_offset,
                                     ctx.vocal_low, ctx.vocal_high);

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

    // Calculate note timing
    Tick note_start = phrase_start + static_cast<Tick>(rn.beat * TICKS_PER_BEAT);
    Tick note_duration = rn.eighths * TICK_EIGHTH;

    // Apply gate for phrase ending
    bool is_phrase_end = (i == rhythm.size() - 1);
    float gate = is_phrase_end ? tmpl.phrase_end_resolution * 0.8f : 0.9f;
    note_duration = static_cast<Tick>(note_duration * gate);

    // Calculate velocity
    uint8_t velocity = DEFAULT_VELOCITY;
    if (rn.strong) {
      velocity = std::min(127, velocity + 10);
    }
    if (is_phrase_end) {
      velocity = static_cast<uint8_t>(velocity * 0.85f);
    }

    // Add note
    result.notes.push_back({note_start, note_duration,
                            static_cast<uint8_t>(new_pitch), velocity});

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

  // Initialize pitch
  int current_pitch;
  if (prev_pitch < 0) {
    current_pitch = ctx.tessitura.center;
    current_pitch = nearestChordTonePitch(current_pitch, ctx.chord_degree);
  } else {
    current_pitch = prev_pitch;
  }

  // Generate hook pattern based on template settings
  uint8_t note_count = std::clamp(tmpl.hook_note_count,
                                  static_cast<uint8_t>(2),
                                  static_cast<uint8_t>(4));
  uint8_t repeat_count = std::clamp(tmpl.hook_repeat_count,
                                    static_cast<uint8_t>(2),
                                    static_cast<uint8_t>(4));

  // Create hook motif pitches
  std::vector<int> hook_pitches;
  hook_pitches.push_back(current_pitch);

  std::uniform_int_distribution<int> step_dist(-2, 2);
  for (uint8_t i = 1; i < note_count; ++i) {
    int step = step_dist(rng);
    if (step == 0) step = 1;  // Avoid staying same in hook
    int new_pitch = current_pitch + step;
    new_pitch = snapToNearestScaleTone(new_pitch, ctx.key_offset);
    new_pitch = std::clamp(new_pitch,
                           static_cast<int>(ctx.vocal_low),
                           static_cast<int>(ctx.vocal_high));
    hook_pitches.push_back(new_pitch);
  }

  // Calculate timing for hook notes
  Tick note_duration = TICK_EIGHTH;  // Quick notes for hooks
  if (tmpl.rhythm_driven && tmpl.sixteenth_density > 0.3f) {
    note_duration = TICK_SIXTEENTH;
  }

  Tick current_tick = hook_start;

  // Repeat the hook pattern
  for (uint8_t rep = 0; rep < repeat_count; ++rep) {
    for (size_t i = 0; i < hook_pitches.size(); ++i) {
      uint8_t velocity = DEFAULT_VELOCITY;
      if (i == 0) velocity += 10;  // Accent first note

      result.notes.push_back({
          current_tick,
          static_cast<Tick>(note_duration * 0.9f),  // Gate
          static_cast<uint8_t>(hook_pitches[i]),
          velocity
      });

      current_tick += note_duration;
    }

    // Small gap between repetitions
    current_tick += TICK_SIXTEENTH;
  }

  result.last_pitch = hook_pitches.back();
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
    const MelodyTemplate& tmpl,
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
    float pos1, float pos2, uint8_t phrase_length) {

  // Simple vowel section model: divide phrase into 2-beat sections
  constexpr float VOWEL_SECTION_BEATS = 2.0f;

  int section1 = static_cast<int>(pos1 / VOWEL_SECTION_BEATS);
  int section2 = static_cast<int>(pos2 / VOWEL_SECTION_BEATS);

  return section1 == section2;
}

int8_t MelodyDesigner::getMaxStepInVowelSection(bool in_same_vowel) {
  return in_same_vowel ? 2 : 4;
}

int MelodyDesigner::applyPitchChoice(
    PitchChoice choice,
    int current_pitch,
    int target_pitch,
    int8_t chord_degree,
    int key_offset,
    uint8_t vocal_low,
    uint8_t vocal_high) {

  int new_pitch = current_pitch;

  switch (choice) {
    case PitchChoice::Same:
      // Stay on same pitch
      break;

    case PitchChoice::StepUp:
      new_pitch = current_pitch + 2;  // Whole step up
      break;

    case PitchChoice::StepDown:
      new_pitch = current_pitch - 2;  // Whole step down
      break;

    case PitchChoice::TargetStep:
      if (target_pitch >= 0) {
        // Move toward target by 1-2 steps
        int diff = target_pitch - current_pitch;
        if (diff > 0) {
          new_pitch = current_pitch + std::min(diff, 4);
        } else if (diff < 0) {
          new_pitch = current_pitch + std::max(diff, -4);
        }
      }
      break;
  }

  // Snap to scale tone
  new_pitch = snapToNearestScaleTone(new_pitch, key_offset);

  // Clamp to vocal range
  new_pitch = std::clamp(new_pitch,
                         static_cast<int>(vocal_low),
                         static_cast<int>(vocal_high));

  return new_pitch;
}

int MelodyDesigner::calculateTargetPitch(
    const MelodyTemplate& tmpl,
    const SectionContext& ctx,
    int current_pitch,
    const HarmonyContext& harmony,
    std::mt19937& rng) {

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
    std::mt19937& rng) {

  std::vector<RhythmNote> rhythm;
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  float current_beat = 0.0f;
  float end_beat = static_cast<float>(phrase_beats);

  // Apply section density modifier to sixteenth density
  float effective_sixteenth_density = tmpl.sixteenth_density * density_modifier;
  // Clamp to valid range [0.0, 0.95]
  effective_sixteenth_density = std::min(effective_sixteenth_density, 0.95f);

  while (current_beat < end_beat - 0.25f) {
    // Determine note duration
    int eighths;
    if (tmpl.rhythm_driven && dist(rng) < effective_sixteenth_density) {
      eighths = 1;  // 16th note (0.5 eighth)
    } else if (dist(rng) < tmpl.long_note_ratio) {
      eighths = 4;  // Half note
    } else {
      eighths = 2;  // Quarter note (most common)
    }

    // Check if strong beat
    bool strong = (static_cast<int>(current_beat) % 2 == 0);

    rhythm.push_back({current_beat, eighths, strong});

    current_beat += eighths * 0.5f;  // Convert eighths to beats
  }

  return rhythm;
}

}  // namespace midisketch
