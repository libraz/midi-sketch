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
#include "core/i_harmony_context.h"
#include "core/motif.h"
#include "core/motif_types.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/production_blueprint.h"
#include "core/song.h"

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

// M1: Scale interval arrays for different scale types
constexpr int SCALE_MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};           // Ionian
constexpr int SCALE_NATURAL_MINOR[7] = {0, 2, 3, 5, 7, 8, 10};   // Aeolian
constexpr int SCALE_HARMONIC_MINOR[7] = {0, 2, 3, 5, 7, 8, 11};  // Raised 7th
constexpr int SCALE_DORIAN[7] = {0, 2, 3, 5, 7, 9, 10};          // Minor with raised 6th
constexpr int SCALE_MIXOLYDIAN[7] = {0, 2, 4, 5, 7, 9, 10};      // Major with lowered 7th

// M1: Get scale intervals for a given scale type
const int* getScaleIntervals(ScaleType scale) {
  switch (scale) {
    case ScaleType::Major:
      return SCALE_MAJOR;
    case ScaleType::NaturalMinor:
      return SCALE_NATURAL_MINOR;
    case ScaleType::HarmonicMinor:
      return SCALE_HARMONIC_MINOR;
    case ScaleType::Dorian:
      return SCALE_DORIAN;
    case ScaleType::Mixolydian:
      return SCALE_MIXOLYDIAN;
  }
  return SCALE_MAJOR;
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

// Tension intervals in semitones from chord root
constexpr int TENSION_9TH = 14;
constexpr int TENSION_11TH = 17;
constexpr int TENSION_13TH = 21;

// Chord quality for tension selection
enum class ChordQuality { Major, Minor, Diminished };

// Get available tensions for a chord quality
std::vector<int> getAvailableTensions(ChordQuality quality) {
  switch (quality) {
    case ChordQuality::Major:
      return {TENSION_9TH, TENSION_13TH};
    case ChordQuality::Minor:
      return {TENSION_9TH, TENSION_11TH};
    case ChordQuality::Diminished:
      return {TENSION_9TH};
  }
  return {};
}

// Determine chord quality from chord info
ChordQuality getChordQuality(const Chord& chord) {
  if (chord.note_count >= 2) {
    if (chord.intervals[1] == 3) {
      if (chord.note_count >= 3 && chord.intervals[2] == 6) {
        return ChordQuality::Diminished;
      }
      return ChordQuality::Minor;
    }
  }
  return ChordQuality::Major;
}

// Apply tension to a pitch based on chord quality
int applyTension(int base_pitch, uint8_t chord_root, ChordQuality quality, std::mt19937& rng) {
  auto tensions = getAvailableTensions(quality);
  if (tensions.empty()) return base_pitch;

  std::uniform_int_distribution<size_t> dist(0, tensions.size() - 1);
  int tension_interval = tensions[dist(rng)];
  int tension_pitch = chord_root + tension_interval;

  while (tension_pitch > base_pitch + 12) tension_pitch -= 12;
  while (tension_pitch < base_pitch - 12) tension_pitch += 12;

  return tension_pitch;
}

// M1: Convert scale degree to pitch with key offset and scale type
int degreeToPitch(int degree, int base_note, int key_offset, ScaleType scale = ScaleType::Major) {
  const int* scale_intervals = getScaleIntervals(scale);
  int d = ((degree % 7) + 7) % 7;
  int oct_adjust = degree / 7;
  if (degree < 0 && degree % 7 != 0) oct_adjust--;
  return base_note + oct_adjust * 12 + scale_intervals[d] + key_offset;
}

// Check if pitch class is diatonic in C major
// Uses the shared DIATONIC_PITCH_CLASS lookup table from pitch_utils.h
bool isDiatonicPC(int pc) {
  pc = ((pc % 12) + 12) % 12;
  return DIATONIC_PITCH_CLASS[pc];
}

// Get chord tone pitch classes for a chord
std::vector<int> getChordTones(uint8_t chord_root, bool is_minor) {
  int root_pc = chord_root % 12;
  int third_offset = is_minor ? 3 : 4;
  int third_pc = (root_pc + third_offset) % 12;
  int fifth_pc = (root_pc + 7) % 12;
  return {root_pc, third_pc, fifth_pc};
}

// Adjust pitch to avoid dissonance by resolving to nearest chord tone
int adjustForChord(int pitch, uint8_t chord_root, bool is_minor, int8_t chord_degree) {
  if (!isAvoidNoteWithContext(pitch, chord_root, is_minor, chord_degree)) {
    return pitch;
  }

  auto chord_tones = getChordTones(chord_root, is_minor);
  int octave = pitch / 12;

  int best_pitch = pitch;
  int best_dist = 100;

  for (int ct_pc : chord_tones) {
    for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
      int candidate = (octave + oct_offset) * 12 + ct_pc;
      int dist = std::abs(candidate - pitch);
      if (dist < best_dist) {
        best_dist = dist;
        best_pitch = candidate;
      }
    }
  }

  return best_pitch;
}

// Snap pitch to nearest chord tone
int snapToChordTone(int pitch, uint8_t chord_root, bool is_minor) {
  auto chord_tones = getChordTones(chord_root, is_minor);
  int octave = pitch / 12;

  int best_pitch = pitch;
  int best_dist = 100;

  for (int ct_pc : chord_tones) {
    for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
      int candidate = (octave + oct_offset) * 12 + ct_pc;
      int dist = std::abs(candidate - pitch);
      if (dist < best_dist) {
        best_dist = dist;
        best_pitch = candidate;
      }
    }
  }

  return best_pitch;
}

// isDiatonic() is now provided by pitch_utils.h as midisketch::isDiatonic()
// The function below uses it through the namespace scope.

// Check if a pitch is a passing tone
bool isPassingTone(int pitch, uint8_t chord_root, bool is_minor) {
  if (!midisketch::isDiatonic(pitch)) return false;

  int pitch_pc = ((pitch % 12) + 12) % 12;
  int root_pc = chord_root % 12;
  int third_offset = is_minor ? 3 : 4;
  int third_pc = (root_pc + third_offset) % 12;
  int fifth_pc = (root_pc + 7) % 12;

  if (pitch_pc == root_pc || pitch_pc == third_pc || pitch_pc == fifth_pc) {
    return false;
  }

  return true;
}

// Snap pitch to a safe scale tone
int snapToSafeScaleTone(int pitch, uint8_t chord_root, bool is_minor, int8_t chord_degree,
                        float melodic_freedom, std::mt19937& rng) {
  if (isDiatonic(pitch) && !isAvoidNoteWithContext(pitch, chord_root, is_minor, chord_degree)) {
    if (isPassingTone(pitch, chord_root, is_minor)) {
      std::uniform_real_distribution<float> dist(0.0f, 1.0f);
      if (dist(rng) < melodic_freedom) {
        return pitch;
      }
    } else {
      return pitch;
    }
  }

  return snapToChordTone(pitch, chord_root, is_minor);
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
        std::uniform_int_distribution<int> dist(-limit, limit);
        step = dist(rng);
        if (step == 0) step = 1;
        break;
      }
      case MotifMotion::GentleLeap: {
        int limit = std::min(3, max_leap_degrees);
        std::uniform_int_distribution<int> dist(-limit, limit);
        step = dist(rng);
        if (step == 0) step = 1;
        break;
      }
      case MotifMotion::WideLeap: {
        int limit = std::min(5, max_leap_degrees);
        std::uniform_int_distribution<int> dist(-limit, limit);
        step = dist(rng);
        if (step == 0) step = (dist(rng) > 0) ? 2 : -2;
        break;
      }
      case MotifMotion::NarrowStep: {
        std::uniform_int_distribution<int> dist(-1, 1);
        step = dist(rng);
        if (step == 0) step = 1;
        break;
      }
      case MotifMotion::Disjunct: {
        int limit = std::min(6, max_leap_degrees);
        std::uniform_int_distribution<int> dist(2, limit);
        int magnitude = dist(rng);
        std::uniform_int_distribution<int> dir(0, 1);
        step = dir(rng) ? magnitude : -magnitude;
        break;
      }
    }
    step = constrainedStep(step);
    current += step;
    current = std::clamp(current, -4, 7);
    degrees.push_back(current);
  }

  std::uniform_int_distribution<int> q_end(0, 1);
  int question_endings[] = {1, 3};
  degrees.push_back(question_endings[q_end(rng)]);

  current = degrees.back();
  for (uint8_t i = half + 1; i < note_count - 1; ++i) {
    int step = 0;
    switch (motion) {
      case MotifMotion::Stepwise: {
        int limit = std::min(2, max_leap_degrees);
        std::uniform_int_distribution<int> dist(-limit, limit);
        step = dist(rng);
        if (step == 0) step = -1;
        break;
      }
      case MotifMotion::GentleLeap: {
        int limit = std::min(3, max_leap_degrees);
        std::uniform_int_distribution<int> dist(-limit, std::min(2, limit));
        step = dist(rng);
        if (step == 0) step = -1;
        break;
      }
      case MotifMotion::WideLeap: {
        int limit = std::min(4, max_leap_degrees);
        std::uniform_int_distribution<int> dist(-limit, std::min(3, limit));
        step = dist(rng);
        if (step == 0) step = -2;
        break;
      }
      case MotifMotion::NarrowStep: {
        std::uniform_int_distribution<int> dist(-1, 1);
        step = dist(rng);
        if (step == 0) step = -1;
        break;
      }
      case MotifMotion::Disjunct: {
        int limit = std::min(4, max_leap_degrees);
        std::uniform_int_distribution<int> dist(1, limit);
        int magnitude = dist(rng);
        std::uniform_int_distribution<int> dir(0, 2);
        step = (dir(rng) < 2) ? -magnitude : magnitude;
        break;
      }
    }
    step = constrainedStep(step);
    current += step;
    current = std::clamp(current, -4, 7);
    degrees.push_back(current);
  }

  std::uniform_int_distribution<int> a_end(0, 2);
  int answer_endings[] = {0, 2, 4};
  degrees.push_back(answer_endings[a_end(rng)]);

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
    base_note = std::max(static_cast<uint8_t>(67), static_cast<uint8_t>(vocal_high + 5));
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

  std::uniform_real_distribution<float> prob(0.0f, 1.0f);
  if (prob(rng) > strength) return pitch;

  std::uniform_int_distribution<int> step(1, 3);
  int adjustment = step(rng) * (-vocal_direction);

  return pitch + adjustment;
}

}  // namespace motif_detail

// =============================================================================
// MotifGenerator Implementation
// =============================================================================

void MotifGenerator::generateSection(MidiTrack& /* track */, const Section& /* section */,
                                      TrackContext& /* ctx */) {
  // MotifGenerator uses generateFullTrack() for pattern repetition across sections
  // This method is kept for ITrackBase compliance but not used directly.
}

std::vector<NoteEvent> generateMotifPattern(const GeneratorParams& params, std::mt19937& rng) {
  const MotifParams& motif_params = params.motif;
  std::vector<NoteEvent> pattern;

  int key_offset = 0;
  uint8_t base_note = motif_params.register_high ? 67 : 60;

  std::vector<Tick> positions = motif_detail::generateRhythmPositions(
      motif_params.rhythm_density, motif_params.length, motif_params.note_count, rng);

  int max_leap_degrees = 7;
  bool prefer_stepwise = false;
  if (params.blueprint_ref != nullptr) {
    max_leap_degrees = (params.blueprint_ref->constraints.max_leap_semitones * 7 + 11) / 12;
    prefer_stepwise = params.blueprint_ref->constraints.prefer_stepwise;
  }

  std::vector<int> degrees = motif_detail::generatePitchSequence(
      motif_params.note_count, motif_params.motion, rng, max_leap_degrees, prefer_stepwise);

  Tick note_duration = TICKS_PER_BEAT / 2;
  switch (motif_params.rhythm_density) {
    case MotifRhythmDensity::Sparse:
      note_duration = TICKS_PER_BEAT;
      break;
    case MotifRhythmDensity::Medium:
    case MotifRhythmDensity::Driving:
      note_duration = TICKS_PER_BEAT / 2;
      break;
  }

  uint8_t velocity = motif_params.velocity_fixed ? 80 : 75;

  size_t pitch_idx = 0;
  for (Tick pos : positions) {
    int degree = degrees[pitch_idx % degrees.size()];
    int pitch = motif_detail::degreeToPitch(degree, base_note, key_offset);

    pitch = std::clamp(pitch, 36, 96);

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
/// @return Adjusted pitch
int calculateMotifPitch(const NoteEvent& note, const MotifNoteContext& ctx,
                         const GeneratorParams& params, const MotifParams& motif_params,
                         IHarmonyCoordinator* harmony, const MotifContext* vocal_ctx,
                         uint8_t base_note_override, std::mt19937& rng) {
  if (ctx.is_rhythm_lock_global) {
    // Coordinate axis mode: use pattern pitch directly
    return std::clamp(static_cast<int>(note.note), static_cast<int>(MOTIF_LOW),
                      static_cast<int>(MOTIF_HIGH));
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

  return adjusted_pitch;
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
  } else if (section_type == SectionType::Intro || section_type == SectionType::Outro) {
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
  const MotifContext* vocal_ctx = static_cast<const MotifContext*>(ctx.vocal_ctx);

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

  // Check if this is RhythmLock mode (coordinate axis)
  RiffPolicy policy = params.riff_policy;
  bool is_locked = (policy == RiffPolicy::LockedContour || policy == RiffPolicy::LockedPitch ||
                    policy == RiffPolicy::LockedAll);
  bool is_rhythm_lock_global = (params.paradigm == GenerationParadigm::RhythmSync) && is_locked;

  for (const auto& section : sections) {
    if (!hasTrack(section.track_mask, TrackMask::Motif)) {
      sec_idx++;
      continue;
    }

    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    bool is_chorus = (section.type == SectionType::Chorus);

    bool add_octave =
        is_chorus && motif_params.octave_layering_chorus && role_meta.allow_octave_layer;

    // L2: Determine which pattern to use based on RiffPolicy
    std::vector<NoteEvent>* current_pattern = &pattern;
    std::vector<NoteEvent> section_pattern;

    if (is_locked && riff_cache.cached) {
      current_pattern = &riff_cache.pattern;
    } else if (policy == RiffPolicy::Evolving && riff_cache.cached) {
      std::uniform_real_distribution<float> evolve_dist(0.0f, 1.0f);
      if (sec_idx % 2 == 0 && evolve_dist(rng) < 0.3f) {
        riff_cache.pattern = generateMotifPattern(params, rng);
      }
      current_pattern = &riff_cache.pattern;
    } else if (policy == RiffPolicy::Free) {
      if (motif_params.repeat_scope == MotifRepeatScope::Section) {
        auto it = section_patterns.find(section.type);
        if (it == section_patterns.end()) {
          std::uniform_real_distribution<float> var_dist(0.0f, 1.0f);
          if (var_dist(rng) < role_meta.exact_repeat_prob) {
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

        bool should_skip = false;
        if (effective_density < 100) {
          std::uniform_real_distribution<float> density_dist(0.0f, 100.0f);
          should_skip = (density_dist(rng) > effective_density);

          if (should_skip && bar_note_count[current_bar] == 0) {
            should_skip = false;
          }
        }
        if (should_skip) {
          continue;
        }

        // L5: Vocal Coordination - Response Mode
        if (vocal_ctx && motif_params.response_mode) {
          bool in_rest = motif_detail::isInVocalRest(absolute_tick, vocal_ctx->rest_positions);
          if (!in_rest) {
            float skip_prob = vocal_ctx->vocal_density * 0.4f;
            std::uniform_real_distribution<float> resp_dist(0.0f, 1.0f);
            if (resp_dist(rng) < skip_prob && bar_note_count[current_bar] > 0) {
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

        // Calculate adjusted pitch using helper
        int adjusted_pitch =
            calculateMotifPitch(note, note_ctx, params, motif_params, harmony, vocal_ctx,
                                base_note_override, rng);

        // Calculate velocity using helper
        uint8_t vel = calculateMotifVelocity(role_meta.velocity_base, is_chorus, section.type,
                                              motif_params.velocity_fixed);

        uint8_t final_pitch = static_cast<uint8_t>(adjusted_pitch);

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
          opts.range_high = MOTIF_HIGH;
          opts.source = NoteSource::Motif;
          opts.original_pitch = note.note;  // Track pre-adjustment pitch

          createNoteAndAdd(track, *harmony, opts);
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
          opts.range_high = MOTIF_HIGH;
          opts.source = NoteSource::Motif;
          opts.original_pitch = note.note;  // Track pre-adjustment pitch

          auto motif_note = createNoteAndAdd(track, *harmony, opts);

          if (!motif_note) {
            continue;
          }

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
    sec_idx++;
  }
}

}  // namespace midisketch
