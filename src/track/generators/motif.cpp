/**
 * @file motif.cpp
 * @brief Implementation of MotifGenerator.
 *
 * Background motif track generation with RhythmSync/RhythmLock coordination support.
 * Motif can act as "coordinate axis" in RhythmSync paradigm with Locked policy.
 */

#include "track/generators/motif.h"

#include <algorithm>
#include <array>
#include <map>
#include <vector>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/pitch_monotony_tracker.h"
#include "core/motif.h"
#include "core/motif_types.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/production_blueprint.h"
#include "core/rng_util.h"
#include "core/song.h"
#include "core/timing_constants.h"

namespace midisketch {

// =============================================================================
// RiffPolicy Cache for Locked/Evolving modes
// =============================================================================

/// Cache for RiffPolicy::Locked and RiffPolicy::Evolving modes.
/// Stores the pattern from the first valid section to reuse across sections.
struct MotifRiffCache {
  std::vector<NoteEvent> pattern;
  std::vector<uint8_t> adjusted_pitches;  // Pitches after adjustment (for Locked mode)
  bool cached = false;
  bool pitches_adjusted = false;  // True after first section processes pitches
};

// =============================================================================
// M1: ScaleType Support - Convert scale type to interval array
// =============================================================================

// Internal implementation details for motif track generation.
namespace motif_detail {

// Scale interval arrays and getScaleIntervals() are now in pitch_utils.h.

// =============================================================================
// Pitch Monotony Tracking - uses shared PitchMonotonyTracker from core/
// =============================================================================

// =============================================================================
// RhythmSync Motif Rhythm Template System
// =============================================================================

// Template data table indexed by (MotifRhythmTemplate - 1) since None=0.
// Each entry defines the rhythmic skeleton for one cycle (1 or 2 bars).
constexpr MotifRhythmTemplateConfig kRhythmTemplates[] = {
    // EighthDrive: 8 notes, straight 8ths (1 bar)
    {{0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, -1, -1, -1, -1, -1, -1, -1, -1},
     {1.0f, 0.6f, 0.8f, 0.6f, 0.9f, 0.6f, 0.8f, 0.7f, -1, -1, -1, -1, -1, -1, -1, -1},
     8, MotifRhythmDensity::Driving},
    // GallopDrive: 12 notes, galloping 16ths (1 bar)
    {{0.0f, 0.25f, 0.5f, 1.0f, 1.25f, 1.5f, 2.0f, 2.25f, 2.5f, 3.0f, 3.25f, 3.5f, -1, -1, -1, -1},
     {1.0f, 0.5f, 0.7f, 0.9f, 0.5f, 0.7f, 1.0f, 0.5f, 0.7f, 0.9f, 0.5f, 0.7f, -1, -1, -1, -1},
     12, MotifRhythmDensity::Driving},
    // MixedGrooveA: 6 notes, call-and-response (1 bar)
    {{0.0f, 0.5f, 1.0f, 2.0f, 2.5f, 3.0f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {1.0f, 0.7f, 0.65f, 0.9f, 0.7f, 0.65f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     6, MotifRhythmDensity::Medium},
    // MixedGrooveB: 6 notes, front-loaded (1 bar)
    {{0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {1.0f, 0.7f, 0.8f, 0.6f, 0.9f, 0.7f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     6, MotifRhythmDensity::Medium},
    // MixedGrooveC: 6 notes, syncopated push (1 bar)
    {{0.0f, 1.0f, 1.5f, 2.0f, 3.0f, 3.5f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {0.9f, 1.0f, 0.6f, 0.85f, 0.9f, 0.7f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     6, MotifRhythmDensity::Medium},
    // PushGroove: 7 notes, anticipation (1 bar)
    {{0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.5f, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {1.0f, 0.6f, 0.8f, 0.6f, 0.9f, 0.6f, 0.85f, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     7, MotifRhythmDensity::Driving},
    // EighthPickup: 8 notes, 16th pickup ending (1 bar)
    {{0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.75f, -1, -1, -1, -1, -1, -1, -1, -1},
     {1.0f, 0.6f, 0.8f, 0.6f, 0.9f, 0.6f, 0.8f, 0.75f, -1, -1, -1, -1, -1, -1, -1, -1},
     8, MotifRhythmDensity::Driving},
    // HalfNoteSparse: 4 notes, 2-bar half-note rhythm [0,2,4,6]
    {{0.0f, 2.0f, 4.0f, 6.0f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {1.0f, 0.8f, 0.9f, 0.7f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     4, MotifRhythmDensity::Sparse},
};

static_assert(sizeof(kRhythmTemplates) / sizeof(kRhythmTemplates[0]) ==
                  static_cast<size_t>(MotifRhythmTemplate::Count) - 1,
              "kRhythmTemplates count must match MotifRhythmTemplate enum count (excluding None)");

/// @brief Get the template config for a given template ID.
const MotifRhythmTemplateConfig& getTemplateConfig(MotifRhythmTemplate tmpl) {
  auto idx = static_cast<size_t>(tmpl);
  if (idx == 0 || idx >= static_cast<size_t>(MotifRhythmTemplate::Count)) {
    // Fallback to EighthDrive
    return kRhythmTemplates[0];
  }
  return kRhythmTemplates[idx - 1];
}

/// @brief Select a rhythm template based on BPM using weighted probability.
MotifRhythmTemplate selectRhythmSyncTemplate(uint16_t bpm, std::mt19937& rng) {
  // Probability weights for each template by BPM band.
  // Order: EighthDrive, GallopDrive, MixedGrooveA, MixedGrooveB, MixedGrooveC,
  //        PushGroove, EighthPickup, HalfNoteSparse
  constexpr int kTemplateCount = 8;
  struct TemplateWeights {
    int weights[kTemplateCount];
  };

  TemplateWeights w;
  if (bpm >= 160) {
    // Fast (Orangestar core): HalfNoteSparse ~25% for spacious contrast
    w = {{22, 14, 9, 8, 8, 6, 4, 25}};
  } else if (bpm >= 130) {
    // Medium: half-note works well at moderate tempos
    w = {{20, 7, 13, 10, 9, 6, 5, 25}};
  } else {
    // Slow: sparse patterns shine at low BPM
    w = {{12, 4, 20, 15, 15, 8, 4, 22}};
  }

  int total = 0;
  for (int i = 0; i < kTemplateCount; ++i) total += w.weights[i];

  std::uniform_int_distribution<int> dist(0, total - 1);
  int roll = dist(rng);
  int cumulative = 0;
  for (int i = 0; i < kTemplateCount; ++i) {
    cumulative += w.weights[i];
    if (roll < cumulative) {
      // EighthDrive=1, GallopDrive=2, ..., EighthPickup=7
      return static_cast<MotifRhythmTemplate>(i + 1);
    }
  }
  return MotifRhythmTemplate::EighthDrive;  // Fallback
}

/// @brief Generate rhythm positions from a template.
/// @return Vector of tick positions for one bar of the template.
std::vector<Tick> generateRhythmPositionsFromTemplate(MotifRhythmTemplate tmpl) {
  const auto& config = getTemplateConfig(tmpl);
  std::vector<Tick> positions;
  positions.reserve(config.note_count);
  for (uint8_t i = 0; i < config.note_count; ++i) {
    if (config.beat_positions[i] < 0) break;
    Tick tick = static_cast<Tick>(config.beat_positions[i] * TICKS_PER_BEAT);
    positions.push_back(tick);
  }
  return positions;
}

// M1: Determine appropriate scale type based on chord quality and mood
ScaleType selectScaleType(bool is_minor, Mood mood) {
  if (is_minor) {
    switch (mood) {
      case Mood::Dramatic:
      case Mood::DarkPop:
        return ScaleType::HarmonicMinor;
      case Mood::Chill:
      case Mood::CityPop:
        return ScaleType::Dorian;
      default:
        return ScaleType::NaturalMinor;
    }
  } else {
    switch (mood) {
      case Mood::Synthwave:
      case Mood::FutureBass:
        return ScaleType::Mixolydian;
      default:
        return ScaleType::Major;
    }
  }
}

// degreeToPitch() is now in pitch_utils.h.

// Adjust pitch to avoid dissonance by resolving to nearest chord tone.
// Uses ChordToneHelper from chord_utils.h for chord tone lookup.
int adjustForChord(int pitch, uint8_t chord_root, bool is_minor, int8_t chord_degree) {
  if (!isAvoidNoteWithContext(pitch, chord_root, is_minor, chord_degree)) {
    return pitch;
  }
  ChordToneHelper helper(chord_degree);
  return helper.nearestChordTone(static_cast<uint8_t>(std::clamp(pitch, 0, 127)));
}

// Snap pitch to a safe scale tone using ChordToneHelper.
int snapToSafeScaleTone(int pitch, uint8_t chord_root, bool is_minor, int8_t chord_degree,
                        float melodic_freedom, std::mt19937& rng) {
  ChordToneHelper helper(chord_degree);
  uint8_t clamped = static_cast<uint8_t>(std::clamp(pitch, 0, 127));

  if (isDiatonic(pitch) && !isAvoidNoteWithContext(pitch, chord_root, is_minor, chord_degree)) {
    // Passing tone: diatonic but not a chord tone
    if (!helper.isChordTone(clamped)) {
      if (rng_util::rollProbability(rng, melodic_freedom)) {
        return pitch;
      }
    } else {
      return pitch;
    }
  }

  return helper.nearestChordTone(clamped);
}

// Adjust pitch to nearest diatonic scale tone
int adjustToDiatonic(int pitch) {
  if (isDiatonic(pitch)) {
    return pitch;
  }
  int pitch_class = ((pitch % 12) + 12) % 12;
  int adjustment = 0;
  switch (pitch_class) {
    case 1:
      adjustment = -1;
      break;
    case 3:
      adjustment = -1;
      break;
    case 6:
      adjustment = +1;
      break;
    case 8:
      adjustment = -1;
      break;
    case 10:
      adjustment = -1;
      break;
  }
  return pitch + adjustment;
}

// Adjust pitch to nearest scale tone
int adjustPitchToScale(int pitch, uint8_t key_root, ScaleType scale) {
  const int* intervals = getScaleIntervals(scale);
  int pitch_class = ((pitch - static_cast<int>(key_root)) % 12 + 12) % 12;

  for (int i = 0; i < 7; ++i) {
    if (intervals[i] == pitch_class) {
      return pitch;
    }
  }

  int best_pitch = pitch;
  int best_dist = 12;

  for (int i = 0; i < 7; ++i) {
    int scale_pc = intervals[i];
    int dist1 = std::abs(scale_pc - pitch_class);
    int dist2 = 12 - dist1;
    int dist = std::min(dist1, dist2);

    if (dist < best_dist) {
      best_dist = dist;
      if (scale_pc > pitch_class) {
        if (scale_pc - pitch_class <= 6) {
          best_pitch = pitch + (scale_pc - pitch_class);
        } else {
          best_pitch = pitch - (12 - scale_pc + pitch_class);
        }
      } else {
        if (pitch_class - scale_pc <= 6) {
          best_pitch = pitch - (pitch_class - scale_pc);
        } else {
          best_pitch = pitch + (12 - pitch_class + scale_pc);
        }
      }
    }
  }

  return best_pitch;
}

// Generate rhythm positions based on density
std::vector<Tick> generateRhythmPositions(MotifRhythmDensity density, MotifLength length,
                                          uint8_t note_count, std::mt19937& /* rng */) {
  Tick motif_ticks = static_cast<Tick>(length) * TICKS_PER_BAR;
  std::vector<Tick> positions;

  if (density == MotifRhythmDensity::Driving) {
    Tick step = TICKS_PER_BEAT / 2;
    for (Tick t = 0; t < motif_ticks && positions.size() < note_count; t += step) {
      positions.push_back(t);
    }
    return positions;
  }

  Tick half_ticks = motif_ticks / 2;
  uint8_t call_count = (note_count + 1) / 2;
  uint8_t response_count = note_count - call_count;

  auto fillHalf = [&positions](Tick start, Tick end, uint8_t count, MotifRhythmDensity d) {
    if (count == 0) return;

    Tick step = (d == MotifRhythmDensity::Sparse) ? TICKS_PER_BEAT : TICKS_PER_BEAT / 2;

    std::vector<Tick> candidates;
    for (Tick t = start; t < end; t += step) {
      candidates.push_back(t);
    }

    if (d == MotifRhythmDensity::Medium) {
      std::stable_sort(candidates.begin(), candidates.end(), [start](Tick a, Tick b) {
        Tick a_rel = a - start;
        Tick b_rel = b - start;
        bool a_downbeat = (a_rel % TICKS_PER_BEAT == 0);
        bool b_downbeat = (b_rel % TICKS_PER_BEAT == 0);
        if (a_downbeat != b_downbeat) return a_downbeat;
        return a < b;
      });
    }

    uint8_t added = 0;
    for (size_t i = 0; i < candidates.size() && added < count; ++i) {
      positions.push_back(candidates[i]);
      added++;
    }
  };

  fillHalf(0, half_ticks, call_count, density);
  fillHalf(half_ticks, motif_ticks, response_count, density);

  std::sort(positions.begin(), positions.end());
  return positions;
}

// Generate pitch sequence with antecedent-consequent structure
std::vector<int> generatePitchSequence(uint8_t note_count, MotifMotion motion, std::mt19937& rng,
                                       int max_leap_degrees = 7, bool prefer_stepwise = false) {
  std::vector<int> degrees;

  auto constrainedStep = [max_leap_degrees, prefer_stepwise](int step) {
    int limit = prefer_stepwise ? std::min(2, max_leap_degrees) : max_leap_degrees;
    return std::clamp(step, -limit, limit);
  };

  uint8_t half = note_count / 2;

  degrees.push_back(0);
  int current = 0;

  for (uint8_t i = 1; i < half; ++i) {
    int step = 0;
    switch (motion) {
      case MotifMotion::Stepwise: {
        int limit = std::min(2, max_leap_degrees);
        step = rng_util::rollRange(rng, -limit, limit);
        if (step == 0) step = 1;
        break;
      }
      case MotifMotion::GentleLeap: {
        int limit = std::min(3, max_leap_degrees);
        step = rng_util::rollRange(rng, -limit, limit);
        if (step == 0) step = 1;
        break;
      }
      case MotifMotion::WideLeap: {
        int limit = std::min(5, max_leap_degrees);
        step = rng_util::rollRange(rng, -limit, limit);
        if (step == 0) step = (rng_util::rollRange(rng, -limit, limit) > 0) ? 2 : -2;
        break;
      }
      case MotifMotion::NarrowStep: {
        step = rng_util::rollRange(rng, -1, 1);
        if (step == 0) step = 1;
        break;
      }
      case MotifMotion::Disjunct: {
        int limit = std::min(6, max_leap_degrees);
        int magnitude = rng_util::rollRange(rng, 2, limit);
        step = rng_util::rollRange(rng, 0, 1) ? magnitude : -magnitude;
        break;
      }
    }
    step = constrainedStep(step);
    current += step;
    current = std::clamp(current, -4, 7);
    degrees.push_back(current);
  }

  int question_endings[] = {1, 3};
  degrees.push_back(question_endings[rng_util::rollRange(rng, 0, 1)]);

  current = degrees.back();
  for (uint8_t i = half + 1; i < note_count - 1; ++i) {
    int step = 0;
    switch (motion) {
      case MotifMotion::Stepwise: {
        int limit = std::min(2, max_leap_degrees);
        step = rng_util::rollRange(rng, -limit, limit);
        if (step == 0) step = -1;
        break;
      }
      case MotifMotion::GentleLeap: {
        int limit = std::min(3, max_leap_degrees);
        step = rng_util::rollRange(rng, -limit, std::min(2, limit));
        if (step == 0) step = -1;
        break;
      }
      case MotifMotion::WideLeap: {
        int limit = std::min(4, max_leap_degrees);
        step = rng_util::rollRange(rng, -limit, std::min(3, limit));
        if (step == 0) step = -2;
        break;
      }
      case MotifMotion::NarrowStep: {
        step = rng_util::rollRange(rng, -1, 1);
        if (step == 0) step = -1;
        break;
      }
      case MotifMotion::Disjunct: {
        int limit = std::min(4, max_leap_degrees);
        int magnitude = rng_util::rollRange(rng, 1, limit);
        step = (rng_util::rollRange(rng, 0, 2) < 2) ? -magnitude : magnitude;
        break;
      }
    }
    step = constrainedStep(step);
    current += step;
    current = std::clamp(current, -4, 7);
    degrees.push_back(current);
  }

  int answer_endings[] = {0, 2, 4};
  degrees.push_back(answer_endings[rng_util::rollRange(rng, 0, 2)]);

  return degrees;
}

// =============================================================================
// Vocal Coordination Helpers (for MelodyLead mode)
// =============================================================================

bool isInVocalRest(Tick tick, const std::vector<Tick>* rest_positions, Tick threshold = 480) {
  if (!rest_positions || rest_positions->empty()) return false;

  for (const Tick& rest_start : *rest_positions) {
    if (tick >= rest_start && tick < rest_start + threshold * 2) {
      return true;
    }
  }
  return false;
}

uint8_t calculateMotifRegister(uint8_t vocal_low, uint8_t vocal_high, bool register_high,
                               int8_t register_offset) {
  uint8_t vocal_center = (vocal_low + vocal_high) / 2;

  uint8_t base_note;
  if (register_high) {
    base_note = std::min(static_cast<uint8_t>(vocal_high), static_cast<uint8_t>(96));
  } else {
    if (vocal_center >= 66) {
      base_note = std::min(static_cast<uint8_t>(55), static_cast<uint8_t>(vocal_low - 7));
    } else {
      base_note = std::max(static_cast<uint8_t>(72), static_cast<uint8_t>(vocal_high + 5));
    }
  }

  int adjusted = base_note + register_offset;
  return static_cast<uint8_t>(std::clamp(adjusted, 36, 96));
}

int8_t getVocalDirection(const std::map<Tick, int8_t>* direction_at_tick, Tick tick) {
  if (!direction_at_tick || direction_at_tick->empty()) return 0;

  auto it = direction_at_tick->upper_bound(tick);
  if (it == direction_at_tick->begin()) return 0;
  --it;
  return it->second;
}

int applyContraryMotion(int pitch, int8_t vocal_direction, float strength, std::mt19937& rng) {
  if (vocal_direction == 0 || strength <= 0.0f) return pitch;

  if (!rng_util::rollProbability(rng, strength)) return pitch;

  int adjustment = rng_util::rollRange(rng, 1, 3) * (-vocal_direction);

  return pitch + adjustment;
}

}  // namespace motif_detail

// =============================================================================
// MotifGenerator Implementation
// =============================================================================

std::vector<NoteEvent> generateMotifPattern(const GeneratorParams& params, std::mt19937& rng) {
  const MotifParams& motif_params = params.motif;
  std::vector<NoteEvent> pattern;

  int key_offset = 0;
  uint8_t base_note = motif_params.register_high ? 67 : 60;

  // Determine whether to use template-based or legacy rhythm generation
  bool use_template = (motif_params.rhythm_template != MotifRhythmTemplate::None);

  std::vector<Tick> positions;
  uint8_t effective_note_count = motif_params.note_count;
  const motif_detail::MotifRhythmTemplateConfig* tmpl_config = nullptr;

  if (use_template) {
    tmpl_config = &motif_detail::getTemplateConfig(motif_params.rhythm_template);
    positions = motif_detail::generateRhythmPositionsFromTemplate(motif_params.rhythm_template);
    effective_note_count = tmpl_config->note_count;
  } else {
    positions = motif_detail::generateRhythmPositions(
        motif_params.rhythm_density, motif_params.length, motif_params.note_count, rng);
  }

  int max_leap_degrees = 7;
  bool prefer_stepwise = false;
  if (params.blueprint_ref != nullptr) {
    max_leap_degrees = (params.blueprint_ref->constraints.max_leap_semitones * 7 + 11) / 12;
    prefer_stepwise = params.blueprint_ref->constraints.prefer_stepwise;
  }

  std::vector<int> degrees = motif_detail::generatePitchSequence(
      effective_note_count, motif_params.motion, rng, max_leap_degrees, prefer_stepwise);

  uint8_t base_velocity = motif_params.velocity_fixed ? 80 : 75;

  size_t pitch_idx = 0;
  for (size_t i = 0; i < positions.size(); ++i) {
    Tick pos = positions[i];
    int degree = degrees[pitch_idx % degrees.size()];
    int pitch = degreeToPitch(degree, base_note, key_offset);
    pitch = std::clamp(pitch, 36, 96);

    // Calculate note duration: fill gap with articulation margin
    Tick note_duration;
    if (i + 1 < positions.size()) {
      Tick gap = positions[i + 1] - pos;
      // Fill gap with small articulation margin for natural note separation
      constexpr Tick kArticulationGap = 30;  // ~6% of 8th note (240 ticks)
      note_duration = (gap > kArticulationGap + TICK_SIXTEENTH)
                          ? gap - kArticulationGap
                          : gap;  // Very short gaps: fill completely
    } else {
      // Last note: fill to end of cycle with articulation
      Tick cycle_length = static_cast<Tick>(motif_params.length) * TICKS_PER_BAR;
      Tick gap_to_end = cycle_length - pos;
      constexpr Tick kArticulationGap = 30;
      note_duration = (gap_to_end > kArticulationGap + TICK_SIXTEENTH)
                          ? gap_to_end - kArticulationGap
                          : std::max(gap_to_end, static_cast<Tick>(TICK_SIXTEENTH));
    }

    // Calculate velocity from template accent weights
    uint8_t velocity = base_velocity;
    if (use_template && tmpl_config != nullptr && i < 16 && tmpl_config->accent_weights[i] >= 0) {
      // accent=1.0 → base_vel, accent=0.5 → base_vel * 0.775
      float accent = tmpl_config->accent_weights[i];
      velocity = static_cast<uint8_t>(base_velocity * (0.55f + accent * 0.45f));
    }

    pattern.push_back(
        createNoteWithoutHarmony(pos, note_duration, static_cast<uint8_t>(pitch), velocity));
    pitch_idx++;
  }

  return pattern;
}

// ============================================================================
// Motif Note Generation Helper
// ============================================================================

namespace {

/// @brief Context for generating a single motif note.
struct MotifNoteContext {
  Tick absolute_tick;          ///< Absolute position in song
  Tick section_end;            ///< End of current section
  uint8_t current_bar;         ///< Current bar within section
  uint8_t effective_density;   ///< Density after adjustments
  bool is_rhythm_lock_global;  ///< Whether in RhythmSync coordinate axis mode
  bool add_octave;             ///< Whether to add octave doubling
  uint8_t base_velocity;       ///< Base velocity from role meta
  MotifRole role;              ///< Motif role (Hook or Texture)
  SectionType section_type;    ///< Current section type for register variation
};

/// @brief Result of motif pitch calculation with transform tracking.
struct MotifPitchResult {
  int pitch;                    ///< Final adjusted pitch
  int section_octave_shift;     ///< Section-based octave shift (+12, -12, or 0)
  int range_octave_up;          ///< Octave up count for range clamping (multiples of 12)
  bool avoid_note_snapped;      ///< Whether avoid note snap was applied
};

/// @brief Calculate adjusted pitch for a motif note.
/// @param note Source note from pattern
/// @param ctx Note context
/// @param params Generator params
/// @param motif_params Motif-specific params
/// @param harmony Harmony context
/// @param vocal_ctx Optional vocal coordination context
/// @param base_note_override Base note override for vocal coordination
/// @param rng Random number generator
/// @return MotifPitchResult with adjusted pitch and transform info
MotifPitchResult calculateMotifPitch(const NoteEvent& note, const MotifNoteContext& ctx,
                                      const GeneratorParams& params,
                                      const MotifParams& motif_params,
                                      IHarmonyCoordinator* harmony, const MotifContext* vocal_ctx,
                                      uint8_t base_note_override, std::mt19937& rng) {
  MotifPitchResult result = {0, 0, 0, false};

  if (ctx.is_rhythm_lock_global) {
    // Coordinate axis mode: use pattern pitch directly, but snap avoid notes.
    // Non-chord tones (passing tones) are acceptable since Motif is the
    // coordinate axis - other tracks adapt to it.
    int pitch = static_cast<int>(note.note);

    // Section-based register variation for melodic diversity.
    // Chorus/Drop uses higher register, Bridge uses lower register.
    // Use moderate intervals (P5/P4) rather than full octaves to avoid
    // extremes that could clash with other tracks or hit ceiling.
    int octave_shift = 0;
    switch (ctx.section_type) {
      case SectionType::Chorus:
      case SectionType::Drop:
        octave_shift = 7;  // Perfect 5th up for energy
        break;
      case SectionType::Bridge:
        octave_shift = -5;  // Perfect 4th down for contrast
        break;
      default:
        break;  // Verse, Intro, Outro, Interlude use original register
    }
    pitch += octave_shift;
    result.section_octave_shift = octave_shift;

    // Handle pitches below MOTIF_LOW by shifting up an octave.
    // This prevents negative degrees from clamping to C4 and causing concentration.
    int range_octave_up = 0;
    while (pitch < static_cast<int>(MOTIF_LOW) && pitch + 12 <= static_cast<int>(MOTIF_HIGH)) {
      pitch += 12;
      range_octave_up += 12;
    }
    result.range_octave_up = range_octave_up;
    pitch = std::clamp(pitch, static_cast<int>(MOTIF_LOW), static_cast<int>(MOTIF_HIGH));

    // Coordinate axis: skip avoid note snapping.
    // Motif is generated first in RhythmSync - other tracks adapt to it.
    // Avoid snapping changes pitches per chord, breaking Locked riff consistency.

    result.pitch = pitch;
    return result;
  }

  // Standard mode: apply pitch adjustments
  int8_t degree = harmony->getChordDegreeAt(ctx.absolute_tick);
  uint8_t chord_root = degreeToRoot(degree, Key::C);
  Chord chord = getChordNotes(degree);
  bool is_minor = (chord.intervals[1] == 3);

  ScaleType scale = motif_detail::selectScaleType(is_minor, params.mood);
  int adjusted_pitch = motif_detail::adjustPitchToScale(note.note, 0, scale);
  adjusted_pitch = motif_detail::adjustForChord(adjusted_pitch, chord_root, is_minor, degree);

  if (vocal_ctx && motif_params.dynamic_register && base_note_override != 0) {
    int original_base = motif_params.register_high ? 67 : 60;
    int register_shift = static_cast<int>(base_note_override) - original_base;
    adjusted_pitch += register_shift;
  }

  if (vocal_ctx && motif_params.contrary_motion) {
    int8_t vocal_dir =
        motif_detail::getVocalDirection(vocal_ctx->direction_at_tick, ctx.absolute_tick);
    adjusted_pitch = motif_detail::applyContraryMotion(
        adjusted_pitch, vocal_dir, motif_params.contrary_motion_strength, rng);
  }

  adjusted_pitch = motif_detail::adjustToDiatonic(adjusted_pitch);

  while (adjusted_pitch < static_cast<int>(MOTIF_LOW) &&
         adjusted_pitch + 12 <= static_cast<int>(MOTIF_HIGH)) {
    adjusted_pitch += 12;
  }
  adjusted_pitch =
      std::clamp(adjusted_pitch, static_cast<int>(MOTIF_LOW), static_cast<int>(MOTIF_HIGH));

  if (params.paradigm == GenerationParadigm::RhythmSync) {
    int8_t degree_for_snap = harmony->getChordDegreeAt(ctx.absolute_tick);
    uint8_t chord_root_for_snap = degreeToRoot(degree_for_snap, Key::C);
    Chord chord_for_snap = getChordNotes(degree_for_snap);
    bool is_minor_for_snap = (chord_for_snap.intervals[1] == 3);
    adjusted_pitch = motif_detail::snapToSafeScaleTone(adjusted_pitch, chord_root_for_snap,
                                                        is_minor_for_snap, degree_for_snap,
                                                        motif_params.melodic_freedom, rng);
  }

  // Snap non-chord tones on strong beats to avoid close interval issues
  // Skip for RhythmSync: motif is the coordinate axis (generated first),
  // so there are no other tracks to clash with yet.
  bool is_strong_beat = (ctx.absolute_tick % TICKS_PER_BEAT == 0);
  if (is_strong_beat && params.paradigm != GenerationParadigm::RhythmSync) {
    ChordToneHelper ct_helper(degree);
    uint8_t clamped = static_cast<uint8_t>(std::clamp(adjusted_pitch, 0, 127));
    if (!ct_helper.isChordTone(clamped)) {
      adjusted_pitch = ct_helper.nearestInRange(clamped, MOTIF_LOW, MOTIF_HIGH);
    }
  }

  result.pitch = adjusted_pitch;
  return result;
}

/// @brief Calculate velocity for a motif note.
/// @param base_vel Base velocity from role meta
/// @param is_chorus Whether in chorus section
/// @param section_type Current section type
/// @param velocity_fixed Whether velocity is fixed
/// @return Adjusted velocity
uint8_t calculateMotifVelocity(uint8_t base_vel, bool is_chorus, SectionType section_type,
                                bool velocity_fixed) {
  if (velocity_fixed) {
    return base_vel;
  }

  if (is_chorus) {
    return std::min(static_cast<uint8_t>(127), static_cast<uint8_t>(base_vel + 10));
  } else if (isBookendSection(section_type)) {
    return static_cast<uint8_t>(base_vel * 0.85f);
  }
  return base_vel;
}

}  // namespace

void MotifGenerator::generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  if (!ctx.song || !ctx.params || !ctx.rng || !ctx.harmony) {
    return;
  }

  const auto& params = *ctx.params;
  std::mt19937& rng = *ctx.rng;
  IHarmonyCoordinator* harmony = ctx.harmony;
  const MotifContext* vocal_ctx = ctx.vocal_ctx;

  // L1: Generate base motif pattern
  std::vector<NoteEvent> pattern = generateMotifPattern(params, rng);
  ctx.song->setMotifPattern(pattern);

  if (pattern.empty()) return;

  const MotifParams& motif_params = params.motif;
  Tick motif_length = static_cast<Tick>(motif_params.length) * TICKS_PER_BAR;

  // L5 (Vocal Coordination Layer): Calculate vocal-aware base note if context provided
  uint8_t base_note_override = 0;
  if (vocal_ctx && motif_params.dynamic_register) {
    base_note_override = motif_detail::calculateMotifRegister(
        vocal_ctx->vocal_low, vocal_ctx->vocal_high, motif_params.register_high,
        motif_params.register_offset);
  }

  const auto& sections = ctx.song->arrangement().sections();

  // Vocal ceiling: restrict motif range_high to not exceed vocal's highest pitch.
  // Use global vocal highest (across entire song) since motif patterns repeat.
  uint8_t motif_range_high = MOTIF_HIGH;
  {
    Tick song_end = ctx.song->arrangement().totalTicks();
    uint8_t vocal_high = harmony->getHighestPitchForTrackInRange(0, song_end, TrackRole::Vocal);
    if (vocal_high > 0) {
      motif_range_high = std::min(static_cast<int>(MOTIF_HIGH), static_cast<int>(vocal_high));
    }
  }

  // M9: Determine motif role for this track
  MotifRole role = (params.composition_style == CompositionStyle::BackgroundMotif) ? MotifRole::Hook
                                                                                    : MotifRole::Texture;
  MotifRoleMeta role_meta = getMotifRoleMeta(role);

  // M4: Cache for section-specific patterns
  std::map<SectionType, std::vector<NoteEvent>> section_patterns;

  // RiffPolicy cache for Locked/Evolving modes
  MotifRiffCache riff_cache;
  riff_cache.pattern = pattern;
  size_t sec_idx = 0;

  // Monotony tracker for consecutive same pitch avoidance
  PitchMonotonyTracker monotony_tracker;

  // Check if this is RhythmLock mode (coordinate axis)
  RiffPolicy policy = params.riff_policy;
  bool is_locked = (policy == RiffPolicy::LockedContour || policy == RiffPolicy::LockedPitch ||
                    policy == RiffPolicy::LockedAll);
  bool is_rhythm_lock_global = (params.paradigm == GenerationParadigm::RhythmSync) && is_locked;

  // Monotony tracking for collision avoidance - persist across sections
  uint8_t motif_prev_pitch = 0;
  int motif_consecutive_same = 0;

  // Locked mode: cache actual output notes per section type for exact replay.
  // When is_locked && !is_rhythm_lock_global, density skipping, pitch
  // adjustments, monotony tracker state, and collision avoidance cause
  // different pitches in repeat sections. Caching the first occurrence's
  // output notes and replaying them verbatim ensures consistency.
  struct LockedNoteEntry {
    Tick relative_tick;  // Offset from section start
    Tick duration;
    uint8_t pitch;
    uint8_t velocity;
  };
  std::map<SectionType, std::vector<LockedNoteEntry>> locked_note_cache;
  // Separate cache for RhythmSync coordinate axis mode (is_rhythm_lock_global)
  // Uses NoCollisionCheck so cached pitches are replayed exactly as generated.
  std::map<SectionType, std::vector<LockedNoteEntry>> coord_axis_note_cache;

  for (const auto& section : sections) {
    if (!hasTrack(section.track_mask, TrackMask::Motif)) {
      sec_idx++;
      continue;
    }

    // Locked mode: replay cached notes for repeat section types
    if (is_locked && !is_rhythm_lock_global) {
      auto cache_it = locked_note_cache.find(section.type);
      if (cache_it != locked_note_cache.end()) {
        // Replay cached notes with tick offset
        for (const auto& entry : cache_it->second) {
          Tick absolute_tick = section.start_tick + entry.relative_tick;
          if (absolute_tick >= section.endTick()) continue;

          NoteOptions opts;
          opts.start = absolute_tick;
          opts.duration = entry.duration;
          opts.desired_pitch = entry.pitch;
          opts.velocity = entry.velocity;
          opts.role = TrackRole::Motif;
          opts.preference = PitchPreference::PreserveContour;
          opts.range_low = MOTIF_LOW;
          opts.range_high = motif_range_high;
          opts.source = NoteSource::Motif;
          opts.prev_pitch = motif_prev_pitch;
          opts.consecutive_same_count = motif_consecutive_same;

          auto result = createNoteAndAdd(track, *harmony, opts);
          if (result) {
            if (result->note == motif_prev_pitch) {
              motif_consecutive_same++;
            } else {
              motif_consecutive_same = 1;
            }
            motif_prev_pitch = result->note;
          }
        }
        sec_idx++;
        continue;
      }
    }

    // RhythmSync coordinate axis + Locked: replay cached notes verbatim
    // Uses NoCollisionCheck since coordinate axis tracks skip collision avoidance.
    if (is_rhythm_lock_global) {
      auto cache_it = coord_axis_note_cache.find(section.type);
      if (cache_it != coord_axis_note_cache.end()) {
        for (const auto& entry : cache_it->second) {
          Tick absolute_tick = section.start_tick + entry.relative_tick;
          if (absolute_tick >= section.endTick()) continue;

          NoteOptions opts;
          opts.start = absolute_tick;
          opts.duration = entry.duration;
          opts.desired_pitch = entry.pitch;
          opts.velocity = entry.velocity;
          opts.role = TrackRole::Motif;
          opts.preference = PitchPreference::NoCollisionCheck;
          opts.range_low = MOTIF_LOW;
          opts.range_high = motif_range_high;
          opts.source = NoteSource::Motif;
          createNoteAndAdd(track, *harmony, opts);
        }
        sec_idx++;
        continue;
      }
      if (!is_rhythm_lock_global) {
        // Reset monotony tracker at section boundary (not needed in coordinate axis
        // mode where monotony tracking is skipped entirely).
        monotony_tracker.reset();
      }
    }

    Tick section_end = section.endTick();
    bool is_chorus = (section.type == SectionType::Chorus);

    bool add_octave =
        is_chorus && motif_params.octave_layering_chorus && role_meta.allow_octave_layer;

    // L2: Determine which pattern to use based on RiffPolicy
    std::vector<NoteEvent>* current_pattern = &pattern;
    std::vector<NoteEvent> section_pattern;

    if (is_locked && riff_cache.cached) {
      current_pattern = &riff_cache.pattern;
    } else if (policy == RiffPolicy::Evolving && riff_cache.cached) {
      if (sec_idx % 2 == 0 && rng_util::rollProbability(rng, 0.3f)) {
        riff_cache.pattern = generateMotifPattern(params, rng);
      }
      current_pattern = &riff_cache.pattern;
    } else if (policy == RiffPolicy::Free) {
      if (motif_params.repeat_scope == MotifRepeatScope::Section) {
        auto it = section_patterns.find(section.type);
        if (it == section_patterns.end()) {
          if (rng_util::rollProbability(rng, role_meta.exact_repeat_prob)) {
            section_patterns[section.type] = pattern;
          } else {
            section_pattern = generateMotifPattern(params, rng);
            section_patterns[section.type] = section_pattern;
          }
          current_pattern = &section_patterns[section.type];
        } else {
          current_pattern = &it->second;
        }
      }
    } else {
      current_pattern = &riff_cache.pattern;
    }

    if (!riff_cache.cached) {
      riff_cache.cached = true;
    }

    // Repeat motif across the section
    for (Tick pos = section.start_tick; pos < section_end; pos += motif_length) {
      std::map<uint8_t, size_t> bar_note_count;

      for (const auto& note : *current_pattern) {
        Tick absolute_tick = pos + note.start_tick;
        if (absolute_tick >= section_end) continue;

        uint8_t current_bar = static_cast<uint8_t>((absolute_tick - pos) / TICKS_PER_BAR);

        // Apply density_percent to skip notes
        uint8_t effective_density = section.getModifiedDensity(section.density_percent);

        float density_mult = 1.0f;
        switch (section.getEffectiveBackingDensity()) {
          case BackingDensity::Thin:
            density_mult = 0.85f;
            break;
          case BackingDensity::Normal:
            break;
          case BackingDensity::Thick:
            density_mult = 1.10f;
            break;
        }
        effective_density = static_cast<uint8_t>(
            std::min(100.0f, static_cast<float>(effective_density) * density_mult));

        // In coordinate axis mode (RhythmLock), skip density/response thinning
        // to maintain riff consistency - the motif pattern should repeat exactly.
        bool should_skip = false;
        if (!is_rhythm_lock_global && effective_density < 100) {
          should_skip = (rng_util::rollFloat(rng, 0.0f, 100.0f) > effective_density);

          if (should_skip && bar_note_count[current_bar] == 0) {
            should_skip = false;
          }
        }
        if (should_skip) {
          continue;
        }

        // L5: Vocal Coordination - Response Mode (skip in coordinate axis mode)
        if (!is_rhythm_lock_global && vocal_ctx && motif_params.response_mode) {
          bool in_rest = motif_detail::isInVocalRest(absolute_tick, vocal_ctx->rest_positions);
          if (!in_rest) {
            float skip_prob = vocal_ctx->vocal_density * 0.4f;
            if (rng_util::rollProbability(rng, skip_prob) && bar_note_count[current_bar] > 0) {
              continue;
            }
          }
        }

        // Build note context for helper functions
        MotifNoteContext note_ctx;
        note_ctx.absolute_tick = absolute_tick;
        note_ctx.section_end = section_end;
        note_ctx.current_bar = current_bar;
        note_ctx.effective_density = effective_density;
        note_ctx.is_rhythm_lock_global = is_rhythm_lock_global;
        note_ctx.add_octave = add_octave;
        note_ctx.base_velocity = role_meta.velocity_base;
        note_ctx.role = role;
        note_ctx.section_type = section.type;

        // Calculate adjusted pitch using helper
        MotifPitchResult pitch_result =
            calculateMotifPitch(note, note_ctx, params, motif_params, harmony, vocal_ctx,
                                base_note_override, rng);

        // Clamp to vocal ceiling
        int adjusted_pitch = std::min(pitch_result.pitch, static_cast<int>(motif_range_high));

        // Calculate velocity: use pattern velocity for template mode (has accent weights),
        // otherwise use the standard helper.
        uint8_t vel;
        if (is_rhythm_lock_global &&
            motif_params.rhythm_template != MotifRhythmTemplate::None) {
          // Template mode: use pattern velocity (already has accent weighting)
          vel = note.velocity;
          if (is_chorus) {
            vel = std::min(static_cast<uint8_t>(127), static_cast<uint8_t>(vel + 10));
          }
        } else {
          vel = calculateMotifVelocity(role_meta.velocity_base, is_chorus, section.type,
                                       motif_params.velocity_fixed);
        }

        uint8_t final_pitch;
        if (is_rhythm_lock_global) {
          // Coordinate axis + Locked: use pitch as-is from pattern + section shift.
          // Monotony tracking would alter the locked pattern.
          final_pitch = static_cast<uint8_t>(adjusted_pitch);
        } else {
          // Apply monotony tracking to avoid consecutive same pitches
          // Pass chord degree so alternatives are selected from chord tones
          int8_t current_degree = harmony->getChordDegreeAt(absolute_tick);
          final_pitch = monotony_tracker.trackAndSuggest(
              static_cast<uint8_t>(adjusted_pitch), MOTIF_LOW, motif_range_high, current_degree);
        }

        if (is_rhythm_lock_global) {
          // Coordinate axis mode: add note directly with registration (no collision avoidance)
          NoteOptions opts;
          opts.start = absolute_tick;
          opts.duration = note.duration;
          opts.desired_pitch = final_pitch;
          opts.velocity = vel;
          opts.role = TrackRole::Motif;
          opts.preference = PitchPreference::NoCollisionCheck;  // Coordinate axis
          opts.range_low = MOTIF_LOW;
          opts.range_high = motif_range_high;
          opts.source = NoteSource::Motif;
          opts.original_pitch = note.note;  // Track pre-adjustment pitch

          auto added_note_opt = createNoteAndAdd(track, *harmony, opts);

          // Record transforms for provenance tracking
#ifdef MIDISKETCH_NOTE_PROVENANCE
          if (added_note_opt) {
            NoteEvent& added_note = track.notes().back();
            if (pitch_result.section_octave_shift != 0) {
              added_note.addTransformStep(
                  TransformStepType::OctaveAdjust, note.note,
                  static_cast<uint8_t>(note.note + pitch_result.section_octave_shift),
                  static_cast<int8_t>(pitch_result.section_octave_shift / 12), 0);
            }
            if (pitch_result.range_octave_up != 0) {
              uint8_t pre_range = static_cast<uint8_t>(
                  std::clamp(note.note + pitch_result.section_octave_shift, 0, 127));
              added_note.addTransformStep(
                  TransformStepType::OctaveAdjust, pre_range,
                  static_cast<uint8_t>(pre_range + pitch_result.range_octave_up),
                  static_cast<int8_t>(pitch_result.range_octave_up / 12), 1);
            }
            if (pitch_result.avoid_note_snapped) {
              uint8_t pre_snap = static_cast<uint8_t>(std::clamp(
                  note.note + pitch_result.section_octave_shift + pitch_result.range_octave_up, 0, 127));
              added_note.addTransformStep(TransformStepType::ChordToneSnap, pre_snap, final_pitch, 0, 0);
            }
          }
#else
          (void)added_note_opt;  // Suppress unused variable warning
#endif
          bar_note_count[current_bar]++;

          // Octave doubling in RhythmLock
          if (add_octave) {
            int octave_pitch = final_pitch + 12;
            if (octave_pitch <= 108) {
              uint8_t octave_vel = static_cast<uint8_t>(vel * 0.85f);
              NoteOptions octave_opts = opts;
              octave_opts.desired_pitch = static_cast<uint8_t>(octave_pitch);
              octave_opts.velocity = octave_vel;
              createNoteAndAdd(track, *harmony, octave_opts);
            }
          }
        } else {
          // Standard mode: use createNoteAndAdd with PreserveContour for collision avoidance
          constexpr Tick kSwingMargin = 120;
          Tick check_duration = note.duration + kSwingMargin;

          NoteOptions opts;
          opts.start = absolute_tick;
          opts.duration = check_duration;  // Include swing margin for collision check
          opts.desired_pitch = final_pitch;
          opts.velocity = vel;
          opts.role = TrackRole::Motif;
          opts.preference = PitchPreference::PreserveContour;  // Prefers octave shifts
          opts.range_low = MOTIF_LOW;
          opts.range_high = motif_range_high;
          opts.source = NoteSource::Motif;
          opts.chord_boundary = ChordBoundaryPolicy::ClipIfUnsafe;
          opts.original_pitch = note.note;  // Track pre-adjustment pitch
          opts.prev_pitch = motif_prev_pitch;
          opts.consecutive_same_count = motif_consecutive_same;

          auto motif_note = createNoteAndAdd(track, *harmony, opts);

          if (!motif_note) {
            continue;
          }

          // Update monotony tracker
          if (motif_note->note == motif_prev_pitch) {
            motif_consecutive_same++;
          } else {
            motif_consecutive_same = 1;
          }
          motif_prev_pitch = motif_note->note;

          bar_note_count[current_bar]++;

          // L4: Add octave doubling for chorus
          if (add_octave) {
            int octave_pitch = motif_note->note + 12;
            if (octave_pitch <= 108) {
              uint8_t octave_vel = static_cast<uint8_t>(vel * 0.85f);
              NoteOptions octave_opts;
              octave_opts.start = absolute_tick;
              octave_opts.duration = note.duration;
              octave_opts.desired_pitch = static_cast<uint8_t>(octave_pitch);
              octave_opts.velocity = octave_vel;
              octave_opts.role = TrackRole::Motif;
              octave_opts.preference = PitchPreference::SkipIfUnsafe;  // Optional layer
              octave_opts.range_low = MOTIF_LOW;
              octave_opts.range_high = 108;
              octave_opts.source = NoteSource::Motif;

              createNoteAndAdd(track, *harmony, octave_opts);
            }
          }
        }
      }
    }

    // Locked mode: cache output notes for this section type
    if (is_locked && !is_rhythm_lock_global &&
        locked_note_cache.find(section.type) == locked_note_cache.end()) {
      std::vector<LockedNoteEntry> entries;
      for (const auto& evt : track.notes()) {
        if (evt.start_tick >= section.start_tick && evt.start_tick < section.endTick()) {
          entries.push_back({
            evt.start_tick - section.start_tick,
            evt.duration,
            evt.note,
            evt.velocity
          });
        }
      }
      if (!entries.empty()) {
        locked_note_cache[section.type] = std::move(entries);
      }
    }

    // RhythmSync coordinate axis + Locked: cache output notes for replay
    if (is_rhythm_lock_global &&
        coord_axis_note_cache.find(section.type) == coord_axis_note_cache.end()) {
      std::vector<LockedNoteEntry> entries;
      for (const auto& evt : track.notes()) {
        if (evt.start_tick >= section.start_tick && evt.start_tick < section.endTick()) {
          entries.push_back({
            evt.start_tick - section.start_tick,
            evt.duration,
            evt.note,
            evt.velocity
          });
        }
      }
      if (!entries.empty()) {
        coord_axis_note_cache[section.type] = std::move(entries);
      }
    }

    sec_idx++;
  }
}

}  // namespace midisketch
