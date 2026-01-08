#include "track/chord_track.h"
#include "core/chord.h"
#include "core/mood_utils.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "core/timing_constants.h"
#include "core/track_layer.h"
#include "core/velocity.h"
#include "track/bass.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <random>

namespace midisketch {

// =============================================================================
// C10: Chord Track Layer Architecture
// =============================================================================
//
// L1 (Structural Layer):
//   - generateCloseVoicings()     - Basic close position voicings
//   - generateOpenVoicings()      - Drop 2 voicings (C3: Drop2 subtype)
//   - generateDrop3Voicings()     - C3: Drop 3 voicings
//   - generateSpreadVoicings()    - C3: Wide spread voicings
//   - generateRootlessVoicings()  - C4: 4-voice rootless voicings
//   - selectChordExtension()      - Chord extension selection (sus, 7th, 9th)
//
// L2 (Identity Layer):
//   - voicingDistance()           - Voice leading distance calculation
//   - countCommonTones()          - Common tone detection
//   - hasParallelFifthsOrOctaves() - C2: Parallel motion detection
//   - getParallelPenalty()        - C2: Mood-dependent parallel penalty
//   - selectVoicing()             - Voice leading with parallel avoidance
//   - selectOpenVoicingSubtype()  - C3: Open voicing subtype selection
//
// L3 (Safety Layer):
//   - clashesWithBass()           - Bass collision detection
//   - voicingClashesWithBass()    - Voicing-level clash check
//   - removeClashingPitch()       - Clash pitch removal
//   - getSafeDuration()           - Duration adjustment for bass
//   - findBassClashInRange()      - Time-range clash detection
//
// L4 (Performance Layer):
//   - generateChordBar()          - Rhythm pattern application
//   - selectRhythm()              - Rhythm pattern selection
//   - HarmonicRhythmInfo          - Harmonic density control
//   - Anticipation (in main loop) - Chord anticipation
//
// =============================================================================

namespace {

// Local aliases for timing constants
constexpr Tick WHOLE = TICK_WHOLE;
constexpr Tick HALF = TICK_HALF;
constexpr Tick QUARTER = TICK_QUARTER;
constexpr Tick EIGHTH = TICK_EIGHTH;

// Voicing type for different musical contexts
enum class VoicingType {
  Close,    // Standard close voicing
  Open,     // Open voicing (wider spread)
  Rootless  // 4-voice rootless (root omitted, bass handles it)
};

// A voiced chord with absolute pitches
struct VoicedChord {
  std::array<uint8_t, 5> pitches;  // Up to 5 voices (for 9th chords)
  uint8_t count;                   // Number of voices
  VoicingType type;                // Voicing style used
  OpenVoicingType open_subtype = OpenVoicingType::Drop2;  // C3: Open voicing subtype
};

// Calculate distance between two voicings (for voice leading)
int voicingDistance(const VoicedChord& prev, const VoicedChord& next) {
  int total = 0;
  size_t min_count = std::min(prev.count, next.count);
  for (size_t i = 0; i < min_count; ++i) {
    int diff = static_cast<int>(next.pitches[i]) - static_cast<int>(prev.pitches[i]);
    total += std::abs(diff);
  }
  return total;
}

// Count common tones between two voicings
int countCommonTones(const VoicedChord& prev, const VoicedChord& next) {
  int common = 0;
  for (size_t i = 0; i < prev.count; ++i) {
    for (size_t j = 0; j < next.count; ++j) {
      // Consider octave equivalence
      if (prev.pitches[i] % 12 == next.pitches[j] % 12) {
        common++;
        break;
      }
    }
  }
  return common;
}

// Check for parallel fifths or octaves between two voicings
// Returns true if parallel motion in perfect intervals is detected
bool hasParallelFifthsOrOctaves(const VoicedChord& prev, const VoicedChord& next) {
  size_t count = std::min(prev.count, next.count);
  if (count < 2) return false;

  for (size_t i = 0; i < count; ++i) {
    for (size_t j = i + 1; j < count; ++j) {
      // Calculate intervals (mod 12 for octave equivalence)
      int prev_interval = std::abs(static_cast<int>(prev.pitches[i]) -
                                   static_cast<int>(prev.pitches[j])) % 12;
      int next_interval = std::abs(static_cast<int>(next.pitches[i]) -
                                   static_cast<int>(next.pitches[j])) % 12;

      // Check for P5 (7 semitones) or P8/unison (0 semitones)
      bool prev_is_perfect = (prev_interval == 7 || prev_interval == 0);
      bool next_is_perfect = (next_interval == 7 || next_interval == 0);

      if (prev_is_perfect && next_is_perfect && prev_interval == next_interval) {
        // Both intervals are the same perfect interval
        // Check if both voices move in the same direction (parallel motion)
        int motion_i = static_cast<int>(next.pitches[i]) - static_cast<int>(prev.pitches[i]);
        int motion_j = static_cast<int>(next.pitches[j]) - static_cast<int>(prev.pitches[j]);

        // Parallel motion: both move same direction (and not stationary)
        if (motion_i != 0 && motion_j != 0 &&
            ((motion_i > 0) == (motion_j > 0))) {
          return true;
        }
      }
    }
  }
  return false;
}

// Generate close voicings for a chord
std::vector<VoicedChord> generateCloseVoicings(uint8_t root, const Chord& chord) {
  std::vector<VoicedChord> voicings;

  for (int inversion = 0; inversion < chord.note_count; ++inversion) {
    for (uint8_t base_octave = CHORD_LOW; base_octave <= CHORD_HIGH - 12; base_octave += 12) {
      VoicedChord v{};
      v.count = chord.note_count;
      v.type = VoicingType::Close;

      bool valid = true;
      for (uint8_t i = 0; i < chord.note_count; ++i) {
        if (chord.intervals[i] < 0) {
          v.count = i;
          break;
        }

        uint8_t voice_idx = (i + inversion) % chord.note_count;
        int pitch = root + chord.intervals[voice_idx];

        if (i == 0) {
          pitch = base_octave + (pitch % 12);
        } else {
          pitch = base_octave + (pitch % 12);
          while (pitch <= v.pitches[i - 1]) {
            pitch += 12;
          }
        }

        if (pitch < CHORD_LOW || pitch > CHORD_HIGH) {
          valid = false;
          break;
        }

        v.pitches[i] = static_cast<uint8_t>(pitch);
      }

      if (valid && v.count >= 3) {
        voicings.push_back(v);
      }
    }
  }

  return voicings;
}

// Generate open voicings (wider spread, drop 2 style)
std::vector<VoicedChord> generateOpenVoicings(uint8_t root, const Chord& chord) {
  std::vector<VoicedChord> voicings;

  for (uint8_t base_octave = CHORD_LOW; base_octave <= CHORD_HIGH - 24; base_octave += 12) {
    // Drop 2 voicing: drop the second voice from top down an octave
    // Result: bass-low-high pattern with wider spread
    VoicedChord v{};
    v.count = chord.note_count;
    v.type = VoicingType::Open;

    bool valid = true;
    std::array<int, 4> raw_pitches{};

    // First, calculate close position
    for (uint8_t i = 0; i < chord.note_count && i < 4; ++i) {
      if (chord.intervals[i] < 0) {
        v.count = i;
        break;
      }
      int pitch = root + chord.intervals[i];
      raw_pitches[i] = base_octave + (pitch % 12);
      // Stack in close position first
      if (i > 0 && raw_pitches[i] <= raw_pitches[i - 1]) {
        raw_pitches[i] += 12;
      }
    }

    // Drop the second voice down an octave for open voicing
    if (v.count >= 3) {
      // Original: [bass, 3rd, 5th] -> Open: [bass, 5th, 3rd+8va]
      int bass = raw_pitches[0];
      int dropped = raw_pitches[1];  // 3rd drops down
      int top = raw_pitches[2];      // 5th stays

      // Reorder: bass, dropped-octave, top
      v.pitches[0] = static_cast<uint8_t>(bass);
      v.pitches[1] = static_cast<uint8_t>(dropped + 12);  // Move 3rd up instead
      v.pitches[2] = static_cast<uint8_t>(top + 12);      // Move 5th up too

      // Sort ascending
      std::sort(v.pitches.begin(), v.pitches.begin() + v.count);

      // Validate range
      for (uint8_t i = 0; i < v.count; ++i) {
        if (v.pitches[i] < CHORD_LOW || v.pitches[i] > CHORD_HIGH) {
          valid = false;
          break;
        }
      }
    }

    if (valid && v.count >= 3) {
      voicings.push_back(v);
    }
  }

  return voicings;
}

// C3: Generate Drop 3 voicings (drop 3rd voice from top down an octave)
// Creates wider spread than Drop 2, useful for big band / orchestral contexts
std::vector<VoicedChord> generateDrop3Voicings(uint8_t root, const Chord& chord) {
  std::vector<VoicedChord> voicings;

  if (chord.note_count < 4) {
    // Drop3 requires at least 4 voices
    return voicings;
  }

  for (uint8_t base_octave = CHORD_LOW; base_octave <= CHORD_HIGH - 24; base_octave += 12) {
    VoicedChord v{};
    v.count = std::min(chord.note_count, (uint8_t)4);
    v.type = VoicingType::Open;
    v.open_subtype = OpenVoicingType::Drop3;

    bool valid = true;
    std::array<int, 4> raw_pitches{};

    // Build close position first
    for (uint8_t i = 0; i < v.count; ++i) {
      if (chord.intervals[i] < 0) {
        v.count = i;
        break;
      }
      int pitch = root + chord.intervals[i];
      raw_pitches[i] = base_octave + 12 + (pitch % 12);  // Start octave higher
      if (i > 0 && raw_pitches[i] <= raw_pitches[i - 1]) {
        raw_pitches[i] += 12;
      }
    }

    // Drop the 3rd voice from top down an octave
    // Close: [root, 3rd, 5th, 7th] -> Drop3: [root, 5th-8va, 3rd, 7th]
    if (v.count >= 4) {
      int dropped = raw_pitches[1] - 12;  // Drop 3rd down
      v.pitches[0] = static_cast<uint8_t>(std::max(static_cast<int>(CHORD_LOW), dropped));
      v.pitches[1] = static_cast<uint8_t>(raw_pitches[0]);  // Root
      v.pitches[2] = static_cast<uint8_t>(raw_pitches[2]);  // 5th
      v.pitches[3] = static_cast<uint8_t>(raw_pitches[3]);  // 7th

      // Sort ascending
      std::sort(v.pitches.begin(), v.pitches.begin() + v.count);

      // Validate range
      for (uint8_t i = 0; i < v.count; ++i) {
        if (v.pitches[i] < CHORD_LOW || v.pitches[i] > CHORD_HIGH) {
          valid = false;
          break;
        }
      }
    } else {
      valid = false;
    }

    if (valid && v.count >= 3) {
      voicings.push_back(v);
    }
  }

  return voicings;
}

// C3: Generate Spread voicings (wide intervallic spacing 1-5-10 style)
// Creates open, transparent texture suitable for pads and atmospheric sections
std::vector<VoicedChord> generateSpreadVoicings(uint8_t root, const Chord& chord) {
  std::vector<VoicedChord> voicings;

  for (uint8_t base_octave = CHORD_LOW; base_octave <= CHORD_HIGH - 24; base_octave += 12) {
    VoicedChord v{};
    v.count = std::min(chord.note_count, (uint8_t)4);
    v.type = VoicingType::Open;
    v.open_subtype = OpenVoicingType::Spread;

    bool valid = true;

    // Spread voicing: distribute across 2+ octaves
    // Pattern: Root in bass, 5th in middle, 3rd+7th on top
    int root_pitch = base_octave + (root % 12);
    int fifth_pitch = root_pitch + 7 + 12;  // 5th up an octave
    int third_pitch = root_pitch + chord.intervals[1] + 24;  // 3rd up two octaves

    v.pitches[0] = static_cast<uint8_t>(root_pitch);
    v.pitches[1] = static_cast<uint8_t>(fifth_pitch);
    v.pitches[2] = static_cast<uint8_t>(third_pitch);
    v.count = 3;

    // Add 7th if available
    if (chord.note_count >= 4 && chord.intervals[3] >= 0) {
      int seventh_pitch = root_pitch + chord.intervals[3] + 12;  // 7th one octave up
      // Insert in correct sorted position
      if (seventh_pitch < third_pitch) {
        v.pitches[3] = v.pitches[2];
        v.pitches[2] = static_cast<uint8_t>(seventh_pitch);
      } else {
        v.pitches[3] = static_cast<uint8_t>(seventh_pitch);
      }
      v.count = 4;
    }

    // Sort and validate
    std::sort(v.pitches.begin(), v.pitches.begin() + v.count);
    for (uint8_t i = 0; i < v.count; ++i) {
      if (v.pitches[i] < CHORD_LOW || v.pitches[i] > CHORD_HIGH) {
        valid = false;
        break;
      }
    }

    if (valid && v.count >= 3) {
      voicings.push_back(v);
    }
  }

  return voicings;
}

// Check if a pitch class creates a dissonant interval with bass (minor 2nd / major 7th)
bool clashesWithBass(int pitch_class, int bass_pitch_class) {
  int interval = std::abs(pitch_class - bass_pitch_class);
  if (interval > 6) interval = 12 - interval;
  return interval == 1;  // Minor 2nd (major 7th inverts to minor 2nd)
}

// C4: Generate rootless voicings (up to 4-voice, root omitted for bass)
// bass_root: the bass note's pitch class (0-11), or -1 if unknown
// Now supports 4-voice rootless with safe tension additions
std::vector<VoicedChord> generateRootlessVoicings(uint8_t root, const Chord& chord,
                                                   int bass_root_pc = -1) {
  std::vector<VoicedChord> voicings;

  // Rootless voicing: omit root, use 3rd + 5th + 7th + optional 9th
  // Key principle: avoid notes that clash with bass (minor 2nd / major 7th)
  for (uint8_t base_octave = CHORD_LOW; base_octave <= CHORD_HIGH - 12; base_octave += 12) {
    VoicedChord v{};
    v.type = VoicingType::Rootless;

    bool is_minor = (chord.note_count >= 2 && chord.intervals[1] == 3);
    bool is_dominant = (chord.note_count >= 4 && chord.intervals[3] == 10 && chord.intervals[1] == 4);
    int root_pc = root % 12;

    // Build rootless voicing: 3rd, 5th, 7th, + optional 9th (C4 enhancement)
    std::array<int, 5> intervals_rootless{};
    int voice_count = 3;

    if (is_dominant) {
      // Dominant 7th: M3, P5, m7, 9th
      intervals_rootless = {4, 7, 10, 14, -1};  // 14 = 9th (octave + 2)
      voice_count = 4;
    } else if (is_minor) {
      // Minor: m3, P5, m7, optional 9th or 11th
      int extension = 14;  // 9th (sounds natural on minor)
      // Check if 9th clashes with bass
      if (bass_root_pc >= 0) {
        int ninth_pc = (root_pc + 2) % 12;
        if (clashesWithBass(ninth_pc, bass_root_pc)) {
          extension = 17;  // Use 11th instead (octave + 5)
        }
      }
      intervals_rootless = {3, 7, 10, extension, -1};
      voice_count = 4;
    } else {
      // Major: M3, P5, + choose safe 7th + optional 9th
      // M7 (11 semitones) clashes with bass if bass is on root
      int seventh = 9;  // Default to 6th (safe)
      int ninth = 14;   // 9th

      // If bass pitch class is known, check if M7 would clash
      if (bass_root_pc >= 0) {
        int m7_pc = (root_pc + 11) % 12;
        if (!clashesWithBass(m7_pc, bass_root_pc)) {
          seventh = 11;  // M7 is safe, use it for richer sound
        }
        // Check 9th clash
        int ninth_pc = (root_pc + 2) % 12;
        if (clashesWithBass(ninth_pc, bass_root_pc)) {
          ninth = -1;  // Skip 9th
        }
      }

      if (ninth > 0) {
        intervals_rootless = {4, 7, seventh, ninth, -1};
        voice_count = 4;
      } else {
        intervals_rootless = {4, 7, seventh, -1, -1};
        voice_count = 3;
      }
    }

    bool valid = true;
    v.count = 0;
    for (int i = 0; i < voice_count; ++i) {
      if (intervals_rootless[i] < 0) {
        break;
      }
      int pitch = root + intervals_rootless[i];
      pitch = base_octave + (pitch % 12);

      // Handle octave stacking for higher extensions
      if (intervals_rootless[i] >= 12) {
        pitch = base_octave + 12 + ((intervals_rootless[i] - 12) % 12);
      }

      if (v.count > 0 && pitch <= v.pitches[v.count - 1]) {
        pitch += 12;
      }

      if (pitch < CHORD_LOW || pitch > CHORD_HIGH) {
        // Skip this voice if out of range
        if (v.count >= 3) break;  // We have enough voices
        valid = false;
        break;
      }

      // Additional check: skip voicing if this pitch clashes with bass
      if (bass_root_pc >= 0 && clashesWithBass(pitch % 12, bass_root_pc)) {
        // Skip this voice but continue with others
        continue;
      }

      v.pitches[v.count] = static_cast<uint8_t>(pitch);
      v.count++;
    }

    if (valid && v.count >= 3) {
      voicings.push_back(v);
    }
  }

  return voicings;
}

// C3: Select open voicing subtype based on section and chord context
OpenVoicingType selectOpenVoicingSubtype(SectionType section, Mood mood,
                                          const Chord& chord, std::mt19937& rng) {
  bool is_ballad = MoodClassification::isBallad(mood);
  bool is_dramatic = MoodClassification::isDramatic(mood) ||
                     mood == Mood::DarkPop;
  bool has_7th = (chord.note_count >= 4 && chord.intervals[3] >= 0);

  // Spread voicing for atmospheric sections (Intro, Interlude, Bridge)
  if (is_ballad && (section == SectionType::Intro ||
                    section == SectionType::Interlude ||
                    section == SectionType::Bridge)) {
    return OpenVoicingType::Spread;
  }

  // Drop3 for dramatic moments with 7th chords
  if (is_dramatic && has_7th) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    if (dist(rng) < 0.4f) {
      return OpenVoicingType::Drop3;
    }
  }

  // MixBreak benefits from Spread for power
  if (section == SectionType::MixBreak) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng) < 0.3f ? OpenVoicingType::Spread : OpenVoicingType::Drop2;
  }

  // Default: Drop2 (most versatile)
  return OpenVoicingType::Drop2;
}

// Generate all possible voicings for a chord
// bass_root_pc: bass note pitch class (0-11) for collision avoidance, or -1 if unknown
std::vector<VoicedChord> generateVoicings(uint8_t root, const Chord& chord,
                                           VoicingType preferred_type,
                                           int bass_root_pc = -1,
                                           OpenVoicingType open_subtype = OpenVoicingType::Drop2) {
  std::vector<VoicedChord> voicings;

  // Always include close voicings as fallback
  auto close = generateCloseVoicings(root, chord);
  voicings.insert(voicings.end(), close.begin(), close.end());

  if (preferred_type == VoicingType::Open) {
    // C3: Generate requested open voicing subtype
    switch (open_subtype) {
      case OpenVoicingType::Drop2: {
        auto open = generateOpenVoicings(root, chord);
        voicings.insert(voicings.end(), open.begin(), open.end());
        break;
      }
      case OpenVoicingType::Drop3: {
        auto drop3 = generateDrop3Voicings(root, chord);
        voicings.insert(voicings.end(), drop3.begin(), drop3.end());
        // Also include Drop2 as fallback
        if (drop3.empty()) {
          auto open = generateOpenVoicings(root, chord);
          voicings.insert(voicings.end(), open.begin(), open.end());
        }
        break;
      }
      case OpenVoicingType::Spread: {
        auto spread = generateSpreadVoicings(root, chord);
        voicings.insert(voicings.end(), spread.begin(), spread.end());
        // Also include Drop2 as fallback
        if (spread.empty()) {
          auto open = generateOpenVoicings(root, chord);
          voicings.insert(voicings.end(), open.begin(), open.end());
        }
        break;
      }
    }
  } else if (preferred_type == VoicingType::Rootless) {
    auto rootless = generateRootlessVoicings(root, chord, bass_root_pc);
    voicings.insert(voicings.end(), rootless.begin(), rootless.end());
  }

  return voicings;
}

// Select voicing type based on section, mood, and bass pattern
// @param bass_has_root True if bass is playing the root note
VoicingType selectVoicingType(SectionType section, Mood mood, bool bass_has_root) {
  bool is_ballad = MoodClassification::isBallad(mood);
  bool is_dramatic = MoodClassification::isDramatic(mood);

  // Intro/Interlude/Outro/Chant: always close voicing for stability
  if (section == SectionType::Intro || section == SectionType::Interlude ||
      section == SectionType::Outro || section == SectionType::Chant) {
    return VoicingType::Close;
  }

  // MixBreak: open voicing for full energy
  if (section == SectionType::MixBreak) {
    return VoicingType::Open;
  }

  // When bass has root, prefer rootless voicing in B/Chorus for cleaner sound
  if (bass_has_root && (section == SectionType::B || section == SectionType::Chorus)) {
    // Higher probability of rootless when bass covers root
    if (!is_ballad) {
      return VoicingType::Rootless;
    }
  }

  // Rootless voicing for dramatic or nostalgic moods in B/Chorus
  if (is_dramatic && (section == SectionType::B || section == SectionType::Chorus)) {
    return VoicingType::Rootless;
  }

  // Open voicing for ballads in chorus (more spacious sound)
  if (is_ballad && section == SectionType::Chorus) {
    return VoicingType::Open;
  }

  // Open voicing for B section and Bridge to add variety
  if ((section == SectionType::B || section == SectionType::Bridge) && !is_ballad) {
    return VoicingType::Open;
  }

  return VoicingType::Close;
}

// Check if a voicing has any pitch that clashes with bass
bool voicingClashesWithBass(const VoicedChord& v, int bass_root_pc) {
  if (bass_root_pc < 0) return false;
  for (uint8_t i = 0; i < v.count; ++i) {
    if (clashesWithBass(v.pitches[i] % 12, bass_root_pc)) {
      return true;
    }
  }
  return false;
}

// Remove clashing pitch from voicing (returns modified voicing with reduced count)
VoicedChord removeClashingPitch(const VoicedChord& v, int bass_root_pc) {
  if (bass_root_pc < 0) return v;

  VoicedChord result{};
  result.type = v.type;
  result.count = 0;

  for (uint8_t i = 0; i < v.count; ++i) {
    if (!clashesWithBass(v.pitches[i] % 12, bass_root_pc)) {
      result.pitches[result.count] = v.pitches[i];
      result.count++;
    }
  }

  return result;
}

// C2: Get mood-dependent parallel motion penalty
// Classical/sophisticated moods enforce strict voice leading rules
// Pop/energetic moods allow parallel motion for power and energy
int getParallelPenalty(Mood mood) {
  switch (mood) {
    // Strict voice leading (classical/jazz influence)
    case Mood::Dramatic:
    case Mood::Nostalgic:
    case Mood::Ballad:
    case Mood::Sentimental:
      return -200;  // Strong penalty

    // Moderate voice leading (balanced)
    case Mood::EmotionalPop:
    case Mood::MidPop:
    case Mood::CityPop:
    case Mood::StraightPop:
      return -100;  // Medium penalty

    // Relaxed voice leading (pop/dance styles)
    case Mood::EnergeticDance:
    case Mood::IdolPop:
    case Mood::ElectroPop:
    case Mood::Yoasobi:
    case Mood::FutureBass:
    case Mood::Synthwave:
    case Mood::BrightUpbeat:
    case Mood::Anthem:
      return -30;  // Light penalty (parallel OK for power)

    // Default moderate
    default:
      return -100;
  }
}

// Select best voicing considering voice leading from previous chord
// bass_root_pc: bass note pitch class (0-11) for collision avoidance, or -1 if unknown
// rng: random number generator for tiebreaker selection
// open_subtype: C3 - which open voicing variant to prefer
// mood: C2 - affects parallel motion penalty
VoicedChord selectVoicing(uint8_t root, const Chord& chord,
                          const VoicedChord& prev_voicing, bool has_prev,
                          VoicingType preferred_type, int bass_root_pc,
                          std::mt19937& rng,
                          OpenVoicingType open_subtype = OpenVoicingType::Drop2,
                          Mood mood = Mood::StraightPop) {
  std::vector<VoicedChord> candidates = generateVoicings(root, chord, preferred_type,
                                                          bass_root_pc, open_subtype);

  // Filter out voicings that clash with bass, or remove the clashing pitch
  if (bass_root_pc >= 0) {
    std::vector<VoicedChord> filtered;
    for (const auto& v : candidates) {
      if (!voicingClashesWithBass(v, bass_root_pc)) {
        filtered.push_back(v);
      } else {
        // Try removing the clashing pitch
        VoicedChord cleaned = removeClashingPitch(v, bass_root_pc);
        if (cleaned.count >= 2) {  // Need at least 2 notes for a chord
          filtered.push_back(cleaned);
        }
      }
    }
    if (!filtered.empty()) {
      candidates = std::move(filtered);
    }
    // If all candidates clash, keep original candidates (better than nothing)
  }

  if (candidates.empty()) {
    // Fallback: simple root position, avoiding clashing pitches
    VoicedChord fallback{};
    fallback.count = 0;
    fallback.type = VoicingType::Close;
    for (uint8_t i = 0; i < chord.note_count && i < 4; ++i) {
      if (chord.intervals[i] >= 0) {
        int pitch = std::clamp(root + chord.intervals[i], (int)CHORD_LOW, (int)CHORD_HIGH);
        // Skip if clashes with bass
        if (bass_root_pc >= 0 && clashesWithBass(pitch % 12, bass_root_pc)) {
          continue;
        }
        fallback.pitches[fallback.count] = static_cast<uint8_t>(pitch);
        fallback.count++;
      }
    }
    return fallback;
  }

  if (!has_prev) {
    // First chord: prefer the preferred type in middle register
    // Collect tied best candidates for random selection
    std::vector<size_t> tied_indices;
    int best_score = -1000;
    for (size_t i = 0; i < candidates.size(); ++i) {
      int dist = std::abs(candidates[i].pitches[0] - 60);  // Distance from C4
      int type_bonus = (candidates[i].type == preferred_type) ? 50 : 0;
      int score = type_bonus - dist;
      if (score > best_score) {
        tied_indices.clear();
        tied_indices.push_back(i);
        best_score = score;
      } else if (score == best_score) {
        tied_indices.push_back(i);
      }
    }
    // Random selection among tied candidates
    std::uniform_int_distribution<size_t> dist(0, tied_indices.size() - 1);
    return candidates[tied_indices[dist(rng)]];
  }

  // Voice leading: prefer common tones, minimal movement, and preferred type
  // Collect tied best candidates for random selection
  std::vector<size_t> tied_indices;
  int best_score = -1000;

  for (size_t i = 0; i < candidates.size(); ++i) {
    int common = countCommonTones(prev_voicing, candidates[i]);
    int distance = voicingDistance(prev_voicing, candidates[i]);
    int type_bonus = (candidates[i].type == preferred_type) ? 30 : 0;

    // C2: Penalize parallel fifths/octaves based on mood
    int parallel_penalty = hasParallelFifthsOrOctaves(prev_voicing, candidates[i])
                               ? getParallelPenalty(mood)
                               : 0;

    // Score: prioritize type match, common tones, avoid parallels, minimize movement
    int score = type_bonus + common * 100 + parallel_penalty - distance;

    if (score > best_score) {
      tied_indices.clear();
      tied_indices.push_back(i);
      best_score = score;
    } else if (score == best_score) {
      tied_indices.push_back(i);
    }
  }

  // Random selection among tied candidates
  std::uniform_int_distribution<size_t> dist(0, tied_indices.size() - 1);
  return candidates[tied_indices[dist(rng)]];
}

// Chord rhythm pattern types
enum class ChordRhythm {
  Whole,      // Intro: whole note
  Half,       // A section: half notes
  Quarter,    // B section: quarter notes
  Eighth      // Chorus: eighth note pulse
};

// Harmonic rhythm: how often chords change
enum class HarmonicDensity {
  Slow,       // Chord changes every 2 bars (Intro)
  Normal,     // Chord changes every bar (A, B)
  Dense       // Chord may change mid-bar at phrase ends (B end, Chorus)
};

// Determines harmonic density based on section and mood
struct HarmonicRhythmInfo {
  HarmonicDensity density;
  bool double_at_phrase_end;  // Add extra chord change at phrase end

  static HarmonicRhythmInfo forSection(SectionType section, Mood mood) {
    bool is_ballad = MoodClassification::isBallad(mood);

    switch (section) {
      case SectionType::Intro:
      case SectionType::Interlude:
        return {HarmonicDensity::Slow, false};
      case SectionType::Outro:
        return {HarmonicDensity::Slow, false};
      case SectionType::A:
        return {HarmonicDensity::Normal, false};
      case SectionType::B:
        return {HarmonicDensity::Normal, !is_ballad};
      case SectionType::Chorus:
        return {is_ballad ? HarmonicDensity::Normal : HarmonicDensity::Dense,
                !is_ballad};
      case SectionType::Bridge:
        return {HarmonicDensity::Normal, false};
      case SectionType::Chant:
        // Chant section: slow, sustained chords
        return {HarmonicDensity::Slow, false};
      case SectionType::MixBreak:
        // MIX section: driving dense chords
        return {HarmonicDensity::Dense, true};
    }
    return {HarmonicDensity::Normal, false};
  }
};

// Check if a chord degree is the dominant (V)
bool isDominant(int8_t degree) {
  return degree == 4;  // V chord
}

// Select appropriate chord extension based on context
ChordExtension selectChordExtension(int8_t degree, SectionType section,
                                     int bar_in_section, int section_bars,
                                     const ChordExtensionParams& ext_params,
                                     std::mt19937& rng) {
  if (!ext_params.enable_sus && !ext_params.enable_7th && !ext_params.enable_9th) {
    return ChordExtension::None;
  }

  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  float roll = dist(rng);

  // Determine if chord is major or minor based on degree
  bool is_minor = (degree == 1 || degree == 2 || degree == 5);
  bool is_dominant = (degree == 4);  // V chord
  bool is_tonic = (degree == 0);     // I chord

  // Sus chords work well on:
  // - First bar of section (suspension before resolution)
  // - Pre-cadence positions (bar before section end)
  if (ext_params.enable_sus) {
    bool is_sus_context = (bar_in_section == 0) ||
                          (bar_in_section == section_bars - 2);

    if (is_sus_context && !is_minor && roll < ext_params.sus_probability) {
      // sus4 more common than sus2
      return (dist(rng) < 0.7f) ? ChordExtension::Sus4 : ChordExtension::Sus2;
    }
  }

  // 7th chords work well on:
  // - Dominant (V7) - very common
  // - ii7 and vi7 - common in jazz/pop
  // - B section and Chorus for richer harmony
  if (ext_params.enable_7th) {
    bool is_seventh_context =
        (section == SectionType::B || section == SectionType::Chorus) ||
        is_dominant;

    float adjusted_prob = ext_params.seventh_probability;
    if (is_dominant) {
      adjusted_prob *= 2.0f;  // Double probability for V chord
    }

    if (is_seventh_context && roll < adjusted_prob) {
      if (is_dominant) {
        return ChordExtension::Dom7;  // V7
      } else if (is_minor) {
        return ChordExtension::Min7;  // ii7, iii7, vi7
      } else if (is_tonic) {
        return ChordExtension::Maj7;  // Imaj7
      } else {
        // IV chord - major 7th sounds good
        return ChordExtension::Maj7;
      }
    }
  }

  // 9th chords work well on:
  // - Dominant (V9) - jazz/pop feel
  // - Tonic (Imaj9) - lush sound in chorus
  // - Minor chords (ii9, vi9) - sophisticated harmony
  if (ext_params.enable_9th) {
    bool is_ninth_context =
        (section == SectionType::Chorus) ||
        (section == SectionType::B && is_dominant);

    float ninth_roll = dist(rng);
    if (is_ninth_context && ninth_roll < ext_params.ninth_probability) {
      if (is_dominant) {
        return ChordExtension::Dom9;  // V9
      } else if (is_minor) {
        return ChordExtension::Min9;  // ii9, vi9
      } else if (is_tonic) {
        return ChordExtension::Maj9;  // Imaj9
      } else {
        // IV chord - add9 for color
        return ChordExtension::Add9;
      }
    }
  }

  return ChordExtension::None;
}

// Check if the next section is a Chorus (for cadence preparation)
bool shouldAddDominantPreparation(SectionType current, SectionType next,
                                   int8_t current_degree, Mood mood) {
  // Only add dominant preparation before Chorus
  if (next != SectionType::Chorus) return false;

  // Skip for ballads (too dramatic)
  if (MoodClassification::isBallad(mood)) return false;

  // Don't add if already on dominant
  if (isDominant(current_degree)) return false;

  // Add for B -> Chorus transition
  return current == SectionType::B;
}

// Check if section ending needs a cadence fix for irregular progression lengths
// Returns true if the progression ends mid-cycle at section end
bool needsCadenceFix(uint8_t section_bars, uint8_t progression_length,
                     SectionType section, SectionType next_section) {
  // Only apply to main content sections
  if (section == SectionType::Intro || section == SectionType::Interlude ||
      section == SectionType::Outro) {
    return false;
  }

  // Check if progression divides evenly into section
  if (section_bars % progression_length == 0) {
    return false;  // Progression completes naturally
  }

  // Only apply before sections that need resolution (A, Chorus)
  if (next_section == SectionType::Intro || next_section == SectionType::Outro) {
    return false;
  }

  return true;  // Need to insert cadence
}

// Check if section type allows anticipation
bool allowsAnticipation(SectionType section) {
  switch (section) {
    case SectionType::B:
    case SectionType::Chorus:
    case SectionType::MixBreak:
      return true;
    case SectionType::A:
    case SectionType::Bridge:
      return true;  // Allow but less frequently
    case SectionType::Intro:
    case SectionType::Interlude:
    case SectionType::Outro:
    case SectionType::Chant:
      return false;
  }
  return false;
}

// Adjust rhythm one level sparser
ChordRhythm adjustSparser(ChordRhythm rhythm) {
  switch (rhythm) {
    case ChordRhythm::Eighth: return ChordRhythm::Quarter;
    case ChordRhythm::Quarter: return ChordRhythm::Half;
    case ChordRhythm::Half: return ChordRhythm::Whole;
    case ChordRhythm::Whole: return ChordRhythm::Whole;
  }
  return rhythm;
}

// Adjust rhythm one level denser
ChordRhythm adjustDenser(ChordRhythm rhythm) {
  switch (rhythm) {
    case ChordRhythm::Whole: return ChordRhythm::Half;
    case ChordRhythm::Half: return ChordRhythm::Quarter;
    case ChordRhythm::Quarter: return ChordRhythm::Eighth;
    case ChordRhythm::Eighth: return ChordRhythm::Eighth;
  }
  return rhythm;
}

// Select rhythm pattern based on section, mood, and backing density
// Uses RNG to add variation while respecting musical constraints
ChordRhythm selectRhythm(SectionType section, Mood mood,
                          BackingDensity backing_density,
                          std::mt19937& rng) {
  bool is_ballad = MoodClassification::isBallad(mood);
  bool is_energetic = MoodClassification::isDanceOriented(mood) ||
                      mood == Mood::BrightUpbeat;

  // Allowed rhythms for each section (first is most likely)
  std::vector<ChordRhythm> allowed;

  switch (section) {
    case SectionType::Intro:
    case SectionType::Interlude:
      // Intro/Interlude: keep stable, minimal variation
      allowed = {ChordRhythm::Whole, ChordRhythm::Half};
      break;
    case SectionType::Outro:
      allowed = {ChordRhythm::Half, ChordRhythm::Whole};
      break;
    case SectionType::A:
      if (is_ballad) {
        allowed = {ChordRhythm::Whole, ChordRhythm::Half};
      } else {
        allowed = {ChordRhythm::Half, ChordRhythm::Quarter, ChordRhythm::Whole};
      }
      break;
    case SectionType::B:
      if (is_ballad) {
        allowed = {ChordRhythm::Half, ChordRhythm::Quarter};
      } else {
        allowed = {ChordRhythm::Quarter, ChordRhythm::Half, ChordRhythm::Eighth};
      }
      break;
    case SectionType::Chorus:
      if (is_ballad) {
        allowed = {ChordRhythm::Half, ChordRhythm::Quarter};
      } else if (is_energetic) {
        allowed = {ChordRhythm::Eighth, ChordRhythm::Quarter};
      } else {
        allowed = {ChordRhythm::Quarter, ChordRhythm::Eighth, ChordRhythm::Half};
      }
      break;
    case SectionType::Bridge:
      if (is_ballad) {
        allowed = {ChordRhythm::Whole, ChordRhythm::Half};
      } else {
        allowed = {ChordRhythm::Half, ChordRhythm::Quarter, ChordRhythm::Whole};
      }
      break;
    case SectionType::Chant:
      // Chant section: sustained whole notes (no variation)
      allowed = {ChordRhythm::Whole};
      break;
    case SectionType::MixBreak:
      // MIX section: driving patterns
      if (is_energetic) {
        allowed = {ChordRhythm::Eighth, ChordRhythm::Quarter};
      } else {
        allowed = {ChordRhythm::Quarter, ChordRhythm::Eighth};
      }
      break;
  }

  // Weighted random selection: first option has higher probability
  ChordRhythm selected;
  if (allowed.size() == 1) {
    selected = allowed[0];
  } else {
    // 60% first option, 30% second, 10% third (if exists)
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float roll = dist(rng);
    if (roll < 0.60f) {
      selected = allowed[0];
    } else if (roll < 0.90f || allowed.size() == 2) {
      selected = allowed[1];
    } else {
      selected = allowed[allowed.size() > 2 ? 2 : 1];
    }
  }

  // Adjust rhythm based on backing density
  if (backing_density == BackingDensity::Thin) {
    selected = adjustSparser(selected);
  } else if (backing_density == BackingDensity::Thick) {
    selected = adjustDenser(selected);
  }

  return selected;
}

// Find the first bass note in a time range that clashes with a chord pitch
// Returns the start tick of the clashing bass note, or 0 if no clash found
Tick findBassClashInRange(const MidiTrack* bass_track, Tick start, Tick end,
                           int chord_pitch_pc) {
  if (bass_track == nullptr) return 0;

  for (const auto& note : bass_track->notes()) {
    if (note.startTick >= start && note.startTick < end) {
      int bass_pc = note.note % 12;
      int interval = std::abs(chord_pitch_pc - bass_pc);
      if (interval > 6) interval = 12 - interval;
      if (interval == 1) {  // Minor 2nd / Major 7th clash
        return note.startTick;
      }
    }
  }
  return 0;
}

// Calculate safe duration for a chord note to avoid bass clashes
// Returns adjusted duration (may be shorter than original)
Tick getSafeDuration(const MidiTrack* bass_track, Tick start, Tick duration,
                      int chord_pitch_pc) {
  Tick clash_tick = findBassClashInRange(bass_track, start, start + duration,
                                          chord_pitch_pc);
  if (clash_tick > 0 && clash_tick > start) {
    // Shorten duration to end just before the clash
    return clash_tick - start - EIGHTH / 2;  // Small gap before clash
  }
  return duration;
}

// Generate chord notes for one bar
// bass_track: optional, for checking approach note clashes
void generateChordBar(MidiTrack& track, Tick bar_start,
                      const VoicedChord& voicing, ChordRhythm rhythm,
                      SectionType section, Mood mood,
                      const MidiTrack* bass_track = nullptr) {
  uint8_t vel = calculateVelocity(section, 0, mood);
  uint8_t vel_weak = static_cast<uint8_t>(vel * 0.8f);

  switch (rhythm) {
    case ChordRhythm::Whole:
      // Whole note chord
      for (size_t i = 0; i < voicing.count; ++i) {
        int pc = voicing.pitches[i] % 12;
        Tick dur = getSafeDuration(bass_track, bar_start, WHOLE, pc);
        if (dur >= QUARTER) {  // Only add if duration is meaningful
          track.addNote(bar_start, dur, voicing.pitches[i], vel);
        }
      }
      break;

    case ChordRhythm::Half:
      // Two half notes
      for (size_t i = 0; i < voicing.count; ++i) {
        int pc = voicing.pitches[i] % 12;
        // First half: check for clash in first half
        Tick dur1 = getSafeDuration(bass_track, bar_start, HALF, pc);
        if (dur1 >= QUARTER) {
          track.addNote(bar_start, dur1, voicing.pitches[i], vel);
        }
        // Second half: check for clash in second half
        Tick dur2 = getSafeDuration(bass_track, bar_start + HALF, HALF, pc);
        if (dur2 >= QUARTER) {
          track.addNote(bar_start + HALF, dur2, voicing.pitches[i], vel_weak);
        }
      }
      break;

    case ChordRhythm::Quarter:
      // Four quarter notes with accents on 1 and 3
      for (int beat = 0; beat < 4; ++beat) {
        Tick tick = bar_start + beat * QUARTER;
        uint8_t beat_vel = (beat == 0 || beat == 2) ? vel : vel_weak;
        for (size_t i = 0; i < voicing.count; ++i) {
          int pc = voicing.pitches[i] % 12;
          Tick dur = getSafeDuration(bass_track, tick, QUARTER, pc);
          if (dur >= EIGHTH) {
            track.addNote(tick, dur, voicing.pitches[i], beat_vel);
          }
        }
      }
      break;

    case ChordRhythm::Eighth:
      // Eighth note pulse with syncopation
      for (int eighth = 0; eighth < 8; ++eighth) {
        Tick tick = bar_start + eighth * EIGHTH;
        uint8_t beat_vel = vel_weak;

        // Accents on beats 1 and 3
        if (eighth == 0 || eighth == 4) {
          beat_vel = vel;
        }
        // Slight accent on off-beats for energy
        else if (eighth == 3 || eighth == 7) {
          beat_vel = static_cast<uint8_t>(vel * 0.7f);
        } else {
          beat_vel = static_cast<uint8_t>(vel * 0.6f);
        }

        for (size_t i = 0; i < voicing.count; ++i) {
          // Eighth notes are short enough that clashes are less problematic
          // Still check for immediate clash at note start
          int pc = voicing.pitches[i] % 12;
          Tick clash = findBassClashInRange(bass_track, tick, tick + 1, pc);
          if (clash == 0) {  // No immediate clash
            track.addNote(tick, EIGHTH, voicing.pitches[i], beat_vel);
          }
        }
      }
      break;
  }
}

}  // namespace

void generateChordTrack(MidiTrack& track, const Song& song,
                        const GeneratorParams& params,
                        std::mt19937& rng,
                        const MidiTrack* bass_track) {
  const auto& progression = getChordProgression(params.chord_id);
  const auto& sections = song.arrangement().sections();

  // Apply max_chord_count limit for BackgroundMotif style
  // This limits the effective progression length to keep motif-style songs simple
  uint8_t effective_prog_length = progression.length;
  if (params.composition_style == CompositionStyle::BackgroundMotif &&
      params.motif_chord.max_chord_count > 0 &&
      params.motif_chord.max_chord_count < progression.length) {
    effective_prog_length = params.motif_chord.max_chord_count;
  }

  VoicedChord prev_voicing{};
  bool has_prev = false;

  // === SUS RESOLUTION TRACKING ===
  // Track previous chord extension to ensure sus chords resolve properly
  // (sus4 should resolve to 3rd on the next chord)
  ChordExtension prev_extension = ChordExtension::None;

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];
    SectionType next_section_type = (sec_idx + 1 < sections.size())
                                        ? sections[sec_idx + 1].type
                                        : section.type;

    ChordRhythm rhythm = selectRhythm(section.type, params.mood,
                                       section.backing_density, rng);
    HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section.type, params.mood);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      // Harmonic rhythm: determine chord index
      int chord_idx;
      if (harmonic.density == HarmonicDensity::Slow) {
        // Slow: chord changes every 2 bars
        chord_idx = (bar / 2) % effective_prog_length;
      } else {
        // Normal/Dense: chord changes every bar
        chord_idx = bar % effective_prog_length;
      }

      int8_t degree = progression.at(chord_idx);
      // Internal processing is always in C major; transpose at MIDI output time
      uint8_t root = degreeToRoot(degree, Key::C);

      // Select chord extension based on context
      ChordExtension extension = selectChordExtension(
          degree, section.type, bar, section.bars,
          params.chord_extension, rng);

      // === SUS RESOLUTION GUARANTEE ===
      // If previous chord was sus, force this chord to NOT be sus
      // This ensures sus4 resolves to 3rd (natural chord tone)
      if ((prev_extension == ChordExtension::Sus4 ||
           prev_extension == ChordExtension::Sus2) &&
          (extension == ChordExtension::Sus4 ||
           extension == ChordExtension::Sus2)) {
        extension = ChordExtension::None;  // Force resolution to natural chord
      }

      Chord chord = getExtendedChord(degree, extension);

      // Update prev_extension for next iteration
      prev_extension = extension;

      // Analyze bass pattern for this bar if bass track is available
      bool bass_has_root = true;  // Default assumption
      int bass_root_pc = -1;  // Bass root pitch class for collision avoidance
      if (bass_track != nullptr) {
        uint8_t bass_root = static_cast<uint8_t>(
            std::clamp(static_cast<int>(root) - 12, 28, 55));
        BassAnalysis bass_analysis =
            BassAnalysis::analyzeBar(*bass_track, bar_start, bass_root);
        bass_has_root = bass_analysis.has_root_on_beat1;

        // Get the dominant bass pitch class for this bar (check beat 1 notes)
        for (const auto& note : bass_track->notes()) {
          if (note.startTick >= bar_start &&
              note.startTick < bar_start + TICKS_PER_BEAT) {
            bass_root_pc = note.note % 12;
            break;  // Use first bass note on beat 1
          }
        }
      }

      // Select voicing type with bass coordination
      VoicingType voicing_type = selectVoicingType(section.type, params.mood,
                                                    bass_has_root);

      // C3: Select open voicing subtype based on context
      OpenVoicingType open_subtype = selectOpenVoicingSubtype(section.type, params.mood,
                                                               chord, rng);

      // Select voicing with voice leading and type consideration
      // Pass bass_root_pc to avoid clashes with bass, mood for C2 parallel penalty
      VoicedChord voicing = selectVoicing(root, chord, prev_voicing, has_prev,
                                          voicing_type, bass_root_pc, rng, open_subtype,
                                          params.mood);

      // Check if this is the last bar of the section (for cadence preparation)
      bool is_section_last_bar = (bar == section.bars - 1);

      // Add dominant preparation before Chorus
      if (is_section_last_bar &&
          shouldAddDominantPreparation(section.type, next_section_type, degree, params.mood)) {
        // Insert V chord in the second half of the last bar
        uint8_t vel = calculateVelocity(section.type, 0, params.mood);

        // First half: current chord (with bass clash check)
        for (size_t i = 0; i < voicing.count; ++i) {
          int pc = voicing.pitches[i] % 12;
          Tick dur = getSafeDuration(bass_track, bar_start, HALF, pc);
          if (dur >= QUARTER) {
            track.addNote(bar_start, dur, voicing.pitches[i], vel);
          }
        }

        // Second half: dominant (V) chord - use Dom7 if 7th extensions enabled
        int8_t dominant_degree = 4;  // V
        uint8_t dom_root = degreeToRoot(dominant_degree, Key::C);
        ChordExtension dom_ext = params.chord_extension.enable_7th
                                     ? ChordExtension::Dom7
                                     : ChordExtension::None;
        Chord dom_chord = getExtendedChord(dominant_degree, dom_ext);
        VoicedChord dom_voicing = selectVoicing(dom_root, dom_chord, voicing,
                                                 true, voicing_type, bass_root_pc, rng,
                                                 open_subtype, params.mood);

        uint8_t vel_accent = static_cast<uint8_t>(std::min(127, vel + 5));
        for (size_t i = 0; i < dom_voicing.count; ++i) {
          int pc = dom_voicing.pitches[i] % 12;
          Tick dur = getSafeDuration(bass_track, bar_start + HALF, HALF, pc);
          if (dur >= QUARTER) {
            track.addNote(bar_start + HALF, dur, dom_voicing.pitches[i], vel_accent);
          }
        }

        prev_voicing = dom_voicing;
        has_prev = true;
        continue;  // Skip normal generation for this bar
      }

      // Fix cadence for irregular progression lengths (e.g., 5-chord in 8-bar section)
      // Insert ii-V in last 2 bars when progression ends mid-cycle
      bool is_second_last_bar = (bar == section.bars - 2);
      if (is_section_last_bar && !isDominant(degree) &&
          needsCadenceFix(section.bars, progression.length, section.type, next_section_type)) {
        // Last bar: insert V chord
        int8_t dominant_degree = 4;  // V
        uint8_t dom_root = degreeToRoot(dominant_degree, Key::C);
        ChordExtension dom_ext = params.chord_extension.enable_7th
                                     ? ChordExtension::Dom7
                                     : ChordExtension::None;
        Chord dom_chord = getExtendedChord(dominant_degree, dom_ext);
        VoicedChord dom_voicing = selectVoicing(dom_root, dom_chord, prev_voicing,
                                                 has_prev, voicing_type, bass_root_pc, rng,
                                                 open_subtype, params.mood);

        generateChordBar(track, bar_start, dom_voicing, rhythm, section.type, params.mood, bass_track);
        prev_voicing = dom_voicing;
        has_prev = true;
        continue;
      }

      if (is_second_last_bar &&
          needsCadenceFix(section.bars, progression.length, section.type, next_section_type)) {
        // Second-to-last bar: insert ii chord (subdominant preparation)
        int8_t ii_degree = 1;  // ii
        uint8_t ii_root = degreeToRoot(ii_degree, Key::C);
        ChordExtension ii_ext = params.chord_extension.enable_7th
                                    ? ChordExtension::Min7
                                    : ChordExtension::None;
        Chord ii_chord = getExtendedChord(ii_degree, ii_ext);
        VoicedChord ii_voicing = selectVoicing(ii_root, ii_chord, prev_voicing,
                                               has_prev, voicing_type, bass_root_pc, rng,
                                               open_subtype, params.mood);

        generateChordBar(track, bar_start, ii_voicing, rhythm, section.type, params.mood, bass_track);
        prev_voicing = ii_voicing;
        has_prev = true;
        continue;
      }

      // Check if this is a phrase-ending bar
      // Phrase end occurs at:
      // 1. Standard 4-bar phrase boundaries (bar 3, 7, etc.)
      // 2. Chord progression cycle boundaries (last chord of progression)
      bool is_4bar_phrase_end = (bar % 4 == 3);
      bool is_chord_cycle_end = (bar % effective_prog_length == effective_prog_length - 1);
      bool is_phrase_end = harmonic.double_at_phrase_end &&
                           (is_4bar_phrase_end || is_chord_cycle_end) &&
                           (bar < section.bars - 1);

      // Dense harmonic rhythm: also allow mid-bar changes on even bars in Chorus
      // for energetic moods (more dynamic harmonic motion)
      bool is_dense_extra = (harmonic.density == HarmonicDensity::Dense) &&
                            (section.type == SectionType::Chorus) &&
                            (bar % 2 == 0) && (bar > 0) &&
                            (params.mood == Mood::EnergeticDance ||
                             params.mood == Mood::IdolPop ||
                             params.mood == Mood::Yoasobi ||
                             params.mood == Mood::FutureBass);

      if ((is_phrase_end || is_dense_extra) && harmonic.density == HarmonicDensity::Dense) {
        // Dense harmonic rhythm at phrase end: split bar into two chords
        // First half: current chord
        uint8_t vel = calculateVelocity(section.type, 0, params.mood);
        for (size_t i = 0; i < voicing.count; ++i) {
          int pc = voicing.pitches[i] % 12;
          Tick dur = getSafeDuration(bass_track, bar_start, HALF, pc);
          if (dur >= QUARTER) {
            track.addNote(bar_start, dur, voicing.pitches[i], vel);
          }
        }

        // Second half: next chord (anticipation)
        int next_chord_idx = (chord_idx + 1) % effective_prog_length;
        int8_t next_degree = progression.at(next_chord_idx);
        uint8_t next_root = degreeToRoot(next_degree, Key::C);
        ChordExtension next_ext = selectChordExtension(
            next_degree, section.type, bar + 1, section.bars,
            params.chord_extension, rng);
        Chord next_chord = getExtendedChord(next_degree, next_ext);
        VoicedChord next_voicing = selectVoicing(next_root, next_chord, voicing,
                                                  true, voicing_type, bass_root_pc, rng,
                                                  open_subtype, params.mood);

        uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);
        for (size_t i = 0; i < next_voicing.count; ++i) {
          int pc = next_voicing.pitches[i] % 12;
          Tick dur = getSafeDuration(bass_track, bar_start + HALF, HALF, pc);
          if (dur >= QUARTER) {
            track.addNote(bar_start + HALF, dur, next_voicing.pitches[i], vel_weak);
          }
        }

        prev_voicing = next_voicing;
      } else {
        // Normal chord generation for this bar
        generateChordBar(track, bar_start, voicing, rhythm, section.type, params.mood, bass_track);

        // RegisterAdd mode: add octave doublings in Chorus for intensity buildup
        // Instead of adding more instruments, we add register (octave) doublings
        if (params.arrangement_growth == ArrangementGrowth::RegisterAdd &&
            section.type == SectionType::Chorus) {
          uint8_t vel = calculateVelocity(section.type, 0, params.mood);
          uint8_t octave_vel = static_cast<uint8_t>(vel * 0.8f);  // Slightly softer

          // Add lower octave doubling for fuller sound
          for (size_t i = 0; i < voicing.count; ++i) {
            int lower_pitch = static_cast<int>(voicing.pitches[i]) - 12;
            if (lower_pitch >= CHORD_LOW && lower_pitch <= CHORD_HIGH) {
              // Use whole note duration for sustained octave layer
              track.addNote(bar_start, WHOLE, static_cast<uint8_t>(lower_pitch), octave_vel);
            }
          }
        }

        prev_voicing = voicing;
      }

      // === ANTICIPATION ===
      // Add anticipation of NEXT bar's chord at the end of THIS bar
      // Deterministic: use anticipation on specific bar positions to avoid RNG changes
      // Apply on bars 1, 3, 5 (even sections get every other bar anticipation)
      bool is_not_last_bar = (bar < section.bars - 1);
      bool deterministic_ant = (bar % 2 == 1);  // Bars 1, 3, 5, etc.
      if (is_not_last_bar && allowsAnticipation(section.type) && deterministic_ant) {
        // Skip for A/Bridge sections to keep them more stable
        if (section.type != SectionType::A && section.type != SectionType::Bridge) {
          int next_bar = bar + 1;
          int next_chord_idx = (harmonic.density == HarmonicDensity::Slow)
                                   ? (next_bar / 2) % effective_prog_length
                                   : next_bar % effective_prog_length;
          int8_t next_degree = progression.at(next_chord_idx);

          if (next_degree != degree) {
            uint8_t next_root = degreeToRoot(next_degree, Key::C);
            // Use same extension as current chord (deterministic)
            Chord next_chord = getExtendedChord(next_degree, ChordExtension::None);

            // Use close voicing (deterministic, no random)
            VoicedChord ant_voicing;
            ant_voicing.count = std::min(next_chord.note_count, (uint8_t)4);
            for (size_t i = 0; i < ant_voicing.count; ++i) {
              int pitch = 60 + next_root % 12 + next_chord.intervals[i];
              if (pitch > 72) pitch -= 12;
              ant_voicing.pitches[i] = static_cast<uint8_t>(pitch);
            }

            Tick ant_tick = bar_start + WHOLE - EIGHTH;
            uint8_t vel = calculateVelocity(section.type, 0, params.mood);
            uint8_t ant_vel = static_cast<uint8_t>(vel * 0.85f);

            for (size_t i = 0; i < ant_voicing.count; ++i) {
              int pc = ant_voicing.pitches[i] % 12;
              Tick clash = findBassClashInRange(bass_track, ant_tick, ant_tick + 1, pc);
              if (clash == 0) {
                track.addNote(ant_tick, EIGHTH, ant_voicing.pitches[i], ant_vel);
              }
            }
          }
        }
      }

      has_prev = true;
    }
  }
}

}  // namespace midisketch
