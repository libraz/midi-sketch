#ifndef MIDISKETCH_CORE_PIANO_ROLL_SAFETY_H
#define MIDISKETCH_CORE_PIANO_ROLL_SAFETY_H

#include "core/types.h"
#include <cstdint>

namespace midisketch {

class Song;

// ============================================================================
// Collision Detection Types
// ============================================================================

// Collision severity with BGM notes.
enum class CollisionType : uint8_t {
  None,    // No collision
  Mild,    // Tritone (context-dependent)
  Severe   // Minor 2nd or Major 7th (always dissonant)
};

// Detailed collision result.
struct CollisionResult {
  CollisionType type = CollisionType::None;
  uint8_t interval = 0;         // Interval in semitones (1, 6, or 11)
  TrackRole track = TrackRole::Vocal;  // Which track caused collision
  uint8_t colliding_pitch = 0;  // The colliding note's pitch
};

// ============================================================================
// Collision Check Functions
// ============================================================================

// Check for BGM collision at a specific tick.
// Checks against Chord, Bass, Arpeggio, and Aux tracks.
// @param song The generated song containing all tracks
// @param tick Position to check
// @param pitch MIDI pitch to check for collision
// @returns CollisionResult with collision details
CollisionResult checkBgmCollisionDetailed(const Song& song, Tick tick,
                                          uint8_t pitch);

// Simple collision check returning only the type.
// @param song The generated song
// @param tick Position to check
// @param pitch MIDI pitch to check
// @returns CollisionType (None, Mild, or Severe)
CollisionType checkBgmCollision(const Song& song, Tick tick, uint8_t pitch);

// ============================================================================
// Key Detection Functions
// ============================================================================

// Get the current key at a specific tick, considering modulation.
// @param song The generated song (contains modulation info)
// @param tick Position to check
// @param base_key The base key before modulation (0-11, 0=C)
// @returns Current key (0-11) after applying modulation if applicable
uint8_t getCurrentKey(const Song& song, Tick tick, uint8_t base_key);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_PIANO_ROLL_SAFETY_H
