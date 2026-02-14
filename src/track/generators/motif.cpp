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
    // StraightSixteenth: 16 notes, straight 16ths (1 bar)
    {{0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f, 2.75f, 3.0f, 3.25f, 3.5f, 3.75f},
     {1.0f, 0.5f, 0.7f, 0.5f, 1.0f, 0.5f, 0.7f, 0.5f, 1.0f, 0.5f, 0.7f, 0.5f, 1.0f, 0.5f, 0.7f, 0.5f},
     16, MotifRhythmDensity::Driving},
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
  //        PushGroove, EighthPickup, HalfNoteSparse, StraightSixteenth
  constexpr int kTemplateCount = 9;
  struct TemplateWeights {
    int weights[kTemplateCount];
  };

  TemplateWeights w;
  if (bpm >= 160) {
    // Fast (Orangestar core): StraightSixteenth adds driving energy
    w = {{22, 18, 7, 6, 6, 10, 10, 8, 13}};
  } else if (bpm >= 130) {
    // Medium: StraightSixteenth used moderately
    w = {{20, 10, 12, 10, 9, 8, 7, 16, 8}};
  } else {
    // Slow: sparse patterns shine at low BPM, StraightSixteenth rare
    w = {{12, 4, 20, 15, 15, 8, 4, 19, 3}};
  }

  int total = 0;
  for (int i = 0; i < kTemplateCount; ++i) total += w.weights[i];

  int roll = rng_util::rollRange(rng, 0, total - 1);
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

// Adjust pitch to nearest diatonic scale tone, rounding toward range_center
// to distribute pitches more evenly across the range.
int adjustToDiatonicTowardCenter(int pitch, int range_center) {
  if (isDiatonic(pitch)) {
    return pitch;
  }
  // Try rounding toward center first, then away if result is not diatonic
  int toward_center = (pitch < range_center) ? pitch + 1 : pitch - 1;
  if (isDiatonic(toward_center)) {
    return toward_center;
  }
  // Toward-center wasn't diatonic, try the other direction
  int away_from_center = (pitch < range_center) ? pitch - 1 : pitch + 1;
  if (isDiatonic(away_from_center)) {
    return away_from_center;
  }
  // Fallback to original fixed-direction logic
  return adjustToDiatonic(pitch);
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

  // Ostinato: static harmonic foundation - root with 5th/octave variation
  if (motion == MotifMotion::Ostinato) {
    for (uint8_t idx = 0; idx < note_count; ++idx) {
      if (idx % 2 == 0) {
        degrees.push_back(0);  // Root at base octave
      } else {
        // Odd notes: 5th (degree 4) or octave (degree 7)
        degrees.push_back(rng_util::rollRange(rng, 0, 1) ? 4 : 7);
      }
    }
    return degrees;
  }

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
      case MotifMotion::Ostinato:
        break;  // Handled by early return above
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
      case MotifMotion::Ostinato:
        break;  // Handled by early return above
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
      base_note = static_cast<uint8_t>(std::min(55, std::max(0, static_cast<int>(vocal_low) - 7)));
    } else {
      base_note = static_cast<uint8_t>(std::max(72, std::min(127, static_cast<int>(vocal_high) + 5)));
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

    auto note = createNoteWithoutHarmony(pos, note_duration, static_cast<uint8_t>(pitch), velocity);
#ifdef MIDISKETCH_NOTE_PROVENANCE
    note.prov_source = static_cast<uint8_t>(NoteSource::Motif);
    note.prov_lookup_tick = pos;
    note.prov_original_pitch = static_cast<uint8_t>(pitch);
#endif
    pattern.push_back(note);
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
  uint8_t motif_range_high;    ///< Effective upper range limit (vocal-aware)
  uint8_t motif_range_low;     ///< Effective lower range limit (vocal-aware)
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
    // Coordinate axis mode: preserve riff shape via cycle-unit diatonic transposition,
    // then correct avoid notes against the current chord.
    int pitch = static_cast<int>(note.note);

    // --- Step 1: Cycle-unit diatonic transposition ---
    // Use chord at riff cycle start to transpose the entire riff diatonically.
    // This preserves the riff's interval relationships while following the chord progression.
    Tick cycle_start = ctx.absolute_tick - note.start_tick;
    int8_t cycle_degree = harmony->getChordDegreeAt(cycle_start);
    uint8_t cycle_root_midi = degreeToRoot(cycle_degree, Key::C);
    int cycle_root_pc = static_cast<int>(cycle_root_midi) % 12;
    static constexpr int kSemitoneToDegree[] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    int chord_scale_degree = kSemitoneToDegree[cycle_root_pc];

    if (chord_scale_degree != 0) {
      int base_note = motif_params.register_high ? 67 : 60;
      int original_degree = pitchToMajorDegree(pitch, base_note);
      int transposed_degree = original_degree + chord_scale_degree;
      pitch = degreeToPitch(transposed_degree, base_note, 0);
    }

    // --- Step 2: Dynamic register separation ---
    // base_note_override is computed from calculateMotifRegister() using
    // config-based vocal range (not vocal analysis, since Motif is generated first).
    if (base_note_override != 0) {
      int pattern_base = motif_params.register_high ? 67 : 60;
      int register_shift = static_cast<int>(base_note_override) - pattern_base;
      pitch += register_shift;
    }

    // --- Step 3: Section-based register variation ---
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
    // Reduce shift if it would push pitch into ceiling (causes pitch concentration)
    constexpr int kCeilingMargin = 5;
    int effective_high = static_cast<int>(ctx.motif_range_high);
    int effective_low = static_cast<int>(ctx.motif_range_low);
    if (octave_shift > 0 && pitch + octave_shift > effective_high - kCeilingMargin) {
      octave_shift = std::max(0, effective_high - kCeilingMargin - pitch);
    }
    pitch += octave_shift;
    result.section_octave_shift = octave_shift;

    // --- Step 4: Snap to diatonic after shifts ---
    // Register and section shifts may introduce non-diatonic pitches.
    // Round toward range center to distribute pitches more evenly.
    int range_center = (effective_low + effective_high) / 2;
    pitch = motif_detail::adjustToDiatonicTowardCenter(pitch, range_center);

    // --- Step 5: Octave fold-down (if above range), then fold-up, and clamp ---
    // Fold down first: pitches above motif_range_high are folded into range.
    while (pitch > effective_high && pitch - 12 >= effective_low) {
      pitch -= 12;
    }
    int range_octave_up = 0;
    while (pitch < effective_low && pitch + 12 <= effective_high) {
      pitch += 12;
      range_octave_up += 12;
    }
    result.range_octave_up = range_octave_up;
    pitch = std::clamp(pitch, effective_low, effective_high);

    // --- Step 6: Avoid note correction ---
    // Check against current chord (not cycle start chord) to handle mid-riff
    // chord changes. Use nearestInRange to resolve within valid range,
    // preventing clamp->avoid->clamp cycles.
    int8_t current_degree = harmony->getChordDegreeAt(ctx.absolute_tick);
    uint8_t current_root = degreeToRoot(current_degree, Key::C);
    Chord current_chord = getChordNotes(current_degree);
    bool current_is_minor = (current_chord.intervals[1] == 3);
    int pre_avoid = pitch;
    if (isAvoidNoteWithContext(pitch, current_root, current_is_minor, current_degree)) {
      ChordToneHelper ct_helper(current_degree);
      pitch = ct_helper.nearestInRange(
          static_cast<uint8_t>(std::clamp(pitch, 0, 127)),
          ctx.motif_range_low, ctx.motif_range_high);
    }
    result.avoid_note_snapped = (pitch != pre_avoid);

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

  {
    int std_high = static_cast<int>(ctx.motif_range_high);
    int std_low = static_cast<int>(ctx.motif_range_low);
    int std_center = (std_low + std_high) / 2;
    adjusted_pitch = motif_detail::adjustToDiatonicTowardCenter(adjusted_pitch, std_center);

    // Fold down first: pitches above range_high are folded into range
    while (adjusted_pitch > std_high && adjusted_pitch - 12 >= std_low) {
      adjusted_pitch -= 12;
    }
    while (adjusted_pitch < std_low && adjusted_pitch + 12 <= std_high) {
      adjusted_pitch += 12;
    }
    adjusted_pitch = std::clamp(adjusted_pitch, std_low, std_high);
  }

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
      adjusted_pitch = ct_helper.nearestInRange(clamped, ctx.motif_range_low, ctx.motif_range_high);
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
/// @param is_phrase_start Whether this is the first note in a phrase cycle
/// @param is_phrase_end Whether this is the last note in a phrase cycle
/// @return Adjusted velocity
uint8_t calculateMotifVelocity(uint8_t base_vel, bool is_chorus, SectionType section_type,
                                bool velocity_fixed, bool is_phrase_start = false,
                                bool is_phrase_end = false) {
  int vel = static_cast<int>(base_vel);

  if (!velocity_fixed) {
    if (is_chorus) {
      vel += 10;
    } else if (isBookendSection(section_type)) {
      vel = static_cast<int>(base_vel * 0.85f);
    }
  }

  // Phrase-shaped micro-variation (applied even when velocity_fixed)
  if (is_phrase_start) {
    vel += 3;  // Attack feel at phrase head
  } else if (is_phrase_end) {
    vel -= 2;  // Tail fade at phrase end
  }

  // Section type adjustment for expressiveness
  if (section_type == SectionType::Chorus) {
    vel += 5;
  } else if (section_type == SectionType::Bridge) {
    vel -= 3;
  }

  return static_cast<uint8_t>(std::clamp(vel, 30, 127));
}

/// @brief Select best alternative pitch when consecutive same pitch threshold exceeded.
/// Prefers nearby chord tones with different pitch class from current pitch.
/// @param current_pitch The repeated pitch to escape from
/// @param harmony Harmony context for chord tone lookup
/// @param tick Current tick for chord degree lookup
/// @param range_low Minimum allowed pitch
/// @param range_high Maximum allowed pitch
/// @return Alternative pitch, or current_pitch if no better option found
uint8_t selectBestAlternative(uint8_t current_pitch, IHarmonyCoordinator* harmony,
                               Tick tick, uint8_t range_low, uint8_t range_high) {
  int8_t degree = harmony->getChordDegreeAt(tick);
  ChordToneHelper ct_helper(degree);
  const auto& pitch_classes = ct_helper.pitchClasses();
  int current_pc = current_pitch % 12;
  int current_octave = current_pitch / 12;

  uint8_t best = current_pitch;
  int best_dist = 127;

  for (int ct_pc : pitch_classes) {
    if (ct_pc == current_pc) continue;
    // Try octaves near current pitch
    for (int oct = current_octave - 1; oct <= current_octave + 1; ++oct) {
      if (oct < 0 || oct > 10) continue;
      int candidate = oct * 12 + static_cast<int>(ct_pc);
      if (candidate < range_low || candidate > range_high) continue;
      int dist = std::abs(candidate - static_cast<int>(current_pitch));
      if (dist > 0 && dist < best_dist) {
        best_dist = dist;
        best = static_cast<uint8_t>(candidate);
      }
    }
  }
  return best;
}

/// @brief Deterministic hash for variation without RNG consumption.
/// Combines seed, section, cycle, and note indices into a pseudo-random value.
uint32_t motifVariationHash(uint32_t seed, size_t section_idx,
                             size_t cycle_idx, size_t onset_idx) {
  return seed ^ (static_cast<uint32_t>(section_idx) * 2654435761u)
             ^ (static_cast<uint32_t>(cycle_idx) * 40499u)
             ^ static_cast<uint32_t>(onset_idx);
}

/// @brief Cached note entry for Locked/RhythmSync replay.
struct LockedNoteEntry {
  Tick relative_tick;  // Offset from section start
  Tick duration;
  uint8_t pitch;
  uint8_t velocity;
};

/// @brief Shared state for motif generation across sections.
struct MotifGenerationState {
  uint8_t motif_prev_pitch = 0;
  int motif_consecutive_same = 0;
  PitchMonotonyTracker monotony_tracker;
  std::map<SectionType, std::vector<LockedNoteEntry>> locked_note_cache;
  std::map<SectionType, std::vector<LockedNoteEntry>> coord_axis_note_cache;
  std::map<SectionType, std::vector<NoteEvent>> section_patterns;
  MotifRiffCache riff_cache;
  size_t sec_idx = 0;
};

/// @brief Check if a pitch is a chord tone at the given tick.
/// @param pitch MIDI pitch
/// @param harmony Harmony context for chord degree lookup
/// @param tick Tick for chord lookup
/// @return true if pitch class matches a chord tone at the tick's chord
bool isChordToneAtTick(uint8_t pitch, IHarmonyCoordinator* harmony, Tick tick) {
  int8_t degree = harmony->getChordDegreeAt(tick);
  ChordToneHelper ct_helper(degree);
  return ct_helper.isChordTone(pitch);
}

/// @brief Replay cached notes for Locked mode (non-coordinate-axis).
/// @return true if notes were replayed (section should be skipped), false otherwise
bool replayCachedNotesLocked(MidiTrack& track, const Section& section,
                              IHarmonyCoordinator* harmony,
                              MotifGenerationState& state,
                              uint8_t motif_range_high,
                              uint8_t motif_range_low,
                              const MotifParams& motif_params) {
  (void)motif_params;  // Reserved for future use
  auto cache_it = state.locked_note_cache.find(section.type);
  if (cache_it == state.locked_note_cache.end()) {
    return false;
  }

  // Replay cached notes with tick offset
  for (const auto& entry : cache_it->second) {
    Tick absolute_tick = section.start_tick + entry.relative_tick;
    if (absolute_tick >= section.endTick()) continue;

    // Two-stage strategy for consistency:
    // - If cached pitch is safe AND a chord tone at replay tick: keep as-is (100% consistency)
    // - Otherwise: use PreserveContour to resolve while preserving melodic shape
    bool cached_pitch_safe = harmony->isConsonantWithOtherTracks(
        entry.pitch, absolute_tick, entry.duration, TrackRole::Motif);
    bool is_chord_tone_at_replay = isChordToneAtTick(entry.pitch, harmony, absolute_tick);

    NoteOptions opts;
    opts.start = absolute_tick;
    opts.duration = entry.duration;
    opts.desired_pitch = entry.pitch;
    opts.velocity = entry.velocity;
    opts.role = TrackRole::Motif;
    if (cached_pitch_safe && is_chord_tone_at_replay) {
      opts.preference = PitchPreference::NoCollisionCheck;
    } else {
      opts.preference = PitchPreference::PreserveContour;
    }
    opts.range_low = motif_range_low;
    opts.range_high = motif_range_high;
    opts.source = NoteSource::Motif;
    opts.prev_pitch = state.motif_prev_pitch;
    opts.consecutive_same_count = state.motif_consecutive_same;

    auto result = createNoteAndAdd(track, *harmony, opts);
    if (result) {
      if (result->note == state.motif_prev_pitch) {
        state.motif_consecutive_same++;
      } else {
        state.motif_consecutive_same = 1;
      }
      state.motif_prev_pitch = result->note;
    }
  }
  return true;
}

/// @brief Replay cached notes for RhythmSync coordinate axis mode.
/// @return true if notes were replayed, false otherwise
bool replayCachedNotesCoordinateAxis(MidiTrack& track, const Section& section,
                                       IHarmonyCoordinator* harmony,
                                       MotifGenerationState& state,
                                       uint8_t motif_range_high,
                                       uint8_t motif_range_low) {
  auto cache_it = state.coord_axis_note_cache.find(section.type);
  if (cache_it == state.coord_axis_note_cache.end()) {
    return false;
  }

  for (const auto& entry : cache_it->second) {
    Tick absolute_tick = section.start_tick + entry.relative_tick;
    if (absolute_tick >= section.endTick()) continue;

    // Re-apply avoid note correction for the replay position's chord.
    // Use nearestInRange to stay within range while avoiding the note.
    int replay_pitch = static_cast<int>(entry.pitch);
    int8_t replay_degree = harmony->getChordDegreeAt(absolute_tick);
    uint8_t replay_root = degreeToRoot(replay_degree, Key::C);
    Chord replay_chord = getChordNotes(replay_degree);
    bool replay_minor = (replay_chord.intervals[1] == 3);
    if (isAvoidNoteWithContext(replay_pitch, replay_root, replay_minor, replay_degree)) {
      ChordToneHelper ct_helper(replay_degree);
      replay_pitch = ct_helper.nearestInRange(
          static_cast<uint8_t>(replay_pitch), motif_range_low, motif_range_high);
    }

    NoteOptions opts;
    opts.start = absolute_tick;
    opts.duration = entry.duration;
    opts.desired_pitch = static_cast<uint8_t>(replay_pitch);
    opts.velocity = entry.velocity;
    opts.role = TrackRole::Motif;
    opts.preference = PitchPreference::NoCollisionCheck;
    opts.range_low = motif_range_low;
    opts.range_high = motif_range_high;
    opts.source = NoteSource::Motif;
    createNoteAndAdd(track, *harmony, opts);
  }
  return true;
}

/// @brief Generate motif notes for a single section.
void generateMotifForSection(MidiTrack& track, const Section& section,
                               const FullTrackContext& ctx,
                               const GeneratorParams& params,
                               MotifGenerationState& state,
                               std::vector<NoteEvent>& pattern,
                               bool is_locked, bool is_rhythm_lock_global,
                               RiffPolicy policy,
                               uint8_t base_note_override,
                               uint8_t motif_range_high,
                               uint8_t motif_range_low,
                               MotifRole role, const MotifRoleMeta& role_meta) {
  std::mt19937& rng = *ctx.rng;
  IHarmonyCoordinator* harmony = ctx.harmony;
  const MotifContext* vocal_ctx = ctx.vocal_ctx;
  const MotifParams& motif_params = params.motif;
  Tick motif_length = static_cast<Tick>(motif_params.length) * TICKS_PER_BAR;

  if (!is_rhythm_lock_global) {
    // Reset monotony tracker at section boundary (not needed in coordinate axis
    // mode where monotony tracking is skipped entirely).
    // Note: This was originally inside the is_rhythm_lock_global block with a
    // dead !is_rhythm_lock_global check. The intent is to reset only for
    // non-coordinate-axis mode, which is handled here.
  }

  Tick section_end = section.endTick();
  bool is_chorus = (section.type == SectionType::Chorus);

  bool add_octave =
      is_chorus && motif_params.octave_layering_chorus && role_meta.allow_octave_layer;

  // motif_motion_hint override: generate section-specific pattern with hinted motion
  std::vector<NoteEvent> hint_pattern;
  if (section.motif_motion_hint > 0) {
    GeneratorParams hint_params = params;
    hint_params.motif.motion =
        static_cast<MotifMotion>(section.motif_motion_hint - 1);
    hint_pattern = generateMotifPattern(hint_params, rng);
  }

  // L2: Determine which pattern to use based on RiffPolicy
  std::vector<NoteEvent>* current_pattern = &pattern;
  std::vector<NoteEvent> section_pattern;

  if (is_locked && state.riff_cache.cached) {
    current_pattern = &state.riff_cache.pattern;
  } else if (policy == RiffPolicy::Evolving && state.riff_cache.cached) {
    if (state.sec_idx % 2 == 0 && rng_util::rollProbability(rng, 0.3f)) {
      state.riff_cache.pattern = generateMotifPattern(params, rng);
    }
    current_pattern = &state.riff_cache.pattern;
  } else if (policy == RiffPolicy::Free) {
    if (motif_params.repeat_scope == MotifRepeatScope::Section) {
      auto iter = state.section_patterns.find(section.type);
      if (iter == state.section_patterns.end()) {
        if (rng_util::rollProbability(rng, role_meta.exact_repeat_prob)) {
          state.section_patterns[section.type] = pattern;
        } else {
          section_pattern = generateMotifPattern(params, rng);
          state.section_patterns[section.type] = section_pattern;
        }
        current_pattern = &state.section_patterns[section.type];
      } else {
        current_pattern = &iter->second;
      }
    }
  } else {
    current_pattern = &state.riff_cache.pattern;
  }

  if (!state.riff_cache.cached) {
    state.riff_cache.cached = true;
  }

  // Override pattern with motif_motion_hint if set
  if (!hint_pattern.empty()) {
    current_pattern = &hint_pattern;
  }

  // Repeat motif across the section
  size_t cycle_idx = 0;
  for (Tick pos = section.start_tick; pos < section_end; pos += motif_length, ++cycle_idx) {
    std::map<uint8_t, size_t> bar_note_count;

    size_t onset_idx = 0;
    for (const auto& note : *current_pattern) {
      Tick absolute_tick = pos + note.start_tick;
      if (absolute_tick >= section_end) { ++onset_idx; continue; }

      // Hash-based note omission for variation (non-coordinate-axis, cycle > 0)
      if (cycle_idx > 0 && !is_rhythm_lock_global) {
        Tick pos_in_bar = absolute_tick % TICKS_PER_BAR;
        if (pos_in_bar > 0) {  // Not beat 1
          uint32_t skip_hash = motifVariationHash(params.seed, state.sec_idx, cycle_idx, onset_idx);
          if ((skip_hash % 100) < 8) {
            ++onset_idx;
            continue;
          }
        }
      }

      uint8_t current_bar = static_cast<uint8_t>(tickToBar(absolute_tick - pos));

      // Phrase tail rest: skip ~50% of notes in the last bar, reduce in penultimate
      if (section.phrase_tail_rest) {
        uint8_t section_bar = static_cast<uint8_t>(
            tickToBar(absolute_tick - section.start_tick));
        if (isPhraseTail(section_bar, section.bars)) {
          if (isLastBar(section_bar, section.bars)) {
            // Last bar: skip notes in the second half of the bar
            Tick bar_start = section.start_tick + section_bar * TICKS_PER_BAR;
            Tick bar_half = bar_start + TICKS_PER_BAR / 2;
            if (absolute_tick >= bar_half) { ++onset_idx; continue; }
          }
        }
      }

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
        ++onset_idx;
        continue;
      }

      // L5: Vocal Coordination - Response Mode (skip in coordinate axis mode)
      if (!is_rhythm_lock_global && vocal_ctx && motif_params.response_mode) {
        bool in_rest = motif_detail::isInVocalRest(absolute_tick, vocal_ctx->rest_positions);
        if (!in_rest) {
          float skip_prob = vocal_ctx->vocal_density * 0.4f;
          if (rng_util::rollProbability(rng, skip_prob) && bar_note_count[current_bar] > 0) {
            ++onset_idx;
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
      note_ctx.motif_range_high = motif_range_high;
      note_ctx.motif_range_low = motif_range_low;

      // Calculate adjusted pitch using helper
      MotifPitchResult pitch_result =
          calculateMotifPitch(note, note_ctx, params, motif_params, harmony, vocal_ctx,
                              base_note_override, rng);

      // Clamp to vocal ceiling
      int adjusted_pitch = std::min(pitch_result.pitch, static_cast<int>(motif_range_high));

      // Re-apply avoid note correction after vocal ceiling clamp, since clamping
      // may have changed the pitch to an avoid note for the current chord.
      // Use nearestInRange to find a chord tone within the valid range, avoiding
      // the clamp->avoid->clamp cycle that adjustForChord + clamp would cause.
      if (is_rhythm_lock_global) {
        int8_t post_degree = harmony->getChordDegreeAt(absolute_tick);
        uint8_t post_root = degreeToRoot(post_degree, Key::C);
        Chord post_chord = getChordNotes(post_degree);
        bool post_minor = (post_chord.intervals[1] == 3);
        if (isAvoidNoteWithContext(adjusted_pitch, post_root, post_minor, post_degree)) {
          ChordToneHelper ct_helper(post_degree);
          adjusted_pitch = ct_helper.nearestInRange(
              static_cast<uint8_t>(adjusted_pitch), motif_range_low, motif_range_high);
        }
      }

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
        bool phrase_start = (onset_idx == 0);
        bool phrase_end = (onset_idx + 1 == current_pattern->size());
        vel = calculateMotifVelocity(role_meta.velocity_base, is_chorus, section.type,
                                     motif_params.velocity_fixed, phrase_start, phrase_end);
      }

      // Hash-based velocity micro-variation for repeated cycles (non-beat-1 notes)
      if (cycle_idx > 0) {
        Tick pos_in_bar = absolute_tick % TICKS_PER_BAR;
        if (pos_in_bar > 0) {
          uint32_t var_hash = motifVariationHash(params.seed, state.sec_idx, cycle_idx, onset_idx);
          int vel_offset = static_cast<int>(var_hash % 11) - 5;  // -5 to +5
          vel = static_cast<uint8_t>(std::clamp(static_cast<int>(vel) + vel_offset, 30, 127));
        }
      }

      uint8_t final_pitch;
      if (is_rhythm_lock_global) {
        // Coordinate axis + Locked: use pitch as-is from pattern + section shift.
        // Safety valve: if same pitch repeated > 8 times, select chord tone alternative.
        final_pitch = static_cast<uint8_t>(adjusted_pitch);
        constexpr int kCoordAxisMonotonyThreshold = 8;
        if (final_pitch == state.motif_prev_pitch) {
          state.motif_consecutive_same++;
        } else {
          state.motif_consecutive_same = 1;
        }
        if (state.motif_consecutive_same > kCoordAxisMonotonyThreshold) {
          final_pitch = selectBestAlternative(
              final_pitch, harmony, absolute_tick, motif_range_low, motif_range_high);
        }
        state.motif_prev_pitch = final_pitch;
      } else {
        // Apply monotony tracking to avoid consecutive same pitches
        // Pass chord degree so alternatives are selected from chord tones
        int8_t current_degree = harmony->getChordDegreeAt(absolute_tick);
        final_pitch = state.monotony_tracker.trackAndSuggest(
            static_cast<uint8_t>(adjusted_pitch), motif_range_low, motif_range_high, current_degree);
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
        opts.range_low = motif_range_low;
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
        opts.range_low = motif_range_low;
        opts.range_high = motif_range_high;
        opts.source = NoteSource::Motif;
        opts.chord_boundary = ChordBoundaryPolicy::ClipIfUnsafe;
        opts.original_pitch = note.note;  // Track pre-adjustment pitch
        opts.prev_pitch = state.motif_prev_pitch;
        opts.consecutive_same_count = state.motif_consecutive_same;

        auto motif_note = createNoteAndAdd(track, *harmony, opts);

        if (!motif_note) {
          ++onset_idx;
          continue;
        }

        // Update monotony tracker
        if (motif_note->note == state.motif_prev_pitch) {
          state.motif_consecutive_same++;
        } else {
          state.motif_consecutive_same = 1;
        }
        state.motif_prev_pitch = motif_note->note;

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
            octave_opts.range_low = motif_range_low;
            octave_opts.range_high = 108;
            octave_opts.source = NoteSource::Motif;

            createNoteAndAdd(track, *harmony, octave_opts);
          }
        }
      }
      ++onset_idx;
    }
  }

  // Locked mode: cache output notes for this section type
  if (is_locked && !is_rhythm_lock_global &&
      state.locked_note_cache.find(section.type) == state.locked_note_cache.end()) {
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
      state.locked_note_cache[section.type] = std::move(entries);
    }
  }

  // RhythmSync coordinate axis + Locked: cache output notes for replay
  if (is_rhythm_lock_global &&
      state.coord_axis_note_cache.find(section.type) == state.coord_axis_note_cache.end()) {
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
      state.coord_axis_note_cache[section.type] = std::move(entries);
    }
  }
}

}  // namespace

void MotifGenerator::doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  const auto& params = *ctx.params;
  std::mt19937& rng = *ctx.rng;
  IHarmonyCoordinator* harmony = ctx.harmony;
  const MotifContext* vocal_ctx = ctx.vocal_ctx;

  // L1: Generate base motif pattern
  std::vector<NoteEvent> pattern = generateMotifPattern(params, rng);
  ctx.song->setMotifPattern(pattern);

  if (pattern.empty()) return;

  const MotifParams& motif_params = params.motif;

  // L5 (Vocal Coordination Layer): Calculate vocal-aware base note if context provided
  uint8_t base_note_override = 0;
  if (vocal_ctx && motif_params.dynamic_register) {
    base_note_override = motif_detail::calculateMotifRegister(
        vocal_ctx->vocal_low, vocal_ctx->vocal_high, motif_params.register_high,
        motif_params.register_offset);
  }

  const auto& sections = ctx.song->arrangement().sections();

  // Vocal median basis: restrict motif range using vocal median rather than
  // vocal ceiling alone. This prevents pitch concentration at the top of
  // the motif range (e.g., C4/D4/E4 when vocal ceiling is low).
  uint8_t motif_range_high = MOTIF_HIGH;
  uint8_t motif_range_low = MOTIF_LOW;
  {
    Tick song_end = ctx.song->arrangement().totalTicks();
    uint8_t vocal_high = harmony->getHighestPitchForTrackInRange(0, song_end, TrackRole::Vocal);
    uint8_t vocal_low = harmony->getLowestPitchForTrackInRange(0, song_end, TrackRole::Vocal);
    if (vocal_high > 0 && vocal_low > 0) {
      // Use actual vocal data from harmony context (available when vocal is generated first)
      int vocal_median = (static_cast<int>(vocal_low) + static_cast<int>(vocal_high)) / 2;
      motif_range_high = static_cast<uint8_t>(
          std::min(static_cast<int>(MOTIF_HIGH), vocal_median + 3));
      // Guard against Chord/Bass interference in low register
      motif_range_low = static_cast<uint8_t>(
          std::max(55, vocal_median - 15));
    } else if (vocal_ctx) {
      // Fallback: use config-based vocal range from MotifContext
      // (for RhythmSync where motif is generated before vocal)
      int vocal_median = (static_cast<int>(vocal_ctx->vocal_low) +
                          static_cast<int>(vocal_ctx->vocal_high)) / 2;
      motif_range_high = static_cast<uint8_t>(
          std::min(static_cast<int>(MOTIF_HIGH), vocal_median + 3));
      motif_range_low = static_cast<uint8_t>(
          std::max(55, vocal_median - 15));
    }
  }

  // M9: Determine motif role for this track
  MotifRole role = (params.composition_style == CompositionStyle::BackgroundMotif) ? MotifRole::Hook
                                                                                    : MotifRole::Texture;
  MotifRoleMeta role_meta = getMotifRoleMeta(role);

  // Initialize shared generation state
  MotifGenerationState state;
  state.riff_cache.pattern = pattern;

  // Check if this is RhythmLock mode (coordinate axis)
  RiffPolicy policy = params.riff_policy;
  bool is_locked = (policy == RiffPolicy::LockedContour || policy == RiffPolicy::LockedPitch ||
                    policy == RiffPolicy::LockedAll);
  bool is_rhythm_lock_global = (params.paradigm == GenerationParadigm::RhythmSync) && is_locked;

  for (const auto& section : sections) {
    if (shouldSkipSection(section)) {
      state.sec_idx++;
      continue;
    }

    // Locked mode: replay cached notes for repeat section types
    if (is_locked && !is_rhythm_lock_global) {
      if (replayCachedNotesLocked(track, section, harmony, state,
                                   motif_range_high, motif_range_low, motif_params)) {
        state.sec_idx++;
        continue;
      }
    }

    // RhythmSync coordinate axis + Locked: replay cached notes with avoid correction.
    if (is_rhythm_lock_global) {
      if (replayCachedNotesCoordinateAxis(track, section, harmony, state,
                                            motif_range_high, motif_range_low)) {
        state.sec_idx++;
        continue;
      }
    }

    generateMotifForSection(track, section, ctx, params, state, pattern,
                              is_locked, is_rhythm_lock_global, policy,
                              base_note_override, motif_range_high, motif_range_low,
                              role, role_meta);

    state.sec_idx++;
  }
  // Post-generation avoid note correction is no longer needed because
  // secondary dominants are now pre-registered in the harmony context
  // before track generation (see secondary_dominant_planner.h).
}

}  // namespace midisketch
