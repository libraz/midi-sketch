/**
 * @file motif.cpp
 * @brief Implementation of background motif track generation.
 */

#include "track/motif.h"

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <vector>

#include "core/chord.h"
#include "core/i_harmony_context.h"
#include "core/motif.h"
#include "core/motif_types.h"
#include "core/note_factory.h"
#include "core/pitch_utils.h"
#include "core/production_blueprint.h"

namespace midisketch {

// =============================================================================
// RiffPolicy Cache for Locked/Evolving modes
// =============================================================================

/// Cache for RiffPolicy::Locked and RiffPolicy::Evolving modes.
/// Stores the pattern from the first valid section to reuse across sections.
struct MotifRiffCache {
  std::vector<NoteEvent> pattern;
  bool cached = false;
};

// =============================================================================
// M1: ScaleType Support - Convert scale type to interval array
// =============================================================================

// Internal implementation details for motif track generation.
// Using named namespace instead of anonymous to:
// 1. Provide clearer separation from core/motif.cpp
// 2. Enable testing of internal functions if needed
// 3. Avoid potential ODR issues with anonymous namespaces
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
    // Minor chord context
    switch (mood) {
      case Mood::Dramatic:
      case Mood::DarkPop:
        return ScaleType::HarmonicMinor;  // Raised 7th for dramatic effect
      case Mood::Chill:
      case Mood::CityPop:
        return ScaleType::Dorian;  // Softer, jazzier minor
      default:
        return ScaleType::NaturalMinor;
    }
  } else {
    // Major chord context
    switch (mood) {
      case Mood::Synthwave:
      case Mood::FutureBass:
        return ScaleType::Mixolydian;  // Flattened 7th for synth feel
      default:
        return ScaleType::Major;
    }
  }
}

// Backward compatibility: SCALE_MAJOR is used directly via getScaleIntervals()

// Note: Avoid note constants (AVOID_PERFECT_4TH, AVOID_MINOR_6TH, AVOID_TRITONE, AVOID_MAJOR_7TH)
// and detection functions are now in pitch_utils.h for centralized music theory handling.

// Tension intervals in semitones from chord root
constexpr int TENSION_9TH = 14;   // 9th = 2nd + octave (14 semitones from root)
constexpr int TENSION_11TH = 17;  // 11th = 4th + octave (17 semitones)
constexpr int TENSION_13TH = 21;  // 13th = 6th + octave (21 semitones)

// Chord quality for tension selection
enum class ChordQuality { Major, Minor, Diminished };

// Get available tensions for a chord quality
std::vector<int> getAvailableTensions(ChordQuality quality) {
  switch (quality) {
    case ChordQuality::Major:
      // Major chords: 9th and 13th work well
      return {TENSION_9TH, TENSION_13TH};
    case ChordQuality::Minor:
      // Minor chords: 9th and 11th work well
      return {TENSION_9TH, TENSION_11TH};
    case ChordQuality::Diminished:
      // Diminished: limited tensions, b9 is possible but dissonant
      // Use 9th carefully (whole step above root)
      return {TENSION_9TH};
  }
  return {};
}

// Determine chord quality from chord info
ChordQuality getChordQuality(const Chord& chord) {
  if (chord.note_count >= 2) {
    // Check 3rd interval: 3 = minor, 4 = major
    if (chord.intervals[1] == 3) {
      // Check if diminished (3rd is 3, 5th is 6)
      if (chord.note_count >= 3 && chord.intervals[2] == 6) {
        return ChordQuality::Diminished;
      }
      return ChordQuality::Minor;
    }
  }
  return ChordQuality::Major;
}

// Apply 9th or other tension to a pitch based on chord quality
int applyTension(int base_pitch, uint8_t chord_root, ChordQuality quality, std::mt19937& rng) {
  auto tensions = getAvailableTensions(quality);
  if (tensions.empty()) return base_pitch;

  // Randomly select a tension
  std::uniform_int_distribution<size_t> dist(0, tensions.size() - 1);
  int tension_interval = tensions[dist(rng)];

  // Calculate tension pitch (tension is relative to chord root)
  int tension_pitch = chord_root + tension_interval;

  // Adjust to be close to base pitch (within reasonable range)
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

// isAvoidNote is now provided by pitch_utils.h as isAvoidNoteWithContext() and isAvoidNoteSimple()

// Check if pitch class is diatonic in C major
bool isDiatonicPC(int pc) {
  pc = ((pc % 12) + 12) % 12;
  for (int d : SCALE) {  // SCALE defined in pitch_utils.h
    if (d == pc) return true;
  }
  return false;
}

// Get chord tone pitch classes for a chord, filtered to diatonic only
// Returns root, 3rd, 5th as pitch classes (0-11), but only diatonic ones
std::vector<int> getDiatonicChordTones(uint8_t chord_root, bool is_minor) {
  int root_pc = chord_root % 12;
  int third_offset = is_minor ? 3 : 4;  // minor 3rd or major 3rd
  int third_pc = (root_pc + third_offset) % 12;
  int fifth_pc = (root_pc + 7) % 12;

  std::vector<int> tones;
  // Only include diatonic chord tones
  if (isDiatonicPC(root_pc)) tones.push_back(root_pc);
  if (isDiatonicPC(third_pc)) tones.push_back(third_pc);
  if (isDiatonicPC(fifth_pc)) tones.push_back(fifth_pc);

  // If no diatonic chord tones (shouldn't happen in C major), return root
  if (tones.empty()) {
    tones.push_back(root_pc);
  }
  return tones;
}

// Adjust pitch to avoid dissonance by resolving to nearest DIATONIC chord tone
// @param chord_degree Scale degree of the chord (0=I, 4=V, etc.) for context-aware avoid detection
int adjustForChord(int pitch, uint8_t chord_root, bool is_minor, int8_t chord_degree) {
  // Use context-aware avoid note detection from pitch_utils.h
  // This considers chord function (Tonic/Dominant/Subdominant) for tritone handling
  if (!isAvoidNoteWithContext(pitch, chord_root, is_minor, chord_degree)) {
    return pitch;
  }

  // Find nearest diatonic chord tone
  auto chord_tones = getDiatonicChordTones(chord_root, is_minor);
  int octave = pitch / 12;

  int best_pitch = pitch;
  int best_dist = 100;

  for (int ct_pc : chord_tones) {
    // Check same octave and adjacent octaves
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

// Snap pitch to nearest chord tone (root, 3rd, 5th) regardless of avoid note status.
// Used in RhythmSync mode to constrain Motif to chord tones, leaving passing tones for Vocal.
// @param pitch Input pitch (MIDI note number)
// @param chord_root Root note of current chord (MIDI note number)
// @param is_minor Whether the chord is minor
// @returns Pitch snapped to nearest chord tone
int snapToChordTone(int pitch, uint8_t chord_root, bool is_minor) {
  auto chord_tones = getDiatonicChordTones(chord_root, is_minor);
  int octave = pitch / 12;

  int best_pitch = pitch;
  int best_dist = 100;

  for (int ct_pc : chord_tones) {
    // Check same octave and adjacent octaves
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

// Check if a pitch is on the diatonic scale (C major)
// @param pitch MIDI note number
// @returns true if pitch is diatonic
bool isDiatonic(int pitch) {
  // C major scale: C(0), D(2), E(4), F(5), G(7), A(9), B(11)
  int pitch_class = ((pitch % 12) + 12) % 12;
  return pitch_class == 0 || pitch_class == 2 || pitch_class == 4 || pitch_class == 5 ||
         pitch_class == 7 || pitch_class == 9 || pitch_class == 11;
}

// Check if a pitch is a passing tone (non-chord tone scale degree)
// Passing tones: 2nd (D), 4th (F), 6th (A), 7th (B) in C major
// @param pitch MIDI note number
// @param chord_root Root of current chord
// @param is_minor Whether chord is minor quality
// @returns true if pitch is a passing tone (scale tone but not chord tone)
bool isPassingTone(int pitch, uint8_t chord_root, bool is_minor) {
  if (!isDiatonic(pitch)) return false;

  int pitch_pc = ((pitch % 12) + 12) % 12;
  int root_pc = chord_root % 12;

  // Chord tones are root, 3rd (3 or 4 semitones), 5th (7 semitones)
  int third_offset = is_minor ? 3 : 4;
  int third_pc = (root_pc + third_offset) % 12;
  int fifth_pc = (root_pc + 7) % 12;

  // If it's a chord tone, it's not a passing tone
  if (pitch_pc == root_pc || pitch_pc == third_pc || pitch_pc == fifth_pc) {
    return false;
  }

  // It's diatonic but not a chord tone = passing tone
  return true;
}

// Snap pitch to a safe scale tone, allowing passing tones based on melodic_freedom.
// Used in RhythmSync mode to add melodic variety while avoiding harsh dissonance.
// @param pitch Input pitch (MIDI note number)
// @param chord_root Root note of current chord
// @param is_minor Whether chord is minor quality
// @param chord_degree Scale degree of the chord (for avoid note detection)
// @param melodic_freedom 0.0=chord tones only, 1.0=all scale tones allowed
// @param rng Random generator for probabilistic choices
// @returns Pitch snapped to safe scale/chord tone
int snapToSafeScaleTone(int pitch, uint8_t chord_root, bool is_minor, int8_t chord_degree,
                        float melodic_freedom, std::mt19937& rng) {
  // If already diatonic and safe (not an avoid note), consider keeping it
  if (isDiatonic(pitch) && !isAvoidNoteWithContext(pitch, chord_root, is_minor, chord_degree)) {
    // If it's a passing tone, keep it with probability = melodic_freedom
    if (isPassingTone(pitch, chord_root, is_minor)) {
      std::uniform_real_distribution<float> dist(0.0f, 1.0f);
      if (dist(rng) < melodic_freedom) {
        return pitch;  // Keep the passing tone for melodic interest
      }
    } else {
      // It's already a chord tone - keep it
      return pitch;
    }
  }

  // Fallback: snap to nearest chord tone
  return snapToChordTone(pitch, chord_root, is_minor);
}

// Adjust pitch to nearest diatonic scale tone (C major)
// @param pitch Input pitch (MIDI note number)
// @returns Adjusted pitch on C major scale
int adjustToDiatonic(int pitch) {
  if (isDiatonic(pitch)) {
    return pitch;
  }
  // Non-diatonic notes and their resolution (prefer downward for smoother voice leading)
  // C#/Db -> C, D#/Eb -> D, F#/Gb -> G, G#/Ab -> G, A#/Bb -> A
  int pitch_class = ((pitch % 12) + 12) % 12;
  int adjustment = 0;
  switch (pitch_class) {
    case 1:
      adjustment = -1;
      break;  // C# -> C
    case 3:
      adjustment = -1;
      break;  // D# -> D
    case 6:
      adjustment = +1;
      break;  // F# -> G (tritone resolution)
    case 8:
      adjustment = -1;
      break;  // G# -> G
    case 10:
      adjustment = -1;
      break;  // A# -> A
  }
  return pitch + adjustment;
}

// Adjust pitch to nearest scale tone for mood-appropriate melodic color
// @param pitch Input pitch (MIDI note number)
// @param key_root Key root (0-11)
// @param scale Scale type to use
// @returns Adjusted pitch on the scale
int adjustPitchToScale(int pitch, uint8_t key_root, ScaleType scale) {
  const int* intervals = getScaleIntervals(scale);
  int pitch_class = ((pitch - static_cast<int>(key_root)) % 12 + 12) % 12;

  // Check if already on scale - no adjustment needed
  for (int i = 0; i < 7; ++i) {
    if (intervals[i] == pitch_class) {
      return pitch;
    }
  }

  // Find nearest scale tone by checking both directions
  int best_pitch = pitch;
  int best_dist = 12;

  for (int i = 0; i < 7; ++i) {
    int scale_pc = intervals[i];
    // Calculate distance considering octave wrap
    int dist1 = std::abs(scale_pc - pitch_class);
    int dist2 = 12 - dist1;
    int dist = std::min(dist1, dist2);

    if (dist < best_dist) {
      best_dist = dist;
      // Move to nearest scale tone
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

// Generate rhythm positions based on density.
// - Sparse/Medium: Call & response structure (notes distributed across both halves)
// - Driving: Continuous fill for rhythm-focused styles (Orangestar-like)
std::vector<Tick> generateRhythmPositions(MotifRhythmDensity density, MotifLength length,
                                          uint8_t note_count, std::mt19937& /* rng */) {
  Tick motif_ticks = static_cast<Tick>(length) * TICKS_PER_BAR;
  std::vector<Tick> positions;

  // Driving density: continuous fill for rhythm-focused styles
  // Creates a steady, repeating pattern (e.g., eighth notes throughout)
  if (density == MotifRhythmDensity::Driving) {
    Tick step = TICKS_PER_BEAT / 2;  // Eighth note grid
    for (Tick t = 0; t < motif_ticks && positions.size() < note_count; t += step) {
      positions.push_back(t);
    }
    return positions;
  }

  // Sparse/Medium: Call & response structure
  // Distributes notes between first half (call) and second half (response)
  Tick half_ticks = motif_ticks / 2;
  uint8_t call_count = (note_count + 1) / 2;
  uint8_t response_count = note_count - call_count;

  // Helper to fill positions within a half
  auto fillHalf = [&positions](Tick start, Tick end, uint8_t count, MotifRhythmDensity d) {
    if (count == 0) return;

    Tick step = (d == MotifRhythmDensity::Sparse) ? TICKS_PER_BEAT : TICKS_PER_BEAT / 2;

    // Collect candidate positions within this half
    std::vector<Tick> candidates;
    for (Tick t = start; t < end; t += step) {
      candidates.push_back(t);
    }

    // For Medium density, prioritize downbeats
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

    // Add positions up to count for this half
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
// max_leap_degrees: maximum step in scale degrees (derived from BlueprintConstraints.max_leap_semitones)
// prefer_stepwise: if true, limit steps to 1-2 degrees
std::vector<int> generatePitchSequence(uint8_t note_count, MotifMotion motion, std::mt19937& rng,
                                       int max_leap_degrees = 7, bool prefer_stepwise = false) {
  std::vector<int> degrees;

  // Apply constraints to limit step sizes
  // If prefer_stepwise is set, override motion to be more stepwise
  auto constrainedStep = [max_leap_degrees, prefer_stepwise](int step) {
    int limit = prefer_stepwise ? std::min(2, max_leap_degrees) : max_leap_degrees;
    return std::clamp(step, -limit, limit);
  };

  // Split into "question" (first half) and "answer" (second half)
  uint8_t half = note_count / 2;

  // Question phrase - ends on unstable tone
  degrees.push_back(0);  // Start on root
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
        // Up to 5th intervals (5 scale degrees), constrained by blueprint
        int limit = std::min(5, max_leap_degrees);
        std::uniform_int_distribution<int> dist(-limit, limit);
        step = dist(rng);
        if (step == 0) step = (dist(rng) > 0) ? 2 : -2;
        break;
      }
      case MotifMotion::NarrowStep: {
        // Half-step motion (1-2 semitones in scale degree terms)
        std::uniform_int_distribution<int> dist(-1, 1);
        step = dist(rng);
        if (step == 0) step = 1;
        break;
      }
      case MotifMotion::Disjunct: {
        // Irregular leaps with occasional direction changes
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

  // End question on 2nd or 4th (unstable)
  std::uniform_int_distribution<int> q_end(0, 1);
  int question_endings[] = {1, 3};  // 2nd, 4th scale degrees
  degrees.push_back(question_endings[q_end(rng)]);

  // Answer phrase - similar motion but ends on stable tone
  current = degrees.back();
  for (uint8_t i = half + 1; i < note_count - 1; ++i) {
    int step = 0;
    switch (motion) {
      case MotifMotion::Stepwise: {
        int limit = std::min(2, max_leap_degrees);
        std::uniform_int_distribution<int> dist(-limit, limit);
        step = dist(rng);
        if (step == 0) step = -1;  // Tend downward for resolution
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
        // Up to 5th intervals, tend toward resolution
        int limit = std::min(4, max_leap_degrees);
        std::uniform_int_distribution<int> dist(-limit, std::min(3, limit));
        step = dist(rng);
        if (step == 0) step = -2;  // Tend downward
        break;
      }
      case MotifMotion::NarrowStep: {
        // Half-step motion toward resolution
        std::uniform_int_distribution<int> dist(-1, 1);
        step = dist(rng);
        if (step == 0) step = -1;
        break;
      }
      case MotifMotion::Disjunct: {
        // Irregular but trending toward resolution
        int limit = std::min(4, max_leap_degrees);
        std::uniform_int_distribution<int> dist(1, limit);
        int magnitude = dist(rng);
        // More likely to go down for resolution
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

  // End answer on stable tone (root, 3rd, or 5th)
  std::uniform_int_distribution<int> a_end(0, 2);
  int answer_endings[] = {0, 2, 4};
  degrees.push_back(answer_endings[a_end(rng)]);

  return degrees;
}

// =============================================================================
// Vocal Coordination Helpers (for MelodyLead mode)
// =============================================================================

/// Check if the given tick falls within a vocal rest period.
/// @param tick Tick position to check
/// @param rest_positions Sorted list of rest start positions
/// @param threshold Duration threshold to consider as "within rest"
/// @returns true if tick is in a vocal rest
bool isInVocalRest(Tick tick, const std::vector<Tick>* rest_positions, Tick threshold = 480) {
  if (!rest_positions || rest_positions->empty()) return false;

  for (const Tick& rest_start : *rest_positions) {
    // Check if tick is within threshold ticks after rest start
    if (tick >= rest_start && tick < rest_start + threshold * 2) {
      return true;
    }
  }
  return false;
}

/// Calculate motif register to avoid vocal range.
/// @param vocal_low Lowest vocal pitch
/// @param vocal_high Highest vocal pitch
/// @param register_high Whether to prefer high register
/// @param register_offset Additional offset in semitones
/// @returns Adjusted base note for motif
uint8_t calculateMotifRegister(uint8_t vocal_low, uint8_t vocal_high, bool register_high,
                               int8_t register_offset) {
  // Vocal center pitch
  uint8_t vocal_center = (vocal_low + vocal_high) / 2;

  // Default motif ranges: mid (C4-C5) or high (G4-G5)
  uint8_t base_note;
  if (register_high) {
    // High register: aim above vocal
    base_note = std::max(static_cast<uint8_t>(67), static_cast<uint8_t>(vocal_high + 5));
  } else {
    // Mid register: aim below vocal if vocal is high, else above
    if (vocal_center >= 66) {
      // Vocal is high, put motif below
      base_note = std::min(static_cast<uint8_t>(55), static_cast<uint8_t>(vocal_low - 7));
    } else {
      // Vocal is mid/low, put motif above
      base_note = std::max(static_cast<uint8_t>(72), static_cast<uint8_t>(vocal_high + 5));
    }
  }

  // Apply register offset and clamp to valid MIDI range
  int adjusted = base_note + register_offset;
  return static_cast<uint8_t>(std::clamp(adjusted, 36, 96));
}

/// Get vocal pitch direction at a specific tick.
/// @param direction_at_tick Map of tick -> direction
/// @param tick Tick to query
/// @returns Direction: +1=up, -1=down, 0=none/static
int8_t getVocalDirection(const std::map<Tick, int8_t>* direction_at_tick, Tick tick) {
  if (!direction_at_tick || direction_at_tick->empty()) return 0;

  // Find the latest direction at or before this tick
  auto it = direction_at_tick->upper_bound(tick);
  if (it == direction_at_tick->begin()) return 0;
  --it;
  return it->second;
}

/// Apply contrary motion to pitch based on vocal direction.
/// When vocal goes up, motif tends to go down and vice versa.
/// @param pitch Input pitch
/// @param vocal_direction Vocal melodic direction (+1, -1, 0)
/// @param strength Strength of contrary motion (0.0-1.0)
/// @param rng Random generator
/// @returns Adjusted pitch
int applyContraryMotion(int pitch, int8_t vocal_direction, float strength, std::mt19937& rng) {
  if (vocal_direction == 0 || strength <= 0.0f) return pitch;

  std::uniform_real_distribution<float> prob(0.0f, 1.0f);
  if (prob(rng) > strength) return pitch;  // Skip contrary motion based on strength

  // Apply contrary motion: move opposite to vocal direction
  // Small adjustment: 1-3 semitones in opposite direction
  std::uniform_int_distribution<int> step(1, 3);
  int adjustment = step(rng) * (-vocal_direction);  // Opposite direction

  return pitch + adjustment;
}

}  // namespace motif_detail

std::vector<NoteEvent> generateMotifPattern(const GeneratorParams& params, std::mt19937& rng) {
  const MotifParams& motif_params = params.motif;
  std::vector<NoteEvent> pattern;

  // Internal processing is always in C major; transpose at MIDI output time
  int key_offset = 0;
  uint8_t base_note = motif_params.register_high ? 67 : 60;  // G4 or C4

  // Generate rhythm positions
  std::vector<Tick> positions = motif_detail::generateRhythmPositions(
      motif_params.rhythm_density, motif_params.length, motif_params.note_count, rng);

  // Apply BlueprintConstraints to pitch sequence generation
  // Convert max_leap_semitones to approximate scale degrees (12 semitones ≈ 7 degrees)
  int max_leap_degrees = 7;  // Default: full octave
  bool prefer_stepwise = false;
  if (params.blueprint_ref != nullptr) {
    max_leap_degrees = (params.blueprint_ref->constraints.max_leap_semitones * 7 + 11) / 12;
    prefer_stepwise = params.blueprint_ref->constraints.prefer_stepwise;
  }

  // Generate pitch sequence with structure and constraints
  std::vector<int> degrees = motif_detail::generatePitchSequence(
      motif_params.note_count, motif_params.motion, rng, max_leap_degrees, prefer_stepwise);

  // Calculate note duration
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

  // Create notes
  size_t pitch_idx = 0;
  for (Tick pos : positions) {
    int degree = degrees[pitch_idx % degrees.size()];
    int pitch = motif_detail::degreeToPitch(degree, base_note, key_offset);

    pitch = std::clamp(pitch, 36, 96);

    pattern.push_back(NoteEventBuilder::create(pos, note_duration, static_cast<uint8_t>(pitch), velocity));
    pitch_idx++;
  }

  return pattern;
}

// =============================================================================
// Motif Track Layer Architecture
// =============================================================================
//
// L1 (Structural Layer):
//   - generateMotifPattern()       - Base pattern generation
//   - generateRhythmPositions()    - Rhythm structure
//   - generatePitchSequence()      - Melodic contour
//
// L2 (Identity Layer):
//   - M4: Section pattern caching  - Phrase reuse
//   - M9: MotifRole behavior       - Variation control
//   - MotifRepeatScope             - Pattern repetition scope
//
// L3 (Safety Layer):
//   - adjustForChord()             - Avoid note resolution
//   - isAvoidNote()                - Dissonance detection
//   - M1: ScaleType selection      - Scale-aware pitch adjustment
//
// L4 (Performance Layer):
//   - Velocity from MotifRole      - Role-based dynamics
//   - Octave layering              - Chorus enhancement
//   - Tension application          - Color notes
//
// =============================================================================

void generateMotifTrack(MidiTrack& track, Song& song, const GeneratorParams& params,
                        std::mt19937& rng, const IHarmonyContext& harmony,
                        const MotifContext* vocal_ctx) {
  // L1: Generate base motif pattern
  std::vector<NoteEvent> pattern = generateMotifPattern(params, rng);
  song.setMotifPattern(pattern);

  if (pattern.empty()) return;

  NoteFactory factory(harmony);

  const MotifParams& motif_params = params.motif;
  Tick motif_length = static_cast<Tick>(motif_params.length) * TICKS_PER_BAR;

  // L5 (Vocal Coordination Layer): Calculate vocal-aware base note if context provided
  uint8_t base_note_override = 0;
  if (vocal_ctx && motif_params.dynamic_register) {
    base_note_override = motif_detail::calculateMotifRegister(
        vocal_ctx->vocal_low, vocal_ctx->vocal_high, motif_params.register_high,
        motif_params.register_offset);
  }

  const auto& sections = song.arrangement().sections();

  // M9: Determine motif role for this track
  // BackgroundMotif style uses Hook role, SynthDriven uses Texture
  MotifRole role = (params.composition_style == CompositionStyle::BackgroundMotif)
                       ? MotifRole::Hook
                       : MotifRole::Texture;
  MotifRoleMeta role_meta = getMotifRoleMeta(role);

  // M4: Cache for section-specific patterns (used when repeat_scope == Section)
  std::map<SectionType, std::vector<NoteEvent>> section_patterns;

  // RiffPolicy cache for Locked/Evolving modes
  MotifRiffCache riff_cache;
  riff_cache.pattern = pattern;  // Store initial pattern
  size_t sec_idx = 0;

  for (const auto& section : sections) {
    // Skip sections where motif is disabled by track_mask
    if (!hasTrack(section.track_mask, TrackMask::Motif)) {
      sec_idx++;
      continue;
    }

    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    bool is_chorus = (section.type == SectionType::Chorus);

    // M9: Apply octave layering based on role metadata
    bool add_octave =
        is_chorus && motif_params.octave_layering_chorus && role_meta.allow_octave_layer;

    // L2: Determine which pattern to use based on RiffPolicy (takes precedence)
    // and MotifRepeatScope (fallback for Free policy)
    std::vector<NoteEvent>* current_pattern = &pattern;
    std::vector<NoteEvent> section_pattern;

    // Check RiffPolicy
    RiffPolicy policy = params.riff_policy;

    // Handle Locked variants (LockedContour, LockedPitch, LockedAll) as same behavior
    bool is_locked = (policy == RiffPolicy::LockedContour || policy == RiffPolicy::LockedPitch ||
                      policy == RiffPolicy::LockedAll);

    if (is_locked && riff_cache.cached) {
      // Locked: always use cached pattern from first section
      current_pattern = &riff_cache.pattern;
    } else if (policy == RiffPolicy::Evolving && riff_cache.cached) {
      // Evolving: 30% chance to regenerate pattern every 2 sections
      std::uniform_real_distribution<float> evolve_dist(0.0f, 1.0f);
      if (sec_idx % 2 == 0 && evolve_dist(rng) < 0.3f) {
        // Allow evolution - generate new pattern
        riff_cache.pattern = generateMotifPattern(params, rng);
      }
      current_pattern = &riff_cache.pattern;
    } else if (policy == RiffPolicy::Free) {
      // Free: use MotifRepeatScope logic (per-section or full-song)
      if (motif_params.repeat_scope == MotifRepeatScope::Section) {
        // M4: Check cache for this section type
        auto it = section_patterns.find(section.type);
        if (it == section_patterns.end()) {
          // M9: Apply variation based on role
          std::uniform_real_distribution<float> var_dist(0.0f, 1.0f);
          if (var_dist(rng) < role_meta.exact_repeat_prob) {
            // Use base pattern (exact repeat)
            section_patterns[section.type] = pattern;
          } else {
            // Generate new pattern for variation
            section_pattern = generateMotifPattern(params, rng);
            section_patterns[section.type] = section_pattern;
          }
          current_pattern = &section_patterns[section.type];
        } else {
          current_pattern = &it->second;
        }
      }
      // else: FullSong - use the same base pattern for all sections
    } else {
      // First valid section: cache pattern for Locked/Evolving modes
      current_pattern = &riff_cache.pattern;
    }

    // Cache the first valid pattern for Locked/Evolving modes
    if (!riff_cache.cached) {
      riff_cache.cached = true;
    }

    // NOTE: Cross-section motif sharing (P2) was designed but disabled to avoid dissonance.
    // The placeMotifInBridge/placeMotifInFinalChorus functions exist in core/motif.cpp
    // but need integration with the pitch adjustment pipeline before use.

    // Repeat motif across the section
    for (Tick pos = section.start_tick; pos < section_end; pos += motif_length) {
      // Bar coverage tracking: ensure at least one note per bar to prevent full-bar silence
      std::map<uint8_t, size_t> bar_note_count;

      for (const auto& note : *current_pattern) {
        Tick absolute_tick = pos + note.start_tick;
        if (absolute_tick >= section_end) continue;

        // Calculate which bar this note is in (relative to motif start)
        uint8_t current_bar = static_cast<uint8_t>((absolute_tick - pos) / TICKS_PER_BAR);

        // Apply density_percent to skip notes probabilistically (with SectionModifier)
        uint8_t effective_density = section.getModifiedDensity(section.density_percent);

        // Adjust density based on BackingDensity
        // Thin: reduce by 15%, Thick: increase by 10%
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

          // Bar coverage guard: if this bar has no notes yet, don't skip
          // This ensures at least one note per bar for melodic continuity
          if (should_skip && bar_note_count[current_bar] == 0) {
            should_skip = false;
          }
        }
        if (should_skip) {
          continue;  // Skip this note based on density setting
        }

        // L5: Vocal Coordination - Response Mode
        // When vocal is active, reduce motif activity; when vocal rests, increase activity
        if (vocal_ctx && motif_params.response_mode) {
          bool in_rest = motif_detail::isInVocalRest(absolute_tick, vocal_ctx->rest_positions);
          if (!in_rest) {
            // Vocal is active - probabilistically skip notes to give space
            // Higher vocal density = more likely to skip
            float skip_prob = vocal_ctx->vocal_density * 0.4f;  // Max 40% skip rate
            std::uniform_real_distribution<float> resp_dist(0.0f, 1.0f);
            if (resp_dist(rng) < skip_prob && bar_note_count[current_bar] > 0) {
              continue;  // Skip to give vocal space (unless bar needs a note)
            }
          } else {
            // Vocal is resting - boost probability of playing (handled by not skipping)
            // The response_probability param could be used here for additional notes
          }
        }

        // Use HarmonyContext for accurate chord lookup at this tick
        // This ensures Motif uses the same chord as Vocal/Chord/Bass tracks
        int8_t degree = harmony.getChordDegreeAt(absolute_tick);

        // Get chord info (use Key::C for internal processing)
        uint8_t chord_root = degreeToRoot(degree, Key::C);
        Chord chord = getChordNotes(degree);
        bool is_minor = (chord.intervals[1] == 3);
        // ChordQuality was used for tension application, now commented out
        // motif_detail::ChordQuality quality = motif_detail::getChordQuality(chord);

        // M1: Select scale type based on chord quality and mood
        ScaleType scale = motif_detail::selectScaleType(is_minor, params.mood);

        // L3: First adjust pitch to scale, then to chord for dissonance avoidance
        int adjusted_pitch = motif_detail::adjustPitchToScale(note.note, 0, scale);  // Key::C = 0
        adjusted_pitch = motif_detail::adjustForChord(adjusted_pitch, chord_root, is_minor, degree);

        // L5: Apply dynamic register adjustment to avoid vocal range
        if (vocal_ctx && motif_params.dynamic_register && base_note_override != 0) {
          // Shift pitch to target register while maintaining melodic contour
          int original_base = motif_params.register_high ? 67 : 60;
          int register_shift = static_cast<int>(base_note_override) - original_base;
          adjusted_pitch += register_shift;
        }

        // L5: Apply contrary motion based on vocal direction
        if (vocal_ctx && motif_params.contrary_motion) {
          int8_t vocal_dir =
              motif_detail::getVocalDirection(vocal_ctx->direction_at_tick, absolute_tick);
          adjusted_pitch = motif_detail::applyContraryMotion(
              adjusted_pitch, vocal_dir, motif_params.contrary_motion_strength, rng);
        }

        // Ensure result is diatonic (adjustForChord may produce non-diatonic chord tones)
        adjusted_pitch = motif_detail::adjustToDiatonic(adjusted_pitch);
        adjusted_pitch = std::clamp(adjusted_pitch, 36, 108);

        // In RhythmSync mode, constrain Motif to safe scale tones
        // melodic_freedom controls balance between chord tones and passing tones
        // This prevents pure chord harmonization while avoiding harsh dissonance
        if (params.paradigm == GenerationParadigm::RhythmSync) {
          // Snap to safe scale tone with melodic_freedom controlling passing tone probability
          adjusted_pitch = motif_detail::snapToSafeScaleTone(adjusted_pitch, chord_root, is_minor,
                                                             degree, motif_params.melodic_freedom,
                                                             rng);
        }

        // M9: Apply role-based velocity adjustment
        uint8_t vel = role_meta.velocity_base;
        // Section-based velocity variation (only when not fixed)
        if (!motif_params.velocity_fixed) {
          if (is_chorus) {
            vel = std::min(static_cast<uint8_t>(127), static_cast<uint8_t>(vel + 10));
          } else if (section.type == SectionType::Intro || section.type == SectionType::Outro) {
            vel = static_cast<uint8_t>(vel * 0.85f);
          }
        }

        // Add main note with collision avoidance
        // Check if pitch is safe (avoids clashing with Chord track)
        uint8_t final_pitch = static_cast<uint8_t>(adjusted_pitch);
        if (!harmony.isPitchSafe(final_pitch, absolute_tick, note.duration, TrackRole::Motif)) {
          // Use SafePitchResolver via getSafePitch() - always returns a valid pitch
          // This prevents note deletion and ensures melodic continuity
          final_pitch = harmony.getSafePitch(static_cast<uint8_t>(adjusted_pitch), absolute_tick,
                                             note.duration, TrackRole::Motif, MOTIF_LOW, MOTIF_HIGH);
        }
        track.addNote(
            factory.create(absolute_tick, note.duration, final_pitch, vel, NoteSource::Motif));

        // Track that we added a note to this bar (for bar coverage guard)
        bar_note_count[current_bar]++;

        // L4: Add octave doubling for chorus (if role allows)
        if (add_octave) {
          int octave_pitch = final_pitch + 12;
          if (octave_pitch <= 108 &&
              harmony.isPitchSafe(static_cast<uint8_t>(octave_pitch), absolute_tick, note.duration,
                                  TrackRole::Motif)) {
            uint8_t octave_vel = static_cast<uint8_t>(vel * 0.85f);
            track.addNote(factory.create(absolute_tick, note.duration,
                                         static_cast<uint8_t>(octave_pitch), octave_vel,
                                         NoteSource::Motif));
          }
        }
      }
    }
    sec_idx++;
  }
}

}  // namespace midisketch
