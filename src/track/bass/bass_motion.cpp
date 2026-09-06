/**
 * @file bass_motion.cpp
 * @brief Implementation of vocal-aware bass pitch adjustment.
 */

#include "track/bass/bass_motion.h"

#include <cmath>
#include <optional>

#include "core/pitch_utils.h"

// Debug flag for bass transformation logging (set to 1 to enable)
#ifndef BASS_DEBUG_LOG
#define BASS_DEBUG_LOG 0
#endif

namespace midisketch {

namespace {

[[maybe_unused]] const char* motionTypeToString(MotionType motion) {
  switch (motion) {
    case MotionType::Contrary:
      return "Contrary";
    case MotionType::Similar:
      return "Similar";
    case MotionType::Parallel:
      return "Parallel";
    case MotionType::Oblique:
      return "Oblique";
  }
  return "Unknown";
}

// Check if bass pitch would form a minor 2nd (1 semitone) with vocal
bool wouldClashWithVocal(int bass_pitch, int vocal_pitch) {
  if (vocal_pitch <= 0) return false;  // No vocal sounding
  int interval = std::abs((bass_pitch % 12) - (vocal_pitch % 12));
  if (interval > 6) interval = 12 - interval;
  return interval == 1;  // Minor 2nd is a harsh clash
}

// Check if a pitch belongs to the given chord-tone set.
bool isPitchChordTone(int pitch, const ChordTones& chord_tones) {
  int pitch_class = ((pitch % 12) + 12) % 12;
  for (int ct : chord_tones) {
    if (ct == pitch_class) return true;
  }
  return false;
}

std::optional<int> nearestChordToneInDirection(int bass_pitch, int direction,
                                               const ChordTones& chord_tones, int vocal_pitch) {
  if (direction == 0) return std::nullopt;

  for (int distance = 1; distance <= Interval::OCTAVE; ++distance) {
    int candidate = bass_pitch + direction * distance;
    if (candidate < BASS_LOW || candidate > BASS_HIGH) continue;
    if (!isDiatonic(candidate)) continue;
    if (!isPitchChordTone(candidate, chord_tones)) continue;
    if (wouldClashWithVocal(candidate, vocal_pitch)) continue;
    return candidate;
  }

  return std::nullopt;
}

}  // namespace

uint8_t adjustPitchForMotion(uint8_t base_pitch, MotionType motion, int8_t vocal_direction,
                             uint8_t vocal_pitch, int8_t degree) {
  return adjustPitchForMotion(base_pitch, motion, vocal_direction, vocal_pitch,
                              getChordTones(degree));
}

// Adjust bass pitch based on Motion Type and vocal direction
// chord_tones constrains every adjustment to a tone of the chord that sounds here
uint8_t adjustPitchForMotion(uint8_t base_pitch, MotionType motion, int8_t vocal_direction,
                             uint8_t vocal_pitch, const ChordTones& chord_tones) {
  // Ensure 2+ octave separation (24 semitones) for doubling avoidance
  constexpr int kMinOctaveSeparation = 24;

  int bass_pitch = static_cast<int>(base_pitch);
  int v_pitch = static_cast<int>(vocal_pitch);
  [[maybe_unused]] int original_bass = bass_pitch;

  // Check pitch class conflict (same pitch class within 2 octaves)
  if (v_pitch > 0) {  // Only check if vocal is sounding
    int separation = std::abs(bass_pitch - v_pitch);
    if ((bass_pitch % 12) == (v_pitch % 12) && separation < kMinOctaveSeparation) {
      // Same pitch class, too close - adjust bass down an octave if possible
      if (bass_pitch - 12 >= BASS_LOW) {
#if BASS_DEBUG_LOG
        std::cerr << "    [vocal_avoid] same pitch class, -12: " << bass_pitch << " -> "
                  << (bass_pitch - 12) << "\n";
#endif
        bass_pitch -= 12;
      } else if (bass_pitch + 12 <= BASS_HIGH) {
#if BASS_DEBUG_LOG
        std::cerr << "    [vocal_avoid] same pitch class, +12: " << bass_pitch << " -> "
                  << (bass_pitch + 12) << "\n";
#endif
        bass_pitch += 12;
      }
    }
  }

  [[maybe_unused]] int after_vocal_avoid = bass_pitch;

  // Apply motion type adjustments - ONLY if result is diatonic AND doesn't clash with vocal
  int proposed_pitch = bass_pitch;
  switch (motion) {
    case MotionType::Contrary:
      // Move opposite to vocal direction
      if (vocal_direction > 0) {
        if (auto candidate = nearestChordToneInDirection(bass_pitch, -1, chord_tones, v_pitch)) {
          proposed_pitch = *candidate;  // Vocal going up, bass goes down
        }
      } else if (vocal_direction < 0) {
        if (auto candidate = nearestChordToneInDirection(bass_pitch, +1, chord_tones, v_pitch)) {
          proposed_pitch = *candidate;  // Vocal going down, bass goes up
        }
      }
      break;

    case MotionType::Similar:
      // Move same direction as vocal but different interval
      if (vocal_direction > 0) {
        if (auto candidate = nearestChordToneInDirection(bass_pitch, +1, chord_tones, v_pitch)) {
          proposed_pitch = *candidate;
        }
      } else if (vocal_direction < 0) {
        if (auto candidate = nearestChordToneInDirection(bass_pitch, -1, chord_tones, v_pitch)) {
          proposed_pitch = *candidate;
        }
      }
      break;

    case MotionType::Parallel:
    case MotionType::Oblique:
    default:
      // Parallel 5ths/Octaves: Classical music strictly forbids parallel perfect intervals
      // (P5, P8) between outer voices because they reduce perceived voice independence.
      // However, this project targets POP MUSIC where such "rules" are commonly violated:
      // - Power chords (P5 parallel motion) are a cornerstone of rock/pop
      // - Bass doubling melody at octave is standard in many genres
      // - Modern production actively uses parallel fifths for "thick" sound
      //
      // DESIGN DECISION: We do NOT detect or avoid parallel 5ths/octaves because:
      // 1. Pop music aesthetics differ from classical counterpoint
      // 2. False positives would overly constrain bass movement
      // 3. Other mechanisms (chord tone snapping, voice leading) provide sufficient
      //    harmonic coherence for pop style
      //
      // If classical counterpoint rules are needed in the future, add a configuration
      // option and implement parallel interval detection here.
      break;
  }

  // Only apply motion if result is diatonic, chord tone, AND doesn't clash with vocal
  // CRITICAL: Bass must stay on chord tones to define harmony correctly
  if (proposed_pitch != bass_pitch) {
    bool diatonic_ok = isDiatonic(proposed_pitch);
    bool chord_tone_ok = isPitchChordTone(proposed_pitch, chord_tones);
    bool vocal_ok = !wouldClashWithVocal(proposed_pitch, v_pitch);

    if (diatonic_ok && chord_tone_ok && vocal_ok) {
#if BASS_DEBUG_LOG
      std::cerr << "    [motion] " << motionTypeToString(motion) << ": " << bass_pitch << " -> "
                << proposed_pitch << " (diatonic OK, chord tone OK, vocal OK)\n";
#endif
      bass_pitch = proposed_pitch;
    } else {
#if BASS_DEBUG_LOG
      std::cerr << "    [motion] " << motionTypeToString(motion) << ": " << bass_pitch << " -> "
                << proposed_pitch << " REJECTED ("
                << (!diatonic_ok ? "non-diatonic"
                                 : (!chord_tone_ok ? "non-chord-tone" : "vocal clash"))
                << ")\n";
#endif
      // Keep original bass_pitch - motion adjustment rejected
    }
  }

  // Final check: if the current bass_pitch still clashes with vocal, try to fix it
  // CRITICAL: All alternatives must be chord tones to maintain harmonic integrity
  if (wouldClashWithVocal(bass_pitch, v_pitch)) {
    // Vocal priority: bass must yield, but only to chord tones
    // Try moving bass down by a whole step (more musical than half step)
    if (bass_pitch - 2 >= BASS_LOW && isDiatonic(bass_pitch - 2) &&
        isPitchChordTone(bass_pitch - 2, chord_tones) &&
        !wouldClashWithVocal(bass_pitch - 2, v_pitch)) {
#if BASS_DEBUG_LOG
      std::cerr << "    [vocal_priority] clash fix (chord tone): " << bass_pitch << " -> "
                << (bass_pitch - 2) << "\n";
#endif
      bass_pitch -= 2;
    }
    // Try moving up by a whole step
    else if (bass_pitch + 2 <= BASS_HIGH && isDiatonic(bass_pitch + 2) &&
             isPitchChordTone(bass_pitch + 2, chord_tones) &&
             !wouldClashWithVocal(bass_pitch + 2, v_pitch)) {
#if BASS_DEBUG_LOG
      std::cerr << "    [vocal_priority] clash fix (chord tone): " << bass_pitch << " -> "
                << (bass_pitch + 2) << "\n";
#endif
      bass_pitch += 2;
    }
    // Try octave down - always safe for same pitch class
    else if (bass_pitch - 12 >= BASS_LOW) {
#if BASS_DEBUG_LOG
      std::cerr << "    [vocal_priority] octave down: " << bass_pitch << " -> " << (bass_pitch - 12)
                << "\n";
#endif
      bass_pitch -= 12;
    }
  }

  // Final safety check: ensure result is diatonic to C major
  // If motion adjustments produced a non-diatonic pitch, revert to original
  if (!isDiatonic(bass_pitch)) {
#if BASS_DEBUG_LOG
    std::cerr << "    [final_check] non-diatonic " << bass_pitch << " -> reverting to "
              << base_pitch << "\n";
#endif
    bass_pitch = static_cast<int>(base_pitch);
  }

  return clampBass(bass_pitch);
}

}  // namespace midisketch
