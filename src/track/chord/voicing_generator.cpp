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

/// @brief Whether the chord's own stacking places two tones a step apart.
///
/// Compares the intervals as written, not their pitch classes: a major seventh
/// is a pitch class away from the root but eleven semitones above it, and is
/// exactly the tone a close voicing must not fold down next to the root.
bool chordDefinesAdjacentPair(const Chord& chord) {
  for (uint8_t i = 0; i < chord.note_count; ++i) {
    if (chord.intervals[i] < 0) continue;
    for (uint8_t j = i + 1; j < chord.note_count; ++j) {
      if (chord.intervals[j] < 0) continue;
      int diff = std::abs(chord.intervals[i] - chord.intervals[j]);
      if (diff == 1 || diff == 2) return true;
    }
  }
  return false;
}

bool hasAdjacentSecond(const VoicedChord& voicing) {
  for (uint8_t i = 0; i < voicing.count; ++i) {
    for (uint8_t j = i + 1; j < voicing.count; ++j) {
      int diff =
          std::abs(static_cast<int>(voicing.pitches[i]) - static_cast<int>(voicing.pitches[j]));
      if (diff == 1 || diff == 2) return true;
    }
  }
  return false;
}

std::vector<VoicedChord> generateCloseVoicings(uint8_t root, const Chord& chord) {
  std::vector<VoicedChord> voicings;

  // A close voicing packs the chord into one octave, which for a seventh chord
  // puts two of its tones a whole step apart in the inner voices. A major second
  // between adjacent voices is dissonant in close position, so those inversions
  // are rejected unless the chord itself is built on that interval (sus2, add9).
  const bool cluster_is_the_chord = chordDefinesAdjacentPair(chord);

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

      if (valid && v.count >= 3 && (cluster_is_the_chord || !hasAdjacentSecond(v))) {
        voicings.push_back(v);
      }
    }
  }

  return voicings;
}

std::vector<VoicedChord> generateOpenVoicings(uint8_t root, const Chord& chord) {
  std::vector<VoicedChord> voicings;

  for (uint8_t base_octave = CHORD_LOW; base_octave <= CHORD_HIGH - 24; base_octave += 12) {
    VoicedChord v{};
    v.count = std::min(chord.note_count, static_cast<uint8_t>(v.pitches.size()));
    v.type = VoicingType::Open;
    v.open_subtype = OpenVoicingType::Drop2;

    bool valid = true;
    std::array<int, 5> raw_pitches{};

    // First, calculate every voice in close position.
    for (uint8_t i = 0; i < v.count; ++i) {
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

    if (v.count == 3) {
      // Preserve the existing triad spread: [root, 3rd+8va, 5th+8va].
      v.pitches[0] = static_cast<uint8_t>(raw_pitches[0]);
      v.pitches[1] = static_cast<uint8_t>(raw_pitches[1] + 12);
      v.pitches[2] = static_cast<uint8_t>(raw_pitches[2] + 12);
    } else if (v.count >= 4) {
      // Drop the second voice from the top by an octave. Copy every voice so
      // seventh and ninth chords remain genuine open voicings.
      for (uint8_t i = 0; i < v.count; ++i) {
        v.pitches[i] = static_cast<uint8_t>(raw_pitches[i]);
      }
      v.pitches[v.count - 2] = static_cast<uint8_t>(raw_pitches[v.count - 2] - 12);
    } else {
      valid = false;
    }

    if (valid) {
      std::sort(v.pitches.begin(), v.pitches.begin() + v.count);

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

    // Spread voicing: distribute across 2+ octaves.
    // Pattern: root in bass, chord-defined 5th in middle, 3rd+7th on top.
    int root_pitch = base_octave + (root % 12);
    int fifth_interval =
        (chord.note_count >= 3 && chord.intervals[2] >= 0) ? chord.intervals[2] : 7;
    int fifth_pitch = root_pitch + fifth_interval + 12;      // 5th up an octave
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

  // Rootless voicing: drop the root the bass is already holding and keep the
  // upper structure of the chord that was passed in. The intervals come from
  // the chord itself; deriving them from a major/minor/dominant guess put a
  // major third into a suspended chord and a seventh into a plain triad the
  // caller never asked for.
  for (uint8_t base_octave = CHORD_LOW; base_octave <= CHORD_HIGH - 12; base_octave += 12) {
    VoicedChord v{};
    v.type = VoicingType::Rootless;

    int root_pc = root % 12;

    std::array<int, 5> intervals_rootless{};
    int voice_count = 0;
    for (uint8_t i = 1; i < chord.note_count && voice_count < 5; ++i) {
      if (chord.intervals[i] < 0) break;
      int interval = chord.intervals[i];
      if (bass_pitch_mask != 0 &&
          clashesWithBassMask((root_pc + interval) % 12, bass_pitch_mask, root, chord)) {
        // A major seventh sits a semitone under the root the bass is holding.
        // Restating it as the major sixth keeps the chord's colour without the
        // clash; any other colliding voice is simply left out.
        if (interval == 11 &&
            !clashesWithBassMask((root_pc + 9) % 12, bass_pitch_mask, root, chord)) {
          interval = 9;
        } else {
          continue;
        }
      }
      intervals_rootless[voice_count++] = interval;
    }

    // A rootless triad is only two voices, which is not a chord. Add an upper
    // tone above it: the natural 9th, or the 11th when the 9th collides with
    // the bass or merely doubles a tone the chord already has.
    if (voice_count > 0 && voice_count < 3) {
      auto already_present = [&](int interval) {
        for (int i = 0; i < voice_count; ++i) {
          if (intervals_rootless[i] % 12 == interval % 12) return true;
        }
        return false;
      };
      int extension = 14;  // 9th
      if (already_present(extension) ||
          (bass_pitch_mask != 0 &&
           clashesWithBassMask((root_pc + 2) % 12, bass_pitch_mask, root, chord))) {
        extension = 17;  // 11th
      }
      if (!already_present(extension)) {
        intervals_rootless[voice_count++] = extension;
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
      if (bass_pitch_mask != 0 && clashesWithBassMask(pitch % 12, bass_pitch_mask, root, chord)) {
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
                                          VoicingType preferred_type, uint16_t bass_pitch_mask,
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
