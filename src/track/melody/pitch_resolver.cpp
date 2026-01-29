/**
 * @file pitch_resolver.cpp
 * @brief Implementation of pitch resolution logic.
 */

#include "track/melody/pitch_resolver.h"

#include <algorithm>
#include <cmath>

#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/pitch_utils.h"

namespace midisketch {
namespace melody {

int applyPitchChoiceImpl(PitchChoice choice, int current_pitch, int target_pitch,
                         int8_t chord_degree, int key_offset, uint8_t vocal_low,
                         uint8_t vocal_high, VocalAttitude attitude, bool disable_singability,
                         float note_eighths) {
  // VocalAttitude affects candidate pitch selection:
  //   Clean: chord tones only (1, 3, 5)
  //   Expressive: chord tones + tensions (7, 9)
  //   Raw: all scale tones (more freedom)
  //
  // Rhythm-melody coupling: note duration modulates tension allowance
  //   Short notes (< 1 eighth): Force chord tones for stability
  //   Long notes (>= 4 eighths): Allow tensions if attitude permits

  // Get chord tones for current chord
  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);

  // Determine effective attitude based on note duration
  // Short notes should be more consonant (chord tones preferred)
  VocalAttitude effective_attitude = attitude;
  if (note_eighths < 1.0f && attitude != VocalAttitude::Clean) {
    // Short notes: downgrade to Clean for stability
    effective_attitude = VocalAttitude::Clean;
  }

  // Build candidate pitch classes based on VocalAttitude
  std::vector<int> candidate_pcs;
  switch (effective_attitude) {
    case VocalAttitude::Clean:
      // Chord tones only (safe, consonant)
      candidate_pcs = chord_tones;
      break;

    case VocalAttitude::Expressive:
      // Chord tones + tensions (7th, 9th = 2nd, 11th = 4th)
      candidate_pcs = chord_tones;
      // Add color tones for expressiveness
      {
        int root_pc = chord_tones.empty() ? 0 : chord_tones[0];
        int seventh = (root_pc + 11) % 12;   // Major 7th (11 semitones from root)
        int ninth = (root_pc + 2) % 12;      // 9th = 2nd (2 semitones)
        int eleventh = (root_pc + 5) % 12;   // 11th = 4th (5 semitones, sus4-like)
        candidate_pcs.push_back(seventh);
        candidate_pcs.push_back(ninth);
        candidate_pcs.push_back(eleventh);
      }
      break;

    case VocalAttitude::Raw:
      // All scale tones (C major: 0, 2, 4, 5, 7, 9, 11)
      candidate_pcs = {0, 2, 4, 5, 7, 9, 11};
      break;
  }

  // Build candidate pitches within vocal range
  std::vector<int> candidates;
  for (int pc : candidate_pcs) {
    // ABSOLUTE CONSTRAINT: Only allow scale tones
    // This prevents chromatic notes from VocalAttitude::Expressive tensions
    // that fall outside the scale (e.g., G# from Am7 in C major)
    if (!isScaleTone(pc, static_cast<uint8_t>(key_offset))) {
      continue;
    }

    // Check multiple octaves (4-6 covers typical vocal range)
    for (int oct = 4; oct <= 6; ++oct) {
      int candidate = oct * 12 + pc;
      if (candidate >= vocal_low && candidate <= vocal_high) {
        candidates.push_back(candidate);
      }
    }
  }

  // Sort candidates for easier searching
  std::sort(candidates.begin(), candidates.end());

  if (candidates.empty()) {
    // Fallback: use nearest chord tone to current pitch
    return std::clamp(nearestChordTonePitch(current_pitch, chord_degree),
                      static_cast<int>(vocal_low), static_cast<int>(vocal_high));
  }

  int new_pitch = current_pitch;

  switch (choice) {
    case PitchChoice::Same:
      // Stay on nearest chord tone to current pitch
      new_pitch = nearestChordTonePitch(current_pitch, chord_degree);
      break;

    case PitchChoice::StepUp: {
      int best = -1;
      // For machine-style vocals (UltraVocaloid), use chord-tone-first approach
      // to preserve rapid articulation patterns
      if (disable_singability) {
        // Find smallest chord tone above current pitch
        for (int c : candidates) {
          if (c > current_pitch) {
            best = c;
            break;  // Already sorted, first one above is smallest
          }
        }
      } else {
        // SINGABILITY: Prefer step motion while maintaining harmonic awareness
        // Priority order:
        //   1) Scale tone step (whole step > half step for consonance)
        //   2) Chord tone within small interval (M3 = 4 semitones)
        //   3) Any chord tone (fallback)
        // Note: Downbeat chord-tone constraint ensures strong beats are harmonically correct

        // Priority 1: Scale tone step (prefer whole step for more consonant motion)
        for (int step = 2; step >= 1; --step) {
          int candidate = current_pitch + step;
          if (candidate <= vocal_high &&
              isScaleTone(candidate % 12, static_cast<uint8_t>(key_offset))) {
            best = candidate;
            break;
          }
        }

        // Priority 2: Chord tone within small interval
        if (best < 0) {
          for (int c : candidates) {
            if (c > current_pitch && c - current_pitch <= 4) {
              best = c;
              break;
            }
          }
        }

        // Priority 3: Any chord tone (fallback)
        if (best < 0) {
          for (int c : candidates) {
            if (c > current_pitch) {
              best = c;
              break;
            }
          }
        }
      }
      if (best < 0) {
        // No chord tone above, use nearest
        best = nearestChordTonePitch(current_pitch, chord_degree);
      }
      // SINGABILITY: Enforce maximum interval constraint (major 6th = 9 semitones)
      // Large leaps are difficult to sing and sound unnatural in pop melodies
      constexpr int kMaxMelodicInterval = 9;
      if (best >= 0 && std::abs(best - current_pitch) > kMaxMelodicInterval) {
        // Find closest chord tone within max interval
        int closest = -1;
        int closest_dist = 127;
        for (int c : candidates) {
          int dist = std::abs(c - current_pitch);
          if (dist <= kMaxMelodicInterval && dist < closest_dist) {
            closest_dist = dist;
            closest = c;
          }
        }
        if (closest >= 0) {
          best = closest;
        } else {
          // No chord tone within range, stay on current or use nearest
          best = nearestChordTonePitch(current_pitch, chord_degree);
        }
      }
      new_pitch = best;
    } break;

    case PitchChoice::StepDown: {
      int best = -1;
      // For machine-style vocals (UltraVocaloid), use chord-tone-first approach
      if (disable_singability) {
        // Find largest chord tone below current pitch
        for (int i = static_cast<int>(candidates.size()) - 1; i >= 0; --i) {
          if (candidates[i] < current_pitch) {
            best = candidates[i];
            break;
          }
        }
      } else {
        // SINGABILITY: Prefer step motion while maintaining harmonic awareness
        // Same priority order as StepUp

        // Priority 1: Scale tone step (prefer whole step for more consonant motion)
        for (int step = 2; step >= 1; --step) {
          int candidate = current_pitch - step;
          if (candidate >= vocal_low &&
              isScaleTone(candidate % 12, static_cast<uint8_t>(key_offset))) {
            best = candidate;
            break;
          }
        }

        // Priority 2: Chord tone within small interval
        if (best < 0) {
          for (int i = static_cast<int>(candidates.size()) - 1; i >= 0; --i) {
            if (candidates[i] < current_pitch && current_pitch - candidates[i] <= 4) {
              best = candidates[i];
              break;
            }
          }
        }

        // Priority 3: Any chord tone (fallback)
        if (best < 0) {
          for (int i = static_cast<int>(candidates.size()) - 1; i >= 0; --i) {
            if (candidates[i] < current_pitch) {
              best = candidates[i];
              break;
            }
          }
        }
      }
      if (best < 0) {
        best = nearestChordTonePitch(current_pitch, chord_degree);
      }
      // SINGABILITY: Enforce maximum interval constraint (major 6th = 9 semitones)
      constexpr int kMaxMelodicInterval = 9;
      if (best >= 0 && std::abs(best - current_pitch) > kMaxMelodicInterval) {
        // Find closest chord tone within max interval
        int closest = -1;
        int closest_dist = 127;
        for (int c : candidates) {
          int dist = std::abs(c - current_pitch);
          if (dist <= kMaxMelodicInterval && dist < closest_dist) {
            closest_dist = dist;
            closest = c;
          }
        }
        if (closest >= 0) {
          best = closest;
        } else {
          best = nearestChordTonePitch(current_pitch, chord_degree);
        }
      }
      new_pitch = best;
    } break;

    case PitchChoice::TargetStep:
      // Move toward target, using nearest chord tone in that direction
      if (target_pitch >= 0) {
        if (target_pitch > current_pitch) {
          // Going up toward target: find first chord tone above current
          for (int c : candidates) {
            if (c > current_pitch && c <= target_pitch) {
              new_pitch = c;
              break;
            }
          }
          if (new_pitch == current_pitch) {
            // No suitable chord tone found, use nearest above
            for (int c : candidates) {
              if (c > current_pitch) {
                new_pitch = c;
                break;
              }
            }
          }
        } else if (target_pitch < current_pitch) {
          // Going down toward target: find first chord tone below current
          for (int i = static_cast<int>(candidates.size()) - 1; i >= 0; --i) {
            if (candidates[i] < current_pitch && candidates[i] >= target_pitch) {
              new_pitch = candidates[i];
              break;
            }
          }
          if (new_pitch == current_pitch) {
            // No suitable chord tone found, use nearest below
            for (int i = static_cast<int>(candidates.size()) - 1; i >= 0; --i) {
              if (candidates[i] < current_pitch) {
                new_pitch = candidates[i];
                break;
              }
            }
          }
        } else {
          // Already at target
          new_pitch = nearestChordTonePitch(current_pitch, chord_degree);
        }
      } else {
        new_pitch = nearestChordTonePitch(current_pitch, chord_degree);
      }
      break;
  }

  // Clamp to vocal range
  new_pitch = std::clamp(new_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));

  return new_pitch;
}

int calculateTargetPitchImpl([[maybe_unused]] const MelodyTemplate& tmpl, int tessitura_center,
                             int tessitura_range,
                             uint8_t vocal_low, uint8_t vocal_high, Tick section_start,
                             const IHarmonyContext& harmony) {
  // Target is typically a chord tone in the upper part of tessitura
  std::vector<int> chord_tones = harmony.getChordTonesAt(section_start);

  if (chord_tones.empty()) {
    return tessitura_center;
  }

  // Find chord tone nearest to upper tessitura
  int target_area = tessitura_center + tessitura_range / 2;
  int best_pitch = target_area;
  int best_dist = 100;

  for (int pc : chord_tones) {
    // Check multiple octaves
    for (int oct = 4; oct <= 6; ++oct) {
      int candidate = oct * 12 + pc;
      if (candidate < vocal_low || candidate > vocal_high) continue;

      int dist = std::abs(candidate - target_area);
      if (dist < best_dist) {
        best_dist = dist;
        best_pitch = candidate;
      }
    }
  }

  return best_pitch;
}

}  // namespace melody
}  // namespace midisketch
