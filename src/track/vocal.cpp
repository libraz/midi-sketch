#include "track/vocal.h"
#include "core/chord.h"
#include "core/harmony_context.h"
#include "core/velocity.h"
#include <algorithm>
#include <array>
#include <map>
#include <vector>

namespace midisketch {

namespace {

// ============================================================================
// Passaggio and Tessitura Support
// ============================================================================
//
// Passaggio: The transition zone between vocal registers (chest/head voice).
// Singing through this zone requires careful technique, so we:
// - Avoid sustaining notes in the passaggio
// - Prefer stepping through rather than landing on passaggio notes
//
// Tessitura: The comfortable singing range within the full vocal range.
// Most notes should fall within the tessitura for natural vocal quality.
// ============================================================================

// Passaggio zone: generalized for mixed voice types
// Male passaggio: ~E4-F4 (64-65), Female: ~A4-B4 (69-71)
// Using a combined zone covering common transition areas
constexpr uint8_t PASSAGGIO_LOW = 64;   // E4
constexpr uint8_t PASSAGGIO_HIGH = 71;  // B4

// Check if a pitch is in the passaggio zone
bool isInPassaggio(uint8_t pitch) {
  return pitch >= PASSAGGIO_LOW && pitch <= PASSAGGIO_HIGH;
}

// Calculate tessitura (comfortable range) from full vocal range
// Tessitura is typically the middle 60-70% of the range
struct TessituraRange {
  uint8_t low;
  uint8_t high;
  uint8_t center;
};

TessituraRange calculateTessitura(uint8_t vocal_low, uint8_t vocal_high) {
  int range = vocal_high - vocal_low;

  // Tessitura is the middle portion of the range
  // Leave ~15-20% headroom at top and bottom for climactic moments
  int margin = range / 5;  // 20% margin
  margin = std::max(margin, 3);  // At least 3 semitones margin

  TessituraRange t;
  t.low = static_cast<uint8_t>(vocal_low + margin);
  t.high = static_cast<uint8_t>(vocal_high - margin);
  t.center = static_cast<uint8_t>((t.low + t.high) / 2);

  // Ensure valid range
  if (t.low >= t.high) {
    t.low = vocal_low;
    t.high = vocal_high;
    t.center = (vocal_low + vocal_high) / 2;
  }

  return t;
}

// Check if a pitch is within the tessitura
bool isInTessitura(uint8_t pitch, const TessituraRange& tessitura) {
  return pitch >= tessitura.low && pitch <= tessitura.high;
}

// Calculate a comfort score for a pitch (higher = more comfortable)
// Returns value 0.0-1.0
float getComfortScore(uint8_t pitch, const TessituraRange& tessitura,
                       uint8_t vocal_low, uint8_t /* vocal_high */) {
  // Perfect score for tessitura center
  if (pitch == tessitura.center) return 1.0f;

  // High score for tessitura range
  if (isInTessitura(pitch, tessitura)) {
    // Score decreases slightly from center
    int dist_from_center = std::abs(static_cast<int>(pitch) - tessitura.center);
    int tessitura_half = (tessitura.high - tessitura.low) / 2;
    if (tessitura_half == 0) tessitura_half = 1;
    return 0.8f + 0.2f * (1.0f - static_cast<float>(dist_from_center) / tessitura_half);
  }

  // Reduced score for passaggio
  if (isInPassaggio(pitch)) {
    return 0.4f;
  }

  // Lower score for extreme notes
  int dist_from_tessitura = 0;
  if (pitch < tessitura.low) {
    dist_from_tessitura = tessitura.low - pitch;
  } else {
    dist_from_tessitura = pitch - tessitura.high;
  }

  // Extreme notes get scores 0.3-0.6 based on distance
  int total_margin = tessitura.low - vocal_low;
  if (total_margin == 0) total_margin = 1;
  float extremity = static_cast<float>(dist_from_tessitura) / total_margin;
  return std::max(0.3f, 0.6f - 0.3f * extremity);
}

// Adjust pitch to prefer tessitura, avoiding passaggio for sustained notes
// Returns an adjusted pitch that's more comfortable to sing
// IMPORTANT: This function is conservative to avoid breaking interval constraints
// It only makes small adjustments (max 2 semitones) to preserve melodic shape
uint8_t adjustForTessitura(uint8_t target_pitch, uint8_t /* prev_pitch */,
                            const TessituraRange& tessitura,
                            uint8_t vocal_low, uint8_t vocal_high,
                            bool is_sustained, std::mt19937& rng) {
  // Maximum adjustment to avoid breaking interval constraints
  constexpr int MAX_ADJUSTMENT = 2;  // Max 2 semitones shift

  // If already comfortable, keep it
  float comfort = getComfortScore(target_pitch, tessitura, vocal_low, vocal_high);
  if (comfort >= 0.7f) {
    return target_pitch;
  }

  // For sustained notes in passaggio, try small adjustment
  if (is_sustained && isInPassaggio(target_pitch)) {
    // Try moving by 1-2 semitones in the direction away from passaggio center
    int passaggio_center = (PASSAGGIO_LOW + PASSAGGIO_HIGH) / 2;
    int direction = (target_pitch < passaggio_center) ? -1 : 1;

    for (int adj = 1; adj <= MAX_ADJUSTMENT; ++adj) {
      uint8_t candidate = static_cast<uint8_t>(
          std::clamp(static_cast<int>(target_pitch) + direction * adj,
                     static_cast<int>(vocal_low), static_cast<int>(vocal_high)));
      if (!isInPassaggio(candidate)) {
        return candidate;
      }
    }
    // Couldn't escape passaggio with small adjustment - keep original
    return target_pitch;
  }

  // For extreme notes, use probabilistic acceptance
  // Higher comfort = more likely to keep the note
  std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);
  if (prob_dist(rng) < comfort * 1.5f) {  // Increased acceptance rate
    return target_pitch;
  }

  // Small pull toward tessitura (max 1 semitone)
  if (target_pitch < tessitura.low && target_pitch + 1 <= vocal_high) {
    return target_pitch + 1;  // Small step toward tessitura
  } else if (target_pitch > tessitura.high && target_pitch >= vocal_low + 1) {
    return target_pitch - 1;  // Small step toward tessitura
  }

  return target_pitch;
}

// Phase 2: FunctionalProfile adjustment for tension usage
float getFunctionalProfileTensionMultiplier(FunctionalProfile profile) {
  switch (profile) {
    case FunctionalProfile::Loop:
      return 1.0f;  // Standard tension
    case FunctionalProfile::TensionBuild:
      return 1.5f;  // More tension for building sections
    case FunctionalProfile::CadenceStrong:
      return 0.5f;  // Less tension for strong cadences (more resolution)
    case FunctionalProfile::Stable:
      return 0.7f;  // Slightly less tension for stable progressions
  }
  return 1.0f;
}

// Major scale semitones (relative to tonic)
constexpr int SCALE[7] = {0, 2, 4, 5, 7, 9, 11};

// Chord tones as pitch classes (0-11, semitones from C)
struct ChordTones {
  std::array<int, 5> pitch_classes;  // Pitch classes (0-11), -1 = unused
  uint8_t count;                     // Number of chord tones
};

// Scale degree to pitch class offset (C major reference)
constexpr int DEGREE_TO_PITCH_CLASS[7] = {0, 2, 4, 5, 7, 9, 11};  // C,D,E,F,G,A,B

// Get chord tones as pitch classes for a chord built on given scale degree
// Uses actual chord intervals from chord.cpp for accuracy
ChordTones getChordTones(int8_t degree) {
  ChordTones ct{};
  ct.count = 0;

  // Get root pitch class from degree
  int root_pc = DEGREE_TO_PITCH_CLASS[((degree % 7) + 7) % 7];

  // Get chord intervals from the central chord definition
  Chord chord = getChordNotes(degree);

  for (uint8_t i = 0; i < chord.note_count && i < 5; ++i) {
    if (chord.intervals[i] >= 0) {
      ct.pitch_classes[ct.count] = (root_pc + chord.intervals[i]) % 12;
      ct.count++;
    }
  }

  // Fill remaining with -1
  for (uint8_t i = ct.count; i < 5; ++i) {
    ct.pitch_classes[i] = -1;
  }

  return ct;
}

// Get nearest chord tone pitch to a given pitch
// Returns the absolute pitch of the nearest chord tone
int nearestChordTonePitch(int pitch, int8_t degree) {
  ChordTones ct = getChordTones(degree);
  int octave = pitch / 12;

  int best_pitch = pitch;
  int best_dist = 100;

  for (uint8_t i = 0; i < ct.count; ++i) {
    int ct_pc = ct.pitch_classes[i];
    if (ct_pc < 0) continue;

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

// Constrain pitch to be within max_interval of prev_pitch while respecting range
// This is the KEY function for singable melodies - prevents large jumps
int constrainInterval(int target_pitch, int prev_pitch, int max_interval,
                      int range_low, int range_high) {
  if (prev_pitch < 0) {
    // No previous pitch, just clamp to range
    return std::clamp(target_pitch, range_low, range_high);
  }

  int interval = target_pitch - prev_pitch;

  // If interval is within limit, just clamp to range
  if (std::abs(interval) <= max_interval) {
    return std::clamp(target_pitch, range_low, range_high);
  }

  // Interval too large - find closest pitch in the allowed range
  // Direction: preserve the intended direction of movement
  int direction = (interval > 0) ? 1 : -1;

  // Try the maximum allowed interval in the intended direction
  int constrained = prev_pitch + (direction * max_interval);

  // Clamp to vocal range
  constrained = std::clamp(constrained, range_low, range_high);

  // If clamping pushed us too far from target direction, try octave adjustment
  if (direction > 0 && constrained < prev_pitch) {
    // Wanted to go up but couldn't - we're at the top of range
    constrained = prev_pitch;  // Stay on same pitch instead of jumping down
  } else if (direction < 0 && constrained > prev_pitch) {
    // Wanted to go down but couldn't - we're at the bottom of range
    constrained = prev_pitch;  // Stay on same pitch instead of jumping up
  }

  return constrained;
}

// Snap a pitch to the nearest scale tone
// key_offset: transposition amount (0 = C major)
int snapToNearestScaleTone(int pitch, int key_offset) {
  // Get pitch class relative to key
  int pc = ((pitch - key_offset) % 12 + 12) % 12;

  // Find nearest scale tone
  int best_pc = SCALE[0];
  int best_dist = 12;
  for (int s : SCALE) {
    int dist = std::min(std::abs(pc - s), 12 - std::abs(pc - s));
    if (dist < best_dist) {
      best_dist = dist;
      best_pc = s;
    }
  }

  // Reconstruct pitch with snapped pitch class
  int octave = (pitch - key_offset) / 12;
  if ((pitch - key_offset) < 0 && pc != 0) {
    octave--;  // Adjust for negative pitch values
  }
  return octave * 12 + best_pc + key_offset;
}

// Find the closest chord tone to target within max_interval of prev_pitch
// Optionally prefers pitches within the tessitura range
int nearestChordToneWithinInterval(int target_pitch, int prev_pitch,
                                   int8_t chord_degree, int max_interval,
                                   int range_low, int range_high,
                                   const TessituraRange* tessitura = nullptr) {
  ChordTones ct = getChordTones(chord_degree);

  // If no previous pitch, just find nearest chord tone to target
  if (prev_pitch < 0) {
    int result = nearestChordTonePitch(target_pitch, chord_degree);
    return std::clamp(result, range_low, range_high);
  }

  int best_pitch = prev_pitch;  // Default: stay on previous pitch
  int best_score = -1000;       // Higher is better

  // Search for chord tones within max_interval of prev_pitch
  for (uint8_t i = 0; i < ct.count; ++i) {
    int ct_pc = ct.pitch_classes[i];
    if (ct_pc < 0) continue;

    // Check multiple octaves
    for (int oct = (range_low / 12); oct <= (range_high / 12) + 1; ++oct) {
      int candidate = oct * 12 + ct_pc;

      // Must be within vocal range
      if (candidate < range_low || candidate > range_high) continue;

      // Must be within max_interval of prev_pitch
      if (std::abs(candidate - prev_pitch) > max_interval) continue;

      // Calculate score: prefer closer to target, bonus for tessitura
      int dist_to_target = std::abs(candidate - target_pitch);
      int score = 100 - dist_to_target;  // Base score: closer is better

      // Tessitura bonus: prefer comfortable range
      if (tessitura != nullptr) {
        if (candidate >= tessitura->low && candidate <= tessitura->high) {
          score += 20;  // Bonus for being in tessitura
        }
        // Small penalty for passaggio (but don't exclude it)
        if (isInPassaggio(static_cast<uint8_t>(candidate))) {
          score -= 5;
        }
      }

      if (score > best_score) {
        best_score = score;
        best_pitch = candidate;
      }
    }
  }

  return best_pitch;
}


// Convert scale degree to pitch
int degreeToPitch(int degree, int octave, int key_offset) {
  int d = ((degree % 7) + 7) % 7;
  int oct_adjust = degree / 7;
  if (degree < 0 && degree % 7 != 0) oct_adjust--;
  return (octave + oct_adjust) * 12 + SCALE[d] + key_offset;
}

// NonHarmonicType is now defined in types.h

// ============================================================================
// Phrase-based melody generation (Music Theory Foundation)
// ============================================================================
//
// A musical phrase is the smallest complete musical thought, analogous to a
// sentence in language. In vocal music, phrases are bounded by breath points.
//
// Phrase Structure Principles:
// 1. Length: Typically 2-4 bars (human breath capacity ~4-8 beats at moderate tempo)
// 2. Arc: Rise to climax, then fall to resolution (tension-release)
// 3. Cadence: Phrases end on stable tones (chord tones, especially root/5th)
// 4. Breath: Rest between phrases (1/8 to 1/4 note duration)
// 5. Legato: Notes within phrase are connected (gate ~95%)
// 6. Phrase-final shortening: Last note of phrase is shorter (gate ~70%)
//
// Call-and-Response (Antecedent-Consequent):
// - First phrase (call/antecedent): ends on less stable tone (3rd, 5th, or 2nd)
// - Second phrase (response/consequent): ends on stable tone (root)
// ============================================================================

// RhythmNote is now defined in types.h

// Get rhythm patterns with strong beat marking
// Patterns designed for SINGABLE vocal lines (longer notes, clear phrasing)
// Key principle: Most notes should be 2+ eighths (1+ beat) for sustained singing
std::vector<std::vector<RhythmNote>> getRhythmPatterns() {
  return {
    // Pattern 0: Verse rhythm - 8 notes / 2 bars, singable with breathing room
    // "Ta--a Ta Ta--a Ta | Ta--a Ta Ta--a Ta" pattern
    {{0.0f, 3, true}, {1.5f, 2, false}, {2.5f, 3, true}, {3.5f, 1, false},
     {4.0f, 3, true}, {5.5f, 2, false}, {6.5f, 3, true}, {7.5f, 1, false}},
    // Pattern 1: Verse variation - 8 notes with different rhythm feel
    {{0.0f, 2, true}, {1.0f, 2, false}, {2.0f, 3, true}, {3.5f, 2, false},
     {4.5f, 2, true}, {5.5f, 2, false}, {6.5f, 3, true}, {7.5f, 1, false}},
    // Pattern 2: Chorus rhythm - 10 notes / 2 bars, energetic hook
    {{0.0f, 2, true}, {1.0f, 2, false}, {2.0f, 2, true}, {3.0f, 2, false},
     {4.0f, 2, true}, {5.0f, 2, false}, {6.0f, 2, true}, {7.0f, 2, false},
     {7.5f, 1, false}},
    // Pattern 3: Sparse rhythm - 6 notes / 2 bars (bridge/ballad)
    {{0.0f, 4, true}, {2.0f, 3, true}, {3.5f, 2, false},
     {4.5f, 4, true}, {6.5f, 2, true}, {7.5f, 1, false}},
    // Pattern 4: Pre-chorus build - 8 notes with syncopation
    {{0.0f, 2, true}, {1.0f, 3, false}, {2.5f, 2, true}, {3.5f, 2, false},
     {4.5f, 2, true}, {5.5f, 3, false}, {7.0f, 2, true}, {7.5f, 1, false}},

    // === HIGH-DENSITY PATTERNS FOR IDOL/VOCALOID ===

    // Pattern 5: High-density Verse (12 notes / 2 bars)
    // "Ta-Ta-Ta Ta-Ta Ta-Ta-Ta Ta-Ta" - idol-style rapid phrasing
    {{0.0f, 1, true}, {0.5f, 1, false}, {1.0f, 2, false}, {2.0f, 1, true},
     {2.5f, 1, false}, {3.0f, 2, false}, {4.0f, 1, true}, {4.5f, 1, false},
     {5.0f, 2, false}, {6.0f, 1, true}, {6.5f, 1, false}, {7.0f, 2, false}},

    // Pattern 6: High-density Chorus (16 notes / 2 bars)
    // Energetic hook with rapid-fire notes
    {{0.0f, 1, true}, {0.5f, 1, false}, {1.0f, 1, false}, {1.5f, 1, false},
     {2.0f, 1, true}, {2.5f, 1, false}, {3.0f, 1, false}, {3.5f, 1, false},
     {4.0f, 1, true}, {4.5f, 1, false}, {5.0f, 1, false}, {5.5f, 1, false},
     {6.0f, 1, true}, {6.5f, 1, false}, {7.0f, 1, false}, {7.5f, 1, false}},

    // Pattern 7: Syncopated High-density (14 notes / 2 bars)
    // Syncopation emphasis for groove
    {{0.0f, 1, true}, {0.5f, 2, false}, {1.5f, 1, false}, {2.0f, 1, true},
     {2.5f, 1, false}, {3.0f, 2, false}, {4.0f, 1, true}, {4.5f, 2, false},
     {5.5f, 1, false}, {6.0f, 1, true}, {6.5f, 1, false}, {7.0f, 1, false},
     {7.25f, 1, false}, {7.5f, 1, false}},

    // Pattern 8: Vocaloid Standard (20 notes / 2 bars)
    // 16th note grid, machine-like precision
    {{0.0f, 1, true}, {0.25f, 1, false}, {0.5f, 1, false}, {0.75f, 1, false},
     {1.0f, 1, true}, {1.5f, 1, false}, {2.0f, 1, true}, {2.25f, 1, false},
     {2.5f, 1, false}, {2.75f, 1, false}, {3.0f, 1, true}, {3.5f, 1, false},
     {4.0f, 1, true}, {4.25f, 1, false}, {4.5f, 1, false}, {5.0f, 1, true},
     {5.5f, 1, false}, {6.0f, 1, true}, {6.5f, 1, false}, {7.0f, 2, false}},
  };
}

// Generate YOASOBI-style 16th note grid rhythm (VocalStylePreset::Vocaloid)
// Creates dense, rapid-fire melodies characteristic of Vocaloid producers
std::vector<RhythmNote> generateVocaloidRhythm(int bars, float density,
                                                std::mt19937& rng) {
  std::vector<RhythmNote> rhythm;
  const int sixteenths_per_bar = 16;
  const int total_sixteenths = bars * sixteenths_per_bar;

  std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);

  for (int i = 0; i < total_sixteenths; ++i) {
    float beat = static_cast<float>(i) / 4.0f;  // Convert to quarter note beats

    // Strong beats (1 and 3 of each measure)
    bool is_strong = (i % 16 == 0) || (i % 16 == 8);

    // Density-based probability of note
    // Strong beats: almost always have notes
    // Weak beats: probability based on density
    float note_prob = is_strong ? 0.95f : (density * 0.8f);

    if (prob_dist(rng) < note_prob) {
      // Duration: mostly 16th notes, occasionally 8th notes on strong beats
      int eighths = (is_strong && prob_dist(rng) < 0.3f) ? 2 : 1;

      rhythm.push_back({beat, eighths, is_strong, NonHarmonicType::None});
    }
  }

  return rhythm;
}

// Generate Ultra Vocaloid rhythm (VocalStylePreset::UltraVocaloid)
// 32nd note grid, no rests, machine-gun style (e.g., "Hatsune Miku no Shoushitsu")
std::vector<RhythmNote> generateUltraVocaloidRhythm(int bars,
                                                     std::mt19937& rng) {
  std::vector<RhythmNote> rhythm;
  const int thirtyseconds_per_bar = 32;
  const int total_thirtyseconds = bars * thirtyseconds_per_bar;

  std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);

  for (int i = 0; i < total_thirtyseconds; ++i) {
    // Convert to quarter note beats (32 thirty-seconds = 4 quarter notes)
    float beat = static_cast<float>(i) / 8.0f;

    // Strong beats
    bool is_strong = (i % 32 == 0) || (i % 32 == 16);

    // Almost all positions have notes in Ultra mode (95%+)
    // Slight variation to avoid complete monotony
    if (prob_dist(rng) < 0.98f || is_strong) {
      // Duration: 32nd notes (half of an eighth = 0.5 eighths, stored as 1)
      // For 32nd notes, we use eighths=1 but the actual duration is adjusted
      rhythm.push_back({beat, 1, is_strong, NonHarmonicType::None});
    }
  }

  return rhythm;
}

// Select rhythm pattern based on section type and note density
int selectRhythmPattern(SectionType section, float note_density,
                        float sixteenth_ratio, std::mt19937& rng,
                        const StyleMelodyParams& melody_params) {
  // Ultra-high density (vocaloid mode)
  if (note_density >= melody_params.vocaloid_density_threshold &&
      sixteenth_ratio >= 0.25f) {
    return 8;  // Vocaloid Standard
  }

  // High density
  if (note_density >= melody_params.high_density_threshold) {
    if (section == SectionType::Chorus) {
      return (sixteenth_ratio >= 0.2f) ? 6 : 2;  // High-density or standard Chorus
    }
    return (sixteenth_ratio >= 0.15f) ? 5 : 0;  // High-density or standard Verse
  }

  // Medium-high density
  if (note_density >= melody_params.medium_density_threshold) {
    std::uniform_int_distribution<int> dist(0, 1);
    if (section == SectionType::Chorus) {
      return dist(rng) == 0 ? 2 : 6;  // Mix chorus patterns
    }
    if (section == SectionType::B) {
      return dist(rng) == 0 ? 4 : 7;  // Syncopated patterns
    }
    return dist(rng) == 0 ? 0 : 1;  // Standard verse patterns
  }

  // Low density (ballad)
  if (note_density < melody_params.low_density_threshold) {
    return 3;  // Sparse pattern
  }

  // Standard density - section-based selection
  switch (section) {
    case SectionType::Chorus:
      return 2;
    case SectionType::B:
      return 4;
    case SectionType::Bridge:
      return 3;
    default:
      return 0;
  }
}

// Melodic contour that respects chord tones on strong beats
struct MelodicContour {
  std::vector<int> degrees;    // Relative scale degrees
  std::vector<bool> use_chord_tone;  // Force chord tone at this position
};

// Get contour patterns with chord tone markers
// Matched to new rhythm patterns (6-10 notes per 2 bars)
// Key principle: Mostly stepwise motion (0-2 scale degrees), occasional 3rd
std::vector<MelodicContour> getMelodicContours() {
  return {
    // Contour 0: Gentle arch - stepwise, singable (verse)
    {{0, 1, 2, 2, 1, 0, 0, 1, 2, 0},
     {true, false, true, true, false, true, true, false, true, true}},
    // Contour 1: Ascending stepwise (pre-chorus, building)
    {{0, 1, 2, 2, 2, 4, 4, 2, 2, 0},
     {true, false, true, true, false, true, true, false, true, true}},
    // Contour 2: Chorus hook - clear melodic shape
    {{0, 2, 2, 4, 4, 2, 2, 0, 0, 2},
     {true, true, true, true, true, true, true, true, true, true}},
    // Contour 3: Intimate verse - neighbor tones only
    {{0, 1, 0, 0, 1, 0, -1, 0, 1, 0},
     {true, false, true, true, false, true, false, true, false, true}},
    // Contour 4: Bridge - wider range but still singable
    {{0, 2, 4, 2, 2, 0, 2, 4, 2, 0},
     {true, true, true, true, false, true, true, true, true, true}},
  };
}

// Phrase ending contours - always end on stable chord tone
// Matched to new rhythm patterns (6-10 notes)
std::vector<MelodicContour> getEndingContours() {
  return {
    // End on root (resolution) - gradual descent
    {{2, 2, 1, 0, 2, 1, 0, 0, 0, 0},
     {true, true, false, true, true, false, true, true, true, true}},
    // 5th then root (authentic cadence) - strong resolution
    {{4, 2, 4, 2, 0, 4, 2, 0, 0, 0},
     {true, true, true, true, true, true, true, true, true, true}},
    // Stepwise descent - smooth resolution
    {{2, 1, 0, 1, 0, 2, 1, 0, 0, 0},
     {true, false, true, false, true, true, false, true, true, true}},
  };
}

// Suspension types for different harmonic contexts
enum class SuspensionType {
  Sus43,  // 4-3: Most common, works on all chords
  Sus98,  // 9-8: Upper voice suspension, resolves to root
  Sus76   // 7-6: Works well on minor chords
};

// Apply suspension: use suspended note, then resolve
// Returns: {suspension_degree, resolution_degree, durations}
struct SuspensionResult {
  int suspension_degree;    // The suspended note
  int resolution_degree;    // The resolution note
  int suspension_eighths;   // Duration of suspension
  int resolution_eighths;   // Duration of resolution
  SuspensionType type;      // Type of suspension used
};

// Select appropriate suspension type based on chord context
SuspensionType selectSuspensionType(int chord_degree, std::mt19937& rng) {
  int normalized = ((chord_degree % 7) + 7) % 7;
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  float r = dist(rng);

  // Minor chords (ii, iii, vi): 7-6 works well
  if (normalized == 1 || normalized == 2 || normalized == 5) {
    if (r < 0.3f) return SuspensionType::Sus76;
    if (r < 0.6f) return SuspensionType::Sus98;
    return SuspensionType::Sus43;
  }

  // Major chords (I, IV): prefer 4-3 or 9-8
  if (r < 0.7f) return SuspensionType::Sus43;
  return SuspensionType::Sus98;
}

SuspensionResult applySuspension(int chord_root, int original_duration_eighths,
                                  SuspensionType type) {
  int suspension, resolution;

  switch (type) {
    case SuspensionType::Sus43:
      // 4-3 suspension: hold the 4th, resolve to 3rd
      suspension = chord_root + 3;  // 4th scale degree above root
      resolution = chord_root + 2;  // 3rd scale degree above root
      break;
    case SuspensionType::Sus98:
      // 9-8 suspension: hold the 9th (=2nd), resolve to root (octave)
      suspension = chord_root + 8;  // 9th (octave + 2nd) above root
      resolution = chord_root + 7;  // Octave above root
      break;
    case SuspensionType::Sus76:
      // 7-6 suspension: hold the 7th, resolve to 6th
      suspension = chord_root + 6;  // 7th scale degree above root
      resolution = chord_root + 5;  // 6th scale degree above root
      break;
  }

  // Split duration: suspension takes most, resolution takes rest
  int sus_dur = std::max(1, original_duration_eighths * 2 / 3);
  int res_dur = std::max(1, original_duration_eighths - sus_dur);

  return {suspension, resolution, sus_dur, res_dur, type};
}

// Apply anticipation: shift the note earlier and use next chord's tone
struct AnticipationResult {
  float beat_offset;        // How much earlier (negative in beats)
  int degree;               // The anticipated note (from next chord)
  int duration_eighths;     // Duration of anticipation
};

AnticipationResult applyAnticipation(int next_chord_root, int original_duration_eighths) {
  // Anticipate by an eighth note
  float offset = -0.5f;  // Half beat earlier

  // Use the root of the next chord as the anticipated note
  int anticipated = next_chord_root;

  // Short duration for the anticipation
  int dur = std::min(1, original_duration_eighths);

  return {offset, anticipated, dur};
}

// Check if suspension is appropriate at this position
bool shouldUseSuspension(float beat, SectionType section, std::mt19937& rng) {
  // Suspensions work best on strong beats at phrase beginnings
  bool is_strong = (static_cast<int>(beat) % 2 == 0);

  // More likely in emotional sections
  float prob = 0.0f;
  if (section == SectionType::B || section == SectionType::Chorus) {
    prob = is_strong ? 0.15f : 0.05f;
  } else {
    prob = is_strong ? 0.08f : 0.0f;
  }

  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  return dist(rng) < prob;
}

// Check if anticipation is appropriate at this position
bool shouldUseAnticipation(float beat, SectionType section, std::mt19937& rng) {
  // Anticipations work best on off-beats near chord changes
  bool near_bar_end = (beat >= 3.0f && beat < 4.0f) ||
                      (beat >= 7.0f && beat < 8.0f);

  float prob = 0.0f;
  if (section == SectionType::Chorus) {
    prob = near_bar_end ? 0.2f : 0.05f;
  } else if (section == SectionType::B) {
    prob = near_bar_end ? 0.12f : 0.03f;
  }

  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  return dist(rng) < prob;
}

// =============================================================================
// POST-PROCESSING PIPELINE
// =============================================================================
// These functions are applied after note generation to clean up and enhance
// the vocal track. Each function is designed for a specific purpose and can
// be extended independently.

// Sort notes by start tick for proper ordering
void sortNotesByTick(std::vector<NoteEvent>& notes) {
  std::sort(notes.begin(), notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              return a.startTick < b.startTick;
            });
}

// Remove notes with same start tick, keeping the longest duration one
void removeSameTickDuplicates(std::vector<NoteEvent>& notes) {
  if (notes.size() < 2) return;

  // Assumes notes are already sorted by startTick
  std::vector<NoteEvent> unique_notes;
  unique_notes.reserve(notes.size());

  for (size_t i = 0; i < notes.size(); ++i) {
    // Find all notes with the same start tick
    size_t j = i;
    size_t best_idx = i;
    Tick best_duration = notes[i].duration;

    while (j + 1 < notes.size() && notes[j + 1].startTick == notes[i].startTick) {
      ++j;
      if (notes[j].duration > best_duration) {
        best_duration = notes[j].duration;
        best_idx = j;
      }
    }

    // Keep only the note with the longest duration
    unique_notes.push_back(notes[best_idx]);
    i = j;  // Skip to after the duplicate group
  }

  notes = std::move(unique_notes);
}

// Fix overlapping notes by capping duration to not exceed next note's start
void fixNoteOverlaps(std::vector<NoteEvent>& notes) {
  if (notes.size() < 2) return;

  constexpr Tick MIN_GAP = 10;  // Minimum gap between notes for separation
  for (size_t i = 0; i + 1 < notes.size(); ++i) {
    Tick next_start = notes[i + 1].startTick;
    Tick max_duration = (next_start > notes[i].startTick + MIN_GAP)
                            ? (next_start - notes[i].startTick - MIN_GAP)
                            : MIN_GAP;
    if (notes[i].duration > max_duration) {
      notes[i].duration = max_duration;
    }
  }
}

// Apply breath shortening at phrase boundaries
// Creates natural breathing room between phrases (human singers need to breathe)
void applyBreathShortening(std::vector<NoteEvent>& notes,
                           const StyleMelodyParams& melody_params) {
  (void)melody_params;  // Reserved for future use
  if (notes.size() < 2) return;

  // Phrase boundary typically occurs every 4-8 beats
  constexpr Tick PHRASE_LENGTH_TICKS = TICKS_PER_BAR * 2;  // 2 bars = 1 phrase
  constexpr Tick BREATH_DURATION = TICKS_PER_BEAT / 4;    // 16th note breath

  for (size_t i = 0; i < notes.size(); ++i) {
    // Check if this note is near a phrase boundary
    Tick note_end = notes[i].startTick + notes[i].duration;
    Tick next_phrase_start = ((notes[i].startTick / PHRASE_LENGTH_TICKS) + 1) *
                             PHRASE_LENGTH_TICKS;

    // If note ends close to phrase boundary, shorten for breath
    if (note_end > next_phrase_start - BREATH_DURATION &&
        note_end <= next_phrase_start + BREATH_DURATION) {
      // Shorten the note to create breath space
      Tick new_duration = next_phrase_start - notes[i].startTick - BREATH_DURATION;
      if (new_duration > TICKS_PER_BEAT / 4) {  // Keep minimum 16th note
        notes[i].duration = new_duration;
      }
    }
  }
}

// Apply sustain extension to climax notes (highest pitch in a phrase)
// Creates emotional emphasis on melodic peaks
void applySustainExtension(std::vector<NoteEvent>& notes,
                           const StyleMelodyParams& melody_params) {
  (void)melody_params;  // Reserved for future use
  if (notes.size() < 4) return;

  // Find local maxima (notes higher than neighbors) and extend them
  for (size_t i = 1; i < notes.size() - 1; ++i) {
    bool is_local_max = notes[i].note > notes[i - 1].note &&
                        notes[i].note > notes[i + 1].note;

    if (is_local_max) {
      // Extend duration by 25% (up to limit imposed by next note)
      Tick extension = notes[i].duration / 4;
      Tick next_start = notes[i + 1].startTick;
      Tick max_duration = next_start > notes[i].startTick + 10
                              ? next_start - notes[i].startTick - 10
                              : notes[i].duration;

      notes[i].duration = std::min(notes[i].duration + extension, max_duration);

      // Also boost velocity slightly for emphasis
      notes[i].velocity = std::min(static_cast<int>(notes[i].velocity) + 5, 127);
    }
  }
}

// Main post-processing function - orchestrates all post-processing steps
void postProcessVocalTrack(MidiTrack& track, const GeneratorParams& params) {
  auto& notes = track.notes();
  if (notes.empty()) return;

  const StyleMelodyParams& melody_params = params.melody_params;

  // Step 1: Sort notes (may be out of order due to hook repetition)
  sortNotesByTick(notes);

  // Step 2: Remove same-tick duplicates (vocal can only sing one note at a time)
  removeSameTickDuplicates(notes);

  // Step 3: Fix overlapping notes (cap duration to not exceed next note)
  fixNoteOverlaps(notes);

  // Step 4: Apply humanization (only if enabled)
  if (params.humanize) {
    applyBreathShortening(notes, melody_params);
    applySustainExtension(notes, melody_params);

    // Re-fix overlaps after extensions
    fixNoteOverlaps(notes);
  }
}

// Get grid unit for duration calculation based on VocalStylePreset.
// UltraVocaloid: 32nd note (60 ticks)
// Vocaloid: 16th note (120 ticks)
// Default: 8th note (240 ticks)
Tick getGridUnit(VocalStylePreset style) {
  switch (style) {
    case VocalStylePreset::UltraVocaloid:
      return TICKS_PER_BEAT / 8;  // 32nd note = 60 ticks
    case VocalStylePreset::Vocaloid:
      return TICKS_PER_BEAT / 4;  // 16th note = 120 ticks
    default:
      return TICKS_PER_BEAT / 2;  // 8th note = 240 ticks
  }
}

}  // namespace

void generateVocalTrack(MidiTrack& track, Song& song,
                        const GeneratorParams& params, std::mt19937& rng,
                        const MidiTrack* motif_track,
                        const HarmonyContext* harmony_ctx) {
  // BackgroundMotif and SynthDriven suppression settings
  const bool is_background_motif =
      params.composition_style == CompositionStyle::BackgroundMotif;
  const bool is_synth_driven =
      params.composition_style == CompositionStyle::SynthDriven;
  // Note: suppress_vocal is implicitly handled through is_background_motif and is_synth_driven
  const MotifVocalParams& vocal_params = params.motif_vocal;

  // Phase 2: VocalAttitude and StyleMelodyParams
  const VocalAttitude vocal_attitude = params.vocal_attitude;
  const StyleMelodyParams& melody_params = params.melody_params;

  // === BPM-AWARE SINGABILITY ADJUSTMENT ===
  // At high BPM, reduce note density and limit note speed for human singability
  // Reference BPM: 120 (standard J-POP tempo)
  // At BPM 160+: 8th notes feel like 16ths at BPM 120
  const uint16_t bpm = song.bpm();
  const float bpm_factor = static_cast<float>(bpm) / 120.0f;  // 1.0 at 120 BPM

  // Vocaloid styles ignore BPM adjustment (intentionally inhuman)
  const bool is_vocaloid_style = (params.vocal_style == VocalStylePreset::Vocaloid ||
                                   params.vocal_style == VocalStylePreset::UltraVocaloid);

  // === Vocal density parameters ===
  // min_note_division: 4=quarter, 8=eighth, 16=sixteenth, 32=32nd
  // Convert to minimum duration in eighths: 8/min_note_division
  // e.g., min_note_division=4 -> min_eighths=2, min_note_division=16 -> min_eighths=0.5
  //
  // Adjust min_note_division based on VocalStylePreset to allow fast notes
  // Higher value = shorter notes allowed (32 = 32nd notes, 8 = 8th notes minimum)
  uint8_t min_note_division = melody_params.min_note_division;
  if (params.vocal_style == VocalStylePreset::UltraVocaloid) {
    // UltraVocaloid needs 32nd notes - ensure min_note_division is at least 32
    min_note_division = std::max(min_note_division, static_cast<uint8_t>(32));
  } else if (params.vocal_style == VocalStylePreset::Vocaloid) {
    // Vocaloid needs 16th notes - ensure min_note_division is at least 16
    min_note_division = std::max(min_note_division, static_cast<uint8_t>(16));
  } else if (!is_vocaloid_style) {
    // For singable styles, limit note speed based on BPM
    // At high BPM, prevent notes that would be too fast to sing
    if (bpm >= 160) {
      // At 160+ BPM: max 8th notes (no 16ths or 32nds)
      min_note_division = std::min(min_note_division, static_cast<uint8_t>(8));
    } else if (bpm >= 140) {
      // At 140-159 BPM: max 16th notes (limit rapid passages)
      min_note_division = std::min(min_note_division, static_cast<uint8_t>(16));
    }
  }
  const float min_duration_eighths = (min_note_division > 0) ? (8.0f / min_note_division) : 1.0f;

  // vocal_rest_ratio: probability of adding rests between phrases (0.0-0.5)
  const float vocal_rest_ratio = params.vocal_rest_ratio;

  // vocal_allow_extreme_leap: allow up to octave leaps (for vocaloid mode)
  const bool allow_extreme_leap = params.vocal_allow_extreme_leap;

  // Phase 2: Get FunctionalProfile from chord progression
  const ChordProgressionMeta& chord_meta = getChordProgressionMeta(params.chord_id);
  float profile_multiplier = getFunctionalProfileTensionMultiplier(chord_meta.profile);

  // Tension usage based on VocalAttitude and FunctionalProfile
  float effective_tension_usage = melody_params.tension_usage * profile_multiplier;
  if (vocal_attitude == VocalAttitude::Clean) {
    effective_tension_usage *= 0.3f;  // Reduce tension for clean
  } else if (vocal_attitude == VocalAttitude::Expressive) {
    effective_tension_usage *= 1.5f;  // Increase tension for expressive
    effective_tension_usage = std::min(effective_tension_usage, 0.6f);
  }
  // Note: Raw attitude is applied locally per section (see below)

  // Calculate max interval in scale degrees
  // Convert semitones to approximate scale degrees (7 semitones ≈ 4 scale degrees)
  int max_interval_from_params = (melody_params.max_leap_interval * 4) / 7;
  max_interval_from_params = std::clamp(max_interval_from_params, 2, 7);
  int max_interval_degrees = is_background_motif
                                 ? (vocal_params.interval_limit <= 4 ? 2 : 4)
                                 : max_interval_from_params;

  // Velocity reduction for background/synth-driven modes
  float velocity_scale = 1.0f;
  if (is_background_motif && vocal_params.prominence == VocalProminence::Background) {
    velocity_scale = 0.7f;
  } else if (is_synth_driven) {
    velocity_scale = 0.75f;  // Subdued vocals in SynthDriven mode
  }

  // Effective vocal range (adjusted based on motif track if present)
  uint8_t effective_vocal_low = params.vocal_low;
  uint8_t effective_vocal_high = params.vocal_high;

  // Adjust vocal range to avoid collision with motif track
  if (is_background_motif && motif_track != nullptr && !motif_track->empty()) {
    auto [motif_low, motif_high] = motif_track->analyzeRange();

    // If motif is in high register (above C5 = 72)
    if (motif_high > 72) {
      // Limit vocal high to avoid overlap
      effective_vocal_high = std::min(effective_vocal_high, static_cast<uint8_t>(72));
      // Ensure minimum range of one octave
      if (effective_vocal_high - effective_vocal_low < 12) {
        effective_vocal_low = std::max(static_cast<uint8_t>(48),
                                        static_cast<uint8_t>(effective_vocal_high - 12));
      }
    }
    // If motif is in low register (below C4 = 60)
    else if (motif_low < 60) {
      // Raise vocal low to avoid overlap
      effective_vocal_low = std::max(effective_vocal_low, static_cast<uint8_t>(65));
      // Ensure minimum range of one octave
      if (effective_vocal_high - effective_vocal_low < 12) {
        effective_vocal_high = std::min(static_cast<uint8_t>(96),
                                         static_cast<uint8_t>(effective_vocal_low + 12));
      }
    }
  }

  const auto& progression = getChordProgression(params.chord_id);
  // Internal processing is always in C major; transpose at MIDI output time
  int key_offset = 0;

  // Calculate tessitura for comfortable pitch selection
  TessituraRange tessitura = calculateTessitura(effective_vocal_low, effective_vocal_high);

  // Helper: clamp pitch to effective vocal range
  auto clampPitch = [&](int pitch) -> uint8_t {
    return static_cast<uint8_t>(
        std::clamp(pitch, (int)effective_vocal_low, (int)effective_vocal_high));
  };

  // Helper: get safe pitch that doesn't clash with chord/bass tracks
  auto getSafePitch = [&](int pitch, Tick start, Tick duration) -> uint8_t {
    uint8_t clamped = clampPitch(pitch);

    if (harmony_ctx == nullptr) {
      return clamped;
    }

    // Check for bass collision in low register first.
    // If collision detected, prefer moving up by octave to avoid muddy sound.
    if (harmony_ctx->hasBassCollision(clamped, start, duration)) {
      // Try octave up
      int octave_up = clamped + 12;
      if (octave_up <= effective_vocal_high) {
        // Check if octave up is also safe from bass collision
        if (!harmony_ctx->hasBassCollision(static_cast<uint8_t>(octave_up),
                                            start, duration)) {
          clamped = static_cast<uint8_t>(octave_up);
        }
      }
      // If octave up is out of range or still collides, getSafePitch will handle
    }

    // Use HarmonyContext to find a pitch that doesn't clash
    return harmony_ctx->getSafePitch(
        clamped, start, duration, TrackRole::Vocal,
        effective_vocal_low, effective_vocal_high);
  };

  // Tessitura pointer for optional use in pitch selection functions
  const TessituraRange* tessitura_ptr = is_vocaloid_style ? nullptr : &tessitura;

  // Helper: get chord info for a bar
  auto getChordInfo = [&](int bar_in_section) -> std::pair<int, bool> {
    int chord_idx = bar_in_section % progression.length;
    int8_t degree = progression.at(chord_idx);
    Chord chord = getChordNotes(degree);
    int root = (degree == 10) ? 6 : degree;  // bVII -> treat as 6
    bool is_minor = (chord.intervals[1] == 3);
    return {root, is_minor};
  };

  auto rhythm_patterns = getRhythmPatterns();
  auto melody_contours = getMelodicContours();
  auto ending_contours = getEndingContours();

  // Phrase cache for repetition
  std::map<SectionType, std::vector<NoteEvent>> phrase_cache;
  std::map<SectionType, int> section_occurrence;

  // Starting octave based on vocal range
  int center_pitch = (effective_vocal_low + effective_vocal_high) / 2;
  int base_octave = center_pitch / 12;

  // Track previous pitch for leap checking
  int prev_pitch = -1;
  int prev_interval = 0;

  const auto& sections = song.arrangement().sections();
  for (const auto& section : sections) {
    // Skip instrumental sections and call sections (no vocal melody)
    if (section.type == SectionType::Intro ||
        section.type == SectionType::Interlude ||
        section.type == SectionType::Outro ||
        section.type == SectionType::Chant ||
        section.type == SectionType::MixBreak) {
      continue;
    }

    section_occurrence[section.type]++;
    bool is_repeat = (section_occurrence[section.type] > 1);
    bool use_cached = is_repeat &&
                      (phrase_cache.find(section.type) != phrase_cache.end());

    // Modulation is applied at MIDI output time (in MidiWriter), not here.
    // This ensures consistent handling across all tracks.

    if (use_cached) {
      // Reuse cached phrase with absolute tick offset
      // Still check for clashes since chord voicings may differ
      const auto& cached = phrase_cache[section.type];
      for (const auto& note : cached) {
        Tick absolute_tick = section.start_tick + note.startTick;
        uint8_t pitch = note.note;

        // Apply getSafePitch to avoid clashes with chord track
        // (voicings may differ between repeated sections)
        if (harmony_ctx != nullptr) {
          pitch = harmony_ctx->getSafePitch(
              pitch, absolute_tick, note.duration, TrackRole::Vocal,
              effective_vocal_low, effective_vocal_high);
        }

        track.addNote(absolute_tick, note.duration, pitch, note.velocity);
        // Update prev_pitch for continuity with next section
        prev_pitch = pitch;
      }
      continue;
    }

    // Generate new phrase
    std::vector<NoteEvent> phrase_notes;
    // NOTE: Don't reset prev_pitch here - maintain continuity across sections
    // This ensures smooth melodic transitions at section boundaries

    // Get base density from style preset, apply section multiplier
    float base_density = melody_params.note_density;
    float sixteenth_ratio = melody_params.sixteenth_note_ratio;
    int contour_variation = 0;
    float note_density = base_density;

    // Section-specific melody parameters
    // register_shift: shift vocal range (semitones, positive = higher)
    int8_t register_shift = 0;
    float section_density_factor = 1.0f;

    switch (section.type) {
      case SectionType::Intro:
      case SectionType::Interlude:
      case SectionType::Outro:
      case SectionType::Chant:
      case SectionType::MixBreak:
        // These are skipped above, but handle for completeness
        section_density_factor = 0.6f;
        break;
      case SectionType::A:
        // A melody: lyrical, natural phrasing
        contour_variation = 0;
        section_density_factor = 0.95f;  // Slightly reduced for verse
        register_shift = melody_params.verse_register_shift;
        break;
      case SectionType::B:
        // B melody: building tension, syncopated
        contour_variation = 1;
        section_density_factor = 1.05f;  // Modest increase for tension build
        register_shift = melody_params.prechorus_register_shift;
        break;
      case SectionType::Chorus:
        // Chorus: climactic, dense, emphatic
        contour_variation = 2;
        section_density_factor = melody_params.chorus_density_modifier;
        register_shift = melody_params.chorus_register_shift;
        break;
      case SectionType::Bridge:
        // Bridge: contrasting, more melodic breath
        contour_variation = 4;
        section_density_factor = 0.85f;  // Lower for contrast
        register_shift = melody_params.bridge_register_shift;
        break;
    }

    // Apply section density factor
    // Skip for Vocaloid/UltraVocaloid - rhythm generator controls density
    if (!is_vocaloid_style) {
      note_density = base_density * section_density_factor;

      // === VOICE PHYSIOLOGY-BASED DENSITY ADJUSTMENT ===
      // Human vocal limits based on syllable rate research:
      // - Comfortable sustained singing: 3-5 syllables/second
      // - Fast pop/idol: 5-7 syllables/second
      // - Extreme rap: 8-10 syllables/second
      //
      // Convert density to approximate notes-per-second (NPS):
      // NPS = (density * 4) * (BPM / 60) at 8th note base
      // Target NPS by style:
      // - Ballad: 2-3 NPS
      // - Idol/Standard: 3-5 NPS
      // - Anime: 4-6 NPS
      float target_max_nps = 5.0f;  // Default comfortable limit
      switch (params.vocal_style) {
        case VocalStylePreset::Ballad:
          target_max_nps = 3.0f;  // Slow, sustained
          break;
        case VocalStylePreset::Idol:
          target_max_nps = 4.5f;  // Dance-friendly, needs space for choreography
          break;
        case VocalStylePreset::Rock:
          target_max_nps = 5.0f;  // Powerful, with shouts
          break;
        case VocalStylePreset::Anime:
          target_max_nps = 6.0f;  // Dramatic, faster passages OK
          break;
        case VocalStylePreset::CityPop:
          target_max_nps = 4.0f;  // Groove-based, not too busy
          break;
        default:
          target_max_nps = 5.0f;  // Standard
          break;
      }

      // Calculate current NPS based on density and BPM
      // Approximate: density 1.0 at BPM 120 = 4 NPS (8th note base)
      float current_nps = note_density * 4.0f * bpm_factor;

      // If current NPS exceeds target, reduce density proportionally
      if (current_nps > target_max_nps) {
        float reduction_factor = target_max_nps / current_nps;
        note_density *= reduction_factor;
      }
    }
    // Clamp to reasonable range
    note_density = std::clamp(note_density, 0.3f, 2.0f);

    // Apply register shift to effective range for this section
    // IMPORTANT: Must stay within user-specified vocal range (params.vocal_low/high)
    int section_vocal_low = static_cast<int>(effective_vocal_low) + register_shift;
    int section_vocal_high = static_cast<int>(effective_vocal_high) + register_shift;

    // First clamp to user-specified vocal range (this is the hard constraint)
    section_vocal_low = std::clamp(section_vocal_low,
                                    static_cast<int>(params.vocal_low),
                                    static_cast<int>(params.vocal_high));
    section_vocal_high = std::clamp(section_vocal_high,
                                     static_cast<int>(params.vocal_low),
                                     static_cast<int>(params.vocal_high));

    // Ensure minimum range of 5 semitones (perfect 4th) for singability
    if (section_vocal_high - section_vocal_low < 5) {
      // Center the range within the constraint
      int center = (section_vocal_low + section_vocal_high) / 2;
      section_vocal_low = std::max(static_cast<int>(params.vocal_low), center - 6);
      section_vocal_high = std::min(static_cast<int>(params.vocal_high), center + 6);
    }

    // Apply BackgroundMotif suppression (skip for Vocaloid styles)
    if (is_background_motif && !is_vocaloid_style) {
      switch (vocal_params.rhythm_bias) {
        case VocalRhythmBias::Sparse:
          note_density *= 0.5f;
          sixteenth_ratio = 0.0f;
          break;
        case VocalRhythmBias::OnBeat:
          note_density *= 0.7f;
          sixteenth_ratio *= 0.5f;
          break;
        case VocalRhythmBias::OffBeat:
          note_density *= 0.7f;
          sixteenth_ratio *= 0.5f;
          break;
      }
    }

    // Apply SynthDriven suppression (skip for Vocaloid styles)
    if (is_synth_driven && !is_vocaloid_style) {
      note_density *= 0.5f;    // Reduce density significantly
      sixteenth_ratio = 0.0f;  // No fast notes
    }

    // Phase 2: Apply Section.vocal_density to note density
    // VocalDensity::None still applies to Vocaloid (skip entire section)
    // but Sparse is ignored for Vocaloid styles
    switch (section.vocal_density) {
      case VocalDensity::None:
        continue;  // Skip this section entirely
      case VocalDensity::Sparse:
        if (!is_vocaloid_style) {
          note_density *= 0.6f;  // Reduce to 60% of current density
          sixteenth_ratio = 0.0f;  // No fast notes for sparse
        }
        break;
      case VocalDensity::Full:
        // No modification - use full density
        break;
    }

    // Phase 3: Raw attitude local application
    // Raw is only applied in sections where deviation_allowed is true
    bool apply_raw = (vocal_attitude == VocalAttitude::Raw) &&
                     section.deviation_allowed;
    bool allow_non_chord_landing = apply_raw;  // Allow non-chord tone resolution
    int raw_leap_boost = apply_raw ? 2 : 0;  // Allow larger leaps

    // Chorus hook: store first 2-bar phrase and repeat it
    std::vector<NoteEvent> chorus_hook_notes;
    bool is_chorus = (section.type == SectionType::Chorus);

    // Process 2-bar motifs
    for (uint8_t motif_start = 0; motif_start < section.bars; motif_start += 2) {
      bool is_phrase_ending = ((motif_start + 2) % 4 == 0);
      uint8_t bars_in_motif =
          std::min((uint8_t)2, (uint8_t)(section.bars - motif_start));

      Tick motif_start_tick = section.start_tick + motif_start * TICKS_PER_BAR;
      Tick relative_motif_start = motif_start * TICKS_PER_BAR;

      // === CHORUS HOOK REPETITION ===
      // In chorus: repeat the first 2-bar hook phrase throughout
      // This creates a memorable, singable hook pattern
      // Pattern: Hook (motif 0) -> Variation (motif 1) -> Hook (motif 2) -> Ending (motif 3)
      bool use_chorus_hook = is_chorus && motif_start > 0 &&
                             !chorus_hook_notes.empty() &&
                             (motif_start % 2 == 0) &&  // Every 2 bars (motif 2, 4, 6...)
                             !is_phrase_ending;  // Not at phrase endings (allow variation)

      if (use_chorus_hook) {
        // Repeat the chorus hook with optional pitch shift for climax
        int pitch_shift = 0;
        if (motif_start >= 4) {
          // Second half of chorus: transpose up slightly for climax
          pitch_shift = 2;  // One whole step up
        }

        const int MAX_HOOK_INTERVAL = allow_extreme_leap ? 12 : 7;  // Max interval for hook continuity

        for (const auto& note : chorus_hook_notes) {
          Tick absolute_tick = motif_start_tick + note.startTick;
          int varied_pitch = note.note + pitch_shift;

          // First clamp to section range
          varied_pitch = std::clamp(varied_pitch, section_vocal_low, section_vocal_high);

          // Apply interval constraint for EVERY note (not just first)
          // This ensures melodic continuity within the hook replay
          if (prev_pitch > 0) {
            int interval = varied_pitch - prev_pitch;
            if (std::abs(interval) > MAX_HOOK_INTERVAL) {
              // Constrain to max interval while preserving direction
              int direction = (interval > 0) ? 1 : -1;
              int constrained = prev_pitch + (direction * MAX_HOOK_INTERVAL);
              // Only use constrained if it's within the section range
              if (constrained >= section_vocal_low && constrained <= section_vocal_high) {
                varied_pitch = constrained;
              }
              // Otherwise keep the clamped pitch (may violate interval but stay in range)
            }
          }

          // Apply getSafePitch to avoid clashes with chord track
          uint8_t safe_pitch = getSafePitch(varied_pitch, absolute_tick, note.duration);

          // IMPORTANT: If getSafePitch violates interval constraint, prefer melodic continuity
          // Prioritize singability (max 7 semitone interval) over harmonic safety
          if (prev_pitch > 0 && std::abs(static_cast<int>(safe_pitch) - prev_pitch) > MAX_HOOK_INTERVAL) {
            // Revert to the interval-constrained pitch
            safe_pitch = static_cast<uint8_t>(varied_pitch);
          }

          track.addNote(absolute_tick, note.duration, safe_pitch, note.velocity);
          phrase_notes.push_back({note.startTick + relative_motif_start,
                                  note.duration,
                                  safe_pitch,
                                  note.velocity});

          // Update prev_pitch for continuity
          prev_pitch = safe_pitch;
        }
        continue;
      }

      // Get chord info for this 2-bar segment
      auto [chord_root1, is_minor1] = getChordInfo(motif_start);
      auto [chord_root2, is_minor2] = getChordInfo(motif_start + 1);

      // Select contour
      MelodicContour contour;
      if (is_phrase_ending) {
        std::uniform_int_distribution<size_t> end_dist(
            0, ending_contours.size() - 1);
        contour = ending_contours[end_dist(rng)];
      } else {
        std::uniform_int_distribution<size_t> cont_dist(
            0, melody_contours.size() - 1);
        size_t idx = (cont_dist(rng) + contour_variation) % melody_contours.size();
        contour = melody_contours[idx];
      }

      // Apply interval limiting for BackgroundMotif
      // Phase 3: Raw allows larger leaps
      int section_max_interval = max_interval_degrees + raw_leap_boost;
      if (is_background_motif) {
        for (auto& degree : contour.degrees) {
          degree = std::clamp(degree, -section_max_interval, section_max_interval);
        }
      }

      // Select rhythm based on vocal style
      std::vector<RhythmNote> custom_rhythm;
      const std::vector<RhythmNote>* rhythm_ptr = nullptr;

      if (params.vocal_style == VocalStylePreset::Vocaloid) {
        // YOASOBI-style 16th note grid
        custom_rhythm = generateVocaloidRhythm(bars_in_motif, note_density, rng);
        rhythm_ptr = &custom_rhythm;
      } else if (params.vocal_style == VocalStylePreset::UltraVocaloid) {
        // Ultra Vocaloid 32nd note grid
        custom_rhythm = generateUltraVocaloidRhythm(bars_in_motif, rng);
        rhythm_ptr = &custom_rhythm;
      } else {
        // Standard pattern-based selection
        int actual_rhythm = selectRhythmPattern(section.type, note_density,
                                                 sixteenth_ratio, rng,
                                                 melody_params);
        // Ensure pattern index is within bounds
        actual_rhythm = std::clamp(actual_rhythm, 0,
                                    static_cast<int>(rhythm_patterns.size()) - 1);
        rhythm_ptr = &rhythm_patterns[actual_rhythm];
      }
      const auto& rhythm = *rhythm_ptr;

      // Calculate motif end tick for duration limiting
      Tick motif_end_tick = motif_start_tick + bars_in_motif * TICKS_PER_BAR;

      // Generate notes for this motif
      size_t contour_idx = 0;
      for (size_t rn_idx = 0; rn_idx < rhythm.size(); ++rn_idx) {
        const auto& rn = rhythm[rn_idx];
        // === MIN NOTE DIVISION FILTER ===
        // Skip notes shorter than the minimum note division
        // rn.eighths is duration in eighth notes
        // min_duration_eighths: 2.0 for quarter, 1.0 for eighth, 0.5 for 16th
        if (static_cast<float>(rn.eighths) < min_duration_eighths) {
          continue;  // Skip notes shorter than minimum duration
        }

        // === BPM-AWARE SINGABILITY SKIP LOGIC ===
        // Formula-based calculation for human-singable note density
        //
        // Voice physiology limits:
        // - Comfortable sustained singing: 3-5 syllables/second
        // - Fast pop/idol: 5-7 syllables/second
        // - Maximum human limit: ~10 syllables/second (brief passages only)
        //
        // Calculation:
        // max_nps_at_bpm = (bpm / 60) * notes_per_beat_in_pattern
        // For 8th note patterns: notes_per_beat ≈ 2
        // skip_ratio = max(0, 1 - target_nps / (current_density * max_nps))
        //
        bool should_skip = false;

        // Vocaloid styles skip this check entirely (intentionally inhuman)
        if (!is_vocaloid_style) {
          // Calculate current maximum NPS based on BPM and pattern density
          // Assuming average of 2 notes per beat in the pattern
          float notes_per_beat = 2.0f * note_density;  // density scales notes
          float max_nps = (static_cast<float>(bpm) / 60.0f) * notes_per_beat;

          // Target NPS based on vocal style
          // Key insight: At high BPM, even the same NPS feels more rushed
          // because the listener's perception is tied to beats, not absolute time
          // Scale target down at high BPM to maintain "singable feel"
          float base_target_nps;
          switch (params.vocal_style) {
            case VocalStylePreset::Ballad:
              base_target_nps = 3.0f;
              break;
            case VocalStylePreset::Idol:
              base_target_nps = 4.0f;  // Lower base for dance-friendly
              break;
            case VocalStylePreset::CityPop:
              base_target_nps = 3.5f;
              break;
            case VocalStylePreset::Anime:
              base_target_nps = 5.0f;
              break;
            case VocalStylePreset::Rock:
              base_target_nps = 4.5f;
              break;
            default:
              base_target_nps = 4.5f;
              break;
          }

          // Scale target NPS inversely with BPM
          // Reference: BPM 120 = base target, BPM 180 = 67% of base target
          // Formula: target = base * (120 / bpm)^0.5
          // This gives a gentle curve that reduces target at high BPM
          float bpm_scale = std::sqrt(120.0f / static_cast<float>(bpm));
          float target_nps = base_target_nps * bpm_scale;

          // Calculate skip probability to achieve target NPS
          float skip_prob = 0.0f;
          if (max_nps > target_nps) {
            skip_prob = 1.0f - (target_nps / max_nps);
          }

          // Only skip weak beats (preserve strong beat structure for singability)
          // Chorus sections skip at 50% rate (maintain energy but stay singable)
          if (!rn.strong && skip_prob > 0.0f) {
            float effective_skip_prob = skip_prob;
            if (section.type == SectionType::Chorus) {
              effective_skip_prob *= 0.5f;  // Chorus: half the skip rate
            }
            std::uniform_real_distribution<float> skip_dist(0.0f, 1.0f);
            should_skip = skip_dist(rng) < effective_skip_prob;
          }

          // === VOCAL REST RATIO ===
          // Add additional rests for breathing room
          if (!should_skip && !rn.strong && vocal_rest_ratio > 0.0f) {
            std::uniform_real_distribution<float> rest_dist(0.0f, 1.0f);
            should_skip = rest_dist(rng) < vocal_rest_ratio;
          }
        }

        if (should_skip) continue;

        float beat_in_motif = rn.beat;
        int bar_offset = static_cast<int>(beat_in_motif / 4.0f);
        if (bar_offset >= bars_in_motif) continue;

        float beat_in_bar = beat_in_motif - bar_offset * 4.0f;
        Tick note_tick = motif_start_tick + bar_offset * TICKS_PER_BAR +
                         static_cast<Tick>(beat_in_bar * TICKS_PER_BEAT);
        Tick relative_tick = relative_motif_start + bar_offset * TICKS_PER_BAR +
                             static_cast<Tick>(beat_in_bar * TICKS_PER_BEAT);

        // Get the chord for this bar
        int current_chord_root = (bar_offset == 0) ? chord_root1 : chord_root2;

        // Get contour degree
        int contour_degree = contour.degrees[contour_idx % contour.degrees.size()];
        bool force_chord_tone = contour.use_chord_tone[contour_idx % contour.use_chord_tone.size()];

        // Calculate scale degree
        int scale_degree = current_chord_root + contour_degree;

        // Phrase ending resolution (music theory: phrases must resolve)
        // Check if this is the last note in the motif (phrase ending)
        bool is_last_note_in_motif = (contour_idx == contour.degrees.size() - 1) ||
                                      (rn_idx == rhythm.size() - 1);

        // Strong resolution at phrase boundaries
        // Music theory: phrase endings should land on stable chord tones
        if (is_phrase_ending && is_last_note_in_motif) {
          // Response phrase (consequent): strong resolution to root
          // This is the "answer" to the preceding "call" phrase
          force_chord_tone = true;

          // For response phrases, strongly prefer root for resolution
          // The contour degree is already set, but we ensure stability
          if (contour_degree != 0) {
            // High probability of resolving to root (strong cadence)
            std::uniform_real_distribution<float> res_dist(0.0f, 1.0f);
            if (res_dist(rng) < melody_params.phrase_end_resolution) {
              contour_degree = 0;  // Resolve to root
              scale_degree = current_chord_root;
            } else if (contour_degree != 4) {
              // If not root, at least prefer 5th for stability
              contour_degree = 4;
              scale_degree = current_chord_root + 4;
            }
          }
        } else if (is_last_note_in_motif && !is_phrase_ending) {
          // Call phrase (antecedent): end on unstable tone for tension
          // This creates a "question" that the response phrase answers
          force_chord_tone = true;
          std::uniform_real_distribution<float> call_dist(0.0f, 1.0f);
          float call_roll = call_dist(rng);

          // Prefer 3rd (creates tension) or 5th (semi-stable) for call phrase
          if (contour_degree == 0) {
            // Avoid root at call phrase end - creates premature resolution
            if (call_roll < 0.6f) {
              contour_degree = 2;  // 3rd - most tension
              scale_degree = current_chord_root + 2;
            } else {
              contour_degree = 4;  // 5th - semi-stable
              scale_degree = current_chord_root + 4;
            }
          }
        }

        // Phase 3: Raw allows non-chord tone landing - skip chord tone enforcement
        if (allow_non_chord_landing) {
          // Raw: randomly allow non-chord tones even on strong beats (50% chance)
          std::uniform_real_distribution<float> raw_dist(0.0f, 1.0f);
          if (raw_dist(rng) < 0.5f) {
            force_chord_tone = false;
          }
        }

        contour_idx++;

        // === SINGABLE MELODY GENERATION ===
        // Key principle: Limit intervals to create smooth, singable lines
        // Maximum interval: 5 semitones (perfect 4th) for most notes
        // Occasionally allow 7 semitones (perfect 5th) for expressiveness
        // When allow_extreme_leap is true (vocaloid mode), allow up to octave
        const int MAX_SINGABLE_INTERVAL = allow_extreme_leap ? 12 : 5;  // Octave or Perfect 4th
        const int MAX_EXPRESSIVE_INTERVAL = allow_extreme_leap ? 12 : 7;  // Octave or Perfect 5th

        // Convert to target pitch first
        int target_pitch = degreeToPitch(scale_degree, base_octave, key_offset);

        // Determine effective max interval based on context
        int max_interval = MAX_SINGABLE_INTERVAL;
        if (section.type == SectionType::Chorus && rn.strong) {
          // Allow slightly larger leaps in chorus on strong beats
          max_interval = MAX_EXPRESSIVE_INTERVAL;
        }

        int pitch;
        // IMPORTANT: Use GLOBAL vocal range for interval constraint, not section range
        // Section range is for register shift, but interval must be enforced globally
        // to prevent jarring jumps at section boundaries
        int constraint_low = static_cast<int>(effective_vocal_low);
        int constraint_high = static_cast<int>(effective_vocal_high);

        // === SECTION CADENCE (終止形) ===
        // Check for section final note and force chord tone resolution
        bool is_section_final_note_check = (motif_start + bars_in_motif >= section.bars) &&
                                           (rn_idx == rhythm.size() - 1);

        if (is_section_final_note_check) {
          // Section final note: FORCE resolution to chord tone (root, 3rd, or 5th)
          // Prefer root (degree 0) for strongest cadence
          pitch = nearestChordToneWithinInterval(
              target_pitch, prev_pitch, static_cast<int8_t>(current_chord_root),
              max_interval, constraint_low, constraint_high, tessitura_ptr);
        } else if (rn.strong || force_chord_tone) {
          // On strong beats: find chord tone within interval limit
          // Use tessitura preference for comfortable pitch selection
          pitch = nearestChordToneWithinInterval(
              target_pitch, prev_pitch, static_cast<int8_t>(current_chord_root),
              max_interval, constraint_low, constraint_high, tessitura_ptr);
        } else {
          // On weak beats: constrain interval, then snap to scale tone
          pitch = constrainInterval(target_pitch, prev_pitch, max_interval,
                                    constraint_low, constraint_high);
          // Ensure weak beat notes are scale tones (not arbitrary pitches)
          pitch = snapToNearestScaleTone(pitch, key_offset);
        }

        // After interval constraint, prefer section range but don't violate interval
        if (prev_pitch > 0) {
          if (pitch < section_vocal_low && section_vocal_low - prev_pitch <= max_interval) {
            pitch = section_vocal_low;
          } else if (pitch > section_vocal_high && prev_pitch - section_vocal_high <= max_interval) {
            pitch = section_vocal_high;
          }
        } else {
          // First note: clamp to section range
          pitch = std::clamp(pitch, section_vocal_low, section_vocal_high);
        }

        // Phase 2: Apply allow_unison_repeat constraint
        if (prev_pitch > 0 && !melody_params.allow_unison_repeat && pitch == prev_pitch) {
          // Avoid unison repetition - move by step in contour direction
          int direction = (contour_degree >= 0) ? 1 : -1;
          int stepped = prev_pitch + direction * 2;  // Move by a whole step
          if (stepped >= section_vocal_low && stepped <= section_vocal_high) {
            pitch = stepped;
          }
        }

        // Track interval for step-back rule
        if (prev_pitch > 0) {
          int interval = pitch - prev_pitch;

          // If previous was a large leap, encourage opposite direction by step
          if (std::abs(prev_interval) >= MAX_EXPRESSIVE_INTERVAL) {
            if (prev_interval > 0 && interval > 2) {
              // Was ascending leap, prefer small descent or stay
              pitch = std::max(section_vocal_low, prev_pitch - 2);
            } else if (prev_interval < 0 && interval < -2) {
              // Was descending leap, prefer small ascent or stay
              pitch = std::min(section_vocal_high, prev_pitch + 2);
            }
          }

          prev_interval = pitch - prev_pitch;
        }

        // Save the actual previous pitch for interval checking in the final step
        // prev_pitch will be updated to the STORED pitch after the note is added
        int actual_prev_pitch = prev_pitch;

        // === PHRASE-AWARE DURATION CALCULATION ===
        // Key principle: Last note of phrase should be LONGER for singability
        // Pattern: short notes (8ths) -> phrase-final note (held, 1-2 beats)
        //
        // Grid unit varies by VocalStylePreset:
        // - UltraVocaloid: 32nd note (60 ticks)
        // - Vocaloid: 16th note (120 ticks)
        // - Default: 8th note (240 ticks)
        Tick grid_unit = getGridUnit(params.vocal_style);
        Tick base_duration = static_cast<Tick>(rn.eighths * grid_unit);

        bool is_last_note_of_phrase = is_phrase_ending && (rn_idx == rhythm.size() - 1);
        bool is_last_note_of_motif = (rn_idx == rhythm.size() - 1);
        bool is_near_phrase_end = is_phrase_ending && (beat_in_motif >= 6.0f);

        // Determine gate and duration extension
        float gate;
        float duration_extend = 1.0f;  // Multiplier for extending duration

        if (is_last_note_of_phrase) {
          // Phrase-final note: EXTEND duration for held note, then breath
          // This is the "landing note" that gets sustained
          gate = melody_params.phrase_end_gate;
          duration_extend = 2.0f;  // Double the duration for held note
        } else if (is_last_note_of_motif) {
          // End of 2-bar motif (not phrase): hold longer for continuity
          gate = melody_params.legato_gate;
          duration_extend = 1.5f;  // Extend by 50%
        } else if (is_near_phrase_end) {
          // Approaching phrase end: prepare for hold
          gate = melody_params.legato_gate;
        } else {
          // Within phrase: legato (connected)
          gate = melody_params.legato_gate;
        }

        // === LONG NOTE RATIO & CHORUS LONG TONES ===
        // Apply probabilistic long note extension based on style parameters
        // This creates the "anthem" feel for Idol/Rock/Ballad styles
        float long_note_prob = melody_params.long_note_ratio;

        // Boost probability in chorus when chorus_long_tones is enabled
        if (section.type == SectionType::Chorus && melody_params.chorus_long_tones) {
          long_note_prob = std::min(1.0f, long_note_prob * 1.5f + 0.2f);
        }

        // Strong beats are more likely to be long notes (anchor points)
        if (rn.strong) {
          long_note_prob = std::min(1.0f, long_note_prob * 1.3f);
        }

        // Apply long note extension probabilistically
        std::uniform_real_distribution<float> long_dist(0.0f, 1.0f);
        if (long_dist(rng) < long_note_prob && !is_last_note_of_phrase) {
          // Extend to 1.5-2.5 beats for "anthem" feel
          duration_extend *= 1.8f;
        }

        // Calculate final duration
        Tick duration = static_cast<Tick>(base_duration * duration_extend * gate);
        // Ensure minimum duration based on style (use grid_unit as minimum)
        // UltraVocaloid: 60 ticks, Vocaloid: 120 ticks, Default: 120 ticks
        Tick min_duration = (params.vocal_style == VocalStylePreset::UltraVocaloid)
                                ? grid_unit
                                : std::max(grid_unit, static_cast<Tick>(TICKS_PER_BEAT / 4));
        duration = std::max(duration, min_duration);

        // === OVERLAP PREVENTION ===
        // Cap duration to avoid overlapping with the next note
        // Calculate the limit tick (next note start or motif end)
        Tick limit_tick = motif_end_tick;
        if (rn_idx + 1 < rhythm.size()) {
          // Look ahead to find the next rhythm note's timing
          const auto& next_rn = rhythm[rn_idx + 1];
          float next_beat_in_motif = next_rn.beat;
          int next_bar_offset = static_cast<int>(next_beat_in_motif / 4.0f);
          if (next_bar_offset < bars_in_motif) {
            float next_beat_in_bar = next_beat_in_motif - next_bar_offset * 4.0f;
            Tick next_note_tick = motif_start_tick + next_bar_offset * TICKS_PER_BAR +
                                  static_cast<Tick>(next_beat_in_bar * TICKS_PER_BEAT);
            limit_tick = std::min(limit_tick, next_note_tick);
          }
        }
        // Ensure at least 10 ticks gap for note separation
        Tick max_duration = (limit_tick > note_tick + 10) ? (limit_tick - note_tick - 10) : 10;
        duration = std::min(duration, max_duration);

        // Velocity - stronger on chord tones
        uint8_t beat_num = static_cast<uint8_t>(beat_in_bar);
        uint8_t velocity = calculateVelocity(section.type, beat_num, params.mood);

        // Apply velocity scaling for BackgroundMotif
        velocity = static_cast<uint8_t>(std::clamp(
            static_cast<int>(velocity * velocity_scale), 40, 127));

        // Determine next chord for anticipation (if applicable)
        int next_chord_root = (bar_offset == 0) ? chord_root2 : chord_root1;

        // Check for suspension or anticipation (not in BackgroundMotif mode)
        // Phase 2: Adjust based on VocalAttitude
        // Phase 3: Raw attitude further increases non-harmonic tones
        bool use_suspension = false;
        bool use_anticipation = false;
        if (!is_background_motif && rn.eighths >= 2) {
          // Calculate base probability then adjust by attitude
          float attitude_factor = 1.0f;
          if (vocal_attitude == VocalAttitude::Clean) {
            attitude_factor = 0.2f;  // Greatly reduce non-harmonic tones
          } else if (vocal_attitude == VocalAttitude::Expressive) {
            attitude_factor = 1.8f;  // Increase expressiveness
          }
          // Phase 3: Raw increases non-harmonic tones in allowed sections
          if (apply_raw) {
            attitude_factor = 2.5f;  // Maximum expressiveness for raw
          }

          // Check suspension with attitude-adjusted probability
          if (shouldUseSuspension(beat_in_motif, section.type, rng)) {
            std::uniform_real_distribution<float> check(0.0f, 1.0f);
            use_suspension = (check(rng) < attitude_factor);
          }
          // Check anticipation with attitude-adjusted probability
          if (!use_suspension && shouldUseAnticipation(beat_in_motif, section.type, rng)) {
            std::uniform_real_distribution<float> check(0.0f, 1.0f);
            use_anticipation = (check(rng) < attitude_factor);
          }
        }

        if (use_suspension) {
          // Select suspension type based on chord context (4-3, 9-8, or 7-6)
          SuspensionType sus_type = selectSuspensionType(current_chord_root, rng);
          SuspensionResult sus = applySuspension(current_chord_root, rn.eighths, sus_type);

          int sus_pitch = degreeToPitch(sus.suspension_degree, base_octave, key_offset);
          while (sus_pitch < effective_vocal_low) sus_pitch += 12;
          while (sus_pitch > effective_vocal_high) sus_pitch -= 12;
          sus_pitch = std::clamp(sus_pitch, (int)effective_vocal_low, (int)effective_vocal_high);

          int res_pitch = degreeToPitch(sus.resolution_degree, base_octave, key_offset);
          while (res_pitch < effective_vocal_low) res_pitch += 12;
          while (res_pitch > effective_vocal_high) res_pitch -= 12;
          res_pitch = std::clamp(res_pitch, (int)effective_vocal_low, (int)effective_vocal_high);

          Tick sus_duration = static_cast<Tick>(sus.suspension_eighths * TICKS_PER_BEAT / 2);
          Tick res_duration = static_cast<Tick>(sus.resolution_eighths * TICKS_PER_BEAT / 2);

          // Add suspension note - use safe pitch to avoid chord clashes
          uint8_t safe_sus_pitch = getSafePitch(sus_pitch, note_tick, sus_duration);
          track.addNote(note_tick, sus_duration, safe_sus_pitch, velocity);
          phrase_notes.push_back({relative_tick, sus_duration, safe_sus_pitch, velocity});

          // Add resolution note - use safe pitch to avoid chord clashes
          Tick res_tick = note_tick + sus_duration;
          Tick relative_res_tick = relative_tick + sus_duration;
          uint8_t res_vel = static_cast<uint8_t>(velocity * 0.9f);
          uint8_t safe_res_pitch = getSafePitch(res_pitch, res_tick, res_duration);
          track.addNote(res_tick, res_duration, safe_res_pitch, res_vel);
          phrase_notes.push_back({relative_res_tick, res_duration, safe_res_pitch, res_vel});

          prev_pitch = safe_res_pitch;
        } else if (use_anticipation && beat_in_motif >= 0.5f) {
          // Apply anticipation: early arrival of next chord tone
          AnticipationResult ant = applyAnticipation(next_chord_root, rn.eighths);

          int ant_pitch = degreeToPitch(ant.degree, base_octave, key_offset);
          while (ant_pitch < effective_vocal_low) ant_pitch += 12;
          while (ant_pitch > effective_vocal_high) ant_pitch -= 12;
          ant_pitch = std::clamp(ant_pitch, (int)effective_vocal_low, (int)effective_vocal_high);

          // Shift note earlier by the offset
          Tick ant_offset_ticks = static_cast<Tick>(std::abs(ant.beat_offset) * TICKS_PER_BEAT);
          Tick ant_tick = (note_tick > ant_offset_ticks) ? (note_tick - ant_offset_ticks) : note_tick;
          Tick relative_ant_tick = (relative_tick > ant_offset_ticks) ?
                                   (relative_tick - ant_offset_ticks) : relative_tick;

          Tick ant_duration = static_cast<Tick>(ant.duration_eighths * TICKS_PER_BEAT / 2);

          // Add anticipation note
          uint8_t safe_ant_pitch = getSafePitch(ant_pitch, ant_tick, ant_duration);
          track.addNote(ant_tick, ant_duration, safe_ant_pitch, velocity);
          phrase_notes.push_back({relative_ant_tick, ant_duration, safe_ant_pitch, velocity});

          prev_pitch = safe_ant_pitch;
        } else {
          // Regular note - use safe pitch to avoid chord clashes
          uint8_t safe_pitch = getSafePitch(pitch, note_tick, duration);

          // IMPORTANT: If getSafePitch violates interval constraint, prefer melodic continuity
          // Use actual_prev_pitch (the PREVIOUS note's stored pitch) for accurate interval check
          const int MAX_REGULAR_INTERVAL = allow_extreme_leap ? 12 : 7;  // Octave or Perfect 5th
          if (actual_prev_pitch > 0 && std::abs(static_cast<int>(safe_pitch) - actual_prev_pitch) > MAX_REGULAR_INTERVAL) {
            // Revert to the interval-constrained pitch
            safe_pitch = static_cast<uint8_t>(pitch);
          }

          track.addNote(note_tick, duration, safe_pitch, velocity);
          phrase_notes.push_back(
              {relative_tick, duration, safe_pitch, velocity});

          // Update prev_pitch to the STORED pitch for next iteration
          prev_pitch = safe_pitch;

          // Store notes for chorus hook (first 2-bar phrase only)
          if (is_chorus && motif_start == 0) {
            // Use relative tick within the motif (not section)
            Tick motif_relative_tick = relative_tick - relative_motif_start;
            chorus_hook_notes.push_back(
                {motif_relative_tick, duration, safe_pitch, velocity});
          }
        }
      }
    }

    phrase_cache[section.type] = std::move(phrase_notes);
  }

  // === POST-PROCESSING PIPELINE ===
  postProcessVocalTrack(track, params);
}

}  // namespace midisketch
