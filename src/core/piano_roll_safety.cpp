/**
 * @file piano_roll_safety.cpp
 * @brief Implementation of collision detection.
 */

#include "core/piano_roll_safety.h"

#include <cstdlib>

#include "core/song.h"

namespace midisketch {

namespace {

// Check if interval creates a severe collision (minor 2nd or major 7th).
bool isSevereInterval(int interval) {
  int normalized = interval % 12;
  if (normalized < 0) normalized += 12;
  return normalized == 1 || normalized == 11;
}

// Check if interval is a tritone (6 semitones).
bool isTritone(int interval) {
  int normalized = interval % 12;
  if (normalized < 0) normalized += 12;
  return normalized == 6;
}

// Check collision with a single track.
CollisionResult checkTrackCollision(const MidiTrack& track, TrackRole role, Tick tick,
                                    uint8_t pitch) {
  CollisionResult result;

  for (const auto& note : track.notes()) {
    // Check if note is sounding at this tick
    if (note.start_tick <= tick && tick < note.start_tick + note.duration) {
      int interval = std::abs(static_cast<int>(pitch) - static_cast<int>(note.note));

      if (isSevereInterval(interval)) {
        result.type = CollisionType::Severe;
        result.interval = static_cast<uint8_t>(interval % 12);
        result.track = role;
        result.colliding_pitch = note.note;
        return result;  // Severe is worst, return immediately
      }

      if (isTritone(interval) && result.type == CollisionType::None) {
        result.type = CollisionType::Mild;
        result.interval = 6;
        result.track = role;
        result.colliding_pitch = note.note;
        // Don't return, keep checking for severe collisions
      }
    }
  }

  return result;
}

}  // namespace

CollisionResult checkBgmCollisionDetailed(const Song& song, Tick tick, uint8_t pitch) {
  CollisionResult worst_result;

  // Check tracks in order of typical musical importance
  // Chord and Bass are most important for harmonic foundation
  const TrackRole tracks_to_check[] = {TrackRole::Chord, TrackRole::Bass, TrackRole::Arpeggio,
                                       TrackRole::Aux, TrackRole::Motif};

  for (TrackRole role : tracks_to_check) {
    const MidiTrack& track = song.track(role);
    CollisionResult result = checkTrackCollision(track, role, tick, pitch);

    // Update worst result if this is worse
    if (result.type == CollisionType::Severe) {
      return result;  // Can't get worse than severe
    }
    if (result.type == CollisionType::Mild && worst_result.type == CollisionType::None) {
      worst_result = result;
    }
  }

  return worst_result;
}

CollisionType checkBgmCollision(const Song& song, Tick tick, uint8_t pitch) {
  return checkBgmCollisionDetailed(song, tick, pitch).type;
}

uint8_t getCurrentKey(const Song& song, Tick tick, uint8_t base_key) {
  Tick modulation_tick = song.modulationTick();
  int8_t modulation_amount = song.modulationAmount();

  // If no modulation or before modulation point, return base key
  if (modulation_tick == 0 || tick < modulation_tick) {
    return base_key;
  }

  // Apply modulation (wrap around 0-11)
  int new_key = (static_cast<int>(base_key) + modulation_amount) % 12;
  if (new_key < 0) new_key += 12;
  return static_cast<uint8_t>(new_key);
}

}  // namespace midisketch
