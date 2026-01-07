#include "track/aux_track.h"
#include "core/chord_utils.h"
#include "core/harmony_context.h"
#include <algorithm>
#include <cmath>

namespace midisketch {

namespace {

constexpr Tick TICK_SIXTEENTH = TICKS_PER_BEAT / 4;
constexpr Tick TICK_EIGHTH = TICKS_PER_BEAT / 2;
constexpr Tick TICK_QUARTER = TICKS_PER_BEAT;
constexpr Tick TICK_HALF = TICKS_PER_BEAT * 2;

// Check if two notes overlap in time
bool notesOverlap(Tick start1, Tick end1, Tick start2, Tick end2) {
  return start1 < end2 && start2 < end1;
}

}  // namespace

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
  }

  for (const auto& note : notes) {
    track.addNote(note.startTick, note.duration, note.note, note.velocity);
  }

  return track;
}

std::vector<NoteEvent> AuxTrackGenerator::generatePulseLoop(
    const AuxContext& ctx,
    const AuxConfig& config,
    const HarmonyContext& harmony,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;

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
    // Apply density ratio
    std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
    if (density_dist(rng) > config.density_ratio) {
      current_tick += note_duration;
      continue;
    }

    uint8_t pitch = pattern_pitches[pattern_idx % pattern_pitches.size()];

    // Check for collision with main melody
    pitch = getSafePitch(pitch, current_tick, note_duration,
                         ctx.main_melody, harmony, aux_low, aux_high,
                         ctx.chord_degree);

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

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  uint8_t velocity = static_cast<uint8_t>(ctx.base_velocity * config.velocity_ratio);

  // Find phrase boundaries in main melody (gaps > quarter note)
  std::vector<Tick> phrase_ends;
  for (size_t i = 0; i < ctx.main_melody->size() - 1; ++i) {
    const auto& note = (*ctx.main_melody)[i];
    const auto& next = (*ctx.main_melody)[i + 1];
    Tick gap = next.startTick - (note.startTick + note.duration);
    if (gap > TICK_QUARTER) {
      phrase_ends.push_back(note.startTick + note.duration);
    }
  }

  // Add hints before phrase ends
  for (Tick phrase_end : phrase_ends) {
    // Apply density ratio
    std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
    if (density_dist(rng) > config.density_ratio) continue;

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

    pitch = getSafePitch(pitch, hint_start, TICK_QUARTER,
                         ctx.main_melody, harmony, aux_low, aux_high,
                         ctx.chord_degree);

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

  // Place accents on beat 2 and 4 (backbeat)
  Tick bar_length = TICKS_PER_BAR;
  Tick current_bar = (ctx.section_start / bar_length) * bar_length;

  while (current_bar < ctx.section_end) {
    // Beat 2
    Tick beat2 = current_bar + TICKS_PER_BEAT;
    if (beat2 >= ctx.section_start && beat2 < ctx.section_end) {
      std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
      if (density_dist(rng) < config.density_ratio) {
        uint8_t pitch = getSafePitch(root_pitch, beat2, TICK_EIGHTH,
                                     ctx.main_melody, harmony, aux_low, aux_high,
                                     ctx.chord_degree);
        result.push_back({beat2, TICK_EIGHTH, pitch, velocity});
      }
    }

    // Beat 4
    Tick beat4 = current_bar + TICKS_PER_BEAT * 3;
    if (beat4 >= ctx.section_start && beat4 < ctx.section_end) {
      std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
      if (density_dist(rng) < config.density_ratio) {
        uint8_t pitch = getSafePitch(root_pitch, beat4, TICK_EIGHTH,
                                     ctx.main_melody, harmony, aux_low, aux_high,
                                     ctx.chord_degree);
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

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  uint8_t velocity = static_cast<uint8_t>(ctx.base_velocity * config.velocity_ratio);

  // Find phrase endings (notes followed by rests)
  for (size_t i = 0; i < ctx.main_melody->size(); ++i) {
    const auto& note = (*ctx.main_melody)[i];
    Tick note_end = note.startTick + note.duration;

    // Check if this is a phrase ending
    bool is_phrase_end = false;
    if (i == ctx.main_melody->size() - 1) {
      is_phrase_end = true;
    } else {
      const auto& next = (*ctx.main_melody)[i + 1];
      Tick gap = next.startTick - note_end;
      is_phrase_end = (gap > TICK_QUARTER);
    }

    if (!is_phrase_end) continue;

    // Apply density ratio
    std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
    if (density_dist(rng) > config.density_ratio) continue;

    // Add tail note after phrase ending
    Tick tail_start = note_end + TICK_EIGHTH;
    if (tail_start >= ctx.section_end) continue;

    // Use a note below the phrase ending
    int tail_pitch = note.note - 2;  // Step down
    tail_pitch = snapToNearestScaleTone(tail_pitch, ctx.key_offset);
    tail_pitch = std::clamp(tail_pitch, static_cast<int>(aux_low),
                            static_cast<int>(aux_high));

    uint8_t pitch = getSafePitch(static_cast<uint8_t>(tail_pitch),
                                 tail_start, TICK_EIGHTH,
                                 ctx.main_melody, harmony, aux_low, aux_high,
                                 ctx.chord_degree);

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

  while (current_tick < ctx.section_end) {
    // Apply density ratio
    std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
    if (density_dist(rng) > config.density_ratio) {
      current_tick += pad_duration;
      continue;
    }

    Tick actual_duration = std::min(pad_duration, ctx.section_end - current_tick);

    // Root note
    uint8_t safe_root = getSafePitch(root_pitch, current_tick, actual_duration,
                                     ctx.main_melody, harmony, aux_low, aux_high,
                                     ctx.chord_degree);
    result.push_back({current_tick, actual_duration, safe_root, velocity});

    // Fifth note (if not clashing)
    if (std::abs(static_cast<int>(fifth_pitch) - static_cast<int>(safe_root)) > 2) {
      uint8_t safe_fifth = getSafePitch(fifth_pitch, current_tick, actual_duration,
                                        ctx.main_melody, harmony, aux_low, aux_high,
                                        ctx.chord_degree);
      if (safe_fifth != safe_root) {
        result.push_back({current_tick, actual_duration, safe_fifth,
                          static_cast<uint8_t>(velocity * 0.9f)});
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

bool AuxTrackGenerator::isPitchSafe(
    uint8_t pitch, Tick start, Tick duration,
    const std::vector<NoteEvent>* main_melody,
    const HarmonyContext& harmony) {

  // Check against main melody
  if (main_melody) {
    for (const auto& note : *main_melody) {
      if (notesOverlap(start, start + duration,
                       note.startTick, note.startTick + note.duration)) {
        int interval = std::abs(static_cast<int>(pitch) - static_cast<int>(note.note));
        interval = interval % 12;
        // Avoid minor 2nd (1) and major 7th (11)
        if (interval == 1 || interval == 11) {
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
    int8_t chord_degree) {

  if (isPitchSafe(desired, start, duration, main_melody, harmony)) {
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
                      main_melody, harmony)) {
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

}  // namespace midisketch
