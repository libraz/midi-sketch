#include "core/chord_utils.h"
#include "core/chord.h"
#include <algorithm>
#include <cmath>

namespace midisketch {

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

std::vector<int> getChordTonePitchClasses(int8_t degree) {
  std::vector<int> result;

  // Normalize degree to 0-6 range
  int normalized = ((degree % 7) + 7) % 7;
  int root_pc = DEGREE_TO_PITCH_CLASS[normalized];

  // Get chord from chord.cpp for accurate intervals
  Chord chord = getChordNotes(degree);

  for (uint8_t i = 0; i < chord.note_count && i < 5; ++i) {
    if (chord.intervals[i] >= 0) {
      result.push_back((root_pc + chord.intervals[i]) % 12);
    }
  }

  return result;
}

// Major scale intervals from root: W-W-H-W-W-W-H (0, 2, 4, 5, 7, 9, 11)
static constexpr int MAJOR_SCALE_INTERVALS[7] = {0, 2, 4, 5, 7, 9, 11};

bool isScaleTone(int pitch_class, uint8_t key) {
  // Normalize pitch class
  int normalized_pc = ((pitch_class % 12) + 12) % 12;

  // Check each scale degree
  for (int interval : MAJOR_SCALE_INTERVALS) {
    int scale_pc = (key + interval) % 12;
    if (normalized_pc == scale_pc) {
      return true;
    }
  }
  return false;
}

std::vector<int> getScalePitchClasses(uint8_t key) {
  std::vector<int> result;
  result.reserve(7);

  for (int interval : MAJOR_SCALE_INTERVALS) {
    result.push_back((key + interval) % 12);
  }

  return result;
}

std::vector<int> getAvailableTensionPitchClasses(int8_t degree) {
  std::vector<int> result;

  // Normalize degree to 0-6 range
  int normalized = ((degree % 7) + 7) % 7;
  int root_pc = DEGREE_TO_PITCH_CLASS[normalized];

  // Available tensions by degree (in semitones from root):
  // I (0): 9th (+2), 13th (+9) - avoid 11th (#4 clashes with 3rd)
  // ii (1): 9th (+2), 11th (+5), 13th (+9)
  // iii (2): 11th (+5) - avoid 9th (b9), avoid 13th (b13)
  // IV (3): 9th (+2), #11th (+6), 13th (+9)
  // V (4): 9th (+2), 13th (+9) - 11th only if sus4
  // vi (5): 9th (+2), 11th (+5) - avoid 13th (b13)
  // vii° (6): 11th (+5) - limited use

  switch (normalized) {
    case 0:  // I major
      result.push_back((root_pc + 2) % 12);   // 9th
      result.push_back((root_pc + 9) % 12);   // 13th
      break;
    case 1:  // ii minor
      result.push_back((root_pc + 2) % 12);   // 9th
      result.push_back((root_pc + 5) % 12);   // 11th
      result.push_back((root_pc + 9) % 12);   // 13th
      break;
    case 2:  // iii minor
      result.push_back((root_pc + 5) % 12);   // 11th
      break;
    case 3:  // IV major
      result.push_back((root_pc + 2) % 12);   // 9th
      result.push_back((root_pc + 6) % 12);   // #11th
      result.push_back((root_pc + 9) % 12);   // 13th
      break;
    case 4:  // V dominant
      result.push_back((root_pc + 2) % 12);   // 9th
      result.push_back((root_pc + 9) % 12);   // 13th
      break;
    case 5:  // vi minor
      result.push_back((root_pc + 2) % 12);   // 9th
      result.push_back((root_pc + 5) % 12);   // 11th
      break;
    case 6:  // vii diminished
      result.push_back((root_pc + 5) % 12);   // 11th
      break;
  }

  return result;
}

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

int nearestChordToneWithinInterval(int target_pitch, int prev_pitch,
                                   int8_t chord_degree, int max_interval,
                                   int range_low, int range_high,
                                   const TessituraRange* tessitura) {
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

}  // namespace midisketch
