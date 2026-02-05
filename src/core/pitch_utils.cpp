/**
 * @file pitch_utils.cpp
 * @brief Implementation of pitch and scale utilities.
 */

#include "core/pitch_utils.h"

#include <algorithm>
#include <cmath>

namespace midisketch {

TessituraRange calculateTessitura(uint8_t vocal_low, uint8_t vocal_high) {
  int range = vocal_high - vocal_low;

  // Tessitura is the middle portion of the range
  // Leave ~15-20% headroom at top and bottom for climactic moments
  int margin = range / 5;        // 20% margin
  margin = std::max(margin, 3);  // At least 3 semitones margin

  TessituraRange t;
  t.low = static_cast<uint8_t>(vocal_low + margin);
  t.high = static_cast<uint8_t>(vocal_high - margin);
  t.center = static_cast<uint8_t>((t.low + t.high) / 2);
  t.vocal_low = vocal_low;
  t.vocal_high = vocal_high;

  // Ensure valid range
  if (t.low >= t.high) {
    t.low = vocal_low;
    t.high = vocal_high;
    t.center = (vocal_low + vocal_high) / 2;
  }

  return t;
}

bool isInTessitura(uint8_t pitch, const TessituraRange& tessitura) {
  return pitch >= tessitura.low && pitch <= tessitura.high;
}

float getComfortScore(uint8_t pitch, const TessituraRange& tessitura, uint8_t vocal_low,
                      uint8_t vocal_high) {
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

  // Reduced score for passaggio (dynamically calculated based on voice range)
  // Use distance-based gradient: boundary notes (0.45) are better for climactic moments,
  // while center notes (0.35) are more challenging and evaluated more strictly.
  if (isInPassaggioRange(pitch, vocal_low, vocal_high)) {
    int range = vocal_high - vocal_low;
    int passaggio_low = vocal_low + range * 55 / 100;
    int passaggio_high = vocal_low + range * 75 / 100;
    int passaggio_center = (passaggio_low + passaggio_high) / 2;
    int dist_from_center = std::abs(static_cast<int>(pitch) - passaggio_center);
    int passaggio_half_width = (passaggio_high - passaggio_low) / 2;
    if (passaggio_half_width == 0) passaggio_half_width = 1;
    float gradient = static_cast<float>(dist_from_center) / passaggio_half_width;
    return 0.35f + 0.10f * gradient;  // 0.35 (center) to 0.45 (boundary)
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

PassaggioRange calculateDynamicPassaggio(uint8_t vocal_low, uint8_t vocal_high) {
  int range = vocal_high - vocal_low;

  if (range <= 12) {
    // Very narrow range (octave or less): use fixed passaggio
    return {PASSAGGIO_LOW, PASSAGGIO_HIGH};
  }

  // Passaggio at 55%-75% of range (upper-middle portion)
  // This matches the expected behavior from music theory
  int lower = vocal_low + range * 55 / 100;
  int upper = vocal_low + range * 75 / 100;

  // Ensure bounds are within MIDI range
  lower = std::clamp(lower, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
  upper = std::clamp(upper, static_cast<int>(vocal_low), static_cast<int>(vocal_high));

  return {static_cast<uint8_t>(lower), static_cast<uint8_t>(upper)};
}

bool isInPassaggio(uint8_t pitch) { return pitch >= PASSAGGIO_LOW && pitch <= PASSAGGIO_HIGH; }

// Dynamic passaggio calculation based on vocal range.
// Passaggio is typically in the upper-middle portion of the range.
bool isInPassaggioRange(uint8_t pitch, uint8_t vocal_low, uint8_t vocal_high) {
  PassaggioRange passaggio = calculateDynamicPassaggio(vocal_low, vocal_high);
  return passaggio.contains(pitch);
}

int constrainInterval(int target_pitch, int prev_pitch, int max_interval, int range_low,
                      int range_high) {
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

// ============================================================================
// Avoid Note vs Dissonance: Conceptual Distinction
// ============================================================================
//
// AVOID NOTE (isAvoidNote*):
//   Definition: A melodic note that creates undesirable tension when SUSTAINED
//               against a chord, regardless of what other notes are playing.
//   Time basis: Duration-based (held notes)
//   Context:    Chord function matters (tritone OK on V7, avoid on I)
//   Usage:      Melody generation - prevent bad long notes
//   Examples:   M7 on any chord, P4 on I major, m6 on minor
//
// DISSONANCE (isDissonant*):
//   Definition: Acoustic harshness from two notes sounding SIMULTANEOUSLY,
//               based on interval roughness/beating perception.
//   Time basis: Simultaneity-based (vertical intervals)
//   Context:    Some functions allow dissonance (tritone in V7)
//   Usage:      Track collision detection - polyphonic safety
//   Examples:   m2 always, M2 in close range, M7 at any octave
//
// ============================================================================

// Conservative dissonance check WITHOUT chord context.
// Treats tritone as always dissonant. Use this when:
// - Chord degree is unknown or unavailable
// - You want conservative avoidance (e.g., bass approach notes)
// For context-aware checking, use isDissonantIntervalWithContext() instead.
bool isDissonantInterval(int pc1, int pc2) {
  int interval = std::abs(pc1 - pc2);
  if (interval > 6) interval = 12 - interval;

  // Minor 2nd (1) = major 7th inverted - always dissonant
  // Tritone (6) = context-dependent but treated as dissonant here for safety
  // Note: Tritone IS acceptable on V7 (dominant 7th) and vii° (diminished) chords
  // where it forms a structural interval. Use isDissonantIntervalWithContext()
  // when chord context is available.
  return interval == 1 || interval == 6;
}

bool isDissonantIntervalWithContext(int pc1, int pc2, int8_t chord_degree, bool simultaneous) {
  int interval = std::abs(pc1 - pc2);
  if (interval > 6) interval = 12 - interval;

  // Minor 2nd (1) is always dissonant - creates harsh beating
  if (interval == 1) {
    return true;
  }

  // Major 2nd (2) is dissonant only for simultaneous (vertical) intervals.
  // In melodic (horizontal) context, it's a natural scale step and acceptable.
  // When tracks play at the same time, M2 creates audible beating.
  if (interval == 2 && simultaneous) {
    return true;
  }

  // Tritone (6) is acceptable on dominant (V) and diminished (vii°) chords
  // V: tritone between 3rd and 7th of dominant 7th chord
  // vii°: tritone between root and diminished 5th
  if (interval == 6) {
    int normalized = ((chord_degree % 7) + 7) % 7;
    if (normalized == 4 || normalized == 6) {
      return false;  // V or vii chord - tritone is part of the chord
    }
    return true;  // Other chords - tritone is dissonant
  }

  return false;
}

bool isDissonantActualInterval(int actual_semitones, int8_t chord_degree) {
  // For very wide intervals (3+ octaves), perceptual harshness is reduced.
  // Beyond 3 octaves, beating frequencies become too slow to perceive as dissonance,
  // and the notes occupy different registral spaces (bass vs. soprano).
  // This threshold allows bass-vocal combinations without false positives.
  if (actual_semitones >= 36) {
    return false;
  }

  // Pitch class interval for octave-equivalent checks
  int pc_interval = actual_semitones % 12;

  // Minor 2nd (1 semitone) and minor 9th (13 semitones): harsh beating in close range
  // Compound minor 2nds beyond 13 semitones (e.g. 25) are not perceptually dissonant
  if (pc_interval == 1 && actual_semitones <= 13) {
    return true;
  }

  // Major 2nd: only dissonant in close range (exact 2 semitones)
  // Major 9th (14) is a common chord extension in pop - NOT dissonant
  if (actual_semitones == 2) {
    return true;
  }

  // Major 7th (pitch class 11): dissonant within 2 octaves
  // Bass-upper voice M7 at 23 semitones is still perceptually harsh.
  // Only very wide compound M7 (35+ semitones, ~3 octaves) is allowed.
  if (pc_interval == 11 && actual_semitones <= 23) {
    return true;
  }

  // Tritone (pitch class 6): context-dependent at any octave
  // Allowed on V (dominant) and vii° (diminished) chords
  // Catches: 6, 18, 30 semitones
  if (pc_interval == 6) {
    int normalized = ((chord_degree % 7) + 7) % 7;
    if (normalized != 4 && normalized != 6) {
      return true;  // Not V or vii - tritone is dissonant
    }
  }

  // Minor 7th (10), major 9th (14), perfect 12th (19), etc.: acceptable in Pop
  return false;
}

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
  // Use floor division for correct octave calculation with negative values
  int relative = pitch - key_offset;
  int octave = relative >= 0 ? relative / 12 : (relative - 11) / 12;
  return octave * 12 + best_pc + key_offset;
}

bool isAvoidNoteWithContext(int pitch, uint8_t chord_root, bool is_minor, int8_t chord_degree) {
  int interval = ((pitch - chord_root) % 12 + 12) % 12;
  ChordFunction function = getChordFunction(chord_degree);

  // Major 7th (11): generally dissonant as it clashes with root
  // Exception: Maj7 chords exist, but for melody avoid notes this is still harsh
  if (interval == AVOID_MAJOR_7TH) {
    return true;
  }

  // Minor 2nd (1): harsh dissonance on non-dominant chords
  // - Dominant (V7): b9 is a valid tension (V7b9)
  // - Tonic/Subdominant: creates harsh clash with root (e.g., F# on F chord)
  if (interval == AVOID_MINOR_2ND) {
    return function != ChordFunction::Dominant;
  }

  // Tritone (6): depends on chord function
  // - Dominant: tritone is ESSENTIAL (3rd-7th of V7, root-5th of vii°)
  // - Tonic/Subdominant: tritone creates unwanted tension
  if (interval == AVOID_TRITONE) {
    return function != ChordFunction::Dominant;
  }

  // Perfect 4th (5) on major chords:
  // - Tonic (I): clashes with major 3rd (sus4 aside)
  // - Subdominant (IV): the 4th IS the root, so it's a chord tone, not avoid
  // - V chord: 4th = root of I, tension but resolves
  if (!is_minor && interval == AVOID_PERFECT_4TH) {
    // On I chord (tonic), P4 clashes with major 3rd
    // On IV chord, the "4th" from IV's root is actually the tonic - it's fine
    // On V chord, the "4th" creates suspension, borderline
    return function == ChordFunction::Tonic;
  }

  // Minor 6th (8) on minor chords:
  // - Creates tension against the 5th (only 1 semitone away)
  // - m6 chords exist, but for melody avoid notes this is harsh on minor quality
  if (is_minor && interval == AVOID_MINOR_6TH) {
    return true;
  }

  return false;
}

bool isAvoidNoteSimple(int pitch, uint8_t chord_root, bool is_minor) {
  int interval = ((pitch - chord_root) % 12 + 12) % 12;

  // Conservative: minor 2nd, tritone and major 7th are always avoided
  if (interval == AVOID_MINOR_2ND || interval == AVOID_TRITONE || interval == AVOID_MAJOR_7TH) {
    return true;
  }

  // Quality-dependent avoid notes
  if (is_minor) {
    return interval == AVOID_MINOR_6TH;
  }
  return interval == AVOID_PERFECT_4TH;
}

}  // namespace midisketch
