/**
 * @file drum_constants.h
 * @brief Common constants and utilities for drum generation.
 */

#ifndef MIDISKETCH_TRACK_DRUMS_DRUM_CONSTANTS_H
#define MIDISKETCH_TRACK_DRUMS_DRUM_CONSTANTS_H

#include <algorithm>
#include <random>

#include "core/midi_track.h"
#include "core/rng_util.h"
#include "core/note_source.h"
#include "core/section_types.h"
#include "core/timing_constants.h"
#include "core/types.h"

namespace midisketch {
namespace drums {

// ============================================================================
// GM Drum Map Constants
// ============================================================================

constexpr uint8_t BD = 36;          ///< Bass Drum
constexpr uint8_t SD = 38;          ///< Snare Drum
constexpr uint8_t SIDESTICK = 37;   ///< Side Stick
constexpr uint8_t HANDCLAP = 39;    ///< Hand Clap
constexpr uint8_t CHH = 42;         ///< Closed Hi-Hat
constexpr uint8_t FHH = 44;         ///< Foot Hi-Hat (pedal)
constexpr uint8_t OHH = 46;         ///< Open Hi-Hat
constexpr uint8_t CRASH = 49;       ///< Crash Cymbal
constexpr uint8_t RIDE = 51;        ///< Ride Cymbal
constexpr uint8_t TAMBOURINE = 54;  ///< Tambourine
constexpr uint8_t TOM_H = 50;       ///< High Tom
constexpr uint8_t TOM_M = 47;       ///< Mid Tom
constexpr uint8_t TOM_L = 45;       ///< Low Tom
constexpr uint8_t SHAKER = 70;      ///< Maracas/Shaker

// ============================================================================
// Timing Aliases
// ============================================================================

constexpr Tick EIGHTH = TICK_EIGHTH;
constexpr Tick SIXTEENTH = TICK_SIXTEENTH;

// ============================================================================
// Humanization Constants
// ============================================================================

/// Kick humanization: +/-2% timing variation for natural feel
constexpr float KICK_HUMANIZE_AMOUNT = 0.02f;

// ============================================================================
// Utility Functions
// ============================================================================

/// @brief Add a drum note to track with provenance tracking.
inline void addDrumNote(MidiTrack& track, Tick start, Tick duration, uint8_t note,
                        uint8_t velocity) {
  NoteEvent event;
  event.start_tick = start;
  event.duration = duration;
  event.note = note;
  event.velocity = velocity;
#ifdef MIDISKETCH_NOTE_PROVENANCE
  event.prov_source = static_cast<uint8_t>(NoteSource::Drums);
  event.prov_lookup_tick = start;
  event.prov_chord_degree = -1;  // Drums don't have chord context
  event.prov_original_pitch = note;
#endif
  track.addNote(event);
}

/// @brief Add kick with humanization (timing micro-variation).
/// @param track Target MIDI track
/// @param tick Note start tick
/// @param duration Note duration
/// @param velocity Note velocity
/// @param rng Random number generator
/// @param humanize_amount Base humanization amount (default ±2%)
/// @param humanize_timing Global humanization scaling (0.0-1.0, scales the offset)
inline void addKickWithHumanize(MidiTrack& track, Tick tick, Tick duration, uint8_t velocity,
                                std::mt19937& rng,
                                float humanize_amount = KICK_HUMANIZE_AMOUNT,
                                float humanize_timing = 1.0f) {
  // Scale humanize_amount by humanize_timing for unified control
  float effective_amount = humanize_amount * std::clamp(humanize_timing, 0.0f, 1.0f);
  int max_offset = static_cast<int>(SIXTEENTH * effective_amount);

  Tick humanized_tick = tick;
  if (max_offset > 0) {
    int offset = rng_util::rollRange(rng, -max_offset, max_offset);
    humanized_tick = static_cast<Tick>(std::max(0, static_cast<int>(tick) + offset));
  }

  addDrumNote(track, humanized_tick, duration, BD, velocity);
}

// ============================================================================
// DrumRole Helper Functions
// ============================================================================

/// @brief Get kick probability based on DrumRole.
inline float getDrumRoleKickProbability(DrumRole role) {
  switch (role) {
    case DrumRole::Full:
      return 1.0f;
    case DrumRole::Ambient:
      return 0.25f;
    case DrumRole::Minimal:
    case DrumRole::FXOnly:
      return 0.0f;
  }
  return 1.0f;
}

/// @brief Get snare probability based on DrumRole.
inline float getDrumRoleSnareProbability(DrumRole role) {
  switch (role) {
    case DrumRole::Full:
      return 1.0f;
    case DrumRole::Ambient:
    case DrumRole::Minimal:
    case DrumRole::FXOnly:
      return 0.0f;
  }
  return 1.0f;
}

/// @brief Check if hi-hat should be played based on DrumRole.
inline bool shouldPlayHiHat(DrumRole role) {
  return role != DrumRole::FXOnly;
}

/// @brief Get preferred hi-hat instrument for DrumRole.
inline uint8_t getDrumRoleHiHatInstrument(DrumRole role, bool use_ride) {
  if (role == DrumRole::Ambient) {
    return RIDE;  // Ride is more atmospheric
  }
  return use_ride ? RIDE : CHH;
}

}  // namespace drums
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_DRUM_CONSTANTS_H
