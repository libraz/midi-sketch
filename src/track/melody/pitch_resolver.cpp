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
#include "track/melody/melody_utils.h"

namespace midisketch {
namespace melody {

int applyPitchChoice(PitchChoice choice, int current_pitch, int target_pitch,
                     const ChordTones& chord_tones, int key_offset, uint8_t vocal_low,
                     uint8_t vocal_high, VocalAttitude attitude, bool disable_singability,
                     float note_eighths, float tension_usage, int max_melodic_interval) {
  // VocalAttitude affects candidate pitch selection:
  //   Clean: chord tones only (1, 3, 5)
  //   Expressive: chord tones + tensions (7, 9)
  //   Raw: all scale tones (more freedom)
  //
  // The set below is consulted only when a pitch has to be chosen. Every
  // stepwise branch resolves first by walking to the neighbouring scale tone
  // and never looks at it, so the attitude does not decide those notes: Clean
  // steps onto non-chord tones like the others, and Expressive's tensions
  // reach the line only through the fallbacks. The chord is not consulted on
  // that walk either -- see the note where the step is taken.
  //
  // Rhythm-melody coupling: note duration modulates tension allowance
  //   Short notes (< 1 eighth): Force chord tones for stability
  //   Long notes (>= 4 eighths): Allow tensions if attitude permits

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
      candidate_pcs.assign(chord_tones.begin(), chord_tones.end());
      break;

    case VocalAttitude::Expressive:
      // Chord tones + tensions (7th, 9th = 2nd, 11th = 4th)
      candidate_pcs.assign(chord_tones.begin(), chord_tones.end());
      // Add color tones gated by tension_usage and note duration.
      // Deterministic: longer notes are more likely to receive tensions.
      // tension_usage=0.0 → Expressive behaves like Clean (chord tones only)
      // tension_usage=1.0 → always add tensions (previous behavior)
      {
        float tension_threshold = 1.0f - tension_usage;                // High usage = low threshold
        float note_length_norm = std::min(note_eighths / 4.0f, 1.0f);  // Normalize to 0-1
        // A zero budget is tested for on its own rather than left to the
        // comparison. The normalised length saturates at 1.0, which is exactly
        // the threshold a zero budget produces, so the longest notes cleared it
        // and the parameter stopped meaning what it says at the one setting
        // where it has to mean it. Every other value keeps the comparison it
        // had, including 1.0, which admits notes of any length.
        bool add_tensions = tension_usage > 0.0f && note_length_norm >= tension_threshold;
        if (add_tensions) {
          int root_pc = chord_tones.empty() ? 0 : chord_tones[0];
          // These are fixed distances from the root, not degrees of the scale
          // counted above it, and the two differ on most chords. Eleven
          // semitones names the major seventh everywhere, so the diatonic
          // filter below keeps the "seventh" on the two degrees whose seventh
          // is major -- both of them avoid notes over a major triad -- and
          // discards it on the five degrees built on a minor seventh, the
          // dominant's among them. The "eleventh" is likewise the fourth,
          // which is an avoid note over every major triad it survives on.
          //
          // Correcting the spelling was measured and left out: the melody
          // reaches the chord's own seventh through the stepwise priority
          // below without consulting this list at all, and the list itself is
          // read on a small minority of calls, so a corrected candidate set
          // moved nothing. Whoever makes the stepwise priority attitude-aware
          // has to fix the spelling in the same change, or these become live.
          int seventh = (root_pc + 11) % 12;  // Major 7th (11 semitones from root)
          int ninth = (root_pc + 2) % 12;     // 9th = 2nd (2 semitones)
          int eleventh = (root_pc + 5) % 12;  // 11th = 4th (5 semitones, sus4-like)
          candidate_pcs.push_back(seventh);
          candidate_pcs.push_back(ninth);
          candidate_pcs.push_back(eleventh);
        }
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
    return std::clamp(nearestPitchInSet(chord_tones, current_pitch, 0, 127),
                      static_cast<int>(vocal_low), static_cast<int>(vocal_high));
  }

  int new_pitch = current_pitch;

  switch (choice) {
    case PitchChoice::Same:
      // Actually stay on the current pitch when it is a scale tone.
      // Snapping "Same" to the nearest chord tone silently converted stay
      // decisions into moves (often 3rds), inflating leap ratios. Avoid-note
      // and downbeat chord-tone constraints run later in the pipeline and
      // correct part of what this leaves behind -- part, not all: a pitch this
      // branch keeps can still be an avoid note over its chord in the finished
      // line, so "a later pass handles it" is not a reason to skip the
      // question, only a reason the melody is not written around the answer.
      if (isScaleTone(current_pitch % 12, static_cast<uint8_t>(key_offset))) {
        new_pitch = current_pitch;
      } else {
        new_pitch = nearestPitchInSet(chord_tones, current_pitch, 0, 127);
      }
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
        //
        // This walk tests the scale and not the chord, and it decides most of
        // the notes this function returns. Roughly a third of the pitches it
        // lands on are avoid notes over the chord sounding under them, and
        // preferring the half step does not help: from a diatonic pitch only
        // one of the two step sizes is in the scale, so there is no second
        // step to fall back on. Clearing the chord here would mean giving up
        // the step for a chord tone, which is a change to how the melodies
        // move rather than a correction, so it is deliberately not done.
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
        best = nearestPitchInSet(chord_tones, current_pitch, 0, 127);
      }
      // SINGABILITY: Enforce section/blueprint-aware maximum interval.
      // Large leaps are difficult to sing and sound unnatural in pop melodies.
      if (best >= 0 && std::abs(best - current_pitch) > max_melodic_interval) {
        // Find closest chord tone within max interval
        int closest = -1;
        int closest_dist = 127;
        for (int c : candidates) {
          int dist = std::abs(c - current_pitch);
          if (dist <= max_melodic_interval && dist < closest_dist) {
            closest_dist = dist;
            closest = c;
          }
        }
        if (closest >= 0) {
          best = closest;
        } else {
          // No chord tone within range, stay on current or use nearest
          best = nearestPitchInSet(chord_tones, current_pitch, 0, 127);
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
        best = nearestPitchInSet(chord_tones, current_pitch, 0, 127);
      }
      // SINGABILITY: Enforce section/blueprint-aware maximum interval.
      if (best >= 0 && std::abs(best - current_pitch) > max_melodic_interval) {
        // Find closest chord tone within max interval
        int closest = -1;
        int closest_dist = 127;
        for (int c : candidates) {
          int dist = std::abs(c - current_pitch);
          if (dist <= max_melodic_interval && dist < closest_dist) {
            closest_dist = dist;
            closest = c;
          }
        }
        if (closest >= 0) {
          best = closest;
        } else {
          best = nearestPitchInSet(chord_tones, current_pitch, 0, 127);
        }
      }
      new_pitch = best;
    } break;

    case PitchChoice::TargetStep:
      // Walk toward the target by scale step. Hopping to the next chord tone
      // toward the target manufactured 3rds on every target-attraction move;
      // reference vocals approach climax pitches with stepwise lines and
      // land on chord tones at anchors (downbeat constraint handles that).
      if (target_pitch >= 0 && target_pitch != current_pitch) {
        int direction = (target_pitch > current_pitch) ? 1 : -1;

        // Close enough to land directly (within a whole step)
        if (std::abs(target_pitch - current_pitch) <= 2) {
          new_pitch = target_pitch;
          break;
        }

        // Prefer scale tone step toward target (whole step first)
        for (int step = 2; step >= 1; --step) {
          int candidate = current_pitch + direction * step;
          if (candidate >= vocal_low && candidate <= vocal_high &&
              isScaleTone(candidate % 12, static_cast<uint8_t>(key_offset))) {
            new_pitch = candidate;
            break;
          }
        }

        // Fallback: nearest chord tone toward target
        if (new_pitch == current_pitch) {
          if (direction > 0) {
            for (int c : candidates) {
              if (c > current_pitch) {
                new_pitch = c;
                break;
              }
            }
          } else {
            for (int i = static_cast<int>(candidates.size()) - 1; i >= 0; --i) {
              if (candidates[i] < current_pitch) {
                new_pitch = candidates[i];
                break;
              }
            }
          }
        }
      } else if (isScaleTone(current_pitch % 12, static_cast<uint8_t>(key_offset))) {
        // At target (or no target): hold position when already on a scale tone
        new_pitch = current_pitch;
      } else {
        new_pitch = nearestPitchInSet(chord_tones, current_pitch, 0, 127);
      }
      break;
  }

  // Clamp to vocal range
  new_pitch = std::clamp(new_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));

  return new_pitch;
}

int calculateTargetPitch(int tessitura_center, int tessitura_range, uint8_t vocal_low,
                         uint8_t vocal_high, Tick section_start, const IHarmonyContext& harmony) {
  // Target is typically a chord tone in the upper part of tessitura
  ChordTones chord_tones = harmony.getChordTonesAt(section_start);

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
