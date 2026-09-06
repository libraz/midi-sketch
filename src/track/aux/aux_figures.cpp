/**
 * @file aux_figures.cpp
 * @brief Implementation of the aux functions that depend on nothing but their arguments.
 */

#include <algorithm>
#include <cmath>

#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/note_creator.h"
#include "core/note_timeline_utils.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "core/velocity_helper.h"
#include "track/aux/aux_pitch.h"
#include "track/generators/aux.h"

namespace midisketch {

namespace {

/// Notes starting this close to chord change are treated as "anticipations" (1/16 beat = 120)
constexpr Tick kAnticipationThreshold = 120;

/// @brief Place a chord tone inside an aux voice's own range.
///
/// Folding a pitch class into an octave below the range and then clamping puts
/// every voice on the range floor, which turns a pad's chord into one repeated
/// pitch. Fold into the range instead so the voices stay distinct.
///
/// @param pitch_class Chord tone as a pitch class (0-11).
/// @param low Lowest pitch this aux voice may use.
/// @param high Highest pitch this aux voice may use.
/// @return Pitch of @p pitch_class inside [low, high].
uint8_t placeInAuxRange(int pitch_class, uint8_t low, uint8_t high) {
  const int pitch = normalizeToOctave(pitch_class, static_cast<int>(low));
  return static_cast<uint8_t>(
      std::clamp(pitch, static_cast<int>(low), static_cast<int>(std::max(low, high))));
}

/// Fewest voices a sustained pad may lay down. One held pitch is a drone, not a
/// pad: the function's whole job is to state the chord underneath the melody, so
/// density decides how full the voicing is rather than whether there is a chord.
constexpr int kMinPadVoices = 2;

/// @brief Voice count for a sustained pad at a given density.
///
/// AuxDensityBehavior::VoiceCount makes density_ratio the voicing width knob.
/// Truncating the product collapses every sparse aux profile onto the floor, so
/// the extra voices are rounded rather than cut and counted upward from
/// kMinPadVoices.
///
/// @param density Effective density (density_ratio x base_density).
/// @param max_voices Widest voicing this pad function uses.
/// @return Voice count within [kMinPadVoices, max_voices].
int padVoiceCount(float density, int max_voices) {
  const int span = std::max(0, max_voices - kMinPadVoices);
  const float weight = std::clamp(density, 0.0f, 1.0f);
  const int extra = static_cast<int>(std::lround(weight * static_cast<float>(span)));
  return std::clamp(kMinPadVoices + extra, kMinPadVoices, std::max(kMinPadVoices, max_voices));
}

}  // namespace

std::vector<NoteEvent> AuxGenerator::generateTargetHint(const AuxContext& ctx,
                                                        const AuxConfig& config,
                                                        const IHarmonyContext& harmony,
                                                        std::mt19937& rng) {
  std::vector<NoteEvent> result;

  if (!ctx.main_melody || ctx.main_melody->empty()) return result;

  // A1: Get function meta
  const auto& meta = getAuxFunctionMeta(AuxFunction::TargetHint);

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  uint8_t velocity = vel::scale(ctx.base_velocity, config.velocity_ratio);

  // A4: Use phrase boundaries from vocal if available
  std::vector<Tick> phrase_ends;
  if (ctx.phrase_boundaries && !ctx.phrase_boundaries->empty()) {
    // Use vocal's phrase boundaries for coordination
    for (const auto& boundary : *ctx.phrase_boundaries) {
      if (boundary.is_breath && boundary.tick > ctx.section_start &&
          boundary.tick <= ctx.section_end) {
        phrase_ends.push_back(boundary.tick);
      }
    }
  } else {
    // Fallback: Find phrase boundaries in main melody (gaps > quarter note)
    for (size_t i = 0; i + 1 < ctx.main_melody->size(); ++i) {
      const auto& note = (*ctx.main_melody)[i];
      const auto& next = (*ctx.main_melody)[i + 1];
      Tick gap = next.start_tick - (note.start_tick + note.duration);
      if (gap > TICK_QUARTER) {
        phrase_ends.push_back(note.start_tick + note.duration);
      }
    }
  }

  // Add hints before phrase ends
  for (Tick phrase_end : phrase_ends) {
    // A2: Apply density ratio (EventProbability behavior)
    if (!rng_util::rollProbability(rng, config.density_ratio * meta.base_density)) continue;

    // Play hint note half a bar before phrase end
    Tick hint_start = phrase_end - TICK_HALF;
    if (hint_start < ctx.section_start) continue;

    // Get a chord tone from the harmony active where the hint actually lands.
    ChordTones ct = harmony.getChordTonesAt(hint_start);
    if (ct.count == 0) continue;

    int pc = ct.pitch_classes[rng_util::rollRange(rng, 0, ct.count - 1)];
    if (pc < 0) continue;

    int octave = (aux_low + aux_high) / 2 / 12;
    uint8_t pitch = static_cast<uint8_t>(octave * 12 + pc);
    pitch = std::clamp(pitch, aux_low, aux_high);

    // A7: Use function-specific dissonance tolerance
    pitch = resolveAuxPitch(pitch, hint_start, TICK_QUARTER, ctx.main_melody, harmony, aux_low,
                            aux_high, meta.dissonance_tolerance);

    result.push_back({hint_start, TICK_QUARTER, pitch, velocity});
  }

  return result;
}

std::vector<NoteEvent> AuxGenerator::generatePhraseTail(const AuxContext& ctx,
                                                        const AuxConfig& config,
                                                        const IHarmonyContext& harmony,
                                                        std::mt19937& rng) {
  std::vector<NoteEvent> result;

  if (!ctx.main_melody || ctx.main_melody->empty()) return result;

  // A1: Get function meta
  const auto& meta = getAuxFunctionMeta(AuxFunction::PhraseTail);

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  uint8_t velocity = vel::scale(ctx.base_velocity, config.velocity_ratio);

  // A4: Use phrase boundaries from vocal if available
  std::vector<std::pair<Tick, uint8_t>> phrase_info;  // (end_tick, last_pitch)
  if (ctx.phrase_boundaries && !ctx.phrase_boundaries->empty()) {
    // Use vocal's phrase boundaries for tail placement
    for (const auto& boundary : *ctx.phrase_boundaries) {
      if (boundary.is_breath && boundary.tick >= ctx.section_start &&
          boundary.tick < ctx.section_end) {
        // Find the last melody note before this boundary
        uint8_t last_pitch = 60;  // Default
        for (const auto& note : *ctx.main_melody) {
          Tick note_end = note.start_tick + note.duration;
          if (note_end <= boundary.tick && note_end > boundary.tick - TICKS_PER_BAR) {
            last_pitch = note.note;
          }
        }
        phrase_info.push_back({boundary.tick, last_pitch});
      }
    }
  }

  // Fallback: Find phrase endings in main melody
  if (phrase_info.empty()) {
    for (size_t i = 0; i < ctx.main_melody->size(); ++i) {
      const auto& note = (*ctx.main_melody)[i];
      Tick note_end = note.start_tick + note.duration;

      bool is_phrase_end = false;
      if (i == ctx.main_melody->size() - 1) {
        is_phrase_end = true;
      } else {
        const auto& next = (*ctx.main_melody)[i + 1];
        Tick gap = next.start_tick - note_end;
        is_phrase_end = (gap > TICK_QUARTER);
      }

      if (is_phrase_end) {
        phrase_info.push_back({note_end, note.note});
      }
    }
  }

  // Generate tail notes
  for (const auto& [phrase_end, last_pitch] : phrase_info) {
    // A2: Apply density ratio (SkipRatio behavior)
    if (!rng_util::rollProbability(rng, config.density_ratio * meta.base_density)) continue;

    // Add tail note after phrase ending
    Tick tail_start = phrase_end + TICK_EIGHTH;
    if (tail_start >= ctx.section_end) continue;

    // Use a note below the phrase ending
    int tail_pitch = last_pitch - 2;  // Step down
    tail_pitch = snapToNearestScaleTone(tail_pitch, ctx.key_offset);
    tail_pitch = std::clamp(tail_pitch, static_cast<int>(aux_low), static_cast<int>(aux_high));

    // A7: Use function-specific dissonance tolerance (moderate for tails)
    uint8_t pitch =
        resolveAuxPitch(static_cast<uint8_t>(tail_pitch), tail_start, TICK_EIGHTH, ctx.main_melody,
                        harmony, aux_low, aux_high, meta.dissonance_tolerance);

    result.push_back({tail_start, TICK_EIGHTH, pitch, static_cast<uint8_t>(velocity * 0.8f)});
  }

  return result;
}

std::vector<NoteEvent> AuxGenerator::generateEmotionalPad(const AuxContext& ctx,
                                                          const AuxConfig& config,
                                                          const IHarmonyContext& harmony,
                                                          std::mt19937& rng) {
  std::vector<NoteEvent> result;

  // A1: Get function meta
  const auto& meta = getAuxFunctionMeta(AuxFunction::EmotionalPad);

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  uint8_t velocity = vel::scale(ctx.base_velocity, config.velocity_ratio);

  // Get chord tones for sustained pad
  ChordTones ct = harmony.getChordTonesAt(ctx.section_start);
  if (ct.count < 2) return result;

  // Create sustained tones on root and fifth
  int root_pc = ct.pitch_classes[0];
  int fifth_pc = (ct.count >= 3) ? ct.pitch_classes[2] : ct.pitch_classes[1];

  uint8_t root_pitch = placeInAuxRange(root_pc, aux_low, aux_high);
  uint8_t fifth_pitch = placeInAuxRange(fifth_pc, aux_low, aux_high);

  // Place sustained tones - check safety per bar to avoid clashes
  // with melody changes during long sustain
  Tick pad_duration = TICKS_PER_BAR;  // Check per bar instead of 2 bars
  Tick current_tick = ctx.section_start;

  // A2: VoiceCount behavior - root and fifth, with a tension voice when dense
  const int voice_count = padVoiceCount(config.density_ratio * meta.base_density, 3);

  while (current_tick < ctx.section_end) {
    Tick actual_duration = std::min(pad_duration, ctx.section_end - current_tick);

    // Update chord tones for current position (may change mid-section)
    ChordTones current_ct = harmony.getChordTonesAt(current_tick);
    if (current_ct.count >= 2) {
      root_pc = current_ct.pitch_classes[0];
      fifth_pc =
          (current_ct.count >= 3) ? current_ct.pitch_classes[2] : current_ct.pitch_classes[1];
      root_pitch = placeInAuxRange(root_pc, aux_low, aux_high);
      fifth_pitch = placeInAuxRange(fifth_pc, aux_low, aux_high);
    }

    // A6: Check if this is near section end for tension notes
    bool is_section_ending = (ctx.section_end - current_tick <= TICKS_PER_BAR * 2);

    // Root note (always)
    uint8_t safe_root = resolveAuxPitch(root_pitch, current_tick, actual_duration, ctx.main_melody,
                                        harmony, aux_low, aux_high, meta.dissonance_tolerance);
    result.push_back({current_tick, actual_duration, safe_root, velocity});

    // Fifth note (if voice_count >= 2)
    if (voice_count >= 2 &&
        std::abs(static_cast<int>(fifth_pitch) - static_cast<int>(safe_root)) > 2) {
      uint8_t safe_fifth =
          resolveAuxPitch(fifth_pitch, current_tick, actual_duration, ctx.main_melody, harmony,
                          aux_low, aux_high, meta.dissonance_tolerance);
      if (safe_fifth != safe_root) {
        result.push_back(
            {current_tick, actual_duration, safe_fifth, static_cast<uint8_t>(velocity * 0.9f)});
      }
    }

    // A6: Add tension note (9th or sus4) at section ending
    if (is_section_ending) {
      // Same ordering rule as the pad fifth below: roll first, then decide, so
      // the voicing width does not move the random stream.
      if (rng_util::rollProbability(rng, 0.5f) && voice_count >= 3) {
        // Add 9th (2 semitones above root) or sus4 (5 semitones above root)
        int tension_pc =
            rng_util::rollProbability(rng, 0.5f) ? (root_pc + 2) % 12 : (root_pc + 5) % 12;
        uint8_t tension_pitch = placeInAuxRange(tension_pc, aux_low, aux_high);

        // Tension notes use higher dissonance tolerance
        uint8_t safe_tension = resolveAuxPitch(tension_pitch, current_tick, actual_duration,
                                               ctx.main_melody, harmony, aux_low, aux_high, 0.5f);
        if (safe_tension != safe_root && safe_tension != fifth_pitch) {
          // Softer than the voices it colours, but not so soft that it drops out
          // of the mix: an inaudible tension note is the same as no tension.
          result.push_back(
              {current_tick, actual_duration, safe_tension, static_cast<uint8_t>(velocity * 0.8f)});
        }
      }
    }

    current_tick += pad_duration;
  }

  return result;
}

// ============================================================================
// F: Unison - Doubles the main melody
// ============================================================================

std::vector<NoteEvent> AuxGenerator::generateUnison(const AuxContext& ctx, const AuxConfig& config,
                                                    [[maybe_unused]] const IHarmonyContext& harmony,
                                                    std::mt19937& rng) {
  std::vector<NoteEvent> result;
  if (!ctx.main_melody || ctx.main_melody->empty()) return result;

  // Check derivability: unison requires stable rhythm (contour less important)
  DerivabilityScore score = analyzeDerivability(*ctx.main_melody);
  // Unison only needs rhythm stability - contour doesn't matter for pitch doubling
  if (score.rhythm_stability < 0.5f) {
    // Rhythm too irregular for unison doubling
    return result;
  }

  for (const auto& note : *ctx.main_melody) {
    // Only process notes within section range
    if (note.start_tick < ctx.section_start || note.start_tick >= ctx.section_end) continue;

    NoteEvent unison = note;

    // Add slight timing offset for natural doubling feel (+-5-10 ticks)
    int offset = rng_util::rollRange(rng, 5, 10) * (rng_util::rollRange(rng, 0, 1) ? 1 : -1);
    unison.start_tick = static_cast<Tick>(
        std::max(static_cast<int>(ctx.section_start), static_cast<int>(note.start_tick) + offset));

    // Reduce velocity for background effect
    unison.velocity = vel::scale(note.velocity, config.velocity_ratio);

    result.push_back(unison);
  }

  return result;
}

// ============================================================================
// F+: Harmony - Creates harmony line based on main melody
// ============================================================================

std::vector<NoteEvent> AuxGenerator::generateHarmony(const AuxContext& ctx, const AuxConfig& config,
                                                     const IHarmonyContext& harmony,
                                                     HarmonyMode mode, std::mt19937& rng) {
  std::vector<NoteEvent> result;
  if (!ctx.main_melody || ctx.main_melody->empty()) return result;

  // Check derivability: harmony benefits from stable rhythm and reasonable contour
  DerivabilityScore score = analyzeDerivability(*ctx.main_melody);
  // Harmony needs rhythm stability and pitch simplicity (contour is nice-to-have)
  if (score.rhythm_stability < 0.5f || score.pitch_simplicity < 0.4f) {
    // Melody too complex for parallel harmony
    return result;
  }

  int note_count = 0;
  for (const auto& note : *ctx.main_melody) {
    // Only process notes within section range
    if (note.start_tick < ctx.section_start || note.start_tick >= ctx.section_end) continue;

    NoteEvent harm = note;

    // Determine harmony interval based on mode
    int interval = 0;
    switch (mode) {
      case HarmonyMode::UnisonOnly:
        interval = 0;
        break;
      case HarmonyMode::ThirdAbove:
        interval = 3;  // Minor 3rd (could be 4 for major 3rd)
        break;
      case HarmonyMode::ThirdBelow:
        interval = -3;
        break;
      case HarmonyMode::Alternating:
        // Alternate between unison and third above
        interval = (note_count % 2 == 0) ? 0 : 3;
        break;
    }

    // Add slight timing offset FIRST
    int offset = rng_util::rollRange(rng, 3, 8) * (rng_util::rollRange(rng, 0, 1) ? 1 : -1);
    harm.start_tick = static_cast<Tick>(
        std::max(static_cast<int>(ctx.section_start), static_cast<int>(note.start_tick) + offset));

    // Apply interval and snap to chord tone at the ACTUAL placement tick
    int new_pitch = note.note + interval;
    new_pitch = harmony.snapToNearestChordTone(new_pitch, harm.start_tick);

    // Clamp to reasonable range
    harm.note = static_cast<uint8_t>(
        std::clamp(new_pitch, static_cast<int>(AUX_LOW), static_cast<int>(AUX_HIGH)));

    // Reduce velocity
    harm.velocity = vel::scale(note.velocity, config.velocity_ratio);

    result.push_back(harm);
    ++note_count;
  }

  return result;
}

// ============================================================================
// G: MelodicHook - Creates memorable hook phrase
// ============================================================================

std::vector<NoteEvent> AuxGenerator::generateMelodicHook(const AuxContext& ctx,
                                                         const AuxConfig& config,
                                                         const IHarmonyContext& harmony,
                                                         std::mt19937& rng) {
  std::vector<NoteEvent> result;

  // Calculate aux range
  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  // Hook pattern: AAAB style (3 repeats + variation)
  // Each hook phrase is 2 bars (8 beats)
  constexpr Tick HOOK_PHRASE_TICKS = TICKS_PER_BAR * 2;

  // Simple hook motif: 4 notes per bar
  constexpr int NOTES_PER_BAR = 4;
  constexpr Tick NOTE_DURATION = TICKS_PER_BEAT;

  Tick current_tick = ctx.section_start;

  // Generate base hook pattern (first 2 bars)
  std::vector<NoteEvent> base_hook;
  ChordTones chord_tones = harmony.getChordTonesAt(ctx.section_start);
  int root_pc = chord_tones.pitch_classes[0];
  int third_pc = (chord_tones.count >= 2) ? chord_tones.pitch_classes[1] : root_pc;
  int fifth_pc = (chord_tones.count >= 3) ? chord_tones.pitch_classes[2] : root_pc;

  auto nearestPitchClassInRange = [](int target_pitch, int pitch_class, uint8_t low, uint8_t high) {
    int best_pitch = static_cast<int>(low);
    int best_distance = 128;
    for (int pitch = low; pitch <= high; ++pitch) {
      if (getPitchClass(pitch) != pitch_class) continue;
      int distance = std::abs(pitch - target_pitch);
      if (distance < best_distance) {
        best_pitch = pitch;
        best_distance = distance;
      }
    }
    return best_pitch;
  };

  int range_center = (aux_low + aux_high) / 2;
  int base_pitch = nearestPitchClassInRange(range_center, root_pc, aux_low, aux_high);

  auto intervalFromRoot = [root_pc](int pitch_class) { return (pitch_class - root_pc + 12) % 12; };

  // Simple melodic pattern: root, chord 3rd, chord 5th, chord 3rd.
  std::array<int, 4> intervals = {0, intervalFromRoot(third_pc), intervalFromRoot(fifth_pc),
                                  intervalFromRoot(third_pc)};

  const auto& meta = getAuxFunctionMeta(AuxFunction::MelodicHook);

  for (int i = 0; i < NOTES_PER_BAR * 2; ++i) {
    int pitch = base_pitch + intervals[i % 4];
    pitch = std::clamp(pitch, static_cast<int>(aux_low), static_cast<int>(aux_high));

    // Apply safety check to avoid clashes with vocal
    pitch = resolveAuxPitch(static_cast<uint8_t>(pitch), current_tick, NOTE_DURATION,
                            ctx.main_melody, harmony, aux_low, aux_high, meta.dissonance_tolerance);

    // Create hook note (pitch will be re-checked when placed)
    Tick note_duration = NOTE_DURATION - TICKS_PER_BEAT / 8;  // Slight gap
    auto note = createNoteWithoutHarmony(current_tick, note_duration, static_cast<uint8_t>(pitch),
                                         vel::scale(ctx.base_velocity, config.velocity_ratio));
#ifdef MIDISKETCH_NOTE_PROVENANCE
    note.prov_source = static_cast<uint8_t>(NoteSource::Aux);
    note.prov_lookup_tick = current_tick;
    note.prov_original_pitch = static_cast<uint8_t>(pitch);
#endif
    base_hook.push_back(note);
    current_tick += NOTE_DURATION;
  }

  // Repeat base hook with variations (AAAB pattern)
  Tick section_length = ctx.section_end - ctx.section_start;
  // Cover a trailing odd bar as well. The emitted notes below are clipped at
  // section_end, so rounding up fills the remainder without crossing the
  // section boundary.
  int phrases_needed =
      static_cast<int>((section_length + HOOK_PHRASE_TICKS - 1) / HOOK_PHRASE_TICKS);

  for (int phrase = 0; phrase < phrases_needed; ++phrase) {
    Tick phrase_start = ctx.section_start + phrase * HOOK_PHRASE_TICKS;

    int note_idx = -1;
    for (const auto& note : base_hook) {
      ++note_idx;
      int beat = note_idx % NOTES_PER_BAR;

      NoteEvent hook_note = note;
      hook_note.start_tick = phrase_start + (note.start_tick - ctx.section_start);

      // Per-bar rhythm variation: a verbatim quarter-note cell on every bar
      // measures as one repeated cell (repeat_cell_consistency 0.81 vs
      // reference <=0.52). Position-hashed variants rest beat 3 or split
      // beat 4 into 8ths, keeping the hook identity with cell variety.
      Tick bar_tick = hook_note.start_tick - (hook_note.start_tick % TICKS_PER_BAR);
      uint32_t h = static_cast<uint32_t>(bar_tick) * 2654435761u;
      int variant = static_cast<int>((h >> 16) % 4);
      if (variant == 2 && beat == 2) continue;  // rest on beat 3 (breath)
      bool split_last = (variant == 3 && beat == 3);
      if (split_last) {
        hook_note.duration = TICK_EIGHTH - TICKS_PER_BEAT / 16;  // staccato 8th
      }

      // Apply variation on the B phrase (every 4th phrase)
      if (phrase % 4 == 3) {
        int variation = rng_util::rollRange(rng, -2, 2);
        int new_pitch = hook_note.note + variation;
#ifdef MIDISKETCH_NOTE_PROVENANCE
        uint8_t old_pitch = hook_note.note;
#endif
        hook_note.note = static_cast<uint8_t>(
            std::clamp(new_pitch, static_cast<int>(aux_low), static_cast<int>(aux_high)));
#ifdef MIDISKETCH_NOTE_PROVENANCE
        if (old_pitch != hook_note.note) {
          hook_note.prov_original_pitch = old_pitch;
          // A deliberate variation of the hook shape, not an adaptation to
          // another part's motion; recording it as the latter sends whoever
          // reads the provenance looking for a vocal that never moved.
          hook_note.addTransformStep(TransformStepType::PatternOffset, old_pitch, hook_note.note, 0,
                                     0);
        }
#endif
      }

      // Skip if outside section
      if (hook_note.start_tick >= ctx.section_end) continue;

      // Re-check safety for repeated/varied notes
      hook_note.note =
          resolveAuxPitch(hook_note.note, hook_note.start_tick, hook_note.duration, ctx.main_melody,
                          harmony, aux_low, aux_high, meta.dissonance_tolerance);

      result.push_back(hook_note);

      // Second 8th of a split beat 4: repeat the hook pitch off the beat
      if (split_last) {
        NoteEvent echo = hook_note;
        echo.start_tick = hook_note.start_tick + TICK_EIGHTH;
        if (echo.start_tick < ctx.section_end) {
          echo.velocity = static_cast<uint8_t>(echo.velocity * 0.85f);
          result.push_back(echo);
        }
      }
    }
  }

  return result;
}

// ============================================================================
// H: MotifCounter - Counter melody derived from vocal
// ============================================================================

std::vector<NoteEvent> AuxGenerator::generateMotifCounter(const AuxContext& ctx,
                                                          const AuxConfig& config,
                                                          const IHarmonyContext& harmony,
                                                          const VocalAnalysis& vocal_analysis,
                                                          std::mt19937& rng) {
  std::vector<NoteEvent> result;

  if (!ctx.main_melody || ctx.main_melody->empty()) return result;

  // A1: Get function meta
  const auto& meta = getAuxFunctionMeta(AuxFunction::MotifCounter);

  // Calculate counter melody range (separated from vocal)
  // If vocal is in high register, use low register and vice versa
  uint8_t aux_low, aux_high;
  int vocal_center = (vocal_analysis.lowest_pitch + vocal_analysis.highest_pitch) / 2;

  if (vocal_center >= 72) {  // Vocal is high (C5+)
    // Place counter in lower register
    aux_low = 48;                   // C3
    aux_high = 67;                  // G4
  } else if (vocal_center <= 60) {  // Vocal is low (C4-)
    // Place counter in a separate higher register.
    aux_low = 72;   // C5
    aux_high = 84;  // C6
  } else {
    // Vocal is in middle, use config offset
    calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);
    // Ensure separation: shift if overlapping
    if (aux_low >= vocal_analysis.lowest_pitch - 12 &&
        aux_high <= vocal_analysis.highest_pitch + 12) {
      // Try going an octave lower
      if (aux_low > 48) {
        aux_low -= 12;
        aux_high -= 12;
      } else {
        aux_low += 12;
        aux_high += 12;
      }
    }
  }
  if (aux_low > aux_high) {
    std::swap(aux_low, aux_high);
  }

  uint8_t velocity = vel::scale(ctx.base_velocity, config.velocity_ratio);

  // Rhythmic complementation: Determine counter note density based on vocal density
  // Dense vocal → sparse counter, sparse vocal → dense counter
  Tick base_note_duration;
  if (vocal_analysis.density > 0.6f) {
    // Vocal is dense, use longer notes (sparse counter)
    base_note_duration = TICK_HALF;
  } else if (vocal_analysis.density < 0.3f) {
    // Vocal is sparse, use shorter notes (dense counter)
    base_note_duration = TICK_EIGHTH;
  } else {
    // Medium density, use quarter notes
    base_note_duration = TICK_QUARTER;
  }

  // Iterate through vocal phrases to create counter phrases
  for (const auto& phrase : vocal_analysis.phrases) {
    Tick phrase_start = phrase.start_tick;
    Tick phrase_end = phrase.end_tick;

    // Skip if phrase is outside section
    if (phrase_end <= ctx.section_start || phrase_start >= ctx.section_end) {
      continue;
    }

    // Adjust to section boundaries
    phrase_start = std::max(phrase_start, ctx.section_start);
    phrase_end = std::min(phrase_end, ctx.section_end);

    // Generate counter notes for this phrase
    Tick current_tick = phrase_start;

    while (current_tick < phrase_end) {
      // Apply density ratio
      if (!rng_util::rollProbability(rng, config.density_ratio * meta.base_density)) {
        current_tick += base_note_duration;
        continue;
      }

      // Get vocal direction at this tick for contrary motion
      // getVocalDirectionAt returns: -1=descending, 0=static, 1=ascending
      int8_t vocal_direction = getVocalDirectionAt(vocal_analysis, current_tick);
      int vocal_pitch = getVocalPitchAt(vocal_analysis, current_tick);

      // Get chord tones at current tick (not section start)
      ChordTones ct = harmony.getChordTonesAt(current_tick);

      // Determine counter pitch using contrary motion
      int counter_pitch;

      if (vocal_pitch > 0 && ct.count > 0) {
        // Calculate target based on contrary motion
        int target_pitch = (aux_low + aux_high) / 2;

        if (vocal_direction > 0) {
          // Vocal going up → counter goes down
          target_pitch = aux_low + (aux_high - aux_low) / 3;
        } else if (vocal_direction < 0) {
          // Vocal going down → counter goes up
          target_pitch = aux_high - (aux_high - aux_low) / 3;
        }
        // vocal_direction == 0: static → use middle register

        // Snap to nearest chord tone at current tick
        counter_pitch = harmony.snapToNearestChordTone(target_pitch, current_tick);
        counter_pitch =
            std::clamp(counter_pitch, static_cast<int>(aux_low), static_cast<int>(aux_high));
      } else {
        // Fallback: use middle of range on chord tone
        counter_pitch = harmony.snapToNearestChordTone((aux_low + aux_high) / 2, current_tick);
      }

      // Get safe pitch (avoid collisions)
      Tick note_duration = std::min(base_note_duration, phrase_end - current_tick);

      // Check for chord change during this note (anticipation handling)
      // If note starts close to chord change and extends past it, use new chord's tones
      Tick next_chord_change = harmony.getNextChordChangeTick(current_tick);

      if (next_chord_change > 0 && next_chord_change > current_tick &&
          next_chord_change < current_tick + note_duration &&
          next_chord_change - current_tick < kAnticipationThreshold) {
        // This note anticipates the next chord - use new chord's tones
        counter_pitch = harmony.snapToNearestChordTone(counter_pitch, next_chord_change);
        counter_pitch =
            std::clamp(counter_pitch, static_cast<int>(aux_low), static_cast<int>(aux_high));
      }

      uint8_t safe_pitch =
          resolveAuxPitch(static_cast<uint8_t>(counter_pitch), current_tick, note_duration,
                          ctx.main_melody, harmony, aux_low, aux_high, meta.dissonance_tolerance);

      // Add note
      result.push_back({current_tick, note_duration, safe_pitch, velocity});

      current_tick += base_note_duration;
    }
  }

  // If no phrases were found, generate based on rest positions
  if (result.empty() && !vocal_analysis.rest_positions.empty()) {
    // Play during vocal rests (call-and-response style)
    for (const Tick& rest_start : vocal_analysis.rest_positions) {
      if (rest_start < ctx.section_start || rest_start >= ctx.section_end) {
        continue;
      }

      // Apply density
      if (!rng_util::rollProbability(rng, config.density_ratio)) {
        continue;
      }

      // Get chord tone for this position
      int counter_pitch = harmony.snapToNearestChordTone((aux_low + aux_high) / 2, rest_start);
      counter_pitch =
          std::clamp(counter_pitch, static_cast<int>(aux_low), static_cast<int>(aux_high));

      uint8_t safe_pitch =
          resolveAuxPitch(static_cast<uint8_t>(counter_pitch), rest_start, TICK_QUARTER,
                          ctx.main_melody, harmony, aux_low, aux_high, meta.dissonance_tolerance);

      result.push_back({rest_start, TICK_QUARTER, safe_pitch, velocity});
    }
  }

  return result;
}

// ============================================================================
// I: Sustain Pad - Whole-note chord tone pads for Ballad/Sentimental
// ============================================================================

std::vector<NoteEvent> AuxGenerator::generateSustainPad(const AuxContext& ctx,
                                                        const AuxConfig& config,
                                                        const IHarmonyContext& harmony,
                                                        std::mt19937& rng) {
  std::vector<NoteEvent> result;

  // A1: Get function meta
  const auto& meta = getAuxFunctionMeta(AuxFunction::SustainPad);

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  uint8_t velocity = vel::scale(ctx.base_velocity, config.velocity_ratio);

  // SustainPad generates whole-note (4 beats) chord tone pads
  // Softer and more sustained than EmotionalPad
  constexpr Tick PAD_DURATION = TICKS_PER_BAR;  // One whole note per bar

  Tick current_tick = ctx.section_start;

  // Voice count: root and third, with a fifth on top when density allows
  const int voice_count = padVoiceCount(config.density_ratio * meta.base_density, 3);

  while (current_tick < ctx.section_end) {
    Tick actual_duration = std::min(PAD_DURATION, ctx.section_end - current_tick);

    // Get the chord tones sounding in this bar
    ChordTones current_ct = harmony.getChordTonesAt(current_tick);

    if (current_ct.count < 1) {
      current_tick += PAD_DURATION;
      continue;
    }

    // Get root and third for warm pad voicing
    int root_pc = current_ct.pitch_classes[0];
    int third_pc = (current_ct.count >= 2) ? current_ct.pitch_classes[1] : root_pc;

    uint8_t root_pitch = placeInAuxRange(root_pc, aux_low, aux_high);
    uint8_t third_pitch = placeInAuxRange(third_pc, aux_low, aux_high);

    // Root note (always play)
    uint8_t safe_root = resolveAuxPitch(root_pitch, current_tick, actual_duration, ctx.main_melody,
                                        harmony, aux_low, aux_high, meta.dissonance_tolerance);

    // Softer velocity for sustained pad effect
    uint8_t pad_velocity = static_cast<uint8_t>(velocity * 0.7f);
    result.push_back({current_tick, actual_duration, safe_root, pad_velocity});

    // Third note (if voice_count >= 2 and not too close to root)
    if (voice_count >= 2 &&
        std::abs(static_cast<int>(third_pitch) - static_cast<int>(safe_root)) > 2) {
      uint8_t safe_third =
          resolveAuxPitch(third_pitch, current_tick, actual_duration, ctx.main_melody, harmony,
                          aux_low, aux_high, meta.dissonance_tolerance);
      if (safe_third != safe_root) {
        result.push_back({current_tick, actual_duration, safe_third,
                          static_cast<uint8_t>(pad_velocity * 0.85f)});
      }
    }

    // Optional: Add subtle variation every other bar
    // The roll is taken first so that a pad's voicing width does not change how
    // much of the random stream this bar consumes.
    if (rng_util::rollProbability(rng, 0.3f) && voice_count >= 3) {
      // Occasionally add fifth for richer texture
      int fifth_pc = (current_ct.count >= 3) ? current_ct.pitch_classes[2] : root_pc;
      uint8_t fifth_pitch = placeInAuxRange(fifth_pc, aux_low, aux_high);

      if (fifth_pitch != safe_root && fifth_pitch != third_pitch) {
        uint8_t safe_fifth =
            resolveAuxPitch(fifth_pitch, current_tick, actual_duration, ctx.main_melody, harmony,
                            aux_low, aux_high, meta.dissonance_tolerance);
        result.push_back({current_tick, actual_duration, safe_fifth,
                          static_cast<uint8_t>(pad_velocity * 0.75f)});
      }
    }

    current_tick += PAD_DURATION;
  }

  return result;
}

}  // namespace midisketch
