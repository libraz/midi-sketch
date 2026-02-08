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

#include <algorithm>
#include <cmath>

#include "core/rng_util.h"

#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"

namespace midisketch {

namespace {

// Set embellishment provenance on a note.
inline void setEmbellishmentProv(NoteEvent& note, int8_t chord_degree) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
  note.prov_source = static_cast<uint8_t>(NoteSource::Embellishment);
  note.prov_chord_degree = chord_degree;
  note.prov_lookup_tick = note.start_tick;
  note.prov_original_pitch = note.note;
#else
  (void)note;
  (void)chord_degree;
#endif
}

// Pentatonic scale pitch classes (yonanuki - no 4th or 7th)
// Note: Major scale uses SCALE from pitch_utils.h
constexpr int PENTATONIC[] = {0, 2, 4, 7, 9};              // C D E G A (major pentatonic)
constexpr int MINOR_PENTATONIC[] = {0, 3, 5, 7, 10};      // C Eb F G Bb
constexpr int BLUES_SCALE[] = {0, 3, 5, 6, 7, 10};        // C Eb F F# G Bb (minor penta + blue note)

// Minimum interval for passing tone insertion (minor 3rd)
constexpr int MIN_PT_INTERVAL = 3;

// Minimum duration for splitting into NCT + resolution
constexpr Tick MIN_SPLIT_DURATION = TICK_EIGHTH;

// Probability (0-100) to use 16th note grid instead of 8th note grid
// Adds rhythmic variety while keeping 8th notes as the default
constexpr int SIXTEENTH_NOTE_PROBABILITY = 25;

// Returns the quantization grid size (TICK_EIGHTH or TICK_SIXTEENTH)
// based on probability. 25% chance of using 16th note grid.
inline Tick getQuantizationGrid(std::mt19937& rng) {
  return (rng_util::rollRange(rng, 0, 99) < SIXTEENTH_NOTE_PROBABILITY) ? TICK_SIXTEENTH : TICK_EIGHTH;
}

}  // namespace

// ============================================================================
// Configuration
// ============================================================================

EmbellishmentConfig MelodicEmbellisher::getConfigForMood(Mood mood) {
  EmbellishmentConfig config;

  switch (mood) {
    // === Bright/Upbeat moods: stable, consonant ===
    // Slightly increased NCT ratios for more melodic interest
    case Mood::BrightUpbeat:
    case Mood::IdolPop:
    case Mood::Anthem:
      config.chord_tone_ratio = 0.72f;
      config.passing_tone_ratio = 0.13f;  // Increased from 0.12
      config.neighbor_tone_ratio = 0.08f;
      config.appoggiatura_ratio = 0.04f;  // Increased from 0.03
      config.anticipation_ratio = 0.03f;  // Increased from 0.02
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
      config.pentatonic_mode = PentatonicMode::Minor;
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
      config.pentatonic_mode = PentatonicMode::Blues;
      config.chromatic_approach = true;
      config.syncopation_level = 0.6f;
      break;

    // === Ballad/Emotional: expressive appoggiaturas ===
    // Increased NCT ratios for more expressive, emotional melodies
    case Mood::Ballad:
    case Mood::Sentimental:
    case Mood::EmotionalPop:
      config.chord_tone_ratio = 0.55f;
      config.passing_tone_ratio = 0.14f;
      config.neighbor_tone_ratio = 0.10f;
      config.appoggiatura_ratio = 0.12f;  // Expressive appoggiaturas for "setsunai"
      config.anticipation_ratio = 0.06f;
      config.enable_tensions = true;
      config.tension_ratio = 0.06f;  // Doubled: richer 9th/13th color for emotional depth
      config.prefer_pentatonic = true;
      config.pentatonic_mode = PentatonicMode::Minor;
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
      config.chromatic_approach = true;
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
      config.chromatic_approach = true;
      config.syncopation_level = 0.4f;
      break;

    // === Default: balanced pop ===
    // Increased NCT ratios for more musical melodies
    case Mood::StraightPop:
    case Mood::MidPop:
    case Mood::ModernPop:
    case Mood::ElectroPop:
    default:
      config.chord_tone_ratio = 0.65f;
      config.passing_tone_ratio = 0.15f;  // Increased from 0.12
      config.neighbor_tone_ratio = 0.10f;  // Increased from 0.08
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

std::vector<NoteEvent> MelodicEmbellisher::embellish(const std::vector<NoteEvent>& skeleton,
                                                     const EmbellishmentConfig& config,
                                                     const IHarmonyContext& harmony, int key_offset,
                                                     std::mt19937& rng) {
  if (skeleton.empty()) return {};

  std::vector<NoteEvent> result;
  result.reserve(skeleton.size() * 2);  // May add notes

  int consecutive_ncts = 0;

  for (size_t i = 0; i < skeleton.size(); ++i) {
    const NoteEvent& current = skeleton[i];
    const NoteEvent* next = (i + 1 < skeleton.size()) ? &skeleton[i + 1] : nullptr;

    BeatStrength beat = getBeatStrength(current.start_tick);
    int8_t chord_degree = harmony.getChordDegreeAt(current.start_tick);

    float roll = rng_util::rollFloat(rng, 0.0f, 1.0f);
    float cumulative = 0.0f;

    // Reset consecutive NCT count on chord tones
    bool added_as_ct = false;

    // === Check for NCT opportunity ===
    // NCT selection uses cumulative probability distribution:
    // - passing_tone_ratio, neighbor_tone_ratio, appoggiatura_ratio, anticipation_ratio
    //   are checked sequentially with cumulative thresholds
    // - If roll doesn't fall into any NCT band, the note remains as a Chord Tone
    // - Therefore: chord_tone_ratio = 1.0 - sum(all NCT ratios)
    // - The chord_tone_ratio field in config is for DOCUMENTATION purposes only;
    //   actual CT probability is implicitly the remaining probability mass

    // 1. Passing Tone: between notes with large intervals
    cumulative += config.passing_tone_ratio;
    if (next != nullptr && roll < cumulative && consecutive_ncts < config.max_consecutive_ncts) {
      int interval = std::abs(static_cast<int>(next->note) - static_cast<int>(current.note));
      if (interval >= MIN_PT_INTERVAL) {
        auto pt = tryInsertPassingTone(current, *next, key_offset, config.prefer_pentatonic, rng);
        if (pt && harmony.isConsonantWithOtherTracks(pt->note, pt->start_tick, pt->duration,
                                                      TrackRole::Vocal)) {
          result.push_back(current);  // Original chord tone
          setEmbellishmentProv(*pt, chord_degree);
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
      bool upper = rng_util::rollFloat(rng, 0.0f, 1.0f) > 0.5f;
      auto nt_pair = tryAddNeighborTone(current, upper, key_offset, config.prefer_pentatonic, rng);
      if (nt_pair && harmony.isConsonantWithOtherTracks(nt_pair->first.note, nt_pair->first.start_tick,
                                                         nt_pair->first.duration, TrackRole::Vocal)) {
        setEmbellishmentProv(nt_pair->first, chord_degree);
        setEmbellishmentProv(nt_pair->second, chord_degree);
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
      bool upper = rng_util::rollFloat(rng, 0.0f, 1.0f) > 0.5f;
      auto app_pair =
          tryConvertToAppoggiatura(current, upper, key_offset, config.chromatic_approach, rng);
      if (app_pair && harmony.isConsonantWithOtherTracks(app_pair->first.note,
                                                          app_pair->first.start_tick,
                                                          app_pair->first.duration, TrackRole::Vocal,
                                                          true /* is_weak_beat: appoggiatura is intentionally dissonant */)) {
        setEmbellishmentProv(app_pair->first, chord_degree);
        setEmbellishmentProv(app_pair->second, chord_degree);
        result.push_back(app_pair->first);   // Appoggiatura
        result.push_back(app_pair->second);  // Resolution
        consecutive_ncts++;
        continue;
      }
    }

    // 4. Anticipation: syncopation before chord change
    cumulative += config.anticipation_ratio;
    if (roll < cumulative && next != nullptr && rng_util::rollFloat(rng, 0.0f, 1.0f) < config.syncopation_level &&
        consecutive_ncts < config.max_consecutive_ncts) {
      // Check if chord changes between current and next
      int8_t next_chord_degree = harmony.getChordDegreeAt(next->start_tick);
      if (next_chord_degree != chord_degree) {
        auto ant = tryAddAnticipation(current, *next, next->start_tick, next_chord_degree, rng);
        if (ant && ant->start_tick > current.start_tick &&
            harmony.isConsonantWithOtherTracks(ant->note, ant->start_tick, ant->duration,
                                                TrackRole::Vocal)) {
          // Shorten current note (check for underflow)
          Tick ant_offset = ant->start_tick - current.start_tick;
          if (ant_offset < current.duration) {
            NoteEvent shortened = current;
            shortened.duration = current.duration - ant_offset;
            result.push_back(shortened);
            setEmbellishmentProv(*ant, next_chord_degree);
            result.push_back(*ant);  // Anticipation
            consecutive_ncts++;
            continue;
          }
        }
      }
    }

    // 5. Tension: replace CT with tension tone (if enabled)
    if (config.enable_tensions && config.tension_ratio > 0.0f &&
        rng_util::rollFloat(rng, 0.0f, 1.0f) < config.tension_ratio) {
      auto tension_pitch = getTensionPitch(chord_degree, current.note, 48, 84, rng);
      if (tension_pitch &&
          harmony.isConsonantWithOtherTracks(*tension_pitch, current.start_tick, current.duration,
                                              TrackRole::Vocal)) {
        NoteEvent tension_note = current;
        tension_note.note = *tension_pitch;
        setEmbellishmentProv(tension_note, chord_degree);
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

  // Safety filter: snap chromatic notes to nearest scale tone if chromatic_approach is disabled
  // This preserves melodic contour instead of silently dropping notes, which could create gaps.
  if (!config.chromatic_approach) {
    for (auto& note : result) {
      if (!isScaleTone(getPitchClass(note.note), key_offset)) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
        uint8_t old_pitch = note.note;
#endif
        uint8_t snapped = static_cast<uint8_t>(snapToNearestScaleTone(note.note, key_offset));
        // Re-verify collision safety after scale snap
        if (harmony.isConsonantWithOtherTracks(snapped, note.start_tick, note.duration,
                                                TrackRole::Vocal)) {
          note.note = snapped;
        }
#ifdef MIDISKETCH_NOTE_PROVENANCE
        if (old_pitch != note.note) {
          note.prov_original_pitch = old_pitch;
          note.addTransformStep(TransformStepType::ScaleSnap, old_pitch, note.note, 0, 0);
        }
#endif
      }
    }
  }

  return result;
}

// ============================================================================
// Beat Strength
// ============================================================================

BeatStrength MelodicEmbellisher::getBeatStrength(Tick tick) {
  // Position within bar
  Tick pos_in_bar = positionInBar(tick);

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

  // Accept notes from both major and minor pentatonic scales.
  // This broadens the acceptance set, which is musically appropriate
  // since embellishment already controls style through config ratios.
  for (int pc : PENTATONIC) {
    if (relative_pc == pc) return true;
  }
  for (int pc : MINOR_PENTATONIC) {
    if (relative_pc == pc) return true;
  }
  return false;
}

bool MelodicEmbellisher::isInPentatonicMode(int pitch_class, int key_offset, PentatonicMode mode) {
  int relative_pc = ((pitch_class - key_offset) % 12 + 12) % 12;
  switch (mode) {
    case PentatonicMode::Major:
      for (int pc : PENTATONIC) {
        if (relative_pc == pc) return true;
      }
      break;
    case PentatonicMode::Minor:
      for (int pc : MINOR_PENTATONIC) {
        if (relative_pc == pc) return true;
      }
      break;
    case PentatonicMode::Blues:
      for (int pc : BLUES_SCALE) {
        if (relative_pc == pc) return true;
      }
      break;
  }
  return false;
}


int MelodicEmbellisher::scaleStep(int pitch, int direction, int key_offset,
                                  bool prefer_pentatonic) {
  int pc = pitch % 12;
  int octave = pitch / 12;

  // Find current position in scale
  const int* scale = prefer_pentatonic ? PENTATONIC : SCALE;
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

std::optional<NoteEvent> MelodicEmbellisher::tryInsertPassingTone(const NoteEvent& from,
                                                                  const NoteEvent& to,
                                                                  int key_offset,
                                                                  bool prefer_pentatonic,
                                                                  std::mt19937& rng) {
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

  // Place PT at midpoint, quantized to grid (probabilistic 8th/16th note)
  Tick grid = getQuantizationGrid(rng);
  Tick pt_start = from.start_tick + from.duration;
  // Snap to next grid position for natural rhythm
  pt_start = ((pt_start + grid - 1) / grid) * grid;

  // Check for underflow: pt_start must be before to.start_tick
  if (pt_start >= to.start_tick) return std::nullopt;

  Tick available_space = to.start_tick - pt_start;
  Tick pt_duration = std::min(grid, available_space);

  if (pt_duration < grid) return std::nullopt;  // Minimum grid duration

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
    const NoteEvent& chord_tone, bool upper, int key_offset, bool prefer_pentatonic,
    std::mt19937& rng) {
  if (chord_tone.duration < MIN_SPLIT_DURATION * 2) return std::nullopt;

  int direction = upper ? 1 : -1;
  int nt_pitch = scaleStep(chord_tone.note, direction, key_offset, prefer_pentatonic);

  // Split duration: NT + return, quantized to grid (probabilistic 8th/16th)
  Tick grid = getQuantizationGrid(rng);
  Tick nt_duration = (chord_tone.duration / 2 / grid) * grid;
  if (nt_duration < grid) nt_duration = grid;
  Tick return_duration = chord_tone.duration - nt_duration;

  // Safety check: ensure both durations are valid
  if (return_duration < grid) return std::nullopt;

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
    const NoteEvent& chord_tone, bool upper, int key_offset, bool allow_chromatic,
    std::mt19937& rng) {
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
  // Quantize to grid (probabilistic 8th/16th) for natural rhythm
  Tick grid = getQuantizationGrid(rng);
  Tick app_duration = ((chord_tone.duration * 2) / 3 / grid) * grid;
  if (app_duration < grid) app_duration = grid;
  Tick res_duration = chord_tone.duration - app_duration;

  // Safety check: ensure resolution has valid duration
  if (res_duration < grid) return std::nullopt;

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

std::optional<NoteEvent> MelodicEmbellisher::tryAddAnticipation(const NoteEvent& current,
                                                                const NoteEvent& next,
                                                                Tick next_chord_tick,
                                                                int8_t next_chord_degree,
                                                                std::mt19937& rng) {
  // Anticipation window: just before chord change (probabilistic 8th/16th grid)
  Tick grid = getQuantizationGrid(rng);
  Tick ant_start = next_chord_tick - grid;

  // Must be within current note's duration
  if (ant_start <= current.start_tick) return std::nullopt;
  if (ant_start >= next.start_tick) return std::nullopt;

  // Get a chord tone from next chord
  auto chord_tones = getChordTonePitchClasses(next_chord_degree);
  if (chord_tones.empty()) return std::nullopt;

  // Find chord tone nearest to current pitch
  int best_ct = chord_tones[0];
  int current_pc = getPitchClass(current.note);
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
  ant.duration = grid;  // Match grid quantization
  ant.note = static_cast<uint8_t>(ant_pitch);
  ant.velocity = current.velocity;

  return ant;
}

std::optional<uint8_t> MelodicEmbellisher::getTensionPitch(int8_t chord_degree, uint8_t base_pitch,
                                                           uint8_t range_low, uint8_t range_high,
                                                           std::mt19937& rng) {
  auto tensions = getAvailableTensionPitchClasses(chord_degree);
  if (tensions.empty()) return std::nullopt;

  // Random tension selection
  int tension_pc = rng_util::selectRandom(rng, tensions);

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
