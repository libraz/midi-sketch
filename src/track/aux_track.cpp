#include "track/aux_track.h"
#include "core/chord_utils.h"
#include "core/harmony_context.h"
#include "core/timing_constants.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace midisketch {

// ============================================================================
// A1: AuxFunction Meta Information
// ============================================================================

namespace {

// Meta information table for each AuxFunction.
// Index matches AuxFunction enum value.
constexpr std::array<AuxFunctionMeta, 7> kAuxFunctionMetaTable = {{
    // PulseLoop: Rhythmic, ChordTone, EventProbability
    {AuxTimingRole::Rhythmic, AuxHarmonicRole::ChordTone,
     AuxDensityBehavior::EventProbability, 0.7f, 0.1f},
    // TargetHint: Reactive, Target, EventProbability
    {AuxTimingRole::Reactive, AuxHarmonicRole::Target,
     AuxDensityBehavior::EventProbability, 0.5f, 0.2f},
    // GrooveAccent: Rhythmic, Accent, EventProbability
    {AuxTimingRole::Rhythmic, AuxHarmonicRole::Accent,
     AuxDensityBehavior::EventProbability, 0.6f, 0.0f},
    // PhraseTail: Reactive, Following, SkipRatio
    {AuxTimingRole::Reactive, AuxHarmonicRole::Following,
     AuxDensityBehavior::SkipRatio, 0.4f, 0.3f},
    // EmotionalPad: Sustained, ChordTone, VoiceCount
    {AuxTimingRole::Sustained, AuxHarmonicRole::ChordTone,
     AuxDensityBehavior::VoiceCount, 1.0f, 0.4f},
    // Unison: Reactive, Unison, EventProbability (full density)
    {AuxTimingRole::Reactive, AuxHarmonicRole::Unison,
     AuxDensityBehavior::EventProbability, 1.0f, 0.0f},
    // MelodicHook: Rhythmic, ChordTone, EventProbability
    {AuxTimingRole::Rhythmic, AuxHarmonicRole::ChordTone,
     AuxDensityBehavior::EventProbability, 1.0f, 0.1f},
}};


// Check if two notes overlap in time
bool notesOverlap(Tick start1, Tick end1, Tick start2, Tick end2) {
  return start1 < end2 && start2 < end1;
}

}  // namespace

const AuxFunctionMeta& getAuxFunctionMeta(AuxFunction func) {
  size_t idx = static_cast<size_t>(func);
  if (idx < kAuxFunctionMetaTable.size()) {
    return kAuxFunctionMetaTable[idx];
  }
  // Default to PulseLoop meta if out of range
  return kAuxFunctionMetaTable[0];
}

MidiTrack AuxTrackGenerator::generate(
    const AuxConfig& config,
    const AuxContext& ctx,
    const HarmonyContext& harmony,
    std::mt19937& rng) {

  MidiTrack track;
  std::vector<NoteEvent> notes;

  switch (config.function) {
    case AuxFunction::PulseLoop:
      notes = generatePulseLoop(ctx, config, harmony, rng);
      break;
    case AuxFunction::TargetHint:
      notes = generateTargetHint(ctx, config, harmony, rng);
      break;
    case AuxFunction::GrooveAccent:
      notes = generateGrooveAccent(ctx, config, harmony, rng);
      break;
    case AuxFunction::PhraseTail:
      notes = generatePhraseTail(ctx, config, harmony, rng);
      break;
    case AuxFunction::EmotionalPad:
      notes = generateEmotionalPad(ctx, config, harmony, rng);
      break;
    case AuxFunction::Unison:
      notes = generateUnison(ctx, config, harmony, rng);
      break;
    case AuxFunction::MelodicHook:
      notes = generateMelodicHook(ctx, config, harmony, rng);
      break;
  }

  for (const auto& note : notes) {
    track.addNote(note.start_tick, note.duration, note.note, note.velocity);
  }

  return track;
}

std::vector<NoteEvent> AuxTrackGenerator::generatePulseLoop(
    const AuxContext& ctx,
    const AuxConfig& config,
    const HarmonyContext& harmony,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;

  // A1: Get function meta for dissonance tolerance
  const auto& meta = getAuxFunctionMeta(AuxFunction::PulseLoop);

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  // Get chord tones for the section
  ChordTones ct = getChordTones(ctx.chord_degree);
  if (ct.count == 0) return result;

  // Create a short repeating pattern (2-4 notes)
  std::uniform_int_distribution<int> pattern_len_dist(2, 4);
  int pattern_length = pattern_len_dist(rng);

  // Build pattern pitches from chord tones
  std::vector<uint8_t> pattern_pitches;
  int base_octave = aux_low / 12;

  for (int i = 0; i < pattern_length && i < static_cast<int>(ct.count); ++i) {
    int pc = ct.pitch_classes[i % ct.count];
    if (pc < 0) continue;
    uint8_t pitch = static_cast<uint8_t>(base_octave * 12 + pc);
    if (pitch >= aux_low && pitch <= aux_high) {
      pattern_pitches.push_back(pitch);
    }
  }

  if (pattern_pitches.empty()) return result;

  // Calculate velocity
  uint8_t velocity = static_cast<uint8_t>(ctx.base_velocity * config.velocity_ratio);

  // Repeat pattern throughout section
  Tick note_duration = TICK_EIGHTH;
  Tick current_tick = ctx.section_start;
  size_t pattern_idx = 0;

  while (current_tick < ctx.section_end) {
    // A2: Apply density ratio (EventProbability behavior)
    std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
    if (density_dist(rng) > config.density_ratio * meta.base_density) {
      current_tick += note_duration;
      continue;
    }

    uint8_t pitch = pattern_pitches[pattern_idx % pattern_pitches.size()];

    // A7: Check for collision with function-specific tolerance
    pitch = getSafePitch(pitch, current_tick, note_duration,
                         ctx.main_melody, harmony, aux_low, aux_high,
                         ctx.chord_degree, meta.dissonance_tolerance);

    result.push_back({current_tick, note_duration, pitch, velocity});

    current_tick += note_duration;
    pattern_idx++;
  }

  return result;
}

std::vector<NoteEvent> AuxTrackGenerator::generateTargetHint(
    const AuxContext& ctx,
    const AuxConfig& config,
    const HarmonyContext& harmony,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;

  if (!ctx.main_melody || ctx.main_melody->empty()) return result;

  // A1: Get function meta
  const auto& meta = getAuxFunctionMeta(AuxFunction::TargetHint);

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  uint8_t velocity = static_cast<uint8_t>(ctx.base_velocity * config.velocity_ratio);

  // A4: Use phrase boundaries from vocal if available
  std::vector<Tick> phrase_ends;
  if (ctx.phrase_boundaries && !ctx.phrase_boundaries->empty()) {
    // Use vocal's phrase boundaries for coordination
    for (const auto& boundary : *ctx.phrase_boundaries) {
      if (boundary.is_breath &&
          boundary.tick > ctx.section_start &&
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
    std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
    if (density_dist(rng) > config.density_ratio * meta.base_density) continue;

    // Play hint note half a bar before phrase end
    Tick hint_start = phrase_end - TICK_HALF;
    if (hint_start < ctx.section_start) continue;

    // Get a chord tone as hint
    ChordTones ct = getChordTones(ctx.chord_degree);
    if (ct.count == 0) continue;

    std::uniform_int_distribution<int> tone_dist(0, ct.count - 1);
    int pc = ct.pitch_classes[tone_dist(rng)];
    if (pc < 0) continue;

    int octave = (aux_low + aux_high) / 2 / 12;
    uint8_t pitch = static_cast<uint8_t>(octave * 12 + pc);
    pitch = std::clamp(pitch, aux_low, aux_high);

    // A7: Use function-specific dissonance tolerance
    pitch = getSafePitch(pitch, hint_start, TICK_QUARTER,
                         ctx.main_melody, harmony, aux_low, aux_high,
                         ctx.chord_degree, meta.dissonance_tolerance);

    result.push_back({hint_start, TICK_QUARTER, pitch, velocity});
  }

  return result;
}

std::vector<NoteEvent> AuxTrackGenerator::generateGrooveAccent(
    const AuxContext& ctx,
    const AuxConfig& config,
    const HarmonyContext& harmony,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;

  // A1: Get function meta
  const auto& meta = getAuxFunctionMeta(AuxFunction::GrooveAccent);

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  uint8_t velocity = static_cast<uint8_t>(ctx.base_velocity * config.velocity_ratio);

  // Get root of chord for accent
  ChordTones ct = getChordTones(ctx.chord_degree);
  if (ct.count == 0) return result;

  int root_pc = ct.pitch_classes[0];
  int octave = aux_low / 12;
  uint8_t root_pitch = static_cast<uint8_t>(octave * 12 + root_pc);
  root_pitch = std::clamp(root_pitch, aux_low, aux_high);

  // A5: Place accents on beat 2 and 4 (backbeat)
  // Future: Could vary based on VocalGrooveFeel from params
  Tick bar_length = TICKS_PER_BAR;
  Tick current_bar = (ctx.section_start / bar_length) * bar_length;

  while (current_bar < ctx.section_end) {
    // Beat 2
    Tick beat2 = current_bar + TICKS_PER_BEAT;
    if (beat2 >= ctx.section_start && beat2 < ctx.section_end) {
      // A2: Apply density ratio (EventProbability behavior)
      std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
      if (density_dist(rng) < config.density_ratio * meta.base_density) {
        // A7: Use function-specific dissonance tolerance (very low for accents)
        uint8_t pitch = getSafePitch(root_pitch, beat2, TICK_EIGHTH,
                                     ctx.main_melody, harmony, aux_low, aux_high,
                                     ctx.chord_degree, meta.dissonance_tolerance);
        result.push_back({beat2, TICK_EIGHTH, pitch, velocity});
      }
    }

    // Beat 4
    Tick beat4 = current_bar + TICKS_PER_BEAT * 3;
    if (beat4 >= ctx.section_start && beat4 < ctx.section_end) {
      std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
      if (density_dist(rng) < config.density_ratio * meta.base_density) {
        uint8_t pitch = getSafePitch(root_pitch, beat4, TICK_EIGHTH,
                                     ctx.main_melody, harmony, aux_low, aux_high,
                                     ctx.chord_degree, meta.dissonance_tolerance);
        result.push_back({beat4, TICK_EIGHTH, pitch, velocity});
      }
    }

    current_bar += bar_length;
  }

  return result;
}

std::vector<NoteEvent> AuxTrackGenerator::generatePhraseTail(
    const AuxContext& ctx,
    const AuxConfig& config,
    const HarmonyContext& harmony,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;

  if (!ctx.main_melody || ctx.main_melody->empty()) return result;

  // A1: Get function meta
  const auto& meta = getAuxFunctionMeta(AuxFunction::PhraseTail);

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  uint8_t velocity = static_cast<uint8_t>(ctx.base_velocity * config.velocity_ratio);

  // A4: Use phrase boundaries from vocal if available
  std::vector<std::pair<Tick, uint8_t>> phrase_info;  // (end_tick, last_pitch)
  if (ctx.phrase_boundaries && !ctx.phrase_boundaries->empty()) {
    // Use vocal's phrase boundaries for tail placement
    for (const auto& boundary : *ctx.phrase_boundaries) {
      if (boundary.is_breath &&
          boundary.tick >= ctx.section_start &&
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
    std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
    if (density_dist(rng) > config.density_ratio * meta.base_density) continue;

    // Add tail note after phrase ending
    Tick tail_start = phrase_end + TICK_EIGHTH;
    if (tail_start >= ctx.section_end) continue;

    // Use a note below the phrase ending
    int tail_pitch = last_pitch - 2;  // Step down
    tail_pitch = snapToNearestScaleTone(tail_pitch, ctx.key_offset);
    tail_pitch = std::clamp(tail_pitch, static_cast<int>(aux_low),
                            static_cast<int>(aux_high));

    // A7: Use function-specific dissonance tolerance (moderate for tails)
    uint8_t pitch = getSafePitch(static_cast<uint8_t>(tail_pitch),
                                 tail_start, TICK_EIGHTH,
                                 ctx.main_melody, harmony, aux_low, aux_high,
                                 ctx.chord_degree, meta.dissonance_tolerance);

    result.push_back({tail_start, TICK_EIGHTH, pitch,
                      static_cast<uint8_t>(velocity * 0.8f)});
  }

  return result;
}

std::vector<NoteEvent> AuxTrackGenerator::generateEmotionalPad(
    const AuxContext& ctx,
    const AuxConfig& config,
    const HarmonyContext& harmony,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;

  // A1: Get function meta
  const auto& meta = getAuxFunctionMeta(AuxFunction::EmotionalPad);

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  uint8_t velocity = static_cast<uint8_t>(ctx.base_velocity * config.velocity_ratio);

  // Get chord tones for sustained pad
  ChordTones ct = getChordTones(ctx.chord_degree);
  if (ct.count < 2) return result;

  // Create sustained tones on root and fifth
  int root_pc = ct.pitch_classes[0];
  int fifth_pc = (ct.count >= 3) ? ct.pitch_classes[2] : ct.pitch_classes[1];

  int octave = aux_low / 12;
  uint8_t root_pitch = static_cast<uint8_t>(octave * 12 + root_pc);
  uint8_t fifth_pitch = static_cast<uint8_t>(octave * 12 + fifth_pc);

  root_pitch = std::clamp(root_pitch, aux_low, aux_high);
  fifth_pitch = std::clamp(fifth_pitch, aux_low, aux_high);

  // Place long sustained tones every 2 bars
  Tick pad_duration = TICKS_PER_BAR * 2;
  Tick current_tick = ctx.section_start;

  // A2: VoiceCount behavior - calculate how many voices based on density
  int voice_count = static_cast<int>(2.0f * config.density_ratio * meta.base_density);
  voice_count = std::clamp(voice_count, 1, 3);

  while (current_tick < ctx.section_end) {
    Tick actual_duration = std::min(pad_duration, ctx.section_end - current_tick);

    // A6: Check if this is near section end for tension notes
    bool is_section_ending = (ctx.section_end - current_tick <= TICKS_PER_BAR * 2);

    // Root note (always)
    uint8_t safe_root = getSafePitch(root_pitch, current_tick, actual_duration,
                                     ctx.main_melody, harmony, aux_low, aux_high,
                                     ctx.chord_degree, meta.dissonance_tolerance);
    result.push_back({current_tick, actual_duration, safe_root, velocity});

    // Fifth note (if voice_count >= 2)
    if (voice_count >= 2 &&
        std::abs(static_cast<int>(fifth_pitch) - static_cast<int>(safe_root)) > 2) {
      uint8_t safe_fifth = getSafePitch(fifth_pitch, current_tick, actual_duration,
                                        ctx.main_melody, harmony, aux_low, aux_high,
                                        ctx.chord_degree, meta.dissonance_tolerance);
      if (safe_fifth != safe_root) {
        result.push_back({current_tick, actual_duration, safe_fifth,
                          static_cast<uint8_t>(velocity * 0.9f)});
      }
    }

    // A6: Add tension note (9th or sus4) at section ending
    if (is_section_ending && voice_count >= 2) {
      std::uniform_real_distribution<float> tension_dist(0.0f, 1.0f);
      if (tension_dist(rng) < 0.5f) {  // 50% chance of tension
        // Add 9th (2 semitones above root) or sus4 (5 semitones above root)
        int tension_pc = (tension_dist(rng) < 0.5f) ? (root_pc + 2) % 12 : (root_pc + 5) % 12;
        uint8_t tension_pitch = static_cast<uint8_t>(octave * 12 + tension_pc);
        tension_pitch = std::clamp(tension_pitch, aux_low, aux_high);

        // Tension notes use higher dissonance tolerance
        uint8_t safe_tension = getSafePitch(tension_pitch, current_tick, actual_duration,
                                            ctx.main_melody, harmony, aux_low, aux_high,
                                            ctx.chord_degree, 0.5f);
        if (safe_tension != safe_root && safe_tension != fifth_pitch) {
          result.push_back({current_tick, actual_duration, safe_tension,
                            static_cast<uint8_t>(velocity * 0.7f)});  // Softer tension
        }
      }
    }

    current_tick += pad_duration;
  }

  return result;
}

void AuxTrackGenerator::calculateAuxRange(
    const AuxConfig& config,
    const TessituraRange& main_tessitura,
    uint8_t& out_low, uint8_t& out_high) {

  int center = main_tessitura.center + config.range_offset;
  int half_width = config.range_width / 2;

  out_low = static_cast<uint8_t>(std::clamp(center - half_width, 36, 96));
  out_high = static_cast<uint8_t>(std::clamp(center + half_width, 36, 96));

  if (out_low > out_high) {
    std::swap(out_low, out_high);
  }
}

// A4: Find breath points (phrase boundaries) within a time range.
std::vector<Tick> AuxTrackGenerator::findBreathPointsInRange(
    const std::vector<PhraseBoundary>* boundaries,
    Tick start, Tick end) {
  std::vector<Tick> result;
  if (!boundaries) return result;

  for (const auto& boundary : *boundaries) {
    if (boundary.is_breath && boundary.tick >= start && boundary.tick < end) {
      result.push_back(boundary.tick);
    }
  }
  return result;
}

bool AuxTrackGenerator::isPitchSafe(
    uint8_t pitch, Tick start, Tick duration,
    const std::vector<NoteEvent>* main_melody,
    const HarmonyContext& harmony,
    float dissonance_tolerance) {

  // Check against main melody
  if (main_melody) {
    for (const auto& note : *main_melody) {
      if (notesOverlap(start, start + duration,
                       note.start_tick, note.start_tick + note.duration)) {
        int interval = std::abs(static_cast<int>(pitch) - static_cast<int>(note.note));
        interval = interval % 12;

        // A7: With higher tolerance, allow more intervals
        // Base case: minor 2nd (1) and major 7th (11) are dissonant
        bool is_dissonant = (interval == 1 || interval == 11);

        // With tolerance > 0.3, also allow tritone (6)
        if (dissonance_tolerance < 0.3f && interval == 6) {
          is_dissonant = true;
        }

        // With tolerance > 0, probabilistically allow dissonance
        if (is_dissonant && dissonance_tolerance > 0.0f) {
          // Random check would need RNG, so just use threshold
          if (dissonance_tolerance < 0.5f) {
            return false;  // Still reject
          }
          // High tolerance: allow
        } else if (is_dissonant) {
          return false;
        }
      }
    }
  }

  // Also check against HarmonyContext
  return harmony.isPitchSafe(pitch, start, duration, TrackRole::Aux);
}

uint8_t AuxTrackGenerator::getSafePitch(
    uint8_t desired, Tick start, Tick duration,
    const std::vector<NoteEvent>* main_melody,
    const HarmonyContext& harmony,
    uint8_t low, uint8_t high,
    int8_t chord_degree,
    float dissonance_tolerance) {

  if (isPitchSafe(desired, start, duration, main_melody, harmony, dissonance_tolerance)) {
    return desired;
  }

  // Try chord tones nearby
  ChordTones ct = getChordTones(chord_degree);
  int octave = desired / 12;

  int best_pitch = desired;
  int best_dist = 100;

  for (uint8_t i = 0; i < ct.count; ++i) {
    int pc = ct.pitch_classes[i];
    if (pc < 0) continue;

    for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
      int candidate = (octave + oct_offset) * 12 + pc;
      if (candidate < low || candidate > high) continue;

      if (isPitchSafe(static_cast<uint8_t>(candidate), start, duration,
                      main_melody, harmony, dissonance_tolerance)) {
        int dist = std::abs(candidate - static_cast<int>(desired));
        if (dist < best_dist) {
          best_dist = dist;
          best_pitch = candidate;
        }
      }
    }
  }

  return static_cast<uint8_t>(std::clamp(best_pitch,
                                          static_cast<int>(low),
                                          static_cast<int>(high)));
}

// ============================================================================
// F: Unison - Doubles the main melody
// ============================================================================

std::vector<NoteEvent> AuxTrackGenerator::generateUnison(
    const AuxContext& ctx,
    const AuxConfig& config,
    [[maybe_unused]] const HarmonyContext& harmony,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;
  if (!ctx.main_melody || ctx.main_melody->empty()) return result;

  // Timing offset distribution (±5-10 ticks for natural doubling feel)
  std::uniform_int_distribution<int> offset_dist(5, 10);
  std::uniform_int_distribution<int> sign_dist(0, 1);

  for (const auto& note : *ctx.main_melody) {
    // Only process notes within section range
    if (note.start_tick < ctx.section_start ||
        note.start_tick >= ctx.section_end) continue;

    NoteEvent unison = note;

    // Add slight timing offset for natural doubling feel
    int offset = offset_dist(rng) * (sign_dist(rng) ? 1 : -1);
    unison.start_tick = static_cast<Tick>(
        std::max(static_cast<int>(ctx.section_start),
                 static_cast<int>(note.start_tick) + offset));

    // Reduce velocity for background effect
    unison.velocity = static_cast<uint8_t>(
        std::clamp(static_cast<int>(note.velocity * config.velocity_ratio),
                   1, 127));

    result.push_back(unison);
  }

  return result;
}

// ============================================================================
// F+: Harmony - Creates harmony line based on main melody
// ============================================================================

std::vector<NoteEvent> AuxTrackGenerator::generateHarmony(
    const AuxContext& ctx,
    const AuxConfig& config,
    const HarmonyContext& harmony,
    HarmonyMode mode,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;
  if (!ctx.main_melody || ctx.main_melody->empty()) return result;

  // Timing offset distribution
  std::uniform_int_distribution<int> offset_dist(3, 8);
  std::uniform_int_distribution<int> sign_dist(0, 1);

  int note_count = 0;
  for (const auto& note : *ctx.main_melody) {
    // Only process notes within section range
    if (note.start_tick < ctx.section_start ||
        note.start_tick >= ctx.section_end) continue;

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

    // Apply interval and snap to chord tone
    int new_pitch = note.note + interval;
    int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);
    new_pitch = nearestChordTonePitch(new_pitch, chord_degree);

    // Clamp to reasonable range
    harm.note = static_cast<uint8_t>(std::clamp(new_pitch, 48, 84));

    // Add slight timing offset
    int offset = offset_dist(rng) * (sign_dist(rng) ? 1 : -1);
    harm.start_tick = static_cast<Tick>(
        std::max(static_cast<int>(ctx.section_start),
                 static_cast<int>(note.start_tick) + offset));

    // Reduce velocity
    harm.velocity = static_cast<uint8_t>(
        std::clamp(static_cast<int>(note.velocity * config.velocity_ratio),
                   1, 127));

    result.push_back(harm);
    ++note_count;
  }

  return result;
}

// ============================================================================
// G: MelodicHook - Creates memorable hook phrase
// ============================================================================

std::vector<NoteEvent> AuxTrackGenerator::generateMelodicHook(
    const AuxContext& ctx,
    const AuxConfig& config,
    const HarmonyContext& harmony,
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
  int8_t chord_degree = harmony.getChordDegreeAt(ctx.section_start);

  // Start from chord root in aux range
  int base_pitch = nearestChordTonePitch(
      (aux_low + aux_high) / 2, chord_degree);

  // Simple melodic pattern: root, 3rd, 5th, 3rd
  std::array<int, 4> intervals = {0, 4, 7, 4};  // Major chord intervals

  for (int i = 0; i < NOTES_PER_BAR * 2; ++i) {
    int pitch = base_pitch + intervals[i % 4];
    pitch = std::clamp(pitch, static_cast<int>(aux_low), static_cast<int>(aux_high));

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = NOTE_DURATION - TICKS_PER_BEAT / 8;  // Slight gap
    note.note = static_cast<uint8_t>(pitch);
    note.velocity = static_cast<uint8_t>(ctx.base_velocity * config.velocity_ratio);

    base_hook.push_back(note);
    current_tick += NOTE_DURATION;
  }

  // Repeat base hook with variations (AAAB pattern)
  Tick section_length = ctx.section_end - ctx.section_start;
  int phrases_needed = static_cast<int>(section_length / HOOK_PHRASE_TICKS);

  std::uniform_int_distribution<int> variation_dist(-2, 2);

  for (int phrase = 0; phrase < phrases_needed; ++phrase) {
    Tick phrase_start = ctx.section_start + phrase * HOOK_PHRASE_TICKS;

    for (const auto& note : base_hook) {
      NoteEvent hook_note = note;
      hook_note.start_tick = phrase_start + (note.start_tick - ctx.section_start);

      // Apply variation on the B phrase (every 4th phrase)
      if (phrase % 4 == 3) {
        int variation = variation_dist(rng);
        int new_pitch = hook_note.note + variation;
        hook_note.note = static_cast<uint8_t>(
            std::clamp(new_pitch, static_cast<int>(aux_low), static_cast<int>(aux_high)));
      }

      // Skip if outside section
      if (hook_note.start_tick >= ctx.section_end) continue;

      result.push_back(hook_note);
    }
  }

  return result;
}

}  // namespace midisketch
