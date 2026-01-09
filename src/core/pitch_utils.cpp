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

bool isInTessitura(uint8_t pitch, const TessituraRange& tessitura) {
  return pitch >= tessitura.low && pitch <= tessitura.high;
}

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

bool isInPassaggio(uint8_t pitch) {
  return pitch >= PASSAGGIO_LOW && pitch <= PASSAGGIO_HIGH;
}

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

bool isDissonantInterval(int pc1, int pc2) {
  int interval = std::abs(pc1 - pc2);
  if (interval > 6) interval = 12 - interval;

  // Minor 2nd (1) = major 7th inverted - always dissonant
  // Tritone (6) = highly dissonant, avoid in vocal against chord
  return interval == 1 || interval == 6;
}

bool isDissonantIntervalWithContext(int pc1, int pc2, int8_t chord_degree) {
  int interval = std::abs(pc1 - pc2);
  if (interval > 6) interval = 12 - interval;

  // Minor 2nd (1) is always dissonant
  if (interval == 1) {
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

}  // namespace midisketch
