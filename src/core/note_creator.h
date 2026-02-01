/**
 * @file note_creator.h
 * @brief Unified note creation API for all tracks (v2 Architecture).
 *
 * This is the single entry point for creating notes with harmony awareness.
 * All track generators should use createNote() instead of legacy APIs.
 * Exception: NoteEventBuilder is acceptable for Drums/SE/C API (no harmony needed).
 */

#ifndef MIDISKETCH_CORE_NOTE_CREATOR_H
#define MIDISKETCH_CORE_NOTE_CREATOR_H

#include <optional>
#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"

namespace midisketch {

class IHarmonyContext;
class MidiTrack;

/// @brief Options for createNote().
///
/// All parameters for note creation are specified here. Tracks set
/// PitchPreference to control how pitch resolution behaves.
struct NoteOptions {
  // Required parameters
  Tick start = 0;                               ///< Start tick
  Tick duration = 0;                            ///< Duration in ticks
  uint8_t desired_pitch = 60;                   ///< Desired MIDI pitch
  uint8_t velocity = 100;                       ///< MIDI velocity
  TrackRole role = TrackRole::Vocal;            ///< Track role

  // Pitch selection strategy
  PitchPreference preference = PitchPreference::Default;

  // Pitch range constraints
  uint8_t range_low = 0;                        ///< Minimum allowed pitch
  uint8_t range_high = 127;                     ///< Maximum allowed pitch

  // Optional flags
  bool record_provenance = true;                ///< Record source/chord info
  bool register_to_harmony = true;              ///< Register with HarmonyContext

  // Provenance
  NoteSource source = NoteSource::Unknown;      ///< Generation phase
  uint8_t original_pitch = 0;                   ///< Pre-adjustment pitch (0=use desired_pitch)

  // Additional context (track-specific)
  int8_t contour_direction = 0;                 ///< -1:descending, 0:none, +1:ascending (Motif)
};

/// @brief Result of createNote() with detailed information.
struct CreateNoteResult {
  std::optional<NoteEvent> note;                ///< Created note (nullopt if skipped)
  uint8_t final_pitch = 0;                      ///< Actual pitch used
  CollisionAvoidStrategy strategy_used;         ///< How collision was resolved
  bool was_adjusted = false;                    ///< True if pitch was changed
  bool was_registered = false;                  ///< True if registered to harmony

  CreateNoteResult()
      : strategy_used(CollisionAvoidStrategy::None) {}
};

// ============================================================================
// Main API: createNote()
// ============================================================================

/**
 * @brief Create a note with full harmony awareness.
 *
 * This is the primary note creation function. It handles:
 * - Collision detection with other tracks
 * - Pitch resolution based on PitchPreference
 * - Provenance recording
 * - Registration with HarmonyContext
 *
 * @param harmony Harmony context for collision detection and registration
 * @param opts Note creation options
 * @return Created note, or nullopt if SkipIfUnsafe and unsafe
 */
std::optional<NoteEvent> createNote(IHarmonyContext& harmony, const NoteOptions& opts);

/**
 * @brief Create a note and add it to a track.
 *
 * Convenience function that creates a note and adds it to the track.
 *
 * @param track Target track to add the note to
 * @param harmony Harmony context
 * @param opts Note creation options
 * @return Created note, or nullopt if skipped
 */
std::optional<NoteEvent> createNoteAndAdd(MidiTrack& track, IHarmonyContext& harmony,
                                           const NoteOptions& opts);

/**
 * @brief Create a note with detailed result information.
 *
 * Use this when you need to know exactly what happened during creation
 * (e.g., for debugging or conditional logic based on resolution strategy).
 *
 * @param harmony Harmony context
 * @param opts Note creation options
 * @return CreateNoteResult with full details
 */
CreateNoteResult createNoteWithResult(IHarmonyContext& harmony, const NoteOptions& opts);

// ============================================================================
// Drums/SE API: createNoteWithoutHarmony()
// ============================================================================

/**
 * @brief Create a note without harmony context.
 *
 * Use for Drums and SE tracks where no collision checking is needed.
 * Does not record provenance or register with harmony context.
 *
 * @param start Start tick
 * @param duration Duration in ticks
 * @param pitch MIDI pitch
 * @param velocity MIDI velocity
 * @return Created note
 */
NoteEvent createNoteWithoutHarmony(Tick start, Tick duration, uint8_t pitch, uint8_t velocity);

/**
 * @brief Create a note without harmony and add to track.
 *
 * @param track Target track
 * @param start Start tick
 * @param duration Duration in ticks
 * @param pitch MIDI pitch
 * @param velocity MIDI velocity
 * @return Created note
 */
NoteEvent createNoteWithoutHarmonyAndAdd(MidiTrack& track, Tick start, Tick duration,
                                          uint8_t pitch, uint8_t velocity);

// ============================================================================
// Candidate-based API (for advanced usage)
// ============================================================================

/**
 * @brief Get safe pitch candidates for a desired pitch.
 *
 * Returns multiple candidate pitches ranked by preference. Tracks can
 * use this for custom selection logic (e.g., preferring specific intervals).
 *
 * @param harmony Harmony context
 * @param desired_pitch Desired MIDI pitch
 * @param start Start tick
 * @param duration Duration in ticks
 * @param role Track role (for collision exclusion)
 * @param range_low Minimum pitch
 * @param range_high Maximum pitch
 * @param preference How to rank candidates
 * @param max_candidates Maximum candidates to return (default: 5)
 * @return Vector of PitchCandidate sorted by preference
 */
std::vector<PitchCandidate> getSafePitchCandidates(
    const IHarmonyContext& harmony,
    uint8_t desired_pitch,
    Tick start,
    Tick duration,
    TrackRole role,
    uint8_t range_low,
    uint8_t range_high,
    PitchPreference preference = PitchPreference::Default,
    size_t max_candidates = 5);

// ============================================================================
// Musical candidate selection (for melody generation)
// ============================================================================

/// @brief Hints for selecting the best candidate based on musical context.
struct PitchSelectionHints {
  int8_t prev_pitch = -1;          ///< Previous pitch (-1 = none)
  int8_t contour_direction = 0;    ///< -1:descending, 0:any, +1:ascending
  bool prefer_chord_tones = false; ///< Prioritize chord tone candidates
  bool prefer_small_intervals = true; ///< Prefer smaller intervals from prev_pitch
};

/**
 * @brief Select the best candidate based on musical context.
 *
 * This function provides musically-aware selection from a list of safe pitch
 * candidates. Use this instead of blindly selecting candidates[0] when:
 * - You have context about the previous pitch
 * - You want to maintain melodic contour
 * - You prefer chord tones for harmonic stability
 *
 * @param candidates List of safe pitch candidates (from getSafePitchCandidates)
 * @param fallback_pitch Pitch to return if candidates is empty
 * @param hints Musical context hints for selection
 * @return Best candidate pitch based on hints, or fallback if none available
 */
uint8_t selectBestCandidate(const std::vector<PitchCandidate>& candidates,
                             uint8_t fallback_pitch,
                             const PitchSelectionHints& hints = {});

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_NOTE_CREATOR_H
