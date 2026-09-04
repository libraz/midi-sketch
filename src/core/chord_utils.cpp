/**
 * @file chord_utils.cpp
 * @brief Implementation of chord voicing utilities.
 */

#include "core/chord_utils.h"

#include <algorithm>
#include <cmath>

#include "core/chord.h"
#include "core/i_harmony_context.h"

namespace midisketch {

ChordTones getChordTones(int8_t degree) {
  ChordTones ct{};
  ct.count = 0;

  // Get root pitch class from degree (handles borrowed chords via degreeToSemitone)
  int root_pc = ((degreeToSemitone(degree) % 12) + 12) % 12;

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
  const ChordTones tones = getChordTones(degree);
  return {tones.begin(), tones.end()};
}

std::vector<int> getGuideTonePitchClasses(int8_t degree) {
  std::vector<int> guides;

  // Get chord intervals from central chord definition
  Chord chord = getChordNotes(degree);
  int root_pc = ((degreeToSemitone(degree) % 12) + 12) % 12;

  // 3rd is interval index 1 (after root)
  if (chord.note_count > 1) {
    guides.push_back((root_pc + chord.intervals[1]) % 12);
  }

  // 7th: if chord has 4+ notes, index 3 is the 7th
  if (chord.note_count > 3) {
    guides.push_back((root_pc + chord.intervals[3]) % 12);
  } else {
    // For triads, infer the diatonic 7th above the root.
    //
    // Diatonic degrees (intervals measured within the C major scale):
    //   I  (0): major 7th (11)  -> CMaj7
    //   ii (1): minor 7th (10)  -> Dm7
    //   iii(2): minor 7th (10)  -> Em7
    //   IV (3): major 7th (11)  -> FMaj7
    //   V  (4): minor 7th (10)  -> G7 (dominant)
    //   vi (5): minor 7th (10)  -> Am7
    //   vii(6): minor 7th (10)  -> Bm7b5 (B + 10 semitones = A, diatonic).
    //           NOT 9 (diminished 7th = Ab, non-diatonic Bdim7).
    //
    // Borrowed chords (degree > 6): there is no single in-key 7th, so pick by
    // quality. Borrowed major chords (bVI, bVII, bIII, bII) take a minor 7th
    // (Mixolydian/dominant color, the most idiomatic for modal interchange),
    // borrowed minor iv takes a minor 7th (Fm7), and diminished #IVdim takes a
    // minor 7th (yielding a half-diminished color, parallel to vii°).
    int seventh_interval;
    if (degree >= 0 && degree <= 6) {
      switch (degree) {
        case 0:  // I  - major 7th
        case 3:  // IV - major 7th
          seventh_interval = 11;
          break;
        default:  // ii, iii, V, vi, vii - minor 7th (vii: B + 10 = A, half-dim)
          seventh_interval = 10;
          break;
      }
    } else {
      // Borrowed chords: minor 7th color regardless of triad quality.
      seventh_interval = 10;
    }
    guides.push_back((root_pc + seventh_interval) % 12);
  }

  return guides;
}

std::vector<int> getScalePitchClasses(uint8_t key) {
  std::vector<int> result;
  result.reserve(7);

  for (int interval : SCALE) {
    result.push_back((key + interval) % 12);
  }

  return result;
}

std::vector<int> getAvailableTensionPitchClasses(int8_t degree) {
  std::vector<int> result;

  // Root pitch class via degreeToSemitone() so borrowed degrees resolve to the
  // correct chromatic root (not collapsed into 0-6 by %7).
  int root_pc = ((degreeToSemitone(degree) % 12) + 12) % 12;

  // For borrowed chords there is no single canonical diatonic-tension set. Use a
  // conservative selection based on chord quality: major borrowed chords get the
  // safe 9th, minor borrowed chords (iv) get 9th + 11th, diminished (#IVdim) gets
  // the 11th only. This mirrors the diatonic logic below without guessing at
  // modal-interchange-specific colors.
  if (degree < 0 || degree > 6) {
    ChordQuality quality = getChordQuality(degree);
    if (quality == ChordQuality::Diminished) {
      result.push_back((root_pc + 5) % 12);  // 11th
    } else if (quality == ChordQuality::Minor) {
      result.push_back((root_pc + 2) % 12);  // 9th
      result.push_back((root_pc + 5) % 12);  // 11th
    } else {
      result.push_back((root_pc + 2) % 12);  // 9th
    }
    return result;
  }

  // Normalize degree to 0-6 range (only reached for diatonic degrees).
  int normalized = ((degree % 7) + 7) % 7;

  // Available tensions by degree (in semitones from root):
  // I (0): 9th (+2), 13th (+9) - avoid 11th (#4 clashes with 3rd)
  // ii (1): 9th (+2), 11th (+5), 13th (+9)
  // iii (2): 11th (+5), b13th (+8) - avoid 9th (b9)
  // IV (3): 9th (+2), #11th (+6), 13th (+9)
  // V (4): 9th (+2), 13th (+9) - 11th only if sus4
  // vi (5): 9th (+2), 11th (+5) - avoid 13th (b13)
  // vii° (6): 11th (+5) - limited use

  switch (normalized) {
    case 0:                                  // I major
      result.push_back((root_pc + 2) % 12);  // 9th
      result.push_back((root_pc + 9) % 12);  // 13th
      break;
    case 1:                                  // ii minor
      result.push_back((root_pc + 2) % 12);  // 9th
      result.push_back((root_pc + 5) % 12);  // 11th
      result.push_back((root_pc + 9) % 12);  // 13th
      break;
    case 2:                                  // iii minor
      result.push_back((root_pc + 5) % 12);  // 11th
      result.push_back((root_pc + 8) %
                       12);  // b13th (natural 13th from scale = minor 6th from root)
      break;
    case 3:                                  // IV major
      result.push_back((root_pc + 2) % 12);  // 9th
      result.push_back((root_pc + 6) % 12);  // #11th
      result.push_back((root_pc + 9) % 12);  // 13th
      break;
    case 4:                                  // V dominant
      result.push_back((root_pc + 2) % 12);  // 9th
      result.push_back((root_pc + 9) % 12);  // 13th
      break;
    case 5:                                  // vi minor
      result.push_back((root_pc + 2) % 12);  // 9th
      result.push_back((root_pc + 5) % 12);  // 11th
      break;
    case 6:                                  // vii diminished
      result.push_back((root_pc + 5) % 12);  // 11th
      break;
  }

  return result;
}

// ============================================================================
// ChordToneHelper Implementation
// ============================================================================

ChordToneHelper::ChordToneHelper(int8_t degree)
    : degree_(degree),
      root_pc_(((degreeToSemitone(degree) % 12) + 12) % 12),
      pitch_classes_(getChordTones(degree)) {}

bool ChordToneHelper::isChordTone(uint8_t pitch) const {
  int pitch_class = pitch % 12;
  return isChordTonePitchClass(pitch_class);
}

bool ChordToneHelper::isChordTonePitchClass(int pitch_class) const {
  int normalized = ((pitch_class % 12) + 12) % 12;
  for (int ct : pitch_classes_) {
    if (normalized == ct) {
      return true;
    }
  }
  return false;
}

uint8_t ChordToneHelper::nearestChordTone(uint8_t pitch) const {
  return static_cast<uint8_t>(nearestChordTonePitch(static_cast<int>(pitch), degree_));
}

uint8_t ChordToneHelper::nearestInRange(uint8_t pitch, uint8_t low, uint8_t high) const {
  return static_cast<uint8_t>(
      findNearestChordToneInRange(static_cast<int>(pitch), degree_, low, high));
}

std::vector<uint8_t> ChordToneHelper::allInRange(uint8_t low, uint8_t high) const {
  std::vector<uint8_t> result;

  for (int oct = low / 12; oct <= high / 12 + 1; ++oct) {
    for (int ct_pc : pitch_classes_) {
      int pitch = oct * 12 + ct_pc;
      if (pitch >= low && pitch <= high && pitch >= 0 && pitch <= 127) {
        result.push_back(static_cast<uint8_t>(pitch));
      }
    }
  }

  // Sort by pitch
  std::sort(result.begin(), result.end());
  return result;
}

int ChordToneHelper::rootPitchClass() const { return root_pc_; }

// ============================================================================
// Nearest Chord Tone Functions
// ============================================================================

int nearestChordTonePitch(int pitch, int8_t degree) {
  // No range restriction: use full MIDI range
  return findNearestChordToneInRange(pitch, degree, 0, 127);
}

int findNearestChordToneInRange(int pitch, int8_t degree, int range_low, int range_high) {
  ChordTones ct = getChordTones(degree);
  int octave = pitch / 12;

  int best_pitch = pitch;
  int best_dist = 1000;

  for (uint8_t idx = 0; idx < ct.count; ++idx) {
    int ct_pc = ct.pitch_classes[idx];
    if (ct_pc < 0) continue;

    // Check nearby octaves (-2 to +2)
    for (int oct_offset = -2; oct_offset <= 2; ++oct_offset) {
      int candidate = (octave + oct_offset) * 12 + ct_pc;
      if (candidate < range_low || candidate > range_high) continue;
      if (candidate < 0 || candidate > 127) continue;

      int dist = std::abs(candidate - pitch);
      if (dist < best_dist) {
        best_dist = dist;
        best_pitch = candidate;
      }
    }
  }

  return best_pitch;
}

int nearestChordToneWithinInterval(int target_pitch, int prev_pitch, int8_t chord_degree,
                                   int max_interval, int range_low, int range_high,
                                   const TessituraRange* tessitura) {
  ChordTones ct = getChordTones(chord_degree);

  // If no previous pitch, just find nearest chord tone to target
  if (prev_pitch < 0) {
    int result = nearestChordTonePitch(target_pitch, chord_degree);
    return std::clamp(result, range_low, range_high);
  }

  // Default: stay on previous pitch, but clamped to range
  int best_pitch = std::clamp(prev_pitch, range_low, range_high);
  int best_score = -1000;  // Higher is better

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

      // Calculate score: balance target proximity with stepwise motion
      int dist_to_target = std::abs(candidate - target_pitch);
      int dist_to_prev = std::abs(candidate - prev_pitch);

      // Base score: closer to target is better
      int score = 100 - dist_to_target;

      // SINGABILITY: prefer small intervals while still reaching target
      // Balance: don't over-penalize movement, but discourage large leaps
      if (dist_to_prev == 0) {
        score += 20;  // Same note: stable (reduced to allow target progression)
      } else if (dist_to_prev <= 2) {
        score += 25;  // Step motion (1-2 semitones): most singable
      } else if (dist_to_prev <= 4) {
        score += 5;  // Small leap (3-4 semitones): acceptable
      } else {
        score -= (dist_to_prev - 4) * 8;  // Large leaps: stronger penalty
      }

      // Tessitura bonus: prefer comfortable range
      if (tessitura != nullptr) {
        if (candidate >= tessitura->low && candidate <= tessitura->high) {
          score += 15;  // Bonus for being in tessitura
        }
        // Use dynamic passaggio calculation based on vocal range
        if (isInPassaggioRange(static_cast<uint8_t>(candidate), tessitura->vocal_low,
                               tessitura->vocal_high)) {
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

// ============================================================================
// Tritone Detection
// ============================================================================

bool hasTritoneWithChord(int pitch_pc, const std::vector<int>& chord_pcs) {
  for (int chord_pc : chord_pcs) {
    int interval = std::abs(pitch_pc - chord_pc);
    if (interval > 6) interval = 12 - interval;
    if (interval == 6) return true;
  }
  return false;
}

bool hasTritoneWithChord(int pitch_pc, const ChordTones& chord_pcs) {
  for (int chord_pc : chord_pcs) {
    int interval = std::abs(pitch_pc - chord_pc);
    if (interval > 6) interval = 12 - interval;
    if (interval == 6) return true;
  }
  return false;
}

// ============================================================================
// Voices of one track sounding together
// ============================================================================

bool isDissonantVoicingGap(int semitones) {
  int gap = std::abs(semitones);
  return gap == 1 || gap == 2 || gap == 13;
}

bool isVoicingCluster(uint8_t pitch_a, uint8_t pitch_b, const ChordTones& tones) {
  const int gap = static_cast<int>(pitch_a) - static_cast<int>(pitch_b);
  if (!isDissonantVoicingGap(gap)) return false;
  if (std::abs(gap) != 2) return true;

  bool a_is_chord_tone = false;
  bool b_is_chord_tone = false;
  for (int pc : tones) {
    if (pc < 0) continue;
    if (pitch_a % 12 == pc % 12) a_is_chord_tone = true;
    if (pitch_b % 12 == pc % 12) b_is_chord_tone = true;
  }
  return !(a_is_chord_tone && b_is_chord_tone);
}

// ============================================================================
// Diatonic Fifth Utilities
// ============================================================================

using namespace Interval;

uint8_t getDiatonicFifth(uint8_t root) {
  int pitch_class = root % OCTAVE;
  // B (pitch class 11) has a diminished 5th in C major (B->F)
  // All other diatonic roots have perfect 5th
  int interval = (pitch_class == 11) ? TRITONE : PERFECT_5TH;
  int fifth = root + interval;
  // Shift octave down if above bass range (preserves pitch class)
  if (fifth > BASS_HIGH) {
    fifth -= OCTAVE;
  }
  return clampBass(fifth);
}

uint8_t getSafeChordTone(uint8_t root, const IHarmonyContext& harmony, Tick start, Tick duration,
                         TrackRole role, uint8_t range_low, uint8_t range_high) {
  int8_t degree = harmony.getChordDegreeAt(start);
  const ChordTones chord_pcs = getChordTones(degree);

  // Helper: check if pitch class is a chord tone
  auto isChordTone = [&](int pc) {
    for (int ct : chord_pcs) {
      if (ct == pc) return true;
    }
    return false;
  };

  // Helper: find pitch in range near root and check consonance
  auto tryPitch = [&](int pitch_class) -> int {
    int root_oct = root / OCTAVE;
    // Try same octave as root, then octave above, then below
    for (int oct_offset : {0, 1, -1}) {
      int candidate = (root_oct + oct_offset) * OCTAVE + pitch_class;
      if (candidate >= range_low && candidate <= range_high && candidate != root &&
          harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(candidate), start, duration,
                                             role)) {
        return candidate;
      }
    }
    return -1;
  };

  // First try: diatonic 5th of root (most common case)
  uint8_t fifth = getDiatonicFifth(root);
  if (isChordTone(fifth % OCTAVE)) {
    int result = tryPitch(fifth % OCTAVE);
    if (result >= 0) return static_cast<uint8_t>(result);
  }

  // Second try: chord's actual 5th (for slash chord cases)
  if (chord_pcs.size() >= 3) {
    int result = tryPitch(chord_pcs[2]);
    if (result >= 0) return static_cast<uint8_t>(result);
  }

  // Third try: chord's 3rd
  if (chord_pcs.size() >= 2) {
    int result = tryPitch(chord_pcs[1]);
    if (result >= 0) return static_cast<uint8_t>(result);
  }

  // Fall back to root (always safest)
  return root;
}

}  // namespace midisketch
