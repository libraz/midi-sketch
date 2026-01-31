/**
 * @file voicing_generator.cpp
 * @brief Implementation of chord voicing generation.
 */

#include "track/chord/voicing_generator.h"

#include <algorithm>

#include "track/chord/bass_coordination.h"

namespace midisketch {
namespace chord_voicing {

int voicingDistance(const VoicedChord& prev, const VoicedChord& next) {
  int total = 0;
  size_t min_count = std::min(prev.count, next.count);
  for (size_t i = 0; i < min_count; ++i) {
    int diff = std::abs(static_cast<int>(next.pitches[i]) - static_cast<int>(prev.pitches[i]));
    // Weight bass (i=0) and soprano (i=min_count-1) 2x
    int weight = (i == 0 || i == min_count - 1) ? 2 : 1;
    total += diff * weight;
  }
  return total;
}

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

bool hasParallelFifthsOrOctaves(const VoicedChord& prev, const VoicedChord& curr) {
  size_t count = std::min(prev.count, curr.count);
  if (count < 2) return false;

  for (size_t i = 0; i < count; ++i) {
    for (size_t j = i + 1; j < count; ++j) {
      // Calculate intervals (mod 12 for octave equivalence)
      int prev_interval =
          std::abs(static_cast<int>(prev.pitches[i]) - static_cast<int>(prev.pitches[j])) % 12;
      int next_interval =
          std::abs(static_cast<int>(curr.pitches[i]) - static_cast<int>(curr.pitches[j])) % 12;

      // Check for P5 (7 semitones) or P8/unison (0 semitones)
      bool prev_is_perfect = (prev_interval == 7 || prev_interval == 0);
      bool next_is_perfect = (next_interval == 7 || next_interval == 0);

      if (prev_is_perfect && next_is_perfect && prev_interval == next_interval) {
        // Both intervals are the same perfect interval
        // Check if both voices move in the same direction (parallel motion)
        int motion_i = static_cast<int>(curr.pitches[i]) - static_cast<int>(prev.pitches[i]);
        int motion_j = static_cast<int>(curr.pitches[j]) - static_cast<int>(prev.pitches[j]);

        // Parallel motion: both move same direction (and not stationary)
        if (motion_i != 0 && motion_j != 0 && ((motion_i > 0) == (motion_j > 0))) {
          return true;
        }
      }
    }
  }
  return false;
}

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
    int fifth_pitch = root_pitch + 7 + 12;                   // 5th up an octave
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

std::vector<VoicedChord> generateRootlessVoicings(uint8_t root, const Chord& chord,
                                                   uint16_t bass_pitch_mask) {
  std::vector<VoicedChord> voicings;

  // Rootless voicing: omit root, use 3rd + 5th + 7th + optional 9th
  // Key principle: avoid notes that clash with bass (minor 2nd / major 7th)
  for (uint8_t base_octave = CHORD_LOW; base_octave <= CHORD_HIGH - 12; base_octave += 12) {
    VoicedChord v{};
    v.type = VoicingType::Rootless;

    bool is_minor = (chord.note_count >= 2 && chord.intervals[1] == 3);
    bool is_dominant =
        (chord.note_count >= 4 && chord.intervals[3] == 10 && chord.intervals[1] == 4);
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
      if (bass_pitch_mask != 0) {
        int ninth_pc = (root_pc + 2) % 12;
        if (clashesWithBassMask(ninth_pc, bass_pitch_mask)) {
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
      if (bass_pitch_mask != 0) {
        int m7_pc = (root_pc + 11) % 12;
        if (!clashesWithBassMask(m7_pc, bass_pitch_mask)) {
          seventh = 11;  // M7 is safe, use it for richer sound
        }
        // Check 9th clash
        int ninth_pc = (root_pc + 2) % 12;
        if (clashesWithBassMask(ninth_pc, bass_pitch_mask)) {
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
      // Place note in base_octave, with higher octave for extensions >= 12
      int octave_offset = (intervals_rootless[i] >= 12) ? 12 : 0;
      pitch = base_octave + octave_offset + (pitch % 12);

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
      if (bass_pitch_mask != 0 && clashesWithBassMask(pitch % 12, bass_pitch_mask)) {
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

std::vector<VoicedChord> generateVoicings(uint8_t root, const Chord& chord,
                                          VoicingType preferred_type,
                                          uint16_t bass_pitch_mask,
                                          OpenVoicingType open_subtype) {
  std::vector<VoicedChord> voicings;

  // Always include close voicings as fallback
  auto close = generateCloseVoicings(root, chord);
  voicings.insert(voicings.end(), close.begin(), close.end());

  if (preferred_type == VoicingType::Open) {
    // Generate requested open voicing subtype
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
    auto rootless = generateRootlessVoicings(root, chord, bass_pitch_mask);
    voicings.insert(voicings.end(), rootless.begin(), rootless.end());
  }

  return voicings;
}

}  // namespace chord_voicing
}  // namespace midisketch
