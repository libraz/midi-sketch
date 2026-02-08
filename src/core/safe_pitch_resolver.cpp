/**
 * @file safe_pitch_resolver.cpp
 * @brief Implementation of safe pitch resolution.
 */

#include "core/safe_pitch_resolver.h"

#include <cmath>

#include "core/chord_progression_tracker.h"
#include "core/track_collision_detector.h"

namespace midisketch {

PitchResolutionResult SafePitchResolver::resolvePitchWithStrategy(
    uint8_t desired, Tick start, Tick duration, TrackRole track, uint8_t low, uint8_t high,
    const ChordProgressionTracker& chord_tracker,
    const TrackCollisionDetector& collision_detector) const {
  // If desired pitch is already safe, use it
  if (collision_detector.isConsonantWithOtherTracks(desired, start, duration, track, &chord_tracker)) {
    return {desired, CollisionAvoidStrategy::None};
  }

  int octave = desired / 12;
  int best_pitch = -1;
  int best_dist = 100;

  // Strategy 1: Try actual sounding pitches from chord/bass (doubling is safe)
  // This ensures we match the actual voicing, not just theoretical chord tones
  for (const auto& note : collision_detector.notes()) {
    if (note.track == track) continue;  // Skip same track
    if (note.track == TrackRole::Drums || note.track == TrackRole::SE) continue;

    // Check if this note is sounding at our time
    Tick end = start + duration;
    if (note.start < end && note.end > start) {
      // This note is sounding - try its pitch in different octaves
      int note_pc = note.pitch % 12;
      for (int oct_offset = -2; oct_offset <= 2; ++oct_offset) {
        int candidate = (octave + oct_offset) * 12 + note_pc;
        if (candidate < static_cast<int>(low) || candidate > static_cast<int>(high)) continue;
        if (!collision_detector.isConsonantWithOtherTracks(static_cast<uint8_t>(candidate), start, duration, track,
                                            &chord_tracker))
          continue;

        int dist = std::abs(candidate - static_cast<int>(desired));
        if (dist < best_dist) {
          best_dist = dist;
          best_pitch = candidate;
        }
      }
    }
  }

  if (best_pitch >= 0) {
    return {static_cast<uint8_t>(best_pitch), CollisionAvoidStrategy::ActualSounding};
  }

  // Strategy 2: Try theoretical chord tones
  auto chord_tones = chord_tracker.getChordTonesAt(start);
  for (int ct_pc : chord_tones) {
    for (int oct_offset = -2; oct_offset <= 2; ++oct_offset) {
      int candidate = (octave + oct_offset) * 12 + ct_pc;
      if (candidate < static_cast<int>(low) || candidate > static_cast<int>(high)) continue;
      if (!collision_detector.isConsonantWithOtherTracks(static_cast<uint8_t>(candidate), start, duration, track,
                                          &chord_tracker))
        continue;

      int dist = std::abs(candidate - static_cast<int>(desired));
      if (dist < best_dist) {
        best_dist = dist;
        best_pitch = candidate;
      }
    }
  }

  if (best_pitch >= 0) {
    return {static_cast<uint8_t>(best_pitch), CollisionAvoidStrategy::ChordTones};
  }

  // Strategy 3: Try any safe pitch nearby (prioritize small adjustments)
  // Order: consonant intervals first (3rds, 5ths, octaves), then others
  int adjustments[] = {3, -3, 4, -4, 5, -5, 7, -7, 12, -12, 2, -2, 1, -1};
  for (int adj : adjustments) {
    int candidate = static_cast<int>(desired) + adj;
    if (candidate < static_cast<int>(low) || candidate > static_cast<int>(high)) continue;
    if (collision_detector.isConsonantWithOtherTracks(static_cast<uint8_t>(candidate), start, duration, track,
                                       &chord_tracker)) {
      return {static_cast<uint8_t>(candidate), CollisionAvoidStrategy::ConsonantInterval};
    }
  }

  // Strategy 4: Exhaustive search in range
  for (int dist = 1; dist <= 24; ++dist) {
    for (int sign = -1; sign <= 1; sign += 2) {
      int candidate = static_cast<int>(desired) + sign * dist;
      if (candidate < static_cast<int>(low) || candidate > static_cast<int>(high)) continue;
      if (collision_detector.isConsonantWithOtherTracks(static_cast<uint8_t>(candidate), start, duration, track,
                                         &chord_tracker)) {
        return {static_cast<uint8_t>(candidate), CollisionAvoidStrategy::ExhaustiveSearch};
      }
    }
  }

  // Last resort: return original (clashing is better than invalid pitch)
  return {desired, CollisionAvoidStrategy::Failed};
}

}  // namespace midisketch
