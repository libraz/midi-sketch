/**
 * @file motif.cpp
 * @brief Implementation of background motif track generation.
 */

#include "track/motif.h"
#include "core/chord.h"
#include "core/harmony_context.h"
#include "core/motif.h"
#include "core/note_factory.h"
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <vector>

namespace midisketch {

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
constexpr int SCALE_MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};       // Ionian
constexpr int SCALE_NATURAL_MINOR[7] = {0, 2, 3, 5, 7, 8, 10}; // Aeolian
constexpr int SCALE_HARMONIC_MINOR[7] = {0, 2, 3, 5, 7, 8, 11}; // Raised 7th
constexpr int SCALE_DORIAN[7] = {0, 2, 3, 5, 7, 9, 10};       // Minor with raised 6th
constexpr int SCALE_MIXOLYDIAN[7] = {0, 2, 4, 5, 7, 9, 10};   // Major with lowered 7th

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

// Avoid notes for common chords (relative to chord root in semitones)
// These notes create dissonance when held against the chord
constexpr int AVOID_MAJOR = 5;  // Perfect 4th above root (e.g., F over C major)
constexpr int AVOID_MINOR = 8;  // Minor 6th above root (e.g., C over E minor - clashes with 5th)

// Tension intervals in semitones from chord root
constexpr int TENSION_9TH = 14;   // 9th = 2nd + octave (14 semitones from root)
constexpr int TENSION_11TH = 17;  // 11th = 4th + octave (17 semitones)
constexpr int TENSION_13TH = 21;  // 13th = 6th + octave (21 semitones)

// Chord quality for tension selection
enum class ChordQuality {
  Major,
  Minor,
  Diminished
};

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
int applyTension(int base_pitch, uint8_t chord_root, ChordQuality quality,
                 std::mt19937& rng) {
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
int degreeToPitch(int degree, int base_note, int key_offset,
                  ScaleType scale = ScaleType::Major) {
  const int* scale_intervals = getScaleIntervals(scale);
  int d = ((degree % 7) + 7) % 7;
  int oct_adjust = degree / 7;
  if (degree < 0 && degree % 7 != 0) oct_adjust--;
  return base_note + oct_adjust * 12 + scale_intervals[d] + key_offset;
}

// Check if a pitch is an avoid note for the given chord
bool isAvoidNote(int pitch, uint8_t chord_root, bool is_minor) {
  int interval = ((pitch - chord_root) % 12 + 12) % 12;
  if (is_minor) {
    return interval == AVOID_MINOR;
  }
  return interval == AVOID_MAJOR;
}

// Get chord tone pitch classes for a chord
// Returns root, 3rd, 5th as pitch classes (0-11)
std::array<int, 3> getChordPitchClasses(uint8_t chord_root, bool is_minor) {
  int root_pc = chord_root % 12;
  int third_offset = is_minor ? 3 : 4;  // minor 3rd or major 3rd
  return {{
    root_pc,
    (root_pc + third_offset) % 12,
    (root_pc + 7) % 12  // perfect 5th
  }};
}

// Adjust pitch to avoid dissonance by resolving to nearest chord tone
int adjustForChord(int pitch, uint8_t chord_root, bool is_minor) {
  if (!isAvoidNote(pitch, chord_root, is_minor)) {
    return pitch;
  }

  // Find nearest chord tone
  auto chord_tones = getChordPitchClasses(chord_root, is_minor);
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

// Generate rhythm positions with musical structure
std::vector<Tick> generateRhythmPositions(MotifRhythmDensity density,
                                           MotifLength length,
                                           uint8_t note_count,
                                           std::mt19937& /* rng */) {
  Tick motif_ticks = static_cast<Tick>(length) * TICKS_PER_BAR;
  std::vector<Tick> positions;

  // Create structured rhythm patterns instead of random
  switch (density) {
    case MotifRhythmDensity::Sparse: {
      // Quarter note based - predictable pattern
      Tick step = TICKS_PER_BEAT;
      std::vector<Tick> base_pattern;
      // Always start on beat 1, then distribute remaining notes
      base_pattern.push_back(0);
      for (Tick t = step; t < motif_ticks && base_pattern.size() < note_count; t += step) {
        base_pattern.push_back(t);
      }
      positions = base_pattern;
      break;
    }
    case MotifRhythmDensity::Medium: {
      // Eighth note grid with emphasis on downbeats
      Tick step = TICKS_PER_BEAT / 2;
      // Pattern: start, then fill with preference for on-beat
      positions.push_back(0);
      for (Tick t = step; t < motif_ticks && positions.size() < note_count; t += step) {
        // Prefer on-beat positions (every other eighth)
        bool on_beat = (t % TICKS_PER_BEAT == 0);
        if (on_beat || positions.size() < note_count / 2) {
          positions.push_back(t);
        }
      }
      // Fill remaining from off-beats if needed
      for (Tick t = step; t < motif_ticks && positions.size() < note_count; t += step) {
        if (std::find(positions.begin(), positions.end(), t) == positions.end()) {
          positions.push_back(t);
        }
      }
      break;
    }
    case MotifRhythmDensity::Driving: {
      // Consistent eighth note pattern for driving feel
      Tick step = TICKS_PER_BEAT / 2;
      for (Tick t = 0; t < motif_ticks && positions.size() < note_count; t += step) {
        positions.push_back(t);
      }
      break;
    }
  }

  std::sort(positions.begin(), positions.end());
  return positions;
}

// Generate pitch sequence with antecedent-consequent structure
std::vector<int> generatePitchSequence(uint8_t note_count,
                                        MotifMotion motion,
                                        std::mt19937& rng) {
  std::vector<int> degrees;

  // Split into "question" (first half) and "answer" (second half)
  uint8_t half = note_count / 2;

  // Question phrase - ends on unstable tone
  degrees.push_back(0);  // Start on root
  int current = 0;

  for (uint8_t i = 1; i < half; ++i) {
    int step = 0;
    switch (motion) {
      case MotifMotion::Stepwise: {
        std::uniform_int_distribution<int> dist(-2, 2);
        step = dist(rng);
        if (step == 0) step = 1;
        break;
      }
      case MotifMotion::GentleLeap: {
        std::uniform_int_distribution<int> dist(-3, 3);
        step = dist(rng);
        if (step == 0) step = 1;
        break;
      }
    }
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
        std::uniform_int_distribution<int> dist(-2, 2);
        step = dist(rng);
        if (step == 0) step = -1;  // Tend downward for resolution
        break;
      }
      case MotifMotion::GentleLeap: {
        std::uniform_int_distribution<int> dist(-3, 2);
        step = dist(rng);
        if (step == 0) step = -1;
        break;
      }
    }
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

}  // namespace motif_detail

std::vector<NoteEvent> generateMotifPattern(const GeneratorParams& params,
                                             std::mt19937& rng) {
  const MotifParams& motif_params = params.motif;
  std::vector<NoteEvent> pattern;

  // Internal processing is always in C major; transpose at MIDI output time
  int key_offset = 0;
  uint8_t base_note = motif_params.register_high ? 67 : 60;  // G4 or C4

  // Generate rhythm positions
  std::vector<Tick> positions = motif_detail::generateRhythmPositions(
      motif_params.rhythm_density, motif_params.length,
      motif_params.note_count, rng);

  // Generate pitch sequence with structure
  std::vector<int> degrees = motif_detail::generatePitchSequence(
      motif_params.note_count, motif_params.motion, rng);

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

    pattern.push_back({pos, note_duration, static_cast<uint8_t>(pitch), velocity});
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

void generateMotifTrack(MidiTrack& track, Song& song,
                        const GeneratorParams& params, std::mt19937& rng,
                        const HarmonyContext& harmony) {
  // L1: Generate base motif pattern
  std::vector<NoteEvent> pattern = generateMotifPattern(params, rng);
  song.setMotifPattern(pattern);

  if (pattern.empty()) return;

  NoteFactory factory(harmony);

  const MotifParams& motif_params = params.motif;
  const auto& progression = getChordProgression(params.chord_id);
  Tick motif_length = static_cast<Tick>(motif_params.length) * TICKS_PER_BAR;

  // Apply max_chord_count limit for BackgroundMotif style
  uint8_t effective_prog_length = progression.length;
  if (params.motif_chord.max_chord_count > 0 &&
      params.motif_chord.max_chord_count < progression.length) {
    effective_prog_length = params.motif_chord.max_chord_count;
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

  for (const auto& section : sections) {
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    bool is_chorus = (section.type == SectionType::Chorus);

    // M9: Apply octave layering based on role metadata
    bool add_octave = is_chorus && motif_params.octave_layering_chorus &&
                      role_meta.allow_octave_layer;

    // L2: Determine which pattern to use based on repeat_scope
    std::vector<NoteEvent>* current_pattern = &pattern;
    std::vector<NoteEvent> section_pattern;

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

    // Repeat motif across the section
    for (Tick pos = section.start_tick; pos < section_end; pos += motif_length) {
      // Calculate which bar we're in for chord info
      uint32_t bar_in_section = (pos - section.start_tick) / TICKS_PER_BAR;

      for (const auto& note : *current_pattern) {
        Tick absolute_tick = pos + note.start_tick;
        if (absolute_tick >= section_end) continue;

        // Determine which bar this note falls in
        uint32_t note_bar = bar_in_section + (note.start_tick / TICKS_PER_BAR);
        // Use effective_prog_length to limit chord variety in BackgroundMotif mode
        int chord_idx = note_bar % effective_prog_length;
        int8_t degree = progression.at(chord_idx);

        // Get chord info (use Key::C for internal processing)
        uint8_t chord_root = degreeToRoot(degree, Key::C);
        Chord chord = getChordNotes(degree);
        bool is_minor = (chord.intervals[1] == 3);
        motif_detail::ChordQuality quality = motif_detail::getChordQuality(chord);

        // M1: Select scale type based on chord quality and mood
        ScaleType scale = motif_detail::selectScaleType(is_minor, params.mood);

        // L3: First adjust pitch to scale, then to chord for dissonance avoidance
        int adjusted_pitch = motif_detail::adjustPitchToScale(note.note, 0, scale);  // Key::C = 0
        adjusted_pitch = motif_detail::adjustForChord(adjusted_pitch, chord_root, is_minor);
        adjusted_pitch = std::clamp(adjusted_pitch, 36, 108);

        // L4: Occasionally apply tension based on chord quality (9th, 11th, 13th)
        // Only apply if chord extensions are enabled to maintain Motif/Chord consistency
        std::uniform_real_distribution<float> tension_prob(0.0f, 1.0f);
        bool extensions_enabled = params.chord_extension.enable_7th ||
                                  params.chord_extension.enable_9th;
        bool use_tension = extensions_enabled &&
                           (section.type == SectionType::B ||
                            section.type == SectionType::Chorus) &&
                           tension_prob(rng) < 0.15f;  // 15% chance in B/Chorus

        if (use_tension) {
          adjusted_pitch = motif_detail::applyTension(adjusted_pitch, chord_root, quality, rng);
          adjusted_pitch = std::clamp(adjusted_pitch, 36, 108);
        }

        // M9: Apply role-based velocity adjustment
        uint8_t vel = role_meta.velocity_base;
        // Section-based velocity variation (only when not fixed)
        if (!motif_params.velocity_fixed) {
          if (is_chorus) {
            vel = std::min(static_cast<uint8_t>(127), static_cast<uint8_t>(vel + 10));
          } else if (section.type == SectionType::Intro ||
                     section.type == SectionType::Outro) {
            vel = static_cast<uint8_t>(vel * 0.85f);
          }
        }

        // Add main note with collision avoidance
        // Check if pitch is safe (avoids clashing with Chord track)
        uint8_t final_pitch = static_cast<uint8_t>(adjusted_pitch);
        if (!harmony.isPitchSafe(final_pitch, absolute_tick, note.duration, TrackRole::Motif)) {
          // Try adjusting pitch to avoid collision
          // First try half-step up
          if (adjusted_pitch + 1 <= 108 &&
              harmony.isPitchSafe(static_cast<uint8_t>(adjusted_pitch + 1),
                                  absolute_tick, note.duration, TrackRole::Motif)) {
            final_pitch = static_cast<uint8_t>(adjusted_pitch + 1);
          }
          // Then try half-step down
          else if (adjusted_pitch - 1 >= 36 &&
                   harmony.isPitchSafe(static_cast<uint8_t>(adjusted_pitch - 1),
                                       absolute_tick, note.duration, TrackRole::Motif)) {
            final_pitch = static_cast<uint8_t>(adjusted_pitch - 1);
          }
          // Skip note if still clashing (rare case)
          else {
            continue;
          }
        }
        track.addNote(factory.create(absolute_tick, note.duration, final_pitch, vel, NoteSource::Motif));

        // L4: Add octave doubling for chorus (if role allows)
        if (add_octave) {
          int octave_pitch = final_pitch + 12;
          if (octave_pitch <= 108 &&
              harmony.isPitchSafe(static_cast<uint8_t>(octave_pitch),
                                  absolute_tick, note.duration, TrackRole::Motif)) {
            uint8_t octave_vel = static_cast<uint8_t>(vel * 0.85f);
            track.addNote(factory.create(absolute_tick, note.duration,
                          static_cast<uint8_t>(octave_pitch), octave_vel, NoteSource::Motif));
          }
        }
      }
    }
  }
}

}  // namespace midisketch
