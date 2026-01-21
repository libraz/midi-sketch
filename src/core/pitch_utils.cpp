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
                      uint8_t /* vocal_high */) {
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

bool isInPassaggio(uint8_t pitch) { return pitch >= PASSAGGIO_LOW && pitch <= PASSAGGIO_HIGH; }

// Dynamic passaggio calculation based on vocal range.
// Passaggio is typically in the upper-middle portion of the range.
bool isInPassaggioRange(uint8_t pitch, uint8_t vocal_low, uint8_t vocal_high) {
  int range = vocal_high - vocal_low;
  if (range <= 12) {
    // Very narrow range: use fixed passaggio
    return isInPassaggio(pitch);
  }
  // Passaggio at 55%-75% of range (upper-middle third)
  int passaggio_low = vocal_low + range * 55 / 100;
  int passaggio_high = vocal_low + range * 75 / 100;
  return pitch >= passaggio_low && pitch <= passaggio_high;
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

bool isDissonantIntervalWithContext(int pc1, int pc2, int8_t chord_degree) {
  int interval = std::abs(pc1 - pc2);
  if (interval > 6) interval = 12 - interval;

  // Minor 2nd (1) is always dissonant - creates harsh beating
  if (interval == 1) {
    return true;
  }

  // Major 2nd (2) is dissonant when tracks overlap
  // While acceptable as passing tone or tension within a chord,
  // it sounds harsh when Chord and Vocal play simultaneously
  if (interval == 2) {
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

  // Minor 2nd (pitch class 1): harsh beating at any octave
  // Catches: 1, 13, 25 semitones (within 36 limit)
  if (pc_interval == 1) {
    return true;
  }

  // Major 2nd: only dissonant in close range (exact 2 semitones)
  // Major 9th (14) is a common chord extension in pop - NOT dissonant
  if (actual_semitones == 2) {
    return true;
  }

  // Major 7th (pitch class 11): creates tension with root at any octave
  // Catches: 11, 23, 35 semitones
  if (pc_interval == 11) {
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

  // Conservative: tritone and major 7th are always avoided
  if (interval == AVOID_TRITONE || interval == AVOID_MAJOR_7TH) {
    return true;
  }

  // Quality-dependent avoid notes
  if (is_minor) {
    return interval == AVOID_MINOR_6TH;
  }
  return interval == AVOID_PERFECT_4TH;
}

}  // namespace midisketch
