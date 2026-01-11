/**
 * @file melody_embellishment.cpp
 * @brief Implementation of melodic embellishment system.
 *
 * References:
 * - Kostka, S., & Payne, D. (2012). Tonal Harmony (7th ed.)
 * - Huron, D. (2006). Sweet Anticipation: Music and the Psychology of Expectation
 * - de Clercq, T., & Temperley, D. (2011). A corpus analysis of rock harmony
 */

#include "core/melody_embellishment.h"
#include "core/chord_utils.h"
#include "core/harmony_context.h"
#include <algorithm>
#include <cmath>

namespace midisketch {

namespace {

// C major scale pitch classes (reference for scale steps)
constexpr int MAJOR_SCALE[] = {0, 2, 4, 5, 7, 9, 11};  // C D E F G A B

// Pentatonic scale pitch classes (yonanuki - no 4th or 7th)
constexpr int PENTATONIC[] = {0, 2, 4, 7, 9};  // C D E G A

// Minimum interval for passing tone insertion (minor 3rd)
constexpr int MIN_PT_INTERVAL = 3;

// Minimum duration for splitting into NCT + resolution
constexpr Tick MIN_SPLIT_DURATION = TICK_EIGHTH;

}  // namespace

// ============================================================================
// Configuration
// ============================================================================

EmbellishmentConfig MelodicEmbellisher::getConfigForMood(Mood mood) {
  EmbellishmentConfig config;

  switch (mood) {
    // === Bright/Upbeat moods: stable, consonant ===
    case Mood::BrightUpbeat:
    case Mood::IdolPop:
    case Mood::Anthem:
      config.chord_tone_ratio = 0.75f;
      config.passing_tone_ratio = 0.12f;
      config.neighbor_tone_ratio = 0.08f;
      config.appoggiatura_ratio = 0.03f;
      config.anticipation_ratio = 0.02f;
      config.prefer_pentatonic = true;
      config.syncopation_level = 0.2f;
      break;

    // === Dark/Dramatic moods: more tension, chromatic ===
    case Mood::DarkPop:
    case Mood::Dramatic:
    case Mood::Nostalgic:
      config.chord_tone_ratio = 0.60f;
      config.passing_tone_ratio = 0.12f;
      config.neighbor_tone_ratio = 0.10f;
      config.appoggiatura_ratio = 0.12f;
      config.anticipation_ratio = 0.06f;
      config.prefer_pentatonic = false;
      config.chromatic_approach = true;
      config.syncopation_level = 0.4f;
      break;

    // === Jazz-influenced: CityPop has jazz harmony ===
    case Mood::CityPop:
      config.chord_tone_ratio = 0.50f;
      config.passing_tone_ratio = 0.15f;
      config.neighbor_tone_ratio = 0.10f;
      config.appoggiatura_ratio = 0.10f;
      config.anticipation_ratio = 0.10f;
      config.enable_tensions = true;
      config.tension_ratio = 0.05f;
      config.prefer_pentatonic = false;
      config.syncopation_level = 0.6f;
      break;

    // === Ballad/Emotional: expressive appoggiaturas ===
    case Mood::Ballad:
    case Mood::Sentimental:
    case Mood::EmotionalPop:
      config.chord_tone_ratio = 0.65f;
      config.passing_tone_ratio = 0.12f;
      config.neighbor_tone_ratio = 0.08f;
      config.appoggiatura_ratio = 0.10f;
      config.anticipation_ratio = 0.05f;
      config.enable_tensions = true;
      config.tension_ratio = 0.03f;  // Subtle 9ths for "setsunai" sound
      config.prefer_pentatonic = true;
      config.syncopation_level = 0.3f;
      break;

    // === Energetic/Dance: rhythmic focus ===
    case Mood::EnergeticDance:
    case Mood::LightRock:
    case Mood::FutureBass:
      config.chord_tone_ratio = 0.78f;
      config.passing_tone_ratio = 0.10f;
      config.neighbor_tone_ratio = 0.05f;
      config.appoggiatura_ratio = 0.02f;
      config.anticipation_ratio = 0.05f;
      config.prefer_pentatonic = true;
      config.syncopation_level = 0.5f;
      break;

    // === Chill/Synth: floating, gentle ===
    case Mood::Chill:
    case Mood::Synthwave:
      config.chord_tone_ratio = 0.68f;
      config.passing_tone_ratio = 0.12f;
      config.neighbor_tone_ratio = 0.10f;
      config.appoggiatura_ratio = 0.05f;
      config.anticipation_ratio = 0.05f;
      config.enable_tensions = true;
      config.tension_ratio = 0.04f;
      config.prefer_pentatonic = true;
      config.syncopation_level = 0.25f;
      break;

    // === Anime/YOASOBI: fast, melodic, some tension ===
    case Mood::Yoasobi:
      config.chord_tone_ratio = 0.65f;
      config.passing_tone_ratio = 0.15f;
      config.neighbor_tone_ratio = 0.08f;
      config.appoggiatura_ratio = 0.07f;
      config.anticipation_ratio = 0.05f;
      config.enable_tensions = true;
      config.tension_ratio = 0.02f;
      config.prefer_pentatonic = true;
      config.syncopation_level = 0.4f;
      break;

    // === Default: balanced pop ===
    case Mood::StraightPop:
    case Mood::MidPop:
    case Mood::ModernPop:
    case Mood::ElectroPop:
    default:
      config.chord_tone_ratio = 0.70f;
      config.passing_tone_ratio = 0.12f;
      config.neighbor_tone_ratio = 0.08f;
      config.appoggiatura_ratio = 0.05f;
      config.anticipation_ratio = 0.05f;
      config.prefer_pentatonic = true;
      config.syncopation_level = 0.3f;
      break;
  }

  return config;
}

// ============================================================================
// Main Embellishment Logic
// ============================================================================

std::vector<NoteEvent> MelodicEmbellisher::embellish(
    const std::vector<NoteEvent>& skeleton,
    const EmbellishmentConfig& config,
    const HarmonyContext& harmony,
    int key_offset,
    std::mt19937& rng) {

  if (skeleton.empty()) return {};

  std::vector<NoteEvent> result;
  result.reserve(skeleton.size() * 2);  // May add notes

  std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);
  int consecutive_ncts = 0;

  for (size_t i = 0; i < skeleton.size(); ++i) {
    const NoteEvent& current = skeleton[i];
    const NoteEvent* next = (i + 1 < skeleton.size()) ? &skeleton[i + 1] : nullptr;

    BeatStrength beat = getBeatStrength(current.start_tick);
    int8_t chord_degree = harmony.getChordDegreeAt(current.start_tick);

    float roll = prob_dist(rng);
    float cumulative = 0.0f;

    // Reset consecutive NCT count on chord tones
    bool added_as_ct = false;

    // === Check for NCT opportunity ===

    // 1. Passing Tone: between notes with large intervals
    cumulative += config.passing_tone_ratio;
    if (next != nullptr && roll < cumulative && consecutive_ncts < config.max_consecutive_ncts) {
      int interval = std::abs(static_cast<int>(next->note) - static_cast<int>(current.note));
      if (interval >= MIN_PT_INTERVAL) {
        auto pt = tryInsertPassingTone(current, *next, key_offset,
                                       config.prefer_pentatonic, rng);
        if (pt) {
          result.push_back(current);  // Original chord tone
          result.push_back(*pt);      // Passing tone
          consecutive_ncts++;
          continue;
        }
      }
    }

    // 2. Neighbor Tone: decoration on weak beats
    cumulative += config.neighbor_tone_ratio;
    if (roll < cumulative && beat != BeatStrength::Strong &&
        current.duration >= MIN_SPLIT_DURATION * 2 &&
        consecutive_ncts < config.max_consecutive_ncts) {
      bool upper = prob_dist(rng) > 0.5f;
      auto nt_pair = tryAddNeighborTone(current, upper, key_offset,
                                        config.prefer_pentatonic, rng);
      if (nt_pair) {
        result.push_back(nt_pair->first);   // Neighbor tone
        result.push_back(nt_pair->second);  // Return to CT
        consecutive_ncts++;
        continue;
      }
    }

    // 3. Appoggiatura: expressive dissonance on strong beats
    cumulative += config.appoggiatura_ratio;
    if (roll < cumulative && beat == BeatStrength::Strong &&
        current.duration >= MIN_SPLIT_DURATION * 2 &&
        consecutive_ncts < config.max_consecutive_ncts) {
      bool upper = prob_dist(rng) > 0.5f;
      auto app_pair = tryConvertToAppoggiatura(current, upper, key_offset,
                                               config.chromatic_approach, rng);
      if (app_pair) {
        result.push_back(app_pair->first);   // Appoggiatura
        result.push_back(app_pair->second);  // Resolution
        consecutive_ncts++;
        continue;
      }
    }

    // 4. Anticipation: syncopation before chord change
    cumulative += config.anticipation_ratio;
    if (roll < cumulative && next != nullptr &&
        prob_dist(rng) < config.syncopation_level &&
        consecutive_ncts < config.max_consecutive_ncts) {
      // Check if chord changes between current and next
      int8_t next_chord_degree = harmony.getChordDegreeAt(next->start_tick);
      if (next_chord_degree != chord_degree) {
        auto ant = tryAddAnticipation(current, *next, next->start_tick,
                                      next_chord_degree, rng);
        if (ant && ant->start_tick > current.start_tick) {
          // Shorten current note (check for underflow)
          Tick ant_offset = ant->start_tick - current.start_tick;
          if (ant_offset < current.duration) {
            NoteEvent shortened = current;
            shortened.duration = current.duration - ant_offset;
            result.push_back(shortened);
            result.push_back(*ant);  // Anticipation
            consecutive_ncts++;
            continue;
          }
        }
      }
    }

    // 5. Tension: replace CT with tension tone (if enabled)
    if (config.enable_tensions && config.tension_ratio > 0.0f &&
        prob_dist(rng) < config.tension_ratio) {
      auto tension_pitch = getTensionPitch(chord_degree, current.note, 48, 84, rng);
      if (tension_pitch) {
        NoteEvent tension_note = current;
        tension_note.note = *tension_pitch;
        result.push_back(tension_note);
        consecutive_ncts = 0;  // Tensions are "quasi-chord tones"
        continue;
      }
    }

    // Default: keep as chord tone
    result.push_back(current);
    added_as_ct = true;
    if (added_as_ct) {
      consecutive_ncts = 0;
    }
  }

  // Safety filter: remove any chromatic notes if chromatic_approach is disabled
  if (!config.chromatic_approach) {
    std::vector<NoteEvent> filtered;
    filtered.reserve(result.size());
    for (const auto& note : result) {
      if (isScaleTone(note.note % 12, key_offset)) {
        filtered.push_back(note);
      }
      // Silently drop chromatic notes that shouldn't be there
    }
    return filtered;
  }

  return result;
}

// ============================================================================
// Beat Strength
// ============================================================================

BeatStrength MelodicEmbellisher::getBeatStrength(Tick tick) {
  // Position within bar
  Tick pos_in_bar = tick % TICKS_PER_BAR;

  // Position within beat
  Tick pos_in_beat = tick % TICKS_PER_BEAT;

  // Strong beats: 1 and 3 (ticks 0 and 960 in a bar)
  if (pos_in_beat == 0) {
    if (pos_in_bar == 0 || pos_in_bar == TICKS_PER_BEAT * 2) {
      return BeatStrength::Strong;
    }
    // Beats 2 and 4
    return BeatStrength::Medium;
  }

  // Off-beat 8th notes
  if (pos_in_beat == TICK_EIGHTH) {
    return BeatStrength::Weak;
  }

  // 16th note subdivisions
  return BeatStrength::VeryWeak;
}

// ============================================================================
// Scale Functions
// ============================================================================

bool MelodicEmbellisher::isInPentatonic(int pitch_class, int key_offset) {
  int relative_pc = ((pitch_class - key_offset) % 12 + 12) % 12;

  for (int pc : PENTATONIC) {
    if (relative_pc == pc) return true;
  }
  return false;
}

bool MelodicEmbellisher::isScaleTone(int pitch_class, int key_offset) {
  int relative_pc = ((pitch_class - key_offset) % 12 + 12) % 12;

  for (int pc : MAJOR_SCALE) {
    if (relative_pc == pc) return true;
  }
  return false;
}

int MelodicEmbellisher::scaleStep(int pitch, int direction, int key_offset, bool prefer_pentatonic) {
  int pc = pitch % 12;
  int octave = pitch / 12;

  // Find current position in scale
  const int* scale = prefer_pentatonic ? PENTATONIC : MAJOR_SCALE;
  int scale_size = prefer_pentatonic ? 5 : 7;

  // Convert to relative pitch class
  int relative_pc = ((pc - key_offset) % 12 + 12) % 12;

  // Find index in scale
  int index = -1;
  int closest_index = 0;
  int closest_dist = 100;

  for (int i = 0; i < scale_size; ++i) {
    if (scale[i] == relative_pc) {
      index = i;
      break;
    }
    int dist = std::abs(scale[i] - relative_pc);
    if (dist < closest_dist) {
      closest_dist = dist;
      closest_index = i;
    }
  }

  if (index < 0) {
    index = closest_index;  // Snap to nearest scale tone
  }

  // Step in direction
  int new_index = index + direction;
  int octave_adjust = 0;

  if (new_index >= scale_size) {
    new_index = 0;
    octave_adjust = 1;
  } else if (new_index < 0) {
    new_index = scale_size - 1;
    octave_adjust = -1;
  }

  int new_relative_pc = scale[new_index];
  int new_pc = (new_relative_pc + key_offset) % 12;

  return (octave + octave_adjust) * 12 + new_pc;
}

int MelodicEmbellisher::stepDirection(int from_pitch, int to_pitch) {
  if (to_pitch > from_pitch) return 1;
  if (to_pitch < from_pitch) return -1;
  return 0;
}

// ============================================================================
// NCT Generation
// ============================================================================

std::optional<NoteEvent> MelodicEmbellisher::tryInsertPassingTone(
    const NoteEvent& from,
    const NoteEvent& to,
    int key_offset,
    bool prefer_pentatonic,
    [[maybe_unused]] std::mt19937& rng) {

  int interval = static_cast<int>(to.note) - static_cast<int>(from.note);
  if (std::abs(interval) < MIN_PT_INTERVAL) return std::nullopt;

  int direction = stepDirection(from.note, to.note);

  // Calculate passing tone pitch
  int pt_pitch = scaleStep(from.note, direction, key_offset, prefer_pentatonic);

  // Verify it's between from and to
  if (direction > 0 && (pt_pitch <= from.note || pt_pitch >= to.note)) {
    return std::nullopt;
  }
  if (direction < 0 && (pt_pitch >= from.note || pt_pitch <= to.note)) {
    return std::nullopt;
  }

  // Place PT at midpoint
  Tick pt_start = from.start_tick + from.duration;

  // Check for underflow: pt_start must be before to.start_tick
  if (pt_start >= to.start_tick) return std::nullopt;

  Tick available_space = to.start_tick - pt_start;
  Tick pt_duration = std::min(static_cast<Tick>(TICK_EIGHTH), available_space);

  if (pt_duration < TICK_SIXTEENTH) return std::nullopt;

  // Verify weak beat placement
  BeatStrength beat = getBeatStrength(pt_start);
  if (beat == BeatStrength::Strong) return std::nullopt;

  NoteEvent pt;
  pt.start_tick = pt_start;
  pt.duration = pt_duration;
  pt.note = static_cast<uint8_t>(pt_pitch);
  pt.velocity = static_cast<uint8_t>(from.velocity * 0.85f);  // Slightly softer

  return pt;
}

std::optional<std::pair<NoteEvent, NoteEvent>> MelodicEmbellisher::tryAddNeighborTone(
    const NoteEvent& chord_tone,
    bool upper,
    int key_offset,
    bool prefer_pentatonic,
    [[maybe_unused]] std::mt19937& rng) {

  if (chord_tone.duration < MIN_SPLIT_DURATION * 2) return std::nullopt;

  int direction = upper ? 1 : -1;
  int nt_pitch = scaleStep(chord_tone.note, direction, key_offset, prefer_pentatonic);

  // Split duration: NT + return
  Tick nt_duration = chord_tone.duration / 2;
  Tick return_duration = chord_tone.duration - nt_duration;

  // Safety check: ensure both durations are valid
  if (nt_duration == 0 || return_duration == 0) return std::nullopt;

  // Neighbor tone
  NoteEvent nt;
  nt.start_tick = chord_tone.start_tick;
  nt.duration = nt_duration;
  nt.note = static_cast<uint8_t>(nt_pitch);
  nt.velocity = chord_tone.velocity;

  // Return note (same as original)
  NoteEvent ret;
  ret.start_tick = chord_tone.start_tick + nt_duration;
  ret.duration = return_duration;
  ret.note = chord_tone.note;
  ret.velocity = chord_tone.velocity;

  return std::make_pair(nt, ret);
}

std::optional<std::pair<NoteEvent, NoteEvent>> MelodicEmbellisher::tryConvertToAppoggiatura(
    const NoteEvent& chord_tone,
    bool upper,
    int key_offset,
    bool allow_chromatic,
    [[maybe_unused]] std::mt19937& rng) {

  if (chord_tone.duration < MIN_SPLIT_DURATION * 2) return std::nullopt;

  // Verify strong beat (appoggiaturas are accented dissonances)
  BeatStrength beat = getBeatStrength(chord_tone.start_tick);
  if (beat != BeatStrength::Strong) return std::nullopt;

  int direction = upper ? 1 : -1;

  // Appoggiatura: typically a step above or below resolution
  // Try whole step first (most common)
  int app_pitch = chord_tone.note + (direction * 2);  // Whole step

  if (!isScaleTone(app_pitch % 12, key_offset)) {
    // Whole step is not in scale, try half step
    int half_step_pitch = chord_tone.note + direction;

    if (isScaleTone(half_step_pitch % 12, key_offset)) {
      // Half step is in scale, use it
      app_pitch = half_step_pitch;
    } else if (allow_chromatic) {
      // Half step is chromatic but allowed
      app_pitch = half_step_pitch;
    } else {
      // Neither works and chromatic not allowed, skip
      return std::nullopt;
    }
  }

  // Split: appoggiatura takes more time (expressive emphasis)
  Tick app_duration = (chord_tone.duration * 2) / 3;
  Tick res_duration = chord_tone.duration - app_duration;

  // Safety check: ensure both durations are valid
  if (app_duration == 0 || res_duration == 0) return std::nullopt;

  // Appoggiatura
  NoteEvent app;
  app.start_tick = chord_tone.start_tick;
  app.duration = app_duration;
  app.note = static_cast<uint8_t>(app_pitch);
  app.velocity = static_cast<uint8_t>(std::min(127, chord_tone.velocity + 10));  // Accent

  // Resolution
  NoteEvent res;
  res.start_tick = chord_tone.start_tick + app_duration;
  res.duration = res_duration;
  res.note = chord_tone.note;
  res.velocity = static_cast<uint8_t>(chord_tone.velocity * 0.9f);  // Softer

  return std::make_pair(app, res);
}

std::optional<NoteEvent> MelodicEmbellisher::tryAddAnticipation(
    const NoteEvent& current,
    const NoteEvent& next,
    Tick next_chord_tick,
    int8_t next_chord_degree,
    [[maybe_unused]] std::mt19937& rng) {

  // Anticipation window: just before chord change
  Tick ant_start = next_chord_tick - TICK_SIXTEENTH;

  // Must be within current note's duration
  if (ant_start <= current.start_tick) return std::nullopt;
  if (ant_start >= next.start_tick) return std::nullopt;

  // Get a chord tone from next chord
  auto chord_tones = getChordTonePitchClasses(next_chord_degree);
  if (chord_tones.empty()) return std::nullopt;

  // Find chord tone nearest to current pitch
  int best_ct = chord_tones[0];
  int current_pc = current.note % 12;
  int best_dist = 100;

  for (int ct : chord_tones) {
    int dist = std::min(std::abs(ct - current_pc), 12 - std::abs(ct - current_pc));
    if (dist < best_dist) {
      best_dist = dist;
      best_ct = ct;
    }
  }

  // Build anticipation pitch in same octave
  int octave = current.note / 12;
  int ant_pitch = octave * 12 + best_ct;

  // Adjust octave if needed
  while (ant_pitch < current.note - 6) ant_pitch += 12;
  while (ant_pitch > current.note + 6) ant_pitch -= 12;

  NoteEvent ant;
  ant.start_tick = ant_start;
  ant.duration = TICK_SIXTEENTH;
  ant.note = static_cast<uint8_t>(ant_pitch);
  ant.velocity = current.velocity;

  return ant;
}

std::optional<uint8_t> MelodicEmbellisher::getTensionPitch(
    int8_t chord_degree,
    uint8_t base_pitch,
    uint8_t range_low,
    uint8_t range_high,
    std::mt19937& rng) {

  auto tensions = getAvailableTensionPitchClasses(chord_degree);
  if (tensions.empty()) return std::nullopt;

  // Random tension selection
  std::uniform_int_distribution<size_t> dist(0, tensions.size() - 1);
  int tension_pc = tensions[dist(rng)];

  // Find tension pitch near base
  int octave = base_pitch / 12;
  int tension_pitch = octave * 12 + tension_pc;

  // Adjust to range
  while (tension_pitch < range_low) tension_pitch += 12;
  while (tension_pitch > range_high) tension_pitch -= 12;

  if (tension_pitch < range_low || tension_pitch > range_high) {
    return std::nullopt;
  }

  return static_cast<uint8_t>(tension_pitch);
}

}  // namespace midisketch
